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

#include <ActivePictureAnalyzer.h>
#include <ProcessedFrameQueue.h>
#include <SceneDetector.h>
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

struct SceneAnalysisInput
{
	const uint16_t* p010Luma = nullptr;
	size_t width = 0;
	size_t height = 0;
	size_t strideBytes = 0;
	uint64_t sourceSequence = 0;
	int64_t timestamp = 0;
	uint64_t generation = 0;
	uint64_t frameDuration = 0;
	SceneDetector* detector = nullptr;
};

struct SceneAnalysisResult
{
	bool validInput = false;
	bool safeBoundary = false;
	uint8_t eventFramesBack = 0;
	uint16_t averageLuma = 0;
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
	ActivePictureAnalyzerResult AnalyzeActivePicture(
		const ActivePictureAnalyzerInput& input);
	void ResetActivePicture();
	SceneAnalysisResult AnalyzeScene(const SceneAnalysisInput& input) const;

private:
	ConvertCallback m_convert;
	ClockCallback m_clock;
	ActivePictureAnalyzer m_activePictureAnalyzer;
};
