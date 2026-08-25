#include "pch.h"
#include "DirectShowQueueConstructionContract.h"

#include <climits>


DirectShowQueueConstructionContract::PublishResult
DirectShowQueueConstructionContract::PublishDesired(
	size_t capacity,
	uint64_t revision,
	uint64_t profileGeneration)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	PublishResult result;
	result.committed = m_committed;
	if (capacity == 0 || capacity > static_cast<size_t>(INT_MAX))
	{
		result.disposition = PublishDisposition::InvalidCapacity;
		if (m_desiredKnown)
			result.desired = m_desired;
		result.recreationRequired = RecreationRequired();
		return result;
	}

	if (m_desiredKnown && revision < m_desired.revision)
	{
		result.disposition = PublishDisposition::StaleRevision;
		result.desired = m_desired;
		result.recreationRequired = RecreationRequired();
		return result;
	}

	if (m_desiredKnown && revision == m_desired.revision &&
		(capacity != m_desired.capacity ||
			profileGeneration < m_desired.profileGeneration))
	{
		// One ordering revision must identify one coherent publication. Reject a
		// conflicting reuse instead of manufacturing an order for it.
		result.disposition = PublishDisposition::StaleRevision;
		result.desired = m_desired;
		result.recreationRequired = RecreationRequired();
		return result;
	}

	const bool sameEffectiveValue =
		m_desiredKnown && capacity == m_desired.capacity;
	m_desiredKnown = true;
	m_desired.capacity = capacity;
	m_desired.revision = revision;
	m_desired.profileGeneration = profileGeneration;

	result.disposition = sameEffectiveValue ?
		PublishDisposition::SameEffectiveValue :
		PublishDisposition::ChangedEffectiveValue;
	result.accepted = true;
	result.desired = m_desired;
	result.recreationRequired = RecreationRequired();
	return result;
}


DirectShowQueueConstructionContract::CommitResult
DirectShowQueueConstructionContract::Commit(uint32_t rendererGeneration)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	CommitResult result;
	if (m_committed)
	{
		result.disposition = CommitDisposition::AlreadyCommitted;
		result.committed = true;
		result.key = m_key;
		result.constructedCapacity = m_constructedCapacity;
		result.constructedProfileGeneration =
			m_constructedProfileGeneration;
		return result;
	}
	if (!m_desiredKnown)
	{
		result.disposition = CommitDisposition::NoDesiredContract;
		return result;
	}
	if (rendererGeneration == 0)
	{
		result.disposition =
			CommitDisposition::InvalidRendererGeneration;
		return result;
	}

	m_committed = true;
	m_key.rendererGeneration = rendererGeneration;
	m_key.contractRevision = m_desired.revision;
	m_constructedCapacity = m_desired.capacity;
	m_constructedProfileGeneration = m_desired.profileGeneration;

	result.disposition = CommitDisposition::Committed;
	result.committed = true;
	result.key = m_key;
	result.constructedCapacity = m_constructedCapacity;
	result.constructedProfileGeneration = m_constructedProfileGeneration;
	return result;
}


bool DirectShowQueueConstructionContract::RecordAllocator(
	const Key& key,
	size_t requestedBuffers,
	size_t actualBuffers)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!MatchesCommittedKey(key))
		return false;

	m_allocatorRecorded = true;
	m_allocatorRequested = requestedBuffers;
	m_allocatorActual = actualBuffers;
	return true;
}


bool DirectShowQueueConstructionContract::RecordLaunch(
	const Key& key,
	size_t primeTarget,
	size_t reservoirFrames,
	const DownstreamEstimate& downstreamEstimate)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!MatchesCommittedKey(key))
		return false;

	m_launchRecorded = true;
	m_primeTarget = primeTarget;
	m_reservoirFrames = reservoirFrames;
	m_downstreamEstimateKnown = downstreamEstimate.known;
	m_downstreamEstimateFrames = downstreamEstimate.known ?
		downstreamEstimate.frames : 0;
	m_estimateSatisfaction = !downstreamEstimate.known ?
		EstimateSatisfaction::Unavailable :
		(reservoirFrames >= downstreamEstimate.frames ?
			EstimateSatisfaction::Satisfied :
			EstimateSatisfaction::Unsatisfied);
	return true;
}


bool DirectShowQueueConstructionContract::Activate(
	const Key& key,
	size_t currentCapacity)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!MatchesCommittedKey(key))
		return false;

	m_activationRecorded = true;
	m_currentCapacityKnown = true;
	m_currentCapacity = currentCapacity;
	return true;
}


DirectShowQueueConstructionContract::Snapshot
DirectShowQueueConstructionContract::GetSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);

	Snapshot snapshot;
	snapshot.phase = CurrentPhase();
	snapshot.desiredKnown = m_desiredKnown;
	snapshot.desired = m_desired;
	snapshot.committed = m_committed;
	snapshot.key = m_key;
	snapshot.constructedCapacity = m_constructedCapacity;
	snapshot.constructedProfileGeneration =
		m_constructedProfileGeneration;
	snapshot.currentCapacityKnown = m_currentCapacityKnown;
	snapshot.currentCapacity = m_currentCapacity;
	snapshot.recreationRequired = RecreationRequired();
	snapshot.allocatorRecorded = m_allocatorRecorded;
	snapshot.allocatorRequested = m_allocatorRequested;
	snapshot.allocatorActual = m_allocatorActual;
	snapshot.launchRecorded = m_launchRecorded;
	snapshot.primeTarget = m_primeTarget;
	snapshot.reservoirFrames = m_reservoirFrames;
	snapshot.downstreamEstimateKnown = m_downstreamEstimateKnown;
	snapshot.downstreamEstimateFrames = m_downstreamEstimateFrames;
	snapshot.estimateSatisfaction = m_estimateSatisfaction;
	snapshot.activationRecorded = m_activationRecorded;
	return snapshot;
}


bool DirectShowQueueConstructionContract::MatchesCommittedKey(
	const Key& key) const
{
	return m_committed && key == m_key;
}


bool DirectShowQueueConstructionContract::RecreationRequired() const
{
	if (!m_committed)
		return false;
	return (m_desiredKnown &&
		m_desired.capacity != m_constructedCapacity) ||
		(m_currentCapacityKnown &&
			m_currentCapacity != m_constructedCapacity);
}


DirectShowQueueConstructionContract::Phase
DirectShowQueueConstructionContract::CurrentPhase() const
{
	if (RecreationRequired())
		return Phase::Mismatch;
	if (m_committed)
	{
		if (m_activationRecorded && m_allocatorRecorded && m_launchRecorded)
		{
			return m_estimateSatisfaction ==
				EstimateSatisfaction::Unsatisfied ?
				Phase::Unsatisfied : Phase::Active;
		}
		return Phase::Committed;
	}
	return m_desiredKnown ? Phase::Desired : Phase::Empty;
}
