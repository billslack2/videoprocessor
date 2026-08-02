/*
 * Serialized DirectShow flush/segment transaction.
 *
 * Queue, epoch, and worker state remain owned by the output-pin coordinator.
 * This component only guarantees the renderer-facing order:
 * BeginFlush -> caller's serialized state transition -> EndFlush -> NewSegment.
 * If the state transition throws, EndFlush is still attempted before the
 * exception leaves the coordinator.
 */
#pragma once

#include <functional>

#include <windows.h>

struct DirectShowSegmentTransitionResult
{
	HRESULT beginFlushResult = E_UNEXPECTED;
	HRESULT endFlushResult = E_UNEXPECTED;
	HRESULT newSegmentResult = E_UNEXPECTED;
	bool began = false;
};

class DirectShowSegmentTransition
{
public:
	using BeginFlushCallback = std::function<HRESULT()>;
	using StateTransitionCallback = std::function<void()>;
	using EndFlushCallback = std::function<HRESULT()>;
	using NewSegmentCallback = std::function<HRESULT()>;

	DirectShowSegmentTransitionResult Execute(
		const BeginFlushCallback& beginFlush,
		const StateTransitionCallback& stateTransition,
		const EndFlushCallback& endFlush,
		const NewSegmentCallback& newSegment) const;
};
