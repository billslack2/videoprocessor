#pragma once

#include <cstdint>

enum class RendererResetReason
{
	None,
	Manual,
	PostRendererStart,
	RendererSwitch,
	RefreshTransition,
	HostTransition,
	DisplayTransition,
	Resize,
	QueueSizeChange,
	ProfileChange,
	TimingOffsetChange,
	QueuePressure,
	QueueCapacity,
	SourceGapRecovery,
	LivenessRecovery,
	OutputReadiness,
};

constexpr int RendererResetPriority(RendererResetReason reason)
{
	switch (reason)
	{
	case RendererResetReason::Manual: return 100;
	case RendererResetReason::LivenessRecovery: return 90;
	case RendererResetReason::QueueCapacity: return 90;
	// A concurrent display/HDR transition owns its rebuild and replaces this
	// lower-priority same-contract recovery request.
	case RendererResetReason::SourceGapRecovery: return 65;
	case RendererResetReason::PostRendererStart: return 80;
	case RendererResetReason::RendererSwitch: return 80;
	case RendererResetReason::RefreshTransition: return 80;
	case RendererResetReason::HostTransition: return 80;
	case RendererResetReason::OutputReadiness: return 75;
	case RendererResetReason::DisplayTransition: return 70;
	case RendererResetReason::Resize: return 60;
	case RendererResetReason::QueueSizeChange: return 50;
	case RendererResetReason::ProfileChange: return 55;
	case RendererResetReason::TimingOffsetChange: return 40;
	case RendererResetReason::QueuePressure: return 30;
	default: return 0;
	}
}

constexpr bool RendererResetShouldReplace(
	int incomingPriority,
	uint64_t incomingDeadline,
	int selectedPriority,
	uint64_t selectedDeadline)
{
	return incomingPriority > selectedPriority ||
		(incomingPriority == selectedPriority &&
			incomingDeadline < selectedDeadline);
}

// DirectShow/madVR owns additional downstream queues which a VP-only flush
// cannot refill coherently after a policy change. Keep the renderer object and
// generation, but run its serialized stop/flush/prime/new-segment graph reset.
// Alpha's entire queue is VP-owned, so its lightweight live-queue reset is
// sufficient.
constexpr bool QueuePolicyApplyRequiresGraphReset(
	bool directShowRendererActive)
{
	return directShowRendererActive;
}

enum class DirectShowGraphEventImpact
{
	None,
	GeometryOnly,
	DisplayTransition,
};

// EC_VIDEO_SIZE_CHANGED is expected when madVR accepts a dynamic picture
// aspect update (for example NLS). It changes presentation geometry, not the
// physical output timing, and must not invalidate refresh evidence or re-prime
// the graph. EC_DISPLAY_CHANGED remains a real output transition.
constexpr DirectShowGraphEventImpact ClassifyDirectShowGraphEvent(
	long eventCode)
{
	constexpr long EcVideoSizeChanged = 0x0e;
	constexpr long EcDisplayChanged = 0x16;
	return eventCode == EcDisplayChanged ?
		DirectShowGraphEventImpact::DisplayTransition :
		eventCode == EcVideoSizeChanged ?
		DirectShowGraphEventImpact::GeometryOnly :
		DirectShowGraphEventImpact::None;
}

enum class RendererRestartDispatch
{
	DispatchNow,
	DeferUntilConstructionCompletes,
	DeferUntilRetirementCompletes,
};

// Renderer construction can pump window messages inside third-party Build()
// and Start() calls. A shortcut received there is a new desired renderer, not
// permission to re-enter teardown while the current renderer is only partly
// constructed. Keep the latest intent latched and reconcile it after the
// current lifecycle boundary.
constexpr RendererRestartDispatch ClassifyRendererRestartDispatch(
	bool constructionActive,
	bool retirementPending)
{
	return constructionActive ?
		RendererRestartDispatch::DeferUntilConstructionCompletes :
		retirementPending ?
		RendererRestartDispatch::DeferUntilRetirementCompletes :
		RendererRestartDispatch::DispatchNow;
}

// A graph retarget cannot be safely interrupted once madVR owns it. The
// fullscreen control is still allowed to express the final intent while that
// transaction is in flight; a covered rebuild is required only when that
// final intent differs from the target of the active retarget.
constexpr bool FullscreenRetargetRequiresCoveredRebuild(
	bool retargetExitingFullscreen,
	bool desiredFullscreen)
{
	const bool retargetTargetsFullscreen = !retargetExitingFullscreen;
	return desiredFullscreen != retargetTargetsFullscreen;
}

enum class AlphaFreshStartTransition
{
	None,
	BackendHandoff,
	HostTransition,
	RefreshTransition,
};

// Record the backend boundary for diagnostics. The new renderer receives a
// delayed post-transition queue reset even though the retired queue cannot
// cross into it; fullscreen and renderer changes may still complete while
// display or driver work is settling.
constexpr bool IsDirectShowToAlphaBackendHandoff(
	bool previousRendererWasDirectShow,
	bool nextRendererIsDirectShow)
{
	return previousRendererWasDirectShow && !nextRendererIsDirectShow;
}

// Fullscreen host changes and renderer handoffs are deterministic reset
// boundaries. Use the configured queue-reset delay before checking any
// post-stall advisory so a freshly constructed Alpha queue has the same
// transition-settle behavior as the DirectShow path.
constexpr bool AlphaFreshStartRequiresDelayedReprime(
	AlphaFreshStartTransition transition)
{
	return transition == AlphaFreshStartTransition::RefreshTransition ||
		transition == AlphaFreshStartTransition::HostTransition ||
		transition == AlphaFreshStartTransition::BackendHandoff;
}
