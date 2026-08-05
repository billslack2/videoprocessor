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
		const bool boundedAmbiguousRetention = ambiguousObservation &&
			!input.frameLocalPresentationRetentionEvaluated &&
			(input.sceneVerificationHoldActive || input.ambiguityHoldActive);
		const bool pixelSafeAmbiguousRetention = ambiguousObservation &&
			input.frameLocalPresentationRetentionSafe;
		const bool boundedOutwardExpansion =
			input.outwardPresentationActive &&
			input.outwardExpansionAvailable &&
			input.outwardExpansionSourceGeneration != 0 &&
			input.outwardExpansionSourceGeneration ==
				input.frameSourceGeneration;
		if (!input.latestObservationSupportsCrop &&
			!boundedAmbiguousRetention && !pixelSafeAmbiguousRetention &&
			!boundedOutwardExpansion)
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

			decision.sourceBounds = input.outwardExpansion;
			decision.applyCrop = true;
			decision.outwardExpanded = true;
			decision.reason =
				"bounded outward presentation expansion accepted";
			return decision;
		}

		decision.sourceBounds = input.geometry;
		decision.applyCrop = true;
		decision.reason = input.latestObservationSupportsCrop
			? "generation-current shared crop authority accepted"
			: (pixelSafeAmbiguousRetention
				? "frame-local pixel-safe presentation retained prior crop"
				: (input.sceneVerificationHoldActive
				? "bounded scene verification retained current trusted crop"
				: "bounded ambiguity hold retained current trusted crop"));
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
