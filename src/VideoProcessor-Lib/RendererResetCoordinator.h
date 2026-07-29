#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <RendererIngressState.h>
#include <RendererResetRequest.h>

class IVideoRenderer;

class RendererResetCoordinator
{
public:
	// Public only so the separately allocated weak-token sink can name the
	// opaque shared core. Its definition remains private to the .cpp.
	struct State;

	using Clock = std::function<uint64_t()>;
	// Return true only when the wake was accepted. A failed wake is retried by
	// the permanent executor so a request cannot remain stranded.
	using WakeUi = std::function<bool()>;

	struct Binding
	{
		RendererBindingToken token = 0;
		uint32_t rendererGeneration = 0;
		std::shared_ptr<IRendererResetRequestSink> sink;
	};

	struct SelectedReset
	{
		RendererResetRequest request;
		uint32_t rendererGeneration = 0;
		uint64_t transitionToken = 0;
		uint64_t targetRevision = 0;
	};

	enum class StartResult
	{
		Started,
		Closed,
		StaleSelection,
		OperationActive,
		InvalidRenderer,
	};

	struct OperationResult
	{
		uint64_t operationId = 0;
		RendererResetRequest request;
		uint32_t rendererGeneration = 0;
		uint64_t transitionToken = 0;
		uint64_t targetRevision = 0;
		bool succeeded = false;
		bool staleGeneration = false;
		bool staleBinding = false;
		bool restartRequired = false;
		bool ingressReopened = false;
		std::string failure;
	};

	struct Diagnostics
	{
		bool closed = false;
		bool hasBinding = false;
		bool hasPending = false;
		bool wakePosted = false;
		RendererBindingToken bindingToken = 0;
		uint32_t rendererGeneration = 0;
		RendererResetReason pendingReason = RendererResetReason::None;
		RendererResetScope pendingScope = RendererResetScope::LiveQueue;
		uint64_t pendingDeadlineTick = 0;
		uint64_t acceptedRequestCount = 0;
		uint64_t staleRequestCount = 0;
		bool selectionPrepared = false;
		bool operationActive = false;
		bool completionPending = false;
		bool restartCoverRequired = false;
		uint64_t activeOperationId = 0;
		size_t ingressActiveLeases = 0;
		bool ingressAdmitting = false;
	};

	RendererResetCoordinator(WakeUi wakeUi, Clock clock);
	~RendererResetCoordinator();

	RendererResetCoordinator(const RendererResetCoordinator&) = delete;
	RendererResetCoordinator& operator=(const RendererResetCoordinator&) = delete;

	Binding Bind(uint32_t rendererGeneration);
	void Revoke(RendererBindingToken token) noexcept;
	void Close() noexcept;
	void Join() noexcept;

	// UI-originated requests use the current binding and the injected clock.
	bool RequestUi(RendererResetReason reason, RendererResetScope scope,
		uint64_t delayMs = 0, uint64_t backendEpoch = 0) noexcept;

	// Called on the UI/control thread after a wakeup or deadline timer. Clearing
	// the outstanding-wakeup flag and selecting work happen under one lock so a
	// concurrent submit either joins this drain or posts a new wakeup.
	bool DrainReady(uint64_t now, SelectedReset& selected) noexcept;

	// DrainReady reserves one request. The host first establishes the black
	// cover, then acknowledges that success here to close ingress and enqueue
	// exactly one operation on the permanent worker.
	StartResult AcknowledgeBlackAndStart(
		const SelectedReset& selected,
		std::shared_ptr<IVideoRenderer> renderer) noexcept;

	// Call when the black cover cannot be established. The request becomes a
	// terminal restart result and remains covered until its binding is revoked.
	bool RejectBlackAndRequireRestart(
		const SelectedReset& selected,
		const char* failure) noexcept;

	// Completion is consumed on the host/control thread. Ingress reopens only
	// for a successful result belonging to the current usable generation.
	bool ConsumeCompletion(
		uint32_t currentRendererGeneration,
		bool rendererUsable,
		OperationResult& result,
		uint64_t expectedTransitionToken = 0,
		uint64_t expectedTargetRevision = 0,
		bool transitionReadyForCompletion = true) noexcept;

	std::shared_ptr<RendererIngressState> GetIngressState() const noexcept;

	bool BlocksReveal(uint32_t rendererGeneration) const noexcept;
	bool RequiresLifecycleDeferral() const noexcept;
	Diagnostics GetDiagnostics() const noexcept;

private:
	std::shared_ptr<State> m_state;
	std::thread m_worker;
};
