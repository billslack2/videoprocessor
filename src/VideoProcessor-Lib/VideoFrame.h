/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <ITimingClock.h>


/**
 * Structure which represents a single video frame
 */
class VideoFrame
{
public:

	/**
	 * Constructor
	 *
	 * This is just a pointer to some data.
	 * If this data in any way, shape or form might be gone by the time it's used, you can use the
	 * sourceBuffer argument to have the VideoFrame constr/destr do ref management.
	 */
	VideoFrame() {}
	VideoFrame(
		const void* const data, uint64_t counter,
		timingclocktime_t timingTimestamp, IUnknown* sourceBuffer,
		timingclocktime_t captureTimingTimestamp = TIMING_CLOCK_TIME_INVALID);
	VideoFrame(const VideoFrame&);

	~VideoFrame();

	// Get frame data
	// If you're wondering where the size of GetData() is, it can be found by querying
	// VideoState::BytesPerFrame() which you should get before this gets delivered.
	const void* const GetData() const { return m_data; }

	// Get counter, this is monotoncally increasing from the capture source
	uint64_t GetCounter() const { return m_counter; }

	// Timestamp set by the timing clock.
	timingclocktime_t GetTimingTimestamp() const { return m_timingTimestamp; }

	// Hardware-capture timestamp before VP's configured frame offset is applied.
	// This is diagnostic metadata for comparing video delay with the audio
	// extraction boundary; it never participates in presentation scheduling.
	timingclocktime_t GetCaptureTimingTimestamp() const
	{
		return m_captureTimingTimestamp == TIMING_CLOCK_TIME_INVALID
			? m_timingTimestamp : m_captureTimingTimestamp;
	}

	// VP monotonic arrival tick, assigned only when a buffered live-output pin
	// accepts the frame. It is diagnostic metadata and never participates in
	// source timing or presentation scheduling.
	uint64_t GetCaptureArrivalTick() const { return m_captureArrivalTick; }
	void SetCaptureArrivalTick(uint64_t tick) { m_captureArrivalTick = tick; }

	// A source counter gap means live content was skipped before VP accepted
	// this frame. It does not require a graph reset; the final delivery owner
	// carries this marker into the DirectShow sample discontinuity flag.
	bool IsSourceDiscontinuity() const { return m_sourceDiscontinuity; }
	void SetSourceDiscontinuity(bool discontinuity)
	{
		m_sourceDiscontinuity = discontinuity;
	}

	// Memory functions to hold onto the video buffer for longer
	void SourceBufferAddRef();
	void SourceBufferRelease();

	VideoFrame& operator= (const VideoFrame& videoFrame);

private:
	const void* m_data;
	uint64_t m_counter;
	timingclocktime_t m_timingTimestamp;
	timingclocktime_t m_captureTimingTimestamp = TIMING_CLOCK_TIME_INVALID;
	uint64_t m_captureArrivalTick = 0;
	bool m_sourceDiscontinuity = false;
	IUnknown* m_sourceBuffer;
};
