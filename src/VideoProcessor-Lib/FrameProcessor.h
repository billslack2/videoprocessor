/*
 * Graph-free processing-stage orchestration.
 *
 * A caller supplies conversion and clock callbacks. This stage measures and
 * invokes conversion, then returns neutral processed-frame metadata. It never
 * sets final presentation times, makes cadence decisions, calls Deliver(), or
 * owns a queue/worker.
 */
#pragma once

#include <cstdint>
#include <functional>

#include <ProcessedFrameQueue.h>
#include <VideoFrame.h>

struct FrameProcessorInput
{
	VideoFrame* source = nullptr;
	IMediaSample* sample = nullptr;
	PipelineEpoch epoch;
	uint64_t sourceFrameNumber = 0;
	uint64_t captureTimestamp = 0;
	uint64_t sceneTimingGeneration = 0;
};

struct FrameProcessorResult
{
	HRESULT result = E_UNEXPECTED;
	uint32_t processingDurationUs = 0;
	ProcessedFrame frame;
	bool producedFrame = false;
};

class FrameProcessor
{
public:
	using ConvertCallback = std::function<HRESULT(VideoFrame&, IMediaSample*)>;
	using ClockCallback = std::function<uint64_t()>;

	FrameProcessor() = default;
	FrameProcessor(ConvertCallback convert, ClockCallback clock);

	void Configure(ConvertCallback convert, ClockCallback clock);
	FrameProcessorResult Process(const FrameProcessorInput& input) const;

private:
	ConvertCallback m_convert;
	ClockCallback m_clock;
};
