#include "pch.h"
#include "CppUnitTest.h"

#include <libplacebo/AlphaQueuePolicy.h>

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
