/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <NlsGeometryPolicy.h>
#include <cstdint>
#include <mutex>
#include <string>


struct MadVRActivePictureGeometry
{
	double aspectRatio = 0.0;
	double left = 0.0;
	double top = 0.0;
	double right = 1.0;
	double bottom = 1.0;
	uint64_t generation = 0;
	uint64_t rendererGeneration = 0;
	bool stable = false;
};


// Compatibility aliases keep DirectShow-specific runtime structures focused on
// renderer lifecycle while the aspect decision itself is renderer-neutral.
using MadVRNlsMappingMode = NlsMappingMode;
using MadVRNlsMappingDecision = NlsMappingDecision;


struct MadVRNlsPresentationPlan
{
	bool customShader = false;
	MadVRActivePictureGeometry shaderGeometry;
	double rasterAspect = 0.0;
	unsigned long aspectX = 0;
	unsigned long aspectY = 0;
};


struct MadVRShaderRuntimeSnapshot
{
	std::string requestedRule;
	std::string effectiveRule;
	MadVRNlsMappingMode nlsMode = MadVRNlsMappingMode::OFF;
	MadVRNlsMappingMode lastSafeNlsMode = MadVRNlsMappingMode::OFF;
	double nlsTargetAspect = 0.0;
	uint64_t rendererGeneration = 0;
	MadVRActivePictureGeometry activeGeometry;
	MadVRNlsMappingDecision nlsDecision;
};


bool ResolveMadVRNlsOutputAspect(double targetAspect,
	unsigned long& aspectX, unsigned long& aspectY);
MadVRNlsPresentationPlan ResolveMadVRNlsPresentationPlan(
	const MadVRNlsMappingDecision& decision,
	const MadVRActivePictureGeometry& geometry);
MadVRNlsMappingDecision ConstrainMadVRNlsMappingToGeometry(
	const MadVRNlsMappingDecision& decision,
	const MadVRActivePictureGeometry& geometry);
bool MadVROutputAspectRequiresRestart(unsigned long currentAspectX,
	unsigned long currentAspectY, unsigned long desiredAspectX,
	unsigned long desiredAspectY, double nativeAspect);
bool MadVRNlsOutputContractIsPrepared(
	const MadVRShaderRuntimeSnapshot& snapshot);

class MadVRShaderRuntimeState
{
public:
	MadVRShaderRuntimeSnapshot GetSnapshot() const;
	bool PrepareNlsOutputContractRendererReplacement();
	uint64_t BeginRendererGeneration();
	void SetRuleSelection(const std::string& requestedRule,
		const std::string& effectiveRule, MadVRNlsMappingMode nlsMode);
	void SetRequestedRule(const std::string& requestedRule);
	void SetEffectiveRule(const std::string& effectiveRule);
	void SetNlsTargetAspect(double targetAspect);
	void SetNlsDecision(const MadVRNlsMappingDecision& decision);
	bool SetActiveGeometry(const MadVRActivePictureGeometry& geometry);

private:
	mutable std::mutex m_mutex;
	MadVRShaderRuntimeSnapshot m_state;
	bool m_preserveGeometryOnNextRenderer = false;
};
