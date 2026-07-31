#include <pch.h>

#include <FrameProcessor.h>

#include <algorithm>
#include <limits>

FrameProcessor::FrameProcessor(ConvertCallback convert, ClockCallback clock)
{
	Configure(std::move(convert), std::move(clock));
}

void FrameProcessor::Configure(ConvertCallback convert, ClockCallback clock)
{
	m_convert = std::move(convert);
	m_clock = std::move(clock);
}

FrameProcessorResult FrameProcessor::Process(const FrameProcessorInput& input) const
{
	FrameProcessorResult result;
	if (!input.source || !input.sample || !m_convert || !m_clock)
	{
		result.result = E_POINTER;
		return result;
	}

	const uint64_t start = m_clock();
	result.result = m_convert(*input.source, input.sample);
	const uint64_t end = m_clock();
	const uint64_t durationUs = end >= start ? (end - start) / 10 : 0;
	result.processingDurationUs = static_cast<uint32_t>(
		std::min<uint64_t>(durationUs, std::numeric_limits<uint32_t>::max()));

	if (FAILED(result.result))
		return result;

	result.frame.sample = input.sample;
	result.frame.frameNumber = input.sourceFrameNumber;
	result.frame.captureTimestamp = input.captureTimestamp;
	result.frame.processingDurationUs = result.processingDurationUs;
	result.frame.queueEpoch = input.epoch.value;
	result.frame.sceneTimingGeneration = input.sceneTimingGeneration;
	result.producedFrame = true;
	return result;
}

ActivePictureAnalyzerResult FrameProcessor::AnalyzeActivePicture(
	const ActivePictureAnalyzerInput& input)
{
	return m_activePictureAnalyzer.Analyze(input);
}

void FrameProcessor::ResetActivePicture()
{
	m_activePictureAnalyzer.Reset();
}

SceneAnalysisResult FrameProcessor::AnalyzeScene(
	const SceneAnalysisInput& input) const
{
	SceneAnalysisResult result;
	if (!input.detector || !input.p010Luma || input.width == 0 ||
		input.height == 0 || input.strideBytes < input.width * sizeof(uint16_t) ||
		input.frameDuration == 0)
		return result;

	result.validInput = true;
	const SceneDetectorResult detection = input.detector->Analyze({
		input.p010Luma, input.width, input.height, input.strideBytes,
		input.sourceSequence, input.timestamp, input.generation,
		input.frameDuration, true });
	result.safeBoundary = detection.safeBoundary;
	result.eventFramesBack = detection.eventFramesBack;
	result.averageLuma = detection.averageLuma;
	return result;
}
