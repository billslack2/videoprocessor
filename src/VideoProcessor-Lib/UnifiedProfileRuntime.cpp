#include <pch.h>

#include "UnifiedProfileRuntime.h"

#include <DebugLog.h>
#include <EventActionLauncher.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <sstream>
#include <windows.h>


namespace
{
	bool ReadStateEntry(const std::string& line,
		std::string& key, std::string& value)
	{
		const std::string trimmed = ConfigFile::Trim(line);
		if (trimmed.empty() || trimmed.front() == '#' ||
			trimmed.front() == ';')
			return false;
		const size_t colon = trimmed.find(':');
		const size_t equals = trimmed.find('=');
		const size_t separator = colon == std::string::npos ? equals :
			(equals == std::string::npos ? colon :
				std::min(colon, equals));
		if (separator == std::string::npos)
			return false;
		key = ConfigFile::NormalizeName(trimmed.substr(0, separator));
		value = ConfigFile::NormalizeName(trimmed.substr(separator + 1));
		return !key.empty() && !value.empty();
	}


	bool SameEffectiveState(
		const UnifiedProfileRuntime::Snapshot& left,
		const UnifiedProfileRuntime::Snapshot& right)
	{
		if (!(left.manualSelections == right.manualSelections &&
			left.effectiveSelections == right.effectiveSelections &&
			left.viewport.profile == right.viewport.profile &&
			left.viewport.screenAspect.numerator ==
				right.viewport.screenAspect.numerator &&
			left.viewport.screenAspect.denominator ==
				right.viewport.screenAspect.denominator &&
			left.viewport.hasScreenAspect ==
				right.viewport.hasScreenAspect &&
			left.viewport.verticalAlignment ==
				right.viewport.verticalAlignment &&
			left.viewport.anamorphicScale.numerator ==
				right.viewport.anamorphicScale.numerator &&
			left.viewport.anamorphicScale.denominator ==
				right.viewport.anamorphicScale.denominator &&
			left.viewport.automaticCrop == right.viewport.automaticCrop &&
			left.viewport.subtitleFit == right.viewport.subtitleFit &&
			left.viewport.subtitleHoldMilliseconds ==
				right.viewport.subtitleHoldMilliseconds &&
			left.viewport.subtitleEngageDriftMilliseconds ==
				right.viewport.subtitleEngageDriftMilliseconds &&
			left.viewport.subtitleReleaseDriftMilliseconds ==
				right.viewport.subtitleReleaseDriftMilliseconds &&
			left.viewport.subtitlePaddingPixels ==
				right.viewport.subtitlePaddingPixels &&
			left.viewport.subtitleTargetBufferPixels ==
				right.viewport.subtitleTargetBufferPixels))
			return false;
		if (left.queue.profile != right.queue.profile ||
			left.queue.hasQueueSize != right.queue.hasQueueSize ||
			left.queue.queueSize != right.queue.queueSize ||
			left.queue.hasLeadFrames != right.queue.hasLeadFrames ||
			left.queue.leadFrames != right.queue.leadFrames ||
			left.queue.hasTargetFrames != right.queue.hasTargetFrames ||
			left.queue.targetFrames != right.queue.targetFrames ||
			left.queue.hasActivePictureLookaheadFrames !=
				right.queue.hasActivePictureLookaheadFrames ||
			left.queue.activePictureLookaheadFrames !=
				right.queue.activePictureLookaheadFrames ||
			left.queue.hasStartupPrerollFrames !=
				right.queue.hasStartupPrerollFrames ||
			left.queue.startupPrerollFrames !=
				right.queue.startupPrerollFrames ||
			left.queue.hasResetAfterRendererRestartSeconds !=
				right.queue.hasResetAfterRendererRestartSeconds ||
			left.queue.resetAfterRendererRestartSeconds !=
				right.queue.resetAfterRendererRestartSeconds ||
			left.queue.hasResetQueueTooLargePercent !=
				right.queue.hasResetQueueTooLargePercent ||
			left.queue.resetQueueTooLargePercent !=
				right.queue.resetQueueTooLargePercent)
			return false;
		if (left.lldv.profile != right.lldv.profile ||
			left.lldv.hasMaxCll != right.lldv.hasMaxCll ||
			left.lldv.maxCll != right.lldv.maxCll ||
			left.lldv.hasMaxFall != right.lldv.hasMaxFall ||
			left.lldv.maxFall != right.lldv.maxFall ||
			left.lldv.hasMasteringMinLuminance !=
				right.lldv.hasMasteringMinLuminance ||
			left.lldv.masteringMinLuminance !=
				right.lldv.masteringMinLuminance ||
			left.lldv.hasMasteringMaxLuminance !=
				right.lldv.hasMasteringMaxLuminance ||
			left.lldv.masteringMaxLuminance !=
				right.lldv.masteringMaxLuminance)
			return false;

		const auto& leftValues = left.variables.Values();
		const auto& rightValues = right.variables.Values();
		for (const auto& item : leftValues)
		{
			if (item.first == "viewport_generation")
				continue;
			const auto other = rightValues.find(item.first);
			if (other == rightValues.end() ||
				item.second.type != other->second.type)
				return false;
			std::string leftText;
			std::string rightText;
			if (!item.second.ToExpressionText(leftText) ||
				!other->second.ToExpressionText(rightText) ||
				leftText != rightText)
				return false;
		}
		for (const auto& item : rightValues)
			if (item.first != "viewport_generation" &&
				leftValues.find(item.first) == leftValues.end())
				return false;
		return true;
	}


	void PublishSourceVariables(
		const DisplayRuleExpression::ValueLookup& lookup,
		std::map<std::string, StateVariables::Value>& variables)
	{
		for (const char* rawName :
			{ "eotf", "transfer", "colorspace", "primaries", "format",
			  "resolution", "range", "scan", "hdr_metadata", "interlaced",
			  "source_rate", "cadence", "width", "height" })
		{
			const std::string name(rawName);
			std::string text;
			if (!lookup(name, text) || text.empty())
				continue;
			DisplayRuleExpression::ValueType type;
			if (!DisplayRuleExpression::GetVariableType(name, type))
				continue;
			if (type == DisplayRuleExpression::ValueType::Boolean)
			{
				const std::string normalized =
					ConfigFile::NormalizeName(text);
				if (normalized == "true" || normalized == "false")
					variables[name] = StateVariables::Value::Boolean(
						normalized == "true");
			}
			else if (type == DisplayRuleExpression::ValueType::Number)
			{
				double number = 0.0;
				if (DisplayRuleExpression::ParseNumber(text, number))
					variables[name] =
						StateVariables::Value::Number(number);
			}
			else
			{
				variables[name] = StateVariables::Value::Text(text);
			}
		}
	}


	bool SnapshotValueChanged(
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& previous,
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& current,
		const std::string& name)
	{
		std::string before;
		std::string after;
		const bool hadBefore = previous && previous->LookupVariable(name, before);
		const bool hasAfter = current && current->LookupVariable(name, after);
		return hadBefore != hasAfter || (hadBefore && before != after);
	}


	bool LookupActionValue(const std::string& name, const std::string& event,
		const std::string& reason,
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& previous,
		const std::shared_ptr<const UnifiedProfileRuntime::Snapshot>& current,
		std::string& value)
	{
		if (name == "event") { value = event; return true; }
		if (name == "event_reason") { value = reason; return true; }
		const std::string previousProfile = "previous_profile.";
		if (name.size() > previousProfile.size() &&
			name.compare(0, previousProfile.size(), previousProfile) == 0)
			return previous && previous->LookupVariable("profile." +
				name.substr(previousProfile.size()), value);
		const std::string previousValue = "previous.";
		if (name.size() > previousValue.size() &&
			name.compare(0, previousValue.size(), previousValue) == 0)
			return previous && previous->LookupVariable(
				name.substr(previousValue.size()), value);
		return current && current->LookupVariable(name, value);
	}
}


namespace UnifiedProfileRuntime
{
	Runtime::Runtime()
	{
		std::atomic_store(&m_snapshot, std::shared_ptr<const Snapshot>());
	}


	bool Runtime::Initialize(const ConfigFile& config,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		std::string& error)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		error.clear();
		if (m_initialized)
		{
			error = "unified profile runtime is already initialized";
			return false;
		}
		RendererProfileConfig::Model model;
		if (!RendererProfileConfig::Read(config, model, error))
			return false;

		m_model = std::move(model);
		m_configPath = config.GetLoadedPath();
		m_statePath = m_model.persistSelection ?
			RendererProfileConfig::StatePath(config) : std::string();
		std::map<std::string, std::string> restored;
		if (m_model.persistSelection &&
			!LoadPersistedSelections(restored, error))
			return false;

		m_sessionOverrideGroups.clear();
		std::shared_ptr<const Snapshot> initial;
		if (!BuildSnapshot(restored, {}, sourceValues, 1, initial, error))
			return false;

		m_generation = 1;
		m_initialized = true;
		std::atomic_store(&m_snapshot, initial);
		DebugLog::Log(
			"unified profile runtime initialized state=%s viewport=%s aspect=%s generation=%llu",
			m_statePath.c_str(), initial->viewport.profile.c_str(),
			initial->viewport.screenAspect.Canonical().c_str(),
			static_cast<unsigned long long>(initial->generation));
		return true;
	}


	bool Runtime::Reload(const ConfigFile& config,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		RefreshResult& result, std::string& error)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		result = {};
		error.clear();
		if (!m_initialized)
		{
			error = "unified profile runtime is not initialized";
			return false;
		}

		RendererProfileConfig::Model candidateModel;
		if (!RendererProfileConfig::Read(config, candidateModel, error))
			return false;

		const std::shared_ptr<const Snapshot> previous =
			std::atomic_load(&m_snapshot);
		RendererProfileConfig::Model previousModel = std::move(m_model);
		const std::string previousConfigPath = m_configPath;
		const std::string previousStatePath = m_statePath;
		m_model = std::move(candidateModel);
		m_configPath = config.GetLoadedPath();
		m_statePath = m_model.persistSelection ?
			RendererProfileConfig::StatePath(config) : std::string();

		std::shared_ptr<const Snapshot> candidate;
		std::map<std::string, std::string> manual = previous ?
			previous->manualSelections :
			std::map<std::string, std::string>();
		std::set<std::string> sessionOverrides = m_sessionOverrideGroups;
		for (auto selection = manual.begin(); selection != manual.end();)
		{
			const auto group = std::find_if(m_model.groups.begin(),
				m_model.groups.end(), [&selection](
					const RendererProfileConfig::Group& candidate)
				{
					return candidate.name == selection->first;
				});
			const bool valid = group != m_model.groups.end() &&
				std::find(group->profiles.begin(), group->profiles.end(),
					selection->second) != group->profiles.end();
			if (!valid)
			{
				sessionOverrides.erase(selection->first);
				selection = manual.erase(selection);
			}
			else
				++selection;
		}
		if (!BuildSnapshot(manual, sessionOverrides, sourceValues, m_generation + 1,
			candidate, error))
		{
			m_model = std::move(previousModel);
			m_configPath = previousConfigPath;
			m_statePath = previousStatePath;
			return false;
		}
		result.changed = !previous || !SameEffectiveState(*previous, *candidate);
		if (result.changed && !CollectTransitionActionInvocations(previous,
			candidate, "configuration", result.actions, error))
		{
			m_model = std::move(previousModel);
			m_configPath = previousConfigPath;
			m_statePath = previousStatePath;
			return false;
		}

		++m_generation;
		m_sessionOverrideGroups = std::move(sessionOverrides);
		std::atomic_store(&m_snapshot, candidate);
		result.snapshot = candidate;
		DebugLog::Log(
			"unified profile configuration reloaded viewport=%s aspect=%s generation=%llu changed=%d",
			candidate->viewport.profile.c_str(),
			candidate->viewport.hasScreenAspect ?
				candidate->viewport.screenAspect.Canonical().c_str() : "native",
			static_cast<unsigned long long>(candidate->generation),
			result.changed ? 1 : 0);
		return true;
	}


	bool Runtime::SelectKey(const std::string& key,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		SelectionResult& result, std::string& error)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		result = {};
		error.clear();
		if (!m_initialized)
		{
			error = "unified profile runtime is not initialized";
			return false;
		}

		const DisplayRuleExpression::ValueLookup values = sourceValues ?
			sourceValues :
			[](const std::string&, std::string&) { return false; };
		if (!RendererProfileConfig::SelectForKey(
			m_model, key, values, result.selections, error))
			return false;
		const std::shared_ptr<const Snapshot> current =
			std::atomic_load(&m_snapshot);
		if (result.selections.empty())
		{
			result.snapshot = current;
			return true;
		}

		std::map<std::string, std::string> manual =
			current ? current->manualSelections :
			std::map<std::string, std::string>();
		std::set<std::string> sessionOverrides = m_sessionOverrideGroups;
		for (const RendererProfileConfig::KeySelection& selection :
			result.selections)
		{
			if (selection.resetToAutomatic)
			{
				manual.erase(selection.group);
				sessionOverrides.erase(selection.group);
			}
			else
			{
				manual[selection.group] = selection.profile;
				sessionOverrides.insert(selection.group);
			}
		}

		std::shared_ptr<const Snapshot> candidate;
		if (!BuildSnapshot(manual, sessionOverrides, values, m_generation + 1,
			candidate, error))
			return false;
		const bool overrideStateChanged =
			sessionOverrides != m_sessionOverrideGroups;
		if (current && SameEffectiveState(*current, *candidate) &&
			!overrideStateChanged)
		{
			result.snapshot = current;
			return true;
		}
		if (!CollectTransitionActionInvocations(current, candidate, "manual",
			result.actions, error))
			return false;
		if (m_model.persistSelection &&
			!PersistSelections(manual, error))
			return false;

		++m_generation;
		m_sessionOverrideGroups = std::move(sessionOverrides);
		std::atomic_store(&m_snapshot, candidate);
		result.changed = true;
		result.snapshot = candidate;
		DebugLog::Log(
			"unified profile key=%s viewport=%s aspect=%s generation=%llu",
			key.c_str(), candidate->viewport.profile.c_str(),
			candidate->viewport.screenAspect.Canonical().c_str(),
			static_cast<unsigned long long>(candidate->generation));
		return true;
	}


	bool Runtime::Refresh(
		const DisplayRuleExpression::ValueLookup& sourceValues,
		RefreshResult& result, std::string& error)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		result = {};
		error.clear();
		if (!m_initialized)
		{
			error = "unified profile runtime is not initialized";
			return false;
		}
		const std::shared_ptr<const Snapshot> current =
			std::atomic_load(&m_snapshot);
		std::shared_ptr<const Snapshot> candidate;
		if (!BuildSnapshot(current->manualSelections,
			m_sessionOverrideGroups, sourceValues,
			m_generation + 1, candidate, error))
			return false;
		if (SameEffectiveState(*current, *candidate))
		{
			result.snapshot = current;
			return true;
		}
		if (!CollectTransitionActionInvocations(current, candidate, "source",
			result.actions, error))
			return false;
		++m_generation;
		std::atomic_store(&m_snapshot, candidate);
		result.changed = true;
		result.snapshot = candidate;
		return true;
	}


	bool Runtime::ReapplyRules(
		const DisplayRuleExpression::ValueLookup& sourceValues,
		RefreshResult& result, std::vector<std::string>& clearedGroups,
		std::string& error)
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		result = {};
		clearedGroups.clear();
		error.clear();
		if (!m_initialized)
		{
			error = "unified profile runtime is not initialized";
			return false;
		}

		const std::shared_ptr<const Snapshot> current =
			std::atomic_load(&m_snapshot);
		std::shared_ptr<const Snapshot> candidate;
		if (!BuildSnapshot(current->manualSelections, {}, sourceValues,
			m_generation + 1, candidate, error))
			return false;

		const bool effectiveChanged = !SameEffectiveState(*current, *candidate);
		if (effectiveChanged && !CollectTransitionActionInvocations(current,
			candidate, "rules-reapplied", result.actions, error))
			return false;

		clearedGroups.assign(m_sessionOverrideGroups.begin(),
			m_sessionOverrideGroups.end());
		m_sessionOverrideGroups.clear();
		if (!effectiveChanged)
		{
			result.snapshot = current;
			return true;
		}

		++m_generation;
		std::atomic_store(&m_snapshot, candidate);
		result.changed = true;
		result.snapshot = candidate;
		return true;
	}


	std::shared_ptr<const Snapshot> Runtime::GetSnapshot() const
	{
		return std::atomic_load(&m_snapshot);
	}


	bool Runtime::IsInitialized() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		return m_initialized;
	}


	std::string Runtime::StatePath() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		return m_statePath;
	}


	std::string Runtime::ConfigPath() const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		return m_configPath;
	}


	bool Runtime::CollectActionInvocations(const std::string& event,
		const std::string& reason,
		const std::shared_ptr<const Snapshot>& previous,
		const std::shared_ptr<const Snapshot>& current,
		std::vector<ActionInvocation>& actions, std::string& error,
		const EventActionLauncher::ActionValueLookup& eventValues) const
	{
		std::lock_guard<std::mutex> guard(m_mutex);
		actions.clear();
		error.clear();
		if (!m_initialized)
		{
			error = "unified profile runtime is not initialized";
			return false;
		}
		return CollectActionInvocationsUnlocked(event, reason, previous,
			current, actions, error, eventValues);
	}


	bool Runtime::CollectActionInvocationsUnlocked(const std::string& event,
		const std::string& reason,
		const std::shared_ptr<const Snapshot>& previous,
		const std::shared_ptr<const Snapshot>& current,
		std::vector<ActionInvocation>& actions, std::string& error,
		const EventActionLauncher::ActionValueLookup& eventValues) const
	{
		if (!RendererProfileConfig::IsSupportedActionEvent(event))
		{
			error = "unsupported action event '" + event + "'";
			return false;
		}
		for (const RendererProfileConfig::Model::EventAction& action :
			m_model.actions)
		{
			if (std::find(action.events.begin(), action.events.end(), event) ==
				action.events.end())
				continue;
			const EventActionLauncher::ActionValueLookup values =
				[&event, &reason, &previous, &current, &eventValues](
					const std::string& variable, std::string& value)
				{
					if (eventValues && eventValues(variable, value))
						return true;
					return LookupActionValue(variable, event, reason, previous,
						current, value);
				};
			int specificity = 0;
			std::string matchError;
			const bool matches = action.when.empty() ||
				action.whenExpression.Matches(values, specificity, matchError);
			if (!matches)
			{
				if (!matchError.empty())
					DebugLog::Log("event action '%s' evaluation for %s failed: %s",
						action.name.c_str(), event.c_str(), matchError.c_str());
				else
					DebugLog::Log(
						"event action '%s' did not match %s: condition=false rule=%s",
						action.name.c_str(), event.c_str(), action.when.c_str());
				continue;
			}
			RendererProfileConfig::Model::EventAction expanded;
			std::string expansionError;
			if (!EventActionLauncher::ExpandArgumentVariables(action, values,
				expanded, expansionError))
			{
				DebugLog::Log("event action '%s' expansion for %s failed: %s",
					action.name.c_str(), event.c_str(), expansionError.c_str());
				continue;
			}
			actions.push_back({ std::move(expanded), event, reason });
		}
		return true;
	}


	bool Runtime::CollectTransitionActionInvocations(
		const std::shared_ptr<const Snapshot>& previous,
		const std::shared_ptr<const Snapshot>& current,
		const std::string& reason,
		std::vector<ActionInvocation>& actions, std::string& error) const
	{
		actions.clear();
		if (!current)
			return true;
		for (const char* field :
			{ "eotf", "transfer", "colorspace", "primaries", "format",
			  "resolution", "scan", "hdr_metadata", "interlaced",
			  "source_rate", "cadence", "width", "height" })
			if (SnapshotValueChanged(previous, current, field) &&
				!CollectActionInvocationsUnlocked("source." +
					std::string(field) + ".changed", reason, previous, current,
					actions, error))
				return false;

		std::set<std::string> groups;
		if (previous)
			for (const auto& selection : previous->effectiveSelections)
				groups.insert(selection.first);
		for (const auto& selection : current->effectiveSelections)
			groups.insert(selection.first);
		bool profileChanged = false;
		for (const std::string& group : groups)
		{
			const std::string before = previous &&
				previous->effectiveSelections.count(group) ?
				previous->effectiveSelections.at(group) : std::string();
			const std::string after = current->effectiveSelections.count(group) ?
				current->effectiveSelections.at(group) : std::string();
			if (before == after)
				continue;
			profileChanged = true;
			if (!CollectActionInvocationsUnlocked("profile." + group +
				".changed", reason, previous, current, actions, error))
				return false;
		}
		if (profileChanged && !CollectActionInvocationsUnlocked(
			"profile.changed", reason, previous, current, actions, error))
			return false;
		return CollectActionInvocationsUnlocked("state.committed", reason,
			previous, current, actions, error);
	}


	bool Runtime::LoadPersistedSelections(
		std::map<std::string, std::string>& selections,
		std::string& error) const
	{
		selections.clear();
		error.clear();
		std::ifstream input(m_statePath);
		if (!input.is_open())
			return true;

		std::string line;
		while (std::getline(input, line))
		{
			std::string key;
			std::string value;
			if (!ReadStateEntry(line, key, value))
				continue;
			if (key == "screen_profile")
			{
				DebugLog::Log(
					"unified profile state ignored removed screen_profile=%s",
					value.c_str());
				continue;
			}
			if (key.compare(0, 8, "profile.") != 0)
				continue;
			const std::string group = key.substr(8);
			if (IsPersistedSelectionValid(group, value))
				selections[group] = value;
			else
				DebugLog::Log(
					"unified profile state ignored invalid selection %s=%s",
					group.c_str(), value.c_str());
		}

		return true;
	}


	bool Runtime::PersistSelections(
		const std::map<std::string, std::string>& selections,
		std::string& error) const
	{
		error.clear();
		// The state file is shared with durable application recovery records.
		// Profile persistence owns profile.* and removes the obsolete
		// screen_profile entry while retaining every unrelated recovery record.
		std::vector<std::string> preservedLines;
		{
			std::ifstream existing(m_statePath);
			std::string line;
			while (std::getline(existing, line))
			{
				std::string key;
				std::string value;
				if (ReadStateEntry(line, key, value) &&
					(key == "screen_profile" ||
					 key.compare(0, 8, "profile.") == 0))
					continue;
				if (line == "# Managed by VideoProcessor.")
					continue;
				preservedLines.push_back(line);
			}
		}
		const std::string temporaryPath = m_statePath + ".tmp";
		std::ofstream output(temporaryPath,
			std::ios::out | std::ios::trunc);
		if (!output.is_open())
		{
			error = "cannot open temporary profile state '" +
				temporaryPath + "'";
			return false;
		}
		output << "# Managed by VideoProcessor.\n";
		for (const std::string& line : preservedLines)
			output << line << "\n";
		for (const RendererProfileConfig::Group& group : m_model.groups)
		{
			if (!group.persistSelection)
				continue;
			const auto selection = selections.find(group.name);
			if (selection != selections.end())
				output << "profile." << group.name << ": " <<
					selection->second << "\n";
		}
		output.close();
		if (!output)
		{
			DeleteFileA(temporaryPath.c_str());
			error = "cannot write temporary profile state '" +
				temporaryPath + "'";
			return false;
		}
		if (!MoveFileExA(temporaryPath.c_str(), m_statePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			const DWORD code = GetLastError();
			DeleteFileA(temporaryPath.c_str());
			std::ostringstream message;
			message << "cannot commit profile state '" << m_statePath <<
				"' (Windows error " << code << ")";
			error = message.str();
			return false;
		}
		return true;
	}


	bool Runtime::BuildSnapshot(
		const std::map<std::string, std::string>& manualSelections,
		const std::set<std::string>& sessionOverrideGroups,
		const DisplayRuleExpression::ValueLookup& sourceValues,
		uint64_t generation, std::shared_ptr<const Snapshot>& snapshot,
		std::string& error) const
	{
		error.clear();
		const DisplayRuleExpression::ValueLookup values = sourceValues ?
			sourceValues :
			[](const std::string&, std::string&) { return false; };
		std::vector<RendererProfileConfig::AutomaticSelection> automatic;
		if (!RendererProfileConfig::SelectAutomatic(
			m_model, values, automatic, error))
			return false;

		// A saved key selection is a fallback for groups without a matching rule.
		// Source-driven rules can move persisted settings, while an explicit
		// shortcut is a deliberate session override until this process exits.
		std::map<std::string, std::string> effective = manualSelections;
		for (const RendererProfileConfig::AutomaticSelection& selection :
			automatic)
		{
			// A configured default supplies an otherwise-unselected group; it is
			// not a source rule and must not cancel an operator shortcut. A real
			// when: match remains authoritative over persisted state.
			if ((!selection.configuredDefault &&
				sessionOverrideGroups.find(selection.group) ==
					sessionOverrideGroups.end()) ||
				effective.find(selection.group) == effective.end())
				effective[selection.group] = selection.profile;
		}

		std::string viewportProfile = "default";
		const auto selectedViewport = effective.find("viewport");
		if (selectedViewport != effective.end())
			viewportProfile = selectedViewport->second;
		RendererProfileConfig::ResolvedViewport viewport;
		if (!RendererProfileConfig::ResolveViewport(
			m_model, viewportProfile, generation, viewport, error))
			return false;
		RendererProfileConfig::ResolvedQueue queue;
		const auto selectedQueue = effective.find("queue");
		if (selectedQueue != effective.end() &&
			!RendererProfileConfig::ResolveQueue(
				m_model, selectedQueue->second, queue, error))
			return false;
		RendererProfileConfig::ResolvedLldv lldv;
		const auto selectedLldv = effective.find("lldv");
		if (selectedLldv != effective.end() &&
			!RendererProfileConfig::ResolveLldv(
				m_model, selectedLldv->second, lldv, error))
			return false;

		std::map<std::string, StateVariables::Value> variables;
		PublishSourceVariables(values, variables);
		variables["viewport_profile"] =
			StateVariables::Value::Text(viewport.profile);
		variables["screen_aspect"] =
			StateVariables::Value::Aspect(viewport.screenAspect);
		variables["vertical_alignment"] =
			StateVariables::Value::Text(viewport.verticalAlignment);
		variables["anamorphic_scale"] =
			StateVariables::Value::Aspect(viewport.anamorphicScale);
		variables["automatic_crop"] =
			StateVariables::Value::Boolean(viewport.automaticCrop);
		variables["subtitle_fit"] =
			StateVariables::Value::Boolean(viewport.subtitleFit);
		variables["subtitle_hold_seconds"] =
			StateVariables::Value::Number(
				viewport.subtitleHoldMilliseconds / 1000.0);
		variables["subtitle_engage_drift_ms"] =
			StateVariables::Value::Number(
				static_cast<double>(viewport.subtitleEngageDriftMilliseconds));
		variables["subtitle_release_drift_ms"] =
			StateVariables::Value::Number(
				static_cast<double>(viewport.subtitleReleaseDriftMilliseconds));
		variables["subtitle_padding_pixels"] =
			StateVariables::Value::Number(
				static_cast<double>(viewport.subtitlePaddingPixels));
		variables["subtitle_target_buffer_pixels"] =
			StateVariables::Value::Number(
				static_cast<double>(viewport.subtitleTargetBufferPixels));
		variables["viewport_generation"] =
			StateVariables::Value::Number(
				static_cast<double>(generation));
		for (const auto& selection : effective)
		{
			variables["profile." + selection.first] =
				StateVariables::Value::Text(selection.second);
			if (selection.first == "viewport")
			{
				std::string label = selection.second;
				const auto profile = m_model.profiles.find("viewport." + selection.second);
				if (profile != m_model.profiles.end() && !profile->second.label.empty())
					label = profile->second.label;
				variables["screen_config"] =
					StateVariables::Value::Text(label);
				variables["profile.viewport_name"] =
					StateVariables::Value::Text(label);
			}
		}

		std::shared_ptr<Snapshot> next(new Snapshot());
		next->generation = generation;
		next->manualSelections = manualSelections;
		next->effectiveSelections = effective;
		next->viewport = viewport;
		next->queue = queue;
		next->lldv = lldv;
		next->variables = StateVariables::Snapshot(
			generation, variables);
		snapshot = next;
		return true;
	}


	bool Runtime::IsPersistedSelectionValid(
		const std::string& groupName,
		const std::string& profileName) const
	{
		for (const RendererProfileConfig::Group& group : m_model.groups)
		{
			if (group.name != groupName || !group.persistSelection)
				continue;
			return std::find(group.profiles.begin(), group.profiles.end(),
				profileName) != group.profiles.end();
		}
		return false;
	}
}
