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
