/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <string>

class ConfigFile;

namespace ShaderConfigValidation
{
	// Validates target-style [shader.*] configuration without loading or
	// compiling shader files. This stays renderer- and MFC-independent so the
	// standalone configuration editor can validate a candidate before saving.
	bool Validate(const ConfigFile& config, std::string& reason);
}
