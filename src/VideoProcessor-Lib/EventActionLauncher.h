#pragma once

#include "RendererProfileConfig.h"

#include <string>


// The profile runtime resolves when an action matches; this utility performs
// the small, shared Windows process launch safely from the owning subsystem.
namespace EventActionLauncher
{
	void Launch(const RendererProfileConfig::Model::EventAction& action,
		const std::string& configPath);
}
