#include "pch.h"
#include "CppUnitTest.h"

#include <RendererQueueLaunchAudit.h>
#include <RendererQueueLaunchContractModel.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace Tests
{
	namespace
	{
		RendererQueueLaunchDesired Desired(
			RendererQueueLaunchBackend backend,
			size_t capacity,
			uint64_t revision)
		{
			RendererQueueLaunchDesired desired;
			desired.key.backend = backend;
			desired.key.capacity = capacity;
			desired.effectiveRevision = revision;
			desired.profileGeneration = revision;
			return desired;
		}

		RendererQueueLaunchStableAudit Audit(
			uint32_t rendererGeneration,
			const RendererQueueLaunchDesired& desired,
			RendererQueueLaunchBackend constructedBackend,
			size_t constructedCapacity,
			size_t currentCapacity)
		{
			RendererQueueLaunchStableAudit audit;
			audit.rendererGeneration = rendererGeneration;
			audit.desired = desired;
			audit.constructed.backend = constructedBackend;
			audit.constructed.capacity = constructedCapacity;
			audit.current.backend = constructedBackend;
			audit.current.capacity = currentCapacity;
			return audit;
		}

		void AssertAction(
			const RendererQueueLaunchAction& action,
			RendererQueueLaunchActionType type,
			uint32_t rendererGeneration,
			const RendererQueueLaunchDesired& desired)
		{
			Assert::IsTrue(action.type == type);
			Assert::AreEqual<uint32_t>(
				rendererGeneration, action.rendererGeneration);
			Assert::IsTrue(action.desired == desired);
		}
	}


	TEST_CLASS(RendererQueueLaunchContractModelTests)
	{
	public:
		TEST_METHOD(AuditFormatterKeepsOneExplicitPhaseSchema)
		{
			RendererQueueLaunchAuditRecord record;
			record.trigger = "test";
			record.phase = "commit";
			record.backend = "DirectShow";
			record.profile = "madvr32";
			record.generation = 7;
			record.profileGeneration = 11;
			record.effectiveRevision = 12;
			record.desiredRevision = 12;
			record.constructedRevision = 12;
			record.desired = 32;
			record.retained = 32;
			record.constructed = 32;
			record.state = "committed";

			const std::string formatted =
				FormatRendererQueueLaunchAudit(record);
			const std::string expected =
				"Renderer queue launch contract audit: trigger=test phase=commit "
				"backend=DirectShow profile=madvr32 generation=7 "
				"profile_generation=11 effective_revision=12 "
				"desired_revision=12 constructed_revision=12 desired=32 "
				"retained=32 constructed=32 current=unavailable "
				"allocator_requested=unavailable allocator_actual=unavailable "
				"prime=unavailable reservoir=unavailable "
				"downstream_estimate=unavailable estimate_known=unavailable "
				"estimate_satisfied=unavailable state=committed "
				"settled=unavailable reset=unavailable action=unavailable";
			Assert::AreEqual(expected, formatted);
		}

		TEST_METHOD(PreCommitChangeIsConsumedByConstruction)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			Assert::IsTrue(model.PublishDesired(desired16));
			Assert::IsTrue(model.StartConstruction(
				1, RendererQueueLaunchBackend::DirectShow).accepted);
			Assert::IsTrue(model.PublishDesired(desired32));

			const auto committed = model.CommitConstruction(1);
			Assert::IsTrue(committed.accepted);
			Assert::IsTrue(committed.desired == desired32);
			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(1, desired32,
					RendererQueueLaunchBackend::DirectShow, 32, 32)).type ==
				RendererQueueLaunchActionType::None);
		}

		TEST_METHOD(PostCommitChangeRequestsExactlyOneRecreation)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			const auto audit = Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16);

			AssertAction(model.ConsumeStableAudit(audit),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				1, desired32);
			Assert::IsTrue(model.RecreationInFlight());
			Assert::IsTrue(model.ConsumeStableAudit(audit).type ==
				RendererQueueLaunchActionType::None);
			AssertAction(model.PendingAction(),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				1, desired32);
		}

		TEST_METHOD(SameCapacityNewRevisionNeedsNoRecreation)
		{
			RendererQueueLaunchContractModel model;
			const auto first = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 1);
			const auto reapplied = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(first);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(reapplied);

			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(1, reapplied,
					RendererQueueLaunchBackend::DirectShow, 32, 32)).type ==
				RendererQueueLaunchActionType::None);
			Assert::IsFalse(model.RecreationInFlight());
		}

		TEST_METHOD(ExactRendererCommitWinsOverLaterDesiredPublication)
		{
			RendererQueueLaunchContractModel model;
			const auto constructed16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto latest32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(constructed16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.PublishDesired(latest32);
			Assert::IsTrue(model.CommitConstruction(
				1, constructed16).accepted);

			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(1, latest32,
					RendererQueueLaunchBackend::DirectShow, 16, 16)).type ==
				RendererQueueLaunchActionType::RequestCoveredRecreation);
		}

		TEST_METHOD(SameRevisionProfileRefreshPreservesEffectiveContract)
		{
			RendererQueueLaunchContractModel model;
			auto first = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 5);
			auto refreshed = first;
			refreshed.profileGeneration = first.profileGeneration + 1;
			Assert::IsTrue(model.PublishDesired(first));
			Assert::IsTrue(model.PublishDesired(refreshed));
			RendererQueueLaunchDesired latest;
			Assert::IsTrue(model.LatestDesired(latest));
			Assert::AreEqual<uint64_t>(first.effectiveRevision,
				latest.effectiveRevision);
			Assert::AreEqual<uint64_t>(refreshed.profileGeneration,
				latest.profileGeneration);
		}

		TEST_METHOD(RevertBeforeAuditConsumptionCancelsMismatch)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2));
			const auto reverted = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 3);
			model.PublishDesired(reverted);

			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(1, reverted,
					RendererQueueLaunchBackend::DirectShow, 16, 16)).type ==
				RendererQueueLaunchActionType::None);
		}

		TEST_METHOD(RevertAfterAuditCancelsPendingDispatch)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			model.ConsumeStableAudit(Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));

			model.PublishDesired(Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 3));
			Assert::IsTrue(model.PendingAction().type ==
				RendererQueueLaunchActionType::None);
			Assert::IsFalse(model.RecreationInFlight());
		}

		TEST_METHOD(LatestCapacityWinsOneRecreationAndSuccessor)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			const auto desired64 = Desired(
				RendererQueueLaunchBackend::DirectShow, 64, 3);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			model.ConsumeStableAudit(Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));
			model.PublishDesired(desired64);

			AssertAction(model.PendingAction(),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				1, desired64);
			Assert::IsTrue(model.StartConstruction(2,
				RendererQueueLaunchBackend::DirectShow).accepted);
			const auto committed = model.CommitConstruction(2);
			Assert::IsTrue(committed.desired == desired64);
			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(2, desired64,
					RendererQueueLaunchBackend::DirectShow, 64, 64)).type ==
				RendererQueueLaunchActionType::None);
		}

		TEST_METHOD(StaleRevisionAndGenerationAreIgnored)
		{
			RendererQueueLaunchContractModel model;
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 4);
			Assert::IsTrue(model.PublishDesired(desired32));
			Assert::IsFalse(model.PublishDesired(Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 3)));
			model.StartConstruction(8,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(8);

			Assert::IsFalse(model.StartConstruction(7,
				RendererQueueLaunchBackend::DirectShow).accepted);
			Assert::IsFalse(model.CommitConstruction(7).accepted);
			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(7, desired32,
					RendererQueueLaunchBackend::DirectShow, 16, 16)).type ==
				RendererQueueLaunchActionType::None);
			Assert::IsTrue(model.ConsumeStableAudit(
				Audit(8, Desired(
						RendererQueueLaunchBackend::DirectShow, 16, 3),
					RendererQueueLaunchBackend::DirectShow, 16, 16)).type ==
				RendererQueueLaunchActionType::None);
		}

		TEST_METHOD(RepeatedMismatchInRecoverySuccessorFailsCovered)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			model.ConsumeStableAudit(Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));
			model.StartConstruction(2,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(2);

			const auto mismatch = Audit(2, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16);
			AssertAction(model.ConsumeStableAudit(mismatch),
				RendererQueueLaunchActionType::FailCovered, 2, desired32);
			Assert::IsTrue(model.IsTerminal(
				RendererQueueLaunchBackend::DirectShow, desired32.key));
			Assert::IsTrue(model.ConsumeStableAudit(mismatch).type ==
				RendererQueueLaunchActionType::None);
			Assert::IsFalse(model.RecreationInFlight());
		}

		TEST_METHOD(ManualRetryAndChangedCapacityClearTerminalGate)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			model.ConsumeStableAudit(Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));
			model.StartConstruction(2,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(2);
			model.ConsumeStableAudit(Audit(2, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));

			Assert::IsTrue(model.StartConstruction(3,
				RendererQueueLaunchBackend::DirectShow,
				RendererQueueLaunchStartReason::ManualRetry).accepted);
			Assert::IsFalse(model.IsTerminal(
				RendererQueueLaunchBackend::DirectShow, desired32.key));

			model.CommitConstruction(3);
			model.ConsumeStableAudit(Audit(3, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));
			model.StartConstruction(4,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(4);
			model.ConsumeStableAudit(Audit(4, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16));
			Assert::IsTrue(model.IsTerminal(
				RendererQueueLaunchBackend::DirectShow, desired32.key));

			const auto desired64 = Desired(
				RendererQueueLaunchBackend::DirectShow, 64, 3);
			model.PublishDesired(desired64);
			Assert::IsFalse(model.IsTerminal(
				RendererQueueLaunchBackend::DirectShow, desired32.key));
			AssertAction(model.ConsumeStableAudit(
				Audit(4, desired64,
					RendererQueueLaunchBackend::DirectShow, 16, 16)),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				4, desired64);
		}

		TEST_METHOD(BackendContractsRemainIsolatedAcrossRoundTrip)
		{
			RendererQueueLaunchContractModel model;
			const auto directShow32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 1);
			const auto alpha16 = Desired(
				RendererQueueLaunchBackend::Alpha, 16, 2);
			const auto directShowAgain = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 3);

			model.PublishDesired(directShow32);
			Assert::IsTrue(model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow).desired == directShow32);
			Assert::IsTrue(model.CommitConstruction(1).desired == directShow32);
			model.PublishDesired(alpha16);
			Assert::IsTrue(model.StartConstruction(2,
				RendererQueueLaunchBackend::Alpha).desired == alpha16);
			Assert::IsTrue(model.CommitConstruction(2).desired == alpha16);
			model.PublishDesired(directShowAgain);
			Assert::IsTrue(model.StartConstruction(3,
				RendererQueueLaunchBackend::DirectShow).desired ==
				directShowAgain);
			Assert::IsTrue(model.CommitConstruction(3).desired ==
				directShowAgain);

			RendererQueueLaunchDesired retainedAlpha;
			Assert::IsTrue(model.DesiredForBackend(
				RendererQueueLaunchBackend::Alpha, retainedAlpha));
			Assert::IsTrue(retainedAlpha == alpha16);
		}

		TEST_METHOD(BackendRoundTripStartsNewRecoveryLineage)
		{
			RendererQueueLaunchContractModel model;
			const auto directShow32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 1);
			model.PublishDesired(directShow32);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			AssertAction(model.ConsumeStableAudit(Audit(1, directShow32,
				RendererQueueLaunchBackend::DirectShow, 16, 16)),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				1, directShow32);

			model.StartConstruction(2,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(2);
			AssertAction(model.ConsumeStableAudit(Audit(2, directShow32,
				RendererQueueLaunchBackend::DirectShow, 16, 16)),
				RendererQueueLaunchActionType::FailCovered,
				2, directShow32);
			Assert::IsTrue(model.IsTerminal(
				RendererQueueLaunchBackend::DirectShow, directShow32.key));

			const auto alpha16 = Desired(
				RendererQueueLaunchBackend::Alpha, 16, 2);
			model.PublishDesired(alpha16);
			model.StartConstruction(3, RendererQueueLaunchBackend::Alpha);
			model.CommitConstruction(3);
			Assert::IsTrue(model.ConsumeStableAudit(Audit(3, alpha16,
				RendererQueueLaunchBackend::Alpha, 16, 16)).type ==
				RendererQueueLaunchActionType::None);

			const auto directShowAgain = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 3);
			model.PublishDesired(directShowAgain);
			model.StartConstruction(4,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(4);
			AssertAction(model.ConsumeStableAudit(Audit(4, directShowAgain,
				RendererQueueLaunchBackend::DirectShow, 16, 16)),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				4, directShowAgain);
		}

		TEST_METHOD(UnstableAuditIsSuppressedUntilConstructionAndResetSettle)
		{
			RendererQueueLaunchContractModel model;
			const auto desired16 = Desired(
				RendererQueueLaunchBackend::DirectShow, 16, 1);
			const auto desired32 = Desired(
				RendererQueueLaunchBackend::DirectShow, 32, 2);
			model.PublishDesired(desired16);
			model.StartConstruction(1,
				RendererQueueLaunchBackend::DirectShow);
			model.CommitConstruction(1);
			model.PublishDesired(desired32);
			auto audit = Audit(1, desired32,
				RendererQueueLaunchBackend::DirectShow, 16, 16);
			audit.constructionSettled = false;
			Assert::IsTrue(model.ConsumeStableAudit(audit).type ==
				RendererQueueLaunchActionType::None);
			audit.constructionSettled = true;
			audit.resetInProgress = true;
			Assert::IsTrue(model.ConsumeStableAudit(audit).type ==
				RendererQueueLaunchActionType::None);
			Assert::IsTrue(model.PendingAction().type ==
				RendererQueueLaunchActionType::None);

			audit.resetInProgress = false;
			AssertAction(model.ConsumeStableAudit(audit),
				RendererQueueLaunchActionType::RequestCoveredRecreation,
				1, desired32);
		}
	};
}
