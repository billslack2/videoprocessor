/*
 * Narrow DirectShow delivery boundary.
 *
 * The coordinator retains epoch/flush/lifecycle ownership. This component
 * pairs a pending-media-type attachment with the actual downstream delivery
 * and completion callback, and measures only that transaction. It also
 * applies caller-supplied, non-policy sample preparation (discontinuity and
 * late-bound stop). It does not inspect queue depth, generate timestamps, or
 * make cadence decisions.
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

struct DirectShowSamplePreparationRequest
{
	IMediaSample* sample = nullptr;
	bool markDiscontinuity = false;
	bool lateBindStop = false;
	REFERENCE_TIME frameDuration = 0;
	REFERENCE_TIME lateBindTolerance = 0;
	std::function<HRESULT(IMediaSample*, BOOL)> setDiscontinuity;
	std::function<HRESULT(IMediaSample*, REFERENCE_TIME*, REFERENCE_TIME*)> getTime;
	std::function<HRESULT(IMediaSample*, REFERENCE_TIME*, REFERENCE_TIME*)> setTime;
	std::function<REFERENCE_TIME(REFERENCE_TIME, REFERENCE_TIME, REFERENCE_TIME)> findNextStart;
};

struct DirectShowSamplePreparationResult
{
	HRESULT discontinuityResult = S_OK;
	HRESULT getTimeResult = S_OK;
	HRESULT setTimeResult = S_OK;
	bool lateBoundStopApplied = false;
	REFERENCE_TIME originalStart = 0;
	REFERENCE_TIME originalStop = 0;
	REFERENCE_TIME theoreticalStop = 0;
	REFERENCE_TIME matchedNextStart = static_cast<REFERENCE_TIME>(-1);
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
	DirectShowSamplePreparationResult Prepare(
		const DirectShowSamplePreparationRequest& request) const;
};
