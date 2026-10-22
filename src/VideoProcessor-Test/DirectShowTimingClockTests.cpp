#include "pch.h"
#include "CppUnitTest.h"

#include <atomic>
#include <limits>
#include <microsoft_directshow/DirectShowTimingClock.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    namespace
    {
        class FakeReferenceTimingClock : public ITimingClock
        {
        public:
            explicit FakeReferenceTimingClock(timingclocktime_t ticksPerSecond) :
                rate(ticksPerSecond) {}

            timingclocktime_t TimingClockNow() override
            {
                if (fail.load())
                    throw std::runtime_error("Hardware clock temporarily unavailable");
                return now.load();
            }
            timingclocktime_t TimingClockTicksPerSecond() const override { return rate; }
            const TCHAR* TimingClockDescription() override { return TEXT("Test hardware clock"); }

            std::atomic<timingclocktime_t> now{ 0 };
            std::atomic<bool> fail{ false };
            const timingclocktime_t rate;
        };

        void RequireTime(DirectShowTimingClock& clock, HRESULT expectedResult,
            REFERENCE_TIME expectedTime)
        {
            REFERENCE_TIME observed = -1;
            Assert::AreEqual<HRESULT>(expectedResult, clock.GetTime(&observed));
            Assert::AreEqual<REFERENCE_TIME>(expectedTime, observed);
        }
    }

    TEST_CLASS(DirectShowTimingClockTests)
    {
    public:
        TEST_METHOD(CrossesOldMultiplyOverflowLimitWithoutFreezing)
        {
            FakeReferenceTimingClock hardware(1000000);
            HRESULT result = S_OK;
            DirectShowTimingClock clock(hardware, result);
            Assert::AreEqual<HRESULT>(S_OK, result);

            const timingclocktime_t oldLimit =
                (std::numeric_limits<timingclocktime_t>::max)() / 10000000LL;
            for (timingclocktime_t offset = -1; offset <= 2; ++offset)
            {
                hardware.now = oldLimit + offset;
                RequireTime(clock, S_OK, (oldLimit + offset) * 10);
            }
        }

        TEST_METHOD(LongRunningHardwareClockRetainsEveryMicrosecond)
        {
            FakeReferenceTimingClock hardware(1000000);
            HRESULT result = S_OK;
            DirectShowTimingClock clock(hardware, result);
            Assert::AreEqual<HRESULT>(S_OK, result);

            // The counter already represents 12.7 days when the graph is created.
            // A newly launched player must not require a freshly reset device clock.
            const timingclocktime_t initial = 1097280123456LL;
            const timingclocktime_t offsets[] = { 0, 1, 8333, 16683, 41708 };
            for (const timingclocktime_t offset : offsets)
            {
                hardware.now = initial + offset;
                RequireTime(clock, S_OK, (initial + offset) * 10);
            }
        }

        TEST_METHOD(PreservesTruncationForFractionalReferenceTicks)
        {
            FakeReferenceTimingClock hardware(3);
            HRESULT result = S_OK;
            DirectShowTimingClock clock(hardware, result);
            Assert::AreEqual<HRESULT>(S_OK, result);

            hardware.now = 1;
            RequireTime(clock, S_OK, 3333333);
            hardware.now = 2;
            RequireTime(clock, S_OK, 6666666);
            hardware.now = 3;
            RequireTime(clock, S_OK, 10000000);
        }

        TEST_METHOD(DuplicateRollbackAndHardwareFailureStillHoldLastTime)
        {
            FakeReferenceTimingClock hardware(1000000);
            HRESULT result = S_OK;
            DirectShowTimingClock clock(hardware, result);
            Assert::AreEqual<HRESULT>(S_OK, result);

            hardware.fail = true;
            RequireTime(clock, S_FALSE, 0);
            hardware.fail = false;
            hardware.now = 100;
            RequireTime(clock, S_OK, 1000);
            RequireTime(clock, S_FALSE, 1000);
            hardware.now = 99;
            RequireTime(clock, S_FALSE, 1000);
            hardware.fail = true;
            RequireTime(clock, S_FALSE, 1000);
            hardware.fail = false;
            hardware.now = 101;
            RequireTime(clock, S_OK, 1010);
        }

        TEST_METHOD(NegativeSamplesRetainSignedTruncationAndBaseClockClamp)
        {
            FakeReferenceTimingClock hardware(3);
            HRESULT result = S_OK;
            DirectShowTimingClock clock(hardware, result);
            Assert::AreEqual<HRESULT>(S_OK, result);

            hardware.now = -2;
            Assert::AreEqual<REFERENCE_TIME>(-6666666, clock.GetPrivateTime());
            RequireTime(clock, S_FALSE, 0);
        }
    };
}
