#pragma once

#include <cstdint>
#include <string>

#include "ActivePictureTransitionModel.h"
#include "ActivePictureEvidence.h"


namespace AlphaSourceCrop
{
	static constexpr uint32_t OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED = 3;

	struct OutwardPictureConfirmationState
	{
		ActivePictureBounds candidate;
		uint32_t confirmations = 0;
		uint64_t sourceGeneration = 0;
	};

	struct OutwardPictureConfirmationDecision
	{
		OutwardPictureConfirmationState state;
		bool outwardTransition = false;
		bool broadOpposingPicture = false;
		bool authoritative = false;
	};

	// Expanding a trusted crop changes the logical aspect only after the same
	// frame shows broad picture-like occupancy in every opposing excluded band.
	// Localized UI/text still expands presentation immediately, but cannot build
	// aspect authority by appearing on different edges at different times.
	OutwardPictureConfirmationDecision ConfirmOutwardPictureTransition(
		const OutwardPictureConfirmationState& previous,
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& candidate,
		const ActivePicturePresentationRetentionEvidence& evidence,
		uint64_t sourceGeneration);

	enum class BarContentEdge
	{
		NONE,
		TOP,
		BOTTOM,
	};

	// Dense bar inspection is presentation evidence, not format authority. When
	// both encoded bars contain samples, accept only the edge needing the larger
	// displacement. The shared active-picture detector remains responsible for
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
		const VerticalBarContentDecision& observed);

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

	struct VerticalBarPresentationState
	{
		VerticalBarPresentationAction action =
			VerticalBarPresentationAction::NONE;
		int detectedTop = 0;
		int detectedBottom = 0;
		float translationPixels = 0.0f;
		uint64_t lastDetectionTick = 0;
		uint64_t sourceSequence = 0;
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

	// Full raster is always outward-safe. Keep that presentation authority
	// between sparse analysis samples, but withdraw it as soon as trusted bar
	// evidence appears. Ambiguity cannot turn it into crop authority.
	bool UpdateFullRasterPresentationAuthority(bool previouslyAuthoritative,
		ActivePictureClassification currentClassification,
		bool currentBoundsAreFullRaster);

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
	};

	struct AspectLimitFillDecision
	{
		ActivePictureBounds sourceBounds;
		bool applied = false;
		double contentAspect = 0.0;
		std::string reason;
	};

	AspectLimitFillDecision EvaluateAspectLimitFill(
		const AspectLimitFillInput& input);

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

	struct Input
	{
		bool automaticCropEnabled = false;
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
		// Positive, frame-local proof that retaining the prior presentation
		// excludes no currently visible pixels. This preserves presentation only;
		// it never grants or renews crop authority.
		bool frameLocalPresentationRetentionEvaluated = false;
		bool frameLocalPresentationRetentionSafe = false;
		bool presentationFailOpen = false;
		// A current, bounded vertical-only envelope is awaiting its first dense
		// classification. Retain the generation-current trusted geometry without
		// depending on dense-analysis base state, which may not exist yet on this
		// first inspection frame. This never grants or renews crop authority.
		bool verticalInspectionPending = false;
		// A trusted bar observation may disagree slightly with the retained crop
		// while the transition model is still confirming the replacement. Keep the
		// last trusted presentation during that bounded confirmation instead of
		// exposing full raster between old and new bar geometries.
		bool barCropRefinementPending = false;
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
		ActivePictureBounds geometry;
		ActivePictureBounds outwardExpansion;
		uint64_t geometrySourceGeneration = 0;
		uint64_t outwardExpansionSourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
	};

	struct Decision
	{
		ActivePictureBounds sourceBounds;
		bool applyCrop = false;
		bool outwardExpanded = false;
		bool verticallyTranslated = false;
		int verticalTranslationPixels = 0;
		std::string reason;
	};

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

	// Pure crop-authority boundary for Alpha. Observers and consumers may propose
	// geometry, but only a generation-current shared BAR_CROP_TRUSTED snapshot
	// reaffirmed by the latest symmetric observation can contract the source
	// rectangle. The user-visible Off state always returns the complete raster.
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
