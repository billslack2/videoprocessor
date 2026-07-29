#include <pch.h>

#include <RendererResetCoordinator.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>


struct RendererResetCoordinator::State
{
	mutable std::mutex mutex;
	WakeUi wakeUi;
	Clock clock;
	bool closed = false;
	bool wakePosted = false;
	RendererBindingToken currentToken = 0;
	uint32_t rendererGeneration = 0;
	RendererBindingToken nextToken = 0;
	uint64_t nextSequence = 0;
	bool hasPending = false;
	RendererResetRequest pending;
	uint64_t acceptedRequestCount = 0;
	uint64_t staleRequestCount = 0;
};


namespace
{
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

			RendererResetCoordinator::WakeUi wake;
			try
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				if (state->closed || state->currentToken != m_token)
				{
					++state->staleRequestCount;
					return;
				}

				const uint64_t now = state->clock ? state->clock() : 0;
				request.bindingToken = m_token;
				request.sequence = ++state->nextSequence;
				if (request.requestedTick == 0)
					request.requestedTick = now;
				if (request.deadlineTick == 0)
					request.deadlineTick = request.requestedTick;

				if (!state->hasPending)
				{
					state->pending = request;
					state->hasPending = true;
				}
				else
				{
					RendererResetRequest& selected = state->pending;
					const bool graphRequired =
						selected.scope == RendererResetScope::Graph ||
						request.scope == RendererResetScope::Graph;
					const bool replace = RendererResetShouldReplace(
						RendererResetPriority(request.reason),
						request.deadlineTick,
						RendererResetPriority(selected.reason),
						selected.deadlineTick);
					if (replace)
						selected = request;
					if (graphRequired)
						selected.scope = RendererResetScope::Graph;
				}

				++state->acceptedRequestCount;
				if (!state->wakePosted)
				{
					state->wakePosted = true;
					wake = state->wakeUi;
				}
			}
			catch (...)
			{
				return;
			}

			if (wake)
			{
				try
				{
					wake();
				}
				catch (...)
				{
					// Backend hot paths must never observe UI wake failures.
				}
			}
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
}


RendererResetCoordinator::~RendererResetCoordinator()
{
	Close();
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
		m_state->wakePosted = false;
		m_state->wakeUi = {};
	}
	catch (...)
	{
	}
}


bool RendererResetCoordinator::RequestUi(
	RendererResetReason reason,
	RendererResetScope scope,
	uint64_t delayMs,
	uint64_t backendEpoch) noexcept
{
	std::shared_ptr<IRendererResetRequestSink> sink;
	uint64_t now = 0;
	{
		try
		{
			std::lock_guard<std::mutex> lock(m_state->mutex);
			if (m_state->closed || m_state->currentToken == 0)
				return false;
			now = m_state->clock ? m_state->clock() : 0;
			sink = std::make_shared<BoundRendererResetRequestSink>(
				std::weak_ptr<State>(m_state), m_state->currentToken);
		}
		catch (...)
		{
			return false;
		}
	}

	RendererResetRequest request;
	request.backendEpoch = backendEpoch;
	request.reason = reason;
	request.scope = scope;
	request.requestedTick = now;
	request.deadlineTick = SaturatingAdd(now, delayMs);
	sink->Submit(request);
	return true;
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
		return true;
	}
	catch (...)
	{
		return false;
	}
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
			m_state->hasPending;
	}
	catch (...)
	{
		return false;
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
		if (m_state->hasPending)
		{
			diagnostics.pendingReason = m_state->pending.reason;
			diagnostics.pendingScope = m_state->pending.scope;
			diagnostics.pendingDeadlineTick =
				m_state->pending.deadlineTick;
		}
	}
	catch (...)
	{
	}
	return diagnostics;
}
