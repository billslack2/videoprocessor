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
        TEST_METHOD(BrokenSourceTimestampsUseOneAnchoredDomainForPeekAndAccept)
        {
            DisplaySourceClock source;
            Assert::AreEqual(10.0,source.Accept(10.0,100,24,1,false),1e-9);
            const double peek=source.Preview(10.0,101,24,1);
            Assert::AreEqual(10.0+1.0/24,peek,1e-9);
            Assert::AreEqual(peek,source.Accept(10.0,101,24,1,false),1e-9);
            Assert::IsTrue(source.Synthetic());
            Assert::AreEqual(10.0+2.0/24,source.Accept(1.0,102,24,1,false),1e-9);
            Assert::AreEqual(10.0+3.0/24,source.Accept(200.0,103,24,1,false),1e-9);
            Assert::AreEqual(2.0,source.Accept(2.0,1,24,2,false),1e-9);
            Assert::IsFalse(source.Synthetic());
        }
        TEST_METHOD(MissingTimestampsAndCadenceRepeatsShareTheSameFallback)
        {
            DisplaySourceClock source;
            Assert::AreEqual(4.0,source.Accept(NAN,96,24,1,false),1e-9);
            Assert::AreEqual(4.0,source.Preview(NAN,96,24,1),1e-9);
            const double next=source.Preview(NAN,97,24,1);
            Assert::AreEqual(4.0+1.0/24,next,1e-9);
            Assert::AreEqual(next,source.Accept(NAN,97,24,1,false),1e-9);
            Assert::AreEqual(0.0,source.Accept(0.0,0,24,1,true),1e-9);
        }
        TEST_METHOD(HealthySourceClockKeepsActualDriftAndRejectsBackwardJitter)
        {
            DisplaySourceClock source;
            source.Accept(1.0,1,60,1,false);
            Assert::AreEqual(1.0+1.0/59.94,source.Accept(1.0+1.0/59.94,2,60,1,false),1e-9);
            Assert::IsFalse(source.Synthetic());
            const double previous=1.0+1.0/59.94;
            Assert::AreEqual(previous+1.0/60,source.Accept(previous-1e-6,3,60,1,false),1e-9);
            Assert::IsTrue(source.Synthetic());
        }
        TEST_METHOD(HighRefreshRepeatsNominalFieldPicturesWithoutExtraTemporalWork)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0.0,1000000000,1,360,false);
            int slots=0;
            while(!clock.SourceDue(1.0/40)) { ++slots; clock.Submitted(); }
            Assert::AreEqual(9,slots);
            Assert::AreEqual(1.0/40,clock.SourcePts(),1e-9);
        }
        TEST_METHOD(FractionalFilmRatesKeepCadenceWithoutRetimingToDisplay)
        {
            for(double hz:{119.88,120.0}) {
                DisplayVsyncClock clock;
                clock.BeginSource(0,1000000000,1,hz,false);
                int slots=0;
                for(int source=1;source<=23976;++source)
                    while(!clock.SourceDue(source/23.976)) { clock.Submitted(); ++slots; }
                Assert::AreEqual(hz==120.0?120000:119880,slots);
            }
        }
        TEST_METHOD(FeedbackRequiresStableSamplesAndAccountsForQueuedLead)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(0,1000000000,1,100,false);
            clock.Submitted();
            Assert::IsFalse(clock.ObserveQueueTail(10,9,90,90,1000000000,1000000000,1000000000,2));
            clock.Submitted();
            Assert::IsFalse(clock.ObserveQueueTail(11,10,91,91,1010000000,1010000000,1010000000,2));
            clock.Submitted();
            Assert::IsTrue(clock.ObserveQueueTail(12,11,92,92,1020000000,1020000000,1020000000,2));
            Assert::IsTrue(clock.HasFeedback());
            Assert::AreEqual(int64_t(1040000000),clock.DeadlineNs());
            clock.SkipMissed(1030000000);
            Assert::AreEqual(0.03,clock.SourcePts(),1e-9);
            Assert::IsFalse(clock.ObserveQueueTail(30,11,92,92,1020000000,1020000000,1020000000,2));
            clock.InvalidateFeedback(); Assert::IsFalse(clock.HasFeedback());
        }
        TEST_METHOD(NonfiniteTimestampCannotDrainAnActiveSlotClock)
        {
            DisplayVsyncClock clock;
            clock.BeginSource(1.0,1000000000,1,60,false);
            Assert::IsFalse(clock.SourceDue(NAN));
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
