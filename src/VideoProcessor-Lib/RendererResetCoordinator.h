#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <RendererResetRequest.h>


class RendererResetCoordinator
{
public:
	// Public only so the separately allocated weak-token sink can name the
	// opaque shared core. Its definition remains private to the .cpp.
	struct State;

	using Clock = std::function<uint64_t()>;
	using WakeUi = std::function<void()>;

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
	};

	RendererResetCoordinator(WakeUi wakeUi, Clock clock);
	~RendererResetCoordinator();

	RendererResetCoordinator(const RendererResetCoordinator&) = delete;
	RendererResetCoordinator& operator=(const RendererResetCoordinator&) = delete;

	Binding Bind(uint32_t rendererGeneration);
	void Revoke(RendererBindingToken token) noexcept;
	void Close() noexcept;

	// UI-originated requests use the current binding and the injected clock.
	bool RequestUi(RendererResetReason reason, RendererResetScope scope,
		uint64_t delayMs = 0, uint64_t backendEpoch = 0) noexcept;

	// Called on the UI/control thread after a wakeup or deadline timer. Clearing
	// the outstanding-wakeup flag and selecting work happen under one lock so a
	// concurrent submit either joins this drain or posts a new wakeup.
	bool DrainReady(uint64_t now, SelectedReset& selected) noexcept;

	bool BlocksReveal(uint32_t rendererGeneration) const noexcept;
	Diagnostics GetDiagnostics() const noexcept;

private:
	std::shared_ptr<State> m_state;
};
