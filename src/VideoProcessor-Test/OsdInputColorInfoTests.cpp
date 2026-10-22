#include "pch.h"
#include "CppUnitTest.h"
#include <OsdInputColorInfo.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
TEST_CLASS(OsdInputColorInfoTests)
{
    static void Set(VideoState& state, EOTF eotf, ColorSpace primaries,
        VideoFrameEncoding encoding = VideoFrameEncoding::V210)
    {
        state.valid = true; state.eotf = eotf; state.colorspace = primaries;
        state.videoFrameEncoding = encoding;
    }
public:
    TEST_METHOD(DolbyVisionTunnelUsesAcceptedPqPrimariesAndKeepsWireObservation)
    {
        VideoState wire, decoded;
        Set(wire, EOTF::SDR, ColorSpace::REC_709, VideoFrameEncoding::R210);
        Set(decoded, EOTF::PQ, ColorSpace::BT_2020, VideoFrameEncoding::R210);
        decoded.dolbyVisionTunnel = true;
        OsdInputColorInfo::AcceptedState policy; policy.Publish(&decoded, true);
        const auto text = policy.Describe(&wire, true);
        Assert::AreEqual(L"PQ (ST2084) (wire SDR)", text.eotf.c_str());
        const auto expected = std::wstring(ToString(ColorSpace::BT_2020)) +
            L" (wire " + ToString(ColorSpace::REC_709) + L")";
        Assert::AreEqual(expected.c_str(), text.colorspace.c_str());
    }
    TEST_METHOD(LldvSdrToPqRewriteDoesNotInventAColorspaceDifference)
    {
        VideoState wire, effective;
        Set(wire, EOTF::SDR, ColorSpace::BT_2020);
        Set(effective, EOTF::PQ, ColorSpace::BT_2020);
        OsdInputColorInfo::AcceptedState policy; policy.Publish(&effective, true);
        const auto text = policy.Describe(&wire, true);
        Assert::AreEqual(L"PQ (ST2084) (wire SDR)", text.eotf.c_str());
        Assert::AreEqual(ToString(ColorSpace::BT_2020), text.colorspace.c_str());
    }
    TEST_METHOD(ContainerOrForcedPrimariesComeFromAcceptedStateForEveryPixelFormat)
    {
        for (const auto encoding : {VideoFrameEncoding::R210, VideoFrameEncoding::V210,
            VideoFrameEncoding::BGRA_8BIT, VideoFrameEncoding::H265})
        {
            VideoState wire, effective;
            Set(wire, EOTF::PQ, ColorSpace::BT_2020, encoding);
            Set(effective, EOTF::PQ, ColorSpace::P3_D65, encoding);
            OsdInputColorInfo::AcceptedState policy; policy.Publish(&effective, true);
            const auto text = policy.Describe(&wire, true);
            Assert::AreEqual(ToString(EOTF::PQ), text.eotf.c_str());
            const auto expected = std::wstring(ToString(ColorSpace::P3_D65)) +
                L" (wire " + ToString(ColorSpace::BT_2020) + L")";
            Assert::AreEqual(expected.c_str(), text.colorspace.c_str());
        }
    }
    TEST_METHOD(EqualAcceptedStateOmitsWireSuffixAndCopiesValuesAtAcceptance)
    {
        VideoState wire, effective;
        Set(wire, EOTF::HLG, ColorSpace::BT_2020);
        Set(effective, EOTF::HLG, ColorSpace::BT_2020);
        OsdInputColorInfo::AcceptedState policy; policy.Publish(&effective, true);
        effective.eotf = EOTF::PQ; // a candidate mutation cannot alter publication
        const auto text = policy.Describe(&wire, true);
        Assert::AreEqual(L"HLG", text.eotf.c_str());
        Assert::AreEqual(ToString(ColorSpace::BT_2020), text.colorspace.c_str());
    }
    TEST_METHOD(RejectedOrInvalidCandidateCannotAppearAsRendererInput)
    {
        VideoState wire, effective;
        Set(wire, EOTF::SDR, ColorSpace::REC_709);
        Set(effective, EOTF::PQ, ColorSpace::BT_2020);
        OsdInputColorInfo::AcceptedState policy;
        policy.Publish(&wire, true); policy.Publish(&effective, false);
        Assert::AreEqual(L"--- (wire SDR)", policy.Describe(&wire, true).eotf.c_str());
        effective.valid = false; policy.Publish(&effective, true);
        Assert::AreEqual(L"--- (wire SDR)", policy.Describe(&wire, true).eotf.c_str());
        policy.Publish(nullptr, true);
        Assert::AreEqual(L"--- (wire SDR)", policy.Describe(&wire, true).eotf.c_str());
    }
    TEST_METHOD(OlderPluginStaticHdrFallbackUsesCopiedAcceptedMetadata)
    {
        VideoState accepted;
        Set(accepted, EOTF::PQ, ColorSpace::BT_2020);
        accepted.hdrData = std::make_shared<HDRData>();
        accepted.hdrData->maxCll = 1000; accepted.hdrData->maxFall = 400;
        OsdInputColorInfo::AcceptedState policy; policy.Publish(&accepted, true);
        accepted.hdrData->maxCll = 4000; // next candidate must not mutate fallback
        const auto* fallback = policy.Hdr(true); // optional plugin HDR export unavailable
        Assert::IsNotNull(fallback); Assert::AreEqual(1000.0, fallback->maxCll);
        Assert::AreEqual(400.0, fallback->maxFall);
        Assert::IsNull(policy.Hdr(false));
        policy.Reset(); Assert::IsNull(policy.Hdr(true));
    }
    TEST_METHOD(RejectedInvalidOrSdrCandidateNeverSuppliesStaticHdrFallback)
    {
        VideoState candidate;
        Set(candidate, EOTF::PQ, ColorSpace::BT_2020);
        candidate.hdrData = std::make_shared<HDRData>(); candidate.hdrData->maxCll = 4000;
        OsdInputColorInfo::AcceptedState policy;
        policy.Publish(&candidate, true); Assert::IsNotNull(policy.Hdr(true));
        policy.Publish(&candidate, false); Assert::IsNull(policy.Hdr(true));
        candidate.valid = false; policy.Publish(&candidate, true); Assert::IsNull(policy.Hdr(true));
        candidate.valid = true; candidate.eotf = EOTF::SDR;
        policy.Publish(&candidate, true); Assert::IsNull(policy.Hdr(true));
        candidate.eotf = EOTF::PQ; candidate.hdrData.reset();
        policy.Publish(&candidate, true); Assert::IsNull(policy.Hdr(true));
    }
    TEST_METHOD(StoppingReplacementAndMissingCaptureFailClosedUntilNextAcceptance)
    {
        VideoState wire, effective;
        Set(wire, EOTF::SDR, ColorSpace::REC_709);
        Set(effective, EOTF::PQ, ColorSpace::BT_2020);
        OsdInputColorInfo::AcceptedState policy; policy.Publish(&effective, true);
        Assert::AreEqual(L"--- (wire SDR)", policy.Describe(&wire, false).eotf.c_str());
        policy.Reset();
        Assert::AreEqual(L"--- (wire SDR)", policy.Describe(&wire, true).eotf.c_str());
        policy.Publish(&effective, true);
        Assert::AreEqual(L"PQ (ST2084) (wire SDR)", policy.Describe(&wire, true).eotf.c_str());
        Assert::AreEqual(L"---", policy.Describe(nullptr, true).eotf.c_str());
        wire.valid = false;
        Assert::AreEqual(L"---", policy.Describe(&wire, true).colorspace.c_str());
    }
};
}
