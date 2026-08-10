#include <pch.h>

#include "AlphaSourceCropPolicy.h"


namespace AlphaSourceCrop
{
	namespace
	{
		ActivePictureBounds FullRaster(int width, int height)
		{
			ActivePictureBounds bounds;
			bounds.right = width;
			bounds.bottom = height;
			bounds.rasterWidth = width;
			bounds.rasterHeight = height;
			bounds.aspectRatio = width > 0 && height > 0
				? static_cast<double>(width) / height : 0.0;
			bounds.symmetricBars = true;
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

		int ChromaAlignedDisplacement(int pixels)
		{
			if (pixels > 0)
				return (pixels + 1) & ~1;
			if (pixels < 0)
				return -(((-pixels) + 1) & ~1);
			return 0;
		}
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

	bool CanRetainVerticalBarPresentationAcrossAuthorityGap(
		const VerticalBarPresentationState& state,
		uint64_t evidenceSourceGeneration,
		uint64_t currentSourceGeneration,
		uint64_t currentTick, uint64_t holdMs,
		uint64_t currentSourceSequence)
	{
		return evidenceSourceGeneration != 0 &&
			evidenceSourceGeneration == currentSourceGeneration &&
			IsVerticalBarPresentationActive(state, currentTick, holdMs,
				currentSourceSequence);
	}

	bool CanAnalyzeHeldVerticalBarGeometry(
		const HeldBarAnalysisInput& input)
	{
		if (input.currentBarAuthority ||
			!input.trustedBarGeometryAvailable ||
			!input.storedBaseMatchesTrustedGeometry ||
			!input.currentEnvelopeAvailable ||
			input.latestClassification !=
				ActivePictureClassification::PROVISIONAL ||
			input.evidenceSourceGeneration == 0 ||
			input.evidenceSourceGeneration != input.currentSourceGeneration ||
			!IsVerticalBarPresentationActive(input.presentation,
				input.currentTick, input.holdMs,
				input.currentSourceSequence) ||
			input.presentation.action !=
				VerticalBarPresentationAction::TRANSLATE ||
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

		if (input.presentation.translationPixels < -0.5f)
			return input.currentEnvelope.top < input.trustedGeometry.top;
		if (input.presentation.translationPixels > 0.5f)
			return input.currentEnvelope.bottom > input.trustedGeometry.bottom;
		return false;
	}

	VerticalBarPresentationState UpdateVerticalBarPresentation(
		const VerticalBarPresentationUpdateInput& input)
	{
		const bool previousActive = IsVerticalBarPresentationActive(
			input.previous, input.currentTick, input.holdMs,
			input.currentSourceSequence);
		VerticalBarPresentationState state = previousActive
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
			if (previousActive &&
				state.action == VerticalBarPresentationAction::TRANSLATE &&
				input.translationEnabled)
			{
				return state;
			}
			const bool retainPrevious = previousActive;
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

		const bool sameDirection = previousActive &&
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
				decision.action = VerticalBarPresentationAction::FAIL_OPEN;
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
			// The dense bar pass has established this as a one-edge overlay. The
			// coarse envelope has enough information to extend that same edge, but
			// not enough specificity to manufacture an opposite picture edge. Its
			// previous opposite-edge veto turned a subtitle into an aspect fit and
			// caused the visible zoom-out. Genuine two-edge/picture evidence has
			// already selected FIT in the dense pass before reaching this resolver.
			if (requestedShift < 0 && input.genericUpperExpansion)
			{
				requestedShift = std::min(requestedShift,
					ChromaAlignedDisplacement(
						input.genericUpperBound - input.authoritativeTop));
			}
			else if (requestedShift > 0 && input.genericLowerExpansion)
			{
				requestedShift = std::max(requestedShift,
					ChromaAlignedDisplacement(
						input.genericLowerBound - input.authoritativeBottom));
			}

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
		if (input.genericVerticalFitConfirmed &&
			input.genericVerticalFitAuthoritative &&
			input.genericUpperExpansion && input.genericLowerExpansion)
			decision.action = VerticalBarPresentationAction::FIT;
		return decision;
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
		decision.bounds.symmetricBars = false;
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
		if (!input.latestObservationSupportsCrop &&
			!boundedSceneVerificationRetention &&
			!boundedAmbiguousRetention && !pixelSafeAmbiguousRetention &&
			!boundedOutwardExpansion && !boundedVerticalTranslation &&
			!boundedVerticalBaseRetention)
		{
			decision.reason =
				"latest observation does not reaffirm crop authority";
			return decision;
		}
		if (!input.geometry.symmetricBars)
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
		decision.reason = decision.verticallyTranslated
			? (decision.outwardExpanded
				? "bounded outward fit and same-size vertical translation accepted"
				: "same-size vertical presentation translation accepted")
			: (boundedVerticalBaseRetention
				? "subtitle release settled at current trusted base"
				: (decision.outwardExpanded
					? "bounded outward presentation expansion accepted"
					: (input.latestObservationSupportsCrop
			? "generation-current shared crop authority accepted"
			: (pixelSafeAmbiguousRetention
				? "frame-local pixel-safe presentation retained prior crop"
				: (boundedSceneVerificationRetention
					? "bounded scene verification retained current trusted crop"
					: "bounded ambiguity hold retained current trusted crop")))));
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
				else
				{
					decision.reason =
						"cut frame has visible pixels outside retained presentation";
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
		decision.reason = "cut frame contradicts or cannot verify presentation";
		return decision;
	}

	SceneHoldDecision EvaluateSceneHold(const SceneHoldInput& input)
	{
		SceneHoldDecision decision;
		const bool active = input.snapshotAvailable &&
			(!input.nlsRequested || input.retainedMappingCompatible) &&
			input.snapshotSourceGeneration != 0 &&
			input.snapshotSourceGeneration == input.frameSourceGeneration &&
			input.deadlineTick != 0 &&
			input.currentTick < input.deadlineTick;
		decision.cropActive = active;
		decision.nlsActive = active && input.nlsRequested;
		return decision;
	}
}
