#include "pch.h"
#include "CppUnitTest.h"

#include <RendererPostStallResetAdvisor.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	namespace
	{
		PostStallResetObservation VpObservation(uint64_t nowTick)
		{
			PostStallResetObservation observation;
			observation.renderer = PostStallRendererKind::VpRenderer;
			observation.nowTick = nowTick;
			observation.generation = 7;
			observation.outputReady = true;
			observation.rendererQuiet = true;
			observation.queueDepth = 4;
			observation.healthyQueueDepth = 2;
			observation.framePeriodMs = 1000.0 / 59.94;
			observation.oldestQueuedAgeMs = 52.0;
			observation.renderMs = 4.0;
			observation.swapBlockMs = 10.0;
			return observation;
		}

		PostStallResetObservation MadVRObservation(uint64_t nowTick,
			double scheduledLatencyMs)
		{
			PostStallResetObservation observation;
			observation.renderer = PostStallRendererKind::MadVR;
			observation.nowTick = nowTick;
			observation.generation = 12;
			observation.outputReady = true;
			observation.rendererQuiet = true;
			observation.queueDepth = 2;
			observation.healthyQueueDepth = 2;
			observation.framePeriodMs = 1000.0 / 59.94;
			observation.scheduledLatencyKnown = true;
			observation.scheduledLatencyMs = scheduledLatencyMs;
			return observation;
		}
	}

	TEST_CLASS(RendererPostStallResetAdvisorTests)
	{
	public:
		TEST_METHOD(VpAdvisesOnlyAfterPersistentPostStallQueueAgeAndSwapEvidence)
		{
			PostStallResetAdvisor advisor;
			auto observation = VpObservation(100);
			observation.materialStall = true;
			auto decision = advisor.Observe(observation);
			Assert::AreEqual(static_cast<int>(
				PostStallResetDiagnosticState::Settling),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.resetShouldOccur);

			decision = advisor.Observe(VpObservation(3000));
			Assert::AreEqual(1u, decision.persistentBadObservations);
			Assert::IsFalse(decision.resetShouldOccur);
			decision = advisor.Observe(VpObservation(4000));
			Assert::AreEqual(2u, decision.persistentBadObservations);
			decision = advisor.Observe(VpObservation(5000));
			Assert::IsTrue(decision.resetShouldOccur);
			Assert::IsTrue(decision.shouldLog);
			Assert::AreEqual(static_cast<int>(
				PostStallResetDiagnosticState::Advisory),
				static_cast<int>(decision.state));

			decision = advisor.Observe(VpObservation(15000));
			Assert::IsTrue(decision.resetShouldOccur);
			Assert::IsTrue(decision.shouldLog);

			observation = VpObservation(16000);
			observation.queueDepth = 2;
			observation.oldestQueuedAgeMs = 8.0;
			observation.swapBlockMs = 0.2;
			decision = advisor.Observe(observation);
			Assert::IsFalse(decision.resetShouldOccur);
			Assert::IsTrue(decision.shouldLog);
			Assert::AreEqual(static_cast<int>(
				PostStallResetDiagnosticState::Monitoring),
				static_cast<int>(decision.state));
		}

		TEST_METHOD(MadVRUsesNormalizedLatencyDeltaAndNeverInfersQueueOccupancy)
		{
			PostStallResetAdvisor advisor;
			auto decision = advisor.Observe(MadVRObservation(100, 150.0));
			Assert::IsFalse(decision.resetShouldOccur);
			decision = advisor.Observe(MadVRObservation(1100, 150.0));
			Assert::IsFalse(decision.resetShouldOccur);

			auto observation = MadVRObservation(2000, 166.7);
			observation.materialStall = true;
			decision = advisor.Observe(observation);
			Assert::AreEqual(static_cast<int>(
				PostStallResetDiagnosticState::Settling),
				static_cast<int>(decision.state));

			decision = advisor.Observe(MadVRObservation(4000, 166.7));
			Assert::AreEqual(1u, decision.persistentBadObservations);
			decision = advisor.Observe(MadVRObservation(5000, 166.7));
			Assert::AreEqual(2u, decision.persistentBadObservations);
			decision = advisor.Observe(MadVRObservation(6000, 166.7));
			Assert::IsTrue(decision.resetShouldOccur);
			Assert::IsTrue(decision.scheduledLatencyDeltaFrames >= 0.9);
		}

		TEST_METHOD(BusyRendererSuppressesAnOtherwiseEligiblePostStallAdvisory)
		{
			PostStallResetAdvisor advisor;
			auto observation = VpObservation(100);
			observation.materialStall = true;
			advisor.Observe(observation);
			observation = VpObservation(3000);
			observation.rendererQuiet = false;
			const auto decision = advisor.Observe(observation);
			Assert::AreEqual(static_cast<int>(
				PostStallResetDiagnosticState::Suppressed),
				static_cast<int>(decision.state));
			Assert::IsFalse(decision.resetShouldOccur);
		}
	};
}
