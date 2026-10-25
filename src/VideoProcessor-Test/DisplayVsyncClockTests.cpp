#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/DisplayVsyncClock.h>
#include <limits>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
    TEST_CLASS(DisplayVsyncClockTests)
    {
    public:
        TEST_METHOD(FilmOn120HzRepeatsFourTimesBetweenSources)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(10.0, 1000000000, 1, 120.0, false);
            clock.Submitted();
            for (int repeat = 0; repeat < 4; ++repeat) {
                Assert::IsFalse(clock.SourceDue(10.0 + 1.0 / 24.0));
                clock.Submitted();
            }
            Assert::IsTrue(clock.SourceDue(10.0 + 1.0 / 24.0));
        }
        TEST_METHOD(FilmOn60HzUsesThreeTwoCadenceWithoutBlending)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            int counts[4]{};
            for (int frame = 0; frame < 4; ++frame)
                while (!clock.SourceDue((frame + 1) / 24.0)) {
                    ++counts[frame]; clock.Submitted();
                }
            Assert::AreEqual(3, counts[0]); Assert::AreEqual(2, counts[1]);
            Assert::AreEqual(3, counts[2]); Assert::AreEqual(2, counts[3]);
        }
        TEST_METHOD(DiscontinuityGenerationAndRateChangesReanchor)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(10.0, 1000000000, 1, 120.0, false);
            clock.Submitted();
            clock.BeginSource(2.0, 2000000000, 1, 120.0, true);
            Assert::AreEqual(2.0, clock.SourcePts(), 1e-9);
            clock.BeginSource(3.0, 3000000000, 2, 60.0, false);
            Assert::IsFalse(clock.Matches(1, 60.0)); Assert::IsTrue(clock.Matches(2, 60.0));
            clock.BeginSource(3.01, 3010000000, 2, 120.0, false);
            Assert::IsTrue(clock.Matches(2, 120.0));
            Assert::AreEqual(3.01, clock.SourcePts(), 1e-9);
        }
        TEST_METHOD(StallRetiresMissedRefreshesWithoutCatchupBurst)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            clock.Submitted(); clock.SkipMissed(1500000000);
            Assert::AreEqual(0.5, clock.SourcePts(), 1e-9);
            clock.Submitted(); Assert::IsTrue(clock.DeadlineNs() > 1500000000);
        }
        TEST_METHOD(IndependentSourceClockDriftDoesNotReanchorPlayback)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0, 1000000000, 1, 60.0, false);
            for (int frame = 1; frame <= 10000; ++frame) {
                clock.Submitted();
                clock.BeginSource(frame / 59.94,
                    1000000000LL + static_cast<int64_t>(frame * 1e9 / 60.0), 1, 60.0, false);
            }
            Assert::AreEqual(10000.0 / 60.0, clock.SourcePts(), 1e-9);
        }
        TEST_METHOD(UnknownOrInvalidRefreshDisablesTheClock)
        {
            DisplayVsyncClock clock;
            for (double hz : {0.0, -1.0, 1000.0, std::numeric_limits<double>::quiet_NaN()}) {
                clock.BeginSource(1.0, 1000000000, 1, hz, false);
                Assert::IsFalse(clock.Active());
            }
            clock.BeginSource(1.0, 1000000000, 1, 60.0, false);
            clock.Reset(); Assert::IsFalse(clock.Active());
        }
    };
}
