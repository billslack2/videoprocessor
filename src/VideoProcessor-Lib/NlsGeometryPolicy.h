/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <string>


constexpr double NLS_MINIMUM_STRETCH_RATIO = 1.0;
constexpr double NLS_DEFAULT_MAXIMUM_STRETCH_RATIO = 1.4;
// Both shipped shaders clamp to this value as a final coordinate-safety guard.
constexpr double NLS_SHADER_MAXIMUM_STRETCH_RATIO = 1.5;


enum class NlsMappingMode
{
	OFF,
	WAITING,
	LINEAR_PASSTHROUGH,
	ACTIVE,
	SAFE_FIT
};


enum class NlsAspectDirection
{
	NARROWER_ONLY,
	WIDER_ONLY,
	ANY
};


enum class NlsPresentationCropPreference
{
	PRESERVE_IMAGE,
	MINIMIZE_DISTORTION
};


struct NlsMappingDecision
{
	NlsMappingMode mode = NlsMappingMode::WAITING;
	double sourceAspect = 0.0;
	double targetAspect = 0.0;
	double requestedRatio = 1.0;
	double maximumRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO;
	double stretchRatio = 1.0;
	bool verticalWarp = false;
	double safeFitFraction = 1.0;
	bool safeFitVertical = false;
	std::string reason;
};


struct NlsSourceGeometry
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
	double aspect = 0.0;
	bool valid = false;
};


struct NlsPresentationCropDecision
{
	NlsSourceGeometry source;
	bool applied = false;
	bool croppedLeftRight = false;
	int pixelsPerEdge = 0;
	double percentPerEdge = 0.0;
	std::string reason;
};


NlsSourceGeometry ResolveNlsSourceGeometry(bool trustedCropApplied,
	int cropLeft, int cropTop, int cropRight, int cropBottom,
	int rasterWidth, int rasterHeight);


double ResolveNlsTargetAspect(bool configuredTarget,
	double configuredAspect, double outputPanelAspect);

NlsMappingDecision EvaluateNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, NlsAspectDirection direction,
	double maximumStretchRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO);

// Preserve the established Boolean call contract for existing callers and
// tests while production configuration moves to the explicit direction enum.
inline NlsMappingDecision EvaluateNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, bool narrowerOnly,
	double maximumStretchRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO)
{
	return EvaluateNlsMapping(aspectAvailable, activeAspect, targetAspect,
		tolerancePercent, activeAspectMinimum,
		narrowerOnly ? NlsAspectDirection::NARROWER_ONLY :
			NlsAspectDirection::ANY,
		maximumStretchRatio);
}

NlsPresentationCropDecision ResolveNlsPresentationCrop(
	const NlsSourceGeometry& source, double targetAspect,
	double tolerancePercent, double activeAspectMinimum,
	NlsAspectDirection direction, double maximumStretchRatio,
	double maximumCropPercent,
	NlsPresentationCropPreference preference);

const char* NlsMappingModeName(NlsMappingMode mode);
const char* NlsMappingAxisName(const NlsMappingDecision& decision);
const char* NlsAspectDirectionName(NlsAspectDirection direction);
const char* NlsPresentationCropPreferenceName(
	NlsPresentationCropPreference preference);
