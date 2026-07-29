#pragma once

#include <cstdint>
#include <string>


struct ActivePictureBounds
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int rasterWidth = 0;
	int rasterHeight = 0;
	double aspectRatio = 0.0;
	bool symmetricBars = false;
};

enum class ActivePictureClassification
{
	UNAVAILABLE,
	FULL_RASTER_TRUSTED,
	BAR_CROP_TRUSTED,
	PROVISIONAL
};


struct ActivePictureObservation
{
	ActivePictureBounds bounds;
	uint64_t frameNumber = 0;
	bool available = false;
	ActivePictureClassification classification =
		ActivePictureClassification::UNAVAILABLE;
};


enum class ActivePictureTransitionState
{
	UNAVAILABLE,
	STABLE,
	CANDIDATE_TRANSITION
};


struct ActivePictureTransitionDecision
{
	ActivePictureTransitionState state =
		ActivePictureTransitionState::UNAVAILABLE;
	ActivePictureBounds bounds;
	ActivePictureBounds stableBounds;
	bool publish = false;
	bool stable = false;
	bool diagnostic = false;
	bool clearTransition = false;
	uint8_t matchingCandidates = 0;
	uint8_t contradictoryCandidates = 0;
	uint8_t candidateReversals = 0;
	double confidence = 0.0;
	uint64_t firstContradictoryFrame = 0;
	uint64_t decisionLatencyFrames = 0;
	std::string reason;
};


// Worker-owned confidence/hysteresis model. Expensive luma inspection remains
// outside this class; deterministic observations make the transition policy
// independently testable across content patterns and frame-rate families.
class ActivePictureTransitionModel
{
public:
	static constexpr uint8_t INITIAL_CONFIRMATIONS = 4;
	static constexpr uint8_t CLEAR_TRANSITION_CONFIRMATIONS = 2;
	static constexpr double ANALYSIS_PERIOD_SECONDS = 0.080;
	static constexpr double DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT = 2.0;
	static constexpr double MAX_STABLE_GEOMETRY_DEADBAND_PERCENT = 5.0;

	void Reset();
	// A bounded presentation hysteresis. This never grants crop authority; it
	// only retains an already trusted rectangle through a small measured shift.
	void SetStableGeometryDeadbandPercent(double percent);
	static void SetRuntimeStableGeometryDeadbandPercent(double percent);
	static double GetRuntimeStableGeometryDeadbandPercent();
	bool ShouldAnalyze(uint64_t frameNumber, double framesPerSecond);
	ActivePictureTransitionDecision Observe(
		const ActivePictureObservation& observation);

	static uint64_t AnalysisIntervalFrames(double framesPerSecond);

private:
	static bool SameBounds(
		const ActivePictureBounds& left,
		const ActivePictureBounds& right);
	static bool MateriallyDifferent(
		const ActivePictureBounds& left,
		const ActivePictureBounds& right);
	bool WithinStableGeometryDeadband(
		const ActivePictureBounds& stable,
		const ActivePictureBounds& observation) const;
	static bool HasCropAuthority(
		const ActivePictureObservation& observation);
	static bool IsFullRaster(
		const ActivePictureBounds& bounds);
	ActivePictureTransitionDecision CommitCandidate(
		const ActivePictureObservation& observation,
		const char* reason);
	void StartCandidate(const ActivePictureObservation& observation);
	void ClearCandidate();

	bool m_hasStable = false;
	ActivePictureBounds m_stable;
	ActivePictureClassification m_stableClassification =
		ActivePictureClassification::UNAVAILABLE;
	bool m_hasPreviousTrusted = false;
	ActivePictureBounds m_previousTrusted;
	ActivePictureClassification m_previousTrustedClassification =
		ActivePictureClassification::UNAVAILABLE;
	ActivePictureBounds m_candidate;
	ActivePictureClassification m_candidateClassification =
		ActivePictureClassification::UNAVAILABLE;
	bool m_candidateUsesKnownTrustedGeometry = false;
	uint8_t m_matchingCandidates = 0;
	uint8_t m_contradictoryCandidates = 0;
	uint8_t m_candidateReversals = 0;
	uint8_t m_unavailableCandidates = 0;
	uint64_t m_firstContradictoryFrame = 0;
	uint64_t m_lastAnalyzedFrame = 0;
	double m_stableGeometryDeadbandPercent =
		DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT;
};
