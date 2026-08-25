#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


enum class RendererQueueLaunchBackend
{
	DirectShow,
	Alpha,
};


struct RendererQueueLaunchEffectiveKey
{
	RendererQueueLaunchBackend backend =
		RendererQueueLaunchBackend::DirectShow;
	size_t capacity = 0;

	bool operator==(const RendererQueueLaunchEffectiveKey& other) const
	{
		return backend == other.backend && capacity == other.capacity;
	}

	bool operator!=(const RendererQueueLaunchEffectiveKey& other) const
	{
		return !(*this == other);
	}
};


// effectiveRevision is the monotonic UI ordering identity. A unified-profile
// generation can be used directly when it is the ordering source.
struct RendererQueueLaunchDesired
{
	RendererQueueLaunchEffectiveKey key;
	uint64_t effectiveRevision = 0;
	uint64_t profileGeneration = 0;

	bool operator==(const RendererQueueLaunchDesired& other) const
	{
		return key == other.key &&
			effectiveRevision == other.effectiveRevision &&
			profileGeneration == other.profileGeneration;
	}

	bool operator!=(const RendererQueueLaunchDesired& other) const
	{
		return !(*this == other);
	}
};


enum class RendererQueueLaunchStartReason
{
	Normal,
	ManualRetry,
};


struct RendererQueueLaunchConstructionSnapshot
{
	bool accepted = false;
	uint32_t rendererGeneration = 0;
	RendererQueueLaunchDesired desired;
};


struct RendererQueueLaunchStableAudit
{
	uint32_t rendererGeneration = 0;
	RendererQueueLaunchDesired desired;
	RendererQueueLaunchEffectiveKey constructed;
	RendererQueueLaunchEffectiveKey current;
	bool constructionSettled = true;
	bool resetInProgress = false;
};


enum class RendererQueueLaunchActionType
{
	None,
	RequestCoveredRecreation,
	FailCovered,
};


struct RendererQueueLaunchAction
{
	RendererQueueLaunchActionType type =
		RendererQueueLaunchActionType::None;
	uint32_t rendererGeneration = 0;
	RendererQueueLaunchDesired desired;
	RendererQueueLaunchEffectiveKey constructed;
	RendererQueueLaunchEffectiveKey current;
};


// Pure UI lifecycle model. It owns ordering and retry policy, but never calls
// graph control, creates a renderer, or manipulates a transition cover.
class RendererQueueLaunchContractModel
{
public:
	// Publishes the selected backend's latest effective construction contract.
	// Revisions are globally ordered so a late notification from either backend
	// cannot replace a newer renderer-selection intent.
	bool PublishDesired(const RendererQueueLaunchDesired& desired);

	// Starts a renderer generation from the selected backend-specific contract.
	// CommitConstruction takes a fresh snapshot, allowing changes published
	// during construction to coalesce at the construction linearization point.
	RendererQueueLaunchConstructionSnapshot StartConstruction(
		uint32_t rendererGeneration,
		RendererQueueLaunchBackend backend,
		RendererQueueLaunchStartReason reason =
			RendererQueueLaunchStartReason::Normal);
	RendererQueueLaunchConstructionSnapshot CommitConstruction(
		uint32_t rendererGeneration);
	RendererQueueLaunchConstructionSnapshot CommitConstruction(
		uint32_t rendererGeneration,
		const RendererQueueLaunchDesired& constructedDesired);

	// Consumes complete, stable evidence. Calls made during construction/reset,
	// for stale generations, or with stale desired evidence are ignored.
	RendererQueueLaunchAction ConsumeStableAudit(
		const RendererQueueLaunchStableAudit& audit);

	// The host should read this immediately before executing a lifecycle action.
	// It is refreshed by newer desired revisions and becomes None when a revert
	// makes the current renderer consistent before dispatch.
	RendererQueueLaunchAction PendingAction() const
	{
		return m_pendingAction;
	}

	bool LatestDesired(RendererQueueLaunchDesired& desired) const;
	bool DesiredForBackend(
		RendererQueueLaunchBackend backend,
		RendererQueueLaunchDesired& desired) const;
	bool IsTerminal(
		RendererQueueLaunchBackend backend,
		const RendererQueueLaunchEffectiveKey& key) const;
	bool RecreationInFlight() const { return m_recreationInFlight; }

private:
	struct BackendState
	{
		bool hasDesired = false;
		RendererQueueLaunchDesired desired;
		bool terminal = false;
		RendererQueueLaunchEffectiveKey terminalKey;
	};

	struct ActiveConstruction
	{
		bool present = false;
		bool committed = false;
		uint32_t rendererGeneration = 0;
		RendererQueueLaunchBackend backend =
			RendererQueueLaunchBackend::DirectShow;
		RendererQueueLaunchDesired committedDesired;
		bool recoverySuccessor = false;
		RendererQueueLaunchEffectiveKey recoveryKey;
	};

	static size_t BackendIndex(RendererQueueLaunchBackend backend);
	static RendererQueueLaunchAction NoAction();
	RendererQueueLaunchAction MakeAction(
		RendererQueueLaunchActionType type,
		const RendererQueueLaunchStableAudit& audit) const;
	bool AuditMatchesCurrentIntent(
		const RendererQueueLaunchStableAudit& audit) const;
	bool AuditIsConsistent(
		const RendererQueueLaunchStableAudit& audit) const;
	void ClearPendingAction();
	void RefreshOrCancelPendingAction();

	std::array<BackendState, 2> m_backends;
	bool m_hasLatestDesired = false;
	RendererQueueLaunchDesired m_latestDesired;
	uint32_t m_latestRendererGeneration = 0;
	ActiveConstruction m_active;
	bool m_hasStableAudit = false;
	RendererQueueLaunchStableAudit m_lastStableAudit;
	bool m_recreationInFlight = false;
	RendererQueueLaunchEffectiveKey m_recreationKey;
	RendererQueueLaunchAction m_pendingAction;
};
