#include "pch.h"
#include "CppUnitTest.h"

#include <microsoft_directshow/DirectShowEpochPrimePolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(DirectShowEpochPrimePolicyTests)
	{
	public:
		TEST_METHOD(FreshEpochPrimesToTheBoundedPhysicalCapacity)
		{
			Assert::AreEqual<size_t>(32,
				DirectShowEpochPrimePolicy::PrimeTarget(32, 48));
			Assert::AreEqual<size_t>(7,
				DirectShowEpochPrimePolicy::PrimeTarget(64, 8));
			Assert::AreEqual<size_t>(47,
				DirectShowEpochPrimePolicy::PrimeTarget(64, 48));
		}

		TEST_METHOD(DownstreamConfigurationNeverReducesThePhysicalPrime)
		{
			Assert::AreEqual<size_t>(12,
				DirectShowEpochPrimePolicy::PrimeTarget(12, 48));
		}

		TEST_METHOD(DownstreamExcessUsesOnlyABoundedRawBridge)
		{
			Assert::AreEqual<size_t>(3,
				DirectShowEpochPrimePolicy::RawBridgeTarget(3));
		}

		TEST_METHOD(RawBridgeCanBeClampedToSmallConfiguredQueues)
		{
			Assert::AreEqual<size_t>(1,
				std::min(DirectShowEpochPrimePolicy::RawBridgeTarget(3), size_t{ 1 }));
			Assert::AreEqual<size_t>(2,
				std::min(DirectShowEpochPrimePolicy::RawBridgeTarget(3), size_t{ 2 }));
		}

		TEST_METHOD(CurrentPrimeEpochOverridesTheNormalStartupTarget)
		{
			Assert::AreEqual<size_t>(32,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					5, 7, 7, 32, 32));
		}

		TEST_METHOD(OldEpochCannotCompleteANewerPrime)
		{
			Assert::AreEqual<size_t>(5,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					5, 8, 7, 32, 32));
			Assert::AreEqual<size_t>(5,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					5, 0, 0, 32, 32));
		}

		TEST_METHOD(PrimeCannotExceedAChangedQueueCapacity)
		{
			Assert::AreEqual<size_t>(12,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					5, 9, 9, 32, 12));
		}

		TEST_METHOD(NormalTargetCannotExceedThePhysicalBound)
		{
			Assert::AreEqual<size_t>(11,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					40, 12, 12, 32, 11));
			Assert::AreEqual<size_t>(11,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					40, 13, 12, 32, 11));
		}

		TEST_METHOD(AllocatorHeadroomBoundsAnExplicitStartupTarget)
		{
			const size_t physicalBound =
				DirectShowEpochPrimePolicy::PrimeTarget(64, 33);
			Assert::AreEqual<size_t>(32, physicalBound);
			Assert::AreEqual<size_t>(32,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					40, 15, 15, 32, physicalBound));
		}

		TEST_METHOD(LargerExplicitStartupTargetIsPreserved)
		{
			Assert::AreEqual<size_t>(40,
				DirectShowEpochPrimePolicy::ResolveBufferingTarget(
					40, 10, 10, 32, 64));
		}

		TEST_METHOD(OnlyTheCurrentHealthyEpochCanReleaseBuffering)
		{
			Assert::IsTrue(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				7, 7, true, false, false, false, 12, 12, 3, 3, false));
			Assert::IsFalse(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				6, 7, true, false, false, false, 12, 12, 3, 3, false));
			Assert::IsFalse(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				7, 7, true, false, false, true, 12, 12, 3, 3, false));
			Assert::IsFalse(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				7, 7, true, false, false, false, 11, 12, 3, 3, false));
			Assert::IsFalse(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				7, 7, true, false, false, false, 12, 12, 2, 3, false));
		}

		TEST_METHOD(TimeoutFailsOpenOnlyWithAFreshFrame)
		{
			Assert::IsTrue(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				9, 9, true, false, false, false, 1, 32, 0, 3, true));
			Assert::IsFalse(DirectShowEpochPrimePolicy::CanReleaseBuffering(
				9, 9, true, false, false, false, 0, 32, 3, 3, true));
		}
	};
}
