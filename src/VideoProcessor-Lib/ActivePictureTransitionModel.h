#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>


// Serializes reset invalidation with publication of a decision produced from
// an earlier detector generation. Analysis remains worker-owned and lock-free;
// only the short authority mutation is guarded. The callbacks keep the
// generation check inseparable from the corresponding clear/publish write.
class ActivePicturePublicationGate
{
public:
	uint64_t Generation() const
	{
		return m_generation.load(std::memory_order_acquire);
	}

	template <typename ResetCallback>
	uint64_t Reset(ResetCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const uint64_t generation =
			m_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
		callback();
		return generation;
	}

	template <typename PublishCallback>
	bool TryPublish(uint64_t expectedGeneration, PublishCallback callback)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_generation.load(std::memory_order_acquire) != expectedGeneration)
			return false;
		callback();
		return true;
	}

	template <typename ReadCallback>
	void Read(ReadCallback callback) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		callback();
	}

private:
	std::atomic<uint64_t> m_generation = 0;
	mutable std::mutex m_mutex;
};


struct ActivePictureBounds
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	int rasterWidth = 0;
	int rasterHeight = 0;
	double aspectRatio = 0.0;
	enum class BarAxes : uint8_t
	{
		NONE = 0,
		TOP_BOTTOM = 1,
		LEFT_RIGHT = 2,
		BOTH = 3
	};
	BarAxes trustedBarAxes = BarAxes::NONE;
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
	double framesPerSecond = 60.0;
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
	static constexpr double NESTED_CROP_CONFIRMATION_SECONDS = 4.0;
	static constexpr double DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT = 2.0;
	static constexpr double MAX_STABLE_GEOMETRY_DEADBAND_PERCENT = 5.0;

	void Reset();
	// Scene edits invalidate in-flight proof, not the last affirmative geometry.
	// This prevents confirmations from straddling a cut while keeping the stable
	// same-generation reference available to the frame-local presentation policy.
	void ResetCandidateEvidence();
	// A bounded presentation hysteresis. This never grants crop authority; it
	// only retains an already trusted rectangle through a small measured shift.
	void SetStableGeometryDeadbandPercent(double percent);
	static void SetRuntimeStableGeometryDeadbandPercent(double percent);
	static double GetRuntimeStableGeometryDeadbandPercent();
	bool ShouldAnalyze(uint64_t frameNumber, double framesPerSecond);
	ActivePictureTransitionDecision Observe(
		const ActivePictureObservation& observation);
	// Synchronize the live model with a stable decision produced by the bounded
	// queue lookahead model. Invalid or non-authoritative publications fail
	// closed and leave this model unchanged.
	bool AdoptPublishedDecision(
		const ActivePictureTransitionDecision& decision,
		ActivePictureClassification classification);

	static uint64_t AnalysisIntervalFrames(double framesPerSecond);

private:
	struct TrustedGeometry
	{
		ActivePictureBounds bounds;
		ActivePictureClassification classification =
			ActivePictureClassification::UNAVAILABLE;
	};

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
	static bool HasAuthorityForCroppedAxes(
		const ActivePictureBounds& bounds);
	static bool IsNestedOrthogonalCrop(
		const ActivePictureBounds& stable,
		const ActivePictureBounds& candidate);
	void RememberTrustedGeometry(const ActivePictureBounds& bounds,
		ActivePictureClassification classification);
	bool FindRecentTrustedGeometry(const ActivePictureObservation& observation,
		ActivePictureBounds& bounds,
		ActivePictureClassification& classification) const;
	ActivePictureTransitionDecision CommitCandidate(
		const ActivePictureObservation& observation,
		const char* reason);
	void StartCandidate(const ActivePictureObservation& observation);
	void ClearCandidate();

	bool m_hasStable = false;
	ActivePictureBounds m_stable;
	ActivePictureClassification m_stableClassification =
		ActivePictureClassification::UNAVAILABLE;
	// Real feature presentations generally cycle among a small number of
	// aspect modes. Keep only the immediately recent trusted modes within the
	// current source generation; a novel observation receives no history boost.
	static constexpr size_t RECENT_TRUSTED_GEOMETRIES = 3;
	std::array<TrustedGeometry, RECENT_TRUSTED_GEOMETRIES> m_recentTrusted;
	size_t m_recentTrustedCount = 0;
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
