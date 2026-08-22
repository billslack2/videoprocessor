#pragma once

#include "RendererProfileConfig.h"
#include "StateVariables.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


namespace UnifiedProfileRuntime
{
	struct Snapshot
	{
		uint64_t generation = 0;
		// Manual selections are the durable user choices. Effective selections
		// also include configured defaults and automatic source-driven choices.
		std::map<std::string, std::string> manualSelections;
		std::map<std::string, std::string> effectiveSelections;
		RendererProfileConfig::ResolvedViewport viewport;
		RendererProfileConfig::ResolvedQueue queue;
		RendererProfileConfig::ResolvedLldv lldv;
		StateVariables::Snapshot variables;

		bool LookupVariable(const std::string& name, std::string& value) const
		{
			return variables.Lookup(name, value);
		}
	};

	// A resolved action launch request. Matching happens against the immutable
	// snapshots at the transition boundary; the UI owns asynchronous process
	// launch and cancellation.
	struct ActionInvocation
	{
		RendererProfileConfig::Model::EventAction action;
		std::string event;
		std::string reason;
	};

	struct SelectionResult
	{
		bool changed = false;
		std::vector<RendererProfileConfig::KeySelection> selections;
		std::shared_ptr<const Snapshot> snapshot;
		std::vector<ActionInvocation> actions;
	};

	struct RefreshResult
	{
		bool changed = false;
		std::shared_ptr<const Snapshot> snapshot;
		std::vector<ActionInvocation> actions;
	};

	// Application-owned coordinator for unified profile selection. Renderer
	// backends consume Snapshot values; they do not parse keys or persist state.
	class Runtime
	{
	public:
		Runtime();

		bool Initialize(const ConfigFile& config,
			const DisplayRuleExpression::ValueLookup& sourceValues,
			std::string& error);

		// Replaces the parsed configuration while retaining valid manual profile
		// selections. The newly resolved snapshot is published atomically only
		// after the complete candidate configuration has resolved successfully.
		bool Reload(const ConfigFile& config,
			const DisplayRuleExpression::ValueLookup& sourceValues,
			RefreshResult& result, std::string& error);

		bool SelectKey(const std::string& key,
			const DisplayRuleExpression::ValueLookup& sourceValues,
			SelectionResult& result, std::string& error);

		// Re-resolves automatic profiles after source state changes. Manual
		// selections remain authoritative until reset by their group expression.
		bool Refresh(const DisplayRuleExpression::ValueLookup& sourceValues,
			RefreshResult& result, std::string& error);

		// Emits an event against the current committed snapshot. This is used for
		// renderer-ready, which has no profile transition of its own.
		bool CollectActionInvocations(const std::string& event,
			const std::string& reason,
			const std::shared_ptr<const Snapshot>& previous,
			const std::shared_ptr<const Snapshot>& current,
			std::vector<ActionInvocation>& actions, std::string& error) const;

		std::shared_ptr<const Snapshot> GetSnapshot() const;
		bool IsInitialized() const;
		std::string StatePath() const;
		std::string ConfigPath() const;

	private:
		bool LoadPersistedSelections(
			std::map<std::string, std::string>& selections,
			std::string& error) const;
		bool PersistSelections(
			const std::map<std::string, std::string>& selections,
			std::string& error) const;
		bool BuildSnapshot(
			const std::map<std::string, std::string>& manualSelections,
			const DisplayRuleExpression::ValueLookup& sourceValues,
			uint64_t generation, std::shared_ptr<const Snapshot>& snapshot,
			std::string& error) const;
		bool CollectTransitionActionInvocations(
			const std::shared_ptr<const Snapshot>& previous,
			const std::shared_ptr<const Snapshot>& current,
			const std::string& reason,
			std::vector<ActionInvocation>& actions, std::string& error) const;
		bool CollectActionInvocationsUnlocked(const std::string& event,
			const std::string& reason,
			const std::shared_ptr<const Snapshot>& previous,
			const std::shared_ptr<const Snapshot>& current,
			std::vector<ActionInvocation>& actions, std::string& error) const;
		bool IsPersistedSelectionValid(const std::string& group,
			const std::string& profile) const;

		mutable std::mutex m_mutex;
		RendererProfileConfig::Model m_model;
		std::string m_statePath;
		std::string m_configPath;
		bool m_initialized = false;
		uint64_t m_generation = 0;
		std::shared_ptr<const Snapshot> m_snapshot;
	};
}
