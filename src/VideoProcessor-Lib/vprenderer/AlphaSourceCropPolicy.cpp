#include <pch.h>

#include "AlphaSourceCropPolicy.h"

#include <cmath>


namespace AlphaSourceCrop
{
	namespace
	{
		// At 24 fps this is at most 125 ms: enough for the queued dense scan to
		// publish its first overlay result, but too short to hide a real geometry
		// change behind a stale trusted crop.
		constexpr uint64_t VERTICAL_INSPECTION_MAX_SOURCE_SEQUENCES = 3;

		bool SamplingEquivalentAxis(int first, int last, int otherFirst, int otherLast, int tolerance)
		{
			return std::abs(first-otherFirst) <= tolerance && std::abs(last-otherLast) <= tolerance &&
				std::abs((last-first)-(otherLast-otherFirst)) <= tolerance;
		}

		int SamplingTolerance(const ActivePictureBounds& bounds)
		{
			return std::max(2, std::max(bounds.rasterWidth / 480, bounds.rasterHeight / 270));
		}

		bool BroadPictureLike(const ActivePictureEdgeEvidence& edge)
		{
			return edge.barPixels > 0 && edge.blackFraction <= 0.80 &&
				edge.continuity <= 0.85 &&
				// Evidence luma/texture values are raw 10-bit code units. Require
				// either pixels above the maximum bar threshold (104) plus margin,
				// or substantial spatial texture for a genuinely dark picture.
				(edge.lumaP90 >= 112.0 || edge.texture >= 12.0);
		}

		ActivePictureBounds FullRaster(int width, int height)
		{
			ActivePictureBounds bounds;
			bounds.right = width;
			bounds.bottom = height;
			bounds.rasterWidth = width;
			bounds.rasterHeight = height;
			bounds.aspectRatio = width > 0 && height > 0
				? static_cast<double>(width) / height : 0.0;
			bounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			return bounds;
		}

		bool ValidBounds(const ActivePictureBounds& bounds, int width, int height)
		{
			return width > 0 && height > 0 &&
				bounds.rasterWidth == width && bounds.rasterHeight == height &&
				bounds.left >= 0 && bounds.top >= 0 &&
				bounds.right <= width && bounds.bottom <= height &&
				bounds.left < bounds.right && bounds.top < bounds.bottom;
		}

		bool HasAuthorityForCroppedAxes(const ActivePictureBounds& bounds,
			int width, int height)
		{
			const uint8_t axes = static_cast<uint8_t>(bounds.trustedBarAxes);
			const bool cropsTop = bounds.top > 0;
			const bool cropsBottom = bounds.bottom < height;
			const bool cropsLeft = bounds.left > 0;
			const bool cropsRight = bounds.right < width;
			const bool cropsTopBottom = cropsTop || cropsBottom;
			const bool cropsLeftRight = cropsLeft || cropsRight;
			return (cropsTopBottom || cropsLeftRight) &&
				(!cropsTopBottom || (cropsTop && cropsBottom)) &&
				(!cropsLeftRight || (cropsLeft && cropsRight)) &&
				(!cropsTopBottom ||
					(axes & static_cast<uint8_t>(
						ActivePictureBounds::BarAxes::TOP_BOTTOM)) != 0) &&
				(!cropsLeftRight ||
					(axes & static_cast<uint8_t>(
						ActivePictureBounds::BarAxes::LEFT_RIGHT)) != 0);
		}

		bool CropEdgesAreChromaAligned(
			const ActivePictureBounds& bounds, int width, int height)
		{
			const bool horizontalCrop = bounds.left > 0 || bounds.right < width;
			const bool verticalCrop = bounds.top > 0 || bounds.bottom < height;
			return (!horizontalCrop ||
				((bounds.left & 1) == 0 && (bounds.right & 1) == 0)) &&
				(!verticalCrop ||
				((bounds.top & 1) == 0 && (bounds.bottom & 1) == 0));
		}

		bool SameBounds(const ActivePictureBounds& left,
			const ActivePictureBounds& right)
		{
			return left.left == right.left && left.top == right.top &&
				left.right == right.right && left.bottom == right.bottom &&
				left.rasterWidth == right.rasterWidth &&
				left.rasterHeight == right.rasterHeight;
		}

		bool SameTrustedCropContract(const ActivePictureBounds& left,
			const ActivePictureBounds& right)
		{
			return SameBounds(left, right) &&
				left.trustedBarAxes == right.trustedBarAxes &&
				left.trustedBarAxes != ActivePictureBounds::BarAxes::NONE;
		}

		bool HasDistributedSupport(const ActivePictureEdgeEvidence& edge)
		{
			if (edge.barPixels <= 0 || edge.blackFraction > 0.80 ||
				edge.continuity > 0.85)
				return false;
			for (const uint8_t rowMask : edge.distributedSupportDepthRowsByQuartile)
			{
				int supportedRows = 0;
				for (int depth = 0; depth < 6; ++depth)
					supportedRows += (rowMask >> depth) & 1u;
				if (supportedRows < 2)
					return false;
			}
			return true;
		}

		bool HasStrictNativeDistributedPictureProof(
			const ActivePicturePresentationRetentionEvidence& evidence,
			const ActivePictureBounds& trustedGeometry,
			const ActivePictureBounds& candidate)
		{
			const auto& native = evidence.activePicture;
			if (!evidence.analysisValid || !evidence.presentationValid ||
				evidence.EffectiveArNearBlack() || !evidence.expansionStripsAvailable ||
				!SameTrustedCropContract(evidence.expansionBase, trustedGeometry) ||
				!SameTrustedCropContract(evidence.expansionCandidate, candidate) ||
				!native.available ||
				native.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
				native.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
				!SameTrustedCropContract(native.trustedBounds, candidate) ||
				(static_cast<uint8_t>(candidate.trustedBarAxes) &
				 static_cast<uint8_t>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0 ||
				!native.axisEvidence.vertical.scanComplete ||
				native.axisEvidence.vertical.state != ActivePictureAxisState::TRUSTED_BARS ||
				!native.top.trusted || !native.bottom.trusted)
				return false;

			return (candidate.top >= trustedGeometry.top ||
				HasDistributedSupport(evidence.expandingTop)) &&
				(candidate.bottom <= trustedGeometry.bottom ||
				HasDistributedSupport(evidence.expandingBottom));
		}

		bool SamePictureWithinDetectorStep(const ActivePictureBounds& left,
			const ActivePictureBounds& right)
		{
			const int width = left.rasterWidth, height = left.rasterHeight;
			if (!ValidBounds(left, width, height) || !ValidBounds(right, width, height) ||
				left.trustedBarAxes == ActivePictureBounds::BarAxes::NONE ||
				left.trustedBarAxes != right.trustedBarAxes)
				return false;
			const int xStep = std::max(2, width / 960);
			const int yStep = std::max(2, height / 540);
			return std::abs(left.left - right.left) <= xStep &&
				std::abs(left.right - right.right) <= xStep &&
				std::abs(left.top - right.top) <= yStep &&
				std::abs(left.bottom - right.bottom) <= yStep;
		}


		bool ContainedBounds(const ActivePictureBounds& outer, const ActivePictureBounds& inner)
		{
			return ValidBounds(inner, outer.rasterWidth, outer.rasterHeight) &&
				inner.rasterWidth == outer.rasterWidth && inner.rasterHeight == outer.rasterHeight &&
				inner.left >= outer.left && inner.top >= outer.top &&
				inner.right <= outer.right && inner.bottom <= outer.bottom;
		}

		uint32_t NearBlackCropRevalidationSamples(double framesPerSecond)
		{
			if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0)
				framesPerSecond = 60.0;
			return static_cast<uint32_t>(std::max(2.0,
				std::ceil(framesPerSecond * 0.250) + 1.0));
		}

		uint32_t NearBlackBootstrapSamples(double framesPerSecond)
		{
			if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0)
				framesPerSecond = 60.0;
			// Bootstrap only reopens the independently verified normal acquisition
			// path. Keep enough time for the second gate while keeping their combined
			// recovery below about 0.7 seconds for 24 Hz sources.
			return static_cast<uint32_t>(std::max(2.0,
				std::ceil(framesPerSecond * 0.375) + 1.0));
		}

		bool ChromaAlignedTrustedContract(const ActivePictureBounds& bounds)
		{
			return ValidBounds(bounds, bounds.rasterWidth, bounds.rasterHeight) &&
				bounds.trustedBarAxes != ActivePictureBounds::BarAxes::NONE &&
				(bounds.left & 1) == 0 && (bounds.top & 1) == 0 &&
				(bounds.right & 1) == 0 && (bounds.bottom & 1) == 0;
		}

		void ResetNearBlackCropRevalidation(
			NearBlackPresentationEpisodeState& state)
		{
			state.revalidationStartedSourceSequence = 0;
			state.revalidationLastSourceSequence = 0;
			state.revalidationSamples = 0;
            state.revalidationNativeDarkRun = false;
		}

		void ResetNearBlackBootstrap(
			NearBlackPresentationEpisodeState& state)
		{
			state.bootstrapCandidateAvailable = false;
			state.bootstrapCandidate = {};
			state.bootstrapCandidateOrigin = ActivePictureAuthorityOrigin::NATIVE;
			state.bootstrapCandidateStartedTick = 0;
			state.bootstrapLastQualifiedTick = 0;
			state.bootstrapLastSourceSequence = 0;
			state.bootstrapSamples = 0;
		}

		int ChromaAlignedDisplacement(int pixels)
		{
			if (pixels > 0)
				return (pixels + 1) & ~1;
			if (pixels < 0)
				return -(((-pixels) + 1) & ~1);
			return 0;
		}
	}

	const char* DecisionOwnerName(DecisionOwner owner)
	{
		switch (owner)
		{
		case DecisionOwner::TRUSTED_CROP:
			return "trusted";
		case DecisionOwner::PIXEL_SAFE_RETENTION:
			return "pixel-safe";
		case DecisionOwner::SCENE_HOLD:
			return "scene-hold";
		case DecisionOwner::AMBIGUITY_HOLD:
			return "ambiguity-hold";
		case DecisionOwner::MOVING_PICTURE_HOLD:
            return "moving-picture-hold";
		case DecisionOwner::PICTURE_CONFIRMATION:
			return "picture-confirm";
		case DecisionOwner::BAR_REFINEMENT:
			return "bar-refinement";
		case DecisionOwner::VERTICAL_INSPECTION:
			return "inspection";
		case DecisionOwner::TRANSLATION_CONFIRMATION:
			return "translation-confirm";
		case DecisionOwner::FIT_CONFIRMATION:
			return "fit-confirm";
		case DecisionOwner::ENGAGE_BASE:
			return "engage-base";
		case DecisionOwner::RELEASE_BASE:
			return "release-base";
		case DecisionOwner::NEAR_BLACK_EPISODE:
			return "near-black-episode";
		case DecisionOwner::OUTWARD_FIT:
			return "fit";
		case DecisionOwner::VERTICAL_TRANSLATION:
			return "translation";
		case DecisionOwner::SUBMITTED_SCOPE_RETENTION:
			return "submitted-scope-retention";
		case DecisionOwner::FULL_RASTER:
		default:
			return "full-raster";
		}
	}

	const char* NearBlackPresentationModeName(
		NearBlackPresentationMode mode)
	{
		switch (mode)
		{
		case NearBlackPresentationMode::RETAIN_CROP:
			return "retain-crop";
		case NearBlackPresentationMode::FULL_RASTER:
			return "full-raster";
		default:
			return "inactive";
		}
	}

	bool NearBlackBlocksCaptionBackedLogicalAcquisition(bool globalDark,
		NearBlackPresentationMode episodeMode, bool currentStrictProof)
	{
		return (globalDark || episodeMode != NearBlackPresentationMode::INACTIVE) &&
            !currentStrictProof;
	}

	bool ScopeCaptionArAnalysisEligible(bool automaticCrop, bool configuredScreenActive,
		bool nlsRequested, bool fixedCropAspectConfigured,
		NearBlackPresentationMode episodeMode, bool scopeSubtitleFit)
	{
		return automaticCrop && configuredScreenActive && !nlsRequested &&
			!fixedCropAspectConfigured &&
			(scopeSubtitleFit ||
			 episodeMode == NearBlackPresentationMode::FULL_RASTER ||
			 episodeMode == NearBlackPresentationMode::RETAIN_CROP);
	}

	bool ColdCaptionArAnalysisEligible(bool automaticCrop, bool configuredScreenActive,
		bool nlsRequested, bool fixedCropAspectConfigured,
		NearBlackPresentationMode episodeMode)
	{
		return ScopeCaptionArAnalysisEligible(automaticCrop, configuredScreenActive,
			nlsRequested, fixedCropAspectConfigured, episodeMode, false);
	}

	CaptionBootstrapScanAction SelectCaptionBootstrapScan(
		const CaptionBootstrapScanInput& input)
	{
		if (!input.sourceGeneration || !input.analysisDomainKey ||
			!input.acceptedSequence || !input.bridgeFrames ||
			!input.coldProbeIntervalFrames)
			return CaptionBootstrapScanAction::NONE;
		const bool sameContext = input.attemptGeneration == input.sourceGeneration &&
			input.attemptDomainKey == input.analysisDomainKey;
		const bool bridge = sameContext && input.firstProofSequence &&
			input.acceptedSequence >= input.firstProofSequence &&
			input.acceptedSequence - input.firstProofSequence < input.bridgeFrames &&
			(!input.hasEstablishedGeometry || input.establishedMatchesProof);
		if (bridge) return CaptionBootstrapScanAction::BRIDGE;
		if (!input.hasEstablishedGeometry &&
			(!sameContext || !input.lastStandaloneSequence ||
			 (input.acceptedSequence >= input.lastStandaloneSequence &&
			  input.acceptedSequence - input.lastStandaloneSequence >=
				input.coldProbeIntervalFrames)))
			return CaptionBootstrapScanAction::COLD_PROBE;
		return CaptionBootstrapScanAction::NONE;
	}

	bool ShouldSuppressNearBlackBarGeometryMutation(bool acquisitionBlocked,
		bool stable, ActivePictureClassification classification)
	{
		return acquisitionBlocked && stable && classification ==
			ActivePictureClassification::BAR_CROP_TRUSTED;
	}

	BarContentEdge SelectVerticalBarContentEdge(
		float upperRequiredShift, float lowerRequiredShift)
	{
		// Bottom captions win conflicts: moving the source window down shifts
		// the displayed picture up, regardless of relative intrusion depth.
		if (lowerRequiredShift > 0.5f)
			return BarContentEdge::BOTTOM;
		if (upperRequiredShift > 0.5f)
			return BarContentEdge::TOP;
		return BarContentEdge::NONE;
	}

	VerticalBarContentDecision EvaluateVerticalBarContent(
		const VerticalBarContentInput& input)
	{
		VerticalBarContentDecision decision;
		auto overlayLike = [sampledColumns = input.sampledColumns](bool available,
			int occupiedDepth, int barPixels, int peakSamples)
		{
			if (!available || barPixels <= 0 || sampledColumns <= 0)
				return false;
			// A receiver volume bar may be almost full-width but only a few rows
			// deep. Conversely, compact UI may be tall but localized. Treat either
			// shape as overlay-like; real picture fill is broad in both dimensions.
			return occupiedDepth > 0 && peakSamples > 0 &&
				(occupiedDepth <= std::max(8, barPixels * 45 / 100) ||
				 peakSamples <= std::max(8, sampledColumns * 45 / 100));
		};
		decision.upperOverlayLike = overlayLike(input.upperContent,
			input.upperOccupiedDepth, input.upperBarPixels,
			input.upperPeakSamples);
		decision.lowerOverlayLike = overlayLike(input.lowerContent,
			input.lowerOccupiedDepth, input.lowerBarPixels,
			input.lowerPeakSamples);

		const bool upperRequiresPlacement = input.upperRequiredShift > 0.5f;
		const bool lowerRequiresPlacement = input.lowerRequiredShift > 0.5f;
		if ((upperRequiresPlacement &&
			(input.upperOccupiedDepth <= 0 || input.upperPeakSamples <= 0 ||
			 input.upperBarPixels <= 0 || input.sampledColumns <= 0)) ||
			(lowerRequiresPlacement &&
				(input.lowerOccupiedDepth <= 0 || input.lowerPeakSamples <= 0 ||
				 input.lowerBarPixels <= 0 || input.sampledColumns <= 0)))
		{
			decision.action = VerticalBarPresentationAction::FAIL_OPEN;
			return decision;
		}
		const bool overlayOnlyOpposesHeldTranslation =
			(input.upperContent && !input.lowerContent &&
				input.bottomTranslationHeld &&
				decision.upperOverlayLike);
		// Content on both bars is not, by itself, an aspect-ratio change. A
		// bottom subtitle can coexist with a top volume/menu overlay, and sparse
		// details near both edges can occur in the same frame. Only picture-like
		// (broad and deep) evidence may turn a two-edge observation into Fit.
		// When both edges are overlay-like, always reveal the bottom caption,
		// including when a top overlay acquired the previous translation hold.
		const bool bothEdgesContainPicture =
			input.upperContent && input.lowerContent &&
			(!decision.upperOverlayLike || !decision.lowerOverlayLike);
		const bool forceFit =
			bothEdgesContainPicture ||
			(input.upperContent && input.bottomTranslationHeld &&
				!decision.upperOverlayLike) ||
			(input.lowerContent && input.topTranslationHeld &&
				!decision.lowerOverlayLike) ||
			(upperRequiresPlacement && !decision.upperOverlayLike) ||
			(lowerRequiresPlacement && !decision.lowerOverlayLike);
		if (forceFit)
		{
			decision.action = VerticalBarPresentationAction::FIT;
			return decision;
		}
		// Preserve the current subtitle/volume placement across a short gap or a
		// thin opposite-edge overlay. Only picture-like evidence may request the
		// later fit that takes effect after the active subtitle hold releases.
		if (overlayOnlyOpposesHeldTranslation)
			return decision;

		float upperCandidate = decision.upperOverlayLike
			? input.upperRequiredShift : 0.0f;
		float lowerCandidate = decision.lowerOverlayLike
			? input.lowerRequiredShift : 0.0f;
		// Retain bottom priority across gaps in the lower cue. A top hold must
		// never suppress a newly detected bottom caption.
		if (input.bottomTranslationHeld && decision.lowerOverlayLike)
			upperCandidate = 0.0f;

		switch (SelectVerticalBarContentEdge(upperCandidate, lowerCandidate))
		{
		case BarContentEdge::TOP:
			decision.action = VerticalBarPresentationAction::TRANSLATE;
			decision.translationPixels = -input.upperRequiredShift;
			break;
		case BarContentEdge::BOTTOM:
			decision.action = VerticalBarPresentationAction::TRANSLATE;
			decision.translationPixels = input.lowerRequiredShift;
			break;
		default:
			break;
		}
		return decision;
	}

    VerticalBarContentDecision SelectVerticalBarContentPresentation(
        VerticalBarContentDecision observed, bool translationEnabled,
        const VerticalBarContentInput* current, bool classicBarContent)
    {
        if (translationEnabled && classicBarContent && current)
        {
            // Classic moves the native picture window to reveal occupied bar
            // pixels. Wide/deep UI cards are valid content too: neither glyph
            // recognition nor overlay shape is permission for this movement.
            // Native picture/aspect authority is established separately.
            observed = EvaluateVerticalBarContent(*current);
            if (observed.action == VerticalBarPresentationAction::FAIL_OPEN)
                return observed;
            const float lower = current->lowerContent ? current->lowerRequiredShift : 0.0f;
            const float upper = current->upperContent ? current->upperRequiredShift : 0.0f;
            observed.action = VerticalBarPresentationAction::NONE;
            observed.translationPixels = 0.0f;
            if (lower > 0.5f)
            {
                observed.action = VerticalBarPresentationAction::TRANSLATE;
                observed.translationPixels = lower;
            }
            else if (upper > 0.5f && !current->bottomTranslationHeld)
            {
                observed.action = VerticalBarPresentationAction::TRANSLATE;
                observed.translationPixels = -upper;
            }
            return observed;
        }
        if (!translationEnabled && current)
        {
            // Off reveals measured content regardless of its shape or glyphs.
            // Retain the classifier flags for diagnostics and Classic only.
            observed = EvaluateVerticalBarContent(*current);
            if (observed.action != VerticalBarPresentationAction::FAIL_OPEN &&
                ((current->upperContent && current->upperRequiredShift > 0.5f) ||
                 (current->lowerContent && current->lowerRequiredShift > 0.5f)))
            {
                observed.action = VerticalBarPresentationAction::FIT;
                observed.translationPixels = 0.0f;
            }
        }
        if (!translationEnabled && observed.action == VerticalBarPresentationAction::TRANSLATE)
        {
            observed.action = VerticalBarPresentationAction::FIT;
            observed.translationPixels = 0.0f;
        }
        return observed;
    }

	VerticalTranslationConfirmationDecision ConfirmVerticalTranslation(
		const VerticalTranslationConfirmationInput& input)
	{
		VerticalTranslationConfirmationDecision decision;
		decision.effective = input.observed;
		if (input.observed.action !=
				VerticalBarPresentationAction::TRANSLATE ||
			std::abs(input.observed.translationPixels) <= 0.5f)
		{
			return decision;
		}

		const bool acceptedSameDirection =
			input.acceptedTranslationActive &&
			std::abs(input.acceptedTranslationPixels) > 0.5f &&
			((input.acceptedTranslationPixels < 0.0f) ==
			 (input.observed.translationPixels < 0.0f));
		const bool acceptedAlreadyCoversObservation = acceptedSameDirection &&
			std::abs(input.observed.translationPixels) <=
				std::abs(input.acceptedTranslationPixels) +
				VERTICAL_TRANSLATION_STABILITY_PIXELS;
		if (acceptedAlreadyCoversObservation)
		{
			// Keep the buffered accepted placement explicit so harmless detector
			// jitter cannot leak an exact raw target to the interpolator.
			decision.effective.translationPixels =
				input.acceptedTranslationPixels;
			return decision;
		}

		const bool candidateMatches = input.previous.confirmations != 0 &&
			((input.previous.candidateTranslationPixels < 0.0f) ==
			 (input.observed.translationPixels < 0.0f)) &&
			std::abs(input.previous.candidateTranslationPixels -
				input.observed.translationPixels) <=
				VERTICAL_TRANSLATION_STABILITY_PIXELS;
		const bool repeatedSourceSample = candidateMatches &&
			input.sourceSequence != 0 &&
			input.previous.lastObservedSourceSequence == input.sourceSequence;
		if (repeatedSourceSample)
		{
			decision.state = input.previous;
			decision.pending = true;
			if (acceptedSameDirection)
				decision.effective.translationPixels =
					input.acceptedTranslationPixels;
			else
				decision.effective = {};
			return decision;
		}
		decision.state.candidateTranslationPixels = candidateMatches
			? (input.observed.translationPixels < 0.0f
				? std::min(input.previous.candidateTranslationPixels,
					input.observed.translationPixels)
				: std::max(input.previous.candidateTranslationPixels,
					input.observed.translationPixels))
			: input.observed.translationPixels;
		decision.state.confirmations = candidateMatches
			? input.previous.confirmations + 1 : 1;
		decision.state.lastObservedSourceSequence = input.sourceSequence;
		if (decision.state.confirmations >=
			VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED)
		{
			const float outwardBuffer = std::max(0.0f,
				input.targetBufferPixels);
			float bufferedMagnitude = std::abs(
				decision.state.candidateTranslationPixels) + outwardBuffer;
			if (input.maximumTranslationMagnitudePixels > 0.0f)
			{
				bufferedMagnitude = std::min(bufferedMagnitude,
					input.maximumTranslationMagnitudePixels);
			}
			decision.effective.translationPixels =
				decision.state.candidateTranslationPixels < 0.0f
				? -bufferedMagnitude : bufferedMagnitude;
			decision.state = {};
			decision.newlyAccepted = true;
			return decision;
		}

		decision.pending = true;
		if (acceptedSameDirection)
		{
			// Keep the already accepted target while a larger reveal settles. This
			// refreshes ownership without exposing an intermediate motion target.
			decision.effective.translationPixels =
				input.acceptedTranslationPixels;
		}
		else
		{
			decision.effective = {};
		}
		return decision;
	}

	OutwardPictureConfirmationDecision ConfirmOutwardPictureTransition(
		const OutwardPictureConfirmationState& previous,
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& candidate,
		const ActivePicturePresentationRetentionEvidence& evidence,
		uint64_t sourceGeneration,
		uint64_t sourceSequence)
	{
		OutwardPictureConfirmationDecision decision;
		const bool compatible = sourceGeneration != 0 &&
			ValidBounds(trustedGeometry, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight) &&
			ValidBounds(candidate, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight) &&
			(candidate.left < trustedGeometry.left || candidate.top < trustedGeometry.top ||
			 candidate.right > trustedGeometry.right || candidate.bottom > trustedGeometry.bottom);
		if (!compatible)
        {
            decision.diagnosticReason = "no-compatible-expansion";
            return decision;
        }

		const int tolerance = SamplingTolerance(trustedGeometry);
		const bool expandsVertical = (candidate.top < trustedGeometry.top || candidate.bottom > trustedGeometry.bottom) &&
			!SamplingEquivalentAxis(trustedGeometry.top, trustedGeometry.bottom, candidate.top, candidate.bottom, tolerance);
		const bool expandsHorizontal = (candidate.left < trustedGeometry.left || candidate.right > trustedGeometry.right) &&
			!SamplingEquivalentAxis(trustedGeometry.left, trustedGeometry.right, candidate.left, candidate.right, tolerance);
		// Sampling jitter may reach the normal transition deadband; this grants
		// no crop/presentation authority and never suppresses positive pixels.
		if (!expandsVertical && !expandsHorizontal)
        {
            decision.diagnosticReason = "measurement-step-only";
            return decision;
        }
		decision.outwardTransition = true;
		const bool stripsMatch = evidence.expansionStripsAvailable &&
			SameBounds(evidence.expansionBase, trustedGeometry) && SameBounds(evidence.expansionCandidate, candidate);
		// A supplied but mismatched certificate must not fall back to old strips.
		if (evidence.expansionStripsAvailable && !stripsMatch)
        {
            decision.diagnosticReason = "strip-certificate-mismatch";
            return decision;
        }
		const bool verticalPicture = !expandsVertical ||
			((candidate.top >= trustedGeometry.top || BroadPictureLike(stripsMatch ? evidence.expandingTop : evidence.excludedTop)) &&
			 (candidate.bottom <= trustedGeometry.bottom || BroadPictureLike(stripsMatch ? evidence.expandingBottom : evidence.excludedBottom)));
		const bool horizontalPicture = !expandsHorizontal ||
			((candidate.left >= trustedGeometry.left || BroadPictureLike(stripsMatch ? evidence.expandingLeft : evidence.excludedLeft)) &&
			 (candidate.right <= trustedGeometry.right || BroadPictureLike(stripsMatch ? evidence.expandingRight : evidence.excludedRight)));
		decision.broadOpposingPicture = evidence.analysisValid && evidence.presentationValid &&
			verticalPicture && horizontalPicture;
		decision.nativeDistributedPicture = !decision.broadOpposingPicture &&
			sourceSequence != 0 && expandsVertical && !expandsHorizontal &&
			HasStrictNativeDistributedPictureProof(evidence, trustedGeometry, candidate);
        const auto& native = evidence.activePicture;
        const auto& profile = native.verticalBarGuardProfile;
        const bool lowContrastPicture = sourceSequence != 0 && expandsVertical && !expandsHorizontal &&
            evidence.analysisValid && evidence.presentationValid && !evidence.EffectiveArNearBlack() && stripsMatch &&
            native.available && native.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            native.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            native.lowContrastBoundaryProof && profile.completed && profile.clean &&
            profile.apertureTop == candidate.top && profile.apertureBottom == candidate.bottom &&
            SameTrustedCropContract(native.trustedBounds,candidate) &&
            candidate.left == 0 && candidate.right == candidate.rasterWidth &&
            candidate.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM;
        if (!decision.broadOpposingPicture && !decision.nativeDistributedPicture && !lowContrastPicture)
        {
            decision.diagnosticReason = !evidence.analysisValid || !evidence.presentationValid
                ? "measurement-invalid" : (!verticalPicture && !horizontalPicture
                    ? "both-axes-not-broad" : (!verticalPicture ? "vertical-not-broad" : "horizontal-not-broad"));
            return decision;
        }
		const auto proofRoute = decision.broadOpposingPicture
			? OutwardPictureProofRoute::BROAD_OCCUPANCY
			: lowContrastPicture ? OutwardPictureProofRoute::NATIVE_LOW_CONTRAST_BOUNDARIES
            : OutwardPictureProofRoute::NATIVE_DISTRIBUTED_SUPPORT;

		const bool sequenceContinuous = sourceSequence == 0 || previous.lastObservedSourceSequence == 0 ||
			sourceSequence == previous.lastObservedSourceSequence ||
			sourceSequence == previous.lastObservedSourceSequence + 1;
		const bool sameEpisode = previous.sourceGeneration == sourceGeneration && sequenceContinuous &&
			previous.confirmations != 0 && SameBounds(previous.base, trustedGeometry) &&
			previous.base.trustedBarAxes == trustedGeometry.trustedBarAxes &&
			previous.proofRoute == proofRoute;
		const auto& reference = previous.referenceCandidate;
		const bool exactCandidate = SameBounds(reference, candidate) &&
			reference.trustedBarAxes == candidate.trustedBarAxes;
		// Only fresh exact strip evidence can bridge detector-step jitter in a
		// trusted outward picture candidate. Each edge may move one scan step,
		// but every comparison stays anchored to the first candidate in this
		// confirmation run. The current candidate/certificate remain exact.
		const bool samplingEquivalentCandidate = stripsMatch && !evidence.EffectiveArNearBlack() &&
			SameTrustedCropContract(evidence.expansionBase, trustedGeometry) &&
			SameTrustedCropContract(evidence.expansionCandidate, candidate) &&
			ValidBounds(reference, trustedGeometry.rasterWidth, trustedGeometry.rasterHeight) &&
			HasAuthorityForCroppedAxes(reference, trustedGeometry.rasterWidth, trustedGeometry.rasterHeight) &&
			HasAuthorityForCroppedAxes(candidate, trustedGeometry.rasterWidth, trustedGeometry.rasterHeight) &&
			reference.trustedBarAxes == candidate.trustedBarAxes &&
			ContainedBounds(reference, trustedGeometry) && ContainedBounds(candidate, trustedGeometry) &&
			SamePictureWithinDetectorStep(reference, candidate);
		const bool continues = sameEpisode && (exactCandidate || samplingEquivalentCandidate);
		const bool repeatedSourceSample = continues && sourceSequence != 0 &&
			previous.lastObservedSourceSequence == sourceSequence;
		decision.state.candidate = candidate;
		decision.state.base = trustedGeometry;
		decision.state.referenceCandidate = continues ? reference : candidate;
		decision.state.firstPictureSourceSequence = sameEpisode
			? previous.firstPictureSourceSequence : sourceSequence;
		decision.state.sourceGeneration = sourceGeneration;
		decision.state.proofRoute = proofRoute;
		decision.state.lastObservedSourceSequence = sourceSequence;
		decision.state.confirmations = repeatedSourceSample
			? previous.confirmations : (continues
			? std::min(OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED,
				previous.confirmations + 1) : 1);
		decision.authoritative = decision.state.confirmations >=
			OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
        decision.diagnosticReason = lowContrastPicture
            ? (decision.authoritative ? "native-low-contrast-confirmed" : "native-low-contrast-proof-pending")
            : decision.nativeDistributedPicture
            ? (decision.authoritative ? "native-distributed-confirmed" :
                (repeatedSourceSample ? "native-distributed-repeated-source" :
                 (previous.confirmations != 0 && !continues ? "native-distributed-proof-restarted" :
                  "native-distributed-proof-pending")))
            : (decision.authoritative ? "confirmed" :
                (repeatedSourceSample ? "repeated-source" :
                 (previous.confirmations != 0 && !continues ? "proof-restarted" : "proof-pending")));
		return decision;
	}

	VerticalFitConfirmationDecision ConfirmVerticalFit(
		const VerticalFitConfirmationState& previous,
		const VerticalBarContentDecision& observed,
		uint64_t sourceSequence, bool requireRepeatedSamples)
	{
		VerticalFitConfirmationDecision decision;
		decision.effective = observed;
		if (observed.action != VerticalBarPresentationAction::FIT)
            return decision;
        // Off already has a current pixel measurement around a trusted native
        // picture. Reveal it immediately before AR can make that base provisional.
        if (!requireRepeatedSamples && sourceSequence != 0)
        {
            decision.state.confirmations = VERTICAL_FIT_CONFIRMATIONS_REQUIRED;
            decision.state.lastObservedSourceSequence = sourceSequence;
            decision.newlyAccepted = previous.confirmations < VERTICAL_FIT_CONFIRMATIONS_REQUIRED;
            return decision;
        }
		if (sourceSequence != 0 &&
			previous.lastObservedSourceSequence == sourceSequence &&
			previous.confirmations != 0)
		{
			decision.state = previous;
			decision.pending = previous.confirmations <
				VERTICAL_FIT_CONFIRMATIONS_REQUIRED;
			if (decision.pending)
				decision.effective = {};
			return decision;
		}

		decision.state.confirmations = std::min(
			VERTICAL_FIT_CONFIRMATIONS_REQUIRED,
			previous.confirmations + 1);
		decision.state.lastObservedSourceSequence = sourceSequence;
		if (decision.state.confirmations <
			VERTICAL_FIT_CONFIRMATIONS_REQUIRED)
		{
			decision.effective = {};
			decision.pending = true;
			return decision;
		}

		decision.newlyAccepted = previous.confirmations <
			VERTICAL_FIT_CONFIRMATIONS_REQUIRED;
		return decision;
	}

	bool ShouldRetainTrustedBaseForVerticalInspection(
		bool subtitleFitEnabled,
		bool currentEnvelope,
		bool latestObservationCanAwaitInspection,
		bool leftExpansion,
		bool topExpansion,
		bool rightExpansion,
		bool bottomExpansion)
	{
		return subtitleFitEnabled && currentEnvelope &&
			latestObservationCanAwaitInspection &&
			!leftExpansion && !rightExpansion &&
			(topExpansion || bottomExpansion);
	}

	bool ShouldTrackDynamicPresentationGeometry(
		bool automaticCropEnabled, bool subtitleFitEnabled)
	{
		return automaticCropEnabled || subtitleFitEnabled;
	}

	bool CanResolveVerticalInspectionWithConfirmedFit(
		const VerticalInspectionFitResolutionInput& input)
	{
		if (!input.confirmedDenseFit || !input.denseAnalysisCurrent ||
			!input.outwardExpansionAvailable ||
			input.currentHorizontalExpansion ||
			input.outwardExpansionSourceGeneration == 0 ||
			input.outwardExpansionSourceGeneration !=
				input.frameSourceGeneration)
		{
			return false;
		}

		const int width = input.trustedBase.rasterWidth;
		const int height = input.trustedBase.rasterHeight;
		if (!ValidBounds(input.trustedBase, width, height) ||
			!CropEdgesAreChromaAligned(input.trustedBase, width, height) ||
			!ValidBounds(input.outwardExpansion, width, height) ||
			!CropEdgesAreChromaAligned(input.outwardExpansion, width, height))
		{
			return false;
		}

		return input.outwardExpansion.left == input.trustedBase.left &&
			input.outwardExpansion.right == input.trustedBase.right &&
			(input.outwardExpansion.top < input.trustedBase.top ||
			 input.outwardExpansion.bottom > input.trustedBase.bottom);
	}

	VerticalInspectionBridgeDecision UpdateVerticalInspectionBridge(
		const VerticalInspectionBridgeInput& input)
	{
		VerticalInspectionBridgeDecision decision;
		if (input.sourceGeneration == 0 || input.sourceSequence == 0 ||
			input.cropAuthorityResolved || input.fullRasterAuthorityResolved ||
			input.confirmedVerticalFitResolved || input.samplingRetentionResolved || input.containedRetentionResolved ||
			input.verticalPresentationOwnerAvailable)
		{
			return decision;
		}

		const bool sameBase = SameBounds(
			input.previous.trustedBase, input.trustedBase) &&
			input.previous.trustedBase.trustedBarAxes ==
				input.trustedBase.trustedBarAxes;
		const bool sameEpisode = input.previous.active &&
			input.previous.sourceGeneration == input.sourceGeneration &&
			input.previous.presentationEpoch == input.presentationEpoch &&
			sameBase;
		if (!sameEpisode)
		{
			if (!input.candidate)
				return decision;
			decision.state.active = true;
			decision.state.retentionConsumed = false;
			decision.state.denseAnalysisCompleted = false;
			decision.state.sourceGeneration = input.sourceGeneration;
			decision.state.presentationEpoch = input.presentationEpoch;
			decision.state.trustedBase = input.trustedBase;
			decision.state.firstCandidateSourceSequence = input.sourceSequence;
			decision.state.retainedSourceSequence = 0;
			decision.started = true;
		}
		else
		{
			decision.state = input.previous;
		}
		if (input.denseAnalysisCompleted)
			decision.state.denseAnalysisCompleted = true;
		if (!input.candidate)
			return decision;

		const bool repeatedRetainedSource =
			decision.state.retentionConsumed &&
			decision.state.retainedSourceSequence == input.sourceSequence;
		const bool insideBoundedInspectionWindow =
			input.sourceSequence >= decision.state.firstCandidateSourceSequence &&
			input.sourceSequence - decision.state.firstCandidateSourceSequence <
				VERTICAL_INSPECTION_MAX_SOURCE_SEQUENCES;
		decision.retain = input.retentionRequested &&
			(repeatedRetainedSource ||
			 (!decision.state.denseAnalysisCompleted &&
			  insideBoundedInspectionWindow));
		if (decision.retain && !repeatedRetainedSource)
		{
			decision.state.retentionConsumed = true;
			decision.state.retainedSourceSequence = input.sourceSequence;
		}
		else if (input.retentionRequested)
		{
			decision.state.failOpenLatched = true;
		}
		decision.expired =
			input.retentionRequested && !decision.retain;
		return decision;
	}

	bool IsVerticalBarPresentationActive(
		const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence)
	{
		if (state.action == VerticalBarPresentationAction::NONE)
			return false;
		if (state.sourceSequence != 0 &&
			state.sourceSequence == currentSourceSequence)
		{
			return true;
		}
		return holdMs != 0 && state.lastDetectionTick != 0 &&
			currentTick >= state.lastDetectionTick &&
			currentTick - state.lastDetectionTick <= holdMs;
	}

	bool IsVerticalBarPresentationActiveForFrame(
		const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence,
		bool analysisEvaluatedCurrentFrame,
		bool currentBarAuthority,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration)
	{
		if (IsVerticalBarPresentationActive(state, currentTick, holdMs,
			currentSourceSequence))
		{
			return true;
		}
		return !analysisEvaluatedCurrentFrame && currentBarAuthority &&
			state.action != VerticalBarPresentationAction::NONE &&
			evidenceSourceGeneration != 0 &&
			evidenceSourceGeneration == currentSourceGeneration;
	}

	bool CanRetainVerticalBarPresentationAcrossAuthorityGap(
		const VerticalBarPresentationState& state,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence)
	{
		return evidenceSourceGeneration != 0 &&
			evidenceSourceGeneration == currentSourceGeneration &&
			state.action != VerticalBarPresentationAction::NONE &&
			(IsVerticalBarPresentationActive(state, currentTick, holdMs,
				currentSourceSequence) || holdMs == 0);
	}

	bool ShouldDeferVerticalGeometryTransition(
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& candidateGeometry,
		ActivePictureClassification candidateClassification,
		const VerticalBarPresentationState& presentation,
		bool translationDriftActive,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration)
	{
		if (candidateClassification !=
				ActivePictureClassification::BAR_CROP_TRUSTED ||
			evidenceSourceGeneration == 0 ||
			evidenceSourceGeneration != currentSourceGeneration ||
			(!translationDriftActive && presentation.action !=
				VerticalBarPresentationAction::TRANSLATE) ||
			!ValidBounds(trustedGeometry, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight) ||
			!ValidBounds(candidateGeometry, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight))
		{
			return false;
		}
		return candidateGeometry.top < trustedGeometry.top ||
			candidateGeometry.bottom > trustedGeometry.bottom;
	}

	// A provisional vertical boundary cannot keep disproved horizontal bars
	// alive. Inspect the whole OLD side bands, independently of vertical scan
	// jitter, and require three adjacent native observations. This withdraws
	// authority only; it never promotes the provisional rectangle to a format.
	static OutwardPictureConfirmationDecision ConfirmPillarboxContradiction(
		const TransitionAdmissionInput& input)
	{
		const auto& base = input.trustedGeometry;
		const auto& horizontal = input.evidence.axisEvidence.horizontal;
		if (!input.trustedGeometryAvailable || !input.compatiblePresentation ||
			input.sourceGeneration == 0 || input.sourceSequence == 0 ||
			input.trustedGeneration != input.sourceGeneration ||
			!SameTrustedCropContract(base, input.presentationBeforeObservation) ||
			base.trustedBarAxes != ActivePictureBounds::BarAxes::LEFT_RIGHT ||
			base.top != 0 || base.bottom != base.rasterHeight ||
			base.left <= 0 || base.right >= base.rasterWidth ||
			!input.evidence.available ||
			input.evidence.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			input.evidence.classification != ActivePictureClassification::PROVISIONAL ||
			!horizontal.scanComplete || horizontal.barCandidate ||
			!ValidBounds(input.evidence.proposedBounds, base.rasterWidth, base.rasterHeight) ||
			input.evidence.proposedBounds.left != 0 ||
			input.evidence.proposedBounds.right != base.rasterWidth ||
			input.retention.EffectiveArNearBlack() ||
			// A majority of BOTH old bars must contain picture. A dark lower
			// quarter may withhold full-axis support; sparse captions/logos must
			// not withdraw an otherwise genuine pillarbox.
			input.retention.excludedLeft.blackFraction > 0.50 ||
			input.retention.excludedRight.blackFraction > 0.50 ||
			input.retention.excludedLeft.barPixels != base.left ||
			input.retention.excludedRight.barPixels != base.rasterWidth - base.right)
			return {};
		auto bands = input.retention;
		// These are whole old-side-band measurements, not the differently sized
		// expansion strips belonging to the provisional top/bottom candidate.
		bands.expansionStripsAvailable = false;
		return ConfirmOutwardPictureTransition(input.previousPillarboxContradiction,
			base, FullRaster(base.rasterWidth, base.rasterHeight), bands,
			input.sourceGeneration, input.sourceSequence);
	}

	TransitionAdmissionDecision EvaluateTransitionAdmission(const TransitionAdmissionInput& input)
	{
		TransitionAdmissionDecision decision;
		decision.pillarboxContradiction = ConfirmPillarboxContradiction(input);
		decision.observation = MakeActivePictureObservation(input.evidence,
			input.sourceSequence, input.framesPerSecond);
		decision.deferPresentation = input.evidence.available &&
			input.trustedGeometryAvailable && input.trustedGeneration == input.sourceGeneration &&
			ShouldDeferVerticalGeometryTransition(input.trustedGeometry,
				input.evidence.trustedBounds, input.evidence.classification,
				input.presentation, input.translationDriftActive,
				input.presentationEvidenceGeneration, input.sourceGeneration);
		decision.outward = input.compatiblePresentation && input.evidence.available
			? ConfirmOutwardPictureTransition(input.previousOutward, input.presentationBeforeObservation,
				input.outwardCandidate, input.retention, input.sourceGeneration, input.sourceSequence)
			: OutwardPictureConfirmationDecision{};
		const bool continuingEpisode = decision.outward.state.confirmations != 0 &&
			input.previousOutward.confirmations != 0 &&
			input.previousOutward.sourceGeneration == input.sourceGeneration &&
			SameTrustedCropContract(input.previousOutward.base, input.presentationBeforeObservation) &&
			decision.outward.state.firstPictureSourceSequence == input.previousOutward.firstPictureSourceSequence;
		decision.outward.state.verticalPresentationSeen =
			input.presentation.action == VerticalBarPresentationAction::TRANSLATE ||
			input.translationDriftActive ||
			(!continuingEpisode && input.presentation.action == VerticalBarPresentationAction::FIT) ||
			(continuingEpisode && input.previousOutward.verticalPresentationSeen);
		decision.deferOutward = decision.outward.outwardTransition && !decision.outward.authoritative;
		// Broad current picture on the expanding edges can supersede an old
		// subtitle action; a subtitle must not pin a genuinely obsolete aspect.
		decision.deferPresentation = decision.deferPresentation && !decision.outward.authoritative;
		decision.observation.transitionDeferred = decision.deferPresentation || decision.deferOutward;
		if (input.evidence.available)
		{
			decision.observation.bounds = input.evidence.classification == ActivePictureClassification::PROVISIONAL
				? input.evidence.proposedBounds : input.evidence.trustedBounds;
			decision.observation.classification = decision.deferPresentation || decision.deferOutward
				? ActivePictureClassification::PROVISIONAL : input.evidence.classification;
		}
		const auto& proposed = input.evidence.proposedBounds;
		const auto axes = input.evidence.trustedBounds.trustedBarAxes;
		auto coherentMargin = [](const ActivePictureEdgeEvidence& edge) {
			return edge.barPixels > 0 && edge.blackFraction >= 0.95 &&
				edge.lumaP90 <= std::min(104.0, edge.lumaFloor + 24.0) &&
				edge.lumaDispersion <= 24.0 && edge.texture <= 8.0 &&
				edge.neutralChromaFraction >= 0.90 && edge.continuity >= 0.99;
		};
		decision.deferPartialComposition = input.evidence.available &&
			input.evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			!input.evidence.axisEvidence.HasVerifiedVerticalCropProfile(input.evidence.trustedBounds) &&
			(axes == ActivePictureBounds::BarAxes::TOP_BOTTOM ||
			 axes == ActivePictureBounds::BarAxes::LEFT_RIGHT) &&
			input.compatiblePresentation && input.trustedGeometryAvailable &&
			input.sourceGeneration != 0 && input.trustedGeneration == input.sourceGeneration &&
			input.retention.analysisValid && input.retention.presentationValid &&
			!input.retention.EffectiveArNearBlack() && !decision.outward.broadOpposingPicture &&
			ValidBounds(proposed, input.trustedGeometry.rasterWidth, input.trustedGeometry.rasterHeight) &&
			proposed.left >= input.trustedGeometry.left && proposed.top >= input.trustedGeometry.top &&
			proposed.right <= input.trustedGeometry.right && proposed.bottom <= input.trustedGeometry.bottom &&
			coherentMargin(input.evidence.left) && coherentMargin(input.evidence.top) &&
			coherentMargin(input.evidence.right) && coherentMargin(input.evidence.bottom);
		// A queued decision must obey the same veto as the live model. A strict
		// vertical profile bypasses only this inferred-composition veto; it preserves
		// the unresolved horizontal pixels and grants no horizontal crop authority.
		decision.observation.transitionDeferred = decision.observation.transitionDeferred ||
			decision.deferPartialComposition || decision.pillarboxContradiction.authoritative;
		return decision;
	}

	PictureTransitionHandoff MakePictureTransitionHandoff(
		const TransitionAdmissionInput& input, const TransitionAdmissionDecision& admission,
		const ActivePictureTransitionDecision& transition, bool eligibleGeometryChange,
		uint64_t presentationEpoch)
	{
		PictureTransitionHandoff result;
		// A pending history recurrence can name its canonical trusted rectangle
		// one scan step from the current target. Retaining the OLD presentation
		// during that confirmation is safe; publication still checks current pixels.
		const auto& base = input.trustedGeometry;
		const auto& target = input.evidence.trustedBounds;
		const auto& proof = admission.outward;
		const auto& pixels = input.retention;
		if (!eligibleGeometryChange || transition.publish || !transition.stable ||
			!input.evidence.available || !input.trustedGeometryAvailable || !input.compatiblePresentation ||
			input.evidence.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			input.sourceGeneration == 0 || input.sourceSequence == 0 ||
			input.trustedGeneration != input.sourceGeneration ||
			!SameTrustedCropContract(input.presentationBeforeObservation, base) ||
			!SameTrustedCropContract(transition.stableBounds, base) ||
			!ValidBounds(base, base.rasterWidth, base.rasterHeight) ||
			!ValidBounds(target, base.rasterWidth, base.rasterHeight) ||
			!CropEdgesAreChromaAligned(base, base.rasterWidth, base.rasterHeight) ||
			!CropEdgesAreChromaAligned(target, base.rasterWidth, base.rasterHeight) ||
			base.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
			target.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
			base.left != 0 || base.right != base.rasterWidth ||
			target.left != base.left || target.right != base.right ||
			target.top >= base.top || target.bottom <= base.bottom ||
			!SameTrustedCropContract(input.outwardCandidate, target) ||
			!pixels.analysisValid || !pixels.presentationValid || pixels.EffectiveArNearBlack() ||
			!pixels.expansionStripsAvailable ||
			!SameTrustedCropContract(pixels.expansionBase, base) ||
			!SameTrustedCropContract(pixels.expansionCandidate, target) ||
			!proof.outwardTransition ||
			(!proof.broadOpposingPicture && !proof.nativeDistributedPicture) ||
			proof.state.confirmations == 0 || proof.state.verticalPresentationSeen ||
			proof.state.firstPictureSourceSequence == 0 ||
			input.sourceSequence < proof.state.firstPictureSourceSequence ||
			input.sourceSequence - proof.state.firstPictureSourceSequence >= PICTURE_HANDOFF_PENDING_FRAMES ||
			proof.state.sourceGeneration != input.sourceGeneration ||
			proof.state.lastObservedSourceSequence != input.sourceSequence ||
			!SameTrustedCropContract(proof.state.candidate, target) ||
			admission.deferPartialComposition ||
			(proof.authoritative &&
			 (transition.state != ActivePictureTransitionState::CANDIDATE_TRANSITION ||
			  !HasAuthorityForCroppedAxes(transition.bounds, base.rasterWidth, base.rasterHeight) ||
			  !ContainedBounds(transition.bounds, base) ||
			  !SamePictureWithinDetectorStep(transition.bounds, target))))
			return result;
		result.active = true;
		result.trustedBase = base;
		result.sourceGeneration = input.sourceGeneration;
		result.sourceSequence = input.sourceSequence;
		result.presentationEpoch = presentationEpoch;
		return result;
	}

	bool HasCurrentPictureTransitionHandoff(const Input& input)
	{
		const auto& handoff = input.pictureTransitionHandoff;
		return handoff.active && input.automaticCropEnabled && input.sharedGeometryAvailable &&
			!input.presentationFailOpen && !input.nearBlackEpisodeFullRaster &&
			!input.nearBlackEpisodeRetainCrop && !input.fullRasterPresentationAuthoritative &&
			!input.barCropRefinementHorizontalConflict &&
			input.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			input.latestObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			!input.latestObservationIsProvisional && !input.latestObservationIsUnavailable &&
			handoff.sourceGeneration != 0 && handoff.sourceGeneration == input.frameSourceGeneration &&
			input.geometrySourceGeneration == input.frameSourceGeneration &&
			handoff.sourceSequence != 0 && handoff.sourceSequence == input.frameSourceSequence &&
			handoff.presentationEpoch == input.framePresentationEpoch &&
			SameTrustedCropContract(handoff.trustedBase, input.geometry) &&
			ValidBounds(input.geometry, input.rasterWidth, input.rasterHeight) &&
			HasAuthorityForCroppedAxes(input.geometry, input.rasterWidth, input.rasterHeight) &&
			CropEdgesAreChromaAligned(input.geometry, input.rasterWidth, input.rasterHeight);
	}

    bool HasCurrentMovingPictureHold(const Input& input)
    {
        const auto& hold = input.movingPictureHold;
        // Retain the exact already-established scope contract while the motion
        // detector owns a gradual expansion. This is not new crop authority.
        return input.movingPictureTransition && input.automaticCropEnabled &&
            input.sharedGeometryAvailable && !input.presentationFailOpen &&
            !input.nearBlackEpisodeFullRaster && !input.nearBlackEpisodeRetainCrop &&
            !input.fullRasterPresentationAuthoritative &&
            !input.barCropRefinementHorizontalConflict && !hold.competingPresentation &&
            !input.verticalTranslationActive && !input.verticalTranslationConfirmationPending &&
            !input.verticalFitConfirmationPending && !input.verticalTranslationBaseRetentionActive &&
            !input.verticalTranslationEngageBaseRetentionActive &&
            input.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            input.latestObservationClassification != ActivePictureClassification::FULL_RASTER_TRUSTED &&
            hold.sourceGeneration != 0 && hold.sourceGeneration == input.frameSourceGeneration &&
            input.geometrySourceGeneration == input.frameSourceGeneration &&
            hold.sourceSequence != 0 && hold.sourceSequence == input.frameSourceSequence &&
            hold.presentationEpoch == input.framePresentationEpoch &&
            SameTrustedCropContract(hold.base, input.geometry) &&
            input.geometry.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            input.geometry.left == 0 && input.geometry.right == input.rasterWidth &&
            input.geometry.top > 0 && input.geometry.bottom < input.rasterHeight &&
            ValidBounds(input.geometry, input.rasterWidth, input.rasterHeight) &&
            HasAuthorityForCroppedAxes(input.geometry, input.rasterWidth, input.rasterHeight) &&
            CropEdgesAreChromaAligned(input.geometry, input.rasterWidth, input.rasterHeight);
    }

	PresentationObservationDecision ResolvePresentationObservation(
		const ActivePictureBounds& base, const ActivePictureEvidence& evidence,
		const ActivePicturePresentationRetentionEvidence& retention)
	{
		PresentationObservationDecision result;
		result.bounds = !evidence.available ? ActivePictureBounds{} :
			evidence.classification == ActivePictureClassification::PROVISIONAL ?
			evidence.proposedBounds : evidence.trustedBounds;
		const int width = base.rasterWidth, height = base.rasterHeight;
		if (!evidence.available || evidence.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			!ValidBounds(base, width, height) || !HasAuthorityForCroppedAxes(base, width, height) ||
			!ValidBounds(evidence.proposedBounds, width, height) ||
			!ValidBounds(evidence.trustedBounds, width, height) ||
			!retention.analysisValid || !retention.presentationValid ||
			!retention.expansionStripsAvailable || !SameBounds(retention.expansionBase, base) ||
			!retention.activePicture.available ||
			!SameBounds(retention.activePicture.proposedBounds, evidence.proposedBounds))
			return result;
		const auto& proposed = evidence.proposedBounds;
		const auto& visible = retention.outwardVisibleBounds;
		const bool visibleBounded = retention.outwardVisibleBoundsAvailable &&
			ValidBounds(visible, width, height) && visible.left <= base.left &&
			visible.top <= base.top && visible.right >= base.right && visible.bottom >= base.bottom;
		const auto axes = static_cast<uint8_t>(evidence.trustedBounds.trustedBarAxes);
		auto resolveAxis = [&](uint8_t axis, int baseFirst, int baseLast,
			int proposedFirst, int proposedLast, int visibleFirst, int visibleLast,
			bool bandsSafe, int& first, int& last) {
			if ((axes & axis) != 0) return;
			const int beforeFirst = first, beforeLast = last;
			if (bandsSafe && proposedFirst >= baseFirst && proposedLast <= baseLast)
			{
				// Retain the old edge; a missing confidence bit is not expansion.
				first = baseFirst; last = baseLast;
			}
			else if (visibleBounded)
			{
				// Keep every measured visible pixel, including subtitle pixels.
				// Dense subtitle arbitration decides translation versus picture fit.
				first = std::min(baseFirst, std::min(proposedFirst, visibleFirst));
				last = std::max(baseLast, std::max(proposedLast, visibleLast));
			}
			if (first != beforeFirst || last != beforeLast) result.resolvedAxes |= axis;
		};
		resolveAxis(static_cast<uint8_t>(ActivePictureBounds::BarAxes::LEFT_RIGHT),
			base.left, base.right, proposed.left, proposed.right, visible.left, visible.right,
			retention.excludedHorizontalBandsPixelSafe, result.bounds.left, result.bounds.right);
		resolveAxis(static_cast<uint8_t>(ActivePictureBounds::BarAxes::TOP_BOTTOM),
			base.top, base.bottom, proposed.top, proposed.bottom, visible.top, visible.bottom,
			retention.excludedVerticalBandsPixelSafe, result.bounds.top, result.bounds.bottom);
		result.bounds.aspectRatio = double(result.bounds.right-result.bounds.left) /
			std::max(1, result.bounds.bottom-result.bounds.top);
		return result;
	}

	bool HasHorizontalCropRefinementConflict(bool currentLeftExpansion,
		bool currentRightExpansion, bool observationAvailable, bool geometryAvailable,
		bool samplingReaffirmed, const ActivePictureBounds& observation,
		const ActivePictureBounds& geometry)
	{
		return currentLeftExpansion || currentRightExpansion ||
			(observationAvailable && geometryAvailable && !samplingReaffirmed &&
			 (observation.left < geometry.left || observation.right > geometry.right));
	}

	bool CanAnalyzeHeldVerticalBarGeometry(
		const HeldBarAnalysisInput& input)
	{
		const bool activeTranslation =
			input.presentation.action ==
				VerticalBarPresentationAction::TRANSLATE &&
			(input.holdMs == 0 ||
			 IsVerticalBarPresentationActive(input.presentation,
				 input.currentTick, input.holdMs,
				 input.currentSourceSequence));
		const bool pendingTranslation =
			input.translationConfirmationPending &&
			std::abs(input.pendingTranslationPixels) > 0.5f;
		const bool pendingFit = input.fitConfirmationPending;
		if (input.currentBarAuthority ||
			!input.trustedBarGeometryAvailable ||
			!input.storedBaseMatchesTrustedGeometry ||
			!input.currentEnvelopeAvailable ||
			input.latestClassification !=
				ActivePictureClassification::PROVISIONAL ||
			input.evidenceSourceGeneration == 0 ||
			input.evidenceSourceGeneration != input.currentSourceGeneration ||
			(!activeTranslation && !pendingTranslation && !pendingFit) ||
			!ValidBounds(input.trustedGeometry,
				input.trustedGeometry.rasterWidth,
				input.trustedGeometry.rasterHeight) ||
			!ValidBounds(input.currentEnvelope,
				input.trustedGeometry.rasterWidth,
				input.trustedGeometry.rasterHeight))
		{
			return false;
		}

		// The current evidence must be an outward envelope of the exact trusted
		// base, never a replacement or inward crop proposal.
		const bool outwardEnvelope =
			input.currentEnvelope.left <= input.trustedGeometry.left &&
			input.currentEnvelope.top <= input.trustedGeometry.top &&
			input.currentEnvelope.right >= input.trustedGeometry.right &&
			input.currentEnvelope.bottom >= input.trustedGeometry.bottom;
		if (!outwardEnvelope)
			return false;
		if (pendingFit)
		{
			return input.currentEnvelope.left == input.trustedGeometry.left &&
				input.currentEnvelope.right == input.trustedGeometry.right &&
				input.currentEnvelope.top < input.trustedGeometry.top &&
				input.currentEnvelope.bottom > input.trustedGeometry.bottom;
		}

		const float translationPixels = activeTranslation
			? input.presentation.translationPixels
			: input.pendingTranslationPixels;
		if (translationPixels < -0.5f)
			return input.currentEnvelope.top < input.trustedGeometry.top;
		if (translationPixels > 0.5f)
			return input.currentEnvelope.bottom > input.trustedGeometry.bottom;
		return false;
	}

	static bool HasExpiredTranslationHold(const VerticalBarPresentationState& state,
		uint64_t currentTick, uint64_t holdMs, uint64_t currentSequence)
	{
		return state.action == VerticalBarPresentationAction::TRANSLATE &&
			holdMs != 0 && state.lastDetectionTick != 0 &&
			currentTick >= state.lastDetectionTick &&
			currentTick - state.lastDetectionTick > holdMs &&
			state.sourceSequence != 0 && currentSequence > state.sourceSequence;
	}

	bool CanInspectRetiringTranslationFit(const HeldBarAnalysisInput& input,
		const VerticalFitConfirmationState& fit, bool inspectionBlocked)
	{
		if (inspectionBlocked ||
			!HasExpiredTranslationHold(input.presentation, input.currentTick,
				input.holdMs, input.currentSourceSequence) ||
			fit.confirmations < VERTICAL_FIT_CONFIRMATIONS_REQUIRED ||
			fit.lastObservedSourceSequence == 0 ||
			input.currentSourceSequence <= fit.lastObservedSourceSequence ||
			input.currentSourceSequence - fit.lastObservedSourceSequence > 3 ||
			input.trustedGeometry.left != 0 ||
			input.trustedGeometry.right != input.trustedGeometry.rasterWidth ||
			input.trustedGeometry.top <= 0 ||
			input.trustedGeometry.bottom >= input.trustedGeometry.rasterHeight)
			return false;

		// Reuse the same source/base and two-edge envelope checks as pending FIT.
		// Recent confirmed FIT grants one fresh inspection, not stale fit authority.
		// Three source frames are the existing dense-analysis sampling interval.
		auto inspection = input;
		inspection.fitConfirmationPending = true;
		return CanAnalyzeHeldVerticalBarGeometry(inspection);
	}

	VerticalBarPresentationState UpdateVerticalBarPresentation(
		const VerticalBarPresentationUpdateInput& input)
	{
		const bool previousActiveByHold = IsVerticalBarPresentationActive(
			input.previous, input.currentTick, input.holdMs,
			input.currentSourceSequence);
		const bool retiringTranslationFit = input.retiringTranslationFitInspection &&
			input.current.action == VerticalBarPresentationAction::FIT &&
			input.upperContent && input.lowerContent &&
			HasExpiredTranslationHold(input.previous, input.currentTick,
				input.holdMs, input.currentSourceSequence);
		const bool previousOwnsSample = previousActiveByHold ||
			(input.previousOwnsCurrentAnalysis && !retiringTranslationFit);
		VerticalBarPresentationState state = previousOwnsSample
			? input.previous : VerticalBarPresentationState{};
		VerticalBarContentDecision current = input.current;
		if (!input.translationEnabled &&
			current.action == VerticalBarPresentationAction::TRANSLATE)
		{
			current.action = VerticalBarPresentationAction::FIT;
			current.translationPixels = 0.0f;
		}
		if (current.action == VerticalBarPresentationAction::NONE)
		{
			if (!previousActiveByHold)
				return {};
			if (!input.translationEnabled &&
				state.action == VerticalBarPresentationAction::TRANSLATE)
			{
				state.action = VerticalBarPresentationAction::FIT;
				state.translationPixels = 0.0f;
			}
			return state;
		}

		// A subtitle/overlay translation owns presentation until its bounded
		// release timer expires. A fresh same-edge TRANSLATE below can re-arm the
		// timer and grow the shift, but a competing FIT must not turn a subtitle
		// change, a one-second gap, or a second overlay into an aspect decision.
		// Delaying a genuine new fit by this short, configurable interval is less
		// disruptive than visibly resizing picture geometry under text.
		if (current.action == VerticalBarPresentationAction::FAIL_OPEN)
		{
			state = {};
			state.action = current.action;
			state.lastDetectionTick = input.currentTick;
			state.sourceSequence = input.currentSourceSequence;
			return state;
		}

		// Once FIT has revealed controls, a changing one-edge classification must
		// not zoom back in underneath them. Refresh from fresh occupancy while
		// preserving the bounded per-edge extents; NONE still expires normally.
		if (previousActiveByHold && state.action == VerticalBarPresentationAction::FIT &&
			current.action == VerticalBarPresentationAction::TRANSLATE)
		{
			current.action = VerticalBarPresentationAction::FIT;
			current.translationPixels = 0.0f;
		}

		if (current.action == VerticalBarPresentationAction::FIT)
		{
			if (previousOwnsSample &&
				state.action == VerticalBarPresentationAction::TRANSLATE &&
				input.translationEnabled)
			{
				return state;
			}
			// Revisiting a dense FIT sample cannot renew either extent or the
			// action timer. Translation's existing cue ownership above is separate.
			if (input.previous.action == VerticalBarPresentationAction::FIT &&
				input.previous.sourceSequence != 0 && input.currentSourceSequence != 0 &&
				input.currentSourceSequence <= input.previous.sourceSequence)
			{
				if (input.currentSourceSequence < input.previous.sourceSequence ||
					input.currentTick < input.previous.lastDetectionTick ||
					(input.holdMs != 0 &&
					 input.currentTick - input.previous.lastDetectionTick > input.holdMs))
					return {}; // A stale sample cannot resurrect an expired FIT.
				return input.previous;
			}
			const bool retainPrevious = previousOwnsSample;
			const int retainedTop = retainPrevious ? state.detectedTop : 0;
			const int retainedBottom = retainPrevious ? state.detectedBottom : 0;
			// Older state has no per-extent stamps. Bind its initial extent age to
			// the original action tick once, never to another edge's later refresh.
			const uint64_t topTick = state.fitTopExtentTick != 0
				? state.fitTopExtentTick : state.lastDetectionTick;
			const uint64_t bottomTick = state.fitBottomExtentTick != 0
				? state.fitBottomExtentTick : state.lastDetectionTick;
			auto selectExtent = [&](int retained, uint64_t retainedTick,
				bool present, int measured, bool upper, uint64_t& extentTick)
			{
				if (present && (retained == 0 ||
					(upper ? measured <= retained : measured >= retained)))
				{
					extentTick = input.currentTick;
					return measured;
				}
				if (retained > 0 && input.holdMs != 0 && retainedTick != 0 &&
					input.currentTick >= retainedTick &&
					input.currentTick - retainedTick <= input.holdMs)
				{
					extentTick = retainedTick;
					return retained;
				}
				extentTick = present ? input.currentTick : 0;
				return present ? measured : 0;
			};
			state.action = current.action;
			state.translationPixels = 0.0f;
			state.detectedTop = selectExtent(retainedTop, topTick,
				input.upperContent, input.upperContentTop, true, state.fitTopExtentTick);
			state.detectedBottom = selectExtent(retainedBottom, bottomTick,
				input.lowerContent, input.lowerContentBottom, false, state.fitBottomExtentTick);
			state.lastDetectionTick = input.currentTick;
			state.sourceSequence = input.currentSourceSequence;
			return state;
		}

		const bool sameDirection = previousOwnsSample &&
			state.action == VerticalBarPresentationAction::TRANSLATE &&
			((state.translationPixels < 0.0f) ==
			 (current.translationPixels < 0.0f));
		if (!sameDirection)
			state = {};
		state.action = VerticalBarPresentationAction::TRANSLATE;
		if (current.translationPixels < 0.0f)
		{
			state.detectedTop = sameDirection && state.detectedTop > 0
				? std::min(state.detectedTop, input.upperContentTop)
				: input.upperContentTop;
			// Safety is directional: a new cue needing even one more source
			// pixel at this edge must apply immediately. Retaining a larger prior
			// reveal on the reverse move is what removes harmless detector jitter.
			if (!sameDirection || current.translationPixels <
				state.translationPixels)
			{
				state.translationPixels = current.translationPixels;
			}
		}
		else
		{
			state.detectedBottom = sameDirection
				? std::max(state.detectedBottom, input.lowerContentBottom)
				: input.lowerContentBottom;
			if (!sameDirection || current.translationPixels >
				state.translationPixels)
			{
				state.translationPixels = current.translationPixels;
			}
		}
		state.lastDetectionTick = input.currentTick;
		state.sourceSequence = input.currentSourceSequence;
		return state;
	}

	bool CurrentTranslationEnvelopeSupportsGeometry(
		const VerticalBarPresentationState& presentation,
		const ActivePictureBounds& trustedGeometry,
		const ActivePictureBounds& currentEnvelope)
	{
		if (presentation.action != VerticalBarPresentationAction::TRANSLATE ||
			!ValidBounds(trustedGeometry, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight) ||
			!ValidBounds(currentEnvelope, trustedGeometry.rasterWidth,
				trustedGeometry.rasterHeight) ||
			currentEnvelope.left != trustedGeometry.left ||
			currentEnvelope.right != trustedGeometry.right)
		{
			return false;
		}

		if (presentation.translationPixels < -0.5f)
		{
			const int shift = std::max(-trustedGeometry.top,
				ChromaAlignedDisplacement(static_cast<int>(
					std::lround(presentation.translationPixels))));
			return currentEnvelope.top < trustedGeometry.top &&
				currentEnvelope.bottom == trustedGeometry.bottom &&
				currentEnvelope.top >= trustedGeometry.top + shift;
		}
		if (presentation.translationPixels > 0.5f)
		{
			const int maximum = trustedGeometry.rasterHeight -
				trustedGeometry.bottom;
			const int shift = std::min(maximum,
				ChromaAlignedDisplacement(static_cast<int>(
					std::lround(presentation.translationPixels))));
			return currentEnvelope.top == trustedGeometry.top &&
				currentEnvelope.bottom > trustedGeometry.bottom &&
				currentEnvelope.bottom <= trustedGeometry.bottom + shift;
		}
		return false;
	}

	void VerticalTranslationDrift::Reset()
	{
		lastAppliedTranslationPixels = 0.0f;
		targetTranslationPixels = 0.0f;
		driftStartTranslationPixels = 0.0f;
		driftStartTick = 0;
		driftActive = false;
		finalBaseFramePending = false;
	}

	bool VerticalTranslationDrift::ConsumeFinalBaseFrame()
	{
		const bool pending = finalBaseFramePending;
		finalBaseFramePending = false;
		return pending;
	}

	float VerticalTranslationDrift::Resolve(
		float requestedTranslationPixels, uint64_t currentTick,
		uint64_t durationMs)
	{
		const bool targetChanged =
			std::abs(requestedTranslationPixels - targetTranslationPixels) > 0.5f;
		if (durationMs == 0)
		{
			const bool releasing = std::abs(requestedTranslationPixels) <= 0.5f &&
				std::abs(lastAppliedTranslationPixels) > 0.5f;
			Reset();
			targetTranslationPixels = requestedTranslationPixels;
			driftStartTranslationPixels = requestedTranslationPixels;
			lastAppliedTranslationPixels = requestedTranslationPixels;
			if (releasing)
				finalBaseFramePending = true;
			return requestedTranslationPixels;
		}
		if (targetChanged)
		{
			targetTranslationPixels = requestedTranslationPixels;
			driftStartTranslationPixels = lastAppliedTranslationPixels;
			driftStartTick = currentTick;
			driftActive = std::abs(targetTranslationPixels -
				driftStartTranslationPixels) > 0.5f;
			finalBaseFramePending = false;
		}
		if (!driftActive)
			return targetTranslationPixels;
		const uint64_t elapsed = currentTick >= driftStartTick
			? currentTick - driftStartTick : 0;
		if (elapsed >= durationMs)
		{
			lastAppliedTranslationPixels = targetTranslationPixels;
			driftStartTranslationPixels = targetTranslationPixels;
			driftActive = false;
			finalBaseFramePending = std::abs(targetTranslationPixels) <= 0.5f;
			return targetTranslationPixels;
		}
		const float progress = static_cast<float>(elapsed) /
			static_cast<float>(durationMs);
		lastAppliedTranslationPixels = driftStartTranslationPixels +
			(targetTranslationPixels - driftStartTranslationPixels) * progress;
		return lastAppliedTranslationPixels;
	}

	namespace
	{
		bool ApprovedFitCurrentContext(const ApprovedGenericFitHoldInput& input)
		{
			return input.sourceGeneration != 0 && input.sourceSequence != 0 &&
				input.currentTick != 0 && input.holdMs != 0 &&
				input.baseGeneration == input.sourceGeneration &&
				input.retentionGeneration == input.sourceGeneration &&
				input.rawSequence == input.sourceSequence && input.retentionSequence == input.sourceSequence &&
				input.rawAvailable && input.retentionValid && input.nearBlackEvaluated &&
				!input.nearBlack && !input.moving && !input.competingOwner && !input.denseArbitrationEnabled &&
				input.baseClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
				input.rawClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
				input.baseOrigin == ActivePictureAuthorityOrigin::NATIVE &&
				input.rawOrigin == ActivePictureAuthorityOrigin::NATIVE &&
				SameTrustedCropContract(input.base, input.retentionBase) &&
				SamePictureWithinDetectorStep(input.base, input.rawBounds) &&
				HasAuthorityForCroppedAxes(input.base, input.base.rasterWidth, input.base.rasterHeight) &&
				HasAuthorityForCroppedAxes(input.rawBounds, input.base.rasterWidth, input.base.rasterHeight) &&
				CropEdgesAreChromaAligned(input.base, input.base.rasterWidth, input.base.rasterHeight) &&
				input.rawBounds.left == input.base.left && input.rawBounds.right == input.base.right;
		}
		bool ApprovedFitCoversCurrent(const ApprovedGenericFitHoldInput& input,
			const ActivePictureBounds& bounds)
		{
			if (!ValidBounds(bounds, input.base.rasterWidth, input.base.rasterHeight) ||
				!CropEdgesAreChromaAligned(bounds, input.base.rasterWidth, input.base.rasterHeight) ||
				bounds.left != input.base.left || bounds.right != input.base.right ||
				bounds.top >= input.base.top || bounds.bottom <= input.base.bottom ||
				!ContainedBounds(bounds, input.base) || !ContainedBounds(bounds, input.rawBounds))
				return false;
			// A fresh occupied pixel outside the saved rectangle always vetoes the
			// hold, even if a separate band-safety summary says otherwise.
			return input.visibleAvailable ? ContainedBounds(bounds, input.visibleBounds) : input.excludedBandsSafe;
		}
	}

	ApprovedGenericFitHoldDecision ResolveApprovedGenericFitHold(const ApprovedGenericFitHoldInput& input)
	{
		ApprovedGenericFitHoldDecision decision;
		const auto& previous = input.previous;
		if (!previous.available || !ApprovedFitCurrentContext(input) ||
			previous.sourceGeneration != input.sourceGeneration ||
			previous.presentationEpoch != input.presentationEpoch ||
			!SameTrustedCropContract(previous.base, input.base) ||
			previous.admittedSequence == 0 || previous.admittedSequence > input.sourceSequence ||
			previous.admittedTick == 0 || previous.admittedTick > input.currentTick ||
			input.currentTick - previous.admittedTick > input.holdMs ||
			!ApprovedFitCoversCurrent(input, previous.bounds))
			return decision; // Retire invalid snapshots, never leave them dormant.
		decision.state = previous;
		decision.active = true;
		decision.bounds = previous.bounds;
		return decision;
	}
	ApprovedGenericFitHoldState RecordApprovedGenericFitHold(const ApprovedGenericFitHoldInput& input,
		const ActivePictureBounds& candidate, const ActivePictureBounds& admitted,
		bool freshGenericBothEdgeApproved, bool admissionSucceeded)
	{
		if (!freshGenericBothEdgeApproved || !admissionSucceeded || !ApprovedFitCurrentContext(input) ||
			!SameBounds(candidate, admitted) || !ApprovedFitCoversCurrent(input, candidate))
			return {};
		if (input.previous.available && input.previous.sourceGeneration == input.sourceGeneration &&
			input.previous.presentationEpoch == input.presentationEpoch &&
			input.sourceSequence <= input.previous.admittedSequence)
		{
			if (input.sourceSequence != input.previous.admittedSequence ||
				!SameBounds(candidate, input.previous.bounds))
				return {};
			return ResolveApprovedGenericFitHold(input).state;
		}
		ApprovedGenericFitHoldState state;
		state.available = true;
		state.base = input.base;
		state.bounds = candidate;
		state.sourceGeneration = input.sourceGeneration;
		state.presentationEpoch = input.presentationEpoch;
		state.admittedSequence = input.sourceSequence;
		state.admittedTick = input.currentTick;
		return state;
	}

	bool CanUseSamplingRefinementOutwardFit(const SamplingRefinementOutwardFitInput& input)
	{
		// A small native measurement change may reveal the current pixel envelope,
		// but cannot establish or reaffirm the logical crop. Never reuse a union
		// of old overlays or override the detailed presentation owners.
		if (!input.rawAvailable || !input.visibleAvailable || !input.outwardAvailable ||
			!input.nearBlackEvaluated || input.nearBlack || input.moving ||
			input.competingOwner || input.denseArbitrationEnabled ||
			input.frameGeneration == 0 || input.frameSequence == 0 ||
			input.baseGeneration != input.frameGeneration ||
			input.visibleGeneration != input.frameGeneration ||
			input.rawSequence != input.frameSequence || input.visibleSequence != input.frameSequence ||
			input.baseClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			input.rawClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			input.baseOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			input.rawOrigin != ActivePictureAuthorityOrigin::NATIVE)
			return false;

		const int width = input.base.rasterWidth, height = input.base.rasterHeight;
		if (!SameTrustedCropContract(input.base, input.visibleBase) ||
			!SamePictureWithinDetectorStep(input.base, input.rawBounds) ||
			!HasAuthorityForCroppedAxes(input.base, width, height) ||
			!HasAuthorityForCroppedAxes(input.rawBounds, width, height) ||
			!CropEdgesAreChromaAligned(input.base, width, height) ||
			input.rawBounds.left != input.base.left || input.rawBounds.right != input.base.right ||
			!ValidBounds(input.visibleBounds, width, height) ||
			!ValidBounds(input.outwardBounds, width, height) ||
			!CropEdgesAreChromaAligned(input.outwardBounds, width, height) ||
			input.outwardBounds.left != input.base.left || input.outwardBounds.right != input.base.right)
			return false;

		return input.visibleBounds.top < input.base.top &&
			input.visibleBounds.bottom > input.base.bottom &&
			input.outwardBounds.top < input.base.top &&
			input.outwardBounds.bottom > input.base.bottom &&
			ContainedBounds(input.outwardBounds, input.base) &&
			ContainedBounds(input.outwardBounds, input.rawBounds) &&
			ContainedBounds(input.outwardBounds, input.visibleBounds);
	}

	VerticalBarPresentationResolution ResolveVerticalBarPresentation(
		const VerticalBarPresentationResolutionInput& input)
	{
		VerticalBarPresentationResolution decision;
		decision.action = input.detailedAction;
		decision.translationPixels = input.translationPixels;
		if (input.detailedAction == VerticalBarPresentationAction::FAIL_OPEN)
			return decision;
		if (input.detailedAction == VerticalBarPresentationAction::FIT)
		{
			decision.translationPixels = 0.0f;
			return decision;
		}
		if (input.detailedAction == VerticalBarPresentationAction::TRANSLATE)
		{
			if (std::abs(input.translationPixels) <= 0.5f)
			{
				// The first sample of a timed engage is intentionally still at the
				// trusted base. Ordinary zero-shift metadata remains invalid/fail-open.
				decision.action = input.zeroTranslationRetainsTrustedBase
					? VerticalBarPresentationAction::NONE
					: VerticalBarPresentationAction::FAIL_OPEN;
				decision.translationPixels = 0.0f;
				return decision;
			}
			if (input.authoritativeTop < 0 ||
				input.authoritativeBottom <= input.authoritativeTop ||
				input.authoritativeBottom > input.rasterHeight)
			{
				decision.action = VerticalBarPresentationAction::FAIL_OPEN;
				decision.translationPixels = 0.0f;
				return decision;
			}
			int requestedShift = ChromaAlignedDisplacement(
				static_cast<int>(input.translationPixels < 0.0f
					? std::floor(input.translationPixels)
					: std::ceil(input.translationPixels)));
			// The dense bar pass owns the exact one-edge overlay extent and already
			// includes configured padding in this request. A coarse current-frame
			// envelope is classification evidence, not a second motion target. Using
			// it here made the target alternate on sampled/non-sampled frames even
			// after dense target confirmation had succeeded.

			const int minimumShift = -input.authoritativeTop;
			const int maximumShift =
				input.rasterHeight - input.authoritativeBottom;
			// Do not apply a partial translation which would still cut the declared
			// same-edge content. When the full translation cannot fit in the raster,
			// preserving all visible pixels takes the bounded Fit path.
			if (requestedShift < minimumShift || requestedShift > maximumShift)
			{
				decision.action = VerticalBarPresentationAction::FIT;
				decision.translationPixels = 0.0f;
				return decision;
			}
			const int appliedShift = requestedShift;
			const int translatedTop = input.authoritativeTop + appliedShift;
			const int translatedBottom =
				input.authoritativeBottom + appliedShift;
			if (translatedTop < 0 || translatedBottom > input.rasterHeight)
			{
				decision.action = VerticalBarPresentationAction::FAIL_OPEN;
				decision.translationPixels = 0.0f;
				return decision;
			}
			decision.translationPixels = static_cast<float>(appliedShift);
			return decision;
		}
		if (!input.denseVerticalArbitrationEnabled &&
			input.genericVerticalFitConfirmed &&
			input.genericVerticalFitAuthoritative &&
			input.genericUpperExpansion && input.genericLowerExpansion)
			decision.action = VerticalBarPresentationAction::FIT;
		return decision;
	}

	VerticalBarRendererRouting ResolveVerticalBarRendererRouting(
		const VerticalBarPresentationResolution& resolution)
	{
		VerticalBarRendererRouting routing;
		switch (resolution.action)
		{
		case VerticalBarPresentationAction::TRANSLATE:
			routing.translationPixels = static_cast<int>(
				resolution.translationPixels < 0.0f
					? std::floor(resolution.translationPixels)
					: std::ceil(resolution.translationPixels));
			if (routing.translationPixels == 0)
				routing.failOpen = true;
			else
				routing.translationActive = true;
			break;
		case VerticalBarPresentationAction::FIT:
			routing.fitActive = true;
			break;
		case VerticalBarPresentationAction::FAIL_OPEN:
			routing.failOpen = true;
			break;
		case VerticalBarPresentationAction::NONE:
		default:
			break;
		}
		return routing;
	}

	KnownFullRasterRetentionState UpdateKnownFullRasterRetention(
		const KnownFullRasterRetentionInput& input)
	{
		KnownFullRasterRetentionState state = input.previous;
		const bool sameContext = state.sourceGeneration == input.sourceGeneration &&
			state.presentationEpoch == input.presentationEpoch &&
			state.rasterWidth == input.frameWidth && state.rasterHeight == input.frameHeight;
		if (!sameContext)
		{
			state = {};
			state.sourceGeneration = input.sourceGeneration;
			state.presentationEpoch = input.presentationEpoch;
			state.rasterWidth = input.frameWidth;
			state.rasterHeight = input.frameHeight;
		}
		auto withdraw = [&state]()
		{
			state.available = false;
			state.reaffirmationSamples = 0;
		};
		if (!input.analysisValid || input.sourceGeneration == 0 ||
			input.sourceSequence == 0 || input.frameWidth <= 0 || input.frameHeight <= 0)
		{
			withdraw();
			state.lastEvaluatedSequence = std::max(state.lastEvaluatedSequence, input.sourceSequence);
			return state;
		}
		const auto exactFullRaster = [&](const ActivePictureBounds& bounds)
		{
			return bounds.left == 0 && bounds.top == 0 &&
				bounds.right == input.frameWidth && bounds.bottom == input.frameHeight &&
				bounds.rasterWidth == input.frameWidth && bounds.rasterHeight == input.frameHeight &&
				bounds.trustedBarAxes == ActivePictureBounds::BarAxes::NONE;
		};
		// Raw contradictions may withdraw even on a duplicate. Suppression of
		// dark crop acquisition must not disguise genuine opposing-bar evidence.
		if (input.measurementCurrent && input.rawClassification ==
			ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			withdraw();
			state.lastEvaluatedSequence = std::max(state.lastEvaluatedSequence, input.sourceSequence);
			return state;
		}
		const bool currentFullEvidence = input.measurementCurrent &&
			input.rawClassification == ActivePictureClassification::FULL_RASTER_TRUSTED &&
			exactFullRaster(input.rawBounds);
		const bool committedContextMatches = input.committedFullAvailable &&
			input.committedSourceGeneration == input.sourceGeneration &&
			input.committedPresentationEpoch == input.presentationEpoch &&
			input.committedSourceSequence != 0 &&
			input.committedSourceSequence <= input.sourceSequence &&
			exactFullRaster(input.committedBounds);
		const bool freshCommittedFull = currentFullEvidence && committedContextMatches &&
			input.committedSourceSequence == input.sourceSequence &&
			input.committedSourceSequence > state.lastCommittedSequence;
		// A cut remains evidence when notification cooldown suppresses the scene
		// event. Never let that debounce preserve a full-frame lease into an
		// unclassified dark scene, or accumulate recovery proof while it persists.
		if (input.independentCutEvidence)
		{
			withdraw();
			// A genuine new full-frame publication is still recorded, but it must
			// be freshly reaffirmed after the pending cut evidence has cleared.
			if (!input.cadenceRepeat && input.sourceSequence > state.lastEvaluatedSequence && freshCommittedFull)
				state.lastCommittedSequence = input.committedSourceSequence;
			state.lastEvaluatedSequence = std::max(state.lastEvaluatedSequence, input.sourceSequence);
			return state;
		}
		if (input.sceneBoundary)
		{
			state.reaffirmationSamples = 0;
			if (!(state.available && currentFullEvidence))
				state.available = false;
		}
		if (input.sourceSequence < state.lastEvaluatedSequence)
		{
			withdraw();
			return state;
		}
		if (input.sourceSequence == state.lastEvaluatedSequence)
			return state; // Neither repeats nor cached measurements build proof.
		if (input.cadenceRepeat)
		{
			withdraw();
			state.lastEvaluatedSequence = input.sourceSequence;
			return state;
		}
		state.lastEvaluatedSequence = input.sourceSequence;
		if (freshCommittedFull)
		{
			state.available = true;
			state.lastCommittedSequence = input.committedSourceSequence;
			state.reaffirmationSamples = 0;
		}
		else if (!state.available)
		{
			// Stable matching geometry deliberately does not publish again. Two
			// fresh, non-dark affirmative observations can instead revalidate its
			// real prior commitment, using the normal transition confirmation count.
			const bool reaffirmed = !input.sceneBoundary && currentFullEvidence &&
				input.nearBlackEvaluated && !input.globalNearBlack && committedContextMatches &&
				state.lastCommittedSequence != 0 &&
				input.committedSourceSequence == state.lastCommittedSequence;
			if (reaffirmed)
			{
				if (state.reaffirmationSamples < ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS)
					++state.reaffirmationSamples;
				state.available = state.reaffirmationSamples >=
					ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS;
			}
			else if (input.measurementCurrent || !committedContextMatches)
				state.reaffirmationSamples = 0;
		}
		return state;
	}


	KnownFullRasterRetentionState UpdateKnownFullRasterRetentionForScene(
		KnownFullRasterRetentionInput& input, const SceneDetectorResult& scene,
		bool retainFullRasterAtDarknessBoundary)
	{
		input.sceneBoundary = !input.cadenceRepeat && scene.safeBoundary &&
			!retainFullRasterAtDarknessBoundary;
		input.independentCutEvidence = !input.cadenceRepeat &&
			scene.sourceSequence == input.sourceSequence &&
			(scene.hardCutCandidate || scene.hardCutConfirmed);
		return UpdateKnownFullRasterRetention(input);
	}

	bool CanRetainKnownFullRasterAtDarknessBoundary(
		const KnownFullRasterDarknessBoundaryInput& input)
	{
		const auto& current = input.retention;
		const auto& previous = current.previous;
		if (!input.safeBoundary || !input.nearBlackEntry || !input.differenceEvaluated || input.hardCutCandidate ||
			input.hardCutConfirmed || !previous.available || !current.analysisValid ||
			!current.measurementCurrent || current.cadenceRepeat ||
			current.sourceGeneration == 0 || current.sourceSequence == 0 ||
			current.frameWidth <= 0 || current.frameHeight <= 0 ||
			current.sourceSequence <= previous.lastEvaluatedSequence ||
			previous.sourceGeneration != current.sourceGeneration ||
			previous.presentationEpoch != current.presentationEpoch ||
			previous.rasterWidth != current.frameWidth || previous.rasterHeight != current.frameHeight ||
			current.rawClassification == ActivePictureClassification::BAR_CROP_TRUSTED)
			return false;
		const auto& bounds = current.committedBounds;
		return current.committedFullAvailable && previous.lastCommittedSequence != 0 &&
			current.committedSourceSequence == previous.lastCommittedSequence &&
			current.committedSourceSequence <= previous.lastEvaluatedSequence &&
			current.committedSourceGeneration == current.sourceGeneration &&
			current.committedPresentationEpoch == current.presentationEpoch &&
			bounds.left == 0 && bounds.top == 0 && bounds.right == current.frameWidth &&
			bounds.bottom == current.frameHeight && bounds.rasterWidth == current.frameWidth &&
			bounds.rasterHeight == current.frameHeight &&
			bounds.trustedBarAxes == ActivePictureBounds::BarAxes::NONE;
	}

	bool UpdateFullRasterPresentationAuthority(bool previouslyAuthoritative,
		ActivePictureClassification currentClassification,
		bool currentBoundsAreFullRaster, ActivePictureAuthorityOrigin origin)
	{
		if (origin == ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT ||
            origin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN)
			return previouslyAuthoritative;
		if (currentClassification ==
			ActivePictureClassification::FULL_RASTER_TRUSTED)
			return currentBoundsAreFullRaster;
		if (currentClassification ==
			ActivePictureClassification::BAR_CROP_TRUSTED)
			return false;
		return previouslyAuthoritative;
	}

	bool CommitSparseTransitionPresentationAuthority(bool previouslyAuthoritative,
		const ActivePictureTransitionDecision& decision)
	{
		const auto& bounds = decision.bounds;
		const bool committed = decision.publish && decision.stable && !decision.clearTransition &&
			(decision.authorityOrigin == ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT ||
             decision.authorityOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN) &&
			decision.authoritativeClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			bounds.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
			bounds.rasterWidth > 0 && bounds.rasterHeight > 0 && bounds.left == 0 &&
			bounds.right == bounds.rasterWidth && bounds.top > 0 &&
			bounds.bottom < bounds.rasterHeight && bounds.bottom > bounds.top;
		return committed ? false : previouslyAuthoritative;
	}

	bool SparseTransitionMayReaffirmOwnedCrop(const ActivePictureBounds& owned,
		ActivePictureAuthorityOrigin ownedOrigin, const ActivePictureEvidence& evidence)
	{
		if (evidence.authorityOrigin != ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT &&
            evidence.authorityOrigin != ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN)
			return true;
		return ownedOrigin == evidence.authorityOrigin &&
			owned.trustedBarAxes == evidence.trustedBounds.trustedBarAxes &&
			SameBounds(owned, evidence.trustedBounds);
	}

	bool RequiresPerFramePresentationInspection(
		bool trustedCropIsCurrentGeneration,
		bool sceneSnapshotIsCurrentGeneration,
		bool pixelSafeRetentionActive)
	{
		return trustedCropIsCurrentGeneration ||
			sceneSnapshotIsCurrentGeneration || pixelSafeRetentionActive;
	}

	bool RequiresImmediateSubtitleBarAnalysis(bool currentBarAuthority,
		bool retentionJustBecameUnsafe,
		bool frameLocalRetentionEvaluated, bool frameLocalRetentionSafe,
		bool translationAlreadyActive)
	{
		return currentBarAuthority && retentionJustBecameUnsafe &&
			frameLocalRetentionEvaluated &&
			!frameLocalRetentionSafe && !translationAlreadyActive;
	}

	SubtitleInspectionDecision UpdateSubtitleInspection(const SubtitleInspectionInput& input)
	{
		SubtitleInspectionDecision result;
		result.state = input.previous;
		if (result.state.sourceGeneration != input.sourceGeneration) result.state = {};
		const bool evaluated = input.sourceGeneration != 0 && input.barAuthorityAvailable &&
			input.measurementCurrent && input.retention.analysisValid && input.retention.presentationValid &&
			SameBounds(input.measuredBase, input.base);
		// Stale or unavailable evidence cannot consume the next onset trigger.
		if (!evaluated) return result;
		const bool sameBase = result.state.sourceGeneration == input.sourceGeneration &&
			SameBounds(result.state.base, input.base);
		const bool unsafe = !input.retention.excludedVerticalBandsPixelSafe;
		result.forceAnalysis = RequiresImmediateSubtitleBarAnalysis(input.barAuthorityAvailable,
			unsafe && (!sameBase || !result.state.verticalUnsafe), true,
			input.retention.excludedVerticalBandsPixelSafe, input.translationAlreadyActive);
		result.state = {input.sourceGeneration, input.base, unsafe};
		return result;
	}

    PresentationEnvelopeExtentState UpdatePresentationEnvelopeExtents(
        const PresentationEnvelopeExtentState& previous,
        const ActivePictureBounds& base, const ActivePictureBounds& current,
        uint64_t sourceGeneration, uint64_t sourceSequence,
        uint64_t now, uint64_t holdMs)
    {
        PresentationEnvelopeExtentState result;
        if (!sourceGeneration || !sourceSequence || !now ||
            !ValidBounds(base,base.rasterWidth,base.rasterHeight) ||
            !ValidBounds(current,base.rasterWidth,base.rasterHeight) ||
            !CropEdgesAreChromaAligned(base,base.rasterWidth,base.rasterHeight) ||
            !CropEdgesAreChromaAligned(current,base.rasterWidth,base.rasterHeight) ||
            !ContainedBounds(current,base)) return result;
        const bool same = previous.sourceGeneration == sourceGeneration &&
            SameBounds(previous.base,base) && previous.base.trustedBarAxes == base.trustedBarAxes;
        if (same && (sourceSequence < previous.lastObservationSequence ||
            now < previous.lastObservationTick)) return result;
        if (same && sourceSequence == previous.lastObservationSequence) return previous;
        if (same) result=previous;
        result.base=base; result.sourceGeneration=sourceGeneration;
        result.lastObservationSequence=sourceSequence; result.lastObservationTick=now;
        const bool expands=current.left<base.left || current.top<base.top ||
            current.right>base.right || current.bottom>base.bottom;
        // Every retained outer coordinate owns its deadline. A nearer current
        // cue is evidence for that nearer coordinate, not a refresh of an older
        // and wider cue on this edge (or on another edge).
        const bool retainHistory = same && previous.available && holdMs != 0 &&
            ValidBounds(previous.bounds,base.rasterWidth,base.rasterHeight) &&
            CropEdgesAreChromaAligned(previous.bounds,base.rasterWidth,base.rasterHeight) &&
            ContainedBounds(previous.bounds,base);
        const std::array<int,4> baseEdges{{base.left,base.top,base.right,base.bottom}};
        const std::array<int,4> currentEdges{{current.left,current.top,current.right,current.bottom}};
        const std::array<int,4> previousEdges{{previous.bounds.left,previous.bounds.top,
            previous.bounds.right,previous.bounds.bottom}};
        std::array<int,4> selected=currentEdges;
        result.bounds=base;
        result.available=false;
        for (size_t edge=0; edge<selected.size(); ++edge)
        {
            const bool growing = edge>=2;
            const bool currentExpands = growing ? currentEdges[edge]>baseEdges[edge] :
                currentEdges[edge]<baseEdges[edge];
            const bool currentReachesPrevious = growing ? currentEdges[edge]>=previousEdges[edge] :
                currentEdges[edge]<=previousEdges[edge];
            const uint64_t priorTick=previous.extentTicks[edge];
            const bool liveExtent = retainHistory && priorTick != 0 && now>=priorTick &&
                now-priorTick<=holdMs;
            if (liveExtent && !currentReachesPrevious)
            {
                selected[edge]=previousEdges[edge];
                result.extentTicks[edge]=priorTick;
            }
            else
                result.extentTicks[edge]=currentExpands ? now : 0;
            result.available = result.available || selected[edge]!=baseEdges[edge];
        }
        result.bounds.left=selected[0]; result.bounds.top=selected[1];
        result.bounds.right=selected[2]; result.bounds.bottom=selected[3];
        if (expands)
        {
            result.sourceSequence=sourceSequence;
            result.lastDetectionTick=now;
        }
        result.bounds.aspectRatio=static_cast<double>(result.bounds.right-result.bounds.left)/
            std::max(1,result.bounds.bottom-result.bounds.top);
        result.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        return result;
    }
    ActivePictureBounds SelectPresentationEnvelopeBounds(const PresentationEnvelopeSelectionInput& input)
    {
        const int width=input.effectiveBase.rasterWidth, height=input.effectiveBase.rasterHeight;
        if (!input.envelopeActive || !input.frameGeneration || !input.frameSequence ||
            !ValidBounds(input.effectiveBase,width,height) ||
            !CropEdgesAreChromaAligned(input.effectiveBase,width,height))
            return input.effectiveBase;
        // Only current pixels may follow a same-frame native publication to a
        // different base. Historical extents keep their exact original contract.
        const bool sameBase=SameBounds(input.history.base,input.effectiveBase) &&
            input.history.base.trustedBarAxes==input.effectiveBase.trustedBarAxes;
        if (input.history.available && sameBase &&
            input.history.sourceGeneration==input.frameGeneration &&
            ValidBounds(input.history.bounds,width,height) &&
            CropEdgesAreChromaAligned(input.history.bounds,width,height) &&
            ContainedBounds(input.history.bounds,input.effectiveBase))
            return input.history.bounds;
        if (input.currentGeneration==input.frameGeneration &&
            input.currentSequence==input.frameSequence &&
            ValidBounds(input.currentBounds,width,height) &&
            CropEdgesAreChromaAligned(input.currentBounds,width,height))
            return input.currentBounds;
        return input.effectiveBase;
    }
    ActivePictureBounds SelectGenericFitEnvelope(const PresentationEnvelopeSelectionInput& input)
    {
        const auto& current=input.currentBounds;
        const auto& base=input.effectiveBase;
        const auto& history=input.history;
        // This is presentation retention, never new crop authority. The caller
        // has already admitted the current generic FIT. Do not refresh the
        // history's per-edge deadlines or hide any newly visible pixels.
        if (!input.envelopeActive || !history.available ||
            !input.frameGeneration || !input.frameSequence ||
            input.currentGeneration!=input.frameGeneration ||
            input.currentSequence!=input.frameSequence ||
            history.sourceGeneration!=input.frameGeneration ||
            !SameBounds(history.base,base) || history.base.trustedBarAxes!=base.trustedBarAxes ||
            !ValidBounds(current,base.rasterWidth,base.rasterHeight) ||
            !ValidBounds(history.bounds,base.rasterWidth,base.rasterHeight) ||
            !CropEdgesAreChromaAligned(current,base.rasterWidth,base.rasterHeight) ||
            !CropEdgesAreChromaAligned(history.bounds,base.rasterWidth,base.rasterHeight) ||
            !ContainedBounds(current,base) || !ContainedBounds(history.bounds,base) ||
            history.bounds.top>current.top || history.bounds.bottom<current.bottom ||
            !SamplingEquivalentAxis(history.bounds.top,history.bounds.bottom,
                current.top,current.bottom,SamplingTolerance(base)))
            return current;
        auto selected=current;
        selected.top=history.bounds.top;
        selected.bottom=history.bounds.bottom;
        selected.aspectRatio=static_cast<double>(selected.right-selected.left)/(selected.bottom-selected.top);
        return selected;
    }
	PresentationEnvelopeDecision EvaluatePresentationEnvelope(
		const PresentationEnvelopeInput& input)
	{
		PresentationEnvelopeDecision decision;
		if (!input.envelopeAvailable)
			return decision;
		if (!input.effectiveGeometryAvailable)
		{
			decision.reason = "trusted final geometry is unavailable";
			return decision;
		}
		if (input.evidenceSourceGeneration == 0 ||
			input.evidenceSourceGeneration != input.frameSourceGeneration)
		{
			decision.reason = "envelope belongs to a stale source generation";
			return decision;
		}
		if (input.detectedSourceSequence != 0 &&
			input.detectedSourceSequence == input.currentSourceSequence)
		{
			decision.active = true;
			decision.currentFrame = true;
			decision.reason =
				"current-frame envelope follows final trusted geometry";
			return decision;
		}
		if (!input.baseMatchesEffectiveGeometry)
		{
			decision.reason = "held envelope base no longer matches geometry";
			return decision;
		}
		if (input.holdMs == 0 || input.lastDetectionTick == 0 ||
			input.currentTick - input.lastDetectionTick > input.holdMs)
		{
			decision.reason = "held envelope expired";
			return decision;
		}
		decision.active = true;
		decision.held = true;
		decision.reason = "matching envelope retained during release hold";
		return decision;
	}

	PresentationEnvelopeGeometryDecision BuildPresentationEnvelope(
		const PresentationEnvelopeGeometryInput& input)
	{
		PresentationEnvelopeGeometryDecision decision;
		const int width = input.trustedPicture.rasterWidth;
		const int height = input.trustedPicture.rasterHeight;
		decision.bounds = FullRaster(width, height);
		if (!ValidBounds(input.trustedPicture, width, height) ||
			!CropEdgesAreChromaAligned(input.trustedPicture, width, height))
		{
			return decision;
		}

		decision.bounds = input.trustedPicture;
		decision.valid = true;
		decision.reason = "trusted picture selected without bounded expansion";
		if (!input.observedContentAvailable)
			return decision;
		if (!ValidBounds(input.observedContent, width, height))
		{
			decision.valid = false;
			decision.bounds = FullRaster(width, height);
			decision.reason = "observed content is invalid or belongs to another raster";
			return decision;
		}
		if (input.horizontalPadding < 0 || input.verticalPadding < 0)
		{
			decision.valid = false;
			decision.bounds = FullRaster(width, height);
			decision.reason = "presentation padding is invalid";
			return decision;
		}

		auto alignDown = [](int value) { return value & ~1; };
		auto alignUp = [](int value) { return (value + 1) & ~1; };
		if (input.expandLeft &&
			input.observedContent.left < input.trustedPicture.left)
		{
			decision.bounds.left = std::min(decision.bounds.left,
				alignDown(std::max(0,
					input.observedContent.left - input.horizontalPadding)));
		}
		if (input.expandTop &&
			input.observedContent.top < input.trustedPicture.top)
		{
			decision.bounds.top = std::min(decision.bounds.top,
				alignDown(std::max(0,
					input.observedContent.top - input.verticalPadding)));
		}
		if (input.expandRight &&
			input.observedContent.right > input.trustedPicture.right)
		{
			decision.bounds.right = std::max(decision.bounds.right,
				std::min(width, alignUp(std::min(width,
					input.observedContent.right + input.horizontalPadding))));
		}
		if (input.expandBottom &&
			input.observedContent.bottom > input.trustedPicture.bottom)
		{
			decision.bounds.bottom = std::max(decision.bounds.bottom,
				std::min(height, alignUp(std::min(height,
					input.observedContent.bottom + input.verticalPadding))));
		}

		decision.bounds.rasterWidth = width;
		decision.bounds.rasterHeight = height;
		decision.bounds.aspectRatio = static_cast<double>(
			decision.bounds.right - decision.bounds.left) /
			std::max(1, decision.bounds.bottom - decision.bounds.top);
		decision.bounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
		decision.expanded = !SameBounds(
			decision.bounds, input.trustedPicture);
		decision.reason = decision.expanded
			? "bounded content expanded trusted picture outward"
			: "bounded content did not extend beyond trusted picture";
		return decision;
	}

	PresentationEnvelopeGeometryDecision BuildComposedPresentationEnvelope(
		const PresentationEnvelopeCompositionInput& input)
	{
		auto complete = [&](const PresentationEnvelopeContent& content,
			int horizontalPadding, int verticalPadding)
		{
			PresentationEnvelopeGeometryInput geometry;
			geometry.trustedPicture = input.trustedPicture;
			geometry.observedContent = content.bounds;
			geometry.observedContentAvailable = content.expandLeft ||
				content.expandTop || content.expandRight || content.expandBottom;
			geometry.expandLeft = content.expandLeft;
			geometry.expandTop = content.expandTop;
			geometry.expandRight = content.expandRight;
			geometry.expandBottom = content.expandBottom;
			geometry.horizontalPadding = horizontalPadding;
			geometry.verticalPadding = verticalPadding;
			return BuildPresentationEnvelope(geometry);
		};

		// Detector bounds already include whatever safety extent their producer
		// supplied. Only raw dense measurements receive subtitle clearance here.
		// Complete selected edges separately so an unrelated detector edge cannot
		// acquire that clearance, or override vertical translation routing.
		auto selectedDetector = input.detectorContent;
        if (input.measuredVerticalFit &&
            (input.denseContent.expandTop || input.denseContent.expandBottom))
        {
            // Off has scanned the complete native bars. Coarse AR history may
            // contain provisional edges (including full-raster placeholders);
            // it cannot enlarge this measured vertical presentation.
            selectedDetector.expandTop = selectedDetector.expandBottom = false;
        }
        const auto detector = complete(selectedDetector, 0, 0);
		if (!detector.valid)
			return detector;
		const auto dense = complete(input.denseContent,
			input.horizontalPadding, input.verticalPadding);
		if (!dense.valid)
			return dense;

		PresentationEnvelopeGeometryInput combined;
		combined.trustedPicture = input.trustedPicture;
		combined.observedContent = detector.bounds;
		combined.observedContent.left = std::min(detector.bounds.left, dense.bounds.left);
		combined.observedContent.top = std::min(detector.bounds.top, dense.bounds.top);
		combined.observedContent.right = std::max(detector.bounds.right, dense.bounds.right);
		combined.observedContent.bottom = std::max(detector.bounds.bottom, dense.bounds.bottom);
		combined.observedContentAvailable = detector.expanded || dense.expanded;
		combined.expandLeft = combined.expandTop = true;
		combined.expandRight = combined.expandBottom = true;
		// The completed envelopes contain their own clearance. Union without a
		// second padding pass; this changes presentation, never aspect authority.
		return BuildPresentationEnvelope(combined);
	}

	CenteredFitDecision FitAspect(double contentAspect,
		const PresentationRect& screen,
		VerticalPictureAlignment verticalAlignment)
	{
		CenteredFitDecision decision;
		const double screenWidth = screen.right - screen.left;
		const double screenHeight = screen.bottom - screen.top;
		if (!std::isfinite(contentAspect) || contentAspect <= 0.0 ||
			!std::isfinite(screenWidth) || !std::isfinite(screenHeight) ||
			screenWidth <= 0.0 || screenHeight <= 0.0)
		{
			return decision;
		}

		const double screenAspect = screenWidth / screenHeight;
		const double centerX = (screen.left + screen.right) * 0.5;
		double pictureWidth = screenWidth;
		double pictureHeight = screenHeight;
		const double epsilon = 1e-9;
		if (contentAspect > screenAspect + epsilon)
		{
			pictureHeight = pictureWidth / contentAspect;
			decision.unusedAxis = UnusedSpaceAxis::VERTICAL;
		}
		else if (contentAspect < screenAspect - epsilon)
		{
			pictureWidth = pictureHeight * contentAspect;
			decision.unusedAxis = UnusedSpaceAxis::HORIZONTAL;
		}
		else
		{
			decision.unusedAxis = UnusedSpaceAxis::NONE;
		}
		double pictureTop = screen.top;
		if (verticalAlignment == VerticalPictureAlignment::CENTER)
			pictureTop += (screenHeight - pictureHeight) * 0.5;
		else if (verticalAlignment == VerticalPictureAlignment::BOTTOM)
			pictureTop = screen.bottom - pictureHeight;
		else if (verticalAlignment != VerticalPictureAlignment::TOP)
			pictureTop += (screenHeight - pictureHeight) * 0.5;
		decision.picture = {
			centerX - pictureWidth * 0.5,
			pictureTop,
			centerX + pictureWidth * 0.5,
			pictureTop + pictureHeight };
		decision.valid = true;
		return decision;
	}

	CenteredFitDecision FitCenteredAspect(
		double contentAspect, const PresentationRect& screen)
	{
		return FitAspect(contentAspect, screen,
			VerticalPictureAlignment::CENTER);
	}

	ProfileTransitionRetentionDecision EvaluateProfileTransitionRetention(
		const ProfileTransitionRetentionInput& input)
	{
		ProfileTransitionRetentionDecision decision;
		const bool validBounds = input.geometry.rasterWidth > 0 &&
			input.geometry.rasterHeight > 0 &&
			input.geometry.left >= 0 && input.geometry.top >= 0 &&
			input.geometry.right > input.geometry.left &&
			input.geometry.bottom > input.geometry.top &&
			input.geometry.right <= input.geometry.rasterWidth &&
			input.geometry.bottom <= input.geometry.rasterHeight;
		decision.retainSourceGeometry = input.geometryAvailable &&
			input.classification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			input.geometry.trustedBarAxes != ActivePictureBounds::BarAxes::NONE &&
			input.geometrySourceGeneration != 0 &&
			input.geometrySourceGeneration == input.analysisSourceGeneration &&
			input.geometrySourceGeneration == input.frameSourceGeneration &&
			input.sourceFormatMatches && validBounds;
		return decision;
	}

	AspectLimitFillDecision EvaluateAspectLimitFill(
		const AspectLimitFillInput& input)
	{
		AspectLimitFillDecision decision;
		decision.sourceBounds = input.sourceBounds;
		const int width = input.sourceBounds.right - input.sourceBounds.left;
		const int height = input.sourceBounds.bottom - input.sourceBounds.top;
		if (width <= 0 || height <= 0)
		{
			decision.reason = "trusted crop bounds are invalid";
			return decision;
		}
		decision.contentAspect = static_cast<double>(width) / height;
		if (!input.trustedContentAuthorityAccepted)
		{
			decision.reason = "trusted active-picture authority is unavailable";
			return decision;
		}
		if (!std::isfinite(input.screenAspect) || input.screenAspect < 1.0 ||
			(input.narrowerLimitConfigured &&
				(!std::isfinite(input.narrowerAspectLimit) ||
				 input.narrowerAspectLimit < 1.0)) ||
			(input.widerLimitConfigured &&
				(!std::isfinite(input.widerAspectLimit) ||
				 input.widerAspectLimit < 1.0)))
		{
			decision.reason = "aspect limit or screen aspect is invalid";
			return decision;
		}
        decision.referenceAspect = decision.contentAspect;
        if (input.contentReferenceAvailable)
        {
            const auto& reference = input.contentReferenceBounds;
            if (!ValidBounds(reference, input.sourceBounds.rasterWidth, input.sourceBounds.rasterHeight) ||
                !ValidBounds(input.sourceBounds, input.sourceBounds.rasterWidth, input.sourceBounds.rasterHeight))
            {
                decision.reason = "fill content reference is invalid";
                return decision;
            }
            decision.referenceAspect = static_cast<double>(reference.right - reference.left) /
                (reference.bottom - reference.top);
            // Overlay/retention expansion is not a new content format. It may
            // restrict existing fill eligibility, never create it or swap sides.
            const bool narrower = decision.contentAspect < input.screenAspect - 1e-6;
            const bool wider = decision.contentAspect > input.screenAspect + 1e-6;
            if ((narrower && (decision.referenceAspect >= input.screenAspect - 1e-6 ||
                    (input.narrowerLimitConfigured && decision.referenceAspect + 1e-6 < input.narrowerAspectLimit))) ||
                (wider && (decision.referenceAspect <= input.screenAspect + 1e-6 ||
                    (input.widerLimitConfigured && decision.referenceAspect - 1e-6 > input.widerAspectLimit))))
            {
                decision.reason = "trusted content reference does not permit this aspect-limit fill";
                return decision;
            }
        }
		const double epsilon = 1e-6;
		if (decision.contentAspect < input.screenAspect - epsilon)
		{
			if (!input.cropNarrowerContentToFillScreen)
			{
				decision.reason = "narrower-content fill is off";
				return decision;
			}
			if (input.narrowerLimitConfigured &&
				decision.contentAspect + epsilon < input.narrowerAspectLimit)
			{
				decision.reason = "content is narrower than the configured aspect limit";
				return decision;
			}
			decision.contentEligible = true;
			// Crop only top/bottom edges, symmetrically and on chroma boundaries,
			// to widen the selected active picture to the screen aspect.
			int filledHeight = static_cast<int>(std::floor(width / input.screenAspect));
			filledHeight &= ~1;
			if (filledHeight <= 0 || filledHeight >= height)
			{
				decision.reason = "centered narrower-content fill would not remove source rows";
				return decision;
			}
			const int removedRows = height - filledHeight;
			const int topInset = (removedRows / 2) & ~1;
			decision.sourceBounds.top += topInset;
			decision.sourceBounds.bottom = decision.sourceBounds.top + filledHeight;
			decision.sourceBounds.aspectRatio = static_cast<double>(width) / filledHeight;
			decision.sourceBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			decision.applied = true;
			decision.reason = "trusted narrower content filled with centered top and bottom crop";
			return decision;
		}
		if (decision.contentAspect > input.screenAspect + epsilon)
		{
			if (!input.cropWiderContentToFillScreen)
			{
				decision.reason = "wider-content fill is off";
				return decision;
			}
			if (input.widerLimitConfigured &&
				decision.contentAspect - epsilon > input.widerAspectLimit)
			{
				decision.reason = "content is wider than the configured aspect limit";
				return decision;
			}
			decision.contentEligible = true;
			// Crop only left/right edges, symmetrically and on chroma boundaries,
			// to narrow the selected active picture to the screen aspect.
			int filledWidth = static_cast<int>(std::floor(height * input.screenAspect));
			filledWidth &= ~1;
			if (filledWidth <= 0 || filledWidth >= width)
			{
				decision.reason = "centered wider-content fill would not remove source columns";
				return decision;
			}
			const int removedColumns = width - filledWidth;
			const int leftInset = (removedColumns / 2) & ~1;
			decision.sourceBounds.left += leftInset;
			decision.sourceBounds.right = decision.sourceBounds.left + filledWidth;
			decision.sourceBounds.aspectRatio = static_cast<double>(filledWidth) / height;
			decision.sourceBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
			decision.applied = true;
			decision.reason = "trusted wider content filled with centered left and right crop";
			return decision;
		}
		decision.contentEligible = true;
		decision.reason = "content already matches the screen aspect";
		return decision;
	}

    bool ShouldApplyDynamicSourceCrop(bool automaticCropRequested,
        bool subtitleFitEnabled, const Input& crop, const AspectLimitFillInput& fill)
    {
        if (automaticCropRequested)
            return true;
        if (!fill.cropNarrowerContentToFillScreen && !fill.cropWiderContentToFillScreen)
            return subtitleFitEnabled;

        auto contentFill = fill;
        contentFill.contentReferenceAvailable = true;
        if (crop.fullRasterPresentationAuthoritative)
        {
            contentFill.trustedContentAuthorityAccepted = true;
            contentFill.sourceBounds = FullRaster(crop.rasterWidth, crop.rasterHeight);
        }
        else
        {
            contentFill.trustedContentAuthorityAccepted = crop.sharedGeometryAvailable &&
                crop.geometrySourceGeneration != 0 &&
                crop.geometrySourceGeneration == crop.frameSourceGeneration &&
                crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
                ValidBounds(crop.geometry, crop.rasterWidth, crop.rasterHeight);
            contentFill.sourceBounds = crop.geometry;
        }
        contentFill.contentReferenceBounds = contentFill.sourceBounds;
        const auto decision = EvaluateAspectLimitFill(contentFill);
        return decision.contentEligible;
    }

	AspectLimitFillDecision EvaluateFixedAspectCrop(
		const FixedAspectCropInput& input)
	{
		AspectLimitFillDecision decision;
		decision.sourceBounds = input.sourceBounds;
		const int width = input.sourceBounds.right - input.sourceBounds.left;
		const int height = input.sourceBounds.bottom - input.sourceBounds.top;
		if (width <= 0 || height <= 0)
		{
			decision.reason = "fixed crop bounds are invalid";
			return decision;
		}
		decision.contentAspect = static_cast<double>(width) / height;
		if (!std::isfinite(input.fixedAspect) || input.fixedAspect < 1.0 ||
			input.fixedAspect > 4.0)
		{
			decision.reason = "fixed crop aspect is invalid";
			return decision;
		}
		const double epsilon = 1e-6;
		if (decision.contentAspect < input.fixedAspect - epsilon)
		{
			int croppedHeight = static_cast<int>(std::floor(width / input.fixedAspect));
			croppedHeight &= ~1;
			if (croppedHeight <= 0 || croppedHeight >= height)
			{
				decision.reason = "centered fixed crop would not remove source rows";
				return decision;
			}
			const int topInset = ((height - croppedHeight) / 2) & ~1;
			decision.sourceBounds.top += topInset;
			decision.sourceBounds.bottom = decision.sourceBounds.top + croppedHeight;
		}
		else if (decision.contentAspect > input.fixedAspect + epsilon)
		{
			int croppedWidth = static_cast<int>(std::floor(height * input.fixedAspect));
			croppedWidth &= ~1;
			if (croppedWidth <= 0 || croppedWidth >= width)
			{
				decision.reason = "centered fixed crop would not remove source columns";
				return decision;
			}
			const int leftInset = ((width - croppedWidth) / 2) & ~1;
			decision.sourceBounds.left += leftInset;
			decision.sourceBounds.right = decision.sourceBounds.left + croppedWidth;
		}
		else
		{
			decision.reason = "content already matches the fixed crop aspect";
			return decision;
		}
		const int finalWidth = decision.sourceBounds.right - decision.sourceBounds.left;
		const int finalHeight = decision.sourceBounds.bottom - decision.sourceBounds.top;
		decision.sourceBounds.aspectRatio = static_cast<double>(finalWidth) / finalHeight;
		decision.sourceBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
		decision.applied = true;
		decision.reason = "current presentation center-cropped to the fixed aspect";
		return decision;
	}

	const char* VerticalPictureAlignmentName(
		VerticalPictureAlignment alignment)
	{
		switch (alignment)
		{
		case VerticalPictureAlignment::TOP: return "top";
		case VerticalPictureAlignment::CENTER: return "center";
		case VerticalPictureAlignment::BOTTOM: return "bottom";
		default: return "invalid";
		}
	}

	const char* UnusedSpaceAxisName(UnusedSpaceAxis axis)
	{
		switch (axis)
		{
		case UnusedSpaceAxis::NONE: return "none";
		case UnusedSpaceAxis::HORIZONTAL: return "horizontal";
		case UnusedSpaceAxis::VERTICAL: return "vertical";
		default: return "invalid";
		}
	}

	void AmbiguityHold::Reset()
	{
		deadlineTick = 0;
		ownerSourceGeneration = 0;
		eligibleAfterTrustedCrop = false;
	}

	void AmbiguityHold::Observe(uint64_t currentTick,
		uint64_t sourceGeneration, bool hadCurrentTrustedCrop,
		bool trustedCropReaffirmed, ActivePictureClassification classification,
		uint64_t maximumHoldMs)
	{
		if (trustedCropReaffirmed && hadCurrentTrustedCrop &&
			classification == ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			deadlineTick = 0;
			ownerSourceGeneration = sourceGeneration;
			eligibleAfterTrustedCrop = true;
			return;
		}
		const bool ambiguous = classification ==
			ActivePictureClassification::PROVISIONAL || classification ==
			ActivePictureClassification::UNAVAILABLE;
		if (hadCurrentTrustedCrop && ambiguous)
		{
			if (ownerSourceGeneration != sourceGeneration)
			{
				Reset();
				return;
			}
			if (deadlineTick == 0 && eligibleAfterTrustedCrop)
			{
				const uint64_t remaining = UINT64_MAX - currentTick;
				deadlineTick = currentTick + std::min(maximumHoldMs, remaining);
				eligibleAfterTrustedCrop = false;
			}
			return;
		}
		if (!ambiguous)
			Reset();
	}

	bool AmbiguityHold::IsActive(uint64_t currentTick,
		uint64_t sourceGeneration) const
	{
		return deadlineTick != 0 && currentTick < deadlineTick &&
			ownerSourceGeneration == sourceGeneration;
	}

	bool IsNearBlackNativeSamplingExpansion(const ActivePictureBounds& entry,
		const ActivePictureBounds& candidate)
	{
		const int width = entry.rasterWidth, height = entry.rasterHeight;
		if (!ValidBounds(entry, width, height) || !ValidBounds(candidate, width, height) ||
			!HasAuthorityForCroppedAxes(entry, width, height) ||
			!HasAuthorityForCroppedAxes(candidate, width, height) ||
			entry.trustedBarAxes != candidate.trustedBarAxes ||
			candidate.rasterWidth != width || candidate.rasterHeight != height ||
			(candidate.left == 0 && candidate.top == 0 &&
			 candidate.right == width && candidate.bottom == height) ||
			candidate.left > entry.left || candidate.top > entry.top ||
			candidate.right < entry.right || candidate.bottom < entry.bottom ||
			SameBounds(entry, candidate))
			return false;
		// Same sampling allowance used by crop reaffirmation. Bound total size
		// as well as each edge; two outward edges cannot double the allowance.
		const int tolerance = SamplingTolerance(entry);
		return entry.left - candidate.left <= tolerance &&
			entry.top - candidate.top <= tolerance &&
			candidate.right - entry.right <= tolerance &&
			candidate.bottom - entry.bottom <= tolerance &&
			(candidate.right - candidate.left) - (entry.right - entry.left) <= tolerance &&
			(candidate.bottom - candidate.top) - (entry.bottom - entry.top) <= tolerance;
	}

    bool NlsCompetesWithEstablishedSubtitleTranslation(bool nlsRequested,
        double maximumCropPercent)
    {
        // Only zero certifies that NLS cannot further crop an admitted
        // viewport translation. Invalid and nonzero settings stay conservative.
        return nlsRequested && maximumCropPercent != 0.0;
    }

    bool CanRetainEstablishedScopeForSubtitle(const NearBlackPresentationEpisodeInput& input,
        const CropPresentationAdmissionState& previous,
        const EstablishedSubtitleTranslationInput& translation)
    {
        const auto& base = input.trustedCrop;
        const auto& owner = translation.presentation;
        // This cannot acquire scope or recover a full-raster episode. A live
        // translation is an existing presentation owner, not new AR evidence.
        const bool selectedColdTranslation =
            input.previous.mode == NearBlackPresentationMode::FULL_RASTER &&
            input.previous.presentedTranslationAvailable &&
            input.previous.boundedPresentationAvailable &&
            input.previous.entryTrustedCropAvailable &&
            input.previous.sourceGeneration == input.sourceGeneration &&
            input.previous.presentationEpoch == input.presentationEpoch &&
            SameTrustedCropContract(input.previous.entryTrustedCrop, base) &&
            input.previous.boundedPresentationSequence != 0 &&
            input.previous.presentedTranslationSourceSequence >=
                input.previous.boundedPresentationSequence &&
            input.sourceSequence >= input.previous.presentedTranslationSourceSequence &&
            input.sourceSequence -
                input.previous.presentedTranslationSourceSequence <= 1;
        if (translation.competingPresentation || !input.measurementCurrent ||
            !input.nearBlackEvaluated || input.sceneBoundary || input.fullRasterAuthorityAvailable ||
            (input.previous.mode == NearBlackPresentationMode::FULL_RASTER &&
             !selectedColdTranslation) ||
            !input.sourceGeneration || !input.sourceSequence || !input.trustedCropAvailable ||
            input.trustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            !previous.available || previous.sourceGeneration != input.sourceGeneration ||
            previous.presentationEpoch != input.presentationEpoch ||
            !SameBounds(previous.trustedCrop, base) ||
            translation.sourceGeneration != input.sourceGeneration || !SameBounds(translation.base, base) ||
            !ValidBounds(base, base.rasterWidth, base.rasterHeight) ||
            base.left != 0 || base.right != base.rasterWidth || base.top <= 0 || base.bottom >= base.rasterHeight ||
            !input.retentionEvaluated || input.retentionSourceGeneration != input.sourceGeneration ||
            input.retentionSourceSequence != input.sourceSequence || !SameBounds(input.retentionBounds, base) ||
            !input.currentObservationAvailable ||
            (input.currentObservationClassification != ActivePictureClassification::PROVISIONAL &&
             input.currentObservationClassification != ActivePictureClassification::BAR_CROP_TRUSTED))
            return false;
        if (input.previous.mode == NearBlackPresentationMode::RETAIN_CROP &&
            (input.previous.sourceGeneration != input.sourceGeneration ||
             input.previous.presentationEpoch != input.presentationEpoch ||
             !input.previous.entryTrustedCropAvailable || !SameBounds(input.previous.entryTrustedCrop, base)))
            return false;
        // A current, pixel-safe sample with no outward content does not retire
        // an already admitted translation. It supplies no new scope authority:
        // the retained contract remains the exact trusted base, while current
        // native bars may refine inward within it. Outward pixels still use
        // the separate coverage proof below.
        const auto currentInwardNativeBars=[&](const ActivePictureBounds& observation) {
            return observation.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM &&
                observation.left==base.left && observation.right==base.right &&
                ContainedBounds(base,observation);
        };
        const bool cleanBaseSample = !input.boundedVisibleContentOutsideCrop &&
            input.retentionSafe && input.retentionExcludedBandsPixelSafe &&
            input.currentObservationOrigin==ActivePictureAuthorityOrigin::NATIVE &&
            input.currentNativeObservationAvailable &&
            currentInwardNativeBars(input.currentObservation) &&
            currentInwardNativeBars(input.currentNativeObservation);
        if (!input.boundedVisibleContentOutsideCrop && !cleanBaseSample)
            return false;
        // A first caption may need the normal bounded confirmation before its
        // shift engages. Only fresh physical-bar reproof may use that existing
        // confirmation owner in darkness; held geometry alone is insufficient.
        const auto& pending = input.subtitleTranslationConfirmation;
        const bool freshPending = owner.action == VerticalBarPresentationAction::NONE &&
            input.subtitleInspectionGeneration == input.sourceGeneration &&
            SameBounds(input.subtitleInspectionBars, base) &&
            pending.lastObservedSourceSequence == input.sourceSequence &&
            pending.confirmations > 0 && pending.confirmations < VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED &&
            std::isfinite(pending.candidateTranslationPixels) &&
            std::abs(pending.candidateTranslationPixels) > 0.5f;
        const bool confirmed = owner.action == VerticalBarPresentationAction::TRANSLATE &&
            owner.sourceSequence != 0 && owner.sourceSequence <= input.sourceSequence &&
            std::isfinite(owner.translationPixels) &&
            CanRetainVerticalBarPresentationAcrossAuthorityGap(owner, translation.sourceGeneration,
                input.sourceGeneration, input.currentTick, translation.holdMs, input.sourceSequence);
        // The released cue's hold has ended, but an already applied translation
        // may still be drifting toward the trusted base. Its last actually
        // admitted owner is only release provenance; clean current pixels
        // cannot create a new caption or extend outward coverage.
        const auto& releaseOwner = translation.releaseOwner;
        bool releaseCertified = false;
        if (cleanBaseSample && owner.action == VerticalBarPresentationAction::NONE &&
            translation.releaseIntervalAvailable &&
            releaseOwner.action == VerticalBarPresentationAction::TRANSLATE &&
            releaseOwner.sourceSequence != 0 &&
            releaseOwner.sourceSequence <= input.sourceSequence &&
            releaseOwner.lastDetectionTick != 0 &&
            translation.releaseSourceGeneration == input.sourceGeneration &&
            translation.releasePresentationEpoch == input.presentationEpoch &&
            SameBounds(translation.releaseOwnerBase, base) &&
            translation.releaseDurationMs != 0 &&
            translation.releaseStartedTick >= releaseOwner.lastDetectionTick &&
            translation.releaseStartedTick - releaseOwner.lastDetectionTick >= translation.holdMs &&
            input.currentTick >= translation.releaseStartedTick &&
            input.currentTick - translation.releaseStartedTick <= translation.releaseDurationMs)
        {
            const double start = translation.releaseStartTranslationPixels;
            const double target = translation.releaseTargetTranslationPixels;
            const double applied = translation.releaseAppliedTranslationPixels;
            if (std::isfinite(start) && std::isfinite(target) && std::isfinite(applied) &&
                std::isfinite(releaseOwner.translationPixels) &&
                std::abs(target) <= 0.5 && std::abs(start) > 0.5 &&
                (start > 0) == (releaseOwner.translationPixels > 0) &&
                std::abs(start) <= std::abs(releaseOwner.translationPixels) + 1.0 &&
                applied >= std::min(0.0, start) && applied <= std::max(0.0, start))
            {
                VerticalBarPresentationResolutionInput poseInput;
                poseInput.detailedAction = VerticalBarPresentationAction::TRANSLATE;
                poseInput.translationPixels = static_cast<float>(applied);
                poseInput.zeroTranslationRetainsTrustedBase = true;
                poseInput.authoritativeTop = base.top;
                poseInput.authoritativeBottom = base.bottom;
                poseInput.rasterHeight = base.rasterHeight;
                const auto routedPose = ResolveVerticalBarRendererRouting(
                    ResolveVerticalBarPresentation(poseInput));
                releaseCertified = !routedPose.failOpen && !routedPose.fitActive &&
                    routedPose.translationPixels == translation.resolvedTranslationPixels;
            }
        }
        if (!confirmed && !freshPending && !releaseCertified) return false;
        if (cleanBaseSample && !confirmed && !releaseCertified) return false;

        // Preserve the normal configured engage, including its initial zero
        // displacement. Its target already passed confirmation; the animation
        // must not become a new full-raster episode. Release drift has no live
        // requested owner and must never use this target-coverage exception.
        const bool engaging = confirmed && translation.engagingTranslation;
        const double resolved = translation.resolvedTranslationPixels;
        if (engaging && translation.engageIntervalAvailable)
        {
            const double start = translation.engageStartTranslationPixels;
            const double target = translation.engageTargetTranslationPixels;
            const double applied = translation.engageAppliedTranslationPixels;
            // A confirmed target may reverse an existing engage/release pose.
            // Certify that exact animation interval; an arbitrary opposite-sign
            // displacement or an overshoot is not retained scope authority.
            if (!std::isfinite(start) || !std::isfinite(target) || !std::isfinite(applied) ||
                std::abs(target - owner.translationPixels) > 0.5 ||
                applied < std::min(start, target) || applied > std::max(start, target) ||
                base.top + start < 0 || base.bottom + start > base.rasterHeight ||
                base.top + target < 0 || base.bottom + target > base.rasterHeight)
                return false;
            // The renderer rounds each fractional pose outward, then aligns
            // it to chroma. Compare the routed pose itself so a legitimate
            // animation sample cannot fail by nearly two source pixels.
            VerticalBarPresentationResolutionInput poseInput;
            poseInput.detailedAction = VerticalBarPresentationAction::TRANSLATE;
            poseInput.translationPixels = static_cast<float>(applied);
            poseInput.zeroTranslationRetainsTrustedBase = true;
            poseInput.authoritativeTop = base.top;
            poseInput.authoritativeBottom = base.bottom;
            poseInput.rasterHeight = base.rasterHeight;
            const auto routedPose = ResolveVerticalBarRendererRouting(
                ResolveVerticalBarPresentation(poseInput));
            if (routedPose.failOpen || routedPose.fitActive || routedPose.translationPixels != resolved)
                return false;
        }
        else if (engaging && (std::abs(resolved) > std::abs(owner.translationPixels) + 1.0 ||
            (resolved != 0.0 && (resolved > 0) != (owner.translationPixels > 0))))
            return false;
        const auto& shiftOwner = releaseCertified ? releaseOwner : owner;
        const double shift = releaseCertified ? resolved :
            (freshPending ? pending.candidateTranslationPixels :
             (engaging ? owner.translationPixels : resolved));
        if (std::abs(shift) <= 0.5 ||
            (!freshPending && ((shift > 0) != (shiftOwner.translationPixels > 0) ||
             std::abs(shift) > std::abs(shiftOwner.translationPixels) + 1.0)) ||
            base.top + shift < 0 || base.bottom + shift > base.rasterHeight)
            return false;
        // Do not reclassify an opposite-edge, side, or deeper picture intrusion
        // as a caption. Its complete bounded envelope must fit the live shift.
        // During confirmation this only removes the episode veto; the ordinary
        // two-sample confirmation policy still controls the actual presentation.
        auto covered = base;
        if (shift > 0) covered.bottom += static_cast<int>(shift);
        else covered.top += static_cast<int>(shift);
        if (cleanBaseSample)
            return true;
        return ValidBounds(translation.visibleBounds, base.rasterWidth, base.rasterHeight) &&
            ValidBounds(input.currentObservation, base.rasterWidth, base.rasterHeight) &&
            ContainedBounds(covered, translation.visibleBounds) &&
            ContainedBounds(covered, input.currentObservation);
    }
    bool CanRetainWeakFringeWithFill(const NearBlackPresentationEpisodeInput& input,
        const CropPresentationAdmissionState& previous, const AspectLimitFillInput& fill,
        bool weakBoundedFringe, bool competingPresentation)
    {
        // This preserves an already displayed optional fill, never crop acquisition.
        // The caller supplies current source-row evidence; no percentage-wide
        // exemption is permitted for actual picture outside the native contract.
        const auto& base = input.trustedCrop;
        if (!weakBoundedFringe || competingPresentation || !input.measurementCurrent ||
            !input.nearBlackEvaluated || input.globalNearBlack || input.sceneBoundary ||
            input.fullRasterAuthorityAvailable || !input.boundedVisibleContentOutsideCrop ||
            !input.trustedCropAvailable || input.trustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            input.previous.mode != NearBlackPresentationMode::RETAIN_CROP ||
            !input.previous.entryTrustedCropAvailable ||
            input.previous.entryTrustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            input.previous.sourceGeneration != input.sourceGeneration ||
            input.previous.presentationEpoch != input.presentationEpoch ||
            !SameTrustedCropContract(base, input.previous.entryTrustedCrop) ||
            !input.sourceGeneration || !input.sourceSequence ||
            !input.retentionEvaluated || input.retentionSourceGeneration != input.sourceGeneration ||
            input.retentionSourceSequence != input.sourceSequence || !SameBounds(base, input.retentionBounds) ||
            !input.currentObservationAvailable ||
            (input.currentObservationClassification != ActivePictureClassification::BAR_CROP_TRUSTED &&
             input.currentObservationClassification != ActivePictureClassification::PROVISIONAL) ||
            !previous.available || !previous.optionalFillApplied ||
            previous.sourceGeneration != input.sourceGeneration || previous.presentationEpoch != input.presentationEpoch ||
            !SameTrustedCropContract(previous.trustedCrop, base) ||
            !std::isfinite(fill.screenAspect) || fill.screenAspect <= 0.0 ||
            previous.optionalFillScreenAspect != fill.screenAspect ||
            !fill.trustedContentAuthorityAccepted || !fill.contentReferenceAvailable ||
            !SameBounds(fill.sourceBounds, base) || !SameBounds(fill.contentReferenceBounds, base))
            return false;
        const bool certifiedRepeat = previous.optionalFillSourceSequence == input.sourceSequence &&
            input.previous.weakFringeFillSourceSequence == input.sourceSequence;
        if (!certifiedRepeat && (input.cadenceRepeat || previous.optionalFillSourceSequence == 0 ||
            previous.optionalFillSourceSequence != input.sourceSequence - 1))
            return false;
        if (!ValidBounds(base, base.rasterWidth, base.rasterHeight)) return false;
        auto envelope = base;
        envelope.top = std::max(0, base.top - 2);
        envelope.bottom = std::min(base.rasterHeight, base.bottom + 2);
        if (!ContainedBounds(envelope, input.currentObservation)) return false;
        const auto evaluated = EvaluateAspectLimitFill(fill);
        // Only established wider-content fill: it already intentionally removes
        // side pixels. We tolerate at most two weak source rows at one bar edge.
        return evaluated.applied && evaluated.sourceBounds.top == base.top &&
            evaluated.sourceBounds.bottom == base.bottom &&
            (evaluated.sourceBounds.left > base.left || evaluated.sourceBounds.right < base.right) &&
            SameBounds(evaluated.sourceBounds, previous.optionalFillSourceBounds);
    }
	bool QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(
		const NearBlackPresentationEpisodeInput& input)
	{
		const auto& episode = input.previous;
		const auto& proof = input.currentNativeOutwardProof;
		const auto& entry = episode.entryTrustedCrop;
		const auto& candidate = input.currentObservation;
		if ((episode.mode != NearBlackPresentationMode::FULL_RASTER &&
            episode.mode != NearBlackPresentationMode::RETAIN_CROP) ||
			!episode.entryTrustedCropAvailable ||
			episode.entryTrustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			episode.sourceGeneration != input.sourceGeneration ||
			episode.presentationEpoch != input.presentationEpoch ||
			input.sceneBoundary || input.cadenceRepeat ||
			!input.measurementCurrent || !input.nearBlackEvaluated ||
			input.globalNearBlack || input.fullRasterAuthorityAvailable ||
			!input.trustedCropAvailable ||
			input.trustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			!input.currentObservationAvailable ||
			input.currentObservationClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			input.currentObservationOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			!input.currentNativeObservationAvailable ||
			!SameTrustedCropContract(input.currentNativeObservation, candidate) ||
			!input.boundedVisibleContentOutsideCrop ||
			!input.retentionEvaluated || input.retentionSafe ||
			input.retentionSourceGeneration != input.sourceGeneration ||
			input.retentionSourceSequence != input.sourceSequence ||
			!SameTrustedCropContract(input.trustedCrop, entry) ||
			!SameTrustedCropContract(input.retentionBounds, entry) ||
			!ValidBounds(candidate, entry.rasterWidth, entry.rasterHeight) ||
			candidate.trustedBarAxes != entry.trustedBarAxes ||
			!ContainedBounds(candidate, entry) || SameBounds(candidate, entry))
			return false;

		// Bind the producer sample and both exact contracts to this frame.
		// The episode policy independently counts three consecutive bright samples.
		return input.sourceGeneration != 0 && input.sourceSequence != 0 &&
			input.currentNativeOutwardProofPresentationEpoch == input.presentationEpoch &&
			proof.sourceGeneration == input.sourceGeneration &&
			proof.lastObservedSourceSequence == input.sourceSequence &&
			proof.firstPictureSourceSequence != 0 &&
			proof.firstPictureSourceSequence <= input.sourceSequence &&
			proof.confirmations != 0 &&
			proof.proofRoute != OutwardPictureProofRoute::NONE &&
			SameTrustedCropContract(proof.base, entry) &&
			SameTrustedCropContract(proof.candidate, candidate) &&
			SamePictureWithinDetectorStep(proof.referenceCandidate, candidate);
	}
    NearBlackEpisodeDarkness SelectNearBlackEpisodeDarkness(
        bool rawEvaluated, bool rawNearBlack, bool arEffectiveNearBlack)
    {
        (void)arEffectiveNearBlack;
        NearBlackEpisodeDarkness signal;
        signal.evaluated = rawEvaluated;
        signal.nearBlack = rawEvaluated && rawNearBlack;
        return signal;
    }

	NearBlackPresentationEpisodeDecision EvaluateNearBlackPresentationEpisode(
		const NearBlackPresentationEpisodeInput& input)
	{
		NearBlackPresentationEpisodeDecision decision;
		decision.state = input.previous;
		bool presentationEpochChanged = false;
		decision.revalidationSamplesRequired =
			NearBlackCropRevalidationSamples(input.framesPerSecond);
		decision.bootstrapSamplesRequired =
			NearBlackBootstrapSamples(input.framesPerSecond);

		if (decision.state.mode != NearBlackPresentationMode::INACTIVE &&
			decision.state.sourceGeneration != input.sourceGeneration)
		{
			decision.state = {};
			decision.ended = true;
			decision.reason = "source generation ended near-black title episode";
		}

		if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			decision.state.sourceGeneration == input.sourceGeneration &&
			decision.state.presentationEpoch != input.presentationEpoch)
		{
			// A live profile/viewport boundary invalidates every certificate from
			// the old presentation epoch, but it must not strand the episode there.
			// Preserve the conservative presentation choice (and any confirmed
			// outward-content latch), then restart only the partial proof on the
			// current epoch. Fresh current-frame pixels are still required below.
			decision.state.entryTrustedCropAvailable = false;
			decision.state.boundedPresentationAvailable = false;
			decision.state.boundedPresentationFailed = false;
			decision.state.boundedPresentation = {};
			decision.state.boundedPresentationSequence = 0;
			decision.state.entryTrustedCrop = {};
			decision.state.lastEvaluatedSourceSequence = 0;
			decision.state.presentationEpoch = input.presentationEpoch;
			decision.state.fullRasterStartedSourceSequence = input.sourceSequence;
			ResetNearBlackCropRevalidation(decision.state);
			ResetNearBlackBootstrap(decision.state);
			decision.state.outwardConfirmationLastSourceSequence = 0;
			decision.state.outwardConfirmationSamples = 0;
			presentationEpochChanged = true;
			decision.reason =
				"presentation epoch changed; near-black proof restarted";
		}

        if (decision.state.mode == NearBlackPresentationMode::RETAIN_CROP &&
            decision.state.presentationEpoch != input.presentationEpoch)
        {
            decision.state = {};
            decision.ended = true;
            decision.reason = "presentation epoch ended retained near-black episode";
        }
		if (input.sceneBoundary &&
			decision.state.mode != NearBlackPresentationMode::INACTIVE)
		{
			decision.state = {};
			decision.ended = true;
			decision.reason = "scene boundary ended near-black title episode";
		}

		// A known full-frame picture has no excluded bands to become unsafe.
		// Preserve that committed picture through darkness; do not conflate it
		// with startup's unclassified full-raster fallback. The retention lease
		// is revoked independently for resets, cuts, and fresh trusted bars.
		if (input.knownFullRasterRetained && input.fullRasterAuthorityAvailable &&
			!input.trustedCropAvailable)
		{
			decision.ended = decision.ended || decision.state.mode != NearBlackPresentationMode::INACTIVE;
			decision.state = {};
			decision.reason = "retained committed full-raster picture through near-black ambiguity";
			return decision;
		}

		if (input.fullRasterAuthorityAvailable &&
			decision.state.mode != NearBlackPresentationMode::INACTIVE &&
			input.nearBlackEvaluated && !input.globalNearBlack)
		{
			decision.state = {};
			decision.ended = true;
			decision.reason =
				"non-near-black full-raster authority ended title episode";
		}

		// FULL_RASTER can be a temporary fail-open choice made because one frame
		// lost Scope confidence. If that same native Scope was actually submitted,
		// and no current full-raster authority contradicts it, restore the existing
		// presentation contract immediately. This does not publish new geometry.
		const bool currentFullRasterObservation = input.currentObservationAvailable &&
			input.currentObservationClassification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED;
		const bool submittedEntryScopeMatches =
			decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			decision.state.entryTrustedCropAvailable &&
			decision.state.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
			input.previouslyAdmittedCropAvailable &&
			input.previouslyAdmittedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
			input.previouslyAdmittedCropSourceGeneration == input.sourceGeneration &&
			input.previouslyAdmittedCropPresentationEpoch == input.presentationEpoch &&
			SameTrustedCropContract(input.previouslyAdmittedCrop,
				decision.state.entryTrustedCrop);
		if (submittedEntryScopeMatches &&
			decision.state.sourceGeneration == input.sourceGeneration &&
			decision.state.presentationEpoch == input.presentationEpoch &&
			!decision.state.confirmedNonNearBlackContent &&
			!input.fullRasterAuthorityAvailable && !currentFullRasterObservation)
		{
			decision.state.mode = NearBlackPresentationMode::RETAIN_CROP;
			decision.state.startedAtFullRaster = false;
			decision.state.startedSourceSequence = input.sourceSequence;
			decision.state.fullRasterStartedSourceSequence = 0;
			decision.state.boundedPresentationAvailable = false;
			decision.state.boundedPresentationFailed = false;
			decision.state.boundedPresentation = {};
			decision.state.boundedPresentationSequence = 0;
			decision.state.boundedPresentationPresentedSequence = 0;
			decision.state.entryTrustedCrop = input.previouslyAdmittedCrop;
			decision.state.entryTrustedCropAvailable = true;
			decision.state.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
			ResetNearBlackCropRevalidation(decision.state);
			decision.restoredSubmittedScope = true;
			decision.reason =
				"unconfirmed full-raster fallback restored the last submitted native Scope";
		}

        // Confirmation proves a possible replacement owner, not that its pose
        // has been displayed. Retiring an already submitted outward envelope
        // here contracts the picture to native scope for one frame; the next
        // dark sample expands it again. Keep the existing envelope through
        // pending confirmation, including samples just above the dark threshold.
        // Unsubmitted proposals and native-only holds retain their old behavior.
        const auto& pendingSubtitle = input.subtitleTranslationConfirmation;
        const auto& shownEnvelope = decision.state;
        const bool pendingSubmittedEnvelope =
            shownEnvelope.mode == NearBlackPresentationMode::RETAIN_CROP &&
            shownEnvelope.sourceGeneration == input.sourceGeneration &&
            shownEnvelope.presentationEpoch == input.presentationEpoch &&
            shownEnvelope.entryTrustedCropAvailable &&
            SameTrustedCropContract(shownEnvelope.entryTrustedCrop, input.trustedCrop) &&
            shownEnvelope.boundedPresentationAvailable &&
            shownEnvelope.boundedPresentationSequence != 0 &&
            shownEnvelope.boundedPresentationPresentedSequence == shownEnvelope.boundedPresentationSequence &&
            shownEnvelope.boundedPresentationSequence <= input.sourceSequence &&
            ValidBounds(shownEnvelope.boundedPresentation,
                input.trustedCrop.rasterWidth, input.trustedCrop.rasterHeight) &&
            ContainedBounds(shownEnvelope.boundedPresentation, input.trustedCrop) &&
            !SameBounds(shownEnvelope.boundedPresentation, input.trustedCrop) &&
            input.subtitlePresentation.action == VerticalBarPresentationAction::NONE &&
            pendingSubtitle.lastObservedSourceSequence == input.sourceSequence &&
            pendingSubtitle.confirmations > 0 &&
            pendingSubtitle.confirmations < VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED;
        if (input.establishedScopeSubtitleRetained && !pendingSubmittedEnvelope &&
            (!input.globalNearBlack || decision.state.mode == NearBlackPresentationMode::INACTIVE))
        {
            decision.ended = decision.state.mode != NearBlackPresentationMode::INACTIVE;
            decision.state = {};
            decision.reason = "established scope uses bounded subtitle presentation through darkness";
            return decision;
        }
        // A subtitle may have started the conservative episode. Its pixels
        // remain visible via the ordinary confirmed translation. This releases
        // only the episode veto; all normal crop/admission gates still run.
        const auto& subtitleBars = input.subtitleInspectionBars;
        const int subtitleTolerance = std::max(2, input.trustedCrop.rasterHeight / 270);
        const bool currentPhysicalBars = input.measurementCurrent &&
            input.nearBlackEvaluated && !input.globalNearBlack && !input.sceneBoundary &&
            !input.fullRasterAuthorityAvailable && input.trustedCropAvailable &&
            input.trustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            input.sourceGeneration != 0 && input.sourceSequence != 0 &&
            input.subtitleInspectionGeneration == input.sourceGeneration &&
            input.currentObservationAvailable &&
            (input.currentObservationClassification == ActivePictureClassification::PROVISIONAL ||
             input.currentObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED) &&
            ValidBounds(subtitleBars, input.trustedCrop.rasterWidth, input.trustedCrop.rasterHeight) &&
            subtitleBars.left == 0 && subtitleBars.right == input.trustedCrop.rasterWidth &&
            subtitleBars.top > 0 && subtitleBars.bottom < input.trustedCrop.rasterHeight &&
            std::abs(subtitleBars.top - input.trustedCrop.top) <= subtitleTolerance &&
            std::abs(subtitleBars.bottom - input.trustedCrop.bottom) <= subtitleTolerance;
        const bool currentSubtitleTranslation = currentPhysicalBars &&
            input.subtitlePresentation.action == VerticalBarPresentationAction::TRANSLATE &&
            std::isfinite(input.subtitlePresentation.translationPixels) &&
            std::abs(input.subtitlePresentation.translationPixels) > 0.5f &&
            input.subtitlePresentation.sourceSequence == input.sourceSequence;
        // The ordinary crop policy already retains an admitted scope contract
        // while dense translation confirms. Do not let the near-black episode
        // preempt that owner on its first two samples. Fresh physical bars and
        // the exact retained contract are required; established full-raster
        // presentation still needs confirmed recovery above.
        const auto& confirmation = input.subtitleTranslationConfirmation;
        const bool currentScopeConfirmation = currentPhysicalBars &&
            decision.state.mode == NearBlackPresentationMode::RETAIN_CROP &&
            decision.state.sourceGeneration == input.sourceGeneration &&
            decision.state.presentationEpoch == input.presentationEpoch &&
            decision.state.entryTrustedCropAvailable &&
            SameBounds(decision.state.entryTrustedCrop, input.trustedCrop) &&
            SameBounds(subtitleBars, input.trustedCrop) &&
            confirmation.lastObservedSourceSequence == input.sourceSequence &&
            confirmation.confirmations > 0 &&
            confirmation.confirmations < VERTICAL_TRANSLATION_CONFIRMATIONS_REQUIRED &&
            std::isfinite(confirmation.candidateTranslationPixels) &&
            std::abs(confirmation.candidateTranslationPixels) > 0.5f &&
            input.subtitlePresentation.action != VerticalBarPresentationAction::FIT;
        if (currentSubtitleTranslation || (currentScopeConfirmation && !pendingSubmittedEnvelope))
        {
            decision.ended = decision.state.mode != NearBlackPresentationMode::INACTIVE;
            decision.state = {};
            decision.reason = currentScopeConfirmation
                ? "fresh physical bars preserved established scope during bounded confirmation"
                : "fresh physical bars and confirmed subtitle translation resumed shared presentation";
            return decision;
        }

        if (pendingSubmittedEnvelope)
            decision.reason = "submitted overlay envelope retained while subtitle target confirms";

		if (input.measurementCurrent && input.nearBlackEvaluated &&
			input.globalNearBlack &&
			decision.state.mode == NearBlackPresentationMode::INACTIVE)
		{
			const bool exactAdmittedEntryCrop =
				input.previouslyAdmittedCropAvailable &&
				input.previouslyAdmittedCropSourceGeneration == input.sourceGeneration &&
				input.previouslyAdmittedCropPresentationEpoch == input.presentationEpoch &&
				input.previouslyAdmittedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
				ValidBounds(input.previouslyAdmittedCrop,
					input.previouslyAdmittedCrop.rasterWidth,
					input.previouslyAdmittedCrop.rasterHeight) &&
				(!input.trustedCropAvailable ||
				 SameTrustedCropContract(input.previouslyAdmittedCrop, input.trustedCrop));
			const bool entryCropAvailable = input.trustedCropAvailable || exactAdmittedEntryCrop;
			const auto& entryCrop = input.trustedCropAvailable
				? input.trustedCrop : input.previouslyAdmittedCrop;
			decision.state.mode = entryCropAvailable &&
				(!input.boundedVisibleContentOutsideCrop || exactAdmittedEntryCrop)
				? NearBlackPresentationMode::RETAIN_CROP
				: NearBlackPresentationMode::FULL_RASTER;
			decision.state.sourceGeneration = input.sourceGeneration;
			decision.state.startedSourceSequence = input.sourceSequence;
			decision.state.presentationEpoch = input.presentationEpoch;
			decision.state.startedAtFullRaster =
				decision.state.mode == NearBlackPresentationMode::FULL_RASTER;
			// A successfully submitted native Scope can anchor the episode even if
			// the current detector sample temporarily has no crop candidate.
			decision.state.entryTrustedCropAvailable = entryCropAvailable;
			if (decision.state.entryTrustedCropAvailable)
			{
				decision.state.entryTrustedCrop = entryCrop;
				decision.state.entryTrustedCropOrigin = input.trustedCropAvailable
					? input.trustedCropOrigin : ActivePictureAuthorityOrigin::NATIVE;
			}
			if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER)
				decision.state.fullRasterStartedSourceSequence =
					input.sourceSequence;
			decision.started = true;
			decision.reason = decision.state.mode ==
				NearBlackPresentationMode::RETAIN_CROP
				? "near-black title episode retained current crop"
				: "near-black title episode started at full raster";
		}

        const bool oldCropCurrentPixelsSafe =
            decision.state.mode == NearBlackPresentationMode::RETAIN_CROP &&
            decision.state.entryTrustedCropAvailable &&
            input.measurementCurrent && input.retentionEvaluated &&
            input.retentionSourceGeneration == input.sourceGeneration &&
            input.retentionSourceSequence == input.sourceSequence &&
            SameTrustedCropContract(input.retentionBounds,
                decision.state.entryTrustedCrop) &&
            input.retentionSafe && input.retentionExcludedBandsPixelSafe &&
            input.currentObservationAvailable &&
            ValidBounds(input.currentObservation,
                decision.state.entryTrustedCrop.rasterWidth,
                decision.state.entryTrustedCrop.rasterHeight) &&
            ContainedBounds(decision.state.entryTrustedCrop,
                input.currentObservation) &&
            (!input.currentVisibleBoundsAvailable ||
             (input.currentVisibleSourceGeneration == input.sourceGeneration &&
              input.currentVisibleSourceSequence == input.sourceSequence &&
              ValidBounds(input.currentVisibleBounds,
                  decision.state.entryTrustedCrop.rasterWidth,
                  decision.state.entryTrustedCrop.rasterHeight) &&
              ContainedBounds(decision.state.entryTrustedCrop,
                  input.currentVisibleBounds)));
        const bool exactAdmittedEntryStillCurrent =
            decision.state.entryTrustedCropAvailable &&
            decision.state.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            input.previouslyAdmittedCropAvailable &&
            input.previouslyAdmittedCropSourceGeneration == input.sourceGeneration &&
            input.previouslyAdmittedCropPresentationEpoch == input.presentationEpoch &&
            input.previouslyAdmittedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            SameTrustedCropContract(input.previouslyAdmittedCrop,
                decision.state.entryTrustedCrop);
        if (decision.state.mode == NearBlackPresentationMode::RETAIN_CROP &&
            input.measurementCurrent && input.boundedVisibleContentOutsideCrop &&
            !input.weakFringeFillRetained && !oldCropCurrentPixelsSafe &&
            !exactAdmittedEntryStillCurrent)
		{
			decision.state.mode = NearBlackPresentationMode::FULL_RASTER;
			decision.state.fullRasterStartedSourceSequence =
				input.sourceSequence;
			decision.changedToFullRaster = true;
			decision.reason =
				"bounded visible title content latched full raster for episode";
		}
        else if (decision.state.mode ==
            NearBlackPresentationMode::RETAIN_CROP &&
            !input.trustedCropAvailable && !oldCropCurrentPixelsSafe &&
            !exactAdmittedEntryStillCurrent)
		{
			decision.state.mode = NearBlackPresentationMode::FULL_RASTER;
			decision.state.fullRasterStartedSourceSequence =
				input.sourceSequence;
			decision.changedToFullRaster = true;
			decision.reason =
				"lost retained title geometry latched full raster for episode";
		}

        if (decision.state.mode == NearBlackPresentationMode::RETAIN_CROP && input.weakFringeFillRetained)
        {
            decision.state.weakFringeFillSourceSequence = input.sourceSequence;
            decision.reason = "two weak source rows retained established filled crop";
        }
		// The broad AR proof can include title-dark samples. Count an independent
		// run of current, non-dark, native pixels before retiring the old entry.
		// A cadence repeat keeps a certified source sample but never adds a vote.
		auto distinctInput = input;
		distinctInput.cadenceRepeat = false;
		const bool currentSample =
			QualifiesObsoleteNearBlackEntryNativeOutwardPictureSample(distinctInput);
		const bool sameProofAnchor = currentSample &&
			SameTrustedCropContract(decision.state.nativeOutwardReleaseCandidate,
				input.currentNativeOutwardProof.referenceCandidate);
		const bool certifiedRepeat = input.cadenceRepeat &&
			decision.state.nativeOutwardReleaseSamples != 0 &&
			decision.state.nativeOutwardReleaseLastSourceSequence == input.sourceSequence &&
			sameProofAnchor;
		if (certifiedRepeat)
		{
			// Preserve the last distinct source sample unchanged.
		}
		else if (!input.cadenceRepeat && currentSample)
		{
			const bool continues = decision.state.nativeOutwardReleaseSamples != 0 &&
				decision.state.nativeOutwardReleaseLastSourceSequence + 1 == input.sourceSequence &&
				sameProofAnchor;
			decision.state.nativeOutwardReleaseSamples = continues
				? std::min(OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED,
					decision.state.nativeOutwardReleaseSamples + 1) : 1;
			decision.state.nativeOutwardReleaseLastSourceSequence = input.sourceSequence;
			decision.state.nativeOutwardReleaseCandidate =
				input.currentNativeOutwardProof.referenceCandidate;
			if (decision.state.nativeOutwardReleaseSamples >=
					OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED &&
				input.currentNativeOutwardProof.confirmations >=
					OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED)
			{
				decision.state = {};
				decision.bootstrapReleased = true;
				decision.outwardEntryReleased = true;
				decision.resetTransitionEvidence = true;
				decision.resetTrustedGeometry = true;
				decision.ended = true;
				decision.reason = "three fresh non-dark native outward frames retired obsolete title entry";
				return decision;
			}
		}
		else
		{
			decision.state.nativeOutwardReleaseCandidate = {};
			decision.state.nativeOutwardReleaseLastSourceSequence = 0;
			decision.state.nativeOutwardReleaseSamples = 0;
		}
		if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			decision.state.entryTrustedCropAvailable &&
			input.measurementCurrent && !input.cadenceRepeat &&
			input.sourceSequence != 0 && input.nearBlackEvaluated)
		{
			const bool exactCurrentEntryRetention = input.retentionEvaluated &&
				input.retentionSourceGeneration == input.sourceGeneration &&
				input.retentionSourceSequence == input.sourceSequence &&
				SameBounds(input.retentionBounds,
					decision.state.entryTrustedCrop);
			if (!input.globalNearBlack &&
				input.boundedVisibleContentOutsideCrop &&
				exactCurrentEntryRetention)
			{
				if (decision.state.outwardConfirmationLastSourceSequence != 0 &&
					input.sourceSequence ==
						decision.state.outwardConfirmationLastSourceSequence + 1)
				{
					++decision.state.outwardConfirmationSamples;
				}
				else
				{
					decision.state.outwardConfirmationSamples = 1;
				}
				decision.state.outwardConfirmationLastSourceSequence =
					input.sourceSequence;
				if (decision.state.outwardConfirmationSamples >=
					decision.bootstrapSamplesRequired)
				{
					decision.state.confirmedNonNearBlackContent = true;
					decision.reason =
						"sustained non-near-black outward content kept full raster";
				}
			}
			else
			{
				decision.state.outwardConfirmationLastSourceSequence = 0;
				decision.state.outwardConfirmationSamples = 0;
			}
		}

		// A retained-crop episode must also end after fresh bright frames prove
		// its exact entry crop safe. Otherwise a long-finished fade keeps owning
		// presentation and a later bounded menu/subtitle intrusion takes the title
		// path straight to full raster instead of normal presentation arbitration.
		// Use the same current-context/pixel proof as full-raster recovery; no
		// timeout alone, weaker black threshold, or new crop authority is added.
		const bool distinctEntrySample = !input.cadenceRepeat && input.sourceSequence != 0 &&
			input.sourceSequence > decision.state.lastEvaluatedSourceSequence;
		if (decision.state.mode != NearBlackPresentationMode::INACTIVE &&
			decision.state.entryTrustedCropAvailable && !distinctEntrySample)
			decision.revalidationGates |= RECOVERY_REPEAT;
		if (decision.state.mode != NearBlackPresentationMode::INACTIVE &&
			decision.state.entryTrustedCropAvailable && distinctEntrySample)
		{
			decision.state.lastEvaluatedSourceSequence = input.sourceSequence;
			// A sparse startup entry cannot obtain the native association below:
			// episode suppression intentionally blocks new native publications.
			// Independent current native bars may corroborate only that existing
			// rectangle, without upgrading its origin or learning a format.
			const bool sparseEntryNativeReaffirmed =
				decision.state.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT &&
				input.trustedCropAvailable &&
				input.trustedCropOrigin == ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT &&
				SameTrustedCropContract(input.trustedCrop, decision.state.entryTrustedCrop) &&
				input.currentNativeObservationReaffirmsSparseEntry &&
				input.retentionExcludedBandsPixelSafe;
			const bool exactEntryContract = sparseEntryNativeReaffirmed || (
				input.knownTrustedGeometryReacquired &&
				input.reacquiredTrustedClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				input.reacquisitionIsCurrentAssociation &&
				input.reacquiredSourceGeneration == input.sourceGeneration &&
				input.reacquiredSourceSequence >=
					(decision.state.mode == NearBlackPresentationMode::FULL_RASTER
						? decision.state.fullRasterStartedSourceSequence
						: decision.state.startedSourceSequence) &&
				input.reacquiredSourceSequence <= input.sourceSequence &&
				input.reacquiredPresentationEpoch ==
					decision.state.presentationEpoch &&
				SameTrustedCropContract(input.reacquiredTrustedGeometry,
					decision.state.entryTrustedCrop));
			const bool samplingReaffirmed = input.currentObservationAvailable &&
				input.currentObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
				IsPixelSafeCropReaffirmation(decision.state.entryTrustedCrop,
					input.currentObservation, input.retentionExcludedBandsPixelSafe);
			const bool exactCurrentObservation = input.currentObservationAvailable &&
				(ContainedBounds(decision.state.entryTrustedCrop, input.currentObservation) || samplingReaffirmed);
			const bool currentPixelsSafe = input.retentionSafe || samplingReaffirmed;
			const bool exactCurrentSafety = input.retentionEvaluated &&
				currentPixelsSafe &&
				input.retentionSourceGeneration == input.sourceGeneration &&
				input.retentionSourceSequence == input.sourceSequence &&
				SameBounds(input.retentionBounds,
					decision.state.entryTrustedCrop);
			if (!input.measurementCurrent || !input.retentionEvaluated ||
				input.retentionSourceGeneration != input.sourceGeneration ||
				input.retentionSourceSequence != input.sourceSequence)
				decision.revalidationGates |= RECOVERY_MEASUREMENT;
			if (!exactEntryContract) decision.revalidationGates |= RECOVERY_CONTRACT;
			if (!exactCurrentObservation) decision.revalidationGates |= RECOVERY_OBSERVATION;
			if (input.boundedVisibleContentOutsideCrop ||
				(input.measurementCurrent && input.retentionEvaluated &&
				 input.retentionSourceGeneration == input.sourceGeneration &&
				 input.retentionSourceSequence == input.sourceSequence &&
				 SameBounds(input.retentionBounds, decision.state.entryTrustedCrop) &&
				 exactCurrentObservation && !currentPixelsSafe))
				decision.revalidationGates |= RECOVERY_UNSAFE_BANDS;
            // Exact, frame-current native bars and clean excluded-band pixels
            // may revalidate the saved physical Scope even while the picture
            // itself remains dark. This never creates a new crop authority.
            const bool currentNativeCleanScope = input.nearBlackEvaluated &&
                input.globalNearBlack && input.measurementCurrent &&
                input.currentObservationOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                input.currentNativeObservationAvailable &&
                input.currentNativeProposedAvailable &&
                input.currentObservationAvailable &&
                (input.currentObservationClassification == ActivePictureClassification::PROVISIONAL ||
                 input.currentObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED) &&
                ValidBounds(input.currentObservation,
                    decision.state.entryTrustedCrop.rasterWidth,
                    decision.state.entryTrustedCrop.rasterHeight) &&
                ValidBounds(input.currentNativeProposedBounds,
                    decision.state.entryTrustedCrop.rasterWidth,
                    decision.state.entryTrustedCrop.rasterHeight) &&
                input.currentObservation.left == 0 &&
                input.currentObservation.right == decision.state.entryTrustedCrop.rasterWidth &&
                input.currentNativeProposedBounds.left == 0 &&
                input.currentNativeProposedBounds.right ==
                    decision.state.entryTrustedCrop.rasterWidth &&
                std::abs(input.currentObservation.top -
                    input.currentNativeProposedBounds.top) <=
                    std::max(2, decision.state.entryTrustedCrop.rasterHeight / 540) &&
                std::abs(input.currentObservation.bottom -
                    input.currentNativeProposedBounds.bottom) <=
                    std::max(2, decision.state.entryTrustedCrop.rasterHeight / 540) &&
                std::abs(decision.state.entryTrustedCrop.top -
                    input.currentNativeProposedBounds.top) <=
                    std::max(2, decision.state.entryTrustedCrop.rasterHeight / 540) &&
                std::abs(decision.state.entryTrustedCrop.bottom -
                    input.currentNativeProposedBounds.bottom) <=
                    std::max(2, decision.state.entryTrustedCrop.rasterHeight / 540) &&
                input.reacquiredTrustedGeometry.trustedBarAxes ==
                    ActivePictureBounds::BarAxes::TOP_BOTTOM &&
                input.retentionExcludedBandsPixelSafe &&
                !input.boundedVisibleContentOutsideCrop &&
                decision.state.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
                decision.state.entryTrustedCrop.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
                decision.state.entryTrustedCrop.left == 0 &&
                decision.state.entryTrustedCrop.right == decision.state.entryTrustedCrop.rasterWidth &&
                input.reacquiredTrustedClassification == ActivePictureClassification::BAR_CROP_TRUSTED;
            if (!input.nearBlackEvaluated || (input.globalNearBlack && !currentNativeCleanScope))
				decision.revalidationGates |= RECOVERY_NEAR_BLACK;
			if (input.fullRasterAuthorityAvailable)
				decision.revalidationGates |= RECOVERY_FULL_AUTHORITY;
			if (input.presentationEpoch != decision.state.presentationEpoch)
				decision.revalidationGates |= RECOVERY_CONTEXT;
			const bool qualifies = input.measurementCurrent &&
                input.nearBlackEvaluated && (!input.globalNearBlack || currentNativeCleanScope) &&
				!input.boundedVisibleContentOutsideCrop &&
				!input.fullRasterAuthorityAvailable &&
				input.presentationEpoch == decision.state.presentationEpoch &&
				exactEntryContract && exactCurrentObservation &&
				exactCurrentSafety;

			if (qualifies)
			{
                if (decision.state.revalidationLastSourceSequence != 0 &&
                    input.sourceSequence ==
                        decision.state.revalidationLastSourceSequence + 1 &&
                    decision.state.revalidationNativeDarkRun == currentNativeCleanScope)
				{
					++decision.state.revalidationSamples;
				}
				else
				{
					if (decision.state.revalidationSamples != 0)
						decision.revalidationGates |= RECOVERY_SEQUENCE_GAP;
					decision.state.revalidationStartedSourceSequence =
						input.sourceSequence;
					decision.state.revalidationSamples = 1;
				}
				decision.state.revalidationLastSourceSequence =
					input.sourceSequence;
                decision.state.revalidationNativeDarkRun = currentNativeCleanScope;
			}
			else
			{
				ResetNearBlackCropRevalidation(decision.state);
			}

			if (decision.state.revalidationSamples >=
				decision.revalidationSamplesRequired)
			{
				decision.revalidationSamples =
					decision.state.revalidationSamples;
                if (currentNativeCleanScope)
                {
                    // A dark source only nominates its saved native base. The
                    // episode keeps the displayed owner until selected submit.
                    decision.reason = "dark native Scope completed clean source-rate proof";
                }
                else
                {
				const bool wasRetainingCrop = decision.state.mode ==
					NearBlackPresentationMode::RETAIN_CROP;
				decision.state = {};
				decision.releasedToTrustedCrop = true;
				decision.ended = true;
				decision.reason = sparseEntryNativeReaffirmed
					? "independent native bars revalidated sparse entry after pixel-safe dwell"
					: wasRetainingCrop
						? "non-near-black scope revalidated; normal presentation resumed"
						: "exact entry crop revalidated after pixel-safe dwell";
                }
			}
		}

		// A tiny outward native correction cannot revalidate an undersized entry:
		// its excluded rows contain real picture. Verify the candidate's own bars
		// through the existing independent bootstrap proof, then withdraw the old
		// authority so normal acquisition can accept current geometry afterward.
		const bool nativeSamplingExpansion = decision.state.entryTrustedCropAvailable &&
			decision.state.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
			input.nativeBootstrapOrigin == ActivePictureAuthorityOrigin::NATIVE &&
			input.nativeBootstrapContractAvailable &&
			IsNearBlackNativeSamplingExpansion(decision.state.entryTrustedCrop,
				input.nativeBootstrapContract);
		// Preserve the pre-existing native bootstrap route for full-raster entry.
		// A partial exact-entry proof must not stall independent native acquisition.
		if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			(!decision.state.entryTrustedCropAvailable ||
			 decision.state.startedAtFullRaster || nativeSamplingExpansion))
		{
			uint32_t bootstrapGates = RECOVERY_OK;
			if (!input.measurementCurrent || !input.nativeBootstrapRetentionEvaluated ||
				input.nativeBootstrapSourceSequence != input.sourceSequence)
				bootstrapGates |= RECOVERY_MEASUREMENT;
			if (!input.nativeBootstrapContractAvailable) bootstrapGates |= RECOVERY_CONTRACT;
			if (input.nativeBootstrapOutwardVisible ||
				(input.measurementCurrent && input.nativeBootstrapRetentionEvaluated &&
				 input.nativeBootstrapSourceSequence == input.sourceSequence &&
				 !input.nativeBootstrapRetentionSafe))
				bootstrapGates |= RECOVERY_UNSAFE_BANDS;
			if (!input.nearBlackEvaluated || input.globalNearBlack)
				bootstrapGates |= RECOVERY_NEAR_BLACK;
			if (input.fullRasterAuthorityAvailable) bootstrapGates |= RECOVERY_FULL_AUTHORITY;
			if (input.nativeBootstrapSourceGeneration != input.sourceGeneration ||
				input.nativeBootstrapPresentationEpoch != decision.state.presentationEpoch)
				bootstrapGates |= RECOVERY_CONTEXT;
			// Saved-entry diagnostics describe that independent recovery path.
			if (!decision.state.entryTrustedCropAvailable)
				decision.revalidationGates |= bootstrapGates;
			const bool bootstrapQualifies = input.measurementCurrent &&
				input.nearBlackEvaluated && !input.globalNearBlack &&
				!input.fullRasterAuthorityAvailable &&
				input.nativeBootstrapContractAvailable &&
				input.nativeBootstrapRetentionEvaluated &&
				input.nativeBootstrapRetentionSafe &&
				!input.nativeBootstrapOutwardVisible &&
				input.nativeBootstrapSourceGeneration == input.sourceGeneration &&
				input.nativeBootstrapSourceSequence == input.sourceSequence &&
				input.nativeBootstrapPresentationEpoch ==
					decision.state.presentationEpoch &&
				input.presentationEpoch == decision.state.presentationEpoch &&
				ChromaAlignedTrustedContract(
					input.nativeBootstrapContract);
			const bool sameCandidate = bootstrapQualifies &&
				decision.state.bootstrapCandidateAvailable &&
				decision.state.bootstrapCandidateOrigin == input.nativeBootstrapOrigin &&
				SameTrustedCropContract(decision.state.bootstrapCandidate,
					input.nativeBootstrapContract);
			const bool timingContinuous = input.currentTick == 0 ||
				decision.state.bootstrapLastQualifiedTick == 0 ||
				(input.currentTick >=
					decision.state.bootstrapLastQualifiedTick &&
				 input.currentTick -
					decision.state.bootstrapLastQualifiedTick <= 100);
			auto seedCandidate = [&]()
			{
				decision.state.bootstrapCandidateAvailable = true;
				decision.state.bootstrapCandidate = input.nativeBootstrapContract;
				decision.state.bootstrapCandidateOrigin = input.nativeBootstrapOrigin;
				decision.state.bootstrapCandidateStartedTick = input.currentTick;
				decision.state.bootstrapLastQualifiedTick = input.currentTick;
				decision.state.bootstrapLastSourceSequence = input.sourceSequence;
				decision.state.bootstrapSamples = 1;
			};
			if (!bootstrapQualifies)
			{
				ResetNearBlackBootstrap(decision.state);
			}
			else if (!decision.state.bootstrapCandidateAvailable)
			{
				// Cached cadence repeats cannot invent an acquisition candidate.
				if (!input.cadenceRepeat)
					seedCandidate();
			}
			else if (!sameCandidate || !timingContinuous)
			{
				ResetNearBlackBootstrap(decision.state);
				if (!input.cadenceRepeat)
					seedCandidate();
			}
			else if (input.cadenceRepeat)
			{
				if (input.sourceSequence ==
					decision.state.bootstrapLastSourceSequence)
				{
					// Re-evaluate the identical decoded frame without counting it as
					// a new independent sample.
					decision.state.bootstrapLastQualifiedTick = input.currentTick;
				}
				else
				{
					ResetNearBlackBootstrap(decision.state);
				}
			}
			else if (input.sourceSequence ==
				decision.state.bootstrapLastSourceSequence + 1)
			{
				++decision.state.bootstrapSamples;
				decision.state.bootstrapLastSourceSequence = input.sourceSequence;
				decision.state.bootstrapLastQualifiedTick = input.currentTick;
			}
			else
			{
				ResetNearBlackBootstrap(decision.state);
				seedCandidate();
			}

			const bool pausedDwellComplete = bootstrapQualifies &&
				input.cadenceRepeat &&
				decision.state.bootstrapCandidateAvailable &&
				decision.state.bootstrapCandidateOrigin == input.nativeBootstrapOrigin &&
				input.sourceSequence ==
					decision.state.bootstrapLastSourceSequence &&
				SameTrustedCropContract(decision.state.bootstrapCandidate,
					input.nativeBootstrapContract) &&
				decision.state.bootstrapCandidateStartedTick != 0 &&
				input.currentTick >= decision.state.bootstrapCandidateStartedTick &&
				input.currentTick - decision.state.bootstrapCandidateStartedTick >= 500;
			if (decision.state.bootstrapSamples >=
					decision.bootstrapSamplesRequired || pausedDwellComplete)
			{
				decision.bootstrapSamples = decision.state.bootstrapSamples;
				decision.state = {};
				decision.bootstrapReleased = true;
				decision.resetTransitionEvidence = true;
				decision.resetTrustedGeometry = nativeSamplingExpansion;
				decision.ended = true;
				decision.reason = nativeSamplingExpansion
					? "native sampling expansion verified; undersized crop withdrawn for fresh acquisition"
					: "startup crop bootstrap verified; normal acquisition reopened";
			}
		}
		else
		{
			// Eligibility itself can disappear while a saved-entry episode remains
			// active. Do not carry partial proof across an inward/oversized candidate.
			ResetNearBlackBootstrap(decision.state);
		}

		if (!decision.releasedToTrustedCrop)
			decision.revalidationSamples =
				decision.state.revalidationSamples;
		if (!decision.bootstrapReleased)
			decision.bootstrapSamples = decision.state.bootstrapSamples;
		decision.revalidationChanged = presentationEpochChanged ||
			decision.releasedToTrustedCrop ||
			decision.bootstrapReleased ||
			decision.revalidationSamples !=
				input.previous.revalidationSamples ||
			decision.bootstrapSamples != input.previous.bootstrapSamples ||
			decision.state.outwardConfirmationSamples !=
				input.previous.outwardConfirmationSamples ||
			decision.state.confirmedNonNearBlackContent !=
				input.previous.confirmedNonNearBlackContent;

		if (decision.reason.empty())
		{
			decision.reason = decision.state.mode ==
				NearBlackPresentationMode::INACTIVE
				? "near-black title episode inactive"
				: "near-black title episode presentation retained";
		}
		return decision;
	}

	std::string RecoveryGateNames(uint32_t gates)
	{
		if (gates == RECOVERY_OK) return "none";
		std::string names;
		const auto add = [&](uint32_t bit, const char* name)
		{
			if ((gates & bit) == 0) return;
			if (!names.empty()) names += ',';
			names += name;
		};
		add(RECOVERY_MEASUREMENT, "measurement-stale-or-unavailable");
		add(RECOVERY_REPEAT, "repeat-or-out-of-order");
		add(RECOVERY_CONTEXT, "source-raster-epoch");
		add(RECOVERY_CONTRACT, "trusted-contract");
		add(RECOVERY_OBSERVATION, "observation-unavailable-or-not-contained");
		add(RECOVERY_UNSAFE_BANDS, "unsafe-excluded-bands");
		add(RECOVERY_NEAR_BLACK, "near-black-or-not-evaluated");
		add(RECOVERY_FULL_AUTHORITY, "full-raster-authority");
		add(RECOVERY_SEQUENCE_GAP, "source-sequence-gap");
		add(RECOVERY_OWNER, "presentation-owner-unresolved");
		add(RECOVERY_INSPECTION_REENTRY, "inspection-would-contract-occupied-fit");
		return names;
	}

	bool IsPixelSafeCropReaffirmation(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observed, bool excludedBandsPixelSafe)
	{
		const int width = trusted.rasterWidth, height = trusted.rasterHeight;
		if (!excludedBandsPixelSafe || !ValidBounds(trusted, width, height) ||
			!ValidBounds(observed, width, height) ||
			!HasAuthorityForCroppedAxes(trusted, width, height) ||
			!HasAuthorityForCroppedAxes(observed, width, height) ||
			trusted.trustedBarAxes != observed.trustedBarAxes) return false;
		// Same scale as transition-model sampling equivalence, not its much
		// broader 2% presentation deadband. Limit total size as well as edges.
		const int tolerance = std::max(2, std::max(width / 480, height / 270));
		return std::abs(trusted.left - observed.left) <= tolerance &&
			std::abs(trusted.top - observed.top) <= tolerance &&
			std::abs(trusted.right - observed.right) <= tolerance &&
			std::abs(trusted.bottom - observed.bottom) <= tolerance &&
			std::abs((trusted.right - trusted.left) - (observed.right - observed.left)) <= tolerance &&
			std::abs((trusted.bottom - trusted.top) - (observed.bottom - observed.top)) <= tolerance;
	}

	bool IsPixelSafeHorizontalSamplingEnvelope(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observed, const ActivePicturePresentationRetentionEvidence& evidence)
	{
		return evidence.analysisValid && evidence.presentationValid && evidence.excludedHorizontalBandsPixelSafe &&
			ValidBounds(trusted, trusted.rasterWidth, trusted.rasterHeight) &&
			ValidBounds(observed, trusted.rasterWidth, trusted.rasterHeight) &&
			SamplingEquivalentAxis(trusted.left, trusted.right, observed.left, observed.right, SamplingTolerance(trusted));
	}

	bool IsPixelSafeSamplingEnvelope(const ActivePictureBounds& trusted,
		const ActivePictureBounds& observed, const ActivePictureBounds& envelope,
		bool excludedBandsPixelSafe)
	{
		if (!IsPixelSafeCropReaffirmation(trusted, observed, excludedBandsPixelSafe))
			return false;
		// Borrow axes for the geometric comparison only; never publish them as
		// authority for this envelope. Include every edge, including pixel extents.
		auto comparison = envelope;
		comparison.trustedBarAxes = trusted.trustedBarAxes;
		return IsPixelSafeCropReaffirmation(trusted, comparison, excludedBandsPixelSafe);
	}

	bool CanResolveContainedInspectionRetention(
		const VerticalInspectionBridgeState& inspection, const PresentationRecoveryInput& input)
	{
		const auto& crop = input.crop;
		const auto& recovery = input.previous;
		const auto& admitted = input.previousAdmission;
		const auto& base = crop.geometry;
		// Resolve only the already displayed outward-fit/recovery trap. A logical
		// crop, a stale admission, or an unspent inspection is not this contract.
		if (!inspection.active || !inspection.failOpenLatched || !recovery.active ||
			!admitted.available || !admitted.outwardPresentationAvailable ||
			!crop.automaticCropEnabled || !crop.sharedGeometryAvailable ||
			crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			!ValidBounds(base, crop.rasterWidth, crop.rasterHeight) ||
			!HasAuthorityForCroppedAxes(base, crop.rasterWidth, crop.rasterHeight) ||
			!CropEdgesAreChromaAligned(base, crop.rasterWidth, crop.rasterHeight) ||
			!SameTrustedCropContract(inspection.trustedBase, base) ||
			!SameTrustedCropContract(recovery.trustedCrop, base) ||
			!SameTrustedCropContract(admitted.trustedCrop, base) ||
			!SameTrustedCropContract(input.retentionBounds, base))
			return false;
		const uint64_t generation = crop.frameSourceGeneration;
		const uint64_t sequence = crop.frameSourceSequence;
		if (generation == 0 || sequence == 0 ||
			crop.geometrySourceGeneration != generation ||
			inspection.sourceGeneration != generation || recovery.sourceGeneration != generation ||
			admitted.sourceGeneration != generation || input.retentionSourceGeneration != generation ||
			inspection.presentationEpoch != input.presentationEpoch ||
			recovery.presentationEpoch != input.presentationEpoch ||
			admitted.presentationEpoch != input.presentationEpoch ||
			input.retentionSourceSequence != sequence || input.cadenceRepeat ||
			inspection.firstCandidateSourceSequence == 0 || sequence <= inspection.firstCandidateSourceSequence ||
			recovery.lastSourceSequence == 0 || sequence <= recovery.lastSourceSequence ||
			admitted.presentationSourceSequence == 0 || sequence <= admitted.presentationSourceSequence ||
			sequence - admitted.presentationSourceSequence != 1)
			return false;
		if (!ValidBounds(admitted.outwardPresentation, crop.rasterWidth, crop.rasterHeight) ||
			!CropEdgesAreChromaAligned(admitted.outwardPresentation, crop.rasterWidth, crop.rasterHeight) ||
			!ContainedBounds(admitted.outwardPresentation, base) ||
			SameBounds(admitted.outwardPresentation, base))
			return false;
		// Do not turn inspection resolution into authority, or override another
		// current presentation owner. presentationFailOpen is intentionally not
		// checked: the caller preserves independent fail-open causes separately.
		if (!input.measurementCurrent || !input.retentionEvaluated || !input.excludedBandsPixelSafe ||
			!crop.frameLocalPresentationRetentionEvaluated || !crop.frameLocalPresentationRetentionSafe ||
			!input.observationAvailable || !crop.latestObservationIsProvisional ||
			crop.latestObservationIsUnavailable || crop.latestObservationSupportsCrop ||
			input.observationClassification != ActivePictureClassification::PROVISIONAL ||
			crop.latestObservationClassification != ActivePictureClassification::PROVISIONAL ||
			!ValidBounds(input.observation, crop.rasterWidth, crop.rasterHeight) ||
			!ContainedBounds(base, input.observation) ||
			!input.nearBlackEvaluated || input.globalNearBlack || input.confirmedPresentationResolved ||
			crop.fullRasterPresentationAuthoritative || crop.movingPictureTransition ||
			crop.nearBlackEpisodeFullRaster || crop.nearBlackEpisodeRetainCrop ||
			crop.outwardPresentationActive || crop.verticalTranslationActive ||
			crop.verticalTranslationConfirmationPending || crop.verticalFitConfirmationPending ||
			crop.verticalTranslationBaseRetentionActive || crop.verticalTranslationEngageBaseRetentionActive ||
			crop.barCropRefinementPending || crop.barCropRefinementHorizontalConflict ||
			crop.pictureTransitionHandoff.active || crop.movingPictureHold.competingPresentation)
			return false;
		if (crop.currentVisibleBoundsAvailable &&
			(crop.currentVisibleSourceGeneration != generation || crop.currentVisibleSourceSequence != sequence ||
			 !SameTrustedCropContract(crop.currentVisibleBase, base) ||
			 !ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight) ||
			 !ContainedBounds(base, crop.currentVisibleBounds)))
			return false;
		return true;
	}

    ConfirmedFitOwnershipEvidence QualifyConfirmedFitOwnership(
        const ConfirmedFitOwnershipInput& input)
    {
        ConfirmedFitOwnershipEvidence result;
        const auto& native = input.nativeBase;
        const auto& approved = input.approvedBounds;
        const int width = native.rasterWidth, height = native.rasterHeight;
        if (!input.current || !input.confirmed || !input.sourceGeneration || !input.sourceSequence ||
            (input.origin != ConfirmedFitOwnershipOrigin::CURRENT_CAPTION &&
             input.origin != ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY &&
             input.origin != ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT) ||
            !ValidBounds(native,width,height) || !HasAuthorityForCroppedAxes(native,width,height) ||
            !CropEdgesAreChromaAligned(native,width,height) ||
            !ValidBounds(approved,width,height) || !CropEdgesAreChromaAligned(approved,width,height) ||
            !ValidBounds(input.evidenceBounds,width,height) ||
            !ContainedBounds(approved,native) || SameBounds(approved,native) ||
            !ContainedBounds(approved,input.evidenceBounds))
            return result;
        if (input.origin == ConfirmedFitOwnershipOrigin::CURRENT_CAPTION &&
            ContainedBounds(native,input.evidenceBounds))
            return result; // An inside-only cue cannot originate an outward FIT.
        if (input.origin == ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY ||
            input.origin == ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT)
        {
            // Every expanding edge must have independently current bar pixels.
            // Classic additionally requires an overlay-like shape.
            // A mixed UI/picture pair cannot turn broad dense FIT into a caption.
            if (native.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
                native.left != 0 || native.right != width ||
                approved.left != native.left || approved.right != native.right ||
                (!input.upperContent && !input.lowerContent) ||
                (input.origin == ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY &&
                    ((input.upperContent && !input.upperOverlayLike) ||
                     (input.lowerContent && !input.lowerOverlayLike))) ||
                (approved.top < native.top && !input.upperContent) ||
                (approved.bottom > native.bottom && !input.lowerContent) ||
                (input.upperContent && (input.evidenceBounds.top >= native.top ||
                    approved.top >= native.top)) ||
                (input.lowerContent && (input.evidenceBounds.bottom <= native.bottom ||
                    approved.bottom <= native.bottom)))
                return result;
        }
        if (input.origin == ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT)
        {
            PresentationEnvelopeGeometryInput measured;
            measured.trustedPicture = native;
            measured.observedContent = input.evidenceBounds;
            measured.observedContentAvailable = true;
            measured.expandTop = input.upperContent;
            measured.expandBottom = input.lowerContent;
            measured.horizontalPadding = input.horizontalPadding;
            measured.verticalPadding = input.verticalPadding;
            const auto bounded = BuildPresentationEnvelope(measured);
            if (!bounded.valid || !bounded.expanded || !SameBounds(bounded.bounds, approved))
                return result; // No authority beyond the measured content and padding.
        }
        result.available = true;
        result.origin = input.origin;
        result.nativeBase = native;
        result.approvedBounds = approved;
        result.sourceGeneration = input.sourceGeneration;
        result.sourceSequence = input.sourceSequence;
        result.presentationEpoch = input.presentationEpoch;
        return result;
    }

    ConfirmedFitOwnershipEvidence QualifyCaptionFitWithinPadding(
        const ConfirmedFitOwnershipInput& input,int horizontalPadding,int verticalPadding)
    {
        if(input.origin!=ConfirmedFitOwnershipOrigin::CURRENT_CAPTION)
            return {};
        const auto qualified=QualifyConfirmedFitOwnership(input);
        if(!qualified.available)return {};
        PresentationEnvelopeGeometryInput caption;
        caption.trustedPicture=input.nativeBase;
        caption.observedContent=input.evidenceBounds;
        caption.observedContentAvailable=true;
        caption.expandLeft=input.evidenceBounds.left<input.nativeBase.left;
        caption.expandTop=input.evidenceBounds.top<input.nativeBase.top;
        caption.expandRight=input.evidenceBounds.right>input.nativeBase.right;
        caption.expandBottom=input.evidenceBounds.bottom>input.nativeBase.bottom;
        caption.horizontalPadding=horizontalPadding;caption.verticalPadding=verticalPadding;
        const auto maximum=BuildPresentationEnvelope(caption);
        if(!maximum.valid || !maximum.expanded ||
            !ContainedBounds(maximum.bounds,input.approvedBounds))
            return {};
        return qualified;
    }
    static bool MatchesConfirmedFitOwnership(const ConfirmedFitOwnershipEvidence& proof,
        const ActivePictureBounds& native, const ActivePictureBounds& actual,
        uint64_t generation, uint64_t sequence, uint64_t epoch)
    {
        const int width=native.rasterWidth,height=native.rasterHeight;
        return proof.available && generation && sequence &&
            (proof.origin==ConfirmedFitOwnershipOrigin::CURRENT_CAPTION ||
             proof.origin==ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY ||
             proof.origin==ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT) &&
            proof.sourceGeneration==generation && proof.sourceSequence==sequence &&
            proof.presentationEpoch==epoch && SameTrustedCropContract(proof.nativeBase,native) &&
            ValidBounds(native,width,height) && HasAuthorityForCroppedAxes(native,width,height) &&
            CropEdgesAreChromaAligned(native,width,height) &&
            ValidBounds(actual,width,height) && CropEdgesAreChromaAligned(actual,width,height) &&
            ContainedBounds(actual,native) && !SameBounds(actual,native) &&
            ValidBounds(proof.approvedBounds,width,height) && SameBounds(proof.approvedBounds,actual);
    }

    static bool HasCurrentConfirmedOutwardCandidate(const PresentationRecoveryInput& input,
        const Decision& proposed)
    {
        const auto& accepted=input.candidate;
        const auto& crop=input.crop;
        // Native refinement and presentation expansion are independent. A label
        // cannot supply (or discard) exact current caption/content ownership.
        return crop.sharedGeometryAvailable && crop.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
            crop.geometrySourceGeneration==crop.frameSourceGeneration &&
            input.presentationEpoch==crop.framePresentationEpoch &&
            ValidBounds(crop.geometry,crop.rasterWidth,crop.rasterHeight) &&
            accepted.applyCrop && proposed.applyCrop && !accepted.verticallyTranslated && !proposed.verticallyTranslated &&
            accepted.owner!=DecisionOwner::FULL_RASTER && proposed.owner!=DecisionOwner::FULL_RASTER &&
            SameBounds(accepted.sourceBounds,proposed.sourceBounds) &&
            MatchesConfirmedFitOwnership(input.confirmedFitOwnership,crop.geometry,proposed.sourceBounds,
                crop.frameSourceGeneration,crop.frameSourceSequence,input.presentationEpoch);
    }

    static bool HasConfirmedPresentationCandidate(const PresentationRecoveryInput& input,
        const Decision& proposed)
    {
        // Recovery/inspection resolution is not caption ownership. In
        // particular, a dense FIT label cannot authorize a full-raster flash.
        const auto& accepted=input.candidate;
        if (accepted.owner==DecisionOwner::OUTWARD_FIT ||
            (ContainedBounds(accepted.sourceBounds,input.crop.geometry) &&
             !SameBounds(accepted.sourceBounds,input.crop.geometry)))
            return HasCurrentConfirmedOutwardCandidate(input,proposed);
        // Keep the actual confirmed translation contract, including Classic UI.
        return input.confirmedPresentationResolved && input.measurementCurrent && input.retentionEvaluated &&
            input.crop.frameSourceGeneration != 0 && input.crop.frameSourceSequence != 0 &&
            input.retentionSourceGeneration == input.crop.frameSourceGeneration &&
            input.retentionSourceSequence == input.crop.frameSourceSequence &&
            SameTrustedCropContract(input.retentionBounds, input.crop.geometry) &&
            accepted.applyCrop && proposed.applyCrop &&
            accepted.owner == DecisionOwner::VERTICAL_TRANSLATION &&
            accepted.owner == proposed.owner &&
            accepted.verticallyTranslated == proposed.verticallyTranslated &&
            accepted.verticalTranslationPixels == proposed.verticalTranslationPixels &&
            ValidBounds(accepted.sourceBounds, input.crop.rasterWidth, input.crop.rasterHeight) &&
            CropEdgesAreChromaAligned(accepted.sourceBounds, input.crop.rasterWidth, input.crop.rasterHeight) &&
            SameBounds(accepted.sourceBounds, proposed.sourceBounds);
    }

	NearBlackBoundedPresentationDecision EvaluateNearBlackBoundedPresentation(
		const NearBlackPresentationEpisodeState& previous, const PresentationRecoveryInput& input)
	{
		NearBlackBoundedPresentationDecision result;
		result.state = previous;
		result.presentation = input.candidate;
		const auto& crop = input.crop;
		const bool fullRasterEpisode =
			previous.mode == NearBlackPresentationMode::FULL_RASTER &&
			crop.nearBlackEpisodeFullRaster && !input.candidate.applyCrop;
		const bool retainedScopeEpisode =
			previous.mode == NearBlackPresentationMode::RETAIN_CROP &&
			crop.nearBlackEpisodeRetainCrop;
		if (!fullRasterEpisode && !retainedScopeEpisode)
			return result;
		const uint64_t generation = crop.frameSourceGeneration;
		const uint64_t sequence = crop.frameSourceSequence;
		const auto& base = crop.geometry;
		const auto& admission = input.previousAdmission;
        // This exact source already passed the narrow fringe + applied-fill
        // proof. A broader temporary envelope must not undo that presentation.
        if (retainedScopeEpisode && previous.weakFringeFillSourceSequence == sequence &&
            input.candidate.applyCrop && SameBounds(input.candidate.sourceBounds,base))
            return result;
		const auto& currentAnchor = input.submittedScopeAnchor;
        // Unknown evidence cannot originate a different presentation of an
        // already submitted native Scope. Sparse nonblack samples still veto
        // new crop acquisition; they do not identify picture versus subtitle,
        // UI, noise or an unresolved detector-axis fallback. Let the existing
        // independent native/caption confirmation paths own any expansion.
        if (retainedScopeEpisode &&
            IsCurrentSubmittedNativeScopeAnchor(currentAnchor, generation, sequence,
                input.presentationEpoch, crop.rasterWidth, crop.rasterHeight) &&
            previous.sourceGeneration == generation &&
            previous.presentationEpoch == input.presentationEpoch &&
            previous.entryTrustedCropAvailable &&
            previous.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            SameTrustedCropContract(previous.entryTrustedCrop, base) &&
            SameTrustedCropContract(currentAnchor.crop, base) &&
            crop.automaticCropEnabled && crop.sharedGeometryAvailable &&
            crop.geometrySourceGeneration == generation &&
            crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            !crop.fullRasterPresentationAuthoritative &&
            crop.latestObservationClassification != ActivePictureClassification::FULL_RASTER_TRUSTED &&
            input.observationClassification != ActivePictureClassification::FULL_RASTER_TRUSTED &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active &&
            !crop.movingPictureHold.competingPresentation)
        {
            const auto& accepted = input.candidate;
            const bool acceptedTranslation = accepted.applyCrop && accepted.verticallyTranslated &&
                accepted.owner == DecisionOwner::VERTICAL_TRANSLATION && crop.verticalTranslationActive &&
                crop.verticalTranslationSourceGeneration == generation &&
                SameTrustedCropContract(crop.verticalTranslationBase, base) &&
                ValidBounds(accepted.sourceBounds, crop.rasterWidth, crop.rasterHeight) &&
                CropEdgesAreChromaAligned(accepted.sourceBounds, crop.rasterWidth, crop.rasterHeight) &&
                accepted.sourceBounds.left == base.left && accepted.sourceBounds.right == base.right &&
                accepted.sourceBounds.top == base.top + accepted.verticalTranslationPixels &&
                accepted.sourceBounds.bottom == base.bottom + accepted.verticalTranslationPixels;
            if (HasConfirmedPresentationCandidate(input, accepted) || acceptedTranslation)
                return result;
            result.presentation = {};
            result.presentation.sourceBounds = currentAnchor.crop;
            result.presentation.applyCrop = true;
            result.presentation.owner = DecisionOwner::NEAR_BLACK_EPISODE;
            result.presentation.reason =
                "retain submitted native Scope; unqualified bar occupancy cannot expand presentation";
            result.retainedScopeFallback = true;
            return result;
        }
		const bool currentVisibleWitness = crop.currentVisibleBoundsAvailable &&
			crop.currentVisibleSourceGeneration == generation &&
			crop.currentVisibleSourceSequence == sequence &&
			SameTrustedCropContract(crop.currentVisibleBase, base) &&
			ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight);
		const bool currentVisibleOutsideBase = currentVisibleWitness &&
			!ContainedBounds(base, crop.currentVisibleBounds);
		const bool currentVisibleCoversObservation = currentVisibleWitness &&
			ContainedBounds(crop.currentVisibleBounds, input.observation);
		const bool retainedScopePendingEnvelope = retainedScopeEpisode &&
			currentVisibleOutsideBase && currentVisibleCoversObservation;
		// Acquisition deliberately demotes native bars while an episode owns the
		// crop. Retention still carries the original classification. Accept only
		// that existing policy's transformation, never an unrelated class mismatch.
		ActivePictureEvidence rawObservation;
		rawObservation.available = input.observationAvailable;
		rawObservation.classification = input.observationClassification;
		const auto constrainedObservation = ConstrainNearBlackCropAcquisition(rawObservation, true);
		const bool classificationCompatible =
			crop.latestObservationClassification == input.observationClassification ||
			(crop.latestObservationIsProvisional &&
			 crop.latestObservationClassification == constrainedObservation.classification);
        // Retaining logical Scope must not conceal positively measured pixels.
        // Missing evidence may retain its submitted contract; a current unsafe
        // measurement without a complete visible envelope cannot tighten output.
        const bool currentUnsafeWithoutEnvelope = input.measurementCurrent &&
            input.retentionEvaluated && input.retentionSourceGeneration == generation &&
            input.retentionSourceSequence == sequence &&
            SameTrustedCropContract(input.retentionBounds, base) &&
            !input.excludedBandsPixelSafe && !currentVisibleWitness;
        const bool currentVisibleFullRaster = currentVisibleWitness &&
            SameBounds(crop.currentVisibleBounds, FullRaster(crop.rasterWidth, crop.rasterHeight));
        if (currentUnsafeWithoutEnvelope || currentVisibleFullRaster)
        {
            result.presentation = {};
            result.presentation.sourceBounds = FullRaster(crop.rasterWidth, crop.rasterHeight);
            result.presentation.reason = currentVisibleFullRaster
                ? "current visible pixels require full-raster output without retiring logical Scope"
                : "current unsafe excluded pixels lack a complete bounded envelope";
            return result;
        }
		auto holdAdmittedScope = [&]() {
			// Keep a previously submitted native Scope through uncertain frames.
			// If exact current pixels prove outward content, widen only to that
			// bounded extent; an exact full-raster extent remains a full-raster
			// presentation. Admission alone cannot anchor this fallback.
			if (!retainedScopeEpisode || input.candidate.applyCrop ||
				!generation || !sequence ||
				previous.sourceGeneration != generation ||
				previous.presentationEpoch != input.presentationEpoch ||
				!previous.startedSourceSequence || sequence < previous.startedSourceSequence ||
				!previous.entryTrustedCropAvailable ||
				previous.entryTrustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
				!IsCurrentSubmittedNativeScopeAnchor(currentAnchor, generation, sequence,
					input.presentationEpoch, crop.rasterWidth, crop.rasterHeight) ||
				!SameTrustedCropContract(currentAnchor.crop, base) ||
				!crop.automaticCropEnabled || !crop.sharedGeometryAvailable ||
				crop.geometrySourceGeneration != generation ||
				crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
				!ValidBounds(base, crop.rasterWidth, crop.rasterHeight) ||
				!CropEdgesAreChromaAligned(base, crop.rasterWidth, crop.rasterHeight) ||
				!HasAuthorityForCroppedAxes(base, crop.rasterWidth, crop.rasterHeight) ||
				!SameTrustedCropContract(previous.entryTrustedCrop, base) ||
				crop.presentationFailOpen || crop.fullRasterPresentationAuthoritative ||
				crop.movingPictureTransition || crop.pictureTransitionHandoff.active ||
				crop.movingPictureHold.competingPresentation ||
				crop.verticalTranslationActive ||
				crop.latestObservationClassification == ActivePictureClassification::FULL_RASTER_TRUSTED ||
				input.observationClassification == ActivePictureClassification::FULL_RASTER_TRUSTED)
				return false;

			auto heldBounds = base;
			if (currentVisibleOutsideBase)
			{
				if (!currentVisibleCoversObservation)
					return false;
				heldBounds.left = std::min(heldBounds.left, crop.currentVisibleBounds.left);
				heldBounds.top = std::min(heldBounds.top, crop.currentVisibleBounds.top);
			heldBounds.right = std::max(heldBounds.right, crop.currentVisibleBounds.right);
			heldBounds.bottom = std::max(heldBounds.bottom, crop.currentVisibleBounds.bottom);
			heldBounds.left &= ~1;
			heldBounds.top &= ~1;
			heldBounds.right = std::min(crop.rasterWidth, (heldBounds.right + 1) & ~1);
			heldBounds.bottom = std::min(crop.rasterHeight, (heldBounds.bottom + 1) & ~1);
				if (!ValidBounds(heldBounds, crop.rasterWidth, crop.rasterHeight) ||
					SameBounds(heldBounds, FullRaster(crop.rasterWidth, crop.rasterHeight)))
					return false;
				heldBounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
				heldBounds.aspectRatio = double(heldBounds.right - heldBounds.left) /
					std::max(1, heldBounds.bottom - heldBounds.top);
			}
			result.presentation = input.candidate;
			result.presentation.sourceBounds = heldBounds;
			result.presentation.applyCrop = true;
			result.presentation.outwardExpanded = !SameBounds(heldBounds, base);
			result.presentation.owner = DecisionOwner::NEAR_BLACK_EPISODE;
			result.presentation.reason = SameBounds(heldBounds, base)
				? "retain the last submitted native Scope while current dark-frame evidence is uncertain"
				: "retain the last submitted native Scope with a current visible-pixel envelope";
			result.retainedScopeFallback = true;
			return true;
		};
		auto reject = [&]() {
			// A weak or missing witness cannot turn an admitted Scope into full
			// raster. Preserve the exact native Scope contract for this episode.
			holdAdmittedScope();
			return result;
		};
		if (previous.boundedPresentationFailed)
		{
			// Old full-raster envelope state is not logical AR authority. In a
			// retained Scope episode, keep the admitted crop unless independent
			// full-raster authority or a scene transition now owns presentation.
			if (retainedScopeEpisode)
				holdAdmittedScope();
			return result;
		}
		if (!previous.entryTrustedCropAvailable ||
			previous.entryTrustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			!generation || !sequence || previous.sourceGeneration != generation ||
			previous.presentationEpoch != input.presentationEpoch ||
			!(fullRasterEpisode ? previous.fullRasterStartedSourceSequence : previous.startedSourceSequence) ||
			sequence < (fullRasterEpisode ? previous.fullRasterStartedSourceSequence : previous.startedSourceSequence) ||
			!crop.automaticCropEnabled || !crop.sharedGeometryAvailable ||
			crop.geometrySourceGeneration != generation ||
			crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			!ValidBounds(base, crop.rasterWidth, crop.rasterHeight) ||
			!CropEdgesAreChromaAligned(base, crop.rasterWidth, crop.rasterHeight) ||
			!HasAuthorityForCroppedAxes(base, crop.rasterWidth, crop.rasterHeight) ||
			!SameTrustedCropContract(previous.entryTrustedCrop, base) ||
			!admission.available || admission.sourceGeneration != generation ||
			admission.presentationEpoch != input.presentationEpoch ||
			!SameTrustedCropContract(admission.trustedCrop, base) ||
			input.previous.active || crop.presentationFailOpen ||
			crop.fullRasterPresentationAuthoritative || crop.movingPictureTransition ||
			crop.verticalTranslationActive ||
			((crop.verticalTranslationConfirmationPending || crop.verticalFitConfirmationPending ||
			  crop.verticalInspectionPending) && !retainedScopePendingEnvelope) ||
			crop.verticalTranslationBaseRetentionActive || crop.verticalTranslationEngageBaseRetentionActive ||
			crop.pictureTransitionHandoff.active || crop.movingPictureHold.competingPresentation ||
			!input.measurementCurrent || !input.retentionEvaluated ||
			input.retentionSourceGeneration != generation || input.retentionSourceSequence != sequence ||
			!SameTrustedCropContract(input.retentionBounds, base) ||
			!input.nearBlackEvaluated ||
			!input.observationAvailable || crop.latestObservationIsUnavailable ||
			(input.observationClassification != ActivePictureClassification::PROVISIONAL &&
			 input.observationClassification != ActivePictureClassification::BAR_CROP_TRUSTED) ||
			!classificationCompatible ||
			!ValidBounds(input.observation, crop.rasterWidth, crop.rasterHeight))
			return reject();
		if (previous.boundedPresentationAvailable &&
			(sequence < previous.boundedPresentationSequence ||
			 sequence - previous.boundedPresentationSequence > 1))
			return reject();
		// Repeats can reuse an exact-frame certificate, never originate one.
		if (input.cadenceRepeat && (!previous.boundedPresentationAvailable ||
			sequence != previous.boundedPresentationSequence))
			return reject();
		// A previously admitted Scope can see a small real picture fragment
		// outside its crop on the first dark frame. Keep Scope as logical
		// geometry, but present the pixel-measured union so the fragment is not
		// clipped. The extent is exact-source evidence and never becomes crop
		// authority.
		const bool certifiedDarkOutwardRepeat = input.cadenceRepeat &&
			previous.boundedPresentationAvailable &&
			sequence == previous.boundedPresentationSequence;
		const bool darkOutwardWitness = input.globalNearBlack &&
			currentVisibleOutsideBase &&
			(!input.cadenceRepeat || certifiedDarkOutwardRepeat);
		const bool heldCommittedEnvelope = retainedScopeEpisode &&
			!currentVisibleOutsideBase && previous.boundedPresentationAvailable &&
			previous.boundedPresentationPresentedSequence == previous.boundedPresentationSequence &&
			(previous.boundedPresentationSequence == sequence ||
			 previous.boundedPresentationSequence + 1 == sequence) &&
			input.excludedBandsPixelSafe &&
			ContainedBounds(previous.boundedPresentation, input.observation);
		if (input.globalNearBlack && !darkOutwardWitness && !heldCommittedEnvelope)
			return reject();
		if (retainedScopeEpisode && !currentVisibleOutsideBase && !heldCommittedEnvelope)
			return result;
		auto bounds = FullRaster(crop.rasterWidth, crop.rasterHeight);
		bool outwardEnvelope = false;
		if (heldCommittedEnvelope)
			bounds = previous.boundedPresentation;
		if (currentVisibleWitness &&
			ContainedBounds(crop.currentVisibleBounds, input.observation) &&
			ContainedBounds(crop.currentVisibleBounds, base))
			bounds = crop.currentVisibleBounds;
		else if (currentVisibleOutsideBase)
		{
			bounds = base;
			bounds.left = std::min(bounds.left, crop.currentVisibleBounds.left);
			bounds.top = std::min(bounds.top, crop.currentVisibleBounds.top);
			bounds.right = std::max(bounds.right, crop.currentVisibleBounds.right);
			bounds.bottom = std::max(bounds.bottom, crop.currentVisibleBounds.bottom);
			if (!ContainedBounds(bounds, input.observation))
				return reject();
			outwardEnvelope = true;
		}
		else if (!crop.currentVisibleBoundsAvailable && input.excludedBandsPixelSafe &&
			previous.boundedPresentationAvailable &&
			ContainedBounds(previous.boundedPresentation, input.observation))
			bounds = previous.boundedPresentation;
		else
			return reject();
		bounds.left &= ~1;
		bounds.top &= ~1;
		bounds.right = std::min(crop.rasterWidth, (bounds.right + 1) & ~1);
		bounds.bottom = std::min(crop.rasterHeight, (bounds.bottom + 1) & ~1);
		if (previous.boundedPresentationAvailable)
		{
			const auto& held = previous.boundedPresentation;
			if (!ValidBounds(held, crop.rasterWidth, crop.rasterHeight) ||
				!CropEdgesAreChromaAligned(held, crop.rasterWidth, crop.rasterHeight) ||
				(!outwardEnvelope && !ContainedBounds(held, base)))
				return reject();
			bounds.left = std::min(bounds.left, held.left);
			bounds.top = std::min(bounds.top, held.top);
			bounds.right = std::max(bounds.right, held.right);
			bounds.bottom = std::max(bounds.bottom, held.bottom);
		}
		if (SameBounds(bounds, FullRaster(crop.rasterWidth, crop.rasterHeight)))
		{
			// Sparse visible-pixel extent can include subtitle glyphs, noise, or
			// letterbox residue. It is not enough to replace a previously admitted
			// Scope with full raster. Independent full-raster AR authority takes
			// the separate transition path above.
			if (retainedScopeEpisode)
				holdAdmittedScope();
			return result;
		}
		bounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
		bounds.aspectRatio = double(bounds.right - bounds.left) / (bounds.bottom - bounds.top);
		result.proposalAvailable = true;
		result.proposalBase = base;
		result.proposalBounds = bounds;
		result.proposalMode = previous.mode;
		result.sourceGeneration = generation;
		result.presentationEpoch = input.presentationEpoch;
		result.sourceSequence = sequence;
		result.episodeStartSequence = previous.mode == NearBlackPresentationMode::FULL_RASTER
			? previous.fullRasterStartedSourceSequence : previous.startedSourceSequence;
		result.boundedPresentation = true;
		result.presentation = {};
		result.presentation.sourceBounds = bounds;
		result.presentation.applyCrop = true;
		result.presentation.outwardExpanded = false;
		result.presentation.owner = DecisionOwner::NEAR_BLACK_EPISODE;
		result.presentation.reason = "temporary visible envelope keeps the admitted Scope while near-black AR evidence recovers";
		return result;
	}

	NearBlackBoundedPresentationCommitDecision CommitNearBlackBoundedPresentation(
		const NearBlackBoundedPresentationCommitInput& input)
	{
		NearBlackBoundedPresentationCommitDecision result;
		result.state = input.currentEpisode;
		if (!input.renderedAndSubmitted || input.sceneBoundary ||
			!input.sourceGeneration || !input.selectedSourceSequence ||
			input.proposalSourceSequence != input.selectedSourceSequence ||
			input.selectedSourceSequence > input.currentSourceSequence ||
			input.selectedActualOwner != DecisionOwner::NEAR_BLACK_EPISODE ||
			input.currentEpisode.sourceGeneration != input.sourceGeneration ||
			input.currentEpisode.presentationEpoch != input.presentationEpoch ||
			input.currentEpisode.mode != input.proposalMode ||
			!input.episodeStartSequence ||
			(input.proposalMode == NearBlackPresentationMode::RETAIN_CROP
				? input.currentEpisode.startedSourceSequence != input.episodeStartSequence
				: input.proposalMode != NearBlackPresentationMode::FULL_RASTER ||
				  input.currentEpisode.fullRasterStartedSourceSequence != input.episodeStartSequence) ||
			!input.currentEpisode.entryTrustedCropAvailable ||
			input.currentEpisode.entryTrustedCropOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			!input.currentAdmission.available ||
			input.currentAdmission.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			input.currentAdmission.sourceGeneration != input.sourceGeneration ||
			input.currentAdmission.presentationEpoch != input.presentationEpoch ||
			!SameTrustedCropContract(input.currentEpisode.entryTrustedCrop, input.proposalBase) ||
			!SameTrustedCropContract(input.currentAdmission.trustedCrop, input.proposalBase) ||
			!ValidBounds(input.proposalBase, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
			!CropEdgesAreChromaAligned(input.proposalBase, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
			!SameBounds(input.selectedActualBounds, input.proposalBounds))
			return result;
		if (input.currentEpisode.boundedPresentationAvailable &&
			input.selectedSourceSequence <= input.currentEpisode.boundedPresentationPresentedSequence)
		{
			result.reason = "selected source already committed";
			return result;
		}
		const auto fullRaster = FullRaster(input.proposalBase.rasterWidth,
			input.proposalBase.rasterHeight);
		if (!ValidBounds(input.proposalBounds, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
			!CropEdgesAreChromaAligned(input.proposalBounds, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
			(input.failOpenProposal
				? !SameBounds(input.proposalBounds, fullRaster) ||
				  !SameBounds(input.selectedActualBounds, fullRaster)
				: SameBounds(input.proposalBounds, fullRaster) ||
				  !ContainedBounds(input.proposalBounds, input.proposalBase)))
			return result;
		if (input.failOpenProposal)
		{
			result.state.boundedPresentationAvailable = false;
			result.state.boundedPresentation = {};
			result.state.boundedPresentationSequence = 0;
			result.state.boundedPresentationPresentedSequence = 0;
			result.state.boundedPresentationFailed = true;
			result.committed = true;
			result.reason = "selected full-raster visibility safely failed the temporary Scope envelope open";
			return result;
		}
		if (input.currentEpisode.boundedPresentationAvailable)
		{
			const auto& held = input.currentEpisode.boundedPresentation;
			if (!ValidBounds(held, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
				!CropEdgesAreChromaAligned(held, input.proposalBase.rasterWidth, input.proposalBase.rasterHeight) ||
				!ContainedBounds(input.proposalBounds, held))
				return result;
		}
		result.state.boundedPresentationAvailable = true;
		result.state.boundedPresentationFailed = false;
		result.state.boundedPresentation = input.proposalBounds;
		result.state.boundedPresentationSequence = input.selectedSourceSequence;
		result.state.boundedPresentationPresentedSequence = input.selectedSourceSequence;
		result.committed = true;
		result.reason = "selected near-black envelope submitted without changing logical aspect-ratio authority";
		return result;
	}

	PresentationRecoveryDecision EvaluatePresentationRecovery(
		const PresentationRecoveryInput& input)
	{
		PresentationRecoveryDecision result;
		result.state = input.previous;
		result.presentation = input.candidate;
        result.presentation.confirmedFitOwnership={};
        if(HasCurrentConfirmedOutwardCandidate(input,result.presentation))
            result.presentation.confirmedFitOwnership=input.confirmedFitOwnership;
		result.required = NearBlackCropRevalidationSamples(input.framesPerSecond);
		const auto& crop = input.crop;
		const bool currentContract = crop.sharedGeometryAvailable &&
			crop.geometrySourceGeneration == crop.frameSourceGeneration &&
			crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			ValidBounds(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
			HasAuthorityForCroppedAxes(crop.geometry, crop.rasterWidth, crop.rasterHeight);
		const bool contextChanged = result.state.active &&
			(result.state.sourceGeneration != crop.frameSourceGeneration ||
			 result.state.presentationEpoch != input.presentationEpoch ||
			 result.state.trustedCrop.rasterWidth != crop.rasterWidth ||
			 result.state.trustedCrop.rasterHeight != crop.rasterHeight);
		if (!crop.automaticCropEnabled || crop.fullRasterPresentationAuthoritative ||
			contextChanged || input.confirmedPresentationResolved)
		{
			result.ended = result.state.active;
			result.released = result.state.active && input.confirmedPresentationResolved;
			result.gates = contextChanged ? RECOVERY_CONTEXT :
				crop.fullRasterPresentationAuthoritative ? RECOVERY_FULL_AUTHORITY : RECOVERY_OK;
			result.state = {};
			return result;
		}
        // The raw observation can still contain the subtitle which this exact
        // source packet already erased. It cannot deadlock recovery behind the
        // caption. Keep every independent full-raster/transition/fail-open veto.
        if (HasCurrentPreparedSubtitlePresentation(crop) &&
            input.presentationEpoch==crop.framePresentationEpoch &&
            ValidBounds(crop.geometry,crop.rasterWidth,crop.rasterHeight) &&
            !crop.nearBlackEpisodeFullRaster && !crop.movingPictureTransition &&
            !HasCurrentPictureTransitionHandoff(crop) && !crop.presentationFailOpen &&
            input.candidate.applyCrop && input.candidate.owner==DecisionOwner::TRUSTED_CROP &&
            !input.candidate.outwardExpanded && !input.candidate.verticallyTranslated &&
            SameBounds(input.candidate.sourceBounds,crop.geometry))
        {
            result.ended=result.released=result.state.active;
            result.state={};result.gates=RECOVERY_OK;
            return result;
        }
		// A submitted-scope continuity proof is current-frame pixel evidence for an
		// already displayed crop. It resolves stale withdrawal/full-raster recovery
		// immediately; it does not admit a new crop or override positive full authority.
		if (crop.submittedScopeContinuityRetention && input.candidate.applyCrop)
		{
			result.ended = result.released = result.state.active;
			result.state = {};
			result.gates = RECOVERY_OK;
			return result;
		}

        // Inspection may retain a logical crop only if that is still what we
        // were showing. When a dense FIT expires over occupied former bars,
        // restoring the smaller logical base would manufacture a zoom flash.
        // The prior admitted fit is a continuity floor, never current pixel
        // proof: recovery below must still expose this frame's visible extent.
        const auto& prior = input.previousAdmission;
        const bool continuousPresentation = prior.presentationSourceSequence != 0 &&
            (crop.frameSourceSequence > prior.presentationSourceSequence
                ? crop.frameSourceSequence - prior.presentationSourceSequence == 1
                : crop.frameSourceSequence == prior.presentationSourceSequence);
        const bool inspectionReentry = !result.state.active && currentContract &&
            !crop.nearBlackEpisodeFullRaster && !crop.movingPictureTransition &&
            !crop.latestObservationSupportsCrop &&
            input.candidate.applyCrop && input.candidate.owner == DecisionOwner::VERTICAL_INSPECTION &&
            SameBounds(input.candidate.sourceBounds, crop.geometry) &&
            prior.available && prior.outwardPresentationAvailable && continuousPresentation &&
            prior.sourceGeneration != 0 && prior.sourceGeneration == crop.frameSourceGeneration &&
            prior.presentationEpoch == input.presentationEpoch &&
            SameTrustedCropContract(prior.trustedCrop, crop.geometry) &&
            ValidBounds(prior.outwardPresentation, crop.rasterWidth, crop.rasterHeight) &&
            CropEdgesAreChromaAligned(prior.outwardPresentation, crop.rasterWidth, crop.rasterHeight) &&
            ContainedBounds(prior.outwardPresentation, crop.geometry) &&
            !SameBounds(prior.outwardPresentation, crop.geometry) &&
            input.measurementCurrent && input.retentionEvaluated &&
            input.retentionSourceGeneration == crop.frameSourceGeneration &&
            input.retentionSourceSequence == crop.frameSourceSequence &&
            SameTrustedCropContract(input.retentionBounds, crop.geometry) &&
            !input.excludedBandsPixelSafe && input.nearBlackEvaluated && !input.globalNearBlack;
        // Episode full-raster recovery has its own saved contract. Do not stack
        // another dwell on it, but preserve a pre-existing general withdrawal.
        // Repeated frames may preserve an existing FIT; they never earn votes.
        if (!result.state.active && currentContract &&
            (inspectionReentry || (!input.cadenceRepeat &&
             !input.candidate.applyCrop && !crop.nearBlackEpisodeFullRaster &&
             !crop.movingPictureTransition &&
             (input.candidate.withdrawalCause == WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED ||
              crop.barCropRefinementHorizontalConflict || crop.presentationFailOpen))))
		{
			result.state.active = true;
			result.state.trustedCrop = crop.geometry;
			result.state.sourceGeneration = crop.frameSourceGeneration;
			result.state.presentationEpoch = input.presentationEpoch;
			result.state.startedSourceSequence = crop.frameSourceSequence;
			result.state.startedTick = input.currentTick;
            if (inspectionReentry)
            {
                result.state.inspectionReentryPending = true;
                result.state.fallbackBoundsAvailable = true;
                result.state.fallbackBounds = prior.outwardPresentation;
                result.gates |= RECOVERY_INSPECTION_REENTRY;
            }
			result.started = true;
		}
		if (!result.state.active) return result;

		// A current outward FIT already exposes every certified visible pixel.
		// Requiring the smaller logical crop's bands to be empty would deadlock
		// this presentation behind precisely the content it safely includes.
		const auto verifiedCandidate = Evaluate(crop);
		const bool certifiedFit = currentContract && !input.cadenceRepeat &&
			input.measurementCurrent && input.retentionEvaluated &&
			input.retentionSourceGeneration == crop.frameSourceGeneration &&
			input.retentionSourceSequence == crop.frameSourceSequence &&
			SameTrustedCropContract(input.retentionBounds, crop.geometry) &&
			input.nearBlackEvaluated && !input.globalNearBlack &&
			input.candidate.applyCrop && verifiedCandidate.applyCrop &&
			verifiedCandidate.horizontalExpansionPixelBounded &&
			verifiedCandidate.owner == DecisionOwner::OUTWARD_FIT &&
			// Horizontal proof composed with a pending vertical owner is not a
			// certificate that every vertical pixel fits inside this rectangle.
			ContainedBounds(verifiedCandidate.sourceBounds, crop.currentVisibleBounds) &&
			SameBounds(verifiedCandidate.sourceBounds, input.candidate.sourceBounds);
		if (certifiedFit)
		{
			result.presentation = verifiedCandidate;
			result.presentation.reason = "current pixel-certified outward fit resolved crop recovery";
			result.released = result.ended = true;
			result.state = {};
			return result;
		}

        const bool inspectionOriginalContract =
            SameTrustedCropContract(crop.geometry, result.state.trustedCrop);
        bool fallbackRevalidatedForContract = false;
		if (!currentContract || !inspectionOriginalContract)
		{
            // Reset logical crop proof, but keep a wider presentation when THIS
            // frame proves every pixel outside the new, contained crop is safe.
            // A contract change alone must not enlarge a safe envelope to raster.
            const auto& held = result.state.fallbackBounds;
            fallbackRevalidatedForContract = currentContract &&
                result.state.fallbackBoundsAvailable &&
                crop.frameSourceGeneration != 0 && !input.cadenceRepeat &&
                crop.frameSourceSequence != 0 && crop.frameSourceSequence > result.state.lastSourceSequence &&
                !crop.movingPictureTransition && !crop.nearBlackEpisodeFullRaster &&
                input.measurementCurrent && input.retentionEvaluated &&
                input.retentionSourceGeneration == crop.frameSourceGeneration &&
                input.retentionSourceSequence == crop.frameSourceSequence &&
                SameTrustedCropContract(input.retentionBounds, crop.geometry) &&
                input.excludedBandsPixelSafe && input.nearBlackEvaluated && !input.globalNearBlack &&
                input.observationAvailable && !crop.latestObservationIsUnavailable && !crop.latestObservationIsProvisional &&
                input.observationClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
                crop.latestObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
                SameTrustedCropContract(input.observedTrustedCrop, crop.geometry) &&
                ValidBounds(input.observation, crop.rasterWidth, crop.rasterHeight) &&
                ContainedBounds(crop.geometry, input.observation) &&
                input.candidate.applyCrop && verifiedCandidate.applyCrop &&
                SameBounds(verifiedCandidate.sourceBounds, input.candidate.sourceBounds) &&
                ValidBounds(held, crop.rasterWidth, crop.rasterHeight) &&
                CropEdgesAreChromaAligned(held, crop.rasterWidth, crop.rasterHeight) &&
                CropEdgesAreChromaAligned(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
                ContainedBounds(held, crop.geometry) && ContainedBounds(held, input.observation);
			result.gates |= RECOVERY_CONTRACT;
            // Even repeats/older observations must not carry votes to a new
            // contract; the normal per-frame counting branch skips repeats.
            result.proofReset = result.state.samples != 0;
            result.state.samples = 0;
			result.state.fallbackBoundsAvailable = fallbackRevalidatedForContract;
			if (currentContract) result.state.trustedCrop = crop.geometry;
		}
		if (!input.measurementCurrent || !input.retentionEvaluated ||
			input.retentionSourceGeneration != crop.frameSourceGeneration ||
			input.retentionSourceSequence != crop.frameSourceSequence ||
			!SameBounds(input.retentionBounds, result.state.trustedCrop))
			result.gates |= RECOVERY_MEASUREMENT;
		const bool currentRetentionMeasurement = input.measurementCurrent && input.retentionEvaluated &&
			input.retentionSourceGeneration == crop.frameSourceGeneration &&
			input.retentionSourceSequence == crop.frameSourceSequence &&
			SameTrustedCropContract(input.retentionBounds, result.state.trustedCrop);
		const bool trustedSampling = input.observationClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			crop.latestObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
			ContainedBounds(input.observedTrustedCrop, input.observation) &&
			IsPixelSafeCropReaffirmation(result.state.trustedCrop,
				input.observedTrustedCrop, input.excludedBandsPixelSafe);
		const bool provisionalSampling =
			crop.latestObservationClassification == input.observationClassification &&
			crop.frameLocalPresentationRetentionEvaluated && input.excludedBandsPixelSafe &&
			CanRetainProvisionalSamplingCrop(result.state.trustedCrop,
				input.observation, input.observationClassification,
				crop.frameLocalPresentationRetentionSafe, crop.frameLocalPartialSamplingReaffirmed);
		result.samplingReaffirmed = currentRetentionMeasurement && input.observationAvailable &&
			(trustedSampling || provisionalSampling);
        // Inspection already permits a current proven picture to resolve
        // immediately. Preserve that timing for native and pixel-safe sampling
        // owners; do not add general recovery dwell to this short bridge.
        if (result.state.inspectionReentryPending &&
            (!inspectionOriginalContract || input.candidate.owner != DecisionOwner::VERTICAL_INSPECTION))
        {
            const bool nativeResolution = trustedSampling && crop.latestObservationSupportsCrop &&
                !crop.latestObservationIsProvisional &&
                input.candidate.owner == DecisionOwner::TRUSTED_CROP &&
                verifiedCandidate.owner == DecisionOwner::TRUSTED_CROP;
            const bool samplingResolution = provisionalSampling &&
                crop.latestObservationIsProvisional &&
                input.candidate.owner == DecisionOwner::PIXEL_SAFE_RETENTION &&
                verifiedCandidate.owner == DecisionOwner::PIXEL_SAFE_RETENTION;
            const bool currentResolution = currentContract && inspectionOriginalContract &&
                currentRetentionMeasurement && input.observationAvailable &&
                input.nearBlackEvaluated && !input.globalNearBlack &&
                !crop.movingPictureTransition && !crop.nearBlackEpisodeFullRaster &&
                !crop.presentationFailOpen && !crop.latestObservationIsUnavailable &&
                input.candidate.applyCrop && verifiedCandidate.applyCrop &&
                (nativeResolution || samplingResolution) &&
                SameBounds(input.candidate.sourceBounds, crop.geometry) &&
                SameBounds(verifiedCandidate.sourceBounds, crop.geometry);
            if (currentResolution && !input.cadenceRepeat &&
                crop.frameSourceSequence > result.state.startedSourceSequence &&
                crop.frameSourceSequence > result.state.lastSourceSequence)
            {
                result.released = result.ended = true;
                result.state = {};
                result.presentation.reason = "current pixel-safe crop resolved outward fit inspection";
                return result;
            }
            // An idempotent safe repeat neither earns a vote nor consumes the
            // shortcut for the next fresh frame. Any actual unresolved owner,
            // changed contract, expiry or backward frame ends the bridge.
            if (!currentResolution || crop.frameSourceSequence < result.state.lastSourceSequence)
                result.state.inspectionReentryPending = false;
        }
		if (!input.observationAvailable ||
			(!ContainedBounds(result.state.trustedCrop, input.observation) && !result.samplingReaffirmed))
			result.gates |= RECOVERY_OBSERVATION;
		if (input.measurementCurrent && input.retentionEvaluated &&
			input.retentionSourceGeneration == crop.frameSourceGeneration &&
			input.retentionSourceSequence == crop.frameSourceSequence &&
			!input.excludedBandsPixelSafe)
			result.gates |= RECOVERY_UNSAFE_BANDS;
		if (!input.nearBlackEvaluated || input.globalNearBlack)
			result.gates |= RECOVERY_NEAR_BLACK;
		if (crop.presentationFailOpen || crop.nearBlackEpisodeFullRaster || !input.candidate.applyCrop)
			result.gates |= RECOVERY_OWNER;

		const bool repeat = input.cadenceRepeat || crop.frameSourceSequence == 0 ||
			crop.frameSourceSequence <= result.state.lastSourceSequence;
		if (repeat)
			result.gates |= RECOVERY_REPEAT;
		else
		{
			const bool gap = result.state.samples != 0 &&
				crop.frameSourceSequence != result.state.lastSourceSequence + 1;
			result.proofReset = result.proofReset || (result.state.samples != 0 && (result.gates != 0 || gap));
			if (result.gates == 0)
				result.state.samples = gap ? 1 : result.state.samples + 1;
			else result.state.samples = 0;
			if (gap) result.gates |= RECOVERY_SEQUENCE_GAP;
			result.state.lastSourceSequence = crop.frameSourceSequence;
		}
		result.samples = result.state.samples;
		if (!repeat && result.state.samples >= result.required && input.candidate.applyCrop)
		{
			result.released = true;
			result.ended = true;
			result.state = {};
			return result;
		}
		// Recovery concerns the smaller logical crop, not necessarily every edge
		// of the raster. Current pixel extents can expose an unresolved expansion
		// without throwing away independently safe boundaries. This is geometry
		// evidence only: no subtitle/menu classification or new aspect authority.
		const bool currentBoundsProof = currentContract && currentRetentionMeasurement &&
			crop.frameSourceGeneration != 0 && crop.frameSourceSequence != 0 && !crop.movingPictureTransition &&
			!crop.nearBlackEpisodeFullRaster &&
			input.nearBlackEvaluated && !input.globalNearBlack &&
			input.observationAvailable && !crop.latestObservationIsUnavailable &&
			(input.observationClassification == ActivePictureClassification::PROVISIONAL ||
			 input.observationClassification == ActivePictureClassification::BAR_CROP_TRUSTED) &&
			crop.latestObservationClassification == input.observationClassification &&
			ValidBounds(input.observation, crop.rasterWidth, crop.rasterHeight) &&
			CropEdgesAreChromaAligned(crop.geometry, crop.rasterWidth, crop.rasterHeight);
		auto fallback = FullRaster(crop.rasterWidth, crop.rasterHeight);
		if (currentBoundsProof && crop.currentVisibleBoundsAvailable &&
			crop.currentVisibleSourceGeneration == crop.frameSourceGeneration &&
			crop.currentVisibleSourceSequence == crop.frameSourceSequence &&
			SameTrustedCropContract(crop.currentVisibleBase, crop.geometry) &&
			ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight) &&
			ContainedBounds(crop.currentVisibleBounds, crop.geometry) &&
			ContainedBounds(crop.currentVisibleBounds, input.observation))
		{
			fallback = crop.currentVisibleBounds;
			// Round outward for chroma, never cut off an odd measured edge.
			fallback.left &= ~1;
			fallback.top &= ~1;
			fallback.right = std::min(crop.rasterWidth, (fallback.right + 1) & ~1);
			fallback.bottom = std::min(crop.rasterHeight, (fallback.bottom + 1) & ~1);
		}
		else if (currentBoundsProof && input.excludedBandsPixelSafe &&
			result.state.fallbackBoundsAvailable &&
			ContainedBounds(result.state.fallbackBounds, input.observation))
		{
			// Bars are clear again. Keep the outward envelope until the existing
			// adjacent-source recovery proof permits the inward return.
			fallback = result.state.fallbackBounds;
		}
		if (result.state.fallbackBoundsAvailable)
		{
			// Never pump inward/outward with small extent changes during one
			// unresolved episode. Larger real content is exposed immediately.
			const auto& held = result.state.fallbackBounds;
			fallback.left = std::min(fallback.left, held.left);
			fallback.top = std::min(fallback.top, held.top);
			fallback.right = std::max(fallback.right, held.right);
			fallback.bottom = std::max(fallback.bottom, held.bottom);
		}
		fallback.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
		fallback.aspectRatio = double(fallback.right - fallback.left) /
			std::max(1, fallback.bottom - fallback.top);
		result.state.fallbackBoundsAvailable = true;
		result.state.fallbackBounds = fallback;
		result.presentation = {};
		result.presentation.sourceBounds = fallback;
		result.boundedPresentation = fallback.left > 0 || fallback.top > 0 ||
			fallback.right < crop.rasterWidth || fallback.bottom < crop.rasterHeight;
		result.presentation.applyCrop = result.boundedPresentation;
		result.presentation.outwardExpanded = result.boundedPresentation;
		result.presentation.owner = result.boundedPresentation
			? DecisionOwner::OUTWARD_FIT : DecisionOwner::FULL_RASTER;
		result.presentation.reason = result.boundedPresentation
			? (fallbackRevalidatedForContract
                ? "current pixel-safe envelope retained across trusted crop change"
                : "current pixel-bounded expansion retained pending inward crop recovery proof")
			: "full raster retained pending current crop recovery proof";
		return result;
	}


	CaptionBoundedFitAcquisitionDecision BuildCaptionBoundedFitAcquisition(
		const CaptionBoundedFitAcquisitionInput& input)
	{
		CaptionBoundedFitAcquisitionDecision result;
		if (!input.crop || !input.initialAcquisition ||
			!input.translationProposed || !input.captionConfirmed)
			return result;
		const Input& crop = *input.crop;
		if (!crop.automaticCropEnabled ||
			crop.nearBlackEpisodeFullRaster || crop.nearBlackEpisodeRetainCrop ||
			crop.presentationFailOpen || crop.fullRasterPresentationAuthoritative ||
			!crop.sharedGeometryAvailable ||
			!crop.latestObservationSupportsCrop ||
			crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
			crop.outwardPresentationActive || crop.outwardExpansionAvailable ||
			crop.frameSourceGeneration == 0 || crop.frameSourceSequence == 0 ||
			crop.geometrySourceGeneration != crop.frameSourceGeneration ||
			input.captionSourceGeneration != crop.frameSourceGeneration ||
			input.captionSourceSequence != crop.frameSourceSequence)
		{
			result.reason = "caption and trusted picture lack current acquisition authority";
			return result;
		}
		const ActivePictureBounds& picture = crop.geometry;
		const ActivePictureBounds& caption = input.captionBounds;
		if (!ValidBounds(picture, crop.rasterWidth, crop.rasterHeight) ||
			!ValidBounds(caption, crop.rasterWidth, crop.rasterHeight) ||
			!CropEdgesAreChromaAligned(picture, crop.rasterWidth, crop.rasterHeight) ||
			picture.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
			caption.left < picture.left || caption.right > picture.right ||
			caption.right <= picture.left || caption.left >= picture.right)
		{
			result.reason = "caption is not bounded by the trusted vertical picture";
			return result;
		}
		const bool crossesTop = caption.top < picture.top;
		const bool crossesBottom = caption.bottom > picture.bottom;
		if (crossesTop == crossesBottom)
		{
			result.reason = "caption does not extend one trusted vertical edge";
			return result;
		}
		// The current visible scan is a veto only. It cannot create a crop or
		// enlarge the caption proof. Require its identity and exact base first.
		if (!crop.currentVisibleBoundsAvailable ||
			crop.currentVisibleSourceGeneration != crop.frameSourceGeneration ||
			crop.currentVisibleSourceSequence != crop.frameSourceSequence ||
			!SameTrustedCropContract(crop.currentVisibleBase, picture) ||
			!ValidBounds(crop.currentVisibleBounds,
				crop.rasterWidth, crop.rasterHeight) ||
			!ContainedBounds(crop.currentVisibleBounds, picture))
		{
			result.reason = "current visible witness is unavailable or unpaired";
			return result;
		}
		ActivePictureBounds envelope = picture;
		if (crossesTop)
			envelope.top = std::max(0, (caption.top - 2) & ~1);
		else
			envelope.bottom = std::min(crop.rasterHeight,
				(caption.bottom + 3) & ~1);
		envelope.aspectRatio = static_cast<double>(
			envelope.right - envelope.left) /
			std::max(1, envelope.bottom - envelope.top);
		envelope.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
		if (!ValidBounds(envelope, crop.rasterWidth, crop.rasterHeight) ||
			!CropEdgesAreChromaAligned(envelope,
				crop.rasterWidth, crop.rasterHeight) ||
			!ContainedBounds(envelope, picture) ||
			!ContainedBounds(envelope, caption) ||
			!ContainedBounds(envelope, crop.currentVisibleBounds) ||
			(envelope.top == 0 && envelope.bottom == crop.rasterHeight))
		{
			result.reason = "caption envelope excludes current visible pixels";
			return result;
		}
		result.available = true;
		result.bounds = envelope;
		result.reason = "current confirmed caption bounds trusted picture";
		return result;
	}

    // Optional screen fill changes the displayed raster, not the proved physical
    // picture. Accept that difference only from the selected source's immutable
    // admission packet; a live/newer packet cannot certify delayed presentation.
    static bool HasSelectedHorizontalFillWitness(
        const CropPresentationAdmissionState& fill, const ActivePictureBounds& base,
        const ActivePictureBounds& actual, uint64_t generation, uint64_t sequence,
        uint64_t epoch)
    {
        return generation && sequence && fill.available && fill.optionalFillApplied &&
            fill.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            fill.sourceGeneration == generation && fill.presentationEpoch == epoch &&
            fill.optionalFillSourceSequence == sequence &&
            SameTrustedCropContract(fill.trustedCrop, base) &&
            SameBounds(fill.optionalFillSourceBounds, actual) &&
            ValidBounds(actual, base.rasterWidth, base.rasterHeight) &&
            CropEdgesAreChromaAligned(actual, base.rasterWidth, base.rasterHeight) &&
            actual.top == base.top && actual.bottom == base.bottom &&
            actual.left >= base.left && actual.right <= base.right;
    }

    ColdMaskedCaptionPresentationDecision BuildColdMaskedCaptionPresentation(
        const ColdMaskedCaptionPresentationInput& input)
    {
        ColdMaskedCaptionPresentationDecision result;
        result.episodeState = input.episode;
        if (!input.crop) return result;
        const auto& crop = *input.crop;
        const uint64_t generation = crop.frameSourceGeneration;
        const uint64_t sequence = crop.frameSourceSequence;
        const auto& picture = input.strictPicture;
        const auto& caption = input.captionBounds;
        // Reuse only an envelope that was actually presented in this episode.
        // An identical decoded source may repeat indefinitely without a new vote.
        // Each advancing source needs a fresh current-pixel safety witness and
        // must follow the last displayed source, so a worker gap cannot renew authority.
        const auto& held = input.episode.boundedPresentation;
        const bool sameEpisode = generation && sequence &&
            input.episode.mode == NearBlackPresentationMode::FULL_RASTER &&
            input.episode.sourceGeneration == generation &&
            input.episode.presentationEpoch == crop.framePresentationEpoch &&
            input.episode.boundedPresentationAvailable &&
            input.episode.boundedPresentationSequence >=
                input.episode.fullRasterStartedSourceSequence &&
            input.episode.fullRasterStartedSourceSequence != 0;
        const bool currentObservation = input.currentObservationAvailable &&
            input.observationSourceGeneration == generation &&
            input.observationSourceSequence == sequence &&
            ValidBounds(input.currentObservation, crop.rasterWidth, crop.rasterHeight);
        const bool currentRetention = input.currentRetentionEvaluated &&
            input.retentionSourceGeneration == generation &&
            input.retentionSourceSequence == sequence &&
            SameTrustedCropContract(input.retentionBase, crop.geometry);
        const bool pairedVisible = crop.currentVisibleBoundsAvailable &&
            crop.currentVisibleSourceGeneration == generation &&
            crop.currentVisibleSourceSequence == sequence &&
            SameTrustedCropContract(crop.currentVisibleBase, crop.geometry) &&
            ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight) &&
            ContainedBounds(held, crop.currentVisibleBounds);
        const bool heldAdmitted = input.previousAdmission.available &&
            input.previousAdmission.sourceGeneration == generation &&
            input.previousAdmission.presentationEpoch == crop.framePresentationEpoch &&
            SameTrustedCropContract(input.previousAdmission.trustedCrop, crop.geometry);
        // The caption has disappeared. Reuse the already admitted physical base
        // only when this exact native sample finds its excluded bands clean.
        // This releases presentation ownership; it establishes no new picture.
        const bool cleanNativeBase = sameEpisode && heldAdmitted &&
            !input.sceneBoundary && !input.captionMeasured &&
            input.captionAbsenceConfirmed &&
            input.currentNativeTrusted && currentObservation && currentRetention &&
            input.currentRetentionSafe && input.currentExcludedBandsPixelSafe &&
            !input.currentOutwardVisible &&
            crop.automaticCropEnabled && crop.nearBlackEpisodeFullRaster &&
            !crop.presentationFailOpen && !crop.fullRasterPresentationAuthoritative &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active &&
            crop.sharedGeometryAvailable && crop.geometrySourceGeneration == generation &&
            crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            ValidBounds(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
            CropEdgesAreChromaAligned(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
            crop.geometry.left == 0 && crop.geometry.right == crop.rasterWidth &&
            crop.geometry.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            ValidBounds(input.currentNativeBounds, crop.rasterWidth, crop.rasterHeight) &&
            input.currentNativeBounds.left == 0 &&
            input.currentNativeBounds.right == crop.rasterWidth &&
            input.currentNativeBounds.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            ContainedBounds(crop.geometry, input.currentNativeBounds) &&
            ContainedBounds(crop.geometry, input.currentObservation) &&
            (!crop.currentVisibleBoundsAvailable ||
             (crop.currentVisibleSourceGeneration == generation &&
              crop.currentVisibleSourceSequence == sequence &&
              SameTrustedCropContract(crop.currentVisibleBase, crop.geometry) &&
              ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight) &&
              ContainedBounds(crop.geometry, crop.currentVisibleBounds)));
        // A caption-free frame is evidence, not a presentation handoff. Count
        // only distinct consecutive decoded sources against the legacy 250 ms
        // revalidation dwell; repeats preserve a vote without adding one.
        if (cleanNativeBase && !input.cadenceRepeat &&
            sequence > result.episodeState.captionCleanLastSourceSequence)
        {
            const bool continues = result.episodeState.captionCleanSamples &&
                result.episodeState.captionCleanLastSourceSequence + 1 == sequence &&
                SameTrustedCropContract(result.episodeState.captionCleanBase, crop.geometry);
            result.episodeState.captionCleanSamples = continues
                ? std::min(NearBlackCropRevalidationSamples(input.framesPerSecond),
                    result.episodeState.captionCleanSamples + 1) : 1;
            result.episodeState.captionCleanLastSourceSequence = sequence;
            result.episodeState.captionCleanBase = crop.geometry;
        }
        else if (!cleanNativeBase && !input.cadenceRepeat &&
                 sequence > result.episodeState.captionCleanLastSourceSequence)
        {
            result.episodeState.captionCleanSamples = 0;
            result.episodeState.captionCleanLastSourceSequence = 0;
            result.episodeState.captionCleanBase = {};
        }
        // Reuse the episode's existing exact-entry seven-source proof when a
        // dark Scope has no outward pixels. Unlike caption disappearance, this
        // native-only certificate works even if the subtitle worker is pending.
        const bool nativeCleanEntry = (input.episode.mode == NearBlackPresentationMode::FULL_RASTER ||
                input.episode.mode == NearBlackPresentationMode::RETAIN_CROP) &&
            input.episode.sourceGeneration == generation &&
            input.episode.presentationEpoch == crop.framePresentationEpoch &&
            input.episode.revalidationNativeDarkRun &&
            input.episode.revalidationSamples >=
                NearBlackCropRevalidationSamples(input.framesPerSecond) &&
            input.episode.revalidationLastSourceSequence == sequence &&
            input.episode.entryTrustedCropAvailable &&
            input.episode.entryTrustedCropOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            SameTrustedCropContract(input.episode.entryTrustedCrop, crop.geometry) &&
            currentObservation && currentRetention &&
            input.currentExcludedBandsPixelSafe && !input.currentOutwardVisible &&
            !input.sceneBoundary && crop.automaticCropEnabled &&
            ((input.episode.mode == NearBlackPresentationMode::FULL_RASTER && crop.nearBlackEpisodeFullRaster) ||
             (input.episode.mode == NearBlackPresentationMode::RETAIN_CROP && crop.nearBlackEpisodeRetainCrop)) &&
            !crop.presentationFailOpen && !crop.fullRasterPresentationAuthoritative &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active &&
            crop.sharedGeometryAvailable &&
            crop.geometrySourceGeneration == generation &&
            crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            ValidBounds(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
            CropEdgesAreChromaAligned(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
            crop.geometry.left == 0 && crop.geometry.right == crop.rasterWidth &&
            crop.geometry.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            ContainedBounds(crop.geometry, input.currentObservation);
        if (nativeCleanEntry)
        {
            result.available = true;
            result.releasedToBase = true;
            result.nativeCleanBase = true;
            result.presentation.sourceBounds = crop.geometry;
            result.presentation.applyCrop = true;
            result.presentation.owner = DecisionOwner::TRUSTED_CROP;
            result.presentation.reason = "seven current native clean Scope sources nominate physical picture";
            result.reason = "dark native Scope awaits selected safe presentation";
            return result;
        }
        if (cleanNativeBase && result.episodeState.captionCleanSamples >=
            NearBlackCropRevalidationSamples(input.framesPerSecond))
        {
            result.available = true;
            result.releasedToBase = true;
            result.presentation.sourceBounds = crop.geometry;
            result.presentation.applyCrop = true;
            result.presentation.owner = DecisionOwner::TRUSTED_CROP;
            result.presentation.reason = "current clean native bars restore admitted physical picture";
            result.reason = "current native pixel-safe return to presented picture base";
            return result;
        }
        const bool identicalSource = input.cadenceRepeat &&
            sequence == input.episode.boundedPresentationPresentedSequence;
        const bool boundedGap = !input.cadenceRepeat &&
            sequence == input.episode.boundedPresentationPresentedSequence + 1;
        if (sameEpisode && heldAdmitted && (!input.strictProofCurrent || input.cadenceRepeat) &&
            !input.sceneBoundary &&
            crop.automaticCropEnabled && crop.nearBlackEpisodeFullRaster &&
            !crop.presentationFailOpen && !crop.fullRasterPresentationAuthoritative &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active &&
            crop.sharedGeometryAvailable &&
            crop.geometrySourceGeneration == generation &&
            crop.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            ValidBounds(crop.geometry, crop.rasterWidth, crop.rasterHeight) &&
            ValidBounds(held, crop.rasterWidth, crop.rasterHeight) &&
            CropEdgesAreChromaAligned(held, crop.rasterWidth, crop.rasterHeight) &&
            ContainedBounds(held, crop.geometry) &&
            currentObservation && currentRetention &&
            ContainedBounds(held, input.currentObservation) &&
            ((identicalSource && (input.currentRetentionSafe || pairedVisible)) ||
             (boundedGap && (input.currentRetentionSafe || pairedVisible))))
        {
            result.available = true;
            result.held = true;
            result.presentation.sourceBounds = held;
            result.presentation.applyCrop = true;
            result.presentation.outwardExpanded = true;
            result.presentation.owner = DecisionOwner::OUTWARD_FIT;
            result.presentation.reason = "presented dark caption envelope retained by current pixels";
            result.reason = "same episode and current visible pixels preserve admitted envelope";
            return result;
        }
        if (!generation || !sequence ||
            (input.episode.mode != NearBlackPresentationMode::FULL_RASTER &&
             input.episode.mode != NearBlackPresentationMode::RETAIN_CROP) ||
            input.episode.sourceGeneration != generation ||
            input.episode.presentationEpoch != crop.framePresentationEpoch ||
            input.sceneBoundary || input.cadenceRepeat ||
            !input.strictProofCurrent || !input.captionConfirmed ||
            input.proofSourceGeneration != generation ||
            input.proofSourceSequence != sequence ||
            input.proofPresentationEpoch != crop.framePresentationEpoch ||
            input.captionSourceGeneration != generation ||
            input.captionSourceSequence != sequence ||
            input.observationSourceGeneration != generation ||
            input.observationSourceSequence != sequence ||
            !input.currentRetentionEvaluated ||
            input.retentionSourceGeneration != generation ||
            input.retentionSourceSequence != sequence ||
            !SameTrustedCropContract(input.retentionBase, crop.geometry) ||
            !crop.automaticCropEnabled ||
            !(crop.nearBlackEpisodeFullRaster || crop.nearBlackEpisodeRetainCrop) ||
            (input.episode.mode == NearBlackPresentationMode::RETAIN_CROP &&
             (!input.episode.entryTrustedCropAvailable ||
              !input.previousAdmission.available ||
              input.previousAdmission.sourceGeneration != generation ||
              input.previousAdmission.presentationEpoch != crop.framePresentationEpoch ||
              !SameTrustedCropContract(input.previousAdmission.trustedCrop,
                  input.episode.entryTrustedCrop))) ||
            !crop.sharedGeometryAvailable ||
            !crop.latestObservationSupportsCrop ||
            crop.geometrySourceGeneration != generation ||
            crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
            crop.presentationFailOpen || crop.fullRasterPresentationAuthoritative ||
            crop.movingPictureTransition || crop.pictureTransitionHandoff.active ||
            !SameTrustedCropContract(picture, crop.geometry))
        {
            result.reason = "dark caption proof or source authority is stale or competing";
            return result;
        }
        if (!ValidBounds(picture, crop.rasterWidth, crop.rasterHeight) ||
            !CropEdgesAreChromaAligned(picture, crop.rasterWidth, crop.rasterHeight) ||
            picture.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
            picture.left != 0 || picture.right != crop.rasterWidth ||
            !ValidBounds(caption, crop.rasterWidth, crop.rasterHeight) ||
            caption.left < picture.left || caption.right > picture.right ||
            !input.currentObservationAvailable ||
            !ValidBounds(input.currentObservation, crop.rasterWidth, crop.rasterHeight))
        {
            result.reason = "guarded full-width picture, caption, or current observation is invalid";
            return result;
        }
        const bool above = caption.top < picture.top;
        const bool below = caption.bottom > picture.bottom;
        if (above == below)
        {
            result.reason = "caption must extend exactly one physical picture edge";
            return result;
        }
        ActivePictureBounds envelope = picture;
        if (above) envelope.top = std::max(0, (caption.top - 2) & ~1);
        else envelope.bottom = std::min(crop.rasterHeight, (caption.bottom + 3) & ~1);
        // The current retention witness can extend a pixel beyond measured
        // caption ink. Include it before chroma alignment rather than rejecting
        // a safe complete envelope or silently cropping that pixel.
        if (crop.currentVisibleBoundsAvailable &&
            crop.currentVisibleSourceGeneration == generation &&
            crop.currentVisibleSourceSequence == sequence &&
            SameTrustedCropContract(crop.currentVisibleBase, picture) &&
            ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight))
        {
            envelope.top = std::max(0, std::min(envelope.top,
                crop.currentVisibleBounds.top) & ~1);
            envelope.bottom = std::min(crop.rasterHeight,
                (std::max(envelope.bottom, crop.currentVisibleBounds.bottom) + 1) & ~1);
        }
        envelope.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
        envelope.aspectRatio = double(envelope.right - envelope.left) /
            std::max(1, envelope.bottom - envelope.top);
        if (!ValidBounds(envelope, crop.rasterWidth, crop.rasterHeight) ||
            !CropEdgesAreChromaAligned(envelope, crop.rasterWidth, crop.rasterHeight) ||
            !ContainedBounds(envelope, picture) ||
            !ContainedBounds(envelope, caption) ||
            !ContainedBounds(envelope, input.currentObservation) ||
            SameBounds(envelope, FullRaster(crop.rasterWidth, crop.rasterHeight)))
        {
            result.reason = "bounded crop excludes current picture, caption, or observed pixels";
            return result;
        }
        if (!crop.currentVisibleBoundsAvailable ||
            crop.currentVisibleSourceGeneration != generation ||
             crop.currentVisibleSourceSequence != sequence ||
             !SameTrustedCropContract(crop.currentVisibleBase, picture) ||
             !ValidBounds(crop.currentVisibleBounds, crop.rasterWidth, crop.rasterHeight) ||
             !ContainedBounds(envelope, crop.currentVisibleBounds))
        {
            result.reason = "bounded crop excludes current visible witness";
            return result;
        }
        result.available = true;
        result.rebasedFromRetainedCrop =
            input.episode.mode == NearBlackPresentationMode::RETAIN_CROP;
        result.presentation.sourceBounds = envelope;
        result.presentation.applyCrop = true;
        result.presentation.outwardExpanded = true;
        result.presentation.owner = DecisionOwner::OUTWARD_FIT;
        result.presentation.reason = "exact dark native picture and caption bounded together";
        result.reason = "current guarded native picture and caption envelope";
        return result;
    }
    Decision RetainColdTranslatedPresentation(
        const ColdMaskedCaptionPresentationDecision& bounded,
        const NearBlackPresentationEpisodeInput& episode,
        const CropPresentationAdmissionState& previousAdmission,
        const EstablishedSubtitleTranslationInput& translation,
        const Input& translatedCrop)
    {
        const auto& anchor = episode.previous;
        if (!bounded.available || !bounded.held ||
            !anchor.presentedTranslationAvailable ||
            anchor.mode != NearBlackPresentationMode::FULL_RASTER ||
            !anchor.boundedPresentationAvailable ||
            !episode.sourceGeneration || !episode.sourceSequence ||
            episode.sourceGeneration != translatedCrop.frameSourceGeneration ||
            episode.sourceSequence != translatedCrop.frameSourceSequence ||
            episode.presentationEpoch != translatedCrop.framePresentationEpoch ||
            !SameTrustedCropContract(anchor.entryTrustedCrop,
                translatedCrop.geometry) ||
            !translatedCrop.verticalTranslationActive ||
            translatedCrop.verticalTranslationSourceGeneration !=
                episode.sourceGeneration ||
            translatedCrop.verticalTranslationPixels !=
                translation.resolvedTranslationPixels ||
            !CanRetainEstablishedScopeForSubtitle(
                episode, previousAdmission, translation))
            return bounded.presentation;
        auto current = translatedCrop;
        current.nearBlackEpisodeFullRaster = false;
        current.nearBlackEpisodeRetainCrop = false;
        const auto candidate = Evaluate(current);
        if (!candidate.applyCrop ||
            candidate.owner != DecisionOwner::VERTICAL_TRANSLATION ||
            !candidate.verticallyTranslated ||
            candidate.verticalTranslationPixels !=
                translation.resolvedTranslationPixels)
            return bounded.presentation;
        // A settled owner cannot invent a new pose from a held caption. An
        // engaging/releasing drift may move only inside the interval certified
        // by CanRetainEstablishedScopeForSubtitle on this exact source.
        const bool certifiedDrift = translation.engagingTranslation ||
            (translation.presentation.action == VerticalBarPresentationAction::NONE &&
             translation.releaseIntervalAvailable);
        if (!certifiedDrift &&
            (candidate.verticalTranslationPixels !=
                anchor.presentedTranslationPixels ||
             !SameBounds(candidate.sourceBounds,
                anchor.presentedTranslationBounds)))
            return bounded.presentation;
        return candidate;
    }

    ColdMaskedCaptionCommitDecision CommitColdMaskedCaptionPresentation(
        const ColdMaskedCaptionCommitInput& input)
    {
        ColdMaskedCaptionCommitDecision result;
        result.state = input.currentEpisode;
        const auto action = input.action;
        if (!input.renderedAndSubmitted || action == ColdMaskedCaptionCommitAction::NONE ||
            !input.sourceGeneration || !input.selectedSourceSequence ||
            input.selectedSourceSequence > input.currentSourceSequence ||
            !input.proposalStartSequence ||
            input.currentEpisode.sourceGeneration != input.sourceGeneration ||
            input.currentEpisode.presentationEpoch != input.presentationEpoch ||
            input.currentEpisode.mode != input.proposalMode ||
            !input.currentAdmission.available ||
            input.currentAdmission.sourceGeneration != input.sourceGeneration ||
            input.currentAdmission.presentationEpoch != input.presentationEpoch ||
            !SameTrustedCropContract(input.currentAdmission.trustedCrop,
                input.selectedBase) ||
            !ValidBounds(input.selectedBase, input.selectedBase.rasterWidth,
                input.selectedBase.rasterHeight) ||
            !CropEdgesAreChromaAligned(input.selectedBase,
                input.selectedBase.rasterWidth, input.selectedBase.rasterHeight))
            return result;
        const bool fullEpisode =
            input.proposalMode == NearBlackPresentationMode::FULL_RASTER &&
            input.currentEpisode.fullRasterStartedSourceSequence ==
                input.proposalStartSequence;
        const bool retainedEpisode =
            input.proposalMode == NearBlackPresentationMode::RETAIN_CROP &&
            input.currentEpisode.startedSourceSequence ==
                input.proposalStartSequence &&
            input.proposalEntryCropAvailable &&
            input.currentEpisode.entryTrustedCropAvailable &&
            SameTrustedCropContract(input.currentEpisode.entryTrustedCrop,
                input.proposalEntryCrop);
        if (!fullEpisode && !retainedEpisode)
            return result;
        if (action == ColdMaskedCaptionCommitAction::CLEAN_BASE)
        {
            const bool captionClean =
                input.currentEpisode.captionCleanSamples >=
                    NearBlackCropRevalidationSamples(input.framesPerSecond) &&
                SameTrustedCropContract(input.currentEpisode.captionCleanBase,
                    input.selectedBase) &&
                input.currentEpisode.captionCleanLastSourceSequence >=
                    input.selectedSourceSequence;
            const bool nativeClean =
                input.currentEpisode.revalidationNativeDarkRun &&
                input.currentEpisode.revalidationSamples >=
                    NearBlackCropRevalidationSamples(input.framesPerSecond) &&
                input.currentEpisode.revalidationLastSourceSequence >=
                    input.selectedSourceSequence &&
                input.currentEpisode.entryTrustedCropAvailable &&
                input.currentEpisode.entryTrustedCropOrigin ==
                    ActivePictureAuthorityOrigin::NATIVE &&
                SameTrustedCropContract(input.currentEpisode.entryTrustedCrop,
                    input.selectedBase);
            // The selected packet may include the user's admitted horizontal
            // aspect-limit fill. It must carry its own exact fill witness; the
            // newest live admission cannot authorize a delayed selected output.
            const bool selectedHorizontalFill = HasSelectedHorizontalFillWitness(
                input.selectedAdmission, input.selectedBase, input.selectedActualBounds,
                input.sourceGeneration, input.selectedSourceSequence, input.presentationEpoch);
            if ((!captionClean && !nativeClean) ||
                input.selectedActualOwner != DecisionOwner::TRUSTED_CROP ||
                (!SameBounds(input.selectedActualBounds, input.selectedBase) && !selectedHorizontalFill))
                return result;
            NearBlackPresentationEpisodeState retained;
            retained.mode = NearBlackPresentationMode::RETAIN_CROP;
            retained.sourceGeneration = input.sourceGeneration;
            retained.presentationEpoch = input.presentationEpoch;
            retained.startedSourceSequence = input.selectedSourceSequence;
            retained.entryTrustedCropAvailable = true;
            retained.entryTrustedCrop = input.selectedBase;
            retained.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
            result.state = retained;
            result.committed = true;
            result.reason = "selected clean source completed source-rate base release";
            return result;
        }
        if (input.currentEpisode.boundedPresentationAvailable &&
            input.selectedSourceSequence <=
                input.currentEpisode.boundedPresentationPresentedSequence)
        {
            result.reason = "selected source already committed";
            return result;
        }
        if (action == ColdMaskedCaptionCommitAction::TRANSLATED &&
            (!ValidBounds(input.selectedTranslatedBounds,
                input.selectedBase.rasterWidth, input.selectedBase.rasterHeight) ||
             !CropEdgesAreChromaAligned(input.selectedTranslatedBounds,
                input.selectedBase.rasterWidth, input.selectedBase.rasterHeight) ||
             !input.selectedTranslationPixels ||
             input.selectedTranslatedBounds.left != input.selectedBase.left ||
             input.selectedTranslatedBounds.right != input.selectedBase.right ||
             !((input.selectedTranslatedBounds.top ==
                    input.selectedBase.top + input.selectedTranslationPixels &&
                 input.selectedTranslatedBounds.bottom ==
                    input.selectedBase.bottom + input.selectedTranslationPixels) ||
                (input.selectedTranslatedBounds.top ==
                    input.selectedEnvelope.top + input.selectedTranslationPixels &&
                 input.selectedTranslatedBounds.bottom ==
                    input.selectedEnvelope.bottom + input.selectedTranslationPixels))))
            return result;
        if (!input.proofSourceSequence ||
            input.proofSourceSequence < input.proposalStartSequence ||
            input.proofSourceSequence > input.selectedSourceSequence ||
            !ValidBounds(input.selectedEnvelope,
                input.selectedBase.rasterWidth, input.selectedBase.rasterHeight) ||
            !CropEdgesAreChromaAligned(input.selectedEnvelope,
                input.selectedBase.rasterWidth, input.selectedBase.rasterHeight) ||
            !ContainedBounds(input.selectedEnvelope, input.selectedBase))
            return result;
        if (retainedEpisode)
        {
            // The old wide crop remains owner until this exact selected frame
            // submits. Rebase physical authority and visible envelope together.
            result.state.mode = NearBlackPresentationMode::FULL_RASTER;
            result.state.startedAtFullRaster = true;
            result.state.startedSourceSequence = input.proofSourceSequence;
            result.state.fullRasterStartedSourceSequence = input.proofSourceSequence;
            result.state.entryTrustedCropAvailable = true;
            result.state.entryTrustedCrop = input.selectedBase;
            result.state.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
        }
        const bool newCaptionAuthority = retainedEpisode ||
            !input.currentEpisode.boundedPresentationAvailable ||
            input.proofSourceSequence >
                input.currentEpisode.boundedPresentationSequence;
        if (action == ColdMaskedCaptionCommitAction::TRANSLATED)
        {
            result.state.entryTrustedCropAvailable = true;
            result.state.entryTrustedCrop = input.selectedBase;
            result.state.entryTrustedCropOrigin = ActivePictureAuthorityOrigin::NATIVE;
            result.state.presentedTranslationAvailable = true;
            result.state.presentedTranslationBounds = input.selectedTranslatedBounds;
            result.state.presentedTranslationPixels = input.selectedTranslationPixels;
            result.state.presentedTranslationSourceSequence =
                input.selectedSourceSequence;
        }
        else if (newCaptionAuthority)
        {
            result.state.presentedTranslationAvailable = false;
            result.state.presentedTranslationBounds = {};
            result.state.presentedTranslationPixels = 0;
            result.state.presentedTranslationSourceSequence = 0;
        }
        result.state.boundedPresentationAvailable = true;
        result.state.boundedPresentationFailed = false;
        result.state.boundedPresentation = input.selectedEnvelope;
        result.state.boundedPresentationSequence = input.proofSourceSequence;
        result.state.boundedPresentationPresentedSequence = input.selectedSourceSequence;
        if (newCaptionAuthority)
        {
            result.state.captionCleanSamples = 0;
            result.state.captionCleanLastSourceSequence = 0;
            result.state.captionCleanBase = {};
        }
        result.committed = true;
        result.reason = action == ColdMaskedCaptionCommitAction::TRANSLATED
            ? "selected translation kept a bounded fallback in the raw-dark episode"
            : retainedEpisode
                ? "selected bounded output atomically replaced retained wide crop"
                : "selected bounded output continued dark caption owner";
        return result;
    }

    CaptionConfirmationPresentationHoldDecision EvaluateCaptionConfirmationPresentationHold(
        const CaptionConfirmationPresentationHoldInput& input)
    {
        CaptionConfirmationPresentationHoldDecision result;
        const auto reject = [&](const char* reason) {
            result.reason = reason;
            return result; // Rejection never preserves or extends a prior lease.
        };
        if (!input.eligible || input.competingPresentation ||
            (!input.pendingConfirmation && !input.zeroEngage) ||
            !input.pictureCurrentTrusted || !input.captionCurrentMeasured ||
            !input.nearBlackEvaluated || input.nearBlack)
            return reject("confirmation hold lacks current eligibility");
        if (!input.sourceGeneration || !input.sourceSequence || !input.analysisDomainKey ||
            input.pictureGeneration != input.sourceGeneration || input.pictureSequence != input.sourceSequence ||
            input.captionGeneration != input.sourceGeneration || input.captionSequence != input.sourceSequence ||
            input.visibleGeneration != input.sourceGeneration || input.visibleSequence != input.sourceSequence ||
            input.retentionGeneration != input.sourceGeneration || input.retentionSequence != input.sourceSequence ||
            !SameTrustedCropContract(input.retentionBase, input.picture))
            return reject("confirmation hold evidence identity mismatch");
        const auto& picture = input.picture;
        const auto& caption = input.caption;
        const int width = picture.rasterWidth, height = picture.rasterHeight;
        if (!ValidBounds(picture, width, height) || !CropEdgesAreChromaAligned(picture, width, height) ||
            picture.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
            picture.left != 0 || picture.right != width || picture.top <= 0 || picture.bottom >= height ||
            !ValidBounds(caption, width, height) || caption.left < picture.left || caption.right > picture.right ||
            !ValidBounds(input.visibleBounds, width, height) || !ContainedBounds(input.visibleBounds, picture))
            return reject("confirmation hold geometry unavailable");
        const bool top = caption.top < picture.top, bottom = caption.bottom > picture.bottom;
        const double target = input.targetTranslationPixels;
        if (top == bottom || !std::isfinite(target) || std::abs(target) <= 0.5 ||
            (top != (target < 0)) || picture.top + target < 0 || picture.bottom + target > height)
            return reject("confirmation hold target does not match caption");
        // A conservative rectangular envelope, not a claim that every pixel
        // inside it is text. It must contain the entire current visible witness.
        auto envelope = picture;
        if (top) envelope.top = std::max(0, (caption.top - 2) & ~1);
        else envelope.bottom = std::min(height, (caption.bottom + 3) & ~1);
        if (!ContainedBounds(envelope, caption) || !ContainedBounds(envelope, input.visibleBounds))
            return reject("confirmation hold has unexplained visible content");

        const auto& rendered = input.lastRendered;
        if (!rendered.available || rendered.sourceGeneration != input.sourceGeneration ||
            rendered.analysisDomainKey != input.analysisDomainKey || rendered.sourceFormatKey != input.sourceFormatKey ||
            rendered.presentationEpoch != input.presentationEpoch ||
            !rendered.sourceSequence || rendered.sourceSequence > input.sourceSequence ||
            input.sourceSequence - rendered.sourceSequence > 1 || rendered.tick > input.currentTick ||
            input.currentTick - rendered.tick > 500 ||
            !ValidBounds(rendered.bounds, width, height) || !CropEdgesAreChromaAligned(rendered.bounds, width, height) ||
            !ContainedBounds(rendered.bounds, envelope))
            return reject("confirmation hold lacks continuous containing presentation");

        if (input.previous.active)
        {
            const auto& prior = input.previous;
            if (prior.anchor.sourceGeneration != input.sourceGeneration ||
                prior.anchor.analysisDomainKey != input.analysisDomainKey ||
                prior.anchor.sourceFormatKey != input.sourceFormatKey ||
                prior.anchor.presentationEpoch != input.presentationEpoch ||
                !SameBounds(prior.anchor.bounds, rendered.bounds) ||
                !SameTrustedCropContract(prior.picture, picture) ||
                std::abs(prior.targetTranslationPixels - target) > 0.5 ||
                !prior.lastSequence || input.sourceSequence < prior.lastSequence ||
                input.sourceSequence - prior.lastSequence > 1 ||
                (input.sourceSequence == prior.lastSequence && !input.cadenceRepeat) ||
                input.currentTick < prior.startedTick || input.currentTick - prior.startedTick >= 500)
                return reject("confirmation hold context changed or expired");
            result.state = prior;
        }
        else
        {
            if (input.cadenceRepeat || rendered.sourceSequence == input.sourceSequence)
                return reject("confirmation hold cannot originate on a repeated frame");
            result.state.active = true;
            result.state.anchor = rendered;
            result.state.picture = picture;
            result.state.firstSequence = input.sourceSequence;
            result.state.startedTick = input.currentTick;
            result.state.targetTranslationPixels = target;
        }
        result.state.lastSequence = input.sourceSequence;
        result.hold = true;
        result.bounds = result.state.anchor.bounds;
        result.reason = "last rendered presentation contains current picture and caption while target confirms";
        return result;
    }

    CropPresentationAdmissionDecision CompleteCaptionAcquisitionTranslation(
        const CaptionBoundedFitAcquisitionInput& acquisition,
        const CropPresentationAdmissionDecision& admittedFit,
        const EstablishedSubtitleTranslationInput& translation,
        uint64_t presentationEpoch, uint64_t currentTick, std::string* diagnosticReason)
    {
        const auto reject = [&](const char* reason) {
            if (diagnosticReason) *diagnosticReason = reason;
            return admittedFit;
        };
        const auto proof = BuildCaptionBoundedFitAcquisition(acquisition);
        if (!proof.available) return reject(proof.reason);
        if (admittedFit.blocked || !admittedFit.presentation.applyCrop ||
            admittedFit.presentation.owner != DecisionOwner::OUTWARD_FIT ||
            !admittedFit.presentation.outwardExpanded ||
            !SameBounds(admittedFit.presentation.sourceBounds, proof.bounds))
            return reject("admitted-fit-mismatch");
        const auto& crop = *acquisition.crop;
        const auto& owner = translation.presentation;
        if (!HasCurrentCropPresentationAdmission(admittedFit.state, crop, presentationEpoch))
            return reject("admission-contract-mismatch");
        if (!admittedFit.state.outwardPresentationAvailable ||
            admittedFit.state.presentationSourceSequence != crop.frameSourceSequence ||
            !SameBounds(admittedFit.state.outwardPresentation, proof.bounds))
            return reject("admission-envelope-mismatch");
        if (!crop.verticalTranslationActive) return reject("translation-inactive");
        if (crop.verticalTranslationConfirmationPending) return reject("translation-confirmation-pending");
        if (crop.verticalFitConfirmationPending) return reject("fit-confirmation-pending");
        if (crop.verticalInspectionPending) return reject("inspection-pending");
        if (crop.movingPictureTransition) return reject("moving-picture-transition");
        if (translation.competingPresentation) return reject("competing-presentation");
        if (translation.sourceGeneration != crop.frameSourceGeneration)
            return reject("translation-generation-mismatch");
        if (!SameBounds(translation.base, crop.geometry)) return reject("translation-base-mismatch");
        if (owner.action != VerticalBarPresentationAction::TRANSLATE) return reject("owner-not-translation");
        if (owner.sourceSequence == 0 || owner.sourceSequence > crop.frameSourceSequence)
            return reject("owner-sequence-invalid");
        if (!CanRetainVerticalBarPresentationAcrossAuthorityGap(owner, translation.sourceGeneration,
                crop.frameSourceGeneration, currentTick, translation.holdMs, crop.frameSourceSequence))
            return reject("owner-lifetime-expired");
        if (!std::isfinite(owner.translationPixels)) return reject("target-nonfinite");
        if (translation.resolvedTranslationPixels != crop.verticalTranslationPixels)
            return reject("routed-displacement-mismatch");

        const double target = owner.translationPixels;
        const double applied = crop.verticalTranslationPixels;
        if (std::abs(target) <= 0.5 || crop.geometry.top + target < 0 ||
            crop.geometry.bottom + target > crop.rasterHeight)
            return reject("target-outside-raster");
        // The confirmed target must cover this caption, not an old/opposite cue.
        auto covered = crop.geometry;
        covered.top += static_cast<int>(std::min(0.0, target));
        covered.bottom += static_cast<int>(std::max(0.0, target));
        if (!ContainedBounds(covered, proof.bounds)) return reject("target-does-not-cover-caption-envelope");
        if (translation.engagingTranslation)
        {
            const double start = translation.engageStartTranslationPixels;
            const double end = translation.engageTargetTranslationPixels;
            const double pose = translation.engageAppliedTranslationPixels;
            if (!translation.engageIntervalAvailable) return reject("drift-snapshot-unavailable");
            if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(pose))
                return reject("drift-snapshot-nonfinite");
            if (std::abs(end - target) > 0.5) return reject("drift-target-mismatch");
            if (pose < std::min(start, end) || pose > std::max(start, end))
                return reject("drift-pose-outside-interval");
            if (crop.geometry.top + start < 0 || crop.geometry.bottom + start > crop.rasterHeight)
                return reject("drift-start-outside-raster");
            // Compare against the same chroma-aligned route used by rendering.
            // A legitimate fractional pose can round outward by almost two pixels.
            VerticalBarPresentationResolutionInput poseInput;
            poseInput.detailedAction = VerticalBarPresentationAction::TRANSLATE;
            poseInput.translationPixels = static_cast<float>(pose);
            poseInput.authoritativeTop = crop.geometry.top;
            poseInput.authoritativeBottom = crop.geometry.bottom;
            poseInput.rasterHeight = crop.rasterHeight;
            const auto routedPose = ResolveVerticalBarRendererRouting(ResolveVerticalBarPresentation(poseInput));
            if (!routedPose.translationActive || routedPose.translationPixels != crop.verticalTranslationPixels)
                return reject("drift-pose-mismatch");
        }
        else if (std::abs(applied - target) > 1.0)
            return reject("settled-target-mismatch");

        // No second recovery observation or confirmation vote. The logical
        // acquisition above and normal bounded translation resolve atomically.
        const auto candidate = Evaluate(crop);
        if (!candidate.applyCrop || candidate.owner != DecisionOwner::VERTICAL_TRANSLATION)
            return reject(candidate.reason.c_str());
        auto result = AdmitCropPresentation(admittedFit.state, crop, candidate, presentationEpoch);
        if (result.blocked || !result.presentation.applyCrop) return reject(result.presentation.reason.c_str());
        result.presentation.reason = "current caption acquisition completed directly in confirmed translation";
        if (diagnosticReason) *diagnosticReason = "accepted";
        return result;
    }

	bool HasCurrentCropPresentationAdmission(const CropPresentationAdmissionState& previous,
		const Input& input, uint64_t presentationEpoch)
	{
		return previous.available && input.frameSourceGeneration != 0 &&
			previous.sourceGeneration == input.frameSourceGeneration &&
			previous.presentationEpoch == presentationEpoch &&
			previous.trustedCrop.rasterWidth == input.rasterWidth &&
			previous.trustedCrop.rasterHeight == input.rasterHeight &&
			SameTrustedCropContract(previous.trustedCrop, input.geometry);
	}

    bool HasCurrentPreparedSubtitlePresentation(const Input& input)
    {
        const auto& proof=input.preparedSubtitle;
        return proof.available && input.sharedGeometryAvailable &&
            input.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
            input.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
            input.geometrySourceGeneration==input.frameSourceGeneration &&
            input.frameSourceGeneration && input.frameSourceSequence &&
            proof.sourceGeneration==input.frameSourceGeneration &&
            proof.sourceSequence==input.frameSourceSequence &&
            proof.presentationEpoch==input.framePresentationEpoch &&
            SameTrustedCropContract(proof.nativeBase,input.geometry) &&
            input.geometry.trustedBarAxes==ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            input.latestObservationClassification!=ActivePictureClassification::FULL_RASTER_TRUSTED &&
            !input.fullRasterPresentationAuthoritative &&
            input.frameLocalPresentationRetentionEvaluated && input.frameLocalPresentationRetentionSafe &&
            !input.currentVisibleBoundsAvailable;
    }

	CropPresentationAdmissionDecision AdmitCropPresentation(
		const CropPresentationAdmissionState& previous,
		const Input& input, const Decision& candidate,
		uint64_t presentationEpoch)
	{
		CropPresentationAdmissionDecision result;
		result.state = previous;
		result.presentation = candidate;
		if (!input.automaticCropEnabled || input.fullRasterPresentationAuthoritative ||
			(result.state.available &&
			 (result.state.sourceGeneration != input.frameSourceGeneration ||
			  result.state.presentationEpoch != presentationEpoch ||
			  result.state.trustedCrop.rasterWidth != input.rasterWidth ||
			  result.state.trustedCrop.rasterHeight != input.rasterHeight)))
			result.state = {};
        // Logical admission survives temporary withdrawal; the last displayed
        // fit does not. Clear it on every attempt, including blocked/no-crop.
        result.state.confirmedFitOwnership={};
        result.state.outwardPresentationAvailable = false;
        result.state.outwardPresentation = {};
        result.state.presentationSourceSequence = 0;
		if (!candidate.applyCrop) return result;

		// Evaluate (and then recovery) already validated this candidate. A
		// retention owner is permission to reuse an admitted picture contract,
		// not permission to acquire one. In particular, subtitle confirmation
		// must wait for picture authority instead of manufacturing it.
		const bool previouslyPresented = HasCurrentCropPresentationAdmission(
			result.state, input, presentationEpoch);
		// Only resolved picture/fit/translation owners may introduce a new
		// presentation. A freshly published detector rectangle does not turn
		// a pending retention owner into acquisition proof.
		const bool retentionOnly = candidate.owner != DecisionOwner::TRUSTED_CROP &&
			candidate.owner != DecisionOwner::OUTWARD_FIT &&
			candidate.owner != DecisionOwner::VERTICAL_TRANSLATION;
		if (((!input.latestObservationSupportsCrop || retentionOnly) &&
             !(HasCurrentPreparedSubtitlePresentation(input) &&
               input.preparedSubtitle.presentationEpoch==presentationEpoch &&
               candidate.owner==DecisionOwner::TRUSTED_CROP &&
               SameBounds(candidate.sourceBounds,input.geometry))) && !previouslyPresented)
		{
			result.blocked = true;
			result.presentation = {};
			result.presentation.sourceBounds = FullRaster(input.rasterWidth, input.rasterHeight);
			result.presentation.reason =
				"unpresented crop requires current picture acquisition authority";
			return result;
		}
		// Current visible pixels are a veto, never new authority. Check the final
		// resolved rectangle, so a complete caption/FIT envelope still acquires.
		// Established contracts keep their existing bounded overlay handling.
		const bool currentVisibleWitness = input.currentVisibleBoundsAvailable &&
			input.frameSourceGeneration != 0 && input.frameSourceSequence != 0 &&
			input.currentVisibleSourceGeneration == input.frameSourceGeneration &&
			input.currentVisibleSourceSequence == input.frameSourceSequence &&
			SameTrustedCropContract(input.currentVisibleBase, input.geometry) &&
			ValidBounds(input.currentVisibleBounds, input.rasterWidth, input.rasterHeight) &&
			ContainedBounds(input.currentVisibleBounds, input.geometry);
		if (!previouslyPresented && currentVisibleWitness &&
			!ContainedBounds(candidate.sourceBounds, input.currentVisibleBounds))
		{
			result.blocked = true;
			result.presentation = {};
			result.presentation.sourceBounds = FullRaster(input.rasterWidth, input.rasterHeight);
			result.presentation.reason = "unpresented crop excludes current visible pixels";
			return result;
		}
		result.state.available = true;
		result.state.trustedCrop = input.geometry;
		result.state.authorityOrigin = input.authorityOrigin;
		result.state.sourceGeneration = input.frameSourceGeneration;
		result.state.presentationEpoch = presentationEpoch;
        if(input.framePresentationEpoch==presentationEpoch &&
            MatchesConfirmedFitOwnership(candidate.confirmedFitOwnership,input.geometry,candidate.sourceBounds,
                input.frameSourceGeneration,input.frameSourceSequence,presentationEpoch))
            result.state.confirmedFitOwnership=candidate.confirmedFitOwnership;
        result.state.outwardPresentationAvailable = candidate.outwardExpanded &&
            (candidate.owner == DecisionOwner::OUTWARD_FIT || result.state.confirmedFitOwnership.available);
        if (result.state.outwardPresentationAvailable)
        {
            result.state.outwardPresentation = candidate.sourceBounds;
            result.state.presentationSourceSequence = input.frameSourceSequence;
        }
		return result;
	}

	bool IsCurrentSubmittedNativeScopeAnchor(const SubmittedNativeScopeAnchor& anchor,
		uint64_t sourceGeneration, uint64_t sourceSequence,
		uint64_t presentationEpoch, int rasterWidth, int rasterHeight)
	{
		return anchor.available && sourceGeneration != 0 && sourceSequence != 0 &&
			anchor.sourceGeneration == sourceGeneration &&
			anchor.presentationEpoch == presentationEpoch &&
			anchor.lastSubmittedSourceSequence != 0 &&
			sourceSequence >= anchor.lastSubmittedSourceSequence &&
            sourceSequence >= anchor.lastSubmittedPresentationSourceSequence &&
            (!anchor.submittedEnvelopeAvailable ||
             sourceSequence >= anchor.submittedEnvelopeSourceSequence) &&
			ValidBounds(anchor.crop, rasterWidth, rasterHeight) &&
			anchor.crop.trustedBarAxes != ActivePictureBounds::BarAxes::NONE &&
			CropEdgesAreChromaAligned(anchor.crop, rasterWidth, rasterHeight) &&
			HasAuthorityForCroppedAxes(anchor.crop, rasterWidth, rasterHeight);
	}

	bool CanRetainSubmittedScopeContinuity(
		const SubmittedScopeContinuityEvidence& evidence)
	{
		const auto& anchor = evidence.anchor;

		// Transition and near-black latches explain why normal admission paused; current
		// excluded-band proof, not those latches, decides whether Scope remains safe.
		if (!IsCurrentSubmittedNativeScopeAnchor(anchor,
			evidence.sourceGeneration, evidence.sourceSequence,
			evidence.presentationEpoch, evidence.rasterWidth, evidence.rasterHeight) ||
			evidence.fullRasterAuthority ||
			evidence.currentClassification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED ||
			evidence.outwardVisibleBoundsAvailable ||
			evidence.presentationFailOpen ||
			evidence.verticalTranslationActive)
			return false;

		// Re-presenting the exact submitted source frame is safe because its pixels
		// are unchanged. A new source frame must prove the same native edge contract
		// with this frame's own retention measurement and excluded-band scan.
		if (evidence.cadenceRepeat)
			return evidence.sourceSequence == anchor.lastSubmittedSourceSequence;
		return evidence.measurementCurrent && evidence.retentionEvaluated &&
			evidence.retentionSafe && evidence.excludedBandsPixelSafe &&
			evidence.retentionSourceGeneration == evidence.sourceGeneration &&
			evidence.retentionSourceSequence == evidence.sourceSequence &&
			SameTrustedCropContract(evidence.retentionBounds, anchor.crop);
	}

    static bool HasSelectedSamplingEnvelopeFill(const SubmittedNativeScopeAnchorUpdateInput& input)
    {
        const auto& selected = input.selectedAdmission;
        const auto& native = input.nativeCrop;
        const auto& envelope = selected.samplingEnvelopeSourceBounds;
        const auto& actual = input.submittedBounds;
        const int step = std::max(2, native.rasterHeight / 540);
        if (!selected.available || !selected.samplingEnvelopeFillApplied ||
            input.selectedOwner != DecisionOwner::OUTWARD_FIT ||
            selected.sourceGeneration != input.sourceGeneration ||
            selected.presentationEpoch != input.presentationEpoch ||
            selected.samplingEnvelopeFillSourceSequence != input.sourceSequence ||
            selected.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            !SameTrustedCropContract(selected.trustedCrop, native) ||
            !ValidBounds(envelope,native.rasterWidth,native.rasterHeight) ||
            !CropEdgesAreChromaAligned(envelope,native.rasterWidth,native.rasterHeight) ||
            !SameBounds(actual,selected.samplingEnvelopeFillSourceBounds) ||
            envelope.left != native.left || envelope.right != native.right ||
            !ContainedBounds(envelope,native) || SameBounds(envelope,native) ||
            envelope.top < native.top-step || envelope.bottom > native.bottom+step ||
            actual.top != envelope.top || actual.bottom != envelope.bottom)
            return false;
        AspectLimitFillInput fill;
        fill.trustedContentAuthorityAccepted = true;
        fill.cropWiderContentToFillScreen = true;
        fill.screenAspect = selected.samplingEnvelopeFillScreenAspect;
        fill.contentReferenceAvailable = true;
        fill.contentReferenceBounds = native;
        fill.sourceBounds = envelope;
        const auto expected = EvaluateAspectLimitFill(fill);
        return expected.applied && SameBounds(expected.sourceBounds,actual);
    }
    static void ClearSubmittedOptionalFill(SubmittedNativeScopeAnchor& anchor)
    {
        anchor.submittedOptionalFillAvailable = false;
        anchor.submittedOptionalFill = {};
        anchor.submittedOptionalFillSourceSequence = 0;
        anchor.submittedOptionalFillScreenAspect = 0.0;
    }

    static bool HasSubmittedOptionalFill(const SubmittedNativeScopeAnchor& anchor,
        uint64_t generation, uint64_t sequence, uint64_t epoch, int width, int height)
    {
        const auto& actual = anchor.submittedOptionalFill;
        const auto& native = anchor.crop;
        return IsCurrentSubmittedNativeScopeAnchor(anchor,generation,sequence,epoch,width,height) &&
            anchor.submittedOptionalFillAvailable &&
            anchor.submittedOptionalFillSourceSequence != 0 &&
            anchor.submittedOptionalFillSourceSequence >= anchor.lastSubmittedSourceSequence &&
            anchor.submittedOptionalFillSourceSequence == anchor.lastSubmittedPresentationSourceSequence &&
            anchor.submittedOptionalFillSourceSequence <= sequence &&
            std::isfinite(anchor.submittedOptionalFillScreenAspect) &&
            anchor.submittedOptionalFillScreenAspect > 0.0 &&
            ValidBounds(actual,width,height) && CropEdgesAreChromaAligned(actual,width,height) &&
            actual.top == native.top && actual.bottom == native.bottom &&
            actual.left >= native.left && actual.right <= native.right &&
            (actual.left > native.left || actual.right < native.right);
    }

    static bool HasSelectedNativeOptionalFill(const SubmittedNativeScopeAnchorUpdateInput& input)
    {
        const auto& native = input.nativeCrop;
        const auto& actual = input.submittedBounds;
        const auto& selected = input.selectedAdmission;
        if (!input.nativeCropAvailable || input.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            input.selectedOwner == DecisionOwner::FULL_RASTER ||
            input.selectedOwner == DecisionOwner::OUTWARD_FIT ||
            input.selectedOwner == DecisionOwner::MOVING_PICTURE_HOLD ||
            !HasSelectedHorizontalFillWitness(selected,native,actual,input.sourceGeneration,
                input.sourceSequence,input.presentationEpoch) ||
            (actual.left == native.left && actual.right == native.right) ||
            !std::isfinite(selected.optionalFillScreenAspect) || selected.optionalFillScreenAspect <= 0.0)
            return false;
        AspectLimitFillInput fill;
        fill.trustedContentAuthorityAccepted = true;
        fill.cropWiderContentToFillScreen = true;
        fill.screenAspect = selected.optionalFillScreenAspect;
        fill.sourceBounds = native;
        fill.contentReferenceAvailable = true;
        fill.contentReferenceBounds = native;
        const auto expected = EvaluateAspectLimitFill(fill);
        return expected.applied && SameBounds(expected.sourceBounds,actual);
    }

    static void StoreSubmittedOptionalFill(SubmittedNativeScopeAnchor& anchor,
        const SubmittedNativeScopeAnchorUpdateInput& input)
    {
        anchor.submittedOptionalFillAvailable = true;
        anchor.submittedOptionalFill = input.submittedBounds;
        anchor.submittedOptionalFillSourceSequence = input.sourceSequence;
        anchor.submittedOptionalFillScreenAspect = input.selectedAdmission.optionalFillScreenAspect;
    }

	SubmittedNativeScopeAnchor CommitSubmittedNativeScopeAnchor(
		const SubmittedNativeScopeAnchorUpdateInput& input, const char** outcomeReason)
	{
		auto result = input.previous;
        const auto finish=[&](const char* reason) {
            if(outcomeReason)*outcomeReason=reason;
            return result;
        };
		if (!input.frameSubmitted)
			return finish("not-submitted");
        if (result.available && result.sourceGeneration == input.sourceGeneration &&
            result.presentationEpoch == input.presentationEpoch &&
            input.sourceSequence < std::max(std::max(result.lastSubmittedSourceSequence, result.lastSubmittedPresentationSourceSequence),
                std::max(result.submittedEnvelopeSourceSequence,result.submittedOptionalFillSourceSequence)))
            return finish("nonmonotonic-source");
		if (input.fullRasterAuthority) {
            result={};
            return finish("full-authority-cleared");
        }
		const bool previousSameContext = result.available &&
			result.sourceGeneration == input.sourceGeneration &&
			result.presentationEpoch == input.presentationEpoch &&
			ValidBounds(result.crop, input.submittedBounds.rasterWidth,
				input.submittedBounds.rasterHeight) &&
			result.crop.trustedBarAxes != ActivePictureBounds::BarAxes::NONE &&
			CropEdgesAreChromaAligned(result.crop, input.submittedBounds.rasterWidth,
				input.submittedBounds.rasterHeight) &&
			HasAuthorityForCroppedAxes(result.crop, input.submittedBounds.rasterWidth,
				input.submittedBounds.rasterHeight);
		if (!previousSameContext)
			result = {};
		else if (input.sourceSequence < result.lastSubmittedSourceSequence)
			return finish("nonmonotonic-source");
        const bool selectedOptionalFill = HasSelectedNativeOptionalFill(input);
        const bool priorOptionalFill = HasSubmittedOptionalFill(result,input.sourceGeneration,
            input.sourceSequence,input.presentationEpoch,input.submittedBounds.rasterWidth,
            input.submittedBounds.rasterHeight);
        const auto priorFillBounds = result.submittedOptionalFill;
        const double priorFillAspect = result.submittedOptionalFillScreenAspect;
        // A successful superseding output retires the old fill certificate.
        // Missing/proposed/failed submissions returned above without changing it.
        ClearSubmittedOptionalFill(result);
        if (input.selectedAdmission.optionalFillRetained)
        {
            if (previousSameContext && priorOptionalFill && selectedOptionalFill &&
                SameTrustedCropContract(input.nativeCrop,result.crop) &&
                SameBounds(input.submittedBounds,priorFillBounds) &&
                input.selectedAdmission.optionalFillScreenAspect == priorFillAspect)
            {
                StoreSubmittedOptionalFill(result,input);
                result.lastSubmittedPresentationSourceSequence = input.sourceSequence;
            }
            // A retained fill never acquires or renews native AR, even if an
            // invalid caller labels an unsubmitted proposal as retention.
            return finish(result.submittedOptionalFillAvailable ? "retained-optional-fill-committed" : "retained-optional-fill-rejected");
        }
        if (previousSameContext && selectedOptionalFill &&
            SameTrustedCropContract(input.nativeCrop,result.crop))
            StoreSubmittedOptionalFill(result,input);
        const auto& selected=input.selectedAdmission;
        const bool qualifiedEnvelope=selected.available &&
            selected.sourceGeneration==input.sourceGeneration && selected.presentationEpoch==input.presentationEpoch &&
            selected.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE &&
            input.selectedOwner!=DecisionOwner::FULL_RASTER &&
            SameTrustedCropContract(selected.trustedCrop,input.nativeCrop) &&
            MatchesConfirmedFitOwnership(selected.confirmedFitOwnership,input.nativeCrop,input.submittedBounds,
                input.sourceGeneration,input.sourceSequence,input.presentationEpoch);
        if (previousSameContext && input.nativeCropAvailable &&
            input.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            SameTrustedCropContract(input.nativeCrop, result.crop) &&
            // Pending placement may already have displayed a safe outward
            // envelope. Remember that exact submitted view across analysis gaps,
            // without renewing native AR or accepting a proposed-only envelope.
            (qualifiedEnvelope || input.selectedOwner == DecisionOwner::OUTWARD_FIT || input.selectedOwner == DecisionOwner::NEAR_BLACK_EPISODE ||
             ((input.selectedOwner == DecisionOwner::TRANSLATION_CONFIRMATION ||
               input.selectedOwner == DecisionOwner::FIT_CONFIRMATION) &&
              input.selectedAdmission.available &&
              input.selectedAdmission.sourceGeneration == input.sourceGeneration &&
              input.selectedAdmission.presentationEpoch == input.presentationEpoch &&
              input.selectedAdmission.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
              SameTrustedCropContract(input.selectedAdmission.trustedCrop,result.crop))) &&
            ValidBounds(input.submittedBounds, result.crop.rasterWidth, result.crop.rasterHeight) &&
            CropEdgesAreChromaAligned(input.submittedBounds, result.crop.rasterWidth, result.crop.rasterHeight) &&
            ContainedBounds(input.submittedBounds, result.crop) &&
            !SameBounds(input.submittedBounds, result.crop) &&
            input.sourceSequence >= result.submittedEnvelopeSourceSequence)
        {
            result.submittedEnvelopeAvailable = true;
            result.submittedEnvelope = input.submittedBounds;
            result.submittedEnvelopeSourceSequence = input.sourceSequence;
            result.lastSubmittedPresentationSourceSequence = input.sourceSequence;
            return finish(qualifiedEnvelope ? "qualified-fit-envelope-committed" : "submitted-envelope-committed");
        }
		if (!input.nativeCropAvailable || !input.sourceGeneration ||
			!input.sourceSequence || input.selectedOwner == DecisionOwner::FULL_RASTER ||
			input.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
			!ValidBounds(input.nativeCrop, input.nativeCrop.rasterWidth,
				input.nativeCrop.rasterHeight) ||
			!CropEdgesAreChromaAligned(input.nativeCrop,
				input.nativeCrop.rasterWidth, input.nativeCrop.rasterHeight) ||
			!HasAuthorityForCroppedAxes(input.nativeCrop,
				input.nativeCrop.rasterWidth, input.nativeCrop.rasterHeight) ||
			!ValidBounds(input.submittedBounds, input.nativeCrop.rasterWidth,
				input.nativeCrop.rasterHeight))
			return finish("native-contract-rejected");

        if (previousSameContext && SameTrustedCropContract(input.nativeCrop,result.crop) &&
            HasSelectedSamplingEnvelopeFill(input))
        {
            // Actual output proves the vertical extent. The selected optional
            // fill explains the intentionally removed sides; preserve its native
            // width reference without publishing a new AR or reacquiring Scope.
            result.submittedEnvelopeAvailable = true;
            result.submittedEnvelope = input.selectedAdmission.samplingEnvelopeSourceBounds;
            result.submittedEnvelopeSourceSequence = input.sourceSequence;
            result.lastSubmittedPresentationSourceSequence = input.sourceSequence;
            return finish("sampling-envelope-committed");
        }
        // A selected confirmed translation supersedes the old FIT envelope only
        // after submission. Keep an ordering watermark without renewing native AR.
        // Optional side fill may narrow the width, but translation preserves height.
        if (previousSameContext && SameTrustedCropContract(input.nativeCrop, result.crop) &&
            input.selectedOwner == DecisionOwner::VERTICAL_TRANSLATION &&
            input.submittedBounds.left >= input.nativeCrop.left &&
            input.submittedBounds.right <= input.nativeCrop.right &&
            input.submittedBounds.bottom - input.submittedBounds.top ==
                input.nativeCrop.bottom - input.nativeCrop.top)
        {
            result.submittedEnvelopeAvailable = false;
            result.submittedEnvelope = {};
            result.submittedEnvelopeSourceSequence = 0;
            result.lastSubmittedPresentationSourceSequence = input.sourceSequence;
            return finish("translation-retired-envelope");
        }
        // Similar aspect ratios do not prove that the physical picture was
        // displayed: a shifted crop can have exactly the same aspect. Retain
        // only the exact base, or its selected, explicitly approved side fill.
        if (!SameBounds(input.submittedBounds, input.nativeCrop) &&
            !HasSelectedHorizontalFillWitness(input.selectedAdmission,
                input.nativeCrop, input.submittedBounds, input.sourceGeneration,
                input.sourceSequence, input.presentationEpoch))
            return finish("actual-geometry-not-qualified");
		if (result.available && result.sourceGeneration == input.sourceGeneration &&
			result.presentationEpoch == input.presentationEpoch &&
			input.sourceSequence < result.lastSubmittedSourceSequence)
			return finish("nonmonotonic-source");
		result.available = true;
		result.crop = input.nativeCrop;
		result.sourceGeneration = input.sourceGeneration;
		result.presentationEpoch = input.presentationEpoch;
		result.lastSubmittedSourceSequence = input.sourceSequence;
        result.lastSubmittedPresentationSourceSequence = input.sourceSequence;
        result.submittedEnvelopeAvailable = false;
        result.submittedEnvelope = {};
        result.submittedEnvelopeSourceSequence = 0;
        if (selectedOptionalFill) StoreSubmittedOptionalFill(result,input);
		return finish("native-anchor-committed");
	}

    UncertainPicturePresentationDecision ResolveUncertainPicturePresentation(
        const PresentationRecoveryInput& input, const Decision& proposed)
    {
        UncertainPicturePresentationDecision result;
        result.presentation = proposed;
        const auto& crop = input.crop;
        const auto& anchor = input.submittedScopeAnchor;
        const auto raster = FullRaster(crop.rasterWidth, crop.rasterHeight);
        const bool committedFull = crop.sharedGeometryAvailable &&
            crop.geometrySourceGeneration == crop.frameSourceGeneration &&
            crop.frameSourceGeneration != 0 &&
            crop.classification == ActivePictureClassification::FULL_RASTER_TRUSTED &&
            SameBounds(crop.geometry, raster);
        if (!crop.automaticCropEnabled || crop.fullRasterPresentationAuthoritative ||
            committedFull || !IsCurrentSubmittedNativeScopeAnchor(anchor, crop.frameSourceGeneration,
                crop.frameSourceSequence, input.presentationEpoch,
                crop.rasterWidth, crop.rasterHeight))
            return result;

        if (HasConfirmedPresentationCandidate(input, proposed))
        {
            if(HasCurrentConfirmedOutwardCandidate(input,proposed))
                result.presentation.confirmedFitOwnership=input.confirmedFitOwnership;
            return result;
        }

        const bool submittedEnvelope = anchor.submittedEnvelopeAvailable &&
            anchor.submittedEnvelopeSourceSequence >= anchor.lastSubmittedSourceSequence &&
            anchor.submittedEnvelopeSourceSequence <= crop.frameSourceSequence &&
            ValidBounds(anchor.submittedEnvelope, crop.rasterWidth, crop.rasterHeight) &&
            ContainedBounds(anchor.submittedEnvelope, anchor.crop) &&
            !SameBounds(anchor.submittedEnvelope, anchor.crop);
        const bool unresolvedInspection = !input.measurementCurrent || !input.observationAvailable ||
            crop.latestObservationIsUnavailable || crop.verticalTranslationConfirmationPending ||
            crop.verticalFitConfirmationPending || crop.verticalInspectionPending ||
            (input.observationClassification == ActivePictureClassification::PROVISIONAL &&
             !crop.frameLocalPresentationRetentionSafe);
        // Recovery may already have proposed a valid native crop even though it
        // could not inspect this frame. That is not proof that a displayed overlay
        // disappeared. Preserve its submitted extent before the cropped fast path.
        const bool retainSubmittedEnvelope = submittedEnvelope && unresolvedInspection &&
            crop.sharedGeometryAvailable && crop.geometrySourceGeneration == crop.frameSourceGeneration &&
            SameTrustedCropContract(crop.geometry, anchor.crop) &&
            !crop.latestObservationSupportsCrop && !crop.verticalTranslationActive &&
            !crop.verticalTranslationBaseRetentionActive && !crop.verticalTranslationEngageBaseRetentionActive &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active &&
            !crop.movingPictureHold.competingPresentation &&
            proposed.owner != DecisionOwner::VERTICAL_TRANSLATION &&
            proposed.owner != DecisionOwner::ENGAGE_BASE && proposed.owner != DecisionOwner::RELEASE_BASE &&
            proposed.owner != DecisionOwner::MOVING_PICTURE_HOLD && !proposed.verticallyTranslated &&
            SameBounds(anchor.crop, proposed.sourceBounds);
        const bool currentSampling = input.measurementCurrent && input.retentionEvaluated &&
            input.excludedBandsPixelSafe && input.observationAvailable &&
            input.retentionSourceGeneration == crop.frameSourceGeneration &&
            input.retentionSourceSequence == crop.frameSourceSequence &&
            SameTrustedCropContract(input.retentionBounds, anchor.crop) &&
            input.observationClassification == ActivePictureClassification::PROVISIONAL &&
            ValidBounds(input.observation, crop.rasterWidth, crop.rasterHeight);
        const int step = std::max(2, crop.rasterHeight / 540);
        const bool retainedSamplingExtent = currentSampling &&
            proposed.owner == DecisionOwner::NEAR_BLACK_EPISODE &&
            SameBounds(proposed.sourceBounds, anchor.crop) &&
            input.observation.left >= anchor.crop.left && input.observation.right <= anchor.crop.right &&
            input.observation.top >= anchor.crop.top - step &&
            input.observation.bottom <= anchor.crop.bottom + step;
        // An outward FIT synthesized from ambiguous vertical occupancy is
        // still a proposed presentation, even when it stops just short of the
        // raster edge. It needs the same independent confirmation as full FIT.
        // Native replacements and independently owned side/translation paths
        // retain their existing fast paths.
        const bool unconfirmedVerticalFit =
            !proposed.verticallyTranslated &&
            crop.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            crop.sharedGeometryAvailable && crop.geometrySourceGeneration == crop.frameSourceGeneration &&
            SameTrustedCropContract(crop.geometry, anchor.crop) &&
            anchor.crop.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            anchor.crop.left == 0 && anchor.crop.right == crop.rasterWidth &&
            proposed.sourceBounds.left == anchor.crop.left && proposed.sourceBounds.right == anchor.crop.right &&
            ContainedBounds(proposed.sourceBounds, anchor.crop) &&
            !SameBounds(proposed.sourceBounds, anchor.crop) &&
            !crop.movingPictureTransition && !crop.pictureTransitionHandoff.active;
        if (proposed.applyCrop && ValidBounds(proposed.sourceBounds, crop.rasterWidth, crop.rasterHeight) &&
            !SameBounds(proposed.sourceBounds, raster) && !retainSubmittedEnvelope && !retainedSamplingExtent &&
            !unconfirmedVerticalFit)
            return result;
        auto bounds = anchor.crop;
        auto include = [&](const ActivePictureBounds& extent) {
            bounds.left = std::min(bounds.left, extent.left) & ~1;
            bounds.top = std::min(bounds.top, extent.top) & ~1;
            bounds.right = std::min(crop.rasterWidth,
                (std::max(bounds.right, extent.right) + 1) & ~1);
            bounds.bottom = std::min(crop.rasterHeight,
                (std::max(bounds.bottom, extent.bottom) + 1) & ~1);
        };
        // Retain only an envelope that actually reached the display. A late or
        // repeated analysis result cannot replace it or narrow it speculatively.
        if (anchor.submittedEnvelopeAvailable &&
            anchor.submittedEnvelopeSourceSequence >= anchor.lastSubmittedSourceSequence &&
            anchor.submittedEnvelopeSourceSequence <= crop.frameSourceSequence &&
            ValidBounds(anchor.submittedEnvelope, crop.rasterWidth, crop.rasterHeight) &&
            ContainedBounds(anchor.submittedEnvelope, anchor.crop))
            include(anchor.submittedEnvelope);

        // Current visible samples remain acquisition/inspection evidence.
        // Without an independently confirmed presentation owner they cannot
        // originate a new envelope around this already submitted native crop.
        // Only its actually submitted envelope above supplies continuity here.
        // A safe outer-band scan can still find actual edge pixels one sampling
        // step outside the old crop. Include those pixels; never relabel them black.
        if (currentSampling && input.observation.left >= anchor.crop.left &&
            input.observation.right <= anchor.crop.right &&
            input.observation.top >= anchor.crop.top - step &&
            input.observation.bottom <= anchor.crop.bottom + step)
        {
            include(input.observation);
            result.currentPixelExtent = true;
        }
        result.retained = true;
        result.nativeCrop = anchor.crop;
        result.expanded = !SameBounds(bounds, anchor.crop);
        bounds.aspectRatio = double(bounds.right - bounds.left) /
            std::max(1, bounds.bottom - bounds.top);
        if (result.expanded) bounds.trustedBarAxes = ActivePictureBounds::BarAxes::NONE;
        result.presentation = {};
        result.presentation.sourceBounds = bounds;
        result.presentation.applyCrop = !SameBounds(bounds, raster);
        result.presentation.outwardExpanded = result.expanded;
        result.presentation.owner = result.expanded ? DecisionOwner::OUTWARD_FIT :
            DecisionOwner::SUBMITTED_SCOPE_RETENTION;
        result.presentation.reason = result.expanded
            ? "uncertain picture preserves submitted AR and measured outward pixels"
            : "uncertain picture retains last submitted native AR; full raster unproven";
        return result;
    }

    static bool TryFillBoundedVerticalSamplingEnvelope(
        const FinalAspectLimitFillInput& input, AspectLimitFillDecision& result)
    {
        const auto& evidence = input.evidence;
        const auto& crop = input.crop;
        const auto& anchor = evidence.submittedScopeAnchor;
        const auto& actual = input.presentation.sourceBounds;
        // A current safe-band scan may preserve horizontal fill while recovery
        // holds an already selected vertical sampling envelope. Overlay owners
        // retain the existing veto; neither native authority nor rows are changed.
        if (input.samplingCompetingPresentation || !input.presentation.applyCrop ||
            input.presentation.verticallyTranslated || !crop.automaticCropEnabled ||
            crop.fullRasterPresentationAuthoritative || crop.movingPictureTransition ||
            HasCurrentPictureTransitionHandoff(crop) ||
            crop.nearBlackEpisodeRetainCrop || crop.nearBlackEpisodeFullRaster ||
            crop.verticalTranslationActive || crop.verticalTranslationConfirmationPending ||
            crop.verticalFitConfirmationPending || crop.verticalTranslationBaseRetentionActive ||
            crop.verticalTranslationEngageBaseRetentionActive ||
            crop.barCropRefinementHorizontalConflict || crop.currentVisibleBoundsAvailable ||
            evidence.crop.currentVisibleBoundsAvailable ||
            input.nearBlackBoundedPresentation || input.nearBlackRetainedScopeFallback ||
            input.nearBlackFailOpenProposal ||
            !evidence.nearBlackEvaluated || evidence.globalNearBlack ||
            evidence.crop.frameSourceGeneration != crop.frameSourceGeneration ||
            evidence.crop.frameSourceSequence != crop.frameSourceSequence ||
            evidence.presentationEpoch != crop.framePresentationEpoch ||
            !IsCurrentSubmittedNativeScopeAnchor(anchor, crop.frameSourceGeneration,
                crop.frameSourceSequence, evidence.presentationEpoch,
                crop.rasterWidth, crop.rasterHeight) ||
            crop.geometrySourceGeneration != crop.frameSourceGeneration ||
            crop.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
            !SameTrustedCropContract(crop.geometry, anchor.crop) ||
            !ValidBounds(actual, crop.rasterWidth, crop.rasterHeight) ||
            !CropEdgesAreChromaAligned(actual, crop.rasterWidth, crop.rasterHeight))
            return false;

        // Use the existing detector-step bound with a current exact-native
        // sampling contract. The observation may now be contained by the held
        // envelope; it need not recreate the same outward rows on each frame.
        // Generic frame-local retention may be false because rows are preserved.
        if (!evidence.measurementCurrent || !evidence.retentionEvaluated ||
            !evidence.excludedBandsPixelSafe || !evidence.observationAvailable ||
            evidence.retentionSourceGeneration != crop.frameSourceGeneration ||
            evidence.retentionSourceSequence != crop.frameSourceSequence ||
            !SameTrustedCropContract(evidence.retentionBounds, anchor.crop) ||
            evidence.observationClassification != ActivePictureClassification::PROVISIONAL ||
            !ValidBounds(evidence.observation, crop.rasterWidth, crop.rasterHeight))
            return false;
        const auto& observed = evidence.observation;
        const int step = std::max(2, crop.rasterHeight / 540);
        if (actual.left != anchor.crop.left || actual.right != anchor.crop.right ||
            actual.top > anchor.crop.top || actual.bottom < anchor.crop.bottom ||
            actual.top < anchor.crop.top - step || actual.bottom > anchor.crop.bottom + step ||
            SameBounds(actual, anchor.crop) ||
            observed.left < actual.left || observed.right > actual.right ||
            observed.top < actual.top || observed.bottom > actual.bottom)
            return false;

        // Both the independently submitted native content and the actual
        // presentation must pass the current user limits. Only horizontal fill
        // is permitted here; a vertical expansion cannot switch fill direction.
        auto fill = input.configured;
        fill.trustedContentAuthorityAccepted = true;
        fill.contentReferenceAvailable = true;
        fill.contentReferenceBounds = anchor.crop;
        fill.sourceBounds = anchor.crop;
        const auto nativeFill = EvaluateAspectLimitFill(fill);
        if (!nativeFill.applied || nativeFill.sourceBounds.top != anchor.crop.top ||
            nativeFill.sourceBounds.bottom != anchor.crop.bottom)
            return false;
        fill.sourceBounds = actual;
        auto expandedFill = EvaluateAspectLimitFill(fill);
        if (!expandedFill.applied || expandedFill.sourceBounds.top != actual.top ||
            expandedFill.sourceBounds.bottom != actual.bottom ||
            expandedFill.sourceBounds.left > nativeFill.sourceBounds.left ||
            expandedFill.sourceBounds.right < nativeFill.sourceBounds.right)
            return false;
        expandedFill.samplingEnvelopeFilled = true;
        expandedFill.reason =
            "current safe sampling preserves the vertical envelope and eligible horizontal fill";
        result = expandedFill;
        return true;
    }

    static bool TryRetainSubmittedNativeOptionalFill(const FinalAspectLimitFillInput& input,
        AspectLimitFillDecision& result)
    {
        const auto& crop = input.crop;
        const auto& anchor = input.evidence.submittedScopeAnchor;
        const auto& native = anchor.crop;
        const auto& presentation = input.presentation;
        if (!input.configuredScreenActive || input.currentConfirmedPresentationConflict ||
            input.protectedCaptionFit || input.nlsPresentationFailOpen || input.nlsOwnsPresentationGeometry ||
            input.uncertainPicture.expanded || !crop.automaticCropEnabled ||
            !crop.sharedGeometryAvailable || crop.geometrySourceGeneration != crop.frameSourceGeneration ||
            crop.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            crop.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
            crop.fullRasterPresentationAuthoritative ||
            crop.latestObservationClassification == ActivePictureClassification::FULL_RASTER_TRUSTED ||
            crop.movingPictureTransition || HasCurrentPictureTransitionHandoff(crop) ||
            crop.movingPictureHold.competingPresentation || crop.barCropRefinementHorizontalConflict ||
            !presentation.applyCrop || presentation.outwardExpanded ||
            presentation.owner == DecisionOwner::FULL_RASTER ||
            presentation.owner == DecisionOwner::OUTWARD_FIT ||
            presentation.owner == DecisionOwner::MOVING_PICTURE_HOLD ||
            !HasSubmittedOptionalFill(anchor,crop.frameSourceGeneration,crop.frameSourceSequence,
                crop.framePresentationEpoch,crop.rasterWidth,crop.rasterHeight) ||
            input.evidence.presentationEpoch != crop.framePresentationEpoch ||
            !SameTrustedCropContract(crop.geometry,native) ||
            native.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
            native.left != 0 || native.right != crop.rasterWidth ||
            !SameBounds(presentation.sourceBounds,native) ||
            input.configured.screenAspect != anchor.submittedOptionalFillScreenAspect)
            return false;
        // The exact actual native bounds above reject a real translation.
        // Advanced subtitle modes may retain Classic's logical translation
        // flag after physical placement has already restored those bounds.
        // Current policy always wins. The stored rectangle is a witness, never
        // permission to reuse stale limits, a changed screen or a new crop.
        auto fill = input.configured;
        fill.trustedContentAuthorityAccepted = true;
        fill.contentReferenceAvailable = true;
        fill.contentReferenceBounds = native;
        fill.sourceBounds = native;
        auto current = EvaluateAspectLimitFill(fill);
        if (!current.applied || current.sourceBounds.top != native.top ||
            current.sourceBounds.bottom != native.bottom ||
            !SameBounds(current.sourceBounds,anchor.submittedOptionalFill))
            return false;
        current.retainedSubmittedFill = true;
        current.reason = "submitted native side fill retained across uncertain analysis";
        result = current;
        return true;
    }

    AspectLimitFillDecision EvaluateFinalAspectLimitFill(const FinalAspectLimitFillInput& input)
    {
        AspectLimitFillDecision result;
        result.sourceBounds = input.presentation.sourceBounds;
        if (input.fixedCropAspectConfigured)
        {
            FixedAspectCropInput fixed;
            fixed.fixedAspect = input.fixedCropAspect;
            fixed.sourceBounds = input.presentation.sourceBounds;
            return EvaluateFixedAspectCrop(fixed);
        }
        const auto& submittedFillAnchor = input.evidence.submittedScopeAnchor;
        if (input.currentConfirmedPresentationConflict && input.presentation.applyCrop &&
            SameBounds(input.presentation.sourceBounds,submittedFillAnchor.crop) &&
            SameTrustedCropContract(input.crop.geometry,submittedFillAnchor.crop) &&
            HasSubmittedOptionalFill(submittedFillAnchor,input.crop.frameSourceGeneration,
                input.crop.frameSourceSequence,input.crop.framePresentationEpoch,
                input.crop.rasterWidth,input.crop.rasterHeight))
        {
            // A fresh independent conflict must win on measured sources too;
            // changing the recovery route cannot reapply the conflicting fill.
            result.reason = "current confirmed presentation conflict retires submitted side fill";
            return result;
        }
        const bool retainedPresentation = input.uncertainPicture.retained ||
            input.recoveryBoundedPresentation || input.nearBlackBoundedPresentation ||
            input.nearBlackRetainedScopeFallback || input.nearBlackFailOpenProposal;
        if (retainedPresentation && TryRetainSubmittedNativeOptionalFill(input,result))
            return result;
        if (input.protectedCaptionFit || input.nlsPresentationFailOpen ||
            input.uncertainPicture.expanded ||
            (!input.uncertainPicture.retained && (input.recoveryBoundedPresentation ||
             input.nearBlackBoundedPresentation || input.nearBlackRetainedScopeFallback ||
             input.nearBlackFailOpenProposal)))
        {
            if (!input.protectedCaptionFit && !input.nlsPresentationFailOpen &&
                !input.nlsOwnsPresentationGeometry && input.configuredScreenActive &&
                TryFillBoundedVerticalSamplingEnvelope(input, result))
                return result;
            result.reason = input.protectedCaptionFit
                ? "known inward caption return preserves picture and caption; aspect-limit fill withheld"
                : "recovery preserves the complete visible envelope; aspect-limit fill withheld";
            return result;
        }
        if (input.nlsOwnsPresentationGeometry)
        {
            const int width = result.sourceBounds.right - result.sourceBounds.left;
            const int height = result.sourceBounds.bottom - result.sourceBounds.top;
            result.contentAspect = height > 0 ? static_cast<double>(width) / height : 0.0;
            result.reason = "NLS owns final source geometry; aspect-limit fill withheld";
            return result;
        }
        if (!input.configuredScreenActive)
        {
            result.reason = "configured physical screen is not active";
            return result;
        }
        auto fill = input.configured;
        fill.trustedContentAuthorityAccepted = input.presentation.applyCrop ||
            input.crop.fullRasterPresentationAuthoritative;
        fill.sourceBounds = input.presentation.sourceBounds;
        fill.contentReferenceAvailable = true;
        if (input.crop.fullRasterPresentationAuthoritative)
            fill.contentReferenceBounds = FullRaster(input.crop.rasterWidth, input.crop.rasterHeight);
        else if (input.crop.geometrySourceGeneration == input.crop.frameSourceGeneration)
            fill.contentReferenceBounds = input.crop.geometry;
        return EvaluateAspectLimitFill(fill);
    }

	Decision Evaluate(const Input& input)
	{
		Decision decision;
		decision.sourceBounds = FullRaster(input.rasterWidth, input.rasterHeight);
		if (input.presentationFailOpen)
		{
			decision.reason = "presentation evidence requested fail-open";
			return decision;
		}
		if (!input.automaticCropEnabled)
		{
			decision.reason = "automatic crop is off; preserving full raster";
			return decision;
		}
		// Current safe Scope evidence may outlive an ambiguous near-black latch.
		// Explicit full-raster authority still wins below.
		if (input.nearBlackEpisodeFullRaster &&
			!input.submittedScopeContinuityRetention)
		{
			decision.reason =
				"near-black title episode latched full-raster presentation";
			return decision;
		}
		if (input.fullRasterPresentationAuthoritative)
		{
			decision.reason =
				"generation-current full-raster presentation authority accepted";
			return decision;
		}
		if (!input.sharedGeometryAvailable)
		{
			decision.reason = "shared trusted geometry is unavailable";
			return decision;
		}
		if (input.geometrySourceGeneration == 0 ||
			input.geometrySourceGeneration != input.frameSourceGeneration)
		{
			decision.reason = "shared geometry belongs to a stale source generation";
			return decision;
		}
		if (input.classification !=
			ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			decision.reason = input.classification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED
				? "shared authority is full raster"
				: "shared geometry lacks crop authority";
			return decision;
		}
        // Current excluded-band pixel proof is stronger than a transition-state pause.
        if (input.movingPictureTransition &&
            !input.submittedScopeContinuityRetention)
        {
            if (HasCurrentMovingPictureHold(input))
            {
                decision.sourceBounds = input.geometry;
                decision.applyCrop = true;
                decision.owner = DecisionOwner::MOVING_PICTURE_HOLD;
                decision.reason = "established crop retained while moving picture edges settle";
            }
            else
                decision.reason = "moving picture hold unavailable, stale, or conflicting";
            return decision;
        }
		if (HasCurrentPictureTransitionHandoff(input))
		{
			// Preserve the old admitted contract; do not acquire the candidate,
			// apply subtitle padding, or bypass recovery/final admission.
			decision.sourceBounds = input.geometry;
			decision.applyCrop = true;
			decision.owner = DecisionOwner::PICTURE_CONFIRMATION;
			decision.reason = "broad picture transition awaiting model publication";
			return decision;
		}
		// Horizontal visibility and vertical subtitle ownership are independent.
		// The vertical owner is validated again below; it may translate or briefly
		// confirm against the same trusted base while current pixels bound width.
		const bool verticalPresentationOwnsExtent =
			(input.verticalTranslationActive || input.verticalTranslationConfirmationPending ||
			 input.verticalFitConfirmationPending || input.verticalTranslationEngageBaseRetentionActive) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration == input.frameSourceGeneration &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.outwardExpansion.top == input.geometry.top && input.outwardExpansion.bottom == input.geometry.bottom;
		// Provisional geometry does not grant a new inward crop. It also must not
		// veto independently measured outward pixels on the existing trusted base.
		const bool horizontalObservationEligible = !input.latestObservationIsUnavailable &&
			(input.latestObservationClassification == ActivePictureClassification::BAR_CROP_TRUSTED ||
			 (input.latestObservationIsProvisional &&
			  input.latestObservationClassification == ActivePictureClassification::PROVISIONAL));
		const bool horizontalPixelsCurrent = input.currentVisibleBoundsAvailable &&
			ValidBounds(input.currentVisibleBounds, input.rasterWidth, input.rasterHeight) &&
			input.frameSourceSequence != 0 &&
			input.currentVisibleSourceGeneration == input.frameSourceGeneration &&
			input.currentVisibleSourceSequence == input.frameSourceSequence &&
			SameTrustedCropContract(input.currentVisibleBase, input.geometry);
		const bool horizontalCandidateCurrent = input.outwardPresentationActive &&
			input.outwardExpansionAvailable &&
			input.outwardExpansionSourceGeneration == input.frameSourceGeneration &&
			ValidBounds(input.outwardExpansion, input.rasterWidth, input.rasterHeight) &&
			CropEdgesAreChromaAligned(input.outwardExpansion, input.rasterWidth, input.rasterHeight);
		const bool horizontalPixelsCovered =
			ContainedBounds(input.currentVisibleBounds, input.geometry) &&
			(ContainedBounds(input.outwardExpansion, input.currentVisibleBounds) ||
			 (verticalPresentationOwnsExtent &&
			  input.outwardExpansion.left <= input.currentVisibleBounds.left &&
			  input.outwardExpansion.right >= input.currentVisibleBounds.right)) &&
			(input.currentVisibleBounds.left < input.geometry.left ||
			 input.currentVisibleBounds.right > input.geometry.right);
		const bool horizontalExpansionPixelBounded = input.barCropRefinementHorizontalConflict &&
			horizontalObservationEligible && horizontalPixelsCurrent &&
			horizontalCandidateCurrent && horizontalPixelsCovered;
		if (input.barCropRefinementHorizontalConflict && !horizontalExpansionPixelBounded)
		{
			decision.reason = !horizontalObservationEligible
				? "horizontal fit rejected: observation unavailable or full-raster"
				: !horizontalPixelsCurrent
				? "horizontal fit rejected: pixel extent missing, invalid, or stale"
				: !horizontalCandidateCurrent
				? "horizontal fit rejected: outward candidate invalid or stale"
				: "horizontal fit rejected: pixels not covered or vertical owner unresolved";
			return decision;
		}
		const bool ambiguousObservation =
			input.latestObservationIsProvisional ||
			input.latestObservationIsUnavailable;
		// A scene cut may yield several individually trusted bar observations
		// before the transition model can safely publish one as the next stable
		// geometry. Do not toggle between the old crop and full raster during that
		// bounded verification interval. This preserves only the already-trusted
		// rectangle; a full-raster observation or a frame-local pixel conflict
		// still withdraws immediately.
		const bool boundedSceneVerificationRetention =
			!input.frameLocalPresentationRetentionEvaluated &&
			input.sceneVerificationHoldActive &&
			(ambiguousObservation ||
			 input.latestObservationClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED);
		const bool boundedAmbiguousRetention = ambiguousObservation &&
			!input.frameLocalPresentationRetentionEvaluated &&
			input.ambiguityHoldActive;
		const bool pixelSafeAmbiguousRetention = ambiguousObservation &&
			input.frameLocalPresentationRetentionSafe;
		const bool nearBlackEpisodeRetention =
			input.nearBlackEpisodeRetainCrop;
		const bool boundedBarCropRefinementRetention =
			input.barCropRefinementPending &&
			!input.barCropRefinementHorizontalConflict &&
			input.latestObservationClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED;
		const bool boundedVerticalInspectionRetention =
			input.verticalInspectionPending &&
			input.verticalInspectionSourceGeneration != 0 &&
			input.verticalInspectionSourceGeneration ==
				input.frameSourceGeneration &&
			input.verticalInspectionSourceSequence != 0 &&
			input.verticalInspectionSourceSequence ==
				input.frameSourceSequence &&
			input.latestObservationClassification !=
				ActivePictureClassification::FULL_RASTER_TRUSTED;
		const bool boundedOutwardExpansion =
			input.outwardPresentationActive &&
			input.outwardExpansionAvailable &&
			input.outwardExpansionSourceGeneration != 0 &&
			input.outwardExpansionSourceGeneration ==
				input.frameSourceGeneration;
		const bool boundedVerticalTranslation =
			input.verticalTranslationActive &&
			input.verticalTranslationPixels != 0 &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration ==
				input.frameSourceGeneration;
		const bool boundedVerticalBaseRetention =
			input.verticalTranslationBaseRetentionActive &&
			!input.frameLocalPresentationRetentionEvaluated &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration ==
				input.frameSourceGeneration;
		const bool boundedVerticalEngageBaseRetention =
			input.verticalTranslationEngageBaseRetentionActive &&
			input.latestObservationIsProvisional &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration ==
				input.frameSourceGeneration;
		const bool boundedVerticalConfirmationRetention =
			input.verticalTranslationConfirmationPending &&
			input.latestObservationClassification !=
				ActivePictureClassification::FULL_RASTER_TRUSTED &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration ==
				input.frameSourceGeneration;
		const bool boundedVerticalFitConfirmationRetention =
			input.verticalFitConfirmationPending &&
			input.latestObservationClassification !=
				ActivePictureClassification::FULL_RASTER_TRUSTED &&
			SameBounds(input.verticalTranslationBase, input.geometry) &&
			input.verticalTranslationSourceGeneration != 0 &&
			input.verticalTranslationSourceGeneration ==
				input.frameSourceGeneration;
		if (!input.latestObservationSupportsCrop &&
            !HasCurrentPreparedSubtitlePresentation(input) &&
			!boundedSceneVerificationRetention &&
			!boundedAmbiguousRetention && !pixelSafeAmbiguousRetention &&
			!nearBlackEpisodeRetention &&
			!input.submittedScopeContinuityRetention &&
			!boundedBarCropRefinementRetention &&
			!boundedVerticalInspectionRetention &&
			!boundedOutwardExpansion && !boundedVerticalTranslation &&
			!boundedVerticalBaseRetention &&
			!boundedVerticalEngageBaseRetention &&
			!boundedVerticalConfirmationRetention &&
			!boundedVerticalFitConfirmationRetention)
		{
			decision.withdrawalCause =
				WithdrawalCause::LATEST_OBSERVATION_UNREAFFIRMED;
			decision.reason =
				"latest observation does not reaffirm crop authority";
			return decision;
		}
		if (!HasAuthorityForCroppedAxes(
			input.geometry, input.rasterWidth, input.rasterHeight))
		{
			decision.reason =
				"shared crop bounds lack opposing-edge authority";
			return decision;
		}
		if (!ValidBounds(input.geometry, input.rasterWidth, input.rasterHeight))
		{
			decision.reason = "shared crop bounds do not match the current raster";
			return decision;
		}
		if (!CropEdgesAreChromaAligned(
			input.geometry, input.rasterWidth, input.rasterHeight))
		{
			decision.reason = "shared crop bounds are not chroma aligned";
			return decision;
		}
		if (input.verticalTranslationActive &&
			input.outwardPresentationActive &&
			(input.outwardExpansion.top != input.geometry.top ||
			 input.outwardExpansion.bottom != input.geometry.bottom))
		{
			decision.reason =
				"vertical fit and presentation translation cannot be combined";
			return decision;
		}
		ActivePictureBounds presentation = input.geometry;
		if (input.outwardPresentationActive)
		{
			if (!boundedOutwardExpansion)
			{
				decision.reason =
					"outward presentation lacks current bounded evidence";
				return decision;
			}
			if (!ValidBounds(input.outwardExpansion,
				input.rasterWidth, input.rasterHeight) ||
				!CropEdgesAreChromaAligned(input.outwardExpansion,
					input.rasterWidth, input.rasterHeight))
			{
				decision.reason =
					"outward presentation bounds are invalid or not chroma aligned";
				return decision;
			}
			const bool containsAuthority =
				input.outwardExpansion.left <= input.geometry.left &&
				input.outwardExpansion.top <= input.geometry.top &&
				input.outwardExpansion.right >= input.geometry.right &&
				input.outwardExpansion.bottom >= input.geometry.bottom;
			const bool actuallyExpands =
				input.outwardExpansion.left < input.geometry.left ||
				input.outwardExpansion.top < input.geometry.top ||
				input.outwardExpansion.right > input.geometry.right ||
				input.outwardExpansion.bottom > input.geometry.bottom;
			if (!containsAuthority || !actuallyExpands)
			{
				decision.reason =
					"presentation bounds do not expand outward from crop authority";
				return decision;
			}

			presentation = input.outwardExpansion;
			decision.outwardExpanded = true;
		}

		if (input.verticalTranslationActive)
		{
			if (!boundedVerticalTranslation)
			{
				decision.reason =
					"vertical presentation translation lacks current bounded evidence";
				return decision;
			}
			const int alignedRequest = ChromaAlignedDisplacement(
				input.verticalTranslationPixels);
			const int minimumShift = -presentation.top;
			const int maximumShift = input.rasterHeight - presentation.bottom;
			const int appliedShift = std::max(minimumShift,
				std::min(alignedRequest, maximumShift));
			if (appliedShift == 0 || (appliedShift & 1) != 0)
			{
				decision.reason =
					"vertical presentation translation cannot be applied safely";
				return decision;
			}
			presentation.top += appliedShift;
			presentation.bottom += appliedShift;
			if (!ValidBounds(presentation,
				input.rasterWidth, input.rasterHeight) ||
				!CropEdgesAreChromaAligned(presentation,
					input.rasterWidth, input.rasterHeight))
			{
				decision.reason =
					"translated presentation bounds are invalid or not chroma aligned";
				return decision;
			}
			decision.verticallyTranslated = true;
			decision.verticalTranslationPixels = appliedShift;
		}

		decision.sourceBounds = presentation;
		decision.applyCrop = true;
		decision.horizontalExpansionPixelBounded = horizontalExpansionPixelBounded;
		if (decision.verticallyTranslated)
		{
			decision.owner = DecisionOwner::VERTICAL_TRANSLATION;
			decision.reason = decision.outwardExpanded
				? "bounded outward fit and same-size vertical translation accepted"
				: "same-size vertical presentation translation accepted";
		}
        else if (!decision.outwardExpanded && HasCurrentPreparedSubtitlePresentation(input))
        {
            decision.owner=DecisionOwner::TRUSTED_CROP;
            decision.reason="current prepared subtitle permits trusted native presentation";
        }
		else if (boundedVerticalEngageBaseRetention)
		{
			decision.owner = DecisionOwner::ENGAGE_BASE;
			decision.reason =
				"trusted crop retained at timed subtitle engage origin";
		}
		else if (boundedVerticalBaseRetention)
		{
			decision.owner = DecisionOwner::RELEASE_BASE;
			decision.reason =
				"subtitle release settled at current trusted base";
		}
		else if (nearBlackEpisodeRetention)
		{
			decision.owner = DecisionOwner::NEAR_BLACK_EPISODE;
			decision.reason =
				"trusted crop retained for near-black title episode";
		}
		else if (input.submittedScopeContinuityRetention)
		{
			decision.owner = DecisionOwner::SUBMITTED_SCOPE_RETENTION;
			decision.reason =
				"last submitted native Scope retained by current excluded-band pixel proof";
		}
		else if (boundedBarCropRefinementRetention)
		{
			decision.owner = DecisionOwner::BAR_REFINEMENT;
			decision.reason =
				"trusted crop retained while bar refinement confirms";
		}
		else if (boundedVerticalFitConfirmationRetention)
		{
			decision.owner = DecisionOwner::FIT_CONFIRMATION;
			decision.reason =
				"trusted crop retained while vertical fit confirms";
		}
		else if (boundedVerticalConfirmationRetention)
		{
			decision.owner = DecisionOwner::TRANSLATION_CONFIRMATION;
			decision.reason =
				"trusted crop retained while subtitle translation target confirms";
		}
		else if (boundedVerticalInspectionRetention)
		{
			decision.owner = DecisionOwner::VERTICAL_INSPECTION;
			decision.reason =
				"trusted crop retained while vertical overlay inspection completes";
		}
		else if (decision.outwardExpanded)
		{
			decision.owner = DecisionOwner::OUTWARD_FIT;
			decision.reason =
				"bounded outward presentation expansion accepted";
		}
		else if (input.latestObservationSupportsCrop)
		{
			decision.owner = DecisionOwner::TRUSTED_CROP;
			decision.reason =
				"generation-current shared crop authority accepted";
		}
		else if (pixelSafeAmbiguousRetention)
		{
			decision.owner = DecisionOwner::PIXEL_SAFE_RETENTION;
			decision.reason =
				"frame-local pixel-safe presentation retained prior crop";
		}
		else if (boundedSceneVerificationRetention)
		{
			decision.owner = DecisionOwner::SCENE_HOLD;
			decision.reason =
				"bounded scene verification retained current trusted crop";
		}
		else
		{
			decision.owner = DecisionOwner::AMBIGUITY_HOLD;
			decision.reason =
				"bounded ambiguity hold retained current trusted crop";
		}
		return decision;
	}

	SceneDecision EvaluateSceneBoundary(const SceneInput& input)
	{
		SceneDecision decision;
		if (!input.geometryAvailable ||
			!input.geometryIsCurrentGeneration ||
			!input.latestEvidenceIsCurrent)
		{
			decision.reason =
				"scene evidence or presentation geometry is not current";
			return decision;
		}

		if (input.knownFullRasterDarknessRetention &&
			input.geometryClassification == ActivePictureClassification::FULL_RASTER_TRUSTED &&
			input.latestClassification != ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			decision.action = ScenePresentationAction::KEEP_CURRENT;
			decision.reason = "darkness-only boundary retains previously committed full raster";
			return decision;
		}
		if (input.latestClassification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED &&
			input.geometryClassification ==
				ActivePictureClassification::FULL_RASTER_TRUSTED)
		{
			decision.action = ScenePresentationAction::KEEP_CURRENT;
			decision.reason = "cut frame reaffirms full-raster presentation";
			return decision;
		}
		if (input.latestClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			input.geometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			input.latestObservationSupportsCrop)
		{
			decision.action = ScenePresentationAction::KEEP_CURRENT;
			decision.reason = "cut frame reaffirms current bar presentation";
			return decision;
		}
		if ((input.latestClassification ==
				ActivePictureClassification::PROVISIONAL ||
			 input.latestClassification ==
				ActivePictureClassification::UNAVAILABLE) &&
			input.geometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED &&
			input.existingCropCanBeSnapshotted)
		{
			if (input.frameLocalPresentationRetentionEvaluated)
			{
				if (input.frameLocalPresentationRetentionSafe)
				{
					decision.action = ScenePresentationAction::KEEP_CURRENT;
					decision.reason =
						"cut frame positively revalidates retained presentation pixels";
				}
				else if (input.currentOverlayEvidenceSupportsGeometry)
				{
					decision.action = ScenePresentationAction::KEEP_CURRENT;
					decision.reason =
						"cut frame's excluded-band pixels are covered by current overlay evidence";
				}
				else
				{
					decision.action =
						ScenePresentationAction::PRESERVE_REFERENCE;
					decision.reason =
						"cut frame expands presentation without replacing logical geometry";
				}
				return decision;
			}
			decision.action = ScenePresentationAction::HOLD_SNAPSHOT;
			decision.reason = input.latestClassification ==
				ActivePictureClassification::PROVISIONAL
				? "provisional cut retains bounded crop and NLS snapshot"
				: "near-black unavailable cut retains bounded crop and NLS snapshot";
			return decision;
		}
		if (input.geometryClassification ==
				ActivePictureClassification::BAR_CROP_TRUSTED)
		{
			decision.action = ScenePresentationAction::PRESERVE_REFERENCE;
			decision.reason =
				"cut changes current presentation without replacing logical geometry";
			return decision;
		}
		decision.reason = "cut frame contradicts or cannot verify presentation";
		return decision;
	}

	SceneHoldDecision EvaluateSceneHold(const SceneHoldInput& input)
	{
		SceneHoldDecision decision;
		const bool active = input.snapshotAvailable &&
			input.snapshotSourceGeneration != 0 &&
			input.snapshotSourceGeneration == input.frameSourceGeneration &&
			input.deadlineTick != 0 &&
			input.currentTick < input.deadlineTick;
		decision.cropActive = active;
		decision.nlsActive = active && input.nlsRequested &&
			input.retainedMappingCompatible;
		return decision;
	}
    GuardedSceneHoldDecision EvaluateGuardedSceneHold(const GuardedSceneHoldInput& input, bool committed)
    {
        GuardedSceneHoldDecision result;
        result.hold = input.hold;
        const auto& base = input.currentNativeBase;
        const auto sameReference = [](const ActivePictureBounds& a, const ActivePictureBounds& b)
        {
            return SameBounds(a, b) && a.trustedBarAxes == b.trustedBarAxes;
        };
        // Full raster is a legitimate native reference with no cropped axes.
        const bool fullRaster = base.left == 0 && base.top == 0 &&
            base.right == base.rasterWidth && base.bottom == base.rasterHeight;
        if (!input.certificateAvailable || input.cutOrDiscontinuity || input.hold.nlsActive ||
            !input.currentNativeAvailable || !input.currentGeneration ||
            !ValidBounds(base, base.rasterWidth, base.rasterHeight) ||
            (!fullRaster && !HasAuthorityForCroppedAxes(base, base.rasterWidth, base.rasterHeight)) ||
            !sameReference(base, input.nominationBase))
            return result;
        if (input.hold.cropActive && (!input.snapshotAvailable ||
            input.snapshotGeneration != input.currentGeneration ||
            !sameReference(input.snapshotBounds, base)))
            return result;
        // A hold bounds use of old geometry; it must not postpone verification
        // of fresh complete source-frame proof for that same reference.
        result.mayVerify = true;
        if (committed)
        {
            // The caller sets this only after current pixel/history validation
            // and ordinary presentation-owner admission actually commit.
            result.clearSnapshot = true;
            result.hold = {};
        }
        return result;
    }

}
