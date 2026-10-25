#include "pch.h"
#include <ActivePictureEvidence.h>
#include "CppUnitTest.h"
#include <algorithm>
#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct BlackReferenceFrame
{
    const int width = 640, height = 360;
    const size_t pitch = 1280;
    std::vector<uint8_t> bytes, rgb;
    BlackReferenceFrame() : bytes(pitch * height * 2, 0)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) Pixel(x, y, 300);
        for (size_t i = pitch * height; i < bytes.size(); i += 2) Code(i, 512);
    }
    void Code(size_t offset, int value)
    {
        const uint16_t code = static_cast<uint16_t>(value << 6);
        bytes[offset] = static_cast<uint8_t>(code);
        bytes[offset + 1] = static_cast<uint8_t>(code >> 8);
    }
    void Pixel(int x, int y, int value) { Code(size_t(y) * pitch + x * 2, value); }
    void Rect(int left, int top, int right, int bottom, int value)
    {
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) Pixel(x, y, value);
    }
    void Bars(bool vertical, int floor, int noise = 0, bool aliasedNoise = false)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (vertical ? (y < 48 || y >= height - 48) : (x < 64 || x >= width - 64))
                {
                    const int delta = !noise ? 0 : aliasedNoise ?
                        (((x / 14 + y / 6) & 1) ? noise : -noise) :
                        ((x * 17 + y * 11) % (noise * 2 + 1)) - noise;
                    Pixel(x, y, std::max(0, floor + delta));
                }
    }
    AnalysisLumaSource Source(int format, AnalysisLumaRange range = AnalysisLumaRange::Full)
    {
        AnalysisLumaSource source = { bytes.data(), bytes.size(), width, height, pitch, pitch,
            AnalysisLumaFormat::P210, VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
        if (format == 0) source.format = AnalysisLumaFormat::P010;
        if (format == 2) source.format = AnalysisLumaFormat::DecodedDolbyVision422;
        if (format == 3)
        {
            rgb.assign(size_t(width) * height * 4, 255);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const size_t coded = size_t(y) * pitch + x * 2;
                    const int value = (bytes[coded] | (int(bytes[coded + 1]) << 8)) >> 6;
                    const auto gray = static_cast<uint8_t>((value * 255 + 511) / 1023);
                    const size_t pixel = (size_t(y) * width + x) * 4;
                    rgb[pixel] = rgb[pixel + 1] = rgb[pixel + 2] = gray;
                }
            source = { rgb.data(), rgb.size(), width, height, size_t(width) * 4, 0,
                AnalysisLumaFormat::NativeRgb, VideoFrameEncoding::BGRA_8BIT, ColorSpace::REC_709, 1 };
        }
        source.range = range;
        return source;
    }
    ActivePictureBounds Scope() const
    { return { 0,48,width,height-48,width,height,double(width)/(height-96),ActivePictureBounds::BarAxes::TOP_BOTTOM }; }
};
void AssertFull(const ActivePictureEvidence& result, int width = 640, int height = 360)
{
    Assert::AreEqual(int(ActivePictureClassification::FULL_RASTER_TRUSTED), int(result.classification));
    Assert::AreEqual(0, result.proposedBounds.left); Assert::AreEqual(0, result.proposedBounds.top);
    Assert::AreEqual(width, result.proposedBounds.right); Assert::AreEqual(height, result.proposedBounds.bottom);
    Assert::AreEqual(0, result.trustedBounds.left); Assert::AreEqual(0, result.trustedBounds.top);
    Assert::AreEqual(width, result.trustedBounds.right); Assert::AreEqual(height, result.trustedBounds.bottom);
    Assert::AreEqual(int(ActivePictureBounds::BarAxes::NONE), int(result.trustedBounds.trustedBarAxes));
    Assert::AreEqual(int(ActivePictureAxisState::FULL_EXTENT_SUPPORTED), int(result.axisEvidence.horizontal.state));
    Assert::AreEqual(int(ActivePictureAxisState::FULL_EXTENT_SUPPORTED), int(result.axisEvidence.vertical.state));
    Assert::AreEqual(0, result.blackReference.floor); Assert::AreEqual(24, result.blackReference.threshold);
}
}

TEST_CLASS(ActivePictureBlackReferenceTests)
{
public:
    TEST_METHOD(BudgetedMeasurementsPreserveRaisedMatteAuthorityAndRetentionReference)
    {
        for (int format = 0; format < 4; ++format)
            for (bool uncertain : { false, true })
            {
                BlackReferenceFrame frame;
                frame.Bars(true, 64, uncertain ? 6 : 0, uncertain);
                const auto source = frame.Source(format);
                const auto expected = ExtractActivePictureEvidence(source);
                const auto expectedRetention = EvaluateActivePicturePresentationRetention(source, frame.Scope());
                int checkpoints = 0;
                AnalysisEvaluationBudget budget([](void* context) {
                    ++*static_cast<int*>(context);
                    return false;
                }, &checkpoints);
                const auto raw = ExtractActivePictureEvidence(source, &budget);
                const auto global = EvaluateActivePictureGlobalNearBlack(source, &budget);
                const auto darkness = EvaluateActivePictureArDarkness(source, raw, global, &budget);
                const auto retention = EvaluateActivePicturePresentationRetention(
                    source, frame.Scope(), raw, global, darkness, &budget);
                Assert::IsTrue(checkpoints > 0);
                Assert::AreEqual(int(expected.classification), int(raw.classification));
                Assert::AreEqual(int(expected.axisEvidence.vertical.state), int(raw.axisEvidence.vertical.state));
                Assert::AreEqual(int(expected.blackReference.mode), int(raw.blackReference.mode));
                Assert::AreEqual(expected.blackReference.qualifiedEdgeMask, raw.blackReference.qualifiedEdgeMask);
                Assert::AreEqual(expected.blackReference.unresolvedEdgeMask, raw.blackReference.unresolvedEdgeMask);
                Assert::AreEqual(expectedRetention.presentationBlackFloor, retention.presentationBlackFloor);
                Assert::AreEqual(expectedRetention.presentationBlackThreshold, retention.presentationBlackThreshold);
                Assert::AreEqual(expectedRetention.excludedBandsPixelSafe, retention.excludedBandsPixelSafe);
                Assert::AreEqual(raw.blackReference.floor, retention.presentationBlackFloor);
                Assert::AreEqual(raw.blackReference.threshold, retention.presentationBlackThreshold);
                if (raw.blackReference.mode == ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED)
                    Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(raw.classification));
            }
    }
    TEST_METHOD(VariableDarkSideIsPictureInEveryNativeFormat)
    {
        for (int format = 0; format < 4; ++format)
            for (bool side : { false, true })
            {
                BlackReferenceFrame frame;
                // Every sample is genuine picture. The wider61..87 gradient
                // stays beyond the matte-noise envelope after RGB8 rounding;
                // exact captured DV pixels are exercised by native replay.
                // One dark side still raises the legacy pooled percentile.
                for (int y = 0; y < frame.height; ++y)
                    for (int x = 0; x < frame.width; ++x)
                        if (side ? x < 160 : y < 64)
                            frame.Pixel(x, y, 61 + (26 * (side ? y : x)) /
                                ((side ? frame.height : frame.width) - 1));
                const auto result = ExtractActivePictureEvidence(frame.Source(format));
                AssertFull(result);
                Assert::IsTrue(result.blackReference.nominalReclassified);
                Assert::AreEqual(3, int(result.blackReference.nominalFullExtentAxes));
                Assert::AreEqual(0, int(result.blackReference.qualifiedEdgeMask));
                Assert::AreEqual(0, int(result.blackReference.unresolvedEdgeMask));
                Assert::IsTrue(result.lumaSamples < 30000);
            }
    }
    TEST_METHOD(GradientInsideSupportedMatteNoiseEnvelopeStaysUncertain)
    {
        BlackReferenceFrame frame;
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < 160; ++x)
                frame.Pixel(x,y,61+(15*y)/(frame.height-1));
        const auto result = ExtractActivePictureEvidence(frame.Source(2));
        // These sampled flank medians span only twelve codes, which can also
        // be a legitimate raised matte with +/-6 row/block noise. Refuse new
        // authority instead of pretending that the two signals are distinct.
        Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(result.classification));
        Assert::IsTrue(result.blackReference.unresolvedEdgeMask != 0);
        Assert::IsFalse(result.blackReference.nominalReclassified);
    }
    TEST_METHOD(TrueZeroFortyAndSixtyFourBarsKeepBothOrientationsAndFormats)
    {
        for (int format = 0; format < 4; ++format)
            for (bool vertical : { false, true })
                for (int floor : { 0,40,64 })
                {
                    BlackReferenceFrame frame; frame.Bars(vertical, floor);
                    const auto result = ExtractActivePictureEvidence(frame.Source(format));
                    Assert::AreEqual(int(ActivePictureClassification::BAR_CROP_TRUSTED), int(result.classification));
                    Assert::AreEqual(floor, result.blackReference.floor);
                    Assert::IsFalse(result.blackReference.nominalReclassified);
                    if (floor) Assert::AreEqual(vertical ? 1 : 2, int(result.blackReference.qualifiedAxes));
                }
    }
    TEST_METHOD(RaisedMatteNoiseCannotTurnIntoFullRaster)
    {
        for (int format = 0; format < 4; ++format)
            for (bool vertical : { false, true })
                for (bool aliased : { false, true })
                {
                    BlackReferenceFrame frame; frame.Bars(vertical, 64, 6, aliased);
                    const auto result = ExtractActivePictureEvidence(frame.Source(format));
                    Assert::IsFalse(result.classification == ActivePictureClassification::FULL_RASTER_TRUSTED);
                    Assert::IsFalse(result.blackReference.nominalReclassified);
                    Assert::IsTrue(result.blackReference.qualifiedEdgeMask != 0 || result.blackReference.unresolvedEdgeMask != 0);
                }
    }
    TEST_METHOD(CentralCaptionDoesNotDestroyRaisedMatteReference)
    {
        for (int format = 0; format < 4; ++format)
            for (int floor : { 40,64 })
            {
                BlackReferenceFrame frame; frame.Bars(true, floor);
                frame.Rect(180, frame.height - 16, 460, frame.height, 700);
                const auto result = ExtractActivePictureEvidence(frame.Source(format));
                Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(result.classification));
                Assert::AreEqual(int(ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED), int(result.blackReference.mode));
                Assert::IsTrue((result.blackReference.qualifiedEdgeMask & 2) != 0);
                Assert::AreEqual(floor, result.blackReference.floor);
            }
    }
    TEST_METHOD(WholeOpposingEdgeControlLeavesOneMatteUnresolved)
    {
        for (bool vertical : { false,true })
        {
            BlackReferenceFrame frame; frame.Bars(vertical, 64);
            if (vertical) frame.Rect(0, frame.height - 16, frame.width, frame.height, 700);
            else frame.Rect(frame.width - 16, 0, frame.width, frame.height, 700);
            const auto result = ExtractActivePictureEvidence(frame.Source(2));
            Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(result.classification));
            Assert::IsFalse(result.blackReference.nominalReclassified);
            Assert::IsTrue(result.blackReference.qualifiedEdgeMask != 0);
        }
    }
    TEST_METHOD(OneSidedAndMismatchedMattesCannotNominateFullRaster)
    {
        for (bool opposing : { false,true })
        {
            BlackReferenceFrame frame; frame.Rect(0,0,64,frame.height,40);
            if (opposing) frame.Rect(frame.width-64,0,frame.width,frame.height,64);
            const auto result = ExtractActivePictureEvidence(frame.Source(2));
            Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(result.classification));
            Assert::IsFalse(result.blackReference.nominalReclassified);
            Assert::AreEqual(0, int(result.blackReference.qualifiedAxes));
            if (opposing)
            {
                Assert::AreEqual(int(ActivePictureAxisState::UNKNOWN),int(result.axisEvidence.horizontal.state));
                Assert::AreEqual(int(ActivePictureAxisReason::BAR_EDGE_REJECTED),int(result.axisEvidence.horizontal.reason));
            }
        }
    }
    TEST_METHOD(UnresolvedMattePreservesIndependentFullExtentAxis)
    {
        BlackReferenceFrame frame;
        frame.Rect(0,0,frame.width,32,64);
        frame.Rect(0,frame.height-32,frame.width,frame.height,64);
        frame.Rect(0,frame.height-12,frame.width,frame.height,700);
        const auto result = ExtractActivePictureEvidence(frame.Source(2));
        Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(result.classification));
        Assert::AreEqual(int(ActivePictureAxisState::FULL_EXTENT_SUPPORTED),int(result.axisEvidence.horizontal.state));
        Assert::AreEqual(int(ActivePictureAxisReason::FULL_EXTENT_SUPPORTED),int(result.axisEvidence.horizontal.reason));
        Assert::IsTrue(result.axisEvidence.horizontal.scanComplete);
        Assert::AreEqual(int(ActivePictureAxisState::UNKNOWN),int(result.axisEvidence.vertical.state));
        Assert::IsFalse(result.blackReference.nominalReclassified);
    }
    TEST_METHOD(RejectedAdaptiveCropCannotLeakIntoFullRasterCoordinates)
    {
        BlackReferenceFrame frame;
        // Opposing smooth low-code gradients pass the old loose bar profile,
        // but their full-depth references do not describe constant mattes.
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x)
                if (x < 64 || x >= frame.width - 64)
                    frame.Pixel(x,y,61+(18*y)/(frame.height-1));
        const auto result = ExtractActivePictureEvidence(frame.Source(2));
        AssertFull(result);
        Assert::IsTrue(result.blackReference.adaptiveCandidate.left > 0);
        Assert::IsTrue(result.blackReference.adaptiveCandidate.right < frame.width);
    }
    TEST_METHOD(RetentionReusesRejectedReferenceRatherThanRestoringPerimeterFloor)
    {
        BlackReferenceFrame frame;
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < 160; ++x)
                frame.Pixel(x,y,61+(18*y)/(frame.height-1));
        const auto result = EvaluateActivePicturePresentationRetention(frame.Source(2),frame.Scope());
        AssertFull(result.activePicture);
        Assert::AreEqual(0, result.presentationBlackFloor);
        Assert::AreEqual(24, result.presentationBlackThreshold);
        Assert::IsTrue(result.blackReference.nominalReclassified);
        Assert::IsFalse(result.excludedBandsPixelSafe);
    }
    TEST_METHOD(RaisedMatteRetentionDoesNotUseNominalZero)
    {
        for (int floor : {40,64})
        {
            BlackReferenceFrame frame; frame.Bars(true,floor);
            const auto result = EvaluateActivePicturePresentationRetention(frame.Source(2),frame.Scope());
            Assert::AreEqual(floor,result.presentationBlackFloor);
            Assert::AreEqual(floor+24,result.presentationBlackThreshold);
            Assert::IsTrue(result.excludedBandsPixelSafe);
            Assert::IsTrue(result.CanRetainPresentation());
        }
    }
    TEST_METHOD(BudgetedMeasurementsPreserveRaisedMatteAuthorityAndRetentionReference)
    {
        for (int format = 0; format < 4; ++format)
            for (bool uncertain : { false, true })
            {
                BlackReferenceFrame frame;
                frame.Bars(true, 64, uncertain ? 6 : 0, uncertain);
                const auto source = frame.Source(format);
                const auto expected = ExtractActivePictureEvidence(source);
                const auto expectedRetention = EvaluateActivePicturePresentationRetention(source, frame.Scope());
                int checkpoints = 0;
                AnalysisEvaluationBudget budget([](void* context) {
                    ++*static_cast<int*>(context);
                    return false;
                }, &checkpoints);
                const auto raw = ExtractActivePictureEvidence(source, &budget);
                const auto global = EvaluateActivePictureGlobalNearBlack(source, &budget);
                const auto darkness = EvaluateActivePictureArDarkness(source, raw, global, &budget);
                const auto retention = EvaluateActivePicturePresentationRetention(
                    source, frame.Scope(), raw, global, darkness, &budget);
                Assert::IsTrue(checkpoints > 0);
                Assert::AreEqual(int(expected.classification), int(raw.classification));
                Assert::AreEqual(int(expected.axisEvidence.vertical.state), int(raw.axisEvidence.vertical.state));
                Assert::AreEqual(int(expected.blackReference.mode), int(raw.blackReference.mode));
                Assert::AreEqual(expected.blackReference.qualifiedEdgeMask, raw.blackReference.qualifiedEdgeMask);
                Assert::AreEqual(expected.blackReference.unresolvedEdgeMask, raw.blackReference.unresolvedEdgeMask);
                Assert::AreEqual(expectedRetention.presentationBlackFloor, retention.presentationBlackFloor);
                Assert::AreEqual(expectedRetention.presentationBlackThreshold, retention.presentationBlackThreshold);
                Assert::AreEqual(expectedRetention.excludedBandsPixelSafe, retention.excludedBandsPixelSafe);
                Assert::AreEqual(raw.blackReference.floor, retention.presentationBlackFloor);
                Assert::AreEqual(raw.blackReference.threshold, retention.presentationBlackThreshold);
                if (raw.blackReference.mode == ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED)
                    Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL), int(raw.classification));
            }
    }
    TEST_METHOD(LimitedAndUnknownKeepExistingAdaptiveInterpretation)
    {
        for (auto range : {AnalysisLumaRange::Limited,AnalysisLumaRange::Unknown})
        {
            BlackReferenceFrame frame;
            for (int y = 0; y < frame.height; ++y)
                for (int x = 0; x < 160; ++x)
                    frame.Pixel(x,y,61+(18*y)/(frame.height-1));
            const auto result = ExtractActivePictureEvidence(frame.Source(2,range));
            Assert::AreEqual(int(ActivePictureBlackReferenceMode::LEGACY),int(result.blackReference.mode));
            Assert::IsFalse(result.classification == ActivePictureClassification::FULL_RASTER_TRUSTED);
            Assert::IsFalse(result.blackReference.nominalReclassified);
        }
    }
    TEST_METHOD(FullBlackFadeAndSparseTitlesNeverAcquireFullRaster)
    {
        for (int floor : {0,24,37,64})
        {
            BlackReferenceFrame frame; frame.Rect(0,0,frame.width,frame.height,floor);
            frame.Rect(200,160,440,164,700);
            const auto result = ExtractActivePictureEvidence(frame.Source(2));
            Assert::IsFalse(result.classification == ActivePictureClassification::FULL_RASTER_TRUSTED);
            Assert::IsFalse(result.blackReference.nominalReclassified);
        }
    }
    TEST_METHOD(ModeNamesRemainStableForTelemetry)
    {
        Assert::AreEqual("legacy", ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode::LEGACY));
        Assert::AreEqual("full-nominal", ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode::FULL_NOMINAL));
        Assert::AreEqual("full-qualified-raised", ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode::FULL_QUALIFIED_RAISED));
        Assert::AreEqual("full-unresolved-raised", ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED));
        Assert::AreEqual("full-rejected-raised", ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode::FULL_REJECTED_RAISED));
    }
};
}
