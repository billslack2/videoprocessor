/*
 * VP-owned converted-frame transport boundary.
 *
 * Conversion transfers the allocator sample reference into ProcessedFrame.
 * The queue releases that reference on stale rejection, overflow trimming, or
 * flush; delivery takes the same reference without an additional frame copy.
 */
#pragma once

#include <EpochBoundedQueue.h>

#include <streams.h>

struct ProcessedFrame
{
	IMediaSample* sample = nullptr;
	uint64_t frameNumber = 0;
	uint64_t captureTimestamp = 0;
	uint64_t captureArrivalTick = 0;
	uint32_t processingDurationUs = 0;
	// Preserve a capture/segment discontinuity until the final delivery-owned
	// timestamp stamp. Conversion timestamps are provisional for buffered live
	// delivery, but this semantic flag must not be lost when they are replaced.
	bool sourceDiscontinuity = false;
	// Exact VP-owned raw replacements immediately before this source frame.
	// Retained through conversion because final presentation timestamps are
	// owned by the delivery thread.
	uint32_t intentionalRawReplacementSlotsBefore = 0;
	bool isSafeCorrectionPoint = false;
	uint64_t sceneEventId = 0;
	uint64_t queueEpoch = 0;
	uint64_t sceneTimingGeneration = 0;
};

struct ProcessedFrameQueueRelease
{
	void operator()(ProcessedFrame& frame) const noexcept
	{
		if (frame.sample)
		{
			frame.sample->Release();
			frame.sample = nullptr;
		}
	}
};

using ProcessedFrameQueue =
	EpochBoundedQueue<ProcessedFrame, ProcessedFrameQueueRelease>;
