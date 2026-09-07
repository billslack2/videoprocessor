#include "pch.h"
#include "CppUnitTest.h"
#include <CadenceIntervalEstimate.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(CadenceIntervalEstimateTests)
    {
        using Estimate = CadenceIntervalEstimate;
        static void Feed(Estimate& estimate, uint64_t begin, uint64_t end,
            double difference, const Estimate::Contract& contract = {})
        {
            for (uint64_t ms = begin; ms <= end; ms += 1000)
                estimate.Update(ms, 24.0 + difference, 24.0, contract);
        }
    public:
        TEST_METHOD(FormatsRequestedIntervalsAndRoundingBoundaries)
        {
            Assert::AreEqual(L"1h5m30s", Estimate::FormatInterval(3930.0).c_str());
            Assert::AreEqual(L"48m5s", Estimate::FormatInterval(2885.0).c_str());
            Assert::AreEqual(L"1h0m0s", Estimate::FormatInterval(3599.9).c_str());
            Assert::AreEqual(L"<1s", Estimate::FormatInterval(0.5).c_str());
        }
        TEST_METHOD(PublishesDropAndRepeatAfterInitialEvidence)
        {
            Estimate estimate;
            Feed(estimate, 0, 29000, 1.0 / 3930.0);
            Assert::AreEqual(L"Warming", estimate.Text().c_str());
            Feed(estimate, 30000, 30000, 1.0 / 3930.0);
            Assert::AreEqual(L"Drop every 1h5m30s", estimate.Text().c_str());
            estimate.Reset();
            Feed(estimate, 0, 30000, -1.0 / 2885.0);
            Assert::AreEqual(L"Repeat every 48m5s", estimate.Text().c_str());
        }
        TEST_METHOD(AveragesSignedDifferenceBeforeReciprocalAndAgesOutOldRate)
        {
            Estimate estimate;
            Feed(estimate, 0, 90000, 0.002);
            Feed(estimate, 91000, 180000, -0.001);
            Assert::AreEqual(0.0005, estimate.MeanDifferenceHz(), 1e-12);
            Assert::AreEqual(L"Drop every 33m20s", estimate.Text().c_str());
            Feed(estimate, 181000, 270000, -0.001);
            Assert::AreEqual(180.0, estimate.EvidenceSeconds(), 1e-9);
            Assert::AreEqual(L"Repeat every 16m40s", estimate.Text().c_str());
        }
        TEST_METHOD(WeightsElapsedTimeRatherThanUiRefreshCount)
        {
            Estimate estimate;
            estimate.Update(0, 24.001, 24.0, {});
            estimate.Update(1000, 24.001, 24.0, {});
            for (uint64_t ms = 1001; ms < 2000; ++ms)
                estimate.Update(ms, 24.5, 24.0, {});
            estimate.Update(4000, 24.003, 24.0, {});
            Assert::AreEqual(0.0025, estimate.MeanDifferenceHz(), 1e-12);
        }
        TEST_METHOD(ResyncAndContractChangesDiscardOldEvidence)
        {
            Estimate estimate;
            Estimate::Contract contract;
            contract.displayNominalHz = 24.0;
            Feed(estimate, 0, 180000, 0.001, contract);
            estimate.Reset(); // HDMI loss or resync, even at the same rate.
            Feed(estimate, 181000, 210000, -0.001, contract);
            Assert::AreEqual(L"Warming", estimate.Text().c_str());
            Feed(estimate, 211000, 211000, -0.001, contract);
            Assert::AreEqual(L"Repeat every 16m40s", estimate.Text().c_str());
            contract.displayNominalHz = 24000.0 / 1001.0;
            Feed(estimate, 212000, 212000, -0.001, contract);
            Assert::AreEqual(0.0, estimate.EvidenceSeconds());
            Feed(estimate, 213000, 242000, -0.001, contract);
            contract.monitor = L"other monitor";
            Feed(estimate, 243000, 243000, 0.001, contract);
            Assert::AreEqual(L"Warming", estimate.Text().c_str());
            contract.overrideHz = 23.9761;
            Feed(estimate, 244000, 244000, 0.001, contract);
            Assert::AreEqual(0.0, estimate.EvidenceSeconds());
        }
        TEST_METHOD(MissingInvalidAndStaleRatesCannotRetainAnEstimate)
        {
            Estimate estimate;
            Feed(estimate, 0, 30000, 0.001);
            estimate.Update(31000, 0.0, 24.0, {});
            Assert::AreEqual(L"Unavailable", estimate.Text().c_str());
            Feed(estimate, 32000, 62000, 0.001);
            estimate.Update(63000, std::numeric_limits<double>::quiet_NaN(), 24.0, {});
            Assert::AreEqual(L"Unavailable", estimate.Text().c_str());
            Feed(estimate, 64000, 94000, 0.001);
            estimate.Update(100000, 24.001, 24.0, {});
            Assert::AreEqual(L"Warming", estimate.Text().c_str());
            estimate.Update(99000, 24.001, 24.0, {});
            Assert::AreEqual(0.0, estimate.EvidenceSeconds());
        }
        TEST_METHOD(IntervalsBeyondTwoDaysShowNoneForBothDirections)
        {
            Estimate estimate;
            Feed(estimate, 0, 30000, 1.0 / 172801.0);
            Assert::AreEqual(L"None", estimate.Text().c_str());
            estimate.Reset();
            Feed(estimate, 0, 30000, -1.0 / 172801.0);
            Assert::AreEqual(L"None", estimate.Text().c_str());
            estimate.Reset();
            Feed(estimate, 0, 30000, 1.0 / 172799.0);
            Assert::AreEqual(L"Drop every 47h59m59s", estimate.Text().c_str());
            estimate.Reset();
            Feed(estimate, 0, 30000, 1.0 / 172800.0);
            Assert::AreEqual(L"Drop every 48h0m0s", estimate.Text().c_str());
            estimate.Reset();
            Feed(estimate, 0, 30000, -1.0 / 90000.0);
            Assert::AreEqual(L"Repeat every 25h0m0s", estimate.Text().c_str());
        }
        TEST_METHOD(MatchedRatesDoNotInventADropOrRepeat)
        {
            Estimate estimate;
            Feed(estimate, 0, 180000, 0.0);
            Assert::AreEqual(L"None", estimate.Text().c_str());
        }
    };
}
