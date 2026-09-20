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
	// Preserve rendering-profile feedback protection without dropping unrelated
	// screen/color/queue intent while an action process is active.
	inline bool IsProfileActionFeedback(bool processActive,
		const std::string& event, const std::string& reason)
	{
		return processActive && reason == "manual" &&
			event == "profile.display.changed";
	}

	// Classify the original selection, not its emitted actions. Rendering-only
	// feedback may also emit generic profile.changed/state.committed events.
	inline bool IsRenderingSelectionFeedback(bool processActive,
		const std::vector<RendererProfileConfig::KeySelection>& selections,
		bool cycleSelection)
	{
		if (selections.empty()) return false;
		for (const auto& selection : selections)
			if (!IsProfileActionFeedback(processActive,
				"profile." + selection.group + ".changed", cycleSelection ? "cycle" : "manual"))
				return false;
		return true;
	}

	// Tracks the newest delayed invocation for each coalescing identity. By
	// default that identity is the unique action name; an explicit role lets
	// related actions (for example Rec.709 and BT.2020 state writers) share the
	// same newest-trigger-wins debounce slot.
	class PendingActionCoalescer
	{
	public:
		uint64_t Schedule(const std::string& identity, bool* superseded = nullptr)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (superseded)
				*superseded = m_generations.find(identity) != m_generations.end();
			return m_generations[identity] = ++m_nextGeneration;
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
		// Never reuse a token after Claim or CancelAll: old workers may still wake.
		uint64_t m_nextGeneration = 0;
		std::map<std::string, uint64_t> m_generations;
	};

	// Profile actions may execute arbitrary user scripts.  Some legacy scripts
	// send VP shortcuts, so a malformed configuration can accidentally form an
	// action -> shortcut -> profile-change loop.  Bound the number of actual
	// process launches in that feedback path; delayed actions already waiting
	// for their debounce window are invalidated by the owner when this trips.
	class ProfileActionCircuitBreaker
	{
	public:
		enum class Decision
		{
			Allow,
			Suppressed,
			Tripped
		};

		Decision BeginLaunch(uint64_t nowMilliseconds)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (nowMilliseconds < m_suppressedUntilMilliseconds)
				return Decision::Suppressed;
			while (!m_recentLaunches.empty() &&
				nowMilliseconds - m_recentLaunches.front() > kWindowMilliseconds)
				m_recentLaunches.erase(m_recentLaunches.begin());
			if (m_recentLaunches.size() >= kMaximumLaunches)
			{
				m_recentLaunches.clear();
				m_suppressedUntilMilliseconds = nowMilliseconds +
					kSuppressionMilliseconds;
				return Decision::Tripped;
			}
			m_recentLaunches.push_back(nowMilliseconds);
			return Decision::Allow;
		}

	private:
		static constexpr uint64_t kWindowMilliseconds = 10000;
		static constexpr uint64_t kSuppressionMilliseconds = 10000;
		static constexpr size_t kMaximumLaunches = 4;
		std::mutex m_mutex;
		std::vector<uint64_t> m_recentLaunches;
		uint64_t m_suppressedUntilMilliseconds = 0;
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
		const std::string& configPath, bool waitForExit = false,
		uintptr_t cancellationEvent = 0);
}
