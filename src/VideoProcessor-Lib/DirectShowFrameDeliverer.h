/*
 * Narrow DirectShow delivery boundary.
 *
 * The coordinator retains epoch/flush/lifecycle ownership. This component
 * pairs a pending-media-type attachment with the actual downstream delivery
 * and completion callback, and measures only that transaction. It does not
 * inspect queue depth, generate timestamps, alter samples, or make cadence
 * decisions.
 */
#pragma once

#include <cstdint>
#include <functional>

#include <streams.h>

struct DirectShowDeliveryTicket
{
	IMediaSample* sample = nullptr;
	uint64_t mediaTypeGeneration = 0;
	uint64_t startTick100ns = 0;
	bool valid = false;
};

struct DirectShowDeliveryResult
{
	HRESULT result = E_POINTER;
	uint32_t durationUs = 0;
};

class DirectShowFrameDeliverer
{
public:
	using AttachCallback = std::function<uint64_t(IMediaSample*)>;
	using DeliverCallback = std::function<HRESULT(IMediaSample*)>;
	using CompleteCallback = std::function<void(uint64_t, HRESULT)>;
	using ClockCallback = std::function<uint64_t()>;

	DirectShowDeliveryTicket Begin(
		IMediaSample* sample,
		const AttachCallback& attach,
		const ClockCallback& clock) const;
	DirectShowDeliveryResult Complete(
		const DirectShowDeliveryTicket& ticket,
		const DeliverCallback& deliver,
		const CompleteCallback& complete,
		const ClockCallback& clock) const;
};
