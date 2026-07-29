#include <pch.h>

#include <RendererTransitionModel.h>


RendererTransitionModel::Actions RendererTransitionModel::BeginReset(
	uint32_t rendererGeneration,
	uint64_t targetRevision)
{
	if (m_state != RendererTransitionState::Visible)
		return {};

	m_key.rendererGeneration = rendererGeneration;
	m_key.transitionToken = NextToken();
	m_key.targetRevision = targetRevision;
	m_shieldHeld = false;
	m_rebindPending = false;
	m_releaseIssued = false;
	m_targetReplacementPending = false;
	m_pendingRendererGeneration = 0;
	m_pendingTargetRevision = 0;
	m_state = RendererTransitionState::AcquiringBlack;
	return { MakeAction(RendererTransitionActionType::AcquireShield) };
}


RendererTransitionModel::Actions RendererTransitionModel::OnShieldAcquired(
	const RendererTransitionKey& key,
	bool succeeded)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::AcquiringBlack)
	{
		return {};
	}

	if (!succeeded)
	{
		m_state = RendererTransitionState::FailedCovered;
		return {};
	}

	m_shieldHeld = true;
	m_rebindPending = false;
	m_state = RendererTransitionState::BlackHeld;
	return { MakeAction(RendererTransitionActionType::ExecuteReset) };
}


RendererTransitionModel::Actions RendererTransitionModel::OnResetStarted(
	const RendererTransitionKey& key)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::BlackHeld ||
		m_rebindPending)
	{
		return {};
	}

	m_state = RendererTransitionState::Resetting;
	return {};
}


RendererTransitionModel::Actions RendererTransitionModel::OnResetCompleted(
	const RendererTransitionKey& key,
	bool succeeded)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::Resetting)
	{
		return {};
	}

	if (!succeeded)
	{
		m_targetReplacementPending = false;
		m_pendingRendererGeneration = 0;
		m_pendingTargetRevision = 0;
		m_state = RendererTransitionState::FailedCovered;
		return {
			MakeAction(
				RendererTransitionActionType::RequestRendererRebuild)
		};
	}

	if (m_targetReplacementPending)
	{
		const uint32_t rendererGeneration =
			m_pendingRendererGeneration;
		const uint64_t targetRevision =
			m_pendingTargetRevision;
		m_targetReplacementPending = false;
		m_pendingRendererGeneration = 0;
		m_pendingTargetRevision = 0;
		return ApplyCoveredTargetReplacement(
			rendererGeneration, targetRevision);
	}

	m_state = RendererTransitionState::AwaitingFrame;
	return {};
}


RendererTransitionModel::Actions RendererTransitionModel::OnFrameReady(
	const RendererTransitionKey& key)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::AwaitingFrame ||
		m_releaseIssued)
	{
		return {};
	}

	m_releaseIssued = true;
	m_state = RendererTransitionState::ReleasingBlack;
	return { MakeAction(RendererTransitionActionType::ReleaseShield) };
}


RendererTransitionModel::Actions RendererTransitionModel::RequestAnotherReset(
	const RendererTransitionKey& key)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::AwaitingFrame)
	{
		return {};
	}

	m_releaseIssued = false;
	m_state = RendererTransitionState::BlackHeld;
	return { MakeAction(RendererTransitionActionType::ExecuteReset) };
}


RendererTransitionModel::Actions
RendererTransitionModel::ReplaceCoveredTarget(
	uint32_t rendererGeneration,
	uint64_t targetRevision)
{
	if (!IsCovered() ||
		m_state == RendererTransitionState::ReleasingBlack)
	{
		return {};
	}

	if (m_state == RendererTransitionState::Resetting)
	{
		// The current graph operation owns the renderer until completion.
		// Retain only the newest target request and keep the existing shield
		// held; OnResetCompleted will emit the rebind action before any second
		// reset can be requested.
		m_targetReplacementPending = true;
		m_pendingRendererGeneration = rendererGeneration;
		m_pendingTargetRevision = targetRevision;
		return {};
	}

	return ApplyCoveredTargetReplacement(
		rendererGeneration, targetRevision);
}


RendererTransitionModel::Actions
RendererTransitionModel::ApplyCoveredTargetReplacement(
	uint32_t rendererGeneration,
	uint64_t targetRevision)
{
	// Preserve the transition token: the same opaque shield remains held while
	// its presentation target and renderer generation are replaced. Updating
	// the other key fields invalidates all in-flight completions for the old
	// target without creating an uncover/recover interval.
	m_key.rendererGeneration = rendererGeneration;
	m_key.targetRevision = targetRevision;
	m_rebindPending = true;
	m_releaseIssued = false;
	m_state = RendererTransitionState::BlackHeld;
	return {
		MakeAction(RendererTransitionActionType::RebindShieldTarget)
	};
}


RendererTransitionModel::Actions
RendererTransitionModel::OnShieldTargetRebound(
	const RendererTransitionKey& key,
	bool succeeded)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::BlackHeld ||
		!m_rebindPending)
	{
		return {};
	}

	if (!succeeded)
	{
		m_rebindPending = false;
		m_state = RendererTransitionState::FailedCovered;
		return {};
	}

	m_rebindPending = false;
	return { MakeAction(RendererTransitionActionType::ExecuteReset) };
}


RendererTransitionModel::Actions RendererTransitionModel::OnShieldReleased(
	const RendererTransitionKey& key,
	bool succeeded)
{
	if (!Matches(key) ||
		m_state != RendererTransitionState::ReleasingBlack)
	{
		return {};
	}

	if (!succeeded)
	{
		m_state = RendererTransitionState::FailedCovered;
		return {
			MakeAction(
				RendererTransitionActionType::RequestRendererRebuild)
		};
	}

	m_state = RendererTransitionState::Visible;
	m_shieldHeld = false;
	m_rebindPending = false;
	m_releaseIssued = false;
	m_targetReplacementPending = false;
	m_pendingRendererGeneration = 0;
	m_pendingTargetRevision = 0;
	return {};
}


RendererTransitionAction RendererTransitionModel::MakeAction(
	RendererTransitionActionType type) const
{
	RendererTransitionAction action;
	action.type = type;
	action.key = m_key;
	return action;
}


bool RendererTransitionModel::Matches(
	const RendererTransitionKey& key) const
{
	return key == m_key;
}


uint64_t RendererTransitionModel::NextToken()
{
	++m_nextTransitionToken;
	if (m_nextTransitionToken == 0)
		++m_nextTransitionToken;
	return m_nextTransitionToken;
}
