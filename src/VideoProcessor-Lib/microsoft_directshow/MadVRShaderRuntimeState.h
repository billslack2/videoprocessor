/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

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


enum class MadVRNlsMappingMode
{
	OFF,
	WAITING,
	SCOPE_PASSTHROUGH,
	ACTIVE,
	SAFE_FIT
};


struct MadVRNlsMappingDecision
{
	MadVRNlsMappingMode mode = MadVRNlsMappingMode::WAITING;
	double sourceAspect = 0.0;
	double targetAspect = 0.0;
	double stretchRatio = 1.0;
	bool verticalWarp = false;
	double safeFitFraction = 1.0;
	bool safeFitVertical = false;
	std::string reason;
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


MadVRNlsMappingDecision EvaluateMadVRNlsMapping(bool aspectAvailable,
	double activeAspect, double targetAspect, double tolerancePercent,
	double activeAspectMinimum, bool narrowerOnly,
	double maximumStretchRatio = 1.5);

bool ResolveMadVRNlsOutputAspect(double targetAspect,
	unsigned long& aspectX, unsigned long& aspectY);
bool ResolveMadVRNlsPresentationAspect(MadVRNlsMappingMode mode,
	double activeAspect, double targetAspect,
	unsigned long& aspectX, unsigned long& aspectY);
bool MadVROutputAspectRequiresRestart(unsigned long currentAspectX,
	unsigned long currentAspectY, unsigned long desiredAspectX,
	unsigned long desiredAspectY, double nativeAspect);
bool MadVRNlsOutputContractIsPrepared(
	const MadVRShaderRuntimeSnapshot& snapshot);

const char* MadVRNlsMappingModeName(MadVRNlsMappingMode mode);


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
