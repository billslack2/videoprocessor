#pragma once

#include <cstdint>
#include <array>
#include <string>

#include "ActivePictureTransitionModel.h"
#include "ActivePictureEvidence.h"
#include "SceneDetector.h"


namespace AlphaSourceCrop
{
	// Measurement equivalence, not new aspect authority. The caller must supply
	// a current affirmative bar observation and a current excluded-band scan for
	// this exact trusted rectangle. Positive outside pixels always veto this.
	bool IsPixelSafeCropReaffirmation(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observedTrustedCrop, bool excludedBandsPixelSafe);

	// The envelope has no crop authority. Suppress it only when both its complete
	// extent and the affirmative observation reaffirm the same pixel-safe crop.
	// Horizontal sampling equivalence is independent of subtitle pixels in the
	// vertical bars, but requires the complete horizontal bands to be safe.
	bool IsPixelSafeHorizontalSamplingEnvelope(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observed, const ActivePicturePresentationRetentionEvidence& evidence);

	bool IsPixelSafeSamplingEnvelope(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observed, const ActivePictureBounds& envelope,
		bool excludedBandsPixelSafe);

	static constexpr uint32_t OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED = 3;

	enum class OutwardPictureProofRoute : uint8_t
	{
		NONE, BROAD_OCCUPANCY, NATIVE_DISTRIBUTED_SUPPORT
	};

	struct OutwardPictureConfirmationState
	{
		ActivePictureBounds candidate;
		ActivePictureBounds base;
		// Confirmation is anchored here; accepted sampling jitter never walks it.
		ActivePictureBounds referenceCandidate;
		// Continuous broad-picture episode age survives candidate proof restarts.
		uint64_t firstPictureSourceSequence = 0;
		uint32_t confirmations = 0;
		// Preserve subtitle ownership across this proof, even if dense FIT later replaces it.
		bool verticalPresentationSeen = false;
		OutwardPictureProofRoute proofRoute = OutwardPictureProofRoute::NONE;
		uint64_t sourceGeneration = 0;
		uint64_t lastObservedSourceSequence = 0;
	};

	struct OutwardPictureConfirmationDecision
	{
		OutwardPictureConfirmationState state;
		bool outwardTransition = false;
		bool broadOpposingPicture = false;
		bool nativeDistributedPicture = false;
		bool authoritative = false;
        // Explanation only; never consumed by crop policy.
        const char* diagnosticReason = "not-evaluated";
	};

	// Expanding a trusted crop changes logical aspect after same-frame broad
	// picture occupancy, or strict current native vertical-bar authority plus
	// distributed source-relative support in both newly exposed vertical strips.
	// Localized UI/text cannot build aspect authority across different frames.
	OutwardPictureConfirmationDecision ConfirmOutwardPictureTransition(
		const OutwardPictureConfirmationState& previous,
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& candidate,
		const ActivePicturePresentationRetentionEvidence& evidence,
		uint64_t sourceGeneration,
		uint64_t sourceSequence = 0);

	enum class BarContentEdge
	{
		NONE,
		TOP,
		BOTTOM,
	};

	// Dense bar inspection is presentation evidence, not format authority. When
	// both encoded bars need displacement, prefer the bottom edge for upward
	// picture movement. The shared active-picture detector remains responsible for
	// genuine two-edge format changes.
	BarContentEdge SelectVerticalBarContentEdge(
		float upperRequiredShift, float lowerRequiredShift);

	enum class VerticalBarPresentationAction
	{
		NONE,
		TRANSLATE,
		FIT,
		FAIL_OPEN,
	};

	struct VerticalBarContentInput
	{
		bool upperContent = false;
		bool lowerContent = false;
		int upperOccupiedDepth = 0;
		int lowerOccupiedDepth = 0;
        // Current sampled row support; black gaps between UI rows are excluded.
        int upperOccupiedRows = 0, lowerOccupiedRows = 0;
		int upperPeakSamples = 0;
		int lowerPeakSamples = 0;
		int upperBarPixels = 0;
		int lowerBarPixels = 0;
		int sampledColumns = 0;
		bool topTranslationHeld = false;
		bool bottomTranslationHeld = false;
		float upperRequiredShift = 0.0f;
		float lowerRequiredShift = 0.0f;
	};

	struct VerticalBarContentDecision
	{
		VerticalBarPresentationAction action =
			VerticalBarPresentationAction::NONE;
		// Negative reveals the upper bar; positive reveals the lower bar.
		float translationPixels = 0.0f;
		bool upperOverlayLike = false;
		bool lowerOverlayLike = false;
	};

	// One shallow or localized occupied bar is subtitle/UI-like and may translate
	// the stable source window. Opposing or broad-and-deep content belongs to the
	// fit/aspect path. This is deliberately bounded geometry, not OCR or ML.
	VerticalBarContentDecision EvaluateVerticalBarContent(
		const VerticalBarContentInput& input);

    // Off reveals measured content by fitting its bounded envelope. Select that
    // action BEFORE confirmation: a changing UI extent is not a Classic motion
    // target. Classic reveals any measured bar content by translation, bottom
    // first; advanced styles retain their shared overlay classification.
    VerticalBarContentDecision SelectVerticalBarContentPresentation(
        VerticalBarContentDecision observed, bool translationEnabled,
        const VerticalBarContentInput* current = nullptr, bool classicBarContent = false);


	// Subtitle extent often grows over the first few decoded frames. Confirm a
	// new or larger translation on three consecutive dense analysis samples before
	// exposing it to the presentation interpolator. This is deliberately based
	// on analyzed observations rather than wall-clock time or raw frame count.
	static constexpr uint32_t VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED = 3;
	static constexpr float VERTICAL_TRANSLATION_STABILITY_PIXELS = 2.0f;

	struct VerticalTranslationConfirmationState
	{
		float candidateTranslationPixels = 0.0f;
		uint32_t confirmations = 0;
		uint64_t lastObservedSourceSequence = 0;
	};

	struct VerticalTranslationConfirmationInput
	{
		VerticalTranslationConfirmationState previous;
		VerticalBarContentDecision observed;
		bool acceptedTranslationActive = false;
		float acceptedTranslationPixels = 0.0f;
		// A nonnegative outward reserve applied only when a target is accepted.
		// The accepted reserve then absorbs later same-direction detector growth
		// without exposing another presentation target.
		float targetBufferPixels = 0.0f;
		// Positive values cap the buffered magnitude at the source raster edge.
		// Zero leaves the policy uncapped for callers without geometry context.
		float maximumTranslationMagnitudePixels = 0.0f;
		// A cadence repeat may render one decoded source frame more than once.
		// One source sequence can contribute at most one confirmation.
		uint64_t sourceSequence = 0;
	};

	struct VerticalTranslationConfirmationDecision
	{
		VerticalTranslationConfirmationState state;
		VerticalBarContentDecision effective;
		bool pending = false;
		bool newlyAccepted = false;
	};

	VerticalTranslationConfirmationDecision ConfirmVerticalTranslation(
		const VerticalTranslationConfirmationInput& input);

	// Dense picture-like evidence is deliberately conservative in the opposite
	// direction from subtitle translation. One contradictory bar scan must not
	// resize an established scope presentation; two consecutive analyzed scans
	// are still fast enough to reveal genuine live pixels within a bounded delay.
	// Trusted active-picture/full-raster authority does not use this path.
	static constexpr uint32_t VERTICAL_FIT_CONFIRMATIONS_REQUIRED = 2;

	struct VerticalFitConfirmationState
	{
		uint32_t confirmations = 0;
		uint64_t lastObservedSourceSequence = 0;
	};

	struct VerticalFitConfirmationDecision
	{
		VerticalFitConfirmationState state;
		VerticalBarContentDecision effective;
		bool pending = false;
		bool newlyAccepted = false;
	};

	VerticalFitConfirmationDecision ConfirmVerticalFit(
		const VerticalFitConfirmationState& previous,
		const VerticalBarContentDecision& observed,
		uint64_t sourceSequence = 0, bool requireRepeatedSamples = true);

	// A current provisional envelope which expands one or both vertical edges is
	// eligible for trusted-base retention until the first dense sample. This
	// closes the pre-analysis frame without treating coarse vertical geometry as
	// subtitle motion or confirmed picture fill. Horizontal involvement remains
	// fail-open.
	bool ShouldRetainTrustedBaseForVerticalInspection(
		bool subtitleFitEnabled,
		bool currentEnvelope,
		bool latestObservationCanAwaitInspection,
		bool leftExpansion,
		bool topExpansion,
		bool rightExpansion,
		bool bottomExpansion);

	// Dynamic detector geometry may reposition a presentation only when the
	// operator explicitly enabled automatic crop or subtitle containment.
	// Merely configuring a fixed aspect must not opt into either behavior.
	bool ShouldTrackDynamicPresentationGeometry(
		bool automaticCropEnabled, bool subtitleFitEnabled);

	struct VerticalInspectionBridgeState
	{
		bool active = false;
		bool retentionConsumed = false;
		bool denseAnalysisCompleted = false;
		bool failOpenLatched = false;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		ActivePictureBounds trustedBase;
		uint64_t firstCandidateSourceSequence = 0;
		uint64_t retainedSourceSequence = 0;
	};

	// A spent vertical-inspection bridge may be resolved by an already accepted
	// dense Fit only when the same source sample supplies a valid, full-width,
	// vertical-only outward envelope. This deliberately excludes provisional,
	// horizontal, and held-fit evidence.
	struct VerticalInspectionFitResolutionInput
	{
		bool confirmedDenseFit = false;
		bool denseAnalysisCurrent = false;
		bool outwardExpansionAvailable = false;
		bool currentHorizontalExpansion = false;
		ActivePictureBounds trustedBase;
		ActivePictureBounds outwardExpansion;
		uint64_t outwardExpansionSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
	};

	bool CanResolveVerticalInspectionWithConfirmedFit(
		const VerticalInspectionFitResolutionInput& input);

	struct VerticalInspectionBridgeInput
	{
		VerticalInspectionBridgeState previous;
		bool candidate = false;
		bool retentionRequested = false;
		bool denseAnalysisCompleted = false;
		// A current dense result has handed presentation to the bounded
		// translation/Fit confirmation path. That path owns the remaining
		// confirmation frames, so the inspection bridge must not fail open first.
		bool verticalPresentationOwnerAvailable = false;
		// Positive crop/full-raster authority, or a verified current vertical Fit,
		// closes the unresolved episode.
		// A mere coarse-candidate dropout does not, because sparse dark pixels
		// must not rearm alternating scope/full/scope decisions.
		// Current sampling-retention proof closes only this presentation
		// inspection; it does not establish fresh crop authority.
		bool samplingRetentionResolved = false;
		// Current contained pixels may clear an obsolete latch; recovery still owns dwell.
		bool containedRetentionResolved = false;
		bool cropAuthorityResolved = false;
		bool fullRasterAuthorityResolved = false;
		bool confirmedVerticalFitResolved = false;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		ActivePictureBounds trustedBase;
		uint64_t sourceSequence = 0;
	};

	struct VerticalInspectionBridgeDecision
	{
		VerticalInspectionBridgeState state;
		bool retain = false;
		bool started = false;
		bool expired = false;
	};

	// Blind retention is limited to the short, bounded dense-analysis handoff of
	// one unresolved authority episode. Repeated presentation is idempotent, and
	// the bridge cannot outlive three decoded source sequences. Candidate dropouts
	// do not rearm it; only resolved authority, a new trusted base/profile epoch,
	// or a new source generation can do that. Once dense classification completes,
	// later frames need an explicit translation/Fit owner.
	VerticalInspectionBridgeDecision UpdateVerticalInspectionBridge(
		const VerticalInspectionBridgeInput& input);

	struct VerticalBarPresentationState
	{
		VerticalBarPresentationAction action =
			VerticalBarPresentationAction::NONE;
		int detectedTop = 0;
		int detectedBottom = 0;
		float translationPixels = 0.0f;
		uint64_t lastDetectionTick = 0;
		uint64_t sourceSequence = 0;
		// FIT-only ages of the actual retained extreme at each edge. Translation
		// retains its established cue ownership and uses the action timer above.
		uint64_t fitTopExtentTick = 0, fitBottomExtentTick = 0;
	};

	struct VerticalBarPresentationUpdateInput
	{
		VerticalBarPresentationState previous;
		VerticalBarContentDecision current;
		bool upperContent = false;
		bool lowerContent = false;
		int upperContentTop = 0;
		int lowerContentBottom = 0;
		uint64_t currentTick = 0;
		uint64_t currentSourceSequence = 0;
		uint64_t holdMs = 0;
		bool translationEnabled = false;
		// The previous action owns classification of this scheduled sample even
		// when holdMs is zero. An analyzed NONE still releases immediately.
		bool previousOwnsCurrentAnalysis = false;
		// Set only after eligible retiring-owner inspection completed on this frame.
		bool retiringTranslationFitInspection = false;
	};

	bool IsVerticalBarPresentationActive(
		const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence);

	// A zero hold means release on the next negative analysis result, not on a
	// rendered frame which was deliberately skipped by the bounded analyzer.
	// Retention between scheduled samples is allowed only for the exact current
	// bar authority and source generation.
	bool IsVerticalBarPresentationActiveForFrame(
		const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence,
		bool analysisEvaluatedCurrentFrame,
		bool currentBarAuthority,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration);

	// A bar detector can briefly lose crop authority while the active-picture
	// classifier verifies the same frame-local overlay. Preserve a known
	// presentation action only within its source generation and bounded release
	// hold; a new source generation must always reacquire authority first.
	bool CanRetainVerticalBarPresentationAcrossAuthorityGap(
		const VerticalBarPresentationState& state,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence);

	// A subtitle/OSD presentation may make pixels in both encoded bars resemble
	// a new symmetric picture. While a same-generation translation (including
	// its release drift) owns those pixels, keep the trusted base and outward
	// envelope. Once ownership ends, normal confirmations may publish the change.
	bool ShouldDeferVerticalGeometryTransition(
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& candidateGeometry,
		ActivePictureClassification candidateClassification,
		const VerticalBarPresentationState& presentation,
		bool translationDriftActive,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration);

	// Current evidence decides whether logical geometry may change before the
	// renderer considers local history or a queued look-ahead publication.
	struct TransitionAdmissionInput
	{
		ActivePictureEvidence evidence;
		ActivePictureBounds outwardCandidate;
		ActivePictureBounds presentationBeforeObservation;
		ActivePictureBounds trustedGeometry;
		bool trustedGeometryAvailable = false;
		bool compatiblePresentation = false;
		uint64_t trustedGeneration = 0;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
		double framesPerSecond = 60.0;
		VerticalBarPresentationState presentation;
		bool translationDriftActive = false;
		uint64_t presentationEvidenceGeneration = 0;
		ActivePicturePresentationRetentionEvidence retention;
		OutwardPictureConfirmationState previousOutward;
	};

	struct TransitionAdmissionDecision
	{
		ActivePictureObservation observation;
		OutwardPictureConfirmationDecision outward;
		bool deferPresentation = false;
		bool deferOutward = false;
		// Unresolved margins defer composition changes unless the exact vertical
		// crop has current strict excluded-band proof and retains full source width.
		bool deferPartialComposition = false;
	};
	TransitionAdmissionDecision EvaluateTransitionAdmission(const TransitionAdmissionInput& input);

	// Frame-local arbitration only: wait for the existing model to publish a
	// broad vertical picture transition instead of briefly applying subtitle FIT.
	// The last outward-proof sample is the first model sample; the final
	// model sample publishes instead of retaining. No extra hold is added.
	static constexpr uint64_t PICTURE_HANDOFF_PENDING_FRAMES =
		OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED +
		ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS - 2;

	struct PictureTransitionHandoff
	{
		bool active = false;
		ActivePictureBounds trustedBase;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
		uint64_t presentationEpoch = 0;
	};

	PictureTransitionHandoff MakePictureTransitionHandoff(
		const TransitionAdmissionInput& input, const TransitionAdmissionDecision& admission,
		const ActivePictureTransitionDecision& transition, bool eligibleGeometryChange,
		uint64_t presentationEpoch);

	struct PresentationObservationDecision
	{
		ActivePictureBounds bounds;
		uint8_t resolvedAxes = 0;
	};
	// Fresh pixel evidence can replace full-raster placeholders on an
	// uncertified axis for presentation only. This never grants crop authority.
	PresentationObservationDecision ResolvePresentationObservation(
		const ActivePictureBounds& base, const ActivePictureEvidence& evidence,
		const ActivePicturePresentationRetentionEvidence& retention);

	bool HasHorizontalCropRefinementConflict(bool currentLeftExpansion,
		bool currentRightExpansion, bool observationAvailable, bool geometryAvailable,
		bool samplingReaffirmed, const ActivePictureBounds& observation,
		const ActivePictureBounds& geometry);

	struct HeldBarAnalysisInput
	{
		bool currentBarAuthority = false;
		bool trustedBarGeometryAvailable = false;
		bool storedBaseMatchesTrustedGeometry = false;
		bool currentEnvelopeAvailable = false;
		ActivePictureClassification latestClassification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds trustedGeometry;
		ActivePictureBounds currentEnvelope;
		VerticalBarPresentationState presentation;
		bool translationConfirmationPending = false;
		float pendingTranslationPixels = 0.0f;
		bool fitConfirmationPending = false;
		uint64_t evidenceSourceGeneration = 0;
		uint64_t currentSourceGeneration = 0;
		uint64_t currentTick = 0;
		uint64_t holdMs = 0;
		uint64_t currentSourceSequence = 0;
	};

	// Provisional pixels may be inspected against a previously trusted bar
	// rectangle without acquiring crop authority. This bridge exists only while
	// the current envelope expands the same edge as an active, same-generation
	// translation, or while a two-edge dense Fit candidate awaits its second
	// analyzed sample. Contradictory or unavailable evidence must reacquire
	// normally.
	bool CanAnalyzeHeldVerticalBarGeometry(
		const HeldBarAnalysisInput& input);

	// Eligibility grants a fresh dense scan, never crop or presentation authority.
	bool CanInspectRetiringTranslationFit(const HeldBarAnalysisInput& input,
		const VerticalFitConfirmationState& fit, bool inspectionBlocked);

	// Store exactly one held vertical action. FIT retains the widest measured
	// extents; TRANSLATE retains the farthest same-direction displacement.
	VerticalBarPresentationState UpdateVerticalBarPresentation(
		const VerticalBarPresentationUpdateInput& input);

	// A held translation may legitimately predate the current frame when a
	// competing dense Fit was rejected. Current-frame envelope evidence can
	// still attest the overlay, but only when it expands exactly the translated
	// edge and leaves both horizontal and the opposite vertical edge unchanged.
	bool CurrentTranslationEnvelopeSupportsGeometry(
		const VerticalBarPresentationState& presentation,
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& currentEnvelope);

	// Interpolates only a translation which the existing subtitle policy has
	// already selected. Callers supply the active or release duration; zero
	// snaps to the target. Retargeting starts from the current applied position.
	class VerticalTranslationDrift
	{
	public:
		void Reset();
		float Resolve(float targetTranslationPixels, uint64_t currentTick,
			uint64_t durationMs);
		bool IsActive() const { return driftActive; }
        uint64_t ActiveStartTick() const { return driftActive ? driftStartTick : 0; }
		bool TryGetActiveInterval(float& start, float& target, float& applied) const
		{
			if (!driftActive) return false;
			start = driftStartTranslationPixels;
			target = targetTranslationPixels;
			applied = lastAppliedTranslationPixels;
			return true;
		}
		// The sample which lands exactly on the trusted base needs one final
		// generation-checked presentation decision. Consume this marker once so a
		// lost current observation cannot cause a full-raster flash at release.
		bool ConsumeFinalBaseFrame();

	private:
		float lastAppliedTranslationPixels = 0.0f;
		float targetTranslationPixels = 0.0f;
		float driftStartTranslationPixels = 0.0f;
		uint64_t driftStartTick = 0;
		bool driftActive = false;
		bool finalBaseFramePending = false;
	};

	// Presentation-only authority for a current native sampling refinement.
	// Never changes logical crop authority or reuses an older envelope.
	struct SamplingRefinementOutwardFitInput
	{
		ActivePictureBounds base, rawBounds, visibleBase, visibleBounds, outwardBounds;
		ActivePictureClassification baseClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureClassification rawClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureAuthorityOrigin baseOrigin = ActivePictureAuthorityOrigin::NATIVE;
		ActivePictureAuthorityOrigin rawOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t frameGeneration = 0, frameSequence = 0, baseGeneration = 0;
		uint64_t rawSequence = 0, visibleGeneration = 0, visibleSequence = 0;
		bool rawAvailable = false, visibleAvailable = false, outwardAvailable = false;
		bool nearBlackEvaluated = false, nearBlack = false, moving = false;
		bool competingOwner = false, denseArbitrationEnabled = false;
	};
	bool CanUseSamplingRefinementOutwardFit(const SamplingRefinementOutwardFitInput& input);

	struct ApprovedGenericFitHoldState
	{
		bool available = false;
		ActivePictureBounds base, bounds;
		uint64_t sourceGeneration = 0, presentationEpoch = 0;
		uint64_t admittedSequence = 0, admittedTick = 0;
	};
	struct ApprovedGenericFitHoldInput
	{
		ApprovedGenericFitHoldState previous;
		ActivePictureBounds base, rawBounds, visibleBounds, retentionBase;
		ActivePictureClassification baseClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureClassification rawClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureAuthorityOrigin baseOrigin = ActivePictureAuthorityOrigin::NATIVE;
		ActivePictureAuthorityOrigin rawOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t sourceGeneration = 0, sourceSequence = 0, presentationEpoch = 0;
		uint64_t currentTick = 0, holdMs = 0, baseGeneration = 0, rawSequence = 0;
		uint64_t retentionGeneration = 0, retentionSequence = 0;
		bool rawAvailable = false, retentionValid = false, visibleAvailable = false;
		bool excludedBandsSafe = false, nearBlackEvaluated = false, nearBlack = false;
		bool moving = false, competingOwner = false, denseArbitrationEnabled = false;
	};
	struct ApprovedGenericFitHoldDecision
	{
		ApprovedGenericFitHoldState state;
		bool active = false;
		ActivePictureBounds bounds;
	};
	// Retain only a previously admitted generic BOTH-edge FIT. Queries never renew
	// its age; callers retire it if another final owner or rectangle wins.
	ApprovedGenericFitHoldDecision ResolveApprovedGenericFitHold(const ApprovedGenericFitHoldInput& input);
	ApprovedGenericFitHoldState RecordApprovedGenericFitHold(const ApprovedGenericFitHoldInput& input,
		const ActivePictureBounds& candidate, const ActivePictureBounds& admitted,
		bool freshGenericBothEdgeApproved, bool admissionSucceeded);

	struct VerticalBarPresentationResolutionInput
	{
		VerticalBarPresentationAction detailedAction =
			VerticalBarPresentationAction::NONE;
		float translationPixels = 0.0f;
		bool zeroTranslationRetainsTrustedBase = false;
		bool genericUpperExpansion = false;
		bool genericLowerExpansion = false;
		// A retained union of unrelated top/bottom overlays is not aspect-ratio
		// authority. Generic vertical FIT requires both edges on this frame and
		// current trusted active-picture authority; provisional evidence may only
		// retain or fail open, never resize the picture.
		bool genericVerticalFitConfirmed = false;
		bool genericVerticalFitAuthoritative = false;
		// When the dense subtitle/bar-content pass is enabled, a coarse current
		// two-edge envelope is inspection evidence only. Dense classification and
		// its confirmation policy must explicitly select FIT.
		bool denseVerticalArbitrationEnabled = false;
		int genericUpperBound = 0;
		int genericLowerBound = 0;
		int authoritativeTop = 0;
		int authoritativeBottom = 0;
		int rasterHeight = 0;
	};

	struct VerticalBarPresentationResolution
	{
		VerticalBarPresentationAction action =
			VerticalBarPresentationAction::NONE;
		float translationPixels = 0.0f;
	};

	// Reconcile the dense bar pass with the generic presentation envelope before
	// changing geometry. TRANSLATE and vertical FIT are mutually exclusive.
	VerticalBarPresentationResolution ResolveVerticalBarPresentation(
		const VerticalBarPresentationResolutionInput& input);

	struct VerticalBarRendererRouting
	{
		bool translationActive = false;
		int translationPixels = 0;
		bool fitActive = false;
		bool failOpen = false;
	};

	// Convert the reconciled presentation action into the mutually exclusive
	// renderer inputs consumed by Evaluate(). Keeping this seam shared by the
	// renderer and tests prevents a valid TRANSLATE policy decision from being
	// silently rewired into a scale-changing outward Fit.
	VerticalBarRendererRouting ResolveVerticalBarRendererRouting(
		const VerticalBarPresentationResolution& resolution);

	// Retain an already committed full raster through missing dark-frame geometry.
	// This never establishes startup geometry. After revocation, a new commit or
	// fresh affirmative measurements must revalidate the same committed context;
	// a cached old full-raster rectangle alone cannot re-arm it.
	struct KnownFullRasterRetentionState
	{
		bool available = false;
		int rasterWidth = 0;
		int rasterHeight = 0;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		uint64_t lastEvaluatedSequence = 0;
		uint64_t lastCommittedSequence = 0;
		uint8_t reaffirmationSamples = 0;
	};
	struct KnownFullRasterRetentionInput
	{
		KnownFullRasterRetentionState previous;
		bool analysisValid = false;
		bool measurementCurrent = false;
		bool cadenceRepeat = false;
		bool sceneBoundary = false;
		// Unlike safeBoundary, this is not suppressed by scene-notification cooldown.
		bool independentCutEvidence = false;
		bool nearBlackEvaluated = false;
		bool globalNearBlack = false;
		ActivePictureClassification rawClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds rawBounds;
		int frameWidth = 0;
		int frameHeight = 0;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
		uint64_t presentationEpoch = 0;
		bool committedFullAvailable = false;
		ActivePictureBounds committedBounds;
		uint64_t committedSourceGeneration = 0;
		uint64_t committedSourceSequence = 0;
		uint64_t committedPresentationEpoch = 0;
	};
	KnownFullRasterRetentionState UpdateKnownFullRasterRetention(
		const KnownFullRasterRetentionInput& input);

	// Shared renderer/test scene routing; updates the scene fields in input.
	KnownFullRasterRetentionState UpdateKnownFullRasterRetentionForScene(
		KnownFullRasterRetentionInput& input, const SceneDetectorResult& scene,
		bool retainFullRasterAtDarknessBoundary);

	// A generic safeBoundary also marks entry into darkness. Only this narrow
	// predicate permits retaining an existing full raster at that event; an
	// independent immediate/pending/confirmed cut always uses normal policy.
	struct KnownFullRasterDarknessBoundaryInput
	{
		KnownFullRasterRetentionInput retention;
		bool safeBoundary = false;
		bool nearBlackEntry = false;
		bool differenceEvaluated = false;
		bool hardCutCandidate = false;
		bool hardCutConfirmed = false;
	};
	bool CanRetainKnownFullRasterAtDarknessBoundary(
		const KnownFullRasterDarknessBoundaryInput& input);


	// Full raster is always outward-safe. Keep that presentation authority
	// between sparse analysis samples. Native trusted bars withdraw it promptly;
	// a pending sparse transition preserves it until normal publication.
	// Ambiguity cannot turn it into crop authority.
	bool UpdateFullRasterPresentationAuthority(bool previouslyAuthoritative,
		ActivePictureClassification currentClassification,
		bool currentBoundsAreFullRaster,
		ActivePictureAuthorityOrigin origin = ActivePictureAuthorityOrigin::NATIVE);

	// Call only when the renderer commits an unsuppressed model publication.
	bool CommitSparseTransitionPresentationAuthority(bool previouslyAuthoritative,
		const ActivePictureTransitionDecision& decision);

	// A pending contained candidate cannot reclassify or rearm its old base.
	bool SparseTransitionMayReaffirmOwnedCrop(const ActivePictureBounds& owned,
		ActivePictureAuthorityOrigin ownedOrigin, const ActivePictureEvidence& evidence);

	bool RequiresPerFramePresentationInspection(
		bool trustedCropIsCurrentGeneration,
		bool sceneSnapshotIsCurrentGeneration,
		bool pixelSafeRetentionActive);

	// A retained crop which has just become pixel-unsafe needs an immediate
	// low-cost scan of its already-trusted bars. Once a translation is active,
	// normal cadence resumes because the held placement already covers the cue.
	bool RequiresImmediateSubtitleBarAnalysis(bool currentBarAuthority,
		bool retentionJustBecameUnsafe,
		bool frameLocalRetentionEvaluated, bool frameLocalRetentionSafe,
		bool translationAlreadyActive);

	struct SubtitleInspectionState
	{
		uint64_t sourceGeneration = 0;
		ActivePictureBounds base;
		bool verticalUnsafe = false;
	};
	struct SubtitleInspectionInput
	{
		SubtitleInspectionState previous;
		bool barAuthorityAvailable = false;
		bool measurementCurrent = false;
		bool translationAlreadyActive = false;
		uint64_t sourceGeneration = 0;
		ActivePictureBounds base;
		ActivePictureBounds measuredBase;
		ActivePicturePresentationRetentionEvidence retention;
	};
	struct SubtitleInspectionDecision
	{
		SubtitleInspectionState state;
		bool forceAnalysis = false;
	};
	SubtitleInspectionDecision UpdateSubtitleInspection(const SubtitleInspectionInput& input);

    struct PresentationEnvelopeExtentState
    {
        bool available = false;
        ActivePictureBounds base, bounds;
        uint64_t sourceGeneration = 0, sourceSequence = 0, lastDetectionTick = 0;
        uint64_t lastObservationSequence = 0, lastObservationTick = 0;
        // Left, top, right, bottom: age of the actual retained outer extent.
        std::array<uint64_t,4> extentTicks{};
    };
    PresentationEnvelopeExtentState UpdatePresentationEnvelopeExtents(
        const PresentationEnvelopeExtentState& previous,
        const ActivePictureBounds& base, const ActivePictureBounds& current,
        uint64_t sourceGeneration, uint64_t sourceSequence,
        uint64_t now, uint64_t holdMs);
    struct PresentationEnvelopeSelectionInput
    {
        PresentationEnvelopeExtentState history;
        ActivePictureBounds effectiveBase, currentBounds;
        uint64_t frameGeneration=0, frameSequence=0, currentGeneration=0, currentSequence=0;
        bool envelopeActive=false;
    };
    ActivePictureBounds SelectPresentationEnvelopeBounds(const PresentationEnvelopeSelectionInput& input);
    // Generic FIT may retain only sampling-equivalent vertical extents;
    // current pixels always win for outward growth or a different contract.
    ActivePictureBounds SelectGenericFitEnvelope(const PresentationEnvelopeSelectionInput& input);
	struct PresentationEnvelopeInput
	{
		bool envelopeAvailable = false;
		bool effectiveGeometryAvailable = false;
		bool baseMatchesEffectiveGeometry = false;
		uint64_t detectedSourceSequence = 0;
		uint64_t currentSourceSequence = 0;
		uint64_t evidenceSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		uint64_t lastDetectionTick = 0;
		uint64_t currentTick = 0;
		uint64_t holdMs = 0;
	};

	struct PresentationEnvelopeDecision
	{
		bool active = false;
		bool currentFrame = false;
		bool held = false;
		const char* reason = "envelope evidence is unavailable";
	};

	// A frame-local envelope is safe to union with whichever trusted geometry
	// that same frame finally publishes. Historical envelopes remain tied to
	// their exact base geometry and bounded release hold.
	PresentationEnvelopeDecision EvaluatePresentationEnvelope(
		const PresentationEnvelopeInput& input);

	struct PresentationEnvelopeGeometryInput
	{
		ActivePictureBounds trustedPicture;
		ActivePictureBounds observedContent;
		bool observedContentAvailable = false;
		bool expandLeft = false;
		bool expandTop = false;
		bool expandRight = false;
		bool expandBottom = false;
		int horizontalPadding = 0;
		int verticalPadding = 0;
	};

	struct PresentationEnvelopeGeometryDecision
	{
		ActivePictureBounds bounds;
		bool valid = false;
		bool expanded = false;
		const char* reason = "trusted picture is invalid";
	};

	// Source-space presentation is the immutable trusted picture unioned only
	// with selected, bounded content edges and padding. Destination aspect is
	// deliberately absent from this contract.
	PresentationEnvelopeGeometryDecision BuildPresentationEnvelope(
		const PresentationEnvelopeGeometryInput& input);

	struct PresentationEnvelopeContent
	{
		ActivePictureBounds bounds;
		bool expandLeft = false;
		bool expandTop = false;
		bool expandRight = false;
		bool expandBottom = false;
	};

	struct PresentationEnvelopeCompositionInput
	{
		ActivePictureBounds trustedPicture;
		PresentationEnvelopeContent detectorContent;
		PresentationEnvelopeContent denseContent;
        bool measuredVerticalFit = false; // Off: dense bar pixels own vertical extents.
		int horizontalPadding = 0;
		int verticalPadding = 0;
	};

	// The renderer supplies already-routed edges and source/base-validated
	// evidence. Pad raw dense edges once, then union with the detector's selected
	// bounds without extra padding. Flags define active components: invalid active
	// bounds fail; unselected components do not participate. No crop authority or
	// temporal state is created here.
	PresentationEnvelopeGeometryDecision BuildComposedPresentationEnvelope(
		const PresentationEnvelopeCompositionInput& input);

	struct PresentationRect
	{
		double left = 0.0;
		double top = 0.0;
		double right = 0.0;
		double bottom = 0.0;
	};

	enum class UnusedSpaceAxis
	{
		NONE,
		HORIZONTAL,
		VERTICAL,
		INVALID,
	};

	struct CenteredFitDecision
	{
		PresentationRect picture;
		UnusedSpaceAxis unusedAxis = UnusedSpaceAxis::INVALID;
		bool valid = false;
	};

	enum class VerticalPictureAlignment
	{
		TOP,
		CENTER,
		BOTTOM,
	};

	const char* VerticalPictureAlignmentName(
		VerticalPictureAlignment alignment);

	// Preserve aspect while centering horizontally and choosing where any
	// unused vertical destination space rests. Horizontal-only unused space is
	// always centered, and an equal-height fit makes all three modes identical.
	CenteredFitDecision FitAspect(double contentAspect,
		const PresentationRect& screen,
		VerticalPictureAlignment verticalAlignment);

	// anamorphicScale describes the physical lens's horizontal expansion. The
	// renderer must pre-compress by its reciprocal so the projected picture
	// returns to the source proportions after passing through the lens.
	inline double ApplyAnamorphicLensCompensation(
		double sourceAspect, double anamorphicScale)
	{
		return sourceAspect / anamorphicScale;
	}

	// Perform the one ordinary centered aspect-preserving fit used after source
	// envelope selection. The caller may pass anamorphic-adjusted contentAspect;
	// that destination-only mapping never feeds back into source geometry.
	CenteredFitDecision FitCenteredAspect(
		double contentAspect, const PresentationRect& screen);
	const char* UnusedSpaceAxisName(UnusedSpaceAxis axis);

	// A viewport may independently opt into filling its physical screen after
	// current trusted active-picture authority has accepted either a bar crop or
	// the full raster. The optional narrower limit is a minimum content aspect;
	// the optional wider limit is a maximum content aspect. Neither limit makes
	// provisional or stale geometry eligible for cropping.
	struct AspectLimitFillInput
	{
		bool trustedContentAuthorityAccepted = false;
		bool cropNarrowerContentToFillScreen = false;
		bool narrowerLimitConfigured = false;
		double narrowerAspectLimit = 0.0;
		bool cropWiderContentToFillScreen = false;
		bool widerLimitConfigured = false;
		double widerAspectLimit = 0.0;
		double screenAspect = 0.0;
		ActivePictureBounds sourceBounds;
        // Optional independently accepted content, before overlay/retention expansion.
        // This may veto fill; it never changes the actual bounds used for cropping.
        bool contentReferenceAvailable = false;
        ActivePictureBounds contentReferenceBounds;
	};

	struct AspectLimitFillDecision
	{
		ActivePictureBounds sourceBounds;
		bool applied = false;
        bool samplingEnvelopeFilled = false;
        // Reuses only side fill that already reached successful submission.
        bool retainedSubmittedFill = false;
        bool contentEligible = false;
		double contentAspect = 0.0;
        double referenceAspect = 0.0;
		std::string reason;
	};

	AspectLimitFillDecision EvaluateAspectLimitFill(
		const AspectLimitFillInput& input);

    struct Input;

    // Fill-only profiles may remove bars only for eligible trusted content.
    // The content reference is the movie picture, never a subtitle envelope.
    bool ShouldApplyDynamicSourceCrop(bool automaticCropRequested,
        bool subtitleFitEnabled, const Input& crop, const AspectLimitFillInput& fill);

	struct FixedAspectCropInput
	{
		double fixedAspect = 0.0;
		ActivePictureBounds sourceBounds;
	};

	// Center-crop the current presentation bounds to an operator-selected source
	// aspect. This is unconditional while configured: automatic crop authority
	// may refine the input bounds, but its expiry must not disable the fixed crop.
	// The regular physical-screen fit happens after this source decision.
	AspectLimitFillDecision EvaluateFixedAspectCrop(
		const FixedAspectCropInput& input);

	struct ProfileTransitionRetentionInput
	{
		bool geometryAvailable = false;
		ActivePictureClassification classification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds geometry;
		uint64_t geometrySourceGeneration = 0;
		uint64_t analysisSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		bool sourceFormatMatches = false;
	};

	struct ProfileTransitionRetentionDecision
	{
		bool retainSourceGeometry = false;
		// Subtitle motion and NLS mapping are presentation state, not source
		// geometry. They always reacquire for the newly selected profile.
		bool retainSubtitleState = false;
		bool retainNlsPresentationIntent = false;
	};

	// A Screen/Zoom profile change does not alter the source pixels. Bridge the
	// first frame with generation-current trusted bar geometry. A profile which
	// does not consume active-picture geometry may keep that source-only snapshot
	// dormant; it is force-verified before a later profile can present it. Every
	// piece of profile-dependent presentation state starts a fresh epoch.
	ProfileTransitionRetentionDecision EvaluateProfileTransitionRetention(
		const ProfileTransitionRetentionInput& input);

	class AmbiguityHold
	{
	public:
		void Reset();
		void Observe(uint64_t currentTick, uint64_t sourceGeneration,
			bool hadCurrentTrustedCrop, bool trustedCropReaffirmed,
			ActivePictureClassification classification,
			uint64_t maximumHoldMs);
		bool IsActive(uint64_t currentTick,
			uint64_t sourceGeneration) const;

	private:
		uint64_t deadlineTick = 0;
		uint64_t ownerSourceGeneration = 0;
		bool eligibleAfterTrustedCrop = false;
	};

	enum class NearBlackPresentationMode
	{
		INACTIVE,
		RETAIN_CROP,
		FULL_RASTER,
	};

	struct NearBlackPresentationEpisodeState
	{
		NearBlackPresentationMode mode = NearBlackPresentationMode::INACTIVE;
		uint64_t sourceGeneration = 0;
		uint64_t startedSourceSequence = 0;
		uint64_t presentationEpoch = 0;
		// Keep native acquisition available for episodes that began at full raster.
		bool startedAtFullRaster = false;
		bool entryTrustedCropAvailable = false;
		ActivePictureBounds entryTrustedCrop;
		ActivePictureAuthorityOrigin entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t fullRasterStartedSourceSequence = 0;
		bool confirmedNonNearBlackContent = false;
		// Presentation-only envelope; never learned or published as crop authority.
		bool boundedPresentationAvailable = false;
		bool boundedPresentationFailed = false;
		ActivePictureBounds boundedPresentation;
		uint64_t boundedPresentationSequence = 0;
		uint64_t boundedPresentationPresentedSequence = 0;
        // Actual selected translated pose, never inferred from a held cue target.
        bool presentedTranslationAvailable = false;
        ActivePictureBounds presentedTranslationBounds;
        int presentedTranslationPixels = 0;
        uint64_t presentedTranslationSourceSequence = 0;
		// A candidate base is released only after distinct clean source samples.
		ActivePictureBounds captionCleanBase;
		uint64_t captionCleanLastSourceSequence = 0;
		uint32_t captionCleanSamples = 0;
		uint64_t weakFringeFillSourceSequence = 0;
		uint64_t outwardConfirmationLastSourceSequence = 0;
		uint32_t outwardConfirmationSamples = 0;
		// Independent bright/native run; broad AR confirmations may include dark frames.
		ActivePictureBounds nativeOutwardReleaseCandidate;
		uint64_t nativeOutwardReleaseLastSourceSequence = 0;
		uint32_t nativeOutwardReleaseSamples = 0;
		uint64_t lastEvaluatedSourceSequence = 0;
		uint64_t revalidationStartedSourceSequence = 0;
		uint64_t revalidationLastSourceSequence = 0;
		uint32_t revalidationSamples = 0;
        bool revalidationNativeDarkRun = false;
		bool bootstrapCandidateAvailable = false;
		ActivePictureBounds bootstrapCandidate;
		ActivePictureAuthorityOrigin bootstrapCandidateOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t bootstrapCandidateStartedTick = 0;
		uint64_t bootstrapLastQualifiedTick = 0;
		uint64_t bootstrapLastSourceSequence = 0;
		uint32_t bootstrapSamples = 0;
	};

	struct NearBlackPresentationEpisodeInput
	{
        // Freshly re-proved physical bars plus Classic's CURRENT confirmed
        // translation can explain excluded caption pixels without discarding them.
        ActivePictureBounds subtitleInspectionBars;
        VerticalBarPresentationState subtitlePresentation;
        // Current bounded confirmation may retain an already displayed scope
        // contract; it cannot recover an episode that is already full raster.
        VerticalTranslationConfirmationState subtitleTranslationConfirmation;
        uint64_t subtitleInspectionGeneration = 0;

		NearBlackPresentationEpisodeState previous;
		bool measurementCurrent = false;
		bool nearBlackEvaluated = false;
		bool globalNearBlack = false;
		bool sceneBoundary = false;
		bool trustedCropAvailable = false;
		ActivePictureBounds trustedCrop;
		ActivePictureAuthorityOrigin trustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
		// The last native crop successfully submitted in this source generation
		// and viewport epoch. It may anchor presentation retention but never
		// creates new crop authority.
		bool previouslyAdmittedCropAvailable = false;
		ActivePictureBounds previouslyAdmittedCrop;
		ActivePictureAuthorityOrigin previouslyAdmittedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t previouslyAdmittedCropSourceGeneration = 0;
		uint64_t previouslyAdmittedCropPresentationEpoch = 0;
		// Positive read-only model check of the raw current native observation.
		// Current retention provenance and all episode safety gates still apply.
		bool currentNativeObservationReaffirmsSparseEntry = false;
		bool boundedVisibleContentOutsideCrop = false;
		bool currentVisibleBoundsAvailable = false;
		ActivePictureBounds currentVisibleBounds;
		uint64_t currentVisibleSourceGeneration = 0;
		uint64_t currentVisibleSourceSequence = 0;
        bool weakFringeFillRetained = false;
        // Set only by CanRetainEstablishedScopeForSubtitle, using current pixels.
        bool establishedScopeSubtitleRetained = false;
		bool fullRasterAuthorityAvailable = false;
		// Current context checked by UpdateKnownFullRasterRetention; never inferred
		// from the raw full-raster presentation flag alone.
		bool knownFullRasterRetained = false;
		bool cadenceRepeat = false;
		bool currentObservationAvailable = false;
		ActivePictureClassification currentObservationClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds currentObservation;
		ActivePictureAuthorityOrigin currentObservationOrigin = ActivePictureAuthorityOrigin::NATIVE;
		bool currentNativeObservationAvailable = false;
		ActivePictureBounds currentNativeObservation;
        // Raw native proposed bars on this exact retention sample. A provisional
        // observation's trustedBounds is intentionally full raster.
        bool currentNativeProposedAvailable = false;
        ActivePictureBounds currentNativeProposedBounds;
		// Fresh native outward proof can retire the episode entry; it cannot publish a crop.
		OutwardPictureConfirmationState currentNativeOutwardProof;
		uint64_t currentNativeOutwardProofPresentationEpoch = 0;
		bool retentionEvaluated = false;
		bool retentionSafe = false;
		bool retentionExcludedBandsPixelSafe = false;
		ActivePictureBounds retentionBounds;
		uint64_t retentionSourceGeneration = 0;
		uint64_t retentionSourceSequence = 0;
		bool knownTrustedGeometryReacquired = false;
		ActivePictureBounds reacquiredTrustedGeometry;
		ActivePictureClassification reacquiredTrustedClassification =
			ActivePictureClassification::UNAVAILABLE;
		uint64_t reacquiredSourceGeneration = 0;
		uint64_t reacquiredSourceSequence = 0;
		uint64_t reacquiredPresentationEpoch = 0;
		bool reacquisitionIsCurrentAssociation = false;
		bool nativeBootstrapContractAvailable = false;
		ActivePictureBounds nativeBootstrapContract;
		ActivePictureAuthorityOrigin nativeBootstrapOrigin = ActivePictureAuthorityOrigin::NATIVE;
		bool nativeBootstrapRetentionEvaluated = false;
		bool nativeBootstrapRetentionSafe = false;
		bool nativeBootstrapOutwardVisible = false;
		uint64_t nativeBootstrapSourceGeneration = 0;
		uint64_t nativeBootstrapSourceSequence = 0;
		uint64_t nativeBootstrapPresentationEpoch = 0;
		double framesPerSecond = 60.0;
		uint64_t currentTick = 0;
		uint64_t presentationEpoch = 0;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
	};

	struct NearBlackPresentationEpisodeDecision
	{
		NearBlackPresentationEpisodeState state;
		bool started = false;
		bool changedToFullRaster = false;
		bool restoredSubmittedScope = false;
		bool releasedToTrustedCrop = false;
		bool bootstrapReleased = false;
		bool outwardEntryReleased = false;
		bool resetTransitionEvidence = false;
		// Reopen ordinary acquisition after verified outward sampling correction.
		// The caller must withdraw the undersized trusted rectangle, not only votes.
		bool resetTrustedGeometry = false;
		bool revalidationChanged = false;
		uint32_t revalidationGates = 0;
		uint32_t revalidationSamples = 0;
		uint32_t revalidationSamplesRequired = 0;
		uint32_t bootstrapSamples = 0;
		uint32_t bootstrapSamplesRequired = 0;
		bool ended = false;
		std::string reason;
	};

	// A sparse near-black episode chooses one safe presentation on entry. It can
	// move from retained crop to full raster immediately for visibility, except
	// two weak source rows with a current, previously applied optional-fill certificate. A
	// full-raster episode may restore only its exact entry crop after a bounded,
	// current-frame pixel-safe revalidation dwell. The same proof ends retained
	// crop episodes once bright scope returns. Neither path grants authority to
	// startup or unrelated recent geometry. An independently safe sampling-sized
	// outward correction may instead withdraw the undersized trusted contract
	// and reopen ordinary acquisition; it never restores that unsafe old crop.
	bool IsNearBlackNativeSamplingExpansion(const ActivePictureBounds& entry,
		const ActivePictureBounds& candidate);
	// Qualifies one current bright native sample for an obsolete-entry handoff.
	// Ordinary acquisition must still admit any new crop on current pixels.
	bool QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(
		const NearBlackPresentationEpisodeInput& input);
	struct NearBlackEpisodeDarkness
    {
        bool evaluated = false;
        bool nearBlack = false;
    };
    // Episode entry/exit follows the raw frame's stable darkness measurement.
    // Frame-local caption masking remains a separate geometry witness.
    NearBlackEpisodeDarkness SelectNearBlackEpisodeDarkness(
        bool rawEvaluated, bool rawNearBlack, bool arEffectiveNearBlack);
    NearBlackPresentationEpisodeDecision EvaluateNearBlackPresentationEpisode(
        const NearBlackPresentationEpisodeInput& input);
    // Preserve validated ownership through the release frame and retire old inspection.
    inline bool NearBlackEpisodeOwnsCropThisFrame(const NearBlackPresentationEpisodeDecision& decision)
    { return decision.state.mode == NearBlackPresentationMode::RETAIN_CROP || decision.releasedToTrustedCrop; }
    inline bool NearBlackEpisodeResolvesInspection(const NearBlackPresentationEpisodeDecision& decision)
    { return decision.releasedToTrustedCrop; }
	const char* NearBlackPresentationModeName(NearBlackPresentationMode mode);
	// Current strict caption/native-bar proof may establish logical picture bars
	// during an episode, including global darkness. Presentation remains
	// owned by the episode until its independent release conditions pass.
	bool NearBlackBlocksCaptionBackedLogicalAcquisition(bool globalDark,
		NearBlackPresentationMode episodeMode, bool currentStrictProof);
	// Source analysis may establish physical bars and a caption-safe envelope
	// while all subtitle drawing modes are disabled. Drawing remains mode controlled.
	bool ColdCaptionArAnalysisEligible(bool automaticCrop, bool configuredScreenActive,
		bool nlsRequested, bool fixedCropAspectConfigured,
		NearBlackPresentationMode episodeMode);
	// Automatic Scope subtitle fitting needs current caption/bar measurement
	// even in Classic mode. Episode bootstrap remains available without fit.
	bool ScopeCaptionArAnalysisEligible(bool automaticCrop, bool configuredScreenActive,
		bool nlsRequested, bool fixedCropAspectConfigured,
		NearBlackPresentationMode episodeMode, bool scopeSubtitleFit);
	// Bounded bridge from native caption bootstrap to the first exact current
	// subtitle-worker observation. A missing worker never causes a steady
	// per-frame standalone scan. All frame counters are accepted sequences.
	struct CaptionBootstrapScanInput
	{
		uint64_t sourceGeneration = 0, analysisDomainKey = 0, acceptedSequence = 0;
		uint64_t attemptGeneration = 0, attemptDomainKey = 0;
		uint64_t lastStandaloneSequence = 0, firstProofSequence = 0;
		uint64_t bridgeFrames = 0, coldProbeIntervalFrames = 0;
		bool hasEstablishedGeometry = false, establishedMatchesProof = false;
	};
	enum class CaptionBootstrapScanAction { NONE, COLD_PROBE, BRIDGE };
	CaptionBootstrapScanAction SelectCaptionBootstrapScan(
		const CaptionBootstrapScanInput& input);

	bool ShouldSuppressNearBlackBarGeometryMutation(bool acquisitionBlocked,
		bool stable, ActivePictureClassification classification);

    struct MovingPicturePresentationHold
    {
        bool competingPresentation = false;
        ActivePictureBounds base;
        uint64_t sourceGeneration = 0;
        uint64_t sourceSequence = 0;
        uint64_t presentationEpoch = 0;
        // Only an independently proved subtitle-assisted gradual episode may
        // displace legacy vertical FIT in advanced modes. Horizontal FIT stays.
        bool subtitleAssistedBars = false;
        bool verticalFitOnly = false;
    };

    // Successful composition is an exact-frame presentation fact. It may use
    // an independently trusted native base; it never votes for detector geometry.
    struct PreparedSubtitlePresentation
    {
        bool available=false;
        ActivePictureBounds nativeBase;
        uint64_t sourceGeneration=0,sourceSequence=0,presentationEpoch=0;
    };
	struct Input
	{
        PreparedSubtitlePresentation preparedSubtitle;
        MovingPicturePresentationHold movingPictureHold;
		PictureTransitionHandoff pictureTransitionHandoff;
		uint64_t framePresentationEpoch = 0;
        // Retain a previously admitted scope crop during proved continuous motion.
        // Does not grant authority, acquire a crop, or clear existing recovery.
        bool movingPictureTransition = false;
		bool automaticCropEnabled = false;
		bool nearBlackEpisodeRetainCrop = false;
		bool nearBlackEpisodeFullRaster = false;
		// A last-submitted native Scope may retain presentation through a current
		// ambiguous frame only when exact-frame edge pixels still prove its excluded
		// bands safe. This is presentation continuity, never new crop authority.
		bool submittedScopeContinuityRetention = false;
		bool fullRasterPresentationAuthoritative = false;
		bool sharedGeometryAvailable = false;
		bool latestObservationSupportsCrop = false;
		bool sceneVerificationHoldActive = false;
		bool ambiguityHoldActive = false;
		bool latestObservationIsProvisional = false;
		bool latestObservationIsUnavailable = false;
		// This is the current detector classification, distinct from the
		// classification of the retained shared geometry below. During a bounded
		// scene verification window, a trusted bar observation which has not yet
		// reaffirmed the retained bounds may preserve that existing presentation.
		// A trusted full-raster observation must always withdraw it.
		ActivePictureClassification latestObservationClassification =
			ActivePictureClassification::UNAVAILABLE;
		// Current frame-local permission to retain the prior presentation: safe
		// excluded bands or explicit one-step edge tolerance. Presentation only;
		// it never grants or renews crop authority.
		bool frameLocalPresentationRetentionEvaluated = false;
		bool frameLocalPresentationRetentionSafe = false;
		// Current strict partial-proposal strip proof; never inferred from generic retention.
		bool frameLocalPartialSamplingReaffirmed = false;
		bool presentationFailOpen = false;
		// A current, bounded vertical-only envelope reached final crop arbitration
		// without another presentation owner. Retain the generation-current trusted
		// geometry for this one source sequence without depending on dense-analysis
		// base state. This never grants or renews crop authority.
		bool verticalInspectionPending = false;
		uint64_t verticalInspectionSourceGeneration = 0;
		uint64_t verticalInspectionSourceSequence = 0;
		// A trusted bar observation may disagree slightly with the retained crop
		// while the transition model is still confirming the replacement. Keep the
		// last trusted presentation during that bounded confirmation instead of
		// exposing full raster between old and new bar geometries.
		bool barCropRefinementPending = false;
		// A refinement observation which expands left or right can expose live
		// picture pixels. Retaining the old crop is forbidden. A current pixel-
		// bounded outward expansion can expose them without a full-raster jump.
		bool barCropRefinementHorizontalConflict = false;
		// Produced only when every unsafe excluded edge has a bounded current
		// visible extent. The base and source identity bind the proof to geometry.
		bool currentVisibleBoundsAvailable = false;
		ActivePictureBounds currentVisibleBounds;
		ActivePictureBounds currentVisibleBase;
		uint64_t currentVisibleSourceGeneration = 0;
		uint64_t currentVisibleSourceSequence = 0;
		// A first dense subtitle observation is not yet a stable motion target.
		// Retain the current trusted base for the bounded three-sample confirmation
		// instead of flashing to full raster. This may briefly clip the newly seen
		// bar pixels, but cannot establish or renew crop authority.
		bool verticalTranslationConfirmationPending = false;
		// Dense picture-like evidence also requires bounded confirmation. Retain
		// the same trusted base while that confirmation is pending; this does not
		// grant or renew crop authority.
		bool verticalFitConfirmationPending = false;
		// Sparse source-baked text/UI in one encoded bar is a presentation
		// displacement, not a new aspect ratio. Translate the trusted source
		// window without changing its dimensions so scale and NLS stay stable.
		bool verticalTranslationActive = false;
		int verticalTranslationPixels = 0;
		ActivePictureBounds verticalTranslationBase;
		uint64_t verticalTranslationSourceGeneration = 0;
		// A one-frame bridge when release drift reaches zero. It preserves the
		// trusted base rectangle but never applies a displacement or grants new
		// crop authority.
		bool verticalTranslationBaseRetentionActive = false;
		// A timed engage begins at zero displacement. Preserve the same-generation
		// trusted base on that first sample even when the provisional overlay has
		// made frame-local retention pixel-unsafe; subsequent samples reveal it by
		// translation rather than by a full-raster flash.
		bool verticalTranslationEngageBaseRetentionActive = false;
		bool outwardPresentationActive = false;
		bool outwardExpansionAvailable = false;
		ActivePictureClassification classification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureAuthorityOrigin authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
		ActivePictureBounds geometry;
		ActivePictureBounds outwardExpansion;
		uint64_t geometrySourceGeneration = 0;
		uint64_t outwardExpansionSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		uint64_t frameSourceSequence = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
	};

	bool HasCurrentPictureTransitionHandoff(const Input& input);
    bool HasCurrentMovingPictureHold(const Input& input);

	enum class DecisionOwner
	{
		FULL_RASTER,
		TRUSTED_CROP,
		PIXEL_SAFE_RETENTION,
		SCENE_HOLD,
		AMBIGUITY_HOLD,
		BAR_REFINEMENT,
		PICTURE_CONFIRMATION,
		VERTICAL_INSPECTION,
		TRANSLATION_CONFIRMATION,
		FIT_CONFIRMATION,
		ENGAGE_BASE,
		RELEASE_BASE,
		NEAR_BLACK_EPISODE,
		OUTWARD_FIT,
		VERTICAL_TRANSLATION,
        MOVING_PICTURE_HOLD,
		SUBMITTED_SCOPE_RETENTION,
	};

	enum class WithdrawalCause
	{
		NONE,
		LATEST_OBSERVATION_UNREAFFIRMED,
	};

	const char* DecisionOwnerName(DecisionOwner owner);

    enum class ConfirmedFitOwnershipOrigin : uint8_t
    {
        NONE, CURRENT_CAPTION, CURRENT_UI_OVERLAY, CURRENT_BAR_CONTENT
    };
    // Presentation ownership travels independently of the native crop owner.
    // It certifies only the exact current source envelope, never native AR.
    struct ConfirmedFitOwnershipEvidence
    {
        bool available = false;
        ConfirmedFitOwnershipOrigin origin = ConfirmedFitOwnershipOrigin::NONE;
        ActivePictureBounds nativeBase, approvedBounds;
        uint64_t sourceGeneration = 0, sourceSequence = 0, presentationEpoch = 0;
    };
	struct Decision
	{
		ActivePictureBounds sourceBounds;
		bool applyCrop = false;
		bool outwardExpanded = false;
		bool verticallyTranslated = false;
		int verticalTranslationPixels = 0;
		bool horizontalExpansionPixelBounded = false;
		DecisionOwner owner = DecisionOwner::FULL_RASTER;
		WithdrawalCause withdrawalCause = WithdrawalCause::NONE;
		std::string reason;
        ConfirmedFitOwnershipEvidence confirmedFitOwnership;
	};


	// Tracks the logical contract of an actually applied crop, independently of
	// temporary full-raster withdrawals and of diagnostic logging.
	struct CropPresentationAdmissionState
	{
        // Copied with the selected packet; submission validates identity and actual bounds again.
        ConfirmedFitOwnershipEvidence confirmedFitOwnership;
        // A separate presentation-only certificate for an approved side fill of
        // a small vertical sampling envelope. Never certifies native acquisition.
        bool samplingEnvelopeFillApplied = false;
        ActivePictureBounds samplingEnvelopeSourceBounds;
        ActivePictureBounds samplingEnvelopeFillSourceBounds;
        uint64_t samplingEnvelopeFillSourceSequence = 0;
        double samplingEnvelopeFillScreenAspect = 0.0;
        // Actual optional fill from the immediately preceding presentation.
        bool optionalFillApplied = false;
        bool optionalFillRetained = false;
        ActivePictureBounds optionalFillSourceBounds;
        uint64_t optionalFillSourceSequence = 0;
        double optionalFillScreenAspect = 0.0;
		// Last admitted outward fit, distinct from the persistent logical crop.
		bool outwardPresentationAvailable = false;
		ActivePictureBounds outwardPresentation;
		uint64_t presentationSourceSequence = 0;
		bool available = false;
		ActivePictureBounds trustedCrop;
		ActivePictureAuthorityOrigin authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
	};

	// A native Scope that is safe to use as a temporal presentation anchor only
	// after a frame using that geometry was successfully submitted. Admission
	// alone is not evidence that the viewer actually saw the crop.
	struct SubmittedNativeScopeAnchor
	{
		bool available = false;
		ActivePictureBounds crop;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		uint64_t lastSubmittedSourceSequence = 0;
        // Includes submitted presentation handoffs without renewing native AR.
        uint64_t lastSubmittedPresentationSourceSequence = 0;
        // A submitted outward envelope preserves real visible content while
        // subsequent analysis is unavailable. It is not new picture authority.
        bool submittedEnvelopeAvailable = false;
        ActivePictureBounds submittedEnvelope;
        uint64_t submittedEnvelopeSourceSequence = 0;
        // Exact selected optional side fill is presentation-only. Its successful
        // submission cannot teach or renew native picture authority.
        bool submittedOptionalFillAvailable = false;
        ActivePictureBounds submittedOptionalFill;
        uint64_t submittedOptionalFillSourceSequence = 0;
        double submittedOptionalFillScreenAspect = 0.0;
	};
	struct SubmittedNativeScopeAnchorUpdateInput
	{
		SubmittedNativeScopeAnchor previous;
        CropPresentationAdmissionState selectedAdmission;
		ActivePictureBounds nativeCrop;
		ActivePictureBounds submittedBounds;
		ActivePictureAuthorityOrigin authorityOrigin = ActivePictureAuthorityOrigin::NATIVE;
		DecisionOwner selectedOwner = DecisionOwner::FULL_RASTER;
		bool nativeCropAvailable = false;
		bool frameSubmitted = false;
		bool fullRasterAuthority = false;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
		uint64_t presentationEpoch = 0;
	};
	struct SubmittedScopeContinuityEvidence
	{
		SubmittedNativeScopeAnchor anchor;
		ActivePictureBounds retentionBounds;
		ActivePictureClassification currentClassification =
			ActivePictureClassification::UNAVAILABLE;
		uint64_t sourceGeneration = 0;
		uint64_t sourceSequence = 0;
		uint64_t presentationEpoch = 0;
		uint64_t retentionSourceGeneration = 0;
		uint64_t retentionSourceSequence = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
		bool cadenceRepeat = false;
		bool measurementCurrent = false;
		bool retentionEvaluated = false;
		bool retentionSafe = false;
		bool excludedBandsPixelSafe = false;
		bool outwardVisibleBoundsAvailable = false;
		bool fullRasterAuthority = false;
		bool presentationFailOpen = false;
				bool verticalTranslationActive = false;
	};
	bool IsCurrentSubmittedNativeScopeAnchor(const SubmittedNativeScopeAnchor& anchor,
		uint64_t sourceGeneration, uint64_t sourceSequence,
		uint64_t presentationEpoch, int rasterWidth, int rasterHeight);
	bool CanRetainSubmittedScopeContinuity(
		const SubmittedScopeContinuityEvidence& evidence);
	SubmittedNativeScopeAnchor CommitSubmittedNativeScopeAnchor(
		const SubmittedNativeScopeAnchorUpdateInput& input, const char** outcomeReason = nullptr);

    struct EstablishedSubtitleTranslationInput
    {
        ActivePictureBounds base;
        ActivePictureBounds visibleBounds;
        VerticalBarPresentationState presentation;
        uint64_t sourceGeneration = 0;
        uint64_t holdMs = 0;
        int resolvedTranslationPixels = 0;
        // A configured engage may not have reached its confirmed target yet.
        bool engagingTranslation = false;
        // Snapshot from the active renderer drift, not inferred from the new
        // target. A reversal may still be on the old side of the trusted base.
        bool engageIntervalAvailable = false;
        float engageStartTranslationPixels = 0.0f;
        float engageTargetTranslationPixels = 0.0f;
        float engageAppliedTranslationPixels = 0.0f;
        bool competingPresentation = false;
        // An actually admitted translation may finish its existing release
        // animation after the cue hold expires. This never grants new outward
        // content authority or renews the original subtitle owner.
        VerticalBarPresentationState releaseOwner;
        ActivePictureBounds releaseOwnerBase;
        uint64_t releaseSourceGeneration = 0;
        uint64_t releasePresentationEpoch = 0;
        bool releaseIntervalAvailable = false;
        float releaseStartTranslationPixels = 0.0f;
        float releaseTargetTranslationPixels = 0.0f;
        float releaseAppliedTranslationPixels = 0.0f;
        uint64_t releaseStartedTick = 0;
        uint64_t releaseDurationMs = 0;
    };

    // NLS mapping inherits an admitted viewport crop when its own source crop
    // is disabled. Any other crop setting remains a competing owner.
    bool NlsCompetesWithEstablishedSubtitleTranslation(bool nlsRequested,
        double maximumCropPercent);

    // Retains an already admitted scope presentation through darkness only when
    // its live shared translation covers all current outward evidence.
    bool CanRetainEstablishedScopeForSubtitle(const NearBlackPresentationEpisodeInput& episode,
        const CropPresentationAdmissionState& previous,
        const EstablishedSubtitleTranslationInput& translation);
    // Presentation-only tolerance: at most two weak source rows, never new authority.
    bool CanRetainWeakFringeWithFill(const NearBlackPresentationEpisodeInput& episode,
        const CropPresentationAdmissionState& previous, const AspectLimitFillInput& currentFill,
        bool weakBoundedFringe, bool competingPresentation);
	struct CropPresentationAdmissionDecision
	{
		CropPresentationAdmissionState state;
		Decision presentation;
		bool blocked = false;
	};

	// A current confirmed caption may bound an independently trusted picture
	// during first crop acquisition. The caption supplies extent, never picture
	// authority; current visible pixels remain a veto on the final envelope.
	struct CaptionBoundedFitAcquisitionInput
	{
		const Input* crop = nullptr;
		ActivePictureBounds captionBounds;
		uint64_t captionSourceGeneration = 0;
		uint64_t captionSourceSequence = 0;
		bool captionConfirmed = false;
		bool initialAcquisition = false;
		bool translationProposed = false;
	};
	struct CaptionBoundedFitAcquisitionDecision
	{
		bool available = false;
		ActivePictureBounds bounds;
		const char* reason = "caption acquisition unavailable";
	};
	CaptionBoundedFitAcquisitionDecision BuildCaptionBoundedFitAcquisition(
		const CaptionBoundedFitAcquisitionInput& input);

    // A dark full-raster episode may expose only a current guarded native picture
    // together with the complete current caption extent. This is a presentation
    // proposal, never picture authority or an admission by itself.
    struct ColdMaskedCaptionPresentationInput
    {
        const Input* crop = nullptr;
        NearBlackPresentationEpisodeState episode;
        CropPresentationAdmissionState previousAdmission;
        bool strictProofCurrent = false;
        ActivePictureBounds strictPicture;
        uint64_t proofSourceGeneration = 0, proofSourceSequence = 0;
        uint64_t proofPresentationEpoch = 0;
        bool captionConfirmed = false;
        ActivePictureBounds captionBounds;
        uint64_t captionSourceGeneration = 0, captionSourceSequence = 0;
        bool currentObservationAvailable = false;
        ActivePictureBounds currentObservation;
        uint64_t observationSourceGeneration = 0, observationSourceSequence = 0;
        bool currentRetentionEvaluated = false, currentRetentionSafe = false;
        bool currentExcludedBandsPixelSafe = false, currentOutwardVisible = false;
        bool currentNativeTrusted = false;
        ActivePictureBounds currentNativeBounds;
        bool captionMeasured = false;
        uint64_t retentionSourceGeneration = 0, retentionSourceSequence = 0;
        ActivePictureBounds retentionBase;
        bool captionAbsenceConfirmed = false;
        bool sceneBoundary = false, cadenceRepeat = false;
        double framesPerSecond = 60.0;
    };
    struct ColdMaskedCaptionPresentationDecision
    {
        bool available = false;
        bool held = false;
        bool releasedToBase = false;
        bool nativeCleanBase = false;
        bool rebasedFromRetainedCrop = false;
        NearBlackPresentationEpisodeState episodeState;
        Decision presentation;
        const char* reason = "cold masked caption proof unavailable";
    };
    ColdMaskedCaptionPresentationDecision BuildColdMaskedCaptionPresentation(
        const ColdMaskedCaptionPresentationInput& input);
    // May retain the last submitted translated pose across one exact current
    // worker gap. The ordinary owner, native-bar and drift certificates still
    // govern; otherwise the bounded fit remains the safe fallback.
    Decision RetainColdTranslatedPresentation(
        const ColdMaskedCaptionPresentationDecision& bounded,
        const NearBlackPresentationEpisodeInput& episode,
        const CropPresentationAdmissionState& previousAdmission,
        const EstablishedSubtitleTranslationInput& translation,
        const Input& translatedCrop);
    enum class ColdMaskedCaptionCommitAction { NONE, BOUNDED, TRANSLATED, CLEAN_BASE };
    struct ColdMaskedCaptionCommitInput
    {
        NearBlackPresentationEpisodeState currentEpisode;
        CropPresentationAdmissionState currentAdmission;
        // Immutable witness carried by the selected source, after optional fill.
        CropPresentationAdmissionState selectedAdmission;
        ColdMaskedCaptionCommitAction action = ColdMaskedCaptionCommitAction::NONE;
        NearBlackPresentationMode proposalMode = NearBlackPresentationMode::INACTIVE;
        ActivePictureBounds proposalEntryCrop;
        bool proposalEntryCropAvailable = false;
        uint64_t proposalStartSequence = 0;
        uint64_t sourceGeneration = 0, presentationEpoch = 0;
        uint64_t currentSourceSequence = 0, selectedSourceSequence = 0;
        uint64_t proofSourceSequence = 0;
        ActivePictureBounds selectedBase, selectedEnvelope;
        // Final selected raster crop and owner after every source arbitration.
        DecisionOwner selectedActualOwner = DecisionOwner::FULL_RASTER;
        ActivePictureBounds selectedActualBounds;
        ActivePictureBounds selectedTranslatedBounds;
        int selectedTranslationPixels = 0;
        bool renderedAndSubmitted = false;
        double framesPerSecond = 60.0;
    };
    struct ColdMaskedCaptionCommitDecision
    {
        NearBlackPresentationEpisodeState state;
        bool committed = false;
        const char* reason = "selected output did not commit a cold caption owner";
    };
    // Called only after the selected output actually rendered and submitted.
    ColdMaskedCaptionCommitDecision CommitColdMaskedCaptionPresentation(
        const ColdMaskedCaptionCommitInput& input);
    struct CaptionConfirmationPresentationAnchor
    {
        bool available = false; // Set only after successful rendering/submission.
        ActivePictureBounds bounds;
        uint64_t sourceGeneration = 0, analysisDomainKey = 0, sourceFormatKey = 0, presentationEpoch = 0;
        uint64_t sourceSequence = 0, tick = 0;
    };

    struct CaptionConfirmationPresentationHoldState
    {
        bool active = false;
        CaptionConfirmationPresentationAnchor anchor;
        ActivePictureBounds picture;
        uint64_t firstSequence = 0, lastSequence = 0, startedTick = 0;
        double targetTranslationPixels = 0.0;
    };

    struct CaptionConfirmationPresentationHoldInput
    {
        CaptionConfirmationPresentationHoldState previous;
        CaptionConfirmationPresentationAnchor lastRendered;
        ActivePictureBounds picture, caption, visibleBounds, retentionBase;
        uint64_t sourceGeneration = 0, analysisDomainKey = 0, sourceFormatKey = 0, presentationEpoch = 0;
        uint64_t sourceSequence = 0, currentTick = 0;
        uint64_t pictureGeneration = 0, pictureSequence = 0;
        uint64_t captionGeneration = 0, captionSequence = 0;
        uint64_t visibleGeneration = 0, visibleSequence = 0;
        uint64_t retentionGeneration = 0, retentionSequence = 0;
        bool eligible = false, pictureCurrentTrusted = false, captionCurrentMeasured = false;
        bool nearBlackEvaluated = false, nearBlack = false;
        bool pendingConfirmation = false, zeroEngage = false, competingPresentation = false;
        bool cadenceRepeat = false;
        // Include the configured target buffer during pending confirmation so
        // the same target survives its accepted zero-displacement engage frame.
        double targetTranslationPixels = 0.0;
    };

    struct CaptionConfirmationPresentationHoldDecision
    {
        CaptionConfirmationPresentationHoldState state;
        bool hold = false;
        ActivePictureBounds bounds;
        const char* reason = "caption confirmation hold unavailable";
    };

    // Presentation only. Never establishes picture/translation authority or
    // changes admission, recovery, subtitle confirmation, or temporal votes.
    CaptionConfirmationPresentationHoldDecision EvaluateCaptionConfirmationPresentationHold(
        const CaptionConfirmationPresentationHoldInput& input);

    // Complete a recovered, admitted caption-fit acquisition without displaying
    // its temporary size. Failure preserves that safe fit verbatim.
    CropPresentationAdmissionDecision CompleteCaptionAcquisitionTranslation(
        const CaptionBoundedFitAcquisitionInput& acquisition,
        const CropPresentationAdmissionDecision& admittedFit,
        const EstablishedSubtitleTranslationInput& translation,
        uint64_t presentationEpoch, uint64_t currentTick, std::string* diagnosticReason = nullptr);

	// Admission belongs to a specific source, viewport and trusted picture contract.
	// A different movie aspect ratio must acquire its own safe presentation.
	bool HasCurrentCropPresentationAdmission(const CropPresentationAdmissionState& previous,
		const Input& input, uint64_t presentationEpoch);

	// Candidate must be the result of Evaluate followed by presentation recovery.
	// Run before fill/NLS so neither can use geometry rejected by this admission.
	CropPresentationAdmissionDecision AdmitCropPresentation(
		const CropPresentationAdmissionState& previous,
		const Input& input, const Decision& candidate,
		uint64_t presentationEpoch);


	// A temporary presentation withdrawal is not full-raster aspect authority.
	// Once armed, every ordinary inward owner must prove the current saved crop
	// safe on distinct adjacent source frames for 250 ms. This does not delay
	// initial acquisition or a confirmed subtitle/Fit resolution.
	enum RecoveryGate : uint32_t
	{
		RECOVERY_OK = 0,
		RECOVERY_MEASUREMENT = 1u << 0,
		RECOVERY_REPEAT = 1u << 1,
		RECOVERY_CONTEXT = 1u << 2,
		RECOVERY_CONTRACT = 1u << 3,
		RECOVERY_OBSERVATION = 1u << 4,
		RECOVERY_UNSAFE_BANDS = 1u << 5,
		RECOVERY_NEAR_BLACK = 1u << 6,
		RECOVERY_FULL_AUTHORITY = 1u << 7,
		RECOVERY_SEQUENCE_GAP = 1u << 8,
		RECOVERY_OWNER = 1u << 9,
		RECOVERY_INSPECTION_REENTRY = 1u << 10,
	};
	std::string RecoveryGateNames(uint32_t gates);

	struct PresentationRecoveryState
	{
		bool active = false;
        // Preserve the existing inspection window's immediate native resolution.
        bool inspectionReentryPending = false;
		// Outward-only while recovery is pending; inward return uses existing proof.
		bool fallbackBoundsAvailable = false;
		ActivePictureBounds fallbackBounds;
		ActivePictureBounds trustedCrop;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		uint64_t startedSourceSequence = 0;
		uint64_t startedTick = 0;
		uint64_t lastSourceSequence = 0;
		uint32_t samples = 0;
	};


    // The approved envelope is built independently from current accepted
    // caption/UI evidence. A dense geometry/FIT completion is not ownership.
    struct ConfirmedFitOwnershipInput
    {
        ConfirmedFitOwnershipOrigin origin = ConfirmedFitOwnershipOrigin::NONE;
        bool current = false, confirmed = false;
        ActivePictureBounds nativeBase, approvedBounds, evidenceBounds;
        uint64_t sourceGeneration = 0, sourceSequence = 0, presentationEpoch = 0;
        bool upperContent = false, lowerContent = false;
        bool upperOverlayLike = false, lowerOverlayLike = false;
        int horizontalPadding = 0, verticalPadding = 0;
    };

    ConfirmedFitOwnershipEvidence QualifyConfirmedFitOwnership(
        const ConfirmedFitOwnershipInput& input);
    // Established caption FIT may retain its existing smaller clearance, but
    // cannot borrow detector/dense extents beyond this cue's own padded bounds.
    ConfirmedFitOwnershipEvidence QualifyCaptionFitWithinPadding(
        const ConfirmedFitOwnershipInput& input,int horizontalPadding,int verticalPadding);

	struct PresentationRecoveryInput
	{
		PresentationRecoveryState previous;
		CropPresentationAdmissionState previousAdmission;
		SubmittedNativeScopeAnchor submittedScopeAnchor;
		Input crop;
		Decision candidate;
		bool cadenceRepeat = false;
		bool measurementCurrent = false;
		bool retentionEvaluated = false;
		bool excludedBandsPixelSafe = false;
		bool observationAvailable = false;
		ActivePictureBounds observation;
		ActivePictureBounds observedTrustedCrop;
		ActivePictureClassification observationClassification = ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds retentionBounds;
		uint64_t retentionSourceGeneration = 0;
		uint64_t retentionSourceSequence = 0;
		bool nearBlackEvaluated = false;
		bool globalNearBlack = false;
		// Only a confirmed current dense presentation or a completed episode
		// proof can resolve ownership; pending refinement/inspection cannot.
		bool confirmedPresentationResolved = false;
        // Separate from generic recovery/inspection resolution above. Only an
        // exact current caption/UI owner may exempt an expanded candidate.
        ConfirmedFitOwnershipEvidence confirmedFitOwnership;
		uint64_t presentationEpoch = 0;
		uint64_t currentTick = 0;
		double framesPerSecond = 60.0;
	};

	struct PresentationRecoveryDecision
	{
		PresentationRecoveryState state;
		Decision presentation;
		bool started = false;
		bool ended = false;
		bool released = false;
		bool proofReset = false;
		bool samplingReaffirmed = false;
		bool boundedPresentation = false;
		uint32_t samples = 0;
		uint32_t required = 0;
		uint32_t gates = RECOVERY_OK;
	};
	// Clears only an existing same-contract inspection latch. Does not grant crop authority.
	bool CanResolveContainedInspectionRetention(
		const VerticalInspectionBridgeState& inspection, const PresentationRecoveryInput& input);

	PresentationRecoveryDecision EvaluatePresentationRecovery(
		const PresentationRecoveryInput& input);

	struct NearBlackBoundedPresentationDecision
	{
		NearBlackPresentationEpisodeState state;
		Decision presentation;
		bool boundedPresentation = false;
		bool retainedScopeFallback = false;
		// Evaluation proposes a source envelope. It becomes reusable temporal
		// state only after the selected packet with this exact crop is submitted.
		bool proposalAvailable = false;
		bool failOpenProposal = false;
		ActivePictureBounds proposalBase;
		ActivePictureBounds proposalBounds;
		NearBlackPresentationMode proposalMode = NearBlackPresentationMode::INACTIVE;
		uint64_t sourceGeneration = 0;
		uint64_t presentationEpoch = 0;
		uint64_t sourceSequence = 0;
		uint64_t episodeStartSequence = 0;
	};
	NearBlackBoundedPresentationDecision EvaluateNearBlackBoundedPresentation(
		const NearBlackPresentationEpisodeState& previous, const PresentationRecoveryInput& input);

	struct NearBlackBoundedPresentationCommitInput
	{
		NearBlackPresentationEpisodeState currentEpisode;
		CropPresentationAdmissionState currentAdmission;
		NearBlackPresentationMode proposalMode = NearBlackPresentationMode::INACTIVE;
		ActivePictureBounds proposalBase, proposalBounds;
		bool failOpenProposal = false;
		uint64_t sourceGeneration = 0, presentationEpoch = 0;
		uint64_t currentSourceSequence = 0, selectedSourceSequence = 0;
		uint64_t proposalSourceSequence = 0;
		uint64_t episodeStartSequence = 0;
		DecisionOwner selectedActualOwner = DecisionOwner::FULL_RASTER;
		ActivePictureBounds selectedActualBounds;
		bool sceneBoundary = false;
		bool renderedAndSubmitted = false;
	};
	struct NearBlackBoundedPresentationCommitDecision
	{
		NearBlackPresentationEpisodeState state;
		bool committed = false;
		const char* reason = "selected near-black envelope did not commit";
	};
	NearBlackBoundedPresentationCommitDecision CommitNearBlackBoundedPresentation(
		const NearBlackBoundedPresentationCommitInput& input);

	enum class ScenePresentationAction
	{
		WITHDRAW,
		KEEP_CURRENT,
		HOLD_SNAPSHOT,
		// Current pixels change presentation immediately, but do not erase the
		// last affirmative same-generation logical geometry.
		PRESERVE_REFERENCE,
	};

	struct SceneInput
	{
		bool geometryAvailable = false;
		bool geometryIsCurrentGeneration = false;
		bool latestEvidenceIsCurrent = false;
		bool latestObservationSupportsCrop = false;
		bool existingCropCanBeSnapshotted = false;
		// Fresh dense evidence on this exact source frame identifies the pixels
		// outside the trusted rectangle as an overlay whose bounded translation
		// still owns presentation. It may preserve existing geometry at a cut but
		// can never create geometry or cross a source generation.
		bool currentOverlayEvidenceSupportsGeometry = false;
		bool frameLocalPresentationRetentionEvaluated = false;
		bool frameLocalPresentationRetentionSafe = false;
		ActivePictureClassification geometryClassification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureClassification latestClassification =
			ActivePictureClassification::UNAVAILABLE;
		// Set only by CanRetainKnownFullRasterAtDarknessBoundary.
		bool knownFullRasterDarknessRetention = false;
	};

	struct SceneDecision
	{
		ScenePresentationAction action =
			ScenePresentationAction::WITHDRAW;
		std::string reason;
	};

	struct SceneHoldInput
	{
		bool snapshotAvailable = false;
		bool nlsRequested = false;
		bool retainedMappingCompatible = false;
		uint64_t snapshotSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		uint64_t deadlineTick = 0;
		uint64_t currentTick = 0;
	};

	struct SceneHoldDecision
	{
		bool cropActive = false;
		bool nlsActive = false;
	};

    // Compatibility permits current pixel verification, never crop authority.
    // Retiring a snapshot is a separate step after successful guarded commit.
    struct GuardedSceneHoldInput
    {
        SceneHoldDecision hold;
        bool snapshotAvailable = false;
        ActivePictureBounds snapshotBounds, currentNativeBase, nominationBase;
        uint64_t snapshotGeneration = 0, currentGeneration = 0;
        bool currentNativeAvailable = false;
        bool certificateAvailable = false;
        bool cutOrDiscontinuity = false;
    };
    struct GuardedSceneHoldDecision
    {
        bool mayVerify = false;
        bool clearSnapshot = false;
        SceneHoldDecision hold;
    };
    GuardedSceneHoldDecision EvaluateGuardedSceneHold(
        const GuardedSceneHoldInput& input, bool committed = false);

	// Pure crop-authority boundary for Alpha. Observers and consumers may propose
	// geometry, but only a generation-current shared BAR_CROP_TRUSTED snapshot
	// reaffirmed by the latest symmetric observation can contract the source
	// rectangle. The user-visible Off state always returns the complete raster.
    struct UncertainPicturePresentationDecision
    {
        bool retained = false;
        bool expanded = false;
        bool currentPixelExtent = false;
        ActivePictureBounds nativeCrop;
        Decision presentation;
    };

    // Final automatic-picture policy, after recovery and admission. Uncertainty
    // cannot invent full-raster authority or acquire an unsubmitted crop.
    UncertainPicturePresentationDecision ResolveUncertainPicturePresentation(
        const PresentationRecoveryInput& input, const Decision& proposed);

    // Final optional-fill routing shared by the renderer and policy tests.
    // This is presentation only; it cannot publish native geometry or renew authority.
    struct FinalAspectLimitFillInput
    {
        AspectLimitFillInput configured;
        Input crop;
        Decision presentation;
        PresentationRecoveryInput evidence;
        UncertainPicturePresentationDecision uncertainPicture;
        bool fixedCropAspectConfigured = false;
        double fixedCropAspect = 0.0;
        bool protectedCaptionFit = false;
        bool nlsPresentationFailOpen = false;
        bool recoveryBoundedPresentation = false;
        bool nearBlackBoundedPresentation = false;
        bool nearBlackRetainedScopeFallback = false;
        bool nearBlackFailOpenProposal = false;
        bool nlsOwnsPresentationGeometry = false;
        bool configuredScreenActive = false;
        // Fresh confirmed caption/content conflicts veto submitted side-fill
        // retention; missing or held analysis alone is not such a conflict.
        bool currentConfirmedPresentationConflict = false;
        // Fail closed unless the renderer excludes current/held Classic or
        // advanced captions/icons, side content, pending confirmation and drift.
        bool samplingCompetingPresentation = true;
    };
    AspectLimitFillDecision EvaluateFinalAspectLimitFill(
        const FinalAspectLimitFillInput& input);

    // Exact current composition can resolve presentation-only inspection and
    // recovery of this independently trusted native base, without detector votes.
    bool HasCurrentPreparedSubtitlePresentation(const Input& input);
	Decision Evaluate(const Input& input);

	// A scene boundary resets candidate proof, but presentation is atomic:
	// matching trusted geometry keeps crop and NLS together; ambiguous or
	// contradictory current pixels may change presentation immediately while
	// preserving the last affirmative same-generation logical reference.
	SceneDecision EvaluateSceneBoundary(const SceneInput& input);

	// Latch the bounded scene hold once per rendered frame. Crop and NLS consume
	// the same result so expiry, source replacement, and mapping changes cannot
	// split the presentation for one frame.
	SceneHoldDecision EvaluateSceneHold(const SceneHoldInput& input);
}
