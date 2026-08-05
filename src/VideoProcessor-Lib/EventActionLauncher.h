#pragma once

#include "RendererProfileConfig.h"

#include <functional>
#include <string>


// The profile runtime resolves when an action matches; this utility performs
// the small, shared Windows process launch safely from the owning subsystem.
namespace EventActionLauncher
{
	using ActionValueLookup = std::function<bool(const std::string&,
		std::string&)>;

	// Replaces each ${variable} in action arguments with its event-bound value.
	// The parser has already checked variable/event compatibility; a value that
	// is unavailable at the committed boundary prevents the action from running.
	bool ExpandArgumentVariables(
		const RendererProfileConfig::Model::EventAction& action,
		const ActionValueLookup& values,
		RendererProfileConfig::Model::EventAction& expanded,
		std::string& error);

	void Launch(const RendererProfileConfig::Model::EventAction& action,
		const std::string& configPath);
}
