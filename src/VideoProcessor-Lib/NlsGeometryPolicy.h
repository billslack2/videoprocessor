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


double ResolveNlsTargetAspect(bool configuredTarget,
	double configuredAspect, double outputPanelAspect);

NlsMappingDecision EvaluateNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, bool narrowerOnly,
	double maximumStretchRatio = NLS_DEFAULT_MAXIMUM_STRETCH_RATIO);

const char* NlsMappingModeName(NlsMappingMode mode);
const char* NlsMappingAxisName(const NlsMappingDecision& decision);
