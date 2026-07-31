#include <pch.h>

#include <DirectShowSegmentTransition.h>

DirectShowSegmentTransitionResult DirectShowSegmentTransition::Execute(
	const BeginFlushCallback& beginFlush,
	const StateTransitionCallback& stateTransition,
	const EndFlushCallback& endFlush,
	const NewSegmentCallback& newSegment) const
{
	DirectShowSegmentTransitionResult result;
	if (!beginFlush || !stateTransition || !endFlush || !newSegment)
		return result;

	result.beginFlushResult = beginFlush();
	if (FAILED(result.beginFlushResult))
		return result;

	result.began = true;
	try
	{
		stateTransition();
	}
	catch (...)
	{
		result.endFlushResult = endFlush();
		throw;
	}

	result.endFlushResult = endFlush();
	if (FAILED(result.endFlushResult))
		return result;

	result.newSegmentResult = newSegment();
	return result;
}
