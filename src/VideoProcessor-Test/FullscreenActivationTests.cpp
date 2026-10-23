#include "pch.h"
#include "CppUnitTest.h"
#include <FullscreenActivation.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        using FullscreenActivation::Decision;
        constexpr uintptr_t Host = 0x1001;
        constexpr uintptr_t Replacement = 0x2002;
        constexpr uint32_t Generation = 7;

        void Expect(Decision expected, Decision actual)
        {
            Assert::AreEqual(static_cast<unsigned>(expected),
                static_cast<unsigned>(actual));
        }
    }

    TEST_CLASS(FullscreenActivationTests)
    {
    public:
        TEST_METHOD(ArmKeepsOriginalFiveSecondDeadline)
        {
            FullscreenActivation::Session session;
            Assert::IsTrue(session.Arm(100));
            Assert::IsTrue(session.Pending());
            Assert::IsFalse(session.Attempted());
            Assert::AreEqual(uint64_t(5100), session.Deadline());
            Assert::IsFalse(session.Arm(4000));
            Assert::AreEqual(uint64_t(5100), session.Deadline());
            Assert::AreEqual(uint64_t(5000), session.DelayMs(100));
            Assert::AreEqual(uint64_t(1), session.DelayMs(5099));
            Assert::AreEqual(uint64_t(1), session.DelayMs(5100));
            Assert::AreEqual(uint64_t(1), session.DelayMs(20000));
        }

        TEST_METHOD(EarlyOrStaleTimerCannotSpendAttemptBeforeDeadline)
        {
            FullscreenActivation::Session session;
            session.Arm(100);
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitDelay,
                session.Evaluate(101, true, false, Host, Generation, true));
            Expect(Decision::WaitDelay,
                session.Evaluate(5099, true, false, Host, Generation, true));
            Assert::IsFalse(session.Attempted());
            Expect(Decision::Activate,
                session.Evaluate(5100, true, false, Host, Generation, true));
            Assert::IsFalse(session.Pending());
            Assert::IsTrue(session.Attempted());
        }

        TEST_METHOD(SlowStartupRetainsAttemptUntilMatchingRevealWithoutNewDelay)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            Expect(Decision::WaitReady,
                session.Evaluate(5000, true, false, Host, Generation, true));
            Expect(Decision::WaitReady,
                session.Evaluate(20000, true, false, Host, Generation, true));
            Assert::IsTrue(session.Pending());
            Assert::IsFalse(session.Attempted());
            session.OnRevealed(Host, Generation);
            Assert::AreEqual(uint64_t(5000), session.Deadline());
            Expect(Decision::Activate,
                session.Evaluate(20000, true, false, Host, Generation, true));
        }

        TEST_METHOD(ReplacedHostRequiresItsOwnSuccessfulReveal)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitReady,
                session.Evaluate(5000, true, false, Replacement, Generation, true));
            session.OnRevealed(Replacement, Generation);
            Expect(Decision::Activate,
                session.Evaluate(5000, true, false, Replacement, Generation, true));
        }

        TEST_METHOD(ReusedHostRejectsStaleRendererGeneration)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitReady,
                session.Evaluate(5000, true, false, Host, Generation + 1, true));
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitReady,
                session.Evaluate(6000, true, false, Host, Generation + 1, true));
            session.OnRevealed(Host, Generation + 1);
            Expect(Decision::Activate,
                session.Evaluate(6000, true, false, Host, Generation + 1, true));
        }

        TEST_METHOD(RetargetOrShieldReadinessCannotBeBypassedByOldReveal)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitReady,
                session.Evaluate(5000, true, false, Host, Generation, false));
            session.ClearReady();
            Expect(Decision::WaitReady,
                session.Evaluate(6000, true, false, Host, Generation, true));
            Assert::IsTrue(session.Pending());
            session.OnRevealed(Host, Generation);
            Expect(Decision::Activate,
                session.Evaluate(6000, true, false, Host, Generation, true));
        }

        TEST_METHOD(FullscreenExitCancelsAndReentryStartsFreshSession)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            session.OnRevealed(Host, Generation);
            Expect(Decision::Cancel,
                session.Evaluate(2000, false, false, Host, Generation, true));
            Assert::IsFalse(session.Pending());
            Assert::IsFalse(session.Attempted());
            Assert::AreEqual(uint64_t(0), session.Deadline());
            Assert::IsTrue(session.Arm(3000));
            Expect(Decision::WaitDelay,
                session.Evaluate(5000, true, false, Host, Generation, true));
            Expect(Decision::WaitReady,
                session.Evaluate(8000, true, false, Host, Generation, true));
            session.OnRevealed(Replacement, Generation + 1);
            Expect(Decision::Activate,
                session.Evaluate(8000, true, false, Replacement, Generation + 1, true));
            session.Cancel();
            Assert::IsTrue(session.Arm(9000));
        }

        TEST_METHOD(ShutdownCancelsBeforeOrAfterDeadline)
        {
            for (const uint64_t tick : { uint64_t(100), uint64_t(6000) })
            {
                FullscreenActivation::Session session;
                session.Arm(0);
                session.OnRevealed(Host, Generation);
                Expect(Decision::Cancel,
                    session.Evaluate(tick, true, true, Host, Generation, true));
                Assert::IsFalse(session.Pending());
                Assert::IsFalse(session.Attempted());
                Expect(Decision::Idle,
                    session.Evaluate(10000, true, false, Host, Generation, true));
            }
        }

        TEST_METHOD(OsDenialAndRendererRestartCannotRearmCompletedAttempt)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            session.OnRevealed(Host, Generation);
            Expect(Decision::Activate,
                session.Evaluate(5000, true, false, Host, Generation, true));
            // The native API result is deliberately absent from Session's API:
            // either success or denial consumes the same single attempt.
            Assert::IsFalse(session.Arm(6000));
            session.OnRevealed(Replacement, Generation + 1);
            Expect(Decision::Idle,
                session.Evaluate(10000, true, false, Replacement, Generation + 1, true));
            Expect(Decision::Idle,
                session.Evaluate(20000, true, false, Host, Generation, true));
            Assert::IsTrue(session.Attempted());
            Assert::AreEqual(uint64_t(5000), session.Deadline());
        }

        TEST_METHOD(IdleAndInvalidTargetsCannotActivate)
        {
            FullscreenActivation::Session session;
            session.OnRevealed(Host, Generation);
            Expect(Decision::Idle,
                session.Evaluate(10000, true, false, Host, Generation, true));
            session.Arm(0);
            session.OnRevealed(0, Generation);
            session.OnRevealed(Host, 0);
            Expect(Decision::WaitReady,
                session.Evaluate(10000, true, false, Host, Generation, true));
            session.OnRevealed(Host, Generation);
            Expect(Decision::WaitReady,
                session.Evaluate(10000, true, false, 0, Generation, true));
            Expect(Decision::WaitReady,
                session.Evaluate(10000, true, false, Host, 0, true));
        }
    };
}
