#pragma once

#include <algorithm>
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


// Native is the default for every existing caller. Experimental geometry is
// kept out of remembered trusted formats and can be superseded by fresh native
// authority; provenance alone never grants or extends presentation authority.
enum class ActivePictureAuthorityOrigin : uint8_t
{
    NATIVE, SPARSE_EXPERIMENT, SPARSE_TRANSITION_EXPERIMENT, REMEMBERED_EDGE_RETURN
};

// Current-frame proof for the separate live inward-transition experiment.
// Source generation is checked by the source-scoped renderer/helper; the model
// independently matches its exact established native base and this target.
struct SparseBoundaryTransitionProof
{
    bool available = false;
    ActivePictureBounds establishedBase;
    ActivePictureBounds guardedBounds;
    uint64_t referenceId = 0;
    uint64_t sourceGeneration = 0;
    uint64_t sourceSequence = 0;
};

// A native-only, independently learned prior. Matching a source scan coordinate
// nominates a rectangle; it is not proof of an exact physical picture boundary.
enum class RememberedEdge : uint8_t { TOP, BOTTOM };
struct RememberedEdgeReturnNomination
{
    bool available = false;
    ActivePictureBounds establishedBase, rememberedBounds;
    uint64_t historyId = 0, historyRevision = 0, sourceGeneration = 0;
    uint32_t confirmedSceneCount = 0;
    uint64_t lastIndependentNativeSequence = 0, lastIndependentNativeTickMs = 0;
    RememberedEdge matchedEdge = RememberedEdge::TOP;
    int observedEdgeCoordinate = 0;
    uint64_t sceneId = 0, sourceSequence = 0, timestampMs = 0;
    uint64_t rendererGeneration = 0, viewportGeneration = 0, sourceFormatGeneration = 0, policyGeneration = 0;
    bool shadowOnly = false; // A diagnostic nominee can never carry crop authority.
    bool guarded = false;
};
struct RememberedEdgeReturnProof
{
    bool available = false;
    RememberedEdgeReturnNomination nomination;
    uint64_t sourceSequence = 0, timestampMs = 0;
    bool guarded = false;
    ActivePictureBounds presentationBounds;
};
struct RememberedEdgeReturnContext
{
    bool enabled = false;
    uint64_t sourceGeneration = 0, sceneId = 0, sourceSequence = 0, timestampMs = 0;
    uint64_t rendererGeneration = 0, viewportGeneration = 0, sourceFormatGeneration = 0, policyGeneration = 0;
    bool cadenceRepeat = false, discontinuity = false;
    // Diagnostic history collection only; never authorizes a remembered publication.
    bool shadowOnly = false;
    bool guardedEnabled = false;
};
struct RememberedEdgeReturnHistoryStatus
{
    uint32_t entries = 0, qualifiedEntries = 0, maxConfirmedScenes = 0;
    bool overflowPending = false;
};

// Measurement metadata is distinct from safe fallback coordinates.
enum class ActivePictureAxisState : uint8_t { UNKNOWN, TRUSTED_BARS, FULL_EXTENT_SUPPORTED };
enum class ActivePictureAxisReason : uint8_t
{
	NOT_EVALUATED, SCAN_INCOMPLETE, BAR_EDGE_REJECTED, BAR_ASYMMETRY,
	BAR_CONFIRMED, FULL_EXTENT_SUPPORTED, NO_FULL_EXTENT_SUPPORT,
	BAR_PICTURE_CONTINUATION
};
struct ActivePictureAxisEvidence
{
	ActivePictureAxisState state = ActivePictureAxisState::UNKNOWN;
	ActivePictureAxisReason reason = ActivePictureAxisReason::NOT_EVALUATED;
	bool scanComplete = false;
	bool barCandidate = false;
	bool FailedBar() const { return barCandidate && state == ActivePictureAxisState::UNKNOWN; }
	bool operator==(const ActivePictureAxisEvidence& other) const
	{
		return state == other.state && reason == other.reason &&
			scanComplete == other.scanComplete && barCandidate == other.barCandidate;
	}
};
struct ActivePictureAxisEvidenceSet
{
	ActivePictureAxisEvidence horizontal, vertical;
	// Shared raw vertical aperture for side probes and strict excluded-band
	// inspection. An outward boundary guard retains extra rows without moving
	// the diagnostic side sampling positions. Side-picture minima are bright
	// samples (of 12) in four height zones at three depths.
	int sidePictureWidth = 0, sidePictureHeight = 0;
	int sidePictureTop = 0, sidePictureBottom = 0;
	int sidePictureThreshold = 0;
	int leftPictureMinimum = -1, rightPictureMinimum = -1;
	// Diagnostic distributions over the same height zones at all three depths.
	// Only an actually uncropped source side receives these measurements;
	// dim side distributions do not veto independently verified vertical bars.
	uint8_t leftPictureStrongZoneMask = 0, rightPictureStrongZoneMask = 0;
	int leftPictureNonBlackMinimum = -1, rightPictureNonBlackMinimum = -1;
	bool verticalCropProfileEvaluated = false, verticalCropProfileClean = false;
	// Outward padding retains uncertainty rows; it is never extra inward crop.
	int verticalCropGuardTop = 0, verticalCropGuardBottom = 0;
	bool HasVerticalCropBoundaryGuard() const
	{
		return verticalCropGuardTop != 0 || verticalCropGuardBottom != 0;
	}

	bool HasFailedBar() const { return horizontal.FailedBar() || vertical.FailedBar(); }
	bool SupportsVerticalCropDespiteSideAmbiguity(const ActivePictureBounds& bounds) const
	{
		return horizontal.FailedBar() && horizontal.scanComplete &&
			horizontal.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED &&
			vertical.state == ActivePictureAxisState::TRUSTED_BARS && vertical.scanComplete &&
			bounds.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
			bounds.left == 0 && bounds.right == bounds.rasterWidth &&
			bounds.rasterWidth >= 320 && bounds.rasterHeight >= 180 &&
			bounds.top > 0 && bounds.bottom < bounds.rasterHeight && bounds.bottom > bounds.top &&
			sidePictureWidth == bounds.rasterWidth && sidePictureHeight == bounds.rasterHeight &&
			verticalCropGuardTop >= 0 && verticalCropGuardTop <= std::max(2, bounds.rasterHeight / 540) &&
			verticalCropGuardBottom >= 0 && verticalCropGuardBottom <= std::max(2, bounds.rasterHeight / 540) &&
			sidePictureTop >= bounds.top && sidePictureTop < sidePictureBottom &&
			sidePictureBottom <= bounds.bottom &&
			sidePictureTop - bounds.top == verticalCropGuardTop &&
			bounds.bottom - sidePictureBottom == verticalCropGuardBottom &&
			((!HasVerticalCropBoundaryGuard() && (leftPictureMinimum >= 6 || rightPictureMinimum >= 6)) ||
			 (HasVerticalCropBoundaryGuard() && verticalCropProfileEvaluated && verticalCropProfileClean));
	}
	// Permission to remove only the strictly inspected top/bottom bands while
	// retaining every source column. This does not certify picture at the sides.
	bool HasVerifiedVerticalCropProfile(const ActivePictureBounds& bounds) const
	{
		return verticalCropProfileEvaluated && verticalCropProfileClean &&
			HasVerticalCropBoundaryGuard() && SupportsVerticalCropDespiteSideAmbiguity(bounds);
	}
	bool HasBlockingFailedBar(const ActivePictureBounds& bounds) const
	{
		return HasFailedBar() && !SupportsVerticalCropDespiteSideAmbiguity(bounds);
	}
	bool operator==(const ActivePictureAxisEvidenceSet& other) const
	{
		return horizontal == other.horizontal && vertical == other.vertical &&
			sidePictureWidth == other.sidePictureWidth && sidePictureHeight == other.sidePictureHeight &&
			sidePictureTop == other.sidePictureTop && sidePictureBottom == other.sidePictureBottom &&
			sidePictureThreshold == other.sidePictureThreshold &&
			leftPictureMinimum == other.leftPictureMinimum && rightPictureMinimum == other.rightPictureMinimum &&
			leftPictureStrongZoneMask == other.leftPictureStrongZoneMask &&
			rightPictureStrongZoneMask == other.rightPictureStrongZoneMask &&
			leftPictureNonBlackMinimum == other.leftPictureNonBlackMinimum &&
			rightPictureNonBlackMinimum == other.rightPictureNonBlackMinimum &&
			verticalCropProfileEvaluated == other.verticalCropProfileEvaluated &&
			verticalCropProfileClean == other.verticalCropProfileClean &&
			verticalCropGuardTop == other.verticalCropGuardTop && verticalCropGuardBottom == other.verticalCropGuardBottom;
	}
};
const char* ActivePictureAxisStateName(ActivePictureAxisState state);
const char* ActivePictureAxisReasonName(ActivePictureAxisReason reason);

struct ActivePictureObservation
{
	ActivePictureBounds bounds;
	uint64_t frameNumber = 0;
	bool available = false;
	ActivePictureClassification classification =
		ActivePictureClassification::UNAVAILABLE;
	double framesPerSecond = 60.0;
	// Explicit current-evidence veto, distinct from an uncertain observation.
	// History may identify this shape but must not publish it while deferred.
	bool transitionDeferred = false;
	ActivePictureAxisEvidenceSet axisEvidence;
	ActivePictureAuthorityOrigin authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
	SparseBoundaryTransitionProof sparseTransitionProof;
	RememberedEdgeReturnProof rememberedEdgeReturnProof;

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
	// The authority owned by the published stable contract. This may differ
	// from the current raw observation when a provisional sample exactly
	// reacquires geometry that was trusted earlier in the source generation.
	ActivePictureClassification authoritativeClassification =
		ActivePictureClassification::UNAVAILABLE;
	bool knownTrustedGeometryReacquired = false;
	uint8_t matchingCandidates = 0;
	uint8_t contradictoryCandidates = 0;
	uint8_t candidateReversals = 0;
	double confidence = 0.0;
	uint64_t firstContradictoryFrame = 0;
	uint64_t decisionLatencyFrames = 0;
	std::string reason;
	ActivePictureAuthorityOrigin authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
	// Origin of stableBounds; normally the pre-publication reference. Exact
	// native verification of identical bounds upgrades both origins together.
	ActivePictureAuthorityOrigin stableAuthorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
};


enum class ActivePicturePublicationAdmission
{
	NOT_EVALUATED,
	ACCEPTED,
	DEFERRED,
	NON_AUTHORITATIVE,
	STABLE_REFERENCE_MISMATCH,
	STABLE_GEOMETRY_RETAINED,
	CONTAINED_COMPOSITION_RETAINED,
	STABLE_ASPECT_RETAINED,
	INCOMPLETE_AXIS_RETAINED
};

const char* ActivePicturePublicationAdmissionName(
	ActivePicturePublicationAdmission admission);

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
	// Additional inward-only format tolerance, anchored to accepted geometry.
	// Real mixed-aspect expansions remain outward; only a materially narrower
	// picture can replace an established frame.
	static constexpr double STABLE_ASPECT_DEADBAND_PERCENT = 5.0;

	void Reset();
    // Presentation-only changes discard authority and votes exactly like Reset,
    // while retaining source-bound independent native facts for revalidation.
    void ResetPresentationState();
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
	// Read-only eligibility query for presentation ownership. This deliberately
	// ignores transitionDeferred and advances no temporal proof: callers still
	// need ordinary confirmation/admission before publishing any geometry.
	bool WouldAdmitGeometryChange(const ActivePictureObservation& observation) const;
	// Current native bars may corroborate an existing sparse startup crop.
	// Read-only: this never upgrades provenance, trains history or publishes.
	bool NativeObservationReaffirmsSparseEntry(const ActivePictureObservation& observation,
		const ActivePictureBounds& entry) const;
	// History is a prerequisite, never current pixel or publication authority.
	bool FindRecentTrustedBarGeometry(const ActivePictureBounds& bounds, ActivePictureBounds& remembered) const;
    // Read-only multi-format corroboration; never grants pixel or crop authority.
    bool HasQualifiedNativeAspectPair(const ActivePictureBounds& base, const ActivePictureBounds& target,
        uint64_t sourceGeneration, uint64_t sceneId, uint64_t timestampMs) const;
    static constexpr uint64_t REMEMBERED_RETURN_MAX_AGE_MS = 600000;
    static constexpr uint64_t REMEMBERED_RETURN_MAX_SCENE_DISTANCE = 64;
    // Context.sceneId advances only on distinct confirmed scene edits. Generic
    // candidate resets, accepted sequence numbers, and pauses cannot train it.
    void SetRememberedEdgeReturnContext(const RememberedEdgeReturnContext& context);
    bool RecordIndependentNativeGeometry(const ActivePictureObservation& raw);
    RememberedEdgeReturnNomination NominateRememberedEdgeReturn(
        const ActivePictureBounds& observed, bool topTrusted, bool bottomTrusted) const;
    RememberedEdgeReturnNomination NominateGuardedRememberedEdgeReturn(
        const ActivePictureBounds& observed, bool topTrusted, bool bottomTrusted) const;
    bool AdoptGuardedRememberedReturn(const ActivePictureObservation* observations,
        size_t observationCount, ActivePictureTransitionDecision* outDecision = nullptr);
    RememberedEdgeReturnNomination NominateRememberedEdgeReturnShadow(
        const ActivePictureBounds& observed, bool topTrusted, bool bottomTrusted) const;
    RememberedEdgeReturnHistoryStatus GetRememberedEdgeReturnHistoryStatus() const;
	// Synchronize the live model with a stable decision produced by the bounded
	// queue lookahead model. Invalid or non-authoritative publications fail
	// closed and leave this model unchanged.
	bool AdoptPublishedDecision(
		const ActivePictureTransitionDecision& decision,
		ActivePictureClassification classification,
		bool transitionDeferred = false,
		ActivePicturePublicationAdmission* admission = nullptr,
		const ActivePictureAxisEvidenceSet* currentAxisEvidence = nullptr,
		// Opt in only after independent current-frame outward-picture proof.
		// This relaxes the old reference by one detector step, never the target.
		bool currentOutwardPictureConfirmed = false);

	static uint64_t AnalysisIntervalFrames(double framesPerSecond);
	static bool IsSparseBoundaryInwardTransitionGeometry(const ActivePictureBounds& base,
		const ActivePictureBounds& target, double stableGeometryDeadbandPercent);


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
	ActivePicturePublicationAdmission StableRetentionAdmission(
		const ActivePictureBounds& bounds,
		ActivePictureClassification classification) const;
	bool RetainsIncompleteInwardFormat(const ActivePictureBounds& candidate,
		const ActivePictureAxisEvidenceSet& evidence) const;
	static bool HasCropAuthority(
		const ActivePictureObservation& observation);
	static bool IsFullRaster(
		const ActivePictureBounds& bounds);
	static bool HasAuthorityForCroppedAxes(
		const ActivePictureBounds& bounds);
	void RememberTrustedGeometry(const ActivePictureBounds& bounds,
		ActivePictureClassification classification, ActivePictureAuthorityOrigin origin);
	bool FindRecentTrustedGeometry(const ActivePictureObservation& observation,
		ActivePictureBounds& bounds,
		ActivePictureClassification& classification) const;
	ActivePictureTransitionDecision CommitCandidate(
		const ActivePictureObservation& observation,
		const char* reason);
	bool ValidSparseTransitionObservation(const ActivePictureObservation& observation) const;
	bool SameSparseCandidateProof(const ActivePictureObservation& observation) const;
    RememberedEdgeReturnNomination NominateRememberedEdgeReturnImpl(
        const ActivePictureBounds& observed, bool topTrusted, bool bottomTrusted, bool shadow, bool guarded = false) const;
    bool ValidRememberedEdgeReturnObservation(const ActivePictureObservation& observation) const;
    bool SameRememberedCandidateProof(const ActivePictureObservation& observation) const;
    struct RetiredNativeGeometry
    {
        ActivePictureBounds bounds;
        uint64_t lastTickMs = 0, lastSceneId = 0;
    };
    struct QualifiedNativeGeometry
    {
        ActivePictureBounds bounds;
        uint64_t id = 0, revision = 0, sourceGeneration = 0;
        uint32_t confirmedScenes = 0;
        uint64_t lastSceneId = 0, lastSequence = 0, lastTickMs = 0;
        uint64_t pendingSceneId = 0;
        uint8_t pendingVerifications = 0;
        uint64_t activateAfterSceneId = 0;
        std::array<RetiredNativeGeometry, 3> retired{};
        uint64_t overflowTickMs = 0, overflowSceneId = 0;
    };
    bool RememberedWitnessFresh(uint64_t tick, uint64_t scene) const;
    bool RememberedEdgeAmbiguous(const QualifiedNativeGeometry& entry, bool top) const;
    std::array<QualifiedNativeGeometry, 3> m_qualifiedNativeGeometry{};
    // One bounded staging candidate for family correction or a full qualified cache.
    // It cannot nominate a crop until independently qualified and promoted.
    QualifiedNativeGeometry m_pendingNativeGeometry;
    RememberedEdgeReturnContext m_rememberedContext;
    bool m_rememberedContextFresh = false;
    uint64_t m_nextQualifiedNativeId = 0;
    RememberedEdgeReturnProof m_candidateRememberedProof;
    uint64_t m_candidateRememberedLastSequence = 0;
    bool m_guardedWindowAdmission = false;
    bool m_hasGuardedRememberedEnvelope = false;
    ActivePictureBounds m_guardedRememberedNominal;

	void StartCandidate(const ActivePictureObservation& observation);
	void ClearCandidate();

	ActivePictureAuthorityOrigin m_stableOrigin = ActivePictureAuthorityOrigin::NATIVE;
	ActivePictureAuthorityOrigin m_candidateOrigin = ActivePictureAuthorityOrigin::NATIVE;
	SparseBoundaryTransitionProof m_candidateSparseTransitionProof;
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
	// Cadence correction can present one decoded source sequence repeatedly.
	// Observation confidence is source-frame based, never presentation based.
	uint64_t m_lastObservedFrame = 0;
	double m_stableGeometryDeadbandPercent =
		DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT;
};
