#pragma once

#include <cstdint>
#include <string>

// Keeps the UI-side reset trigger tied to a committed, user-initiated queue
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

	// Evidence that a newly constructed renderer has consumed a queue profile.
	// The host publishes this only after the fresh renderer is successfully
	// running. DirectShow additionally proves that its immutable construction
	// contract agrees with the current effective queue contract.
	struct FreshConstructionEvidence
	{
		bool freshConstruction = false;
		bool running = false;
		bool directShow = false;
		uint32_t rendererGeneration = 0;
		uint64_t appliedSnapshotGeneration = 0;
		std::string appliedProfile;
		uint64_t currentSnapshotGeneration = 0;
		std::string currentProfile;
		bool directShowAuditComplete = false;
		bool directShowAuditConsistent = false;
	};

	enum class EnqueueResult
	{
		Ignored,
		Queued,
		Coalesced
	};

	inline bool RequiresResetAfterManualSelection(bool queueWasSelected,
		const std::string& currentProfile, bool effectiveSelectionChanged)
	{
		return queueWasSelected && effectiveSelectionChanged &&
			!currentProfile.empty();
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

	// A synchronous reset submission can lose its coordinator binding between
	// the UI's readiness check and submission. Restore the exact consumed
	// identity in that case, but never overwrite a newer request.
	inline bool RestoreConsumedIfEmpty(PendingRequest& pending,
		const PendingRequest& request)
	{
		if (pending.pending || !request.pending || request.profile.empty())
			return false;
		pending = request;
		return true;
	}

	inline bool IsSatisfiedByFreshConstruction(
		const PendingRequest& pending,
		const FreshConstructionEvidence& evidence)
	{
		if (!pending.pending || pending.snapshotGeneration == 0 ||
			!evidence.freshConstruction || !evidence.running ||
			evidence.rendererGeneration == 0 ||
			evidence.appliedSnapshotGeneration == 0 ||
			evidence.currentSnapshotGeneration == 0)
		{
			return false;
		}

		// The running renderer must still represent the exact current snapshot.
		// A pending request from an older generation of the same profile is also
		// satisfied because the fresh construction consumed its newer successor.
		if (evidence.appliedSnapshotGeneration !=
				evidence.currentSnapshotGeneration ||
			pending.snapshotGeneration > evidence.appliedSnapshotGeneration ||
			evidence.appliedProfile.empty() ||
			evidence.appliedProfile != evidence.currentProfile ||
			pending.profile != evidence.appliedProfile)
		{
			return false;
		}

		if (evidence.directShow &&
			(!evidence.directShowAuditComplete ||
				!evidence.directShowAuditConsistent))
		{
			return false;
		}

		return true;
	}

	// On success, return the original request for coherent source/generation
	// logging. A failed proof leaves both the pending request and output intact.
	inline bool ConsumeIfSatisfiedByFreshConstruction(
		PendingRequest& pending,
		const FreshConstructionEvidence& evidence,
		PendingRequest& satisfiedRequest)
	{
		if (!IsSatisfiedByFreshConstruction(pending, evidence))
			return false;
		satisfiedRequest = pending;
		pending = {};
		return true;
	}
}
