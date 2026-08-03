#pragma once

#include <cstdint>

enum class RendererResetReason
{
	None,
	Manual,
	PostRendererStart,
	RefreshTransition,
	HostTransition,
	DisplayTransition,
	Resize,
	QueueSizeChange,
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
	case RendererResetReason::RefreshTransition: return 80;
	case RendererResetReason::HostTransition: return 80;
	case RendererResetReason::OutputReadiness: return 75;
	case RendererResetReason::DisplayTransition: return 70;
	case RendererResetReason::Resize: return 60;
	case RendererResetReason::QueueSizeChange: return 50;
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

// A DirectShow graph can leave a short-lived delivery reserve while its
// replacement Alpha swapchain is starting. Treat that backend boundary as a
// known transition, rather than inferring it from the Alpha queue depth.
constexpr bool AlphaBackendHandoffRequiresReprime(
	bool previousRendererWasDirectShow,
	bool nextRendererIsDirectShow)
{
	return previousRendererWasDirectShow && !nextRendererIsDirectShow;
}
