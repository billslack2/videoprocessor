#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/DolbyVisionLuminancePolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(DolbyVisionLuminancePolicyTests)
    {
    public:
        TEST_METHOD(RecoveredRangeReplacesFallbackAndClearsInventedContentLight)
        {
            pl_hdr_metadata hdr{};
            hdr.min_luma = 0.1f;
            hdr.max_luma = hdr.max_cll = hdr.max_fall = 1000.0f;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.sourceMaxPq = 4095;
            const auto status = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsTrue(status.available && status.sourceRangeFromMetadata);
            Assert::AreEqual(10000.0f, hdr.max_luma, 1.0f);
            Assert::AreEqual(0.0f, hdr.min_luma, 0.001f);
            Assert::AreEqual(0.0f, hdr.max_cll);
            Assert::AreEqual(0.0f, hdr.max_fall);
            Assert::AreEqual(hdr.max_luma, status.sourceMax);
        }
        TEST_METHOD(InvalidSourceRangeKeepsExplicitFallbackWithoutInventingScene)
        {
            pl_hdr_metadata hdr{};
            hdr.max_luma = 1000.0f;
            hdr.max_cll = hdr.max_fall = 4000.0f;
            hdr.max_pq_y = hdr.avg_pq_y = 0.8f;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.sourceMaxPq = 5000;
            const auto status = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsTrue(status.available);
            Assert::IsFalse(status.sourceRangeFromMetadata || status.sceneAvailable);
            Assert::AreEqual(1000.0f, status.sourceMax);
            Assert::AreEqual(0.0f, hdr.max_cll);
            Assert::AreEqual(0.0f, hdr.max_fall);
            Assert::AreEqual(0.0f, hdr.max_pq_y);
            Assert::AreEqual(0.0f, hdr.avg_pq_y);
        }
        TEST_METHOD(SceneValuesRemainDistinctFromSourceRangeAndContentLight)
        {
            pl_hdr_metadata hdr{};
            hdr.max_luma = hdr.max_cll = hdr.max_fall = 1000.0f;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.sourceMaxPq = 4095;
            dm.hasSceneMetadata = true;
            dm.sceneMaxPq = 2048;
            dm.sceneAvgPq = 1024;
            const auto status = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsTrue(status.sceneAvailable);
            Assert::IsTrue(status.sourceMax > status.sceneMax && status.sceneMax > status.sceneAvg);
            Assert::AreEqual(2048.0f / 4095.0f, hdr.max_pq_y);
            Assert::AreEqual(1024.0f / 4095.0f, hdr.avg_pq_y);
            Assert::AreEqual(0.0f, hdr.max_cll);
            Assert::AreEqual(0.0f, hdr.max_fall);
        }
        TEST_METHOD(BlackSceneIsPresentAndNotConfusedWithMissingL1)
        {
            pl_hdr_metadata hdr{};
            hdr.max_luma = 1000.0f;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.sourceMaxPq = 4095;
            dm.hasSceneMetadata = true;
            const auto black = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsTrue(black.sceneAvailable);
            Assert::AreEqual(0.0f, black.sceneMax, 0.001f);
            Assert::AreEqual(0.0f, black.sceneAvg, 0.001f);
            dm.hasSceneMetadata = false;
            const auto missing = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsFalse(missing.sceneAvailable);
        }
        TEST_METHOD(LostMetadataDoesNotPublishPreviousFrameValues)
        {
            pl_hdr_metadata hdr{};
            hdr.max_luma = 1000.0f;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.sourceMaxPq = 4095;
            dm.hasSceneMetadata = true;
            dm.sceneMaxPq = 3072;
            DolbyVisionLuminancePolicy::Apply(dm, hdr);
            dm.valid = false;
            const auto lost = DolbyVisionLuminancePolicy::Apply(dm, hdr);
            Assert::IsTrue(lost.tunnelActive);
            Assert::IsFalse(lost.available || lost.sceneAvailable || lost.sourceRangeFromMetadata);
            Assert::AreEqual(0.0f, lost.sourceMax);
            Assert::AreEqual(0.0f, lost.sceneMax);
            Assert::AreEqual(0.0f, hdr.max_pq_y);
        }
    };
}
