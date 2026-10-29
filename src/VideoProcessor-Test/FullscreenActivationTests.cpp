#include "pch.h"
#include "CppUnitTest.h"
#include <FullscreenActivation.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest
{
    namespace
    {
        using FullscreenActivation::Decision;
        constexpr uintptr_t Host = 0x1001, Replacement = 0x2002;
        constexpr uint32_t Generation = 7;
        void Expect(Decision expected, Decision actual)
        {
            Assert::AreEqual(static_cast<unsigned>(expected), static_cast<unsigned>(actual));
        }
        Decision Tick(FullscreenActivation::Session& session, uint64_t now)
        {
            return session.Evaluate(now, true, false, Host, Generation, true);
        }
    }
    TEST_CLASS(FullscreenActivationTests)
    {
    public:
        TEST_METHOD(ImmediateRevealAndNineOneSecondRetriesThenStop)
        {
            FullscreenActivation::Session session;
            Assert::IsTrue(session.Arm(0));
            session.OnRevealed(Host, Generation, 500);
            Assert::AreEqual(uint64_t(10500), session.Deadline());
            Assert::AreEqual(uint64_t(1), session.DelayMs(500));
            for (uint32_t attempt = 0; attempt < 10; ++attempt)
            {
                Expect(Decision::Activate, Tick(session, 500 + attempt * 1000));
                Assert::AreEqual(attempt + 1, session.Attempts());
                Assert::AreEqual(attempt < 9, session.Pending());
            }
            Expect(Decision::Idle, Tick(session, 10500));
            Assert::IsTrue(session.Attempted());
            Assert::IsFalse(session.Arm(20000));
            session.OnRevealed(Replacement, Generation + 1, 20000);
            Expect(Decision::Idle, Tick(session, 20000));
        }
        TEST_METHOD(EarlyAndDuplicateMessagesCannotCompressRetryInterval)
        {
            FullscreenActivation::Session session;
            session.Arm(0); session.OnRevealed(Host, Generation, 100);
            Expect(Decision::Activate, Tick(session, 100));
            for (const uint64_t tick : {uint64_t(100), uint64_t(101), uint64_t(1099)})
                Expect(Decision::WaitDelay, Tick(session, tick));
            Assert::AreEqual(uint32_t(1), session.Attempts());
            Assert::AreEqual(uint64_t(1), session.DelayMs(1099));
            Expect(Decision::Activate, Tick(session, 1100));
        }
        TEST_METHOD(SlowStartupStartsFullWindowAtValidatedReveal)
        {
            FullscreenActivation::Session session;
            session.Arm(0);
            Expect(Decision::WaitReady, Tick(session, 20000));
            Assert::AreEqual(uint64_t(1000), session.DelayMs(20000));
            Assert::AreEqual(uint32_t(0), session.Attempts());
            session.OnRevealed(0, Generation, 20000);
            session.OnRevealed(Host, 0, 20000);
            Assert::AreEqual(uint64_t(0), session.Deadline());
            session.OnRevealed(Host, Generation, 20000);
            Assert::AreEqual(uint64_t(30000), session.Deadline());
            Expect(Decision::Activate, Tick(session, 20000));
        }
        TEST_METHOD(StalledUiDoesNotCatchUpOrActivateAfterTenSeconds)
        {
            FullscreenActivation::Session session;
            session.Arm(0); session.OnRevealed(Host, Generation, 0);
            Expect(Decision::Activate, Tick(session, 8500));
            Expect(Decision::WaitDelay, Tick(session, 8500));
            Expect(Decision::Activate, Tick(session, 9500));
            Assert::AreEqual(uint64_t(500), session.DelayMs(9500));
            Expect(Decision::Idle, Tick(session, 10000));
            Expect(Decision::Idle, Tick(session, 20000));
            Assert::AreEqual(uint32_t(2), session.Attempts());
            Assert::IsFalse(session.Pending());
        }
        TEST_METHOD(ReplacedHostAndGenerationRequireFreshRevealWithoutExtendingWindow)
        {
            FullscreenActivation::Session session;
            session.Arm(0); session.OnRevealed(Host, Generation, 0);
            Expect(Decision::Activate, Tick(session, 0));
            Expect(Decision::WaitReady, session.Evaluate(1000, true, false, Replacement, Generation + 1, true));
            session.OnRevealed(Replacement, Generation, 1100);
            Expect(Decision::WaitReady, session.Evaluate(2000, true, false, Replacement, Generation + 1, true));
            session.OnRevealed(Replacement, Generation + 1, 2100);
            Expect(Decision::Activate, session.Evaluate(3000, true, false, Replacement, Generation + 1, true));
            Assert::AreEqual(uint64_t(10000), session.Deadline());
            Assert::IsFalse(session.Arm(4000));
            Expect(Decision::Idle, session.Evaluate(10000, true, false, Replacement, Generation + 1, true));
        }
        TEST_METHOD(ShieldAndRetargetWaitAtOneSecondIntervalsAndExpire)
        {
            FullscreenActivation::Session session;
            session.Arm(0); session.OnRevealed(Host, Generation, 0);
            Expect(Decision::WaitReady, session.Evaluate(0, true, false, Host, Generation, false));
            Assert::AreEqual(uint64_t(1000), session.DelayMs(0));
            Expect(Decision::WaitDelay, Tick(session, 500));
            session.ClearReady();
            Expect(Decision::WaitReady, Tick(session, 1000));
            Assert::AreEqual(uint64_t(1000), session.DelayMs(1000));
            session.OnRevealed(Host, Generation, 1500);
            Expect(Decision::Activate, Tick(session, 2000));
            session.ClearReady();
            Expect(Decision::Idle, Tick(session, 10000));
            Assert::IsFalse(session.Pending());
        }
        TEST_METHOD(FullscreenExitCancelsAndReentryStartsNewWindow)
        {
            FullscreenActivation::Session session;
            session.Arm(0); session.OnRevealed(Host, Generation, 0);
            Expect(Decision::Activate, Tick(session, 0));
            Expect(Decision::Cancel, session.Evaluate(500, false, false, Host, Generation, true));
            Assert::IsFalse(session.Pending()); Assert::IsFalse(session.Attempted());
            Assert::AreEqual(uint64_t(0), session.Deadline());
            Assert::IsTrue(session.Arm(1000));
            Expect(Decision::WaitReady, Tick(session, 2000));
            session.OnRevealed(Host, Generation, 3000);
            Expect(Decision::Activate, Tick(session, 3000));
            Assert::AreEqual(uint32_t(1), session.Attempts());
            Assert::AreEqual(uint64_t(13000), session.Deadline());
        }
        TEST_METHOD(ShutdownCancelsBeforeAndDuringRetries)
        {
            for (const bool revealed : {false, true})
            {
                FullscreenActivation::Session session;
                session.Arm(0);
                if (revealed) { session.OnRevealed(Host, Generation, 0); Tick(session, 0); }
                Expect(Decision::Cancel, session.Evaluate(500, true, true, Host, Generation, true));
                Assert::IsFalse(session.Pending()); Assert::IsFalse(session.Attempted());
                Expect(Decision::Idle, Tick(session, 1000));
            }
        }
        TEST_METHOD(IdleAndInvalidTargetsCannotSpendAttempts)
        {
            FullscreenActivation::Session session;
            session.OnRevealed(Host, Generation, 0);
            Expect(Decision::Idle, Tick(session, 0));
            session.Arm(0); session.OnRevealed(Host, Generation, 0);
            Expect(Decision::WaitReady, session.Evaluate(0, true, false, 0, Generation, true));
            Expect(Decision::WaitReady, session.Evaluate(1000, true, false, Host, 0, true));
            Assert::AreEqual(uint32_t(0), session.Attempts());
            Expect(Decision::Activate, Tick(session, 2000));
        }
    };
}
