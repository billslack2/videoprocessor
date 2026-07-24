/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <dshow.h>
#include <VideoState.h>
#include <cstdint>
#include <string>
#include <vector>


struct ActiveMadVRShader
{
	std::string name;
	bool postResize = false;
};


struct MadVRShaderSelection
{
	std::string ruleName;
	std::string ruleLabel = "None";
	std::vector<ActiveMadVRShader> activeShaders;
	unsigned long outputAspectRatioX = 0;
	unsigned long outputAspectRatioY = 0;
};

struct MadVRActivePictureGeometry
{
	double aspectRatio = 0.0;
	double left = 0.0;
	double top = 0.0;
	double right = 1.0;
	double bottom = 1.0;
	uint64_t generation = 0;
	bool stable = false;
};


class MadVRShaderLoader
{
public:
	// Loads [shaders] from VideoProcessor.cfg and applies the configured shader
	// chains when the selected renderer exposes an external shader interface.
	// Configuration or compilation failures are logged and never abort renderer
	// startup. Returns the shaders from stages that were installed completely.
	static MadVRShaderSelection ApplyConfiguredShaders(IBaseFilter* renderer,
		const VideoState& videoState);

	// Selects a named rule immediately and remembers it across renderer rebuilds.
	// An empty name returns rule selection to the automatic configured rules.
	static MadVRShaderSelection ApplyConfiguredShaderRule(IBaseFilter* renderer,
		const VideoState& videoState, const std::string& ruleName,
		bool updateRuntimeRequest = true);

	// Returns the presentation aspect ratio requested by the currently selected
	// runtime rule. False means retain the source's native aspect ratio.
	static bool GetRuntimeOutputAspectRatio(unsigned long& aspectX,
		unsigned long& aspectY);

	// Validates optional active-picture conditions on a manual rule. Rules with
	// no conditions remain compatible with all formats.
	static bool ValidateActivePictureAspect(const std::string& ruleName,
		bool aspectAvailable, double activeAspectRatio, std::string& reason);
	// Supplies the stable, measured active-picture aspect used to derive NLS
	// crop/stretch parameters. The value survives the graph rebuild required by
	// an output-aspect change.
	static void SetRuntimeActivePictureAspectRatio(double activeAspectRatio);
	static void SetRuntimeActivePictureGeometry(const MadVRActivePictureGeometry& geometry);
	static void SetRuntimeNlsTargetAspect(double targetAspect);
	static void SetRuntimeShaderRequest(const std::string& ruleName);
	static bool GetRuleActivationInfo(const std::string& ruleName,
		std::string& label, std::string& inactiveRule);
};
