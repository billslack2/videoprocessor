#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/DolbyVisionLuminancePolicy.h>
#include <vprenderer/SourceMasteringPrimariesPolicy.h>
#include <libplacebo/colorspace.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
    TEST_CLASS(DolbyVisionLuminancePolicyTests)
    {
    public:
        TEST_METHOD(ValidDvFramesKeepPublishedStatusStableBetweenFrames)
        {
            Assert::IsFalse(
                DolbyVisionLuminancePolicy::ShouldClearStatusForFrame(
                    true, true));
            Assert::IsTrue(
                DolbyVisionLuminancePolicy::ShouldClearStatusForFrame(
                    true, false));
            Assert::IsTrue(
                DolbyVisionLuminancePolicy::ShouldClearStatusForFrame(
                    false, true));
        }

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
        TEST_METHOD(P3MasteringAssumptionHonorsHdrMetadataAndDefaultsOff)
        {
            HDRData provenance;
            provenance.primariesFilledByVp = true;
            HDRData copied(provenance);
            Assert::IsTrue(copied.primariesFilledByVp);
            Assert::IsTrue(provenance == copied);
            copied.primariesFilledByVp = false;
            Assert::IsTrue(provenance != copied);

            VideoState state;
            state.eotf = EOTF::PQ;
            state.colorspace = ColorSpace::BT_2020;
            DolbyVisionTunnelFrameMetadata noDolby;

            auto off = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, false);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(off.selection));
            Assert::AreEqual(std::string("container (no block)"), std::string(off.outcome));
            auto assumed = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::DisplayP3D65),
                static_cast<int>(assumed.selection));

            auto hdr = std::make_shared<HDRData>();
            hdr->displayPrimaryRedX = 0.68; hdr->displayPrimaryRedY = 0.32;
            hdr->displayPrimaryGreenX = 0.265; hdr->displayPrimaryGreenY = 0.69;
            hdr->displayPrimaryBlueX = 0.15; hdr->displayPrimaryBlueY = 0.06;
            hdr->whitePointX = 0.3127; hdr->whitePointY = 0.329;
            hdr->masteringDisplayMaxLuminance = 1000;
            state.hdrData = hdr;
            auto declared = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(declared.selection));
            Assert::AreEqual(std::string("declared"), std::string(declared.outcome));

            hdr->masteringDisplayMaxLuminance = 0;
            auto forcedOn = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Cie),
                static_cast<int>(forcedOn.selection));
            Assert::AreEqual(hdr->displayPrimaryRedX, forcedOn.cie[0]);
            Assert::AreEqual(hdr->whitePointY, forcedOn.cie[7]);
            auto forcedOff = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, false);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(forcedOff.selection));

            state.eotf = EOTF::SDR;
            auto sdr = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(sdr.selection));
            Assert::AreEqual(std::string(""), std::string(sdr.outcome));
            state.eotf = EOTF::HLG;
            auto hlg = SourceMasteringPrimariesPolicy::Resolve(
                state, noDolby, false, true);
            Assert::AreEqual(std::string(""), std::string(hlg.outcome));
        }

        TEST_METHOD(DolbyL9PresetsAndCustomPrimariesResolveWithoutChangingDefaultOff)
        {
            VideoState state;
            state.eotf = EOTF::PQ;
            state.dolbyVisionTunnel = true;
            state.hdrData = std::make_shared<HDRData>();
            state.hdrData->primariesFilledByVp = true;
            DolbyVisionTunnelFrameMetadata dm;
            dm.valid = true;
            dm.hasSourcePrimaries = true;

            const double expected[9][8] = {
                {0.68,0.32,0.265,0.69,0.15,0.06,0.3127,0.329},
                {0.64,0.33,0.30,0.60,0.15,0.06,0.3127,0.329},
                {0.708,0.292,0.170,0.797,0.131,0.046,0.3127,0.329},
                {0.63,0.34,0.31,0.595,0.155,0.07,0.3127,0.329},
                {0.64,0.33,0.29,0.60,0.15,0.06,0.3127,0.329},
                {0.68,0.32,0.265,0.69,0.15,0.06,0.314,0.351},
                {0.7347,0.2653,0.0,1.0,0.0001,-0.077,0.32168,0.33767},
                {0.73,0.28,0.14,0.855,0.10,-0.05,0.3127,0.329},
                {0.766,0.275,0.225,0.80,0.089,-0.087,0.3127,0.329},
            };
            for (uint8_t index = 0; index <= 8; ++index)
            {
                dm.sourcePrimaryIndex = index;
                const auto off = SourceMasteringPrimariesPolicy::Resolve(
                    state, dm, false, false);
                Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                    static_cast<int>(off.selection));

                const auto on = SourceMasteringPrimariesPolicy::Resolve(
                    state, dm, false, true);
                const auto expectedSelection = index == 0 ?
                    SourceMasteringPrimariesPolicy::Selection::DisplayP3D65 :
                    index == 1 ? SourceMasteringPrimariesPolicy::Selection::Bt709 :
                    index == 2 ? SourceMasteringPrimariesPolicy::Selection::Bt2020 :
                    SourceMasteringPrimariesPolicy::Selection::Cie;
                Assert::AreEqual(static_cast<int>(expectedSelection),
                    static_cast<int>(on.selection));
                if (index >= 3)
                    for (size_t i = 0; i < 8; ++i)
                        Assert::AreEqual(expected[index][i], on.cie[i], 0.000001);
            }

            dm.sourcePrimaryIndex = 255;
            dm.hasCustomSourcePrimaries = true;
            const uint16_t custom[] = { 22282, 10485, 8683, 32768, 4915, 1966, 10240, 10780 };
            for (size_t i = 0; i < 8; ++i) dm.sourcePrimaryCoordinates[i] = custom[i];
            const auto customOn = SourceMasteringPrimariesPolicy::Resolve(
                state, dm, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Cie),
                static_cast<int>(customOn.selection));
            for (size_t i = 0; i < 8; ++i)
                Assert::AreEqual(static_cast<double>(custom[i]) / 32767.0,
                    customOn.cie[i], 0.000000001);
            Assert::IsTrue(customOn.cie[3] > 1.0);
            struct pl_raw_primaries extendedCustom{};
            extendedCustom.red = { static_cast<float>(customOn.cie[0]), static_cast<float>(customOn.cie[1]) };
            extendedCustom.green = { static_cast<float>(customOn.cie[2]), static_cast<float>(customOn.cie[3]) };
            extendedCustom.blue = { static_cast<float>(customOn.cie[4]), static_cast<float>(customOn.cie[5]) };
            extendedCustom.white = { static_cast<float>(customOn.cie[6]), static_cast<float>(customOn.cie[7]) };
            Assert::IsTrue(pl_primaries_valid(&extendedCustom),
                L"Valid extended chromaticity must survive decoding and libplacebo validation.");
            const auto customOff = SourceMasteringPrimariesPolicy::Resolve(
                state, dm, false, false);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(customOff.selection));

            dm.sourcePrimaryIndex = 9;
            dm.hasCustomSourcePrimaries = false;
            const auto unknown = SourceMasteringPrimariesPolicy::Resolve(
                state, dm, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(unknown.selection));
        }

        TEST_METHOD(DolbyL9PrecedenceUsesNoL9AssumptionOnlyWhenTheTunnelFillIsOurs)
        {
            VideoState state;
            state.eotf = EOTF::PQ;
            state.dolbyVisionTunnel = true;
            state.hdrData = std::make_shared<HDRData>();
            state.hdrData->primariesFilledByVp = true;
            DolbyVisionTunnelFrameMetadata noL9;
            noL9.valid = true;
            auto noL9On = SourceMasteringPrimariesPolicy::Resolve(
                state, noL9, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::DisplayP3D65),
                static_cast<int>(noL9On.selection));

            DolbyVisionTunnelFrameMetadata malformedL9;
            malformedL9.valid = true;
            malformedL9.sourcePrimariesMalformed = true;
            for (bool assume : { false, true })
            {
                const auto malformed = SourceMasteringPrimariesPolicy::Resolve(
                    state, malformedL9, false, assume);
                Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                    static_cast<int>(malformed.selection));
            }

            DolbyVisionTunnelFrameMetadata l9;
            l9.valid = true; l9.hasSourcePrimaries = true; l9.sourcePrimaryIndex = 1;
            state.hdrData->primariesFilledByVp = false;
            auto forced = SourceMasteringPrimariesPolicy::Resolve(
                state, l9, false, true);
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(forced.selection));
            state.hdrData->primariesFilledByVp = true;
            auto fallback = SourceMasteringPrimariesPolicy::Resolve(
                state, l9, true, true);
            Assert::AreEqual(std::string(""), std::string(fallback.outcome));
            Assert::AreEqual(static_cast<int>(SourceMasteringPrimariesPolicy::Selection::Keep),
                static_cast<int>(fallback.selection));
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
