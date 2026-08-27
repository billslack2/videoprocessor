#pragma once

#include "RendererProfileConfig.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>


// The profile runtime resolves when an action matches; this utility performs
// the small, shared Windows process launch safely from the owning subsystem.
namespace EventActionLauncher
{
	// Tracks the newest delayed invocation for each coalescing identity. By
	// default that identity is the unique action name; an explicit role lets
	// related actions (for example Rec.709 and BT.2020 state writers) share the
	// same newest-trigger-wins debounce slot.
	class PendingActionCoalescer
	{
	public:
		uint64_t Schedule(const std::string& identity)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return ++m_generations[identity];
		}

		bool Claim(const std::string& identity, uint64_t generation)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto pending = m_generations.find(identity);
			if (pending == m_generations.end() || pending->second != generation)
				return false;
			m_generations.erase(pending);
			return true;
		}

		void CancelAll()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_generations.clear();
		}

	private:
		std::mutex m_mutex;
		std::map<std::string, uint64_t> m_generations;
	};

	inline std::string ActionIdentity(
		const RendererProfileConfig::Model::EventAction& action)
	{
		return ConfigFile::NormalizeName(action.coalesceRole.empty() ?
			action.name : action.coalesceRole);
	}

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
