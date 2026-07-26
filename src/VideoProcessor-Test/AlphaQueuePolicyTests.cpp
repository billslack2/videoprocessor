#include "pch.h"
#include "CppUnitTest.h"

#include <libplacebo/AlphaQueuePolicy.h>

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

		TEST_METHOD(HardCapacityIsIndependentFromNormalTarget)
		{
			Assert::AreEqual<size_t>(3, AlphaQueuePolicy::HardCapacity(1));
			Assert::AreEqual<size_t>(8, AlphaQueuePolicy::HardCapacity(4));
			Assert::AreEqual<size_t>(38, AlphaQueuePolicy::HardCapacity(32));
			Assert::AreEqual<size_t>(std::numeric_limits<size_t>::max(),
				AlphaQueuePolicy::HardCapacity(
					std::numeric_limits<size_t>::max()));
		}

		TEST_METHOD(HealthyBandStraddlesDesiredDepth)
		{
			Assert::AreEqual<size_t>(0,
				AlphaQueuePolicy::HealthyLowWater(1));
			Assert::AreEqual<size_t>(3,
				AlphaQueuePolicy::HealthyLowWater(4));
			Assert::AreEqual<size_t>(5,
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
