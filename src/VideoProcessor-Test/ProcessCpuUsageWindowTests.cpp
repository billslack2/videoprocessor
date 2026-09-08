#include "pch.h"
#include "CppUnitTest.h"
#include <ProcessCpuUsageWindow.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(ProcessCpuUsageWindowTests)
    {
    public:
        TEST_METHOD(WeightsUnequalSamplingIntervals)
        {
            ProcessCpuUsageWindow window;
            window.Record(1000, 1000, 10.0);
            window.Record(4000, 3000, 30.0);
            Assert::AreEqual(25.0, window.AveragePercent(), 0.001);
            Assert::AreEqual(30.0, window.PeakPercent(), 0.001);
        }
        TEST_METHOD(ClipsOldestIntervalToExactlyTenSeconds)
        {
            ProcessCpuUsageWindow window;
            window.Record(4000, 4000, 80.0);
            window.Record(12000, 8000, 20.0);
            Assert::AreEqual(32.0, window.AveragePercent(), 0.001);
            Assert::AreEqual(80.0, window.PeakPercent(), 0.001);
        }
        TEST_METHOD(ExpiresPeakAtWindowBoundary)
        {
            ProcessCpuUsageWindow window;
            window.Record(1000, 1000, 90.0);
            window.Record(11000, 10000, 10.0);
            Assert::AreEqual(10.0, window.AveragePercent(), 0.001);
            Assert::AreEqual(10.0, window.PeakPercent(), 0.001);
        }
        TEST_METHOD(LongSamplingGapAndResetDiscardOldEvidence)
        {
            ProcessCpuUsageWindow window;
            window.Record(1000, 1000, 90.0);
            window.Record(31000, 30000, 20.0);
            Assert::AreEqual(20.0, window.AveragePercent(), 0.001);
            Assert::AreEqual(20.0, window.PeakPercent(), 0.001);
            window.Reset();
            Assert::AreEqual(0.0, window.AveragePercent(), 0.001);
            Assert::AreEqual(0.0, window.PeakPercent(), 0.001);
            window.Record(32000, 1000, 5.0);
            Assert::AreEqual(5.0, window.AveragePercent(), 0.001);
        }
    };
}
