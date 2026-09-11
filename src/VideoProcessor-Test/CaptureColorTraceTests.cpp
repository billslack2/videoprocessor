#include "pch.h"
#include "CppUnitTest.h"
#include <CaptureColorTrace.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(CaptureColorTraceTests)
    {
        static CaptureColorTraceSample Hdr()
        {
            CaptureColorTraceSample sample;
            sample.run = 1;
            sample.metadataAvailable = true;
            sample.readEotf = sample.cachedEotf = 2;
            sample.readColorspace = sample.cachedColorspace = 2020;
            sample.cachedHdr = true;
            return sample;
        }
    public:
        TEST_METHOD(StableCaptureHasTenSecondHeartbeat)
        {
            CaptureColorTrace trace;
            const auto sample = Hdr();
            Assert::IsTrue(trace.Observe(sample, 0).emit);
            for (uint64_t time = 1; time < 10000; ++time)
                Assert::IsFalse(trace.Observe(sample, time).emit);
            const auto result = trace.Observe(sample, 10000);
            Assert::IsTrue(result.emit);
            Assert::AreEqual<uint64_t>(10000, result.observations);
            Assert::AreEqual<uint64_t>(0, result.changes);
        }

        TEST_METHOD(BriefChangeAndReturnAreNotHiddenByThrottle)
        {
            CaptureColorTrace trace;
            auto sample = Hdr();
            trace.Observe(sample, 0);
            sample.readEotf = sample.cachedEotf = 0;
            Assert::IsFalse(trace.Observe(sample, 200).emit);
            sample = Hdr();
            Assert::IsFalse(trace.Observe(sample, 300).emit);
            const auto result = trace.Observe(sample, 1000);
            Assert::IsTrue(result.emit);
            Assert::AreEqual<uint64_t>(2, result.changes);
        }

        TEST_METHOD(AlternatingPerFrameMetadataIsRateLimited)
        {
            CaptureColorTrace trace;
            auto sample = Hdr();
            trace.Observe(sample, 0);
            unsigned reports = 0;
            for (uint64_t time = 4; time <= 20000; time += 4)
            {
                sample.flags ^= 1;
                if (trace.Observe(sample, time).emit)
                    ++reports;
            }
            Assert::AreEqual(20u, reports);
        }

        TEST_METHOD(MissingPropertiesAreSeparateFromReportedSdr)
        {
            CaptureColorTrace trace;
            auto sample = Hdr();
            trace.Observe(sample, 0);
            sample.eotfResult = -1;
            sample.readEotf = -1;
            trace.Observe(sample, 100);
            sample.eotfResult = 0;
            sample.readEotf = sample.cachedEotf = 0;
            sample.colorspaceResult = -1;
            sample.readColorspace = -1;
            trace.Observe(sample, 200);
            sample.metadataAvailable = false;
            const auto result = trace.Observe(sample, 1000);
            Assert::IsTrue(result.emit);
            Assert::AreEqual<uint64_t>(1, result.eotfReadFailures);
            Assert::AreEqual<uint64_t>(1, result.colorspaceReadFailures);
            Assert::AreEqual<uint64_t>(1, result.missingInterface);
            Assert::AreEqual<int64_t>(2020, sample.cachedColorspace);
        }

        TEST_METHOD(EachCaptureRunStartsWithIndependentEvidence)
        {
            CaptureColorTrace trace;
            auto sample = Hdr();
            trace.Observe(sample, 1000);
            sample.metadataAvailable = false;
            trace.Observe(sample, 1100);
            sample = Hdr();
            sample.run = 2;
            const auto result = trace.Observe(sample, 1200);
            Assert::IsTrue(result.emit);
            Assert::AreEqual<uint64_t>(1, result.observations);
            Assert::AreEqual<uint64_t>(0, result.missingInterface);
        }
    };
}
