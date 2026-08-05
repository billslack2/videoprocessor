#pragma once

#include <cstdint>
#include <string>

#include "ActivePictureTransitionModel.h"


namespace AlphaSourceCrop
{
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
		int placementSnapThreshold = 0;
		bool translationEnabled = false;
	};

	bool IsVerticalBarPresentationActive(
		const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence);

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

	// Store exactly one held vertical action. FIT retains the widest measured
	// extents; TRANSLATE retains the farthest same-direction displacement.
	VerticalBarPresentationState UpdateVerticalBarPresentation(
		const VerticalBarPresentationUpdateInput& input);

	struct VerticalBarPresentationResolutionInput
	{
		VerticalBarPresentationAction detailedAction =
			VerticalBarPresentationAction::NONE;
		float translationPixels = 0.0f;
		bool genericUpperExpansion = false;
		bool genericLowerExpansion = false;
		// A retained union of unrelated top/bottom overlays is not aspect-ratio
		// authority. Generic vertical FIT requires both edges on this frame.
		bool genericVerticalFitConfirmed = false;
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
		// Positive, frame-local proof that retaining the prior presentation
		// excludes no currently visible pixels. This preserves presentation only;
		// it never grants or renews crop authority.
		bool frameLocalPresentationRetentionEvaluated = false;
		bool frameLocalPresentationRetentionSafe = false;
		bool presentationFailOpen = false;
		// Sparse source-baked text/UI in one encoded bar is a presentation
		// displacement, not a new aspect ratio. Translate the trusted source
		// window without changing its dimensions so scale and NLS stay stable.
		bool verticalTranslationActive = false;
		int verticalTranslationPixels = 0;
		ActivePictureBounds verticalTranslationBase;
		uint64_t verticalTranslationSourceGeneration = 0;
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
	};

	struct SceneInput
	{
		bool geometryAvailable = false;
		bool geometryIsCurrentGeneration = false;
		bool latestEvidenceIsCurrent = false;
		bool latestObservationSupportsCrop = false;
		bool existingCropCanBeSnapshotted = false;
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
	// near-black unavailable cut evidence may retain their existing snapshot
	// briefly; contradiction withdraws both.
	SceneDecision EvaluateSceneBoundary(const SceneInput& input);

	// Latch the bounded scene hold once per rendered frame. Crop and NLS consume
	// the same result so expiry, source replacement, and mapping changes cannot
	// split the presentation for one frame.
	SceneHoldDecision EvaluateSceneHold(const SceneHoldInput& input);
}
