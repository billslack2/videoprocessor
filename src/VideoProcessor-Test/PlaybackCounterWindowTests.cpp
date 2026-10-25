#include "pch.h"
#include "CppUnitTest.h"
#include <PlaybackCounterWindow.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(PlaybackCounterWindowTests)
    {
    public:
        TEST_METHOD(FirstObservationCountsSessionEventsAndRepeatedSamplesDoNotDoubleCount)
        {
            PlaybackCounterWindow counter;
            Assert::AreEqual(uint64_t(0), counter.Total());
            Assert::AreEqual(uint64_t(12), counter.Observe(12, 1));
            Assert::AreEqual(uint64_t(12), counter.Observe(12, 1));
            Assert::AreEqual(uint64_t(17), counter.Observe(17, 1));
        }

        TEST_METHOD(ProducerDecreaseRetainsHistoryAndCountsItsNewEvents)
        {
            PlaybackCounterWindow counter;
            counter.Observe(12, 1);
            Assert::AreEqual(uint64_t(15), counter.Observe(3, 1));
            Assert::AreEqual(uint64_t(17), counter.Observe(5, 1));
            Assert::AreEqual(uint64_t(17), counter.Observe(0, 1));
            Assert::AreEqual(uint64_t(21), counter.Observe(4, 1));
        }

        TEST_METHOD(NewDomainCountsItsRawTotalEvenWhenGreaterThanTheOldRawValue)
        {
            PlaybackCounterWindow counter;
            counter.Observe(12, 41);
            Assert::AreEqual(uint64_t(42), counter.Observe(30, 42));
            Assert::AreEqual(uint64_t(42), counter.Observe(30, 42));
            Assert::AreEqual(uint64_t(45), counter.Observe(33, 42));
            Assert::AreEqual(uint64_t(47), counter.Observe(2, 43));
        }

        TEST_METHOD(ManualResetSeedsCurrentRawValueAndCountsOnlyLaterEvents)
        {
            PlaybackCounterWindow counter;
            counter.Observe(12, 1);
            counter.Observe(3, 2);
            counter.Reset(7, 2); // Fresh host query may be newer than the last OSD draw.
            Assert::AreEqual(uint64_t(0), counter.Total());
            Assert::AreEqual(uint64_t(0), counter.Observe(7, 2));
            Assert::AreEqual(uint64_t(2), counter.Observe(9, 2));
            Assert::AreEqual(uint64_t(3), counter.Observe(1, 2));
        }

        TEST_METHOD(RepeatedResetsNeverRecountTheProducerBaseline)
        {
            PlaybackCounterWindow counter;
            counter.Observe(90, 5);
            counter.Reset(100, 5);
            counter.Reset(100, 5);
            Assert::AreEqual(uint64_t(0), counter.Observe(100, 5));
            Assert::AreEqual(uint64_t(3), counter.Observe(103, 5));
            counter.Reset(105, 5);
            Assert::AreEqual(uint64_t(0), counter.Observe(105, 5));
            Assert::AreEqual(uint64_t(1), counter.Observe(106, 5));
        }

        TEST_METHOD(ResetBeforeFirstSampleDoesNotCountPreviouslyHiddenEvents)
        {
            PlaybackCounterWindow counter;
            counter.Reset(300, 8);
            Assert::AreEqual(uint64_t(0), counter.Total());
            Assert::AreEqual(uint64_t(0), counter.Observe(300, 8));
            Assert::AreEqual(uint64_t(4), counter.Observe(304, 8));
            PlaybackCounterWindow initiallyEmpty;
            initiallyEmpty.Reset(0, 9);
            Assert::AreEqual(uint64_t(2), initiallyEmpty.Observe(2, 9));
        }

        TEST_METHOD(ProducerReplacementAfterResetCountsTheNewDomainWithoutOldHistory)
        {
            PlaybackCounterWindow counter;
            counter.Observe(80, 3);
            counter.Reset(80, 3);
            Assert::AreEqual(uint64_t(100), counter.Observe(100, 4));
            Assert::AreEqual(uint64_t(101), counter.Observe(101, 4));
            counter.Reset(101, 4);
            Assert::AreEqual(uint64_t(5), counter.Observe(5, 5));
        }

        TEST_METHOD(CumulativeOverflowSaturatesAndExplicitResetRestoresCounting)
        {
            const auto maximum = (std::numeric_limits<uint64_t>::max)();
            PlaybackCounterWindow counter;
            Assert::AreEqual(maximum - 2, counter.Observe(maximum - 2, 1));
            Assert::AreEqual(maximum - 1, counter.Observe(1, 2));
            Assert::AreEqual(maximum, counter.Observe(4, 2));
            Assert::AreEqual(maximum, counter.Observe(0, 2));
            Assert::AreEqual(maximum, counter.Observe(maximum, 3));
            counter.Reset(maximum - 1, 3);
            Assert::AreEqual(uint64_t(1), counter.Observe(maximum, 3));
            Assert::AreEqual(uint64_t(3), counter.Observe(2, 3));
        }
    };
}