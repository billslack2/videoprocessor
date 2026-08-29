/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#include <pch.h>

#include "NlsGeometryPolicy.h"

#include <algorithm>
#include <cmath>
#include <sstream>


NlsSourceGeometry ResolveNlsSourceGeometry(bool trustedCropApplied,
	int cropLeft, int cropTop, int cropRight, int cropBottom,
	int rasterWidth, int rasterHeight)
{
	NlsSourceGeometry geometry;
	if (rasterWidth <= 0 || rasterHeight <= 0)
		return geometry;

	geometry.left = trustedCropApplied ? cropLeft : 0;
	geometry.top = trustedCropApplied ? cropTop : 0;
	geometry.right = trustedCropApplied ? cropRight : rasterWidth;
	geometry.bottom = trustedCropApplied ? cropBottom : rasterHeight;
	if (geometry.left < 0 || geometry.top < 0 ||
		geometry.right > rasterWidth || geometry.bottom > rasterHeight ||
		geometry.right <= geometry.left || geometry.bottom <= geometry.top)
		return {};

	geometry.aspect = static_cast<double>(geometry.right - geometry.left) /
		(geometry.bottom - geometry.top);
	geometry.valid = std::isfinite(geometry.aspect) && geometry.aspect > 0.0;
	return geometry;
}


double ResolveNlsTargetAspect(bool configuredTarget,
	double configuredAspect, double outputPanelAspect)
{
	const double selected = configuredTarget ?
		configuredAspect : outputPanelAspect;
	return std::isfinite(selected) && selected > 0.0 ? selected : 0.0;
}


NlsMappingDecision EvaluateNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, NlsAspectDirection direction,
	double maximumStretchRatio)
{
	NlsMappingDecision decision;
	decision.sourceAspect = activeAspect;
	decision.targetAspect = targetAspect;
	decision.maximumRatio = maximumStretchRatio;
	if (!std::isfinite(maximumStretchRatio) ||
		maximumStretchRatio < NLS_MINIMUM_STRETCH_RATIO ||
		maximumStretchRatio > NLS_SHADER_MAXIMUM_STRETCH_RATIO)
	{
		decision.reason = "NLS maximum stretch ratio is invalid";
		return decision;
	}
	if (!aspectAvailable || !std::isfinite(activeAspect) || activeAspect <= 0.0)
	{
		decision.reason = "active picture geometry is not stable";
		return decision;
	}
	if (!std::isfinite(targetAspect) || targetAspect <= 0.0)
	{
		decision.reason = "NLS target aspect is unavailable";
		return decision;
	}
	if (activeAspectMinimum > 0.0 && activeAspect < activeAspectMinimum)
	{
		std::ostringstream message;
		message << "active picture " << activeAspect <<
			" is below minimum " << activeAspectMinimum;
		decision.reason = message.str();
		return decision;
	}

	decision.requestedRatio = std::max(targetAspect / activeAspect,
		activeAspect / targetAspect);
	if (!std::isfinite(decision.requestedRatio))
	{
		std::ostringstream message;
		message << "NLS ratio " << decision.requestedRatio << " is invalid";
		decision.reason = message.str();
		return decision;
	}

	const double signedDifferencePercent =
		(targetAspect - activeAspect) * 100.0 / targetAspect;
	if (std::abs(signedDifferencePercent) <= tolerancePercent)
	{
		decision.mode = NlsMappingMode::LINEAR_PASSTHROUGH;
		decision.reason = "active picture matches the target within tolerance";
		return decision;
	}
	if (direction == NlsAspectDirection::NARROWER_ONLY &&
		signedDifferencePercent <= tolerancePercent)
	{
		decision.mode = NlsMappingMode::LINEAR_PASSTHROUGH;
		decision.reason =
			"active picture is wider than the configured target; preserving source geometry";
		return decision;
	}
	if (direction == NlsAspectDirection::WIDER_ONLY &&
		signedDifferencePercent >= -tolerancePercent)
	{
		decision.mode = NlsMappingMode::LINEAR_PASSTHROUGH;
		decision.reason =
			"active picture is narrower than the configured target; preserving source geometry";
		return decision;
	}
	if (decision.requestedRatio > maximumStretchRatio)
	{
		decision.mode = NlsMappingMode::SAFE_FIT;
		decision.safeFitVertical = activeAspect > targetAspect;
		decision.safeFitFraction =
			std::min(activeAspect, targetAspect) /
			std::max(activeAspect, targetAspect);
		std::ostringstream message;
		message << "NLS ratio " << decision.requestedRatio <<
			" exceeds configured maximum " << maximumStretchRatio <<
			"; preserving source geometry with " <<
			(decision.safeFitVertical ? "letterbox" : "pillarbox") <<
			" safe fit";
		decision.reason = message.str();
		return decision;
	}

	decision.mode = NlsMappingMode::ACTIVE;
	decision.stretchRatio = decision.requestedRatio;
	decision.verticalWarp = activeAspect > targetAspect;
	decision.reason = decision.verticalWarp ?
		"active picture is wider than the target" :
		"active picture is narrower than the target";
	return decision;
}


NlsSourceGeometry ResolveNlsPresentationSourceGeometry(bool nlsRequested,
	bool activePictureAvailable,
	int activeLeft, int activeTop, int activeRight, int activeBottom,
	bool viewportCropApplied,
	int viewportLeft, int viewportTop, int viewportRight, int viewportBottom,
	int rasterWidth, int rasterHeight)
{
	if (viewportCropApplied)
	{
		const NlsSourceGeometry viewport = ResolveNlsSourceGeometry(true,
			viewportLeft, viewportTop, viewportRight, viewportBottom,
			rasterWidth, rasterHeight);
		if (viewport.valid)
			return viewport;
	}
	if (nlsRequested && activePictureAvailable)
	{
		const NlsSourceGeometry activePicture = ResolveNlsSourceGeometry(true,
			activeLeft, activeTop, activeRight, activeBottom,
			rasterWidth, rasterHeight);
		if (activePicture.valid)
			return activePicture;
	}
	return ResolveNlsSourceGeometry(false,
		viewportLeft, viewportTop, viewportRight, viewportBottom,
		rasterWidth, rasterHeight);
}


bool NlsOwnsPresentationGeometry(bool nlsRequested,
	bool presentationFailOpen, bool activePictureAvailable,
	bool viewportCropApplied, NlsMappingMode mappingMode,
	bool presentationCropApplied)
{
	if (!nlsRequested || presentationFailOpen)
		return false;
	if (presentationCropApplied || mappingMode == NlsMappingMode::ACTIVE ||
		mappingMode == NlsMappingMode::SAFE_FIT)
		return true;
	return mappingMode == NlsMappingMode::LINEAR_PASSTHROUGH &&
		activePictureAvailable && !viewportCropApplied;
}


NlsPresentationCropDecision ResolveNlsPresentationCrop(
	const NlsSourceGeometry& source, double targetAspect,
	double tolerancePercent, double activeAspectMinimum,
	NlsAspectDirection direction, double maximumStretchRatio,
	double maximumCropPercent,
	NlsPresentationCropPreference preference)
{
	NlsPresentationCropDecision result;
	result.source = source;
	if (!source.valid)
	{
		result.reason = "NLS presentation crop requires valid source geometry";
		return result;
	}
	if (!std::isfinite(maximumCropPercent) || maximumCropPercent < 0.0 ||
		maximumCropPercent > 10.0)
	{
		result.reason = "NLS presentation crop limit is invalid";
		return result;
	}
	if (maximumCropPercent <= 0.0)
	{
		result.reason = "NLS presentation crop is disabled";
		return result;
	}

	const NlsMappingDecision original = EvaluateNlsMapping(true,
		source.aspect, targetAspect, tolerancePercent, activeAspectMinimum,
		direction, maximumStretchRatio);
	if (original.mode == NlsMappingMode::WAITING ||
		original.mode == NlsMappingMode::LINEAR_PASSTHROUGH)
	{
		result.reason = "NLS presentation crop is not applicable to the mapping";
		return result;
	}
	if (preference == NlsPresentationCropPreference::PRESERVE_IMAGE &&
		original.mode != NlsMappingMode::SAFE_FIT)
	{
		result.reason = "NLS mapping is already within the configured stretch limit";
		return result;
	}

	const bool sourceWider = source.aspect > targetAspect;
	const int extent = sourceWider ?
		(source.right - source.left) : (source.bottom - source.top);
	if (extent <= 2)
	{
		result.reason = "NLS presentation crop has no usable source extent";
		return result;
	}

	const double maximumFraction = maximumCropPercent / 100.0;
	const int maximumPixels = static_cast<int>(std::floor(
		maximumFraction * extent + 1.0e-9));
	const double targetFraction = std::max(0.0,
		(1.0 - 1.0 / original.requestedRatio) * 0.5);
	const int targetPixels = static_cast<int>(std::floor(
		targetFraction * extent + 1.0e-9));
	int cropPixels = 0;

	if (preference == NlsPresentationCropPreference::PRESERVE_IMAGE)
	{
		const double requiredFraction = std::max(0.0,
			(1.0 - maximumStretchRatio / original.requestedRatio) * 0.5);
		cropPixels = static_cast<int>(std::ceil(
			requiredFraction * extent - 1.0e-9));
		if (cropPixels <= 0 || cropPixels > maximumPixels ||
			cropPixels > targetPixels)
		{
			result.reason =
				"configured presentation crop cannot satisfy the NLS stretch limit";
			return result;
		}
	}
	else
	{
		cropPixels = std::min(maximumPixels, targetPixels);
		if (cropPixels <= 0)
		{
			result.reason = "configured presentation crop rounds to zero pixels";
			return result;
		}
	}

	NlsSourceGeometry candidate = source;
	if (sourceWider)
	{
		candidate.left += cropPixels;
		candidate.right -= cropPixels;
	}
	else
	{
		candidate.top += cropPixels;
		candidate.bottom -= cropPixels;
	}
	if (candidate.right <= candidate.left ||
		candidate.bottom <= candidate.top)
	{
		result.reason = "NLS presentation crop produced empty source geometry";
		return result;
	}
	candidate.aspect = static_cast<double>(candidate.right - candidate.left) /
		(candidate.bottom - candidate.top);
	candidate.valid = std::isfinite(candidate.aspect) &&
		candidate.aspect > 0.0;
	const NlsMappingDecision cropped = EvaluateNlsMapping(candidate.valid,
		candidate.aspect, targetAspect, tolerancePercent, activeAspectMinimum,
		direction, maximumStretchRatio);
	if (cropped.mode != NlsMappingMode::ACTIVE &&
		cropped.mode != NlsMappingMode::LINEAR_PASSTHROUGH)
	{
		result.reason =
			"bounded presentation crop cannot produce a safe NLS mapping";
		return result;
	}

	result.source = candidate;
	result.applied = true;
	result.croppedLeftRight = sourceWider;
	result.pixelsPerEdge = cropPixels;
	result.percentPerEdge = 100.0 * cropPixels / extent;
	std::ostringstream message;
	message << "cropped " << cropPixels << " pixels (" <<
		result.percentPerEdge << " percent) from each " <<
		(sourceWider ? "side" : "top/bottom edge") << " for " <<
		NlsPresentationCropPreferenceName(preference);
	result.reason = message.str();
	return result;
}


const char* NlsMappingModeName(NlsMappingMode mode)
{
	switch (mode)
	{
	case NlsMappingMode::OFF:
		return "off";
	case NlsMappingMode::WAITING:
		return "waiting";
	case NlsMappingMode::LINEAR_PASSTHROUGH:
		return "passthrough";
	case NlsMappingMode::ACTIVE:
		return "active";
	case NlsMappingMode::SAFE_FIT:
		return "safe_fit";
	default:
		return "unknown";
	}
}


const char* NlsMappingAxisName(const NlsMappingDecision& decision)
{
	if (decision.mode == NlsMappingMode::ACTIVE)
		return decision.verticalWarp ? "vertical" : "horizontal";
	if (decision.mode == NlsMappingMode::SAFE_FIT)
		return decision.safeFitVertical ? "vertical" : "horizontal";
	return "none";
}


const char* NlsAspectDirectionName(NlsAspectDirection direction)
{
	switch (direction)
	{
	case NlsAspectDirection::NARROWER_ONLY:
		return "narrower_only";
	case NlsAspectDirection::WIDER_ONLY:
		return "wider_only";
	case NlsAspectDirection::ANY:
		return "any";
	default:
		return "unknown";
	}
}


const char* NlsPresentationCropPreferenceName(
	NlsPresentationCropPreference preference)
{
	switch (preference)
	{
	case NlsPresentationCropPreference::PRESERVE_IMAGE:
		return "preserve_image";
	case NlsPresentationCropPreference::MINIMIZE_DISTORTION:
		return "minimize_distortion";
	default:
		return "unknown";
	}
}
