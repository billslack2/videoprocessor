#include "pch.h"
#include "CppUnitTest.h"
#include <InvalidCaptureStateGrace.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(InvalidCaptureStateGraceTests)
	{
	public:
		TEST_METHOD(PersistentInvalidStateExpiresAtOriginalDeadline)
		{
			InvalidCaptureStateGrace grace;
			grace.ObserveInvalid(100);
			Assert::AreEqual<uint64_t>(1, grace.RemainingMs(1599));
			Assert::AreEqual<uint64_t>(0, grace.RemainingMs(1600));
			Assert::IsTrue(grace.Pending());
		}

		TEST_METHOD(ContinuousInvalidNotificationsCannotExtendGrace)
		{
			InvalidCaptureStateGrace grace;
			// Simulate callbacks on every frame, including no-input frames.
			for (uint64_t now = 0; now < 1500; now += 10)
			{
				grace.ObserveInvalid(now);
				Assert::AreEqual<uint64_t>(1500 - now, grace.RemainingMs(now));
			}
			grace.ObserveInvalid(1500);
			Assert::AreEqual<uint64_t>(0, grace.RemainingMs(1500));
			grace.ObserveInvalid(60000);
			Assert::AreEqual<uint64_t>(0, grace.RemainingMs(60000));
		}

		TEST_METHOD(ValidRecoveryCancelsPendingExpiry)
		{
			InvalidCaptureStateGrace grace;
			grace.ObserveInvalid(100);
			// The valid-state handler cancels the timer and resets the policy.
			grace.Reset();
			Assert::IsFalse(grace.Pending());
			Assert::AreEqual<uint64_t>(0, grace.RemainingMs(1600));
		}

		TEST_METHOD(NewEpisodeAfterRecoveryGetsItsOwnGrace)
		{
			InvalidCaptureStateGrace grace;
			grace.ObserveInvalid(100);
			grace.Reset();
			grace.ObserveInvalid(1000);
			Assert::AreEqual<uint64_t>(900, grace.RemainingMs(1600));
			Assert::AreEqual<uint64_t>(0, grace.RemainingMs(2500));
		}

		TEST_METHOD(CaptureTeardownCancelsOldRunDeadline)
		{
			InvalidCaptureStateGrace grace;
			grace.ObserveInvalid(0);
			grace.Reset();
			Assert::IsFalse(grace.Pending());
			grace.ObserveInvalid(10000);
			Assert::AreEqual<uint64_t>(1500, grace.RemainingMs(10000));
		}
	};
}
