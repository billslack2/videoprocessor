#pragma once

#include <cstdint>
#include <string>

// Keeps the UI-side restart trigger tied to a committed, user-initiated queue
// selection. The renderer lifecycle owns execution; this policy only retains
// the newest request while the shortcut burst settles.
namespace QueueProfileRestartPolicy
{
	struct PendingRequest
	{
		bool pending = false;
		uint64_t snapshotGeneration = 0;
		std::string profile;
		std::string source;
	};

	enum class EnqueueResult
	{
		Ignored,
		Queued,
		Coalesced
	};

	inline bool RequiresRestartAfterManualSelection(bool selectionChanged,
		const std::string& previousProfile, const std::string& currentProfile)
	{
		return selectionChanged && !currentProfile.empty() &&
			previousProfile != currentProfile;
	}

	inline EnqueueResult Enqueue(PendingRequest& pending,
		uint64_t snapshotGeneration, const std::string& profile,
		const std::string& source)
	{
		if (profile.empty())
			return EnqueueResult::Ignored;
		const EnqueueResult result = pending.pending ?
			EnqueueResult::Coalesced : EnqueueResult::Queued;
		pending.pending = true;
		pending.snapshotGeneration = snapshotGeneration;
		pending.profile = profile;
		pending.source = source;
		return result;
	}

	inline bool Consume(PendingRequest& pending, PendingRequest& request)
	{
		if (!pending.pending)
			return false;
		request = pending;
		pending = {};
		return true;
	}
}
