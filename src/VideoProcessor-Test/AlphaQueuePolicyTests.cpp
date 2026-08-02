#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaQueuePolicy.h>

#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaQueuePolicyTests)
	{
	public:
		TEST_METHOD(OmittedOverrideUsesSafeAlphaDefault)
		{
			Assert::AreEqual<size_t>(4,
				AlphaQueuePolicy::ResolveDesiredDepth(0));
		}

		TEST_METHOD(OverrideWinsOverRememberedValue)
		{
			Assert::AreEqual<size_t>(6,
				AlphaQueuePolicy::ResolveDesiredDepth(6, 12));
		}

		TEST_METHOD(HardCapacityMatchesConfiguredQueueLimit)
		{
			Assert::AreEqual<size_t>(1, AlphaQueuePolicy::HardCapacity(1));
			Assert::AreEqual<size_t>(4, AlphaQueuePolicy::HardCapacity(4));
			Assert::AreEqual<size_t>(32, AlphaQueuePolicy::HardCapacity(32));
			Assert::AreEqual<size_t>(std::numeric_limits<size_t>::max(),
				AlphaQueuePolicy::HardCapacity(
					std::numeric_limits<size_t>::max()));
		}

		TEST_METHOD(SteadyTargetDoesNotChangeConfiguredCapacity)
		{
			const size_t capacity = AlphaQueuePolicy::HardCapacity(32);
			Assert::AreEqual<size_t>(32, capacity);
			Assert::AreEqual<size_t>(2,
				AlphaQueuePolicy::ClampDesiredDepthToCapacity(2, capacity));
			Assert::AreEqual<size_t>(32,
				AlphaQueuePolicy::ClampDesiredDepthToCapacity(64, capacity));
		}

		TEST_METHOD(HealthyBandStraddlesDesiredDepth)
		{
			Assert::AreEqual<size_t>(0,
				AlphaQueuePolicy::HealthyLowWater(1));
			Assert::AreEqual<size_t>(3,
				AlphaQueuePolicy::HealthyLowWater(4));
			Assert::AreEqual<size_t>(4,
				AlphaQueuePolicy::HealthyHighWater(4));
		}

		TEST_METHOD(StartupPrefillWaitsForDesiredDepth)
		{
			Assert::IsFalse(AlphaQueuePolicy::CanDequeue(3, 4, true));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(4, 4, true));
		}

		TEST_METHOD(SteadyStateRetainsLowWaterReserve)
		{
			Assert::IsFalse(AlphaQueuePolicy::CanDequeue(3, 4, false));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(4, 4, false));
			Assert::IsTrue(AlphaQueuePolicy::CanDequeue(1, 1, false));
		}

		TEST_METHOD(RenderStallThresholdScalesWithFramePeriod)
		{
			Assert::AreEqual(50.0,
				AlphaQueuePolicy::RenderStallThresholdMs(59.94), 0.01);
			Assert::AreEqual(83.42,
				AlphaQueuePolicy::RenderStallThresholdMs(23.976), 0.01);
		}

		TEST_METHOD(BacklogRecoveryRequiresExcessDepthAndBoundedStaleness)
		{
			Assert::IsFalse(AlphaQueuePolicy::ShouldRecoverBacklog(
				2, 2, 500.0, 500.0, 59.94));
			Assert::IsFalse(AlphaQueuePolicy::ShouldRecoverBacklog(
				3, 2, 80.0, 40.0, 59.94));
			Assert::IsTrue(AlphaQueuePolicy::ShouldRecoverBacklog(
				3, 2, 80.0, 60.0, 59.94));
			Assert::IsTrue(AlphaQueuePolicy::ShouldRecoverBacklog(
				8, 2, 140.0, 20.0, 23.976));
		}

		TEST_METHOD(ConfigValueRequiresPositiveDecimalInteger)
		{
			size_t value = 0;
			Assert::IsTrue(
				AlphaQueuePolicy::TryParsePositiveSize("4", value));
			Assert::AreEqual<size_t>(4, value);
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("0", value));
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("-4", value));
			Assert::IsFalse(
				AlphaQueuePolicy::TryParsePositiveSize("4x", value));
		}
	};
}
