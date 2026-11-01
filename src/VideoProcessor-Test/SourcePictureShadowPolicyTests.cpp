#include "pch.h"
#include <vprenderer/SourcePictureShadowPolicy.h>
#include <limits>
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    TEST_CLASS(SourcePictureShadowPolicyTests)
    {
    public:
        TEST_METHOD(DisabledShadowNeverStartsDiagnosticWork)
        {
            SourcePictureShadowPolicy policy(false);
            unsigned work = 0;
            for (int64_t tick : {int64_t(0), int64_t(500), int64_t(1000000)})
                if (policy.Admit(tick, 1000)) ++work;
            Assert::AreEqual(unsigned(0), work);
            Assert::AreEqual(12, int(SourcePictureShadowPolicy::BudgetMilliseconds));
            Assert::AreEqual(500, int(SourcePictureShadowPolicy::IntervalMilliseconds));
        }

        TEST_METHOD(ShadowAdmitsFirstValidSourceAndExactHalfSecondBoundary)
        {
            SourcePictureShadowPolicy policy(true);
            Assert::IsTrue(policy.Admit(0, 1000));
            Assert::IsFalse(policy.Admit(0, 1000));
            Assert::IsFalse(policy.Admit(499, 1000));
            Assert::IsTrue(policy.Admit(500, 1000));
            Assert::IsFalse(policy.Admit(999, 1000));
            Assert::IsTrue(policy.Admit(1000, 1000));
        }

        TEST_METHOD(ShadowInvalidOrBackwardClockCannotSpendOrRenewAdmission)
        {
            SourcePictureShadowPolicy policy(true);
            Assert::IsFalse(policy.Admit(-1, 1000));
            Assert::IsFalse(policy.Admit(100, 0));
            Assert::IsFalse(policy.Admit(100, -1000));
            Assert::IsTrue(policy.Admit(100, 1000));
            Assert::IsFalse(policy.Admit(99, 1000));
            Assert::IsFalse(policy.Admit(599, 1000));
            Assert::IsTrue(policy.Admit(600, 1000));
        }

        TEST_METHOD(CancelledShadowWorkCannotRefundItsRateSlot)
        {
            SourcePictureShadowPolicy policy(true);
            Assert::IsTrue(policy.Admit(100, 1000));
            // The caller exhausts/cancels its 8ms measurement here. There is no
            // completion/renewal API: another source still needs the full interval.
            Assert::IsFalse(policy.Admit(108, 1000));
            Assert::IsFalse(policy.Admit(599, 1000));
            Assert::IsTrue(policy.Admit(600, 1000));
        }

        TEST_METHOD(ShadowLongIdleDoesNotCreateCatchupBurst)
        {
            SourcePictureShadowPolicy policy(true);
            Assert::IsTrue(policy.Admit(0, 1000));
            Assert::IsTrue(policy.Admit(1000000, 1000));
            for (int64_t tick = 1000000; tick < 1000500; ++tick)
                Assert::IsFalse(policy.Admit(tick, 1000));
            Assert::IsTrue(policy.Admit(1000500, 1000));
        }

        TEST_METHOD(ShadowOddAndExtremeFrequenciesRoundSafely)
        {
            SourcePictureShadowPolicy odd(true);
            Assert::IsTrue(odd.Admit(0, 1001));
            Assert::IsFalse(odd.Admit(500, 1001));
            Assert::IsTrue(odd.Admit(501, 1001));
            const int64_t maximum = (std::numeric_limits<int64_t>::max)();
            SourcePictureShadowPolicy huge(true);
            Assert::IsTrue(huge.Admit(0, maximum));
            Assert::IsFalse(huge.Admit(maximum / 2, maximum));
            Assert::IsTrue(huge.Admit(maximum / 2 + 1, maximum));
            Assert::IsFalse(huge.Admit(maximum, maximum));
            Assert::IsFalse(huge.Admit(maximum, maximum));
        }
    };
}
