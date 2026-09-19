#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "AnalysisLumaSource.h"

// Sparse P010 luma scene-boundary detector shared by renderer adapters.  It
// owns no source buffer and only retains a bounded signature of prior frames.
enum class SceneDetectorStatus
{
	Disabled,
	Warming,
	Active,
	Failed,
};

struct SceneDetectorInput
{
	const uint16_t* p010Luma = nullptr;
	size_t width = 0;
	size_t height = 0;
	size_t strideBytes = 0;
	uint64_t sourceSequence = 0;
	int64_t timestamp = 0;
	uint64_t generation = 0;
	uint64_t frameDuration = 0;
	bool enabled = false;
	// Preferred format-neutral source. It is trailing to preserve existing
	// positional P010 initializers while adapters migrate independently.
	const AnalysisLumaSource* analysisSource = nullptr;
};

struct SceneDetectorResult
{
	SceneDetectorStatus status = SceneDetectorStatus::Disabled;
	bool safeBoundary = false;
	uint64_t eventId = 0;
	uint8_t eventFramesBack = 0;
	uint16_t averageLuma = 0;
	uint64_t sourceSequence = 0;
	uint64_t generation = 0;
	// Reasons describe this analysis even if event cooldown suppresses a
	// boundary. A near-black entry is not proof of a fade: low-contrast cuts
	// can remain below the existing hard-cut thresholds.
	bool nearBlackEntry = false;
	// Includes pending evidence present BEFORE settling/expiry/darkness clears
	// it, as well as a new immediate candidate on this frame.
	bool hardCutCandidate = false;
	bool hardCutConfirmed = false;
	bool differenceEvaluated = false;
	uint32_t immediateAverageLumaDifference = 0;
	uint32_t changedSampleCount = 0;
	uint32_t sampleCount = 0;
	uint32_t histogramDistance = 0;
};

class SceneDetector
{
public:
	SceneDetectorResult Analyze(const SceneDetectorInput& input);
	void Reset(uint64_t generation = 0);

private:
	struct Signature
	{
		static constexpr size_t COLUMNS = 32;
		static constexpr size_t ROWS = 18;
		static constexpr size_t HISTOGRAM_BINS = 16;
		std::array<uint16_t, COLUMNS * ROWS> luma{};
		std::array<uint16_t, HISTOGRAM_BINS> histogram{};
		uint32_t averageLuma = 0;
		bool valid = false;
	};

	struct Difference
	{
		uint32_t averageLumaDifference = 0;
		uint32_t changedSampleCount = 0;
		uint32_t histogramDistance = 1000;
	};

	static Difference Compare(const Signature& a, const Signature& b,
		uint16_t threshold);

	Signature m_previous;
	Signature m_pendingHardCut;
	uint8_t m_pendingHardCutFrames = 0;
	uint32_t m_pendingInitialAverageLumaDifference = 0;
	uint32_t m_pendingInitialChangedSampleCount = 0;
	uint32_t m_framesUntilNextEvent = 0;
	bool m_pendingHardCutValid = false;
	bool m_previousNearBlack = false;
	uint64_t m_generation = 0;
	uint64_t m_nextEventId = 0;
};
