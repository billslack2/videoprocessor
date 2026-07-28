/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#include <pch.h>

#include "MadVRShaderRuntimeState.h"

#include <algorithm>
#include <cmath>
#include <sstream>


MadVRNlsMappingDecision EvaluateMadVRNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, bool narrowerOnly, double maximumStretchRatio)
{
	MadVRNlsMappingDecision decision;
	decision.sourceAspect = activeAspect;
	decision.targetAspect = targetAspect;
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

	const double ratio = std::max(targetAspect / activeAspect,
		activeAspect / targetAspect);
	if (!std::isfinite(ratio))
	{
		std::ostringstream message;
		message << "NLS ratio " << ratio << " is invalid";
		decision.reason = message.str();
		return decision;
	}

	const double signedDifferencePercent =
		(targetAspect - activeAspect) * 100.0 / targetAspect;
	if (std::abs(signedDifferencePercent) <= tolerancePercent)
	{
		decision.mode = MadVRNlsMappingMode::SCOPE_PASSTHROUGH;
		decision.reason = "active picture matches the target within tolerance";
		return decision;
	}
	if (narrowerOnly && signedDifferencePercent <= tolerancePercent)
	{
		decision.reason = "active picture is wider than the configured target";
		return decision;
	}
	if (ratio > maximumStretchRatio)
	{
		// Excessive nonlinear expansion is visually destructive. Keep the
		// complete active picture at its original geometry inside the selected
		// viewport instead. Narrow content receives side pillars; wider content
		// receives top and bottom bars.
		decision.mode = MadVRNlsMappingMode::SAFE_FIT;
		decision.safeFitVertical = activeAspect > targetAspect;
		decision.safeFitFraction =
			std::min(activeAspect, targetAspect) /
			std::max(activeAspect, targetAspect);
		std::ostringstream message;
		message << "NLS ratio " << ratio << " exceeds the safe " <<
			maximumStretchRatio << " limit; preserving source geometry with " <<
			(decision.safeFitVertical ? "letterbox" : "pillarbox") <<
			" safe fit";
		decision.reason = message.str();
		return decision;
	}

	decision.mode = MadVRNlsMappingMode::ACTIVE;
	decision.stretchRatio = ratio;
	decision.verticalWarp = activeAspect > targetAspect;
	decision.reason = decision.verticalWarp ?
		"active picture is wider than the target" :
		"active picture is narrower than the target";
	return decision;
}


bool ResolveMadVRNlsOutputAspect(double targetAspect,
	unsigned long& aspectX, unsigned long& aspectY)
{
	aspectX = 0;
	aspectY = 0;
	if (!std::isfinite(targetAspect) || targetAspect <= 0.0)
		return false;
	if (std::abs(targetAspect - 16.0 / 9.0) < 0.0001)
	{
		aspectX = 16;
		aspectY = 9;
		return true;
	}
	if (std::abs(targetAspect - 2.35) < 0.0001)
	{
		aspectX = 235;
		aspectY = 100;
		return true;
	}

	// Preserve arbitrary viewport contracts without teaching the shader layer
	// about every possible screen shape. Four decimal places is substantially
	// tighter than madVR's media-type aspect comparison tolerance.
	unsigned long numerator = static_cast<unsigned long>(
		std::llround(targetAspect * 10000.0));
	unsigned long denominator = 10000;
	if (numerator == 0)
		return false;
	unsigned long a = numerator;
	unsigned long b = denominator;
	while (b != 0)
	{
		const unsigned long remainder = a % b;
		a = b;
		b = remainder;
	}
	aspectX = numerator / a;
	aspectY = denominator / a;
	return true;
}


bool MadVROutputAspectRequiresRestart(unsigned long currentAspectX,
	unsigned long currentAspectY, unsigned long desiredAspectX,
	unsigned long desiredAspectY, double nativeAspect)
{
	if (!std::isfinite(nativeAspect) || nativeAspect <= 0.0)
	{
		return desiredAspectX != currentAspectX ||
			desiredAspectY != currentAspectY;
	}
	const auto effectiveAspect = [nativeAspect](
		unsigned long aspectX, unsigned long aspectY)
	{
		return aspectX > 0 && aspectY > 0 ?
			static_cast<double>(aspectX) / aspectY : nativeAspect;
	};
	return std::abs(effectiveAspect(desiredAspectX, desiredAspectY) -
		effectiveAspect(currentAspectX, currentAspectY)) > 0.0001;
}


const char* MadVRNlsMappingModeName(MadVRNlsMappingMode mode)
{
	switch (mode)
	{
	case MadVRNlsMappingMode::OFF:
		return "off";
	case MadVRNlsMappingMode::WAITING:
		return "waiting";
	case MadVRNlsMappingMode::SCOPE_PASSTHROUGH:
		return "scope_passthrough";
	case MadVRNlsMappingMode::ACTIVE:
		return "active";
	case MadVRNlsMappingMode::SAFE_FIT:
		return "safe_fit";
	default:
		return "unknown";
	}
}


MadVRShaderRuntimeSnapshot MadVRShaderRuntimeState::GetSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_state;
}


uint64_t MadVRShaderRuntimeState::BeginRendererGeneration()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	++m_state.rendererGeneration;
	m_state.activeGeometry = {};
	// SAFE_FIT is the one mapping that can be reconstructed without trusting
	// stale crop coordinates: ResolveNlsRuleForFrame derives a centered active
	// rectangle from the confirmed source aspect and the new raster. Preserve
	// that mode across renderer replacement so the new renderer never exposes
	// the target output aspect without its geometry-preserving fit.
	if (m_state.nlsMode != MadVRNlsMappingMode::OFF &&
		m_state.nlsMode != MadVRNlsMappingMode::SAFE_FIT)
		m_state.nlsMode = MadVRNlsMappingMode::WAITING;
	return m_state.rendererGeneration;
}


void MadVRShaderRuntimeState::SetRuleSelection(
	const std::string& requestedRule, const std::string& effectiveRule,
	MadVRNlsMappingMode nlsMode)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.requestedRule = requestedRule;
	m_state.effectiveRule = effectiveRule;
	m_state.nlsMode = nlsMode;
	if (nlsMode == MadVRNlsMappingMode::ACTIVE ||
		nlsMode == MadVRNlsMappingMode::SCOPE_PASSTHROUGH ||
		nlsMode == MadVRNlsMappingMode::SAFE_FIT)
	{
		m_state.lastSafeNlsMode = nlsMode;
	}
	else if (nlsMode == MadVRNlsMappingMode::OFF)
	{
		m_state.lastSafeNlsMode = MadVRNlsMappingMode::OFF;
		m_state.activeGeometry = {};
	}
	else
	{
		m_state.activeGeometry = {};
	}
}


void MadVRShaderRuntimeState::SetRequestedRule(
	const std::string& requestedRule)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.requestedRule = requestedRule;
}


void MadVRShaderRuntimeState::SetEffectiveRule(
	const std::string& effectiveRule)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.effectiveRule = effectiveRule;
}


void MadVRShaderRuntimeState::SetNlsTargetAspect(double targetAspect)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.nlsTargetAspect = std::isfinite(targetAspect) &&
		targetAspect >= 1.0 && targetAspect <= 4.0 ? targetAspect : 0.0;
}


void MadVRShaderRuntimeState::SetNlsDecision(
	const MadVRNlsMappingDecision& decision)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.nlsDecision = decision;
}


bool MadVRShaderRuntimeState::SetActiveGeometry(
	const MadVRActivePictureGeometry& geometry)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const bool valid = geometry.stable &&
		geometry.rendererGeneration == m_state.rendererGeneration &&
		std::isfinite(geometry.aspectRatio) && geometry.aspectRatio > 0.0 &&
		geometry.left >= 0.0 && geometry.top >= 0.0 &&
		geometry.right <= 1.0 && geometry.bottom <= 1.0 &&
		geometry.right > geometry.left && geometry.bottom > geometry.top;
	if (!valid)
		return false;
	m_state.activeGeometry = geometry;
	return true;
}
