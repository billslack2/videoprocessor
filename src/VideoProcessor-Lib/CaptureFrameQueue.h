/*
 * VP-owned raw-capture transport boundary.
 *
 * This queue keeps the historical ownership rule: accepting a VideoFrame
 * means owning one SourceBufferAddRef() reference until it is converted,
 * overflows, is rejected as stale, or is flushed.
 */
#pragma once

#include <EpochBoundedQueue.h>
#include <VideoFrame.h>

struct CaptureFrameQueueRelease
{
	void operator()(VideoFrame& frame) const noexcept
	{
		try
		{
			frame.SourceBufferRelease();
		}
		catch (...)
		{
			// Queue teardown must never throw.  The existing pin records its own
			// purge diagnostics; this boundary only preserves source ownership.
		}
	}
};

using CaptureFrameQueue = EpochBoundedQueue<VideoFrame, CaptureFrameQueueRelease>;
