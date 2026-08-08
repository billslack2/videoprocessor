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


bool ResolveMadVRNlsOutputAspect(double targetAspect,
	unsigned long& aspectX, unsigned long& aspectY)
{
	aspectX = 0;
	aspectY = 0;
	if (!std::isfinite(targetAspect) || targetAspect <= 0.0)
		return false;
	// Find the closest small rational without teaching the shader layer about
	// named or conventional screen shapes. Exact ratios such as 16:9 and 47:20
	// naturally resolve to their canonical representation.
	double bestError = (std::numeric_limits<double>::max)();
	for (unsigned long denominator = 1; denominator <= 10000; ++denominator)
	{
		const unsigned long numerator = static_cast<unsigned long>(
			std::llround(targetAspect * denominator));
		if (numerator == 0)
			continue;
		const double error = std::abs(
			static_cast<double>(numerator) / denominator - targetAspect);
		if (error < bestError)
		{
			bestError = error;
			aspectX = numerator;
			aspectY = denominator;
			if (error < 1e-12)
				break;
		}
	}
	if (aspectX == 0 || aspectY == 0)
		return false;
	return true;
}


MadVRNlsPresentationPlan ResolveMadVRNlsPresentationPlan(
	const MadVRNlsMappingDecision& decision,
	const MadVRActivePictureGeometry& geometry)
{
	MadVRNlsPresentationPlan plan;
	plan.shaderGeometry = geometry;
	if (decision.mode != MadVRNlsMappingMode::ACTIVE || !geometry.stable)
		return plan;

	const bool validBounds =
		std::isfinite(geometry.aspectRatio) && geometry.aspectRatio > 0.0 &&
		std::isfinite(geometry.left) && std::isfinite(geometry.top) &&
		std::isfinite(geometry.right) && std::isfinite(geometry.bottom) &&
		geometry.left >= 0.0 && geometry.top >= 0.0 &&
		geometry.right <= 1.0 && geometry.bottom <= 1.0 &&
		geometry.right > geometry.left && geometry.bottom > geometry.top;
	if (!validBounds || !std::isfinite(decision.targetAspect) ||
		decision.targetAspect <= 0.0)
	{
		return plan;
	}

	// madVR owns its independently detected videoCropRect. VP may safely add a
	// nonlinear mapping when encoded bars are orthogonal to the warp: horizontal
	// NLS requires full source width; vertical NLS requires full source height.
	// One pixel is intentionally not considered full raster.
	constexpr double fullEdgeEpsilon = 0.000001;
	const bool fullWidth = geometry.left <= fullEdgeEpsilon &&
		geometry.right >= 1.0 - fullEdgeEpsilon;
	const bool fullHeight = geometry.top <= fullEdgeEpsilon &&
		geometry.bottom >= 1.0 - fullEdgeEpsilon;
	if ((decision.verticalWarp && !fullHeight) ||
		(!decision.verticalWarp && !fullWidth))
	{
		return plan;
	}

	const double activeWidth = geometry.right - geometry.left;
	const double activeHeight = geometry.bottom - geometry.top;
	// The shader leaves the orthogonal bars untouched, then madVR removes them
	// exactly once. This whole-raster DAR makes the resulting cropped DAR equal
	// the target: rasterDAR * activeWidth / activeHeight = targetDAR.
	const double rasterAspect =
		decision.targetAspect * activeHeight / activeWidth;
	if (!std::isfinite(rasterAspect) || rasterAspect < 0.25 ||
		rasterAspect > 4.0 ||
		!ResolveMadVRNlsOutputAspect(
			rasterAspect, plan.aspectX, plan.aspectY))
	{
		return MadVRNlsPresentationPlan{};
	}

	plan.customShader = true;
	plan.rasterAspect = rasterAspect;
	// Retain the measured rectangle in runtime state and diagnostics, but make
	// this shader sample the full encoded raster. madVR remains the sole owner
	// of bar removal, preventing the double crop that caused zoom/fit errors.
	plan.shaderGeometry.left = 0.0;
	plan.shaderGeometry.top = 0.0;
	plan.shaderGeometry.right = 1.0;
	plan.shaderGeometry.bottom = 1.0;
	return plan;
}


MadVRNlsMappingDecision ConstrainMadVRNlsMappingToGeometry(
	const MadVRNlsMappingDecision& decision,
	const MadVRActivePictureGeometry& geometry)
{
	MadVRNlsMappingDecision constrained = decision;
	const MadVRNlsPresentationPlan plan =
		ResolveMadVRNlsPresentationPlan(decision, geometry);
	if (decision.mode != MadVRNlsMappingMode::ACTIVE ||
		plan.customShader)
	{
		return constrained;
	}

	// Bars on the warp axis (or malformed geometry) cannot be safely remapped
	// before madVR's independent crop. Preserve the complete picture instead.
	constrained.mode = MadVRNlsMappingMode::SAFE_FIT;
	constrained.safeFitVertical =
		decision.sourceAspect > decision.targetAspect;
	constrained.safeFitFraction = std::max(0.01, std::min(1.0,
		std::min(decision.sourceAspect, decision.targetAspect) /
		std::max(decision.sourceAspect, decision.targetAspect)));
	constrained.reason +=
		"; geometry cannot be mapped before madVR crop; using native safe fit";
	return constrained;
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


bool MadVRNlsOutputContractIsPrepared(
	const MadVRShaderRuntimeSnapshot& snapshot)
{
	return snapshot.nlsMode != MadVRNlsMappingMode::OFF &&
		snapshot.nlsMode != MadVRNlsMappingMode::WAITING &&
		snapshot.activeGeometry.stable &&
		snapshot.activeGeometry.rendererGeneration ==
			snapshot.rendererGeneration;
}


MadVRShaderRuntimeSnapshot MadVRShaderRuntimeState::GetSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_state;
}

bool MadVRShaderRuntimeState::PrepareNlsOutputContractRendererReplacement()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_preserveGeometryOnNextRenderer =
		MadVRNlsOutputContractIsPrepared(m_state);
	return m_preserveGeometryOnNextRenderer;
}


uint64_t MadVRShaderRuntimeState::BeginRendererGeneration()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	++m_state.rendererGeneration;
	// A controlled output-contract replacement does not change the source
	// epoch. Preserve the exact source-owned rectangle and bind it to the new
	// renderer generation so output aspect and shader mapping become visible
	// together. Never reconstruct coordinates from scalar aspect.
	if (m_preserveGeometryOnNextRenderer &&
		m_state.activeGeometry.stable &&
		(m_state.nlsMode == MadVRNlsMappingMode::ACTIVE ||
			m_state.nlsMode == MadVRNlsMappingMode::LINEAR_PASSTHROUGH ||
			m_state.nlsMode == MadVRNlsMappingMode::SAFE_FIT))
	{
		m_state.activeGeometry.rendererGeneration =
			m_state.rendererGeneration;
	}
	else
	{
		m_state.activeGeometry = {};
		if (m_state.nlsMode != MadVRNlsMappingMode::OFF)
			m_state.nlsMode = MadVRNlsMappingMode::WAITING;
	}
	m_preserveGeometryOnNextRenderer = false;
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
		nlsMode == MadVRNlsMappingMode::LINEAR_PASSTHROUGH ||
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
