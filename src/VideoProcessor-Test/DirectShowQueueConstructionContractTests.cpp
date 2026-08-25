#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/DirectShowEpochPrimePolicy.h>
#include <microsoft_directshow/DirectShowQueueConstructionContract.h>
#include <microsoft_directshow/live_source_filter/ALiveSourceVideoOutputPin.h>

#include <atomic>
#include <climits>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	TEST_CLASS(DirectShowQueueConstructionContractTests)
	{
	public:
		TEST_METHOD(RejectsCapacityOutsideSupportedConstructionRange)
		{
			DirectShowQueueConstructionContract contract;
			const auto zero = contract.PublishDesired(0, 1, 10);
			Assert::IsFalse(zero.accepted);
			Assert::IsTrue(zero.disposition ==
				DirectShowQueueConstructionContract::
					PublishDisposition::InvalidCapacity);

			const auto tooLarge = contract.PublishDesired(
				static_cast<size_t>(INT_MAX) + 1u, 2, 11);
			Assert::IsFalse(tooLarge.accepted);
			Assert::IsFalse(contract.GetSnapshot().desiredKnown);
		}

		TEST_METHOD(PublishBeforeCommitIsConsumedAsOneCoherentContract)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(16, 1, 100);
			const auto publication = contract.PublishDesired(32, 2, 101);
			Assert::IsTrue(publication.accepted);
			Assert::IsFalse(publication.committed);
			Assert::IsFalse(publication.recreationRequired);

			const auto commit = contract.Commit(7);
			Assert::IsTrue(commit.disposition ==
				DirectShowQueueConstructionContract::
					CommitDisposition::Committed);
			Assert::AreEqual<size_t>(32, commit.constructedCapacity);
			Assert::AreEqual<uint64_t>(2, commit.key.contractRevision);
			Assert::AreEqual<uint64_t>(101,
				commit.constructedProfileGeneration);

			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<size_t>(32, snapshot.desired.capacity);
			Assert::AreEqual<uint64_t>(2, snapshot.desired.revision);
			Assert::AreEqual<uint64_t>(101,
				snapshot.desired.profileGeneration);
			Assert::AreEqual<size_t>(32, snapshot.constructedCapacity);
			Assert::IsTrue(snapshot.phase ==
				DirectShowQueueConstructionContract::Phase::Committed);
		}

		TEST_METHOD(CommitOccursExactlyOnceAndConstructionEvidenceIsImmutable)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(16, 4, 40);
			const auto first = contract.Commit(8);
			contract.RecordAllocator(first.key, 20, 20);
			contract.RecordLaunch(first.key, 16, 19, { false, 0 });
			contract.Activate(first.key, 16);
			contract.PublishDesired(32, 5, 41);
			const auto second = contract.Commit(9);

			Assert::IsTrue(second.disposition ==
				DirectShowQueueConstructionContract::
					CommitDisposition::AlreadyCommitted);
			Assert::IsTrue(second.key == first.key);
			Assert::AreEqual<size_t>(16, second.constructedCapacity);
			Assert::AreEqual<uint64_t>(40,
				second.constructedProfileGeneration);

			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<size_t>(32, snapshot.desired.capacity);
			Assert::AreEqual<size_t>(16, snapshot.constructedCapacity);
			Assert::IsTrue(snapshot.currentCapacityKnown);
			Assert::AreEqual<size_t>(16, snapshot.currentCapacity);
			Assert::IsTrue(snapshot.recreationRequired);
			Assert::IsTrue(snapshot.phase ==
				DirectShowQueueConstructionContract::Phase::Mismatch);
		}

		TEST_METHOD(SameEffectiveValueAdvancesDesiredEvidenceWithoutRecreation)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(32, 1, 20);
			const auto commit = contract.Commit(12);
			const auto reapply = contract.PublishDesired(32, 2, 21);

			Assert::IsTrue(reapply.accepted);
			Assert::IsTrue(reapply.disposition ==
				DirectShowQueueConstructionContract::
					PublishDisposition::SameEffectiveValue);
			Assert::IsFalse(reapply.recreationRequired);
			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<uint64_t>(2, snapshot.desired.revision);
			Assert::AreEqual<uint64_t>(commit.key.contractRevision,
				snapshot.key.contractRevision);
			Assert::AreEqual<size_t>(32, snapshot.constructedCapacity);
			Assert::IsFalse(snapshot.recreationRequired);
		}

		TEST_METHOD(StaleOrConflictingRevisionCannotReplaceDesiredPair)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(32, 4, 30);
			const auto stale = contract.PublishDesired(16, 3, 31);
			const auto conflicting = contract.PublishDesired(16, 4, 32);

			Assert::IsFalse(stale.accepted);
			Assert::IsFalse(conflicting.accepted);
			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<size_t>(32, snapshot.desired.capacity);
			Assert::AreEqual<uint64_t>(4, snapshot.desired.revision);
			Assert::AreEqual<uint64_t>(30,
				snapshot.desired.profileGeneration);
		}

		TEST_METHOD(KeyedEvidenceRejectsStaleGenerationAndRevision)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(32, 6, 60);
			const auto commit = contract.Commit(15);
			auto staleGeneration = commit.key;
			++staleGeneration.rendererGeneration;
			auto staleRevision = commit.key;
			++staleRevision.contractRevision;

			Assert::IsFalse(contract.RecordAllocator(
				staleGeneration, 36, 36));
			Assert::IsFalse(contract.RecordLaunch(
				staleRevision, 32, 35, { true, 28 }));
			Assert::IsFalse(contract.Activate(staleGeneration, 32));

			const auto snapshot = contract.GetSnapshot();
			Assert::IsFalse(snapshot.allocatorRecorded);
			Assert::IsFalse(snapshot.launchRecorded);
			Assert::IsFalse(snapshot.activationRecorded);
		}

		TEST_METHOD(IncidentEvidenceBecomesActiveAndEstimateSatisfied)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(32, 9, 90);
			const auto commit = contract.Commit(18);
			const size_t allocatorActual = 36;
			const size_t reservoir = allocatorActual -
				DirectShowEpochPrimePolicy::AllocatorHeadroomFrames;
			const size_t prime = DirectShowEpochPrimePolicy::PrimeTarget(
				commit.constructedCapacity, allocatorActual);

			Assert::IsTrue(contract.RecordAllocator(
				commit.key, 36, allocatorActual));
			Assert::IsTrue(contract.RecordLaunch(
				commit.key, prime, reservoir, { true, 28 }));
			Assert::IsTrue(contract.Activate(commit.key, 32));

			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<size_t>(36, snapshot.allocatorRequested);
			Assert::AreEqual<size_t>(36, snapshot.allocatorActual);
			Assert::AreEqual<size_t>(32, snapshot.primeTarget);
			Assert::AreEqual<size_t>(35, snapshot.reservoirFrames);
			Assert::AreEqual<size_t>(28,
				snapshot.downstreamEstimateFrames);
			Assert::IsTrue(snapshot.estimateSatisfaction ==
				DirectShowQueueConstructionContract::
					EstimateSatisfaction::Satisfied);
			Assert::IsTrue(snapshot.phase ==
				DirectShowQueueConstructionContract::Phase::Active);
		}

		TEST_METHOD(UnavailableAndUnsatisfiedEstimatesRemainDistinct)
		{
			DirectShowQueueConstructionContract unavailable;
			unavailable.PublishDesired(8, 1, 1);
			const auto unavailableCommit = unavailable.Commit(1);
			unavailable.RecordAllocator(unavailableCommit.key, 12, 12);
			unavailable.RecordLaunch(
				unavailableCommit.key, 8, 11, { false, 99 });
			unavailable.Activate(unavailableCommit.key, 8);
			const auto unavailableSnapshot = unavailable.GetSnapshot();
			Assert::IsFalse(unavailableSnapshot.downstreamEstimateKnown);
			Assert::IsTrue(unavailableSnapshot.estimateSatisfaction ==
				DirectShowQueueConstructionContract::
					EstimateSatisfaction::Unavailable);

			DirectShowQueueConstructionContract unsatisfied;
			unsatisfied.PublishDesired(8, 2, 2);
			const auto unsatisfiedCommit = unsatisfied.Commit(2);
			unsatisfied.RecordAllocator(unsatisfiedCommit.key, 12, 12);
			unsatisfied.RecordLaunch(
				unsatisfiedCommit.key, 8, 11, { true, 14 });
			unsatisfied.Activate(unsatisfiedCommit.key, 8);
			const auto unsatisfiedSnapshot = unsatisfied.GetSnapshot();
			Assert::IsTrue(unsatisfiedSnapshot.downstreamEstimateKnown);
			Assert::IsTrue(unsatisfiedSnapshot.estimateSatisfaction ==
				DirectShowQueueConstructionContract::
					EstimateSatisfaction::Unsatisfied);
			Assert::IsTrue(unsatisfiedSnapshot.phase ==
				DirectShowQueueConstructionContract::Phase::Unsatisfied);
		}

		TEST_METHOD(SameRevisionMayRefreshOnlyProfileEvidence)
		{
			DirectShowQueueConstructionContract contract;
			Assert::IsTrue(contract.PublishDesired(32, 7, 100).accepted);
			const auto refreshed = contract.PublishDesired(32, 7, 101);
			Assert::IsTrue(refreshed.accepted);
			Assert::AreEqual<uint64_t>(7, refreshed.desired.revision);
			Assert::AreEqual<uint64_t>(101,
				refreshed.desired.profileGeneration);
			Assert::IsFalse(refreshed.recreationRequired);
		}

		TEST_METHOD(AllocatorEvidenceUsesFinalDownstreamAwareRequest)
		{
			Assert::AreEqual<LONG>(36,
				ALiveSourceVideoOutputPin::ResolveAllocatorBufferRequest(1, 36));
			Assert::AreEqual<LONG>(40,
				ALiveSourceVideoOutputPin::ResolveAllocatorBufferRequest(40, 36));
			Assert::AreEqual<LONG>(48,
				ALiveSourceVideoOutputPin::ResolveAllocatorBufferRequest(64, 36));
		}

		TEST_METHOD(CurrentCapacityMismatchDoesNotRewriteConstructionEvidence)
		{
			DirectShowQueueConstructionContract contract;
			contract.PublishDesired(32, 3, 3);
			const auto commit = contract.Commit(3);
			contract.RecordAllocator(commit.key, 36, 36);
			contract.RecordLaunch(commit.key, 32, 35, { false, 0 });
			Assert::IsTrue(contract.Activate(commit.key, 16));

			const auto snapshot = contract.GetSnapshot();
			Assert::AreEqual<size_t>(32, snapshot.constructedCapacity);
			Assert::AreEqual<size_t>(16, snapshot.currentCapacity);
			Assert::IsTrue(snapshot.recreationRequired);
			Assert::IsTrue(snapshot.phase ==
				DirectShowQueueConstructionContract::Phase::Mismatch);
		}

		TEST_METHOD(PublishAndCommitRaceAlwaysHasALinearizableOutcome)
		{
			for (unsigned int iteration = 0; iteration < 100; ++iteration)
			{
				DirectShowQueueConstructionContract contract;
				contract.PublishDesired(16, 1, 100);

				std::atomic<unsigned int> ready{ 0 };
				std::atomic_bool go{ false };
				std::thread publisher([&]()
					{
						ready.fetch_add(1, std::memory_order_acq_rel);
						while (!go.load(std::memory_order_acquire))
							std::this_thread::yield();
						contract.PublishDesired(32, 2, 101);
					});
				std::thread committer([&]()
					{
						ready.fetch_add(1, std::memory_order_acq_rel);
						while (!go.load(std::memory_order_acquire))
							std::this_thread::yield();
						contract.Commit(22);
					});

				while (ready.load(std::memory_order_acquire) != 2)
					std::this_thread::yield();
				go.store(true, std::memory_order_release);
				publisher.join();
				committer.join();

				const auto snapshot = contract.GetSnapshot();
				Assert::IsTrue(snapshot.committed);
				Assert::AreEqual<size_t>(32, snapshot.desired.capacity);
				Assert::AreEqual<uint64_t>(2, snapshot.desired.revision);
				if (snapshot.constructedCapacity == 32)
				{
					Assert::AreEqual<uint64_t>(2,
						snapshot.key.contractRevision);
					Assert::IsFalse(snapshot.recreationRequired);
				}
				else
				{
					Assert::AreEqual<size_t>(16,
						snapshot.constructedCapacity);
					Assert::AreEqual<uint64_t>(1,
						snapshot.key.contractRevision);
					Assert::IsTrue(snapshot.recreationRequired);
				}
			}
		}
	};
}
