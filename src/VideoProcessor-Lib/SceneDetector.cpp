#include "pch.h"
#include "SceneDetector.h"

#include <algorithm>
#include <cstdlib>

SceneDetector::Difference SceneDetector::Compare(
	const Signature& a,
	const Signature& b,
	uint16_t threshold)
{
	Difference result;
	uint64_t totalDifference = 0;
	for (size_t i = 0; i < a.luma.size(); ++i)
	{
		const uint16_t difference = static_cast<uint16_t>(std::abs(
			static_cast<int>(a.luma[i]) - static_cast<int>(b.luma[i])));
		totalDifference += difference;
		if (difference >= threshold)
			++result.changedSampleCount;
	}
	result.averageLumaDifference =
		static_cast<uint32_t>(totalDifference / a.luma.size());

	uint64_t intersection = 0;
	uint64_t total = 0;
	for (size_t i = 0; i < a.histogram.size(); ++i)
	{
		intersection += std::min(a.histogram[i], b.histogram[i]);
		total += b.histogram[i];
	}
	if (total > 0)
		result.histogramDistance = static_cast<uint32_t>(1000 - (intersection * 1000) / total);
	return result;
}

void SceneDetector::Reset(uint64_t generation)
{
	m_previous = {};
	m_pendingHardCut = {};
	m_pendingHardCutFrames = 0;
	m_pendingInitialAverageLumaDifference = 0;
	m_pendingInitialChangedSampleCount = 0;
	m_framesUntilNextEvent = 0;
	m_pendingHardCutValid = false;
	m_previousNearBlack = false;
	m_generation = generation;
}

SceneDetectorResult SceneDetector::Analyze(const SceneDetectorInput& input)
{
	SceneDetectorResult result;
	result.sourceSequence = input.sourceSequence;
	result.generation = input.generation;
	if (!input.enabled)
		return result;
	const AnalysisLumaSource* source = input.analysisSource;
	if (source && (!source->IsValid() || source->width != static_cast<int>(input.width) ||
		source->height != static_cast<int>(input.height)))
	{
		result.status = SceneDetectorStatus::Failed;
		return result;
	}
	if (!source && (!input.p010Luma || input.width == 0 || input.height == 0 ||
		input.strideBytes < input.width * sizeof(uint16_t)))
	{
		result.status = SceneDetectorStatus::Failed;
		return result;
	}
	if (m_generation != input.generation)
		Reset(input.generation);

	Signature current;
	uint64_t totalLuma = 0;
	size_t darkSampleCount = 0;
	for (size_t row = 0; row < Signature::ROWS; ++row)
	{
		const size_t y = ((row * 2 + 1) * input.height) / (Signature::ROWS * 2);
		for (size_t column = 0; column < Signature::COLUMNS; ++column)
		{
			const size_t x = ((column * 2 + 1) * input.width) / (Signature::COLUMNS * 2);
			uint16_t luma = 0;
			if (source)
			{
				AnalysisLumaSample sample;
				if (!source->Sample(static_cast<int>(x), static_cast<int>(y), sample))
				{
					result.status = SceneDetectorStatus::Failed;
					return result;
				}
				luma = sample.luma;
			}
			else
			{
				const auto* line = reinterpret_cast<const uint16_t*>(
					reinterpret_cast<const uint8_t*>(input.p010Luma) + y * input.strideBytes);
				luma = static_cast<uint16_t>(line[x] >> 6);
			}
			const size_t index = row * Signature::COLUMNS + column;
			current.luma[index] = luma;
			current.histogram[std::min<size_t>(luma / 64, Signature::HISTOGRAM_BINS - 1)]++;
			totalLuma += luma;
			if (luma <= 112)
				++darkSampleCount;
		}
	}

	const size_t sampleCount = current.luma.size();
	current.averageLuma = static_cast<uint32_t>(totalLuma / sampleCount);
	current.valid = true;
	result.averageLuma = static_cast<uint16_t>(std::min<uint32_t>(1023, current.averageLuma));
	result.status = m_previous.valid ? SceneDetectorStatus::Active : SceneDetectorStatus::Warming;
	const bool nearBlack = current.averageLuma <= 96 && darkSampleCount >= sampleCount * 9 / 10;
	bool sceneEvent = false;

	if (m_previous.valid)
	{
		const Difference immediate = Compare(current, m_previous, 32);
		if (m_pendingHardCutValid)
		{
			const Difference settling = Compare(current, m_pendingHardCut, 24);
			const bool settled = settling.averageLumaDifference <= 64 &&
				settling.changedSampleCount <= sampleCount * 50 / 100 &&
				settling.histogramDistance <= 140 &&
				static_cast<uint64_t>(settling.averageLumaDifference) * 100 <=
					static_cast<uint64_t>(m_pendingInitialAverageLumaDifference) * 80 &&
				static_cast<uint64_t>(settling.changedSampleCount) * 100 <=
					static_cast<uint64_t>(m_pendingInitialChangedSampleCount) * 85;
			if (settled)
			{
				sceneEvent = true;
				result.eventFramesBack = static_cast<uint8_t>(m_pendingHardCutFrames + 1);
				m_pendingHardCutValid = false;
				m_pendingHardCutFrames = 0;
			}
			else if (++m_pendingHardCutFrames >= 4)
			{
				m_pendingHardCutValid = false;
				m_pendingHardCutFrames = 0;
				m_pendingInitialAverageLumaDifference = 0;
				m_pendingInitialChangedSampleCount = 0;
			}
		}
		const bool broadSpatialChange = immediate.averageLumaDifference >= 44 &&
			immediate.changedSampleCount >= sampleCount * 45 / 100;
		const bool hardSceneCut = broadSpatialChange &&
			(immediate.histogramDistance >= 55 || immediate.averageLumaDifference >= 64);
		if (hardSceneCut && !sceneEvent && !m_pendingHardCutValid)
		{
			m_pendingHardCut = current;
			m_pendingHardCutValid = true;
			m_pendingHardCutFrames = 0;
			m_pendingInitialAverageLumaDifference = immediate.averageLumaDifference;
			m_pendingInitialChangedSampleCount = immediate.changedSampleCount;
		}
	}

	if (nearBlack && !m_previousNearBlack)
	{
		sceneEvent = true;
		m_pendingHardCutValid = false;
		m_pendingHardCutFrames = 0;
		m_pendingInitialAverageLumaDifference = 0;
		m_pendingInitialChangedSampleCount = 0;
	}
	m_previousNearBlack = nearBlack;
	if (m_framesUntilNextEvent > 0)
		--m_framesUntilNextEvent;
	if (sceneEvent && m_framesUntilNextEvent > 0)
		sceneEvent = false;
	m_previous = current;

	if (sceneEvent)
	{
		const uint64_t duration = std::max<uint64_t>(1, input.frameDuration);
		const uint64_t cooldown = (20000000ULL + duration - 1) / duration;
		m_framesUntilNextEvent = static_cast<uint32_t>(std::min<uint64_t>(cooldown, 300));
		result.safeBoundary = true;
		result.eventId = ++m_nextEventId;
	}
	return result;
}
