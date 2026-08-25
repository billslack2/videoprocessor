#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>


// Retains the DirectShow queue capacity selected before source construction.
// All state transitions are linearized by one mutex so capacity and revision
// can never be observed from different publications.
class DirectShowQueueConstructionContract
{
public:
	struct Desired
	{
		size_t capacity = 0;
		uint64_t revision = 0;
		uint64_t profileGeneration = 0;
	};

	struct Key
	{
		uint32_t rendererGeneration = 0;
		uint64_t contractRevision = 0;

		bool operator==(const Key& other) const
		{
			return rendererGeneration == other.rendererGeneration &&
				contractRevision == other.contractRevision;
		}

		bool operator!=(const Key& other) const
		{
			return !(*this == other);
		}
	};

	enum class Phase
	{
		Empty,
		Desired,
		Committed,
		Active,
		Unsatisfied,
		Mismatch,
	};

	enum class EstimateSatisfaction
	{
		Unavailable,
		Unsatisfied,
		Satisfied,
	};

	enum class PublishDisposition
	{
		InvalidCapacity,
		StaleRevision,
		SameEffectiveValue,
		ChangedEffectiveValue,
	};

	struct PublishResult
	{
		PublishDisposition disposition =
			PublishDisposition::InvalidCapacity;
		bool accepted = false;
		bool committed = false;
		bool recreationRequired = false;
		Desired desired;
	};

	enum class CommitDisposition
	{
		Committed,
		AlreadyCommitted,
		NoDesiredContract,
		InvalidRendererGeneration,
	};

	struct CommitResult
	{
		CommitDisposition disposition =
			CommitDisposition::NoDesiredContract;
		bool committed = false;
		Key key;
		size_t constructedCapacity = 0;
		uint64_t constructedProfileGeneration = 0;
	};

	struct DownstreamEstimate
	{
		bool known = false;
		size_t frames = 0;
	};

	struct Snapshot
	{
		Phase phase = Phase::Empty;
		bool desiredKnown = false;
		Desired desired;

		bool committed = false;
		Key key;
		size_t constructedCapacity = 0;
		uint64_t constructedProfileGeneration = 0;

		bool currentCapacityKnown = false;
		size_t currentCapacity = 0;
		bool recreationRequired = false;

		bool allocatorRecorded = false;
		size_t allocatorRequested = 0;
		size_t allocatorActual = 0;

		bool launchRecorded = false;
		size_t primeTarget = 0;
		size_t reservoirFrames = 0;
		bool downstreamEstimateKnown = false;
		size_t downstreamEstimateFrames = 0;
		EstimateSatisfaction estimateSatisfaction =
			EstimateSatisfaction::Unavailable;

		bool activationRecorded = false;
	};

	PublishResult PublishDesired(
		size_t capacity,
		uint64_t revision,
		uint64_t profileGeneration);

	CommitResult Commit(uint32_t rendererGeneration);

	bool RecordAllocator(
		const Key& key,
		size_t requestedBuffers,
		size_t actualBuffers);

	bool RecordLaunch(
		const Key& key,
		size_t primeTarget,
		size_t reservoirFrames,
		const DownstreamEstimate& downstreamEstimate);

	bool Activate(const Key& key, size_t currentCapacity);

	Snapshot GetSnapshot() const;

private:
	bool MatchesCommittedKey(const Key& key) const;
	bool RecreationRequired() const;
	Phase CurrentPhase() const;

	mutable std::mutex m_mutex;
	bool m_desiredKnown = false;
	Desired m_desired;

	bool m_committed = false;
	Key m_key;
	size_t m_constructedCapacity = 0;
	uint64_t m_constructedProfileGeneration = 0;

	bool m_currentCapacityKnown = false;
	size_t m_currentCapacity = 0;

	bool m_allocatorRecorded = false;
	size_t m_allocatorRequested = 0;
	size_t m_allocatorActual = 0;

	bool m_launchRecorded = false;
	size_t m_primeTarget = 0;
	size_t m_reservoirFrames = 0;
	bool m_downstreamEstimateKnown = false;
	size_t m_downstreamEstimateFrames = 0;
	EstimateSatisfaction m_estimateSatisfaction =
		EstimateSatisfaction::Unavailable;

	bool m_activationRecorded = false;
};
