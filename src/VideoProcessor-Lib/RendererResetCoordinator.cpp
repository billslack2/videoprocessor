#include <pch.h>

#include <RendererResetCoordinator.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include <IRenderer.h>

struct RendererResetCoordinator::State
{
	mutable std::mutex mutex;
	std::condition_variable workAvailable;
	WakeUi wakeUi;
	Clock clock;
	bool closed = false;
	bool stopWorker = false;
	bool wakeRetryRequested = false;
	bool wakePosted = false;
	RendererBindingToken currentToken = 0;
	uint32_t rendererGeneration = 0;
	RendererBindingToken nextToken = 0;
	uint64_t nextSequence = 0;
	bool hasPending = false;
	RendererResetRequest pending;
	bool hasPrepared = false;
	SelectedReset prepared;
	bool operationActive = false;
	uint64_t nextOperationId = 0;
	uint64_t activeOperationId = 0;
	std::shared_ptr<IVideoRenderer> operationRenderer;
	SelectedReset operationSelection;
	bool operationQueued = false;
	bool completionPending = false;
	OperationResult completion;
	bool restartCoverRequired = false;
	std::shared_ptr<RendererIngressState> ingress =
		std::make_shared<RendererIngressState>();
	uint64_t acceptedRequestCount = 0;
	uint64_t staleRequestCount = 0;
};


namespace
{
	int ResetScopeRank(RendererResetScope scope)
	{
		switch (scope)
		{
		case RendererResetScope::GraphRetarget: return 2;
		case RendererResetScope::Graph: return 1;
		default: return 0;
		}
	}

	void AttemptWake(
		const std::shared_ptr<RendererResetCoordinator::State>& state)
	{
		RendererResetCoordinator::WakeUi wake;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->closed || state->wakePosted ||
				(!state->hasPending && !state->completionPending))
			{
				return;
			}
			state->wakePosted = true;
			wake = state->wakeUi;
		}

		bool accepted = false;
		if (wake)
		{
			try
			{
				accepted = wake();
			}
			catch (...)
			{
			}
		}
		if (accepted)
			return;

		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->wakePosted = false;
			if (!state->closed &&
				(state->hasPending || state->completionPending))
			{
				state->wakeRetryRequested = true;
			}
		}
		state->workAvailable.notify_one();
	}


	RendererResetCoordinator::SubmissionReceipt SubmitBoundRequest(
		const std::shared_ptr<RendererResetCoordinator::State>& state,
		RendererBindingToken token,
		RendererResetRequest request) noexcept
	{
		RendererResetCoordinator::SubmissionReceipt receipt;
		bool wakeRequired = false;
		try
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->closed)
				return receipt;
			if (state->currentToken != token)
			{
				++state->staleRequestCount;
				receipt.disposition = RendererResetCoordinator::
					SubmissionDisposition::RejectedStaleBinding;
				return receipt;
			}

			const uint64_t now = state->clock ? state->clock() : 0;
			request.bindingToken = token;
			request.sequence = ++state->nextSequence;
			if (request.requestedTick == 0)
				request.requestedTick = now;
			if (request.deadlineTick == 0)
				request.deadlineTick = request.requestedTick;
			request.originContributors |= RendererResetOriginBit(
				request.origin);

			receipt.accepted = true;
			receipt.requestSequence = request.sequence;
			if (!state->hasPending)
			{
				state->pending = request;
				state->hasPending = true;
				receipt.disposition = RendererResetCoordinator::
					SubmissionDisposition::Selected;
			}
			else
			{
				RendererResetRequest& selected = state->pending;
				const RendererResetScope strongestScope =
					ResetScopeRank(request.scope) >
						ResetScopeRank(selected.scope) ?
						request.scope : selected.scope;
				const uintptr_t retargetWindow =
					request.scope == RendererResetScope::GraphRetarget ?
						request.targetWindow :
					selected.scope == RendererResetScope::GraphRetarget ?
						selected.targetWindow : 0;
				const bool replace = RendererResetShouldReplace(
					RendererResetPriority(request.reason),
					request.deadlineTick,
					RendererResetPriority(selected.reason),
					selected.deadlineTick);
				const RendererResetOriginContributors contributors =
					selected.originContributors |
					request.originContributors;
				if (replace)
				{
					selected = request;
					receipt.disposition = RendererResetCoordinator::
						SubmissionDisposition::Replaced;
				}
				else
				{
					receipt.disposition = RendererResetCoordinator::
						SubmissionDisposition::Coalesced;
				}
				selected.scope = strongestScope;
				selected.targetWindow = retargetWindow;
				selected.originContributors = contributors;
			}

			const RendererResetRequest& selected = state->pending;
			receipt.selectedSequence = selected.sequence;
			receipt.selectedReason = selected.reason;
			receipt.selectedScope = selected.scope;
			receipt.selectedOrigin = selected.origin;
			receipt.selectedOriginGeneration =
				selected.originGeneration;
			receipt.selectedOriginContributors =
				selected.originContributors;
			receipt.selectedDeadlineTick = selected.deadlineTick;

			++state->acceptedRequestCount;
			wakeRequired = !state->wakePosted;
		}
		catch (...)
		{
			return {};
		}

		if (wakeRequired)
			AttemptWake(state);
		return receipt;
	}


	class BoundRendererResetRequestSink final :
		public IRendererResetRequestSink
	{
	public:
		BoundRendererResetRequestSink(
			std::weak_ptr<RendererResetCoordinator::State> state,
			RendererBindingToken token):
			m_state(std::move(state)),
			m_token(token)
		{
		}

		void Submit(RendererResetRequest request) noexcept override
		{
			const std::shared_ptr<RendererResetCoordinator::State> state =
				m_state.lock();
			if (!state)
				return;
			SubmitBoundRequest(state, m_token, request);
		}

	private:
		std::weak_ptr<RendererResetCoordinator::State> m_state;
		RendererBindingToken m_token = 0;
	};


	uint64_t SaturatingAdd(uint64_t value, uint64_t increment)
	{
		const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
		return increment > maximum - value ? maximum : value + increment;
	}

}


RendererResetCoordinator::RendererResetCoordinator(WakeUi wakeUi, Clock clock):
	m_state(std::make_shared<State>())
{
	m_state->wakeUi = std::move(wakeUi);
	m_state->clock = std::move(clock);
	const std::shared_ptr<State> state = m_state;
	m_worker = std::thread([state]()
		{
			const HRESULT coInitializeResult =
				CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			for (;;)
			{
				std::shared_ptr<IVideoRenderer> renderer;
				SelectedReset selection;
				uint64_t operationId = 0;
				bool retryWake = false;
				{
					std::unique_lock<std::mutex> lock(state->mutex);
					state->workAvailable.wait(lock, [state]()
						{
							return state->stopWorker ||
								state->operationQueued ||
								state->wakeRetryRequested;
						});
					if (state->stopWorker && !state->operationQueued)
						break;
					if (state->wakeRetryRequested)
					{
						state->wakeRetryRequested = false;
						retryWake = true;
					}
					if (state->operationQueued)
					{
						state->operationQueued = false;
						renderer = state->operationRenderer;
						selection = state->operationSelection;
						operationId = state->activeOperationId;
					}
				}

				if (retryWake)
				{
					std::this_thread::sleep_for(
						std::chrono::milliseconds(10));
					AttemptWake(state);
				}

				if (!renderer)
					continue;

				bool succeeded = false;
				std::string failure;
				try
				{
					if (selection.request.scope ==
						RendererResetScope::GraphRetarget)
					{
						if (selection.request.targetWindow == 0 ||
							!renderer->RetargetWindowWithIngressDrain(
								selection.request.targetWindow,
								[state]()
								{
									state->ingress->WaitForDrain();
								}))
						{
							throw std::runtime_error(
								"renderer window retarget unsupported or failed");
						}
					}
					else if (selection.request.scope ==
						RendererResetScope::Graph)
					{
						renderer->ResetWithIngressDrain([state]()
							{
								state->ingress->WaitForDrain();
							});
					}
					else
					{
						renderer->ResetLiveQueue();
						state->ingress->WaitForDrain();
					}
					succeeded = true;
				}
				catch (const std::exception& ex)
				{
					failure = ex.what();
				}
				catch (...)
				{
					failure = "unknown exception";
				}

				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->operationRenderer.reset();
					state->operationActive = false;
					state->completionPending = true;
					state->completion = {};
					state->completion.operationId = operationId;
					state->completion.request = selection.request;
					state->completion.rendererGeneration =
						selection.rendererGeneration;
					state->completion.transitionToken =
						selection.transitionToken;
					state->completion.targetRevision =
						selection.targetRevision;
					state->completion.succeeded = succeeded;
					state->completion.failure = std::move(failure);
					if (!succeeded)
					{
						state->completion.restartRequired = true;
						state->restartCoverRequired = true;
					}
					state->wakePosted = false;
				}
				AttemptWake(state);
			}
			if (SUCCEEDED(coInitializeResult))
				CoUninitialize();
		});
}


RendererResetCoordinator::~RendererResetCoordinator()
{
	Close();
	Join();
}


RendererResetCoordinator::Binding RendererResetCoordinator::Bind(
	uint32_t rendererGeneration)
{
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (m_state->closed)
		return {};

	RendererBindingToken token = ++m_state->nextToken;
	if (token == 0)
		token = ++m_state->nextToken;
	m_state->currentToken = token;
	m_state->rendererGeneration = rendererGeneration;
	m_state->hasPending = false;
	m_state->pending = {};
	m_state->hasPrepared = false;
	m_state->prepared = {};
	m_state->restartCoverRequired = false;
	m_state->wakePosted = false;

	Binding binding;
	binding.token = token;
	binding.rendererGeneration = rendererGeneration;
	binding.sink = std::make_shared<BoundRendererResetRequestSink>(
		std::weak_ptr<State>(m_state), token);
	return binding;
}


void RendererResetCoordinator::Revoke(RendererBindingToken token) noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (token == 0 || m_state->currentToken != token)
			return;
		m_state->currentToken = 0;
		m_state->rendererGeneration = 0;
		m_state->hasPending = false;
		m_state->pending = {};
		m_state->hasPrepared = false;
		m_state->prepared = {};
		m_state->restartCoverRequired = false;
		m_state->wakePosted = false;
	}
	catch (...)
	{
	}
}


void RendererResetCoordinator::Close() noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->closed = true;
		m_state->currentToken = 0;
		m_state->rendererGeneration = 0;
		m_state->hasPending = false;
		m_state->pending = {};
		m_state->hasPrepared = false;
		m_state->prepared = {};
		m_state->wakePosted = false;
		m_state->wakeUi = {};
		m_state->wakeRetryRequested = false;
		if (m_state->operationQueued)
		{
			// Shutdown cancels work that has not entered renderer code. Once
			// the worker has taken the renderer, it owns that call to terminal
			// completion and Join waits for it.
			m_state->operationQueued = false;
			m_state->operationRenderer.reset();
			m_state->operationSelection = {};
			m_state->operationActive = false;
			m_state->activeOperationId = 0;
		}
		m_state->stopWorker = true;
		m_state->ingress->CloseAdmission();
	}
	catch (...)
	{
	}
	m_state->workAvailable.notify_all();
}


void RendererResetCoordinator::Join() noexcept
{
	Close();
	try
	{
		if (m_worker.joinable())
		{
			HANDLE workerHandle = m_worker.native_handle();
			for (;;)
			{
				const DWORD waitResult = MsgWaitForMultipleObjectsEx(
					1, &workerHandle, INFINITE, QS_SENDMESSAGE,
					MWMO_INPUTAVAILABLE);
				if (waitResult == WAIT_OBJECT_0)
					break;
				if (waitResult != WAIT_OBJECT_0 + 1)
					break;
				MSG message;
				PeekMessage(
					&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
			}
			m_worker.join();
		}
	}
	catch (...)
	{
	}
}


bool RendererResetCoordinator::RequestUi(
	RendererResetReason reason,
	RendererResetScope scope,
	uint64_t delayMs,
	uint64_t backendEpoch,
	uintptr_t targetWindow) noexcept
{
	return RequestUiWithReceipt(
		reason, scope, delayMs, backendEpoch, targetWindow).accepted;
}


RendererResetCoordinator::SubmissionReceipt
RendererResetCoordinator::RequestUiWithReceipt(
	RendererResetReason reason,
	RendererResetScope scope,
	uint64_t delayMs,
	uint64_t backendEpoch,
	uintptr_t targetWindow,
	RendererResetOrigin origin,
	uint64_t originGeneration) noexcept
{
	RendererBindingToken token = 0;
	uint64_t now = 0;
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_state->mutex);
			if (m_state->closed || m_state->currentToken == 0)
				return {};
			now = m_state->clock ? m_state->clock() : 0;
			token = m_state->currentToken;
		}
		catch (...)
		{
			return {};
		}
	}

	RendererResetRequest request;
	request.backendEpoch = backendEpoch;
	request.reason = reason;
	request.scope = scope;
	request.origin = origin;
	request.originGeneration = originGeneration;
	request.targetWindow = targetWindow;
	request.requestedTick = now;
	request.deadlineTick = SaturatingAdd(now, delayMs);
	return SubmitBoundRequest(m_state, token, request);
}


bool RendererResetCoordinator::DrainReady(
	uint64_t now,
	SelectedReset& selected) noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->wakePosted = false;
		if (m_state->closed ||
			m_state->currentToken == 0 ||
			m_state->hasPrepared ||
			m_state->operationActive ||
			m_state->completionPending ||
			!m_state->hasPending ||
			m_state->pending.bindingToken != m_state->currentToken ||
			m_state->pending.deadlineTick > now)
		{
			return false;
		}

		selected.request = m_state->pending;
		selected.rendererGeneration = m_state->rendererGeneration;
		m_state->hasPending = false;
		m_state->pending = {};
		m_state->hasPrepared = true;
		m_state->prepared = selected;
		return true;
	}
	catch (...)
	{
		return false;
	}
}


RendererResetCoordinator::StartResult
RendererResetCoordinator::AcknowledgeBlackAndStart(
	const SelectedReset& selected,
	std::shared_ptr<IVideoRenderer> renderer) noexcept
{
	if (!renderer)
		return StartResult::InvalidRenderer;

	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (m_state->closed)
			return StartResult::Closed;
		if (m_state->operationActive || m_state->completionPending)
			return StartResult::OperationActive;
		if (!m_state->hasPrepared ||
			m_state->prepared.request.bindingToken !=
				selected.request.bindingToken ||
			m_state->prepared.request.sequence != selected.request.sequence ||
			m_state->prepared.rendererGeneration !=
				selected.rendererGeneration ||
			selected.request.bindingToken != m_state->currentToken ||
			selected.rendererGeneration != m_state->rendererGeneration)
		{
			return StartResult::StaleSelection;
		}

		m_state->ingress->CloseAdmission();
		m_state->hasPrepared = false;
		m_state->prepared = {};
		m_state->operationActive = true;
		m_state->activeOperationId = ++m_state->nextOperationId;
		m_state->operationRenderer = std::move(renderer);
		m_state->operationSelection = selected;
		m_state->operationQueued = true;
		m_state->workAvailable.notify_one();
		return StartResult::Started;
	}
	catch (...)
	{
		return StartResult::Closed;
	}
}


bool RendererResetCoordinator::RejectBlackAndRequireRestart(
	const SelectedReset& selected,
	const char* failure) noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		if (m_state->closed || !m_state->hasPrepared ||
			m_state->prepared.request.bindingToken !=
				selected.request.bindingToken ||
			m_state->prepared.request.sequence != selected.request.sequence)
		{
			return false;
		}

		m_state->hasPrepared = false;
		m_state->prepared = {};
		m_state->restartCoverRequired = true;
		m_state->completionPending = true;
		m_state->completion = {};
		m_state->completion.operationId = ++m_state->nextOperationId;
		m_state->completion.request = selected.request;
		m_state->completion.rendererGeneration =
			selected.rendererGeneration;
		m_state->completion.transitionToken =
			selected.transitionToken;
		m_state->completion.targetRevision =
			selected.targetRevision;
		m_state->completion.restartRequired = true;
		m_state->completion.failure =
			failure ? failure : "black cover unavailable";
		m_state->wakePosted = false;
	}
	catch (...)
	{
		return false;
	}
	AttemptWake(m_state);
	return true;
}


bool RendererResetCoordinator::ConsumeCompletion(
	uint32_t currentRendererGeneration,
	bool rendererUsable,
	OperationResult& result,
	uint64_t expectedTransitionToken,
	uint64_t expectedTargetRevision,
	bool transitionReadyForCompletion) noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->wakePosted = false;
		if (!m_state->completionPending)
			return false;

		result = m_state->completion;
		result.staleGeneration =
			result.rendererGeneration != currentRendererGeneration;
		result.staleBinding =
			result.request.bindingToken == 0 ||
			result.request.bindingToken != m_state->currentToken;
		result.restartRequired =
			result.restartRequired ||
			!result.succeeded ||
			result.staleGeneration ||
			result.staleBinding ||
			!rendererUsable ||
			!transitionReadyForCompletion ||
			result.transitionToken != expectedTransitionToken ||
			result.targetRevision != expectedTargetRevision;
		result.ingressReopened = !result.restartRequired;
		if (result.ingressReopened)
		{
			m_state->ingress->OpenAdmission();
			m_state->restartCoverRequired = false;
		}
		else
		{
			m_state->restartCoverRequired = true;
		}

		m_state->completionPending = false;
		m_state->completion = {};
		if (m_state->hasPending)
		{
			m_state->wakeRetryRequested = true;
			m_state->workAvailable.notify_one();
		}
		return true;
	}
	catch (...)
	{
		return false;
	}
}


std::shared_ptr<RendererIngressState>
RendererResetCoordinator::GetIngressState() const noexcept
{
	return m_state->ingress;
}


bool RendererResetCoordinator::BlocksReveal(
	uint32_t rendererGeneration) const noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return !m_state->closed &&
			m_state->currentToken != 0 &&
			m_state->rendererGeneration == rendererGeneration &&
			(m_state->hasPending ||
				m_state->hasPrepared ||
				(m_state->operationActive &&
					m_state->operationSelection.rendererGeneration ==
						rendererGeneration) ||
				(m_state->completionPending &&
					m_state->completion.rendererGeneration ==
						rendererGeneration) ||
				m_state->restartCoverRequired);
	}
	catch (...)
	{
		return false;
	}
}


bool RendererResetCoordinator::RequiresLifecycleDeferral() const noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		return m_state->hasPrepared ||
			m_state->operationActive ||
			m_state->completionPending;
	}
	catch (...)
	{
		return true;
	}
}


RendererResetCoordinator::Diagnostics
RendererResetCoordinator::GetDiagnostics() const noexcept
{
	Diagnostics diagnostics;
	try
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		diagnostics.closed = m_state->closed;
		diagnostics.hasBinding = m_state->currentToken != 0;
		diagnostics.hasPending = m_state->hasPending;
		diagnostics.wakePosted = m_state->wakePosted;
		diagnostics.bindingToken = m_state->currentToken;
		diagnostics.rendererGeneration = m_state->rendererGeneration;
		diagnostics.acceptedRequestCount = m_state->acceptedRequestCount;
		diagnostics.staleRequestCount = m_state->staleRequestCount;
		diagnostics.selectionPrepared = m_state->hasPrepared;
		diagnostics.operationActive = m_state->operationActive;
		diagnostics.completionPending = m_state->completionPending;
		diagnostics.restartCoverRequired =
			m_state->restartCoverRequired;
		diagnostics.activeOperationId = m_state->activeOperationId;
		diagnostics.ingressActiveLeases =
			m_state->ingress->ActiveLeases();
		diagnostics.ingressAdmitting =
			m_state->ingress->IsAdmitting();
		if (m_state->hasPending)
		{
			diagnostics.pendingReason = m_state->pending.reason;
			diagnostics.pendingScope = m_state->pending.scope;
			diagnostics.pendingOrigin = m_state->pending.origin;
			diagnostics.pendingOriginGeneration =
				m_state->pending.originGeneration;
			diagnostics.pendingOriginContributors =
				m_state->pending.originContributors;
			diagnostics.pendingDeadlineTick =
				m_state->pending.deadlineTick;
		}
	}
	catch (...)
	{
	}
	return diagnostics;
}
