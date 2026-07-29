#pragma once

#include <cstdint>

enum class RendererResetReason
{
	None,
	Manual,
	PostRendererStart,
	DisplayTransition,
	Resize,
	QueueSizeChange,
	TimingOffsetChange,
	QueuePressure,
	LivenessRecovery,
};

constexpr int RendererResetPriority(RendererResetReason reason)
{
	switch (reason)
	{
	case RendererResetReason::Manual: return 100;
	case RendererResetReason::LivenessRecovery: return 90;
	case RendererResetReason::PostRendererStart: return 80;
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
