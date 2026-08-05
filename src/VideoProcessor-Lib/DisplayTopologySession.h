#pragma once

#include <string>

namespace DisplayTopologySession
{
	// Restores a durable pre-session topology, if present. A successful no-op
	// returns true with restored=false. The recovery record is removed only
	// after SetDisplayConfig succeeds and the restored targets are active.
	bool RestorePending(const std::string& statePath, const char* reason,
		bool& restored, std::string& error);

	bool HasPendingRecovery(const std::string& statePath);

	// Captures and persists the current topology, then atomically replaces it
	// with the one uniquely connected target matching friendlyName. The target
	// may currently be inactive. Existing topology is restored on any failure
	// after persistence.
	bool BeginTargetOnly(const std::wstring& friendlyName,
		const std::string& statePath, std::string& error);
}
