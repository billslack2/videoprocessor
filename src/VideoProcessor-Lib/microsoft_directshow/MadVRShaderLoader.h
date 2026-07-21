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
};
