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


double ResolveNlsTargetAspect(bool configuredTarget,
	double configuredAspect, double outputPanelAspect)
{
	const double selected = configuredTarget ?
		configuredAspect : outputPanelAspect;
	return std::isfinite(selected) && selected > 0.0 ? selected : 0.0;
}


NlsMappingDecision EvaluateNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, bool narrowerOnly, double maximumStretchRatio)
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
	if (narrowerOnly && signedDifferencePercent <= tolerancePercent)
	{
		decision.reason = "active picture is wider than the configured target";
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
