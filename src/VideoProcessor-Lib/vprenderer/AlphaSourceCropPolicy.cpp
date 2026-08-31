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
		}

		void ResetNearBlackBootstrap(
			NearBlackPresentationEpisodeState& state)
		{
			state.bootstrapCandidateAvailable = false;
			state.bootstrapCandidate = {};
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

	bool ShouldSuppressNearBlackBarGeometryMutation(bool acquisitionBlocked,
		bool stable, ActivePictureClassification classification)
	{
		return acquisitionBlocked && stable && classification ==
			ActivePictureClassification::BAR_CROP_TRUSTED;
	}

	BarContentEdge SelectVerticalBarContentEdge(
		float upperRequiredShift, float lowerRequiredShift)
	{
		if (upperRequiredShift > lowerRequiredShift &&
			upperRequiredShift > 0.5f)
			return BarContentEdge::TOP;
		if (lowerRequiredShift > 0.5f)
			return BarContentEdge::BOTTOM;
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
				decision.upperOverlayLike) ||
			(input.lowerContent && !input.upperContent &&
				input.topTranslationHeld &&
				decision.lowerOverlayLike);
		// Content on both bars is not, by itself, an aspect-ratio change. A
		// bottom subtitle can coexist with a top volume/menu overlay, and sparse
		// details near both edges can occur in the same frame. Only picture-like
		// (broad and deep) evidence may turn a two-edge observation into Fit.
		// When both edges are overlay-like, select the edge needing the larger
		// reveal below while preserving any active translation hold.
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
		// When the held edge is still present, a simultaneous overlay on the
		// opposite edge must not reverse the picture translation. Continue to
		// follow the held edge, including an immediately larger reveal requested
		// by a new subtitle line. If the held edge has disappeared, the bounded
		// hold above suppresses the isolated opposite-edge pulse instead.
		if (input.bottomTranslationHeld && decision.lowerOverlayLike)
			upperCandidate = 0.0f;
		else if (input.topTranslationHeld && decision.upperOverlayLike)
			lowerCandidate = 0.0f;

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
			candidate.left <= trustedGeometry.left &&
			candidate.top <= trustedGeometry.top &&
			candidate.right >= trustedGeometry.right &&
			candidate.bottom >= trustedGeometry.bottom &&
			!SameBounds(candidate, trustedGeometry);
		if (!compatible)
			return decision;

		decision.outwardTransition = true;
		const bool expandsVertical = candidate.top < trustedGeometry.top ||
			candidate.bottom > trustedGeometry.bottom;
		const bool expandsHorizontal = candidate.left < trustedGeometry.left ||
			candidate.right > trustedGeometry.right;
		const bool verticalPicture = !expandsVertical ||
			((trustedGeometry.top == 0 || BroadPictureLike(evidence.excludedTop)) &&
			 (trustedGeometry.bottom == trustedGeometry.rasterHeight ||
				BroadPictureLike(evidence.excludedBottom)));
		const bool horizontalPicture = !expandsHorizontal ||
			((trustedGeometry.left == 0 || BroadPictureLike(evidence.excludedLeft)) &&
			 (trustedGeometry.right == trustedGeometry.rasterWidth ||
				BroadPictureLike(evidence.excludedRight)));
		decision.broadOpposingPicture = verticalPicture && horizontalPicture;
		if (!decision.broadOpposingPicture)
			return decision;

		const bool continues = previous.sourceGeneration == sourceGeneration &&
			previous.confirmations != 0 && SameBounds(previous.candidate, candidate);
		const bool repeatedSourceSample = continues && sourceSequence != 0 &&
			previous.lastObservedSourceSequence == sourceSequence;
		decision.state.candidate = candidate;
		decision.state.sourceGeneration = sourceGeneration;
		decision.state.lastObservedSourceSequence = sourceSequence;
		decision.state.confirmations = repeatedSourceSample
			? previous.confirmations : (continues
			? std::min(OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED,
				previous.confirmations + 1) : 1);
		decision.authoritative = decision.state.confirmations >=
			OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED;
		return decision;
	}

	VerticalFitConfirmationDecision ConfirmVerticalFit(
		const VerticalFitConfirmationState& previous,
		const VerticalBarContentDecision& observed,
		uint64_t sourceSequence)
	{
		VerticalFitConfirmationDecision decision;
		decision.effective = observed;
		if (observed.action != VerticalBarPresentationAction::FIT)
			return decision;
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
			input.confirmedVerticalFitResolved ||
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

	VerticalBarPresentationState UpdateVerticalBarPresentation(
		const VerticalBarPresentationUpdateInput& input)
	{
		const bool previousActiveByHold = IsVerticalBarPresentationActive(
			input.previous, input.currentTick, input.holdMs,
			input.currentSourceSequence);
		const bool previousOwnsSample = previousActiveByHold ||
			input.previousOwnsCurrentAnalysis;
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

		if (current.action == VerticalBarPresentationAction::FIT)
		{
			if (previousOwnsSample &&
				state.action == VerticalBarPresentationAction::TRANSLATE &&
				input.translationEnabled)
			{
				return state;
			}
			const bool retainPrevious = previousOwnsSample;
			const int retainedTop = retainPrevious ? state.detectedTop : 0;
			const int retainedBottom = retainPrevious ? state.detectedBottom : 0;
			state.action = current.action;
			state.translationPixels = 0.0f;
			state.detectedTop = input.upperContent
				? (retainedTop > 0
					? std::min(retainedTop, input.upperContentTop)
					: input.upperContentTop)
				: retainedTop;
			state.detectedBottom = input.lowerContent
				? std::max(retainedBottom, input.lowerContentBottom)
				: retainedBottom;
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

	bool UpdateFullRasterPresentationAuthority(bool previouslyAuthoritative,
		ActivePictureClassification currentClassification,
		bool currentBoundsAreFullRaster)
	{
		if (currentClassification ==
			ActivePictureClassification::FULL_RASTER_TRUSTED)
			return currentBoundsAreFullRaster;
		if (currentClassification ==
			ActivePictureClassification::BAR_CROP_TRUSTED)
			return false;
		return previouslyAuthoritative;
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
		decision.reason = "content already matches the screen aspect";
		return decision;
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
			!decision.state.entryTrustedCropAvailable &&
			decision.state.sourceGeneration == input.sourceGeneration &&
			decision.state.presentationEpoch != input.presentationEpoch)
		{
			// A live profile/viewport boundary invalidates every certificate from
			// the old presentation epoch, but it must not strand the episode there.
			// Preserve the conservative presentation choice (and any confirmed
			// outward-content latch), then restart only the partial proof on the
			// current epoch. Fresh current-frame pixels are still required below.
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

		if (input.sceneBoundary &&
			decision.state.mode != NearBlackPresentationMode::INACTIVE)
		{
			decision.state = {};
			decision.ended = true;
			decision.reason = "scene boundary ended near-black title episode";
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

		if (input.measurementCurrent && input.nearBlackEvaluated &&
			input.globalNearBlack &&
			decision.state.mode == NearBlackPresentationMode::INACTIVE)
		{
			decision.state.mode = input.trustedCropAvailable &&
				!input.boundedVisibleContentOutsideCrop
				? NearBlackPresentationMode::RETAIN_CROP
				: NearBlackPresentationMode::FULL_RASTER;
			decision.state.sourceGeneration = input.sourceGeneration;
			decision.state.startedSourceSequence = input.sourceSequence;
			decision.state.presentationEpoch = input.presentationEpoch;
			decision.state.entryTrustedCropAvailable =
				decision.state.mode == NearBlackPresentationMode::RETAIN_CROP;
			if (decision.state.entryTrustedCropAvailable)
				decision.state.entryTrustedCrop = input.trustedCrop;
			if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER)
				decision.state.fullRasterStartedSourceSequence =
					input.sourceSequence;
			decision.started = true;
			decision.reason = decision.state.mode ==
				NearBlackPresentationMode::RETAIN_CROP
				? "near-black title episode retained current crop"
				: "near-black title episode started at full raster";
		}

		if (decision.state.mode == NearBlackPresentationMode::RETAIN_CROP &&
			input.measurementCurrent && input.boundedVisibleContentOutsideCrop)
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
			!input.trustedCropAvailable)
		{
			decision.state.mode = NearBlackPresentationMode::FULL_RASTER;
			decision.state.fullRasterStartedSourceSequence =
				input.sourceSequence;
			decision.changedToFullRaster = true;
			decision.reason =
				"lost retained title geometry latched full raster for episode";
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

		if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			decision.state.entryTrustedCropAvailable && !input.cadenceRepeat &&
			!decision.state.confirmedNonNearBlackContent &&
			input.sourceSequence !=
				decision.state.revalidationLastSourceSequence)
		{
			const bool exactEntryContract =
				input.knownTrustedGeometryReacquired &&
				input.reacquiredTrustedClassification ==
					ActivePictureClassification::BAR_CROP_TRUSTED &&
				input.reacquisitionIsCurrentAssociation &&
				input.reacquiredSourceGeneration == input.sourceGeneration &&
				input.reacquiredSourceSequence >=
					decision.state.fullRasterStartedSourceSequence &&
				input.reacquiredSourceSequence <= input.sourceSequence &&
				input.reacquiredPresentationEpoch ==
					decision.state.presentationEpoch &&
				SameTrustedCropContract(input.reacquiredTrustedGeometry,
					decision.state.entryTrustedCrop);
			const bool exactCurrentObservation =
				input.currentObservationAvailable &&
				SameBounds(input.currentObservation,
					decision.state.entryTrustedCrop);
			const bool exactCurrentSafety = input.retentionEvaluated &&
				input.retentionSafe &&
				input.retentionSourceSequence == input.sourceSequence &&
				SameBounds(input.retentionBounds,
					decision.state.entryTrustedCrop);
			const bool qualifies = input.measurementCurrent &&
				input.nearBlackEvaluated && !input.globalNearBlack &&
				!input.boundedVisibleContentOutsideCrop &&
				!input.fullRasterAuthorityAvailable &&
				input.presentationEpoch == decision.state.presentationEpoch &&
				exactEntryContract && exactCurrentObservation &&
				exactCurrentSafety;

			if (qualifies)
			{
				if (decision.state.revalidationLastSourceSequence != 0 &&
					input.sourceSequence ==
						decision.state.revalidationLastSourceSequence + 1)
				{
					++decision.state.revalidationSamples;
				}
				else
				{
					decision.state.revalidationStartedSourceSequence =
						input.sourceSequence;
					decision.state.revalidationSamples = 1;
				}
				decision.state.revalidationLastSourceSequence =
					input.sourceSequence;
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
				decision.state = {};
				decision.releasedToTrustedCrop = true;
				decision.ended = true;
				decision.reason =
					"exact entry crop revalidated after pixel-safe dwell";
			}
		}

		if (decision.state.mode == NearBlackPresentationMode::FULL_RASTER &&
			!decision.state.entryTrustedCropAvailable)
		{
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
				decision.ended = true;
				decision.reason =
					"startup crop bootstrap verified; normal acquisition reopened";
			}
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
		if (input.nearBlackEpisodeFullRaster)
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
		if (input.barCropRefinementHorizontalConflict)
		{
			decision.reason =
				"horizontal expansion requires full-raster fail-open";
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
			!boundedSceneVerificationRetention &&
			!boundedAmbiguousRetention && !pixelSafeAmbiguousRetention &&
			!nearBlackEpisodeRetention &&
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
		if (decision.verticallyTranslated)
		{
			decision.owner = DecisionOwner::VERTICAL_TRANSLATION;
			decision.reason = decision.outwardExpanded
				? "bounded outward fit and same-size vertical translation accepted"
				: "same-size vertical presentation translation accepted";
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
}
