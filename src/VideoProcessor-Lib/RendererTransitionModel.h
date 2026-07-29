#pragma once

#include <cstdint>
#include <vector>


struct RendererTransitionKey
{
	uint32_t rendererGeneration = 0;
	uint64_t transitionToken = 0;
	uint64_t targetRevision = 0;

	bool operator==(const RendererTransitionKey& other) const
	{
		return rendererGeneration == other.rendererGeneration &&
			transitionToken == other.transitionToken &&
			targetRevision == other.targetRevision;
	}

	bool operator!=(const RendererTransitionKey& other) const
	{
		return !(*this == other);
	}
};


enum class RendererTransitionState
{
	Visible,
	AcquiringBlack,
	BlackHeld,
	Resetting,
	AwaitingFrame,
	ReleasingBlack,
	FailedCovered,
};


enum class RendererTransitionActionType
{
	AcquireShield,
	RebindShieldTarget,
	ExecuteReset,
	ReleaseShield,
	RequestRendererRebuild,
};


struct RendererTransitionAction
{
	RendererTransitionActionType type =
		RendererTransitionActionType::AcquireShield;
	RendererTransitionKey key;
};


// Pure application-level state model. It owns transition identity and reveal
// policy, but never touches HWNDs, MFC, graph control, or renderer objects.
// The host executes returned actions asynchronously and reports completion
// with the exact key carried by the action.
class RendererTransitionModel
{
public:
	using Actions = std::vector<RendererTransitionAction>;

	RendererTransitionState State() const { return m_state; }
	RendererTransitionKey Key() const { return m_key; }
	bool IsCovered() const { return m_shieldHeld; }

	Actions BeginReset(uint32_t rendererGeneration, uint64_t targetRevision);
	Actions OnShieldAcquired(
		const RendererTransitionKey& key, bool succeeded);
	Actions OnResetStarted(const RendererTransitionKey& key);
	Actions OnResetCompleted(
		const RendererTransitionKey& key, bool succeeded);
	Actions OnFrameReady(const RendererTransitionKey& key);
	Actions RequestAnotherReset(const RendererTransitionKey& key);
	Actions ReplaceCoveredTarget(
		uint32_t rendererGeneration, uint64_t targetRevision);
	Actions OnShieldTargetRebound(
		const RendererTransitionKey& key, bool succeeded);
	Actions OnShieldReleased(
		const RendererTransitionKey& key, bool succeeded);

private:
	RendererTransitionAction MakeAction(
		RendererTransitionActionType type) const;
	Actions ApplyCoveredTargetReplacement(
		uint32_t rendererGeneration, uint64_t targetRevision);
	bool Matches(const RendererTransitionKey& key) const;
	uint64_t NextToken();

	RendererTransitionState m_state = RendererTransitionState::Visible;
	RendererTransitionKey m_key;
	uint64_t m_nextTransitionToken = 0;
	bool m_shieldHeld = false;
	bool m_rebindPending = false;
	bool m_releaseIssued = false;
	bool m_targetReplacementPending = false;
	uint32_t m_pendingRendererGeneration = 0;
	uint64_t m_pendingTargetRevision = 0;
};
