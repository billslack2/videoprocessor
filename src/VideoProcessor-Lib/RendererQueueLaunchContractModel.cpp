#include "pch.h"
#include <RendererQueueLaunchContractModel.h>


bool RendererQueueLaunchContractModel::PublishDesired(
	const RendererQueueLaunchDesired& desired)
{
	if (desired.key.capacity == 0 || desired.effectiveRevision == 0)
		return false;
	if (m_hasLatestDesired &&
		desired.effectiveRevision < m_latestDesired.effectiveRevision)
	{
		return false;
	}
	if (m_hasLatestDesired &&
		desired.effectiveRevision == m_latestDesired.effectiveRevision &&
		(desired.key != m_latestDesired.key ||
			desired.profileGeneration < m_latestDesired.profileGeneration))
	{
		return false;
	}

	const bool backendChanged = m_hasLatestDesired &&
		desired.key.backend != m_latestDesired.key.backend;
	if (backendChanged)
	{
		// A backend handoff starts a new global renderer lineage. A terminal
		// contract from either side must not survive a round trip and suppress
		// the one covered recovery allowed to the newly selected lineage.
		for (BackendState& state : m_backends)
			state.terminal = false;
		m_recreationInFlight = false;
		ClearPendingAction();
	}

	BackendState& backend = m_backends[BackendIndex(desired.key.backend)];
	backend.hasDesired = true;
	backend.desired = desired;
	if (backend.terminal && backend.terminalKey != desired.key)
	{
		backend.terminal = false;
		if (m_pendingAction.type ==
			RendererQueueLaunchActionType::FailCovered)
		{
			ClearPendingAction();
		}
	}

	m_hasLatestDesired = true;
	m_latestDesired = desired;
	if (m_recreationInFlight)
		m_recreationKey = desired.key;
	RefreshOrCancelPendingAction();
	return true;
}


RendererQueueLaunchConstructionSnapshot
RendererQueueLaunchContractModel::StartConstruction(
	uint32_t rendererGeneration,
	RendererQueueLaunchBackend backend,
	RendererQueueLaunchStartReason reason)
{
	RendererQueueLaunchConstructionSnapshot result;
	if (rendererGeneration == 0 ||
		rendererGeneration <= m_latestRendererGeneration ||
		!m_hasLatestDesired || m_latestDesired.key.backend != backend)
	{
		return result;
	}

	BackendState& backendState = m_backends[BackendIndex(backend)];
	if (!backendState.hasDesired)
		return result;

	if (reason == RendererQueueLaunchStartReason::ManualRetry)
	{
		backendState.terminal = false;
		m_recreationInFlight = false;
		ClearPendingAction();
	}

	ActiveConstruction active;
	active.present = true;
	active.rendererGeneration = rendererGeneration;
	active.backend = backend;
	active.recoverySuccessor =
		reason == RendererQueueLaunchStartReason::Normal &&
		m_recreationInFlight && m_recreationKey == m_latestDesired.key;
	if (active.recoverySuccessor)
		active.recoveryKey = m_recreationKey;

	m_active = active;
	m_latestRendererGeneration = rendererGeneration;
	m_hasStableAudit = false;
	m_recreationInFlight = false;
	ClearPendingAction();

	result.accepted = true;
	result.rendererGeneration = rendererGeneration;
	result.desired = backendState.desired;
	return result;
}


RendererQueueLaunchConstructionSnapshot
RendererQueueLaunchContractModel::CommitConstruction(
	uint32_t rendererGeneration)
{
	if (!m_active.present)
		return {};
	const BackendState& backend =
		m_backends[BackendIndex(m_active.backend)];
	if (!backend.hasDesired)
		return {};
	return CommitConstruction(rendererGeneration, backend.desired);
}


RendererQueueLaunchConstructionSnapshot
RendererQueueLaunchContractModel::CommitConstruction(
	uint32_t rendererGeneration,
	const RendererQueueLaunchDesired& constructedDesired)
{
	RendererQueueLaunchConstructionSnapshot result;
	if (!m_active.present || m_active.committed ||
		rendererGeneration != m_active.rendererGeneration ||
		constructedDesired.key.backend != m_active.backend ||
		constructedDesired.key.capacity == 0 ||
		constructedDesired.effectiveRevision == 0)
	{
		return result;
	}

	m_active.committed = true;
	m_active.committedDesired = constructedDesired;
	result.accepted = true;
	result.rendererGeneration = rendererGeneration;
	result.desired = constructedDesired;
	return result;
}


RendererQueueLaunchAction
RendererQueueLaunchContractModel::ConsumeStableAudit(
	const RendererQueueLaunchStableAudit& audit)
{
	if (!m_active.present || !m_active.committed ||
		!audit.constructionSettled || audit.resetInProgress ||
		audit.rendererGeneration != m_active.rendererGeneration ||
		!AuditMatchesCurrentIntent(audit))
	{
		return NoAction();
	}

	m_hasStableAudit = true;
	m_lastStableAudit = audit;
	if (AuditIsConsistent(audit))
	{
		m_active.recoverySuccessor = false;
		m_recreationInFlight = false;
		ClearPendingAction();
		return NoAction();
	}

	BackendState& desiredBackend =
		m_backends[BackendIndex(audit.desired.key.backend)];
	const bool repeatedRecoveryFailure =
		m_active.recoverySuccessor &&
		m_active.recoveryKey == audit.desired.key;
	const bool terminalContract = desiredBackend.terminal &&
		desiredBackend.terminalKey == audit.desired.key;
	if (repeatedRecoveryFailure || terminalContract)
	{
		desiredBackend.terminal = true;
		desiredBackend.terminalKey = audit.desired.key;
		m_recreationInFlight = false;
		if (m_pendingAction.type ==
			RendererQueueLaunchActionType::FailCovered &&
			m_pendingAction.rendererGeneration == audit.rendererGeneration &&
			m_pendingAction.desired.key == audit.desired.key)
		{
			return NoAction();
		}
		m_pendingAction = MakeAction(
			RendererQueueLaunchActionType::FailCovered, audit);
		return m_pendingAction;
	}

	if (m_recreationInFlight)
	{
		RefreshOrCancelPendingAction();
		return NoAction();
	}

	m_recreationInFlight = true;
	m_recreationKey = audit.desired.key;
	m_pendingAction = MakeAction(
		RendererQueueLaunchActionType::RequestCoveredRecreation, audit);
	return m_pendingAction;
}


bool RendererQueueLaunchContractModel::LatestDesired(
	RendererQueueLaunchDesired& desired) const
{
	if (!m_hasLatestDesired)
		return false;
	desired = m_latestDesired;
	return true;
}


bool RendererQueueLaunchContractModel::DesiredForBackend(
	RendererQueueLaunchBackend backend,
	RendererQueueLaunchDesired& desired) const
{
	const BackendState& state = m_backends[BackendIndex(backend)];
	if (!state.hasDesired)
		return false;
	desired = state.desired;
	return true;
}


bool RendererQueueLaunchContractModel::IsTerminal(
	RendererQueueLaunchBackend backend,
	const RendererQueueLaunchEffectiveKey& key) const
{
	const BackendState& state = m_backends[BackendIndex(backend)];
	return state.terminal && state.terminalKey == key;
}


size_t RendererQueueLaunchContractModel::BackendIndex(
	RendererQueueLaunchBackend backend)
{
	return backend == RendererQueueLaunchBackend::DirectShow ? 0 : 1;
}


RendererQueueLaunchAction RendererQueueLaunchContractModel::NoAction()
{
	return {};
}


RendererQueueLaunchAction RendererQueueLaunchContractModel::MakeAction(
	RendererQueueLaunchActionType type,
	const RendererQueueLaunchStableAudit& audit) const
{
	RendererQueueLaunchAction action;
	action.type = type;
	action.rendererGeneration = audit.rendererGeneration;
	action.desired = m_latestDesired;
	action.constructed = audit.constructed;
	action.current = audit.current;
	return action;
}


bool RendererQueueLaunchContractModel::AuditMatchesCurrentIntent(
	const RendererQueueLaunchStableAudit& audit) const
{
	return m_hasLatestDesired && audit.desired == m_latestDesired;
}


bool RendererQueueLaunchContractModel::AuditIsConsistent(
	const RendererQueueLaunchStableAudit& audit) const
{
	return audit.desired.key == audit.constructed &&
		audit.constructed == audit.current &&
		m_active.committedDesired.key == audit.constructed;
}


void RendererQueueLaunchContractModel::ClearPendingAction()
{
	m_pendingAction = NoAction();
}


void RendererQueueLaunchContractModel::RefreshOrCancelPendingAction()
{
	if (m_pendingAction.type == RendererQueueLaunchActionType::None ||
		!m_hasLatestDesired)
	{
		return;
	}

	if (m_pendingAction.type ==
		RendererQueueLaunchActionType::RequestCoveredRecreation)
	{
		const bool revertedToConsistent = m_hasStableAudit &&
			m_lastStableAudit.rendererGeneration ==
				m_active.rendererGeneration &&
			m_lastStableAudit.constructed == m_lastStableAudit.current &&
			m_active.committedDesired.key ==
				m_lastStableAudit.constructed &&
			m_latestDesired.key == m_lastStableAudit.constructed;
		if (revertedToConsistent)
		{
			m_recreationInFlight = false;
			ClearPendingAction();
			return;
		}
		m_pendingAction.desired = m_latestDesired;
		return;
	}

	const BackendState& backend =
		m_backends[BackendIndex(m_latestDesired.key.backend)];
	if (!backend.terminal || backend.terminalKey != m_latestDesired.key)
		ClearPendingAction();
	else
		m_pendingAction.desired = m_latestDesired;
}
