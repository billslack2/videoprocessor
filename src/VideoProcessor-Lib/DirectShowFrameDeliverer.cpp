#include <pch.h>

#include <DirectShowFrameDeliverer.h>

#include <algorithm>
#include <limits>

DirectShowDeliveryTicket DirectShowFrameDeliverer::Begin(
	IMediaSample* sample,
	const AttachCallback& attach,
	const ClockCallback& clock) const
{
	DirectShowDeliveryTicket ticket;
	if (!sample || !attach || !clock)
		return ticket;

	ticket.sample = sample;
	ticket.startTick100ns = clock();
	ticket.mediaTypeGeneration = attach(sample);
	ticket.valid = true;
	return ticket;
}

DirectShowDeliveryResult DirectShowFrameDeliverer::Complete(
	const DirectShowDeliveryTicket& ticket,
	const DeliverCallback& deliver,
	const CompleteCallback& complete,
	const ClockCallback& clock) const
{
	DirectShowDeliveryResult result;
	if (!ticket.valid || !ticket.sample || !deliver || !complete || !clock)
		return result;

	result.result = deliver(ticket.sample);
	complete(ticket.mediaTypeGeneration, result.result);
	const uint64_t end = clock();
	const uint64_t durationUs = end >= ticket.startTick100ns ?
		(end - ticket.startTick100ns) / 10 : 0;
	result.durationUs = static_cast<uint32_t>(
		std::min<uint64_t>(durationUs, std::numeric_limits<uint32_t>::max()));
	return result;
}

DirectShowSamplePreparationResult DirectShowFrameDeliverer::Prepare(
	const DirectShowSamplePreparationRequest& request) const
{
	DirectShowSamplePreparationResult result;
	if (!request.sample)
	{
		result.discontinuityResult = E_POINTER;
		result.getTimeResult = E_POINTER;
		result.setTimeResult = E_POINTER;
		return result;
	}

	if (request.markDiscontinuity && request.setDiscontinuity)
		result.discontinuityResult = request.setDiscontinuity(request.sample, TRUE);

	if (!request.lateBindStop || !request.getTime || !request.setTime ||
		!request.findNextStart)
		return result;

	result.getTimeResult = request.getTime(
		request.sample, &result.originalStart, &result.originalStop);
	const REFERENCE_TIME theoreticalStop =
		result.originalStart + request.frameDuration;
	result.theoreticalStop = theoreticalStop;
	const REFERENCE_TIME bestStart = request.findNextStart(
		result.originalStart, theoreticalStop, request.lateBindTolerance);
	result.matchedNextStart = bestStart;
	if (bestStart == static_cast<REFERENCE_TIME>(-1))
		return result;

	REFERENCE_TIME newStop = bestStart;
	if (newStop <= result.originalStart)
		newStop = result.originalStart + request.frameDuration;
	result.setTimeResult = request.setTime(
		request.sample, &result.originalStart, &newStop);
	// The legacy path records a matched late-bound stop even if a renderer
	// rejects SetTime; preserve that reporting shape while exposing the HRESULT.
	result.lateBoundStopApplied = true;
	return result;
}
