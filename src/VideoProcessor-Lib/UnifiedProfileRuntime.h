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
		StateVariables::Snapshot variables;

		bool LookupVariable(const std::string& name, std::string& value) const
		{
			return variables.Lookup(name, value);
		}
	};

	struct SelectionResult
	{
		bool changed = false;
		std::vector<RendererProfileConfig::KeySelection> selections;
		std::shared_ptr<const Snapshot> snapshot;
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

		bool SelectKey(const std::string& key,
			const DisplayRuleExpression::ValueLookup& sourceValues,
			SelectionResult& result, std::string& error);

		// Re-resolves automatic profiles after source state changes. Manual
		// selections remain authoritative until reset by their group expression.
		bool Refresh(const DisplayRuleExpression::ValueLookup& sourceValues,
			bool& changed, std::string& error);

		std::shared_ptr<const Snapshot> GetSnapshot() const;
		bool IsInitialized() const;
		std::string StatePath() const;

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
		bool IsPersistedSelectionValid(const std::string& group,
			const std::string& profile) const;

		mutable std::mutex m_mutex;
		RendererProfileConfig::Model m_model;
		std::string m_statePath;
		bool m_initialized = false;
		uint64_t m_generation = 0;
		std::shared_ptr<const Snapshot> m_snapshot;
	};
}
