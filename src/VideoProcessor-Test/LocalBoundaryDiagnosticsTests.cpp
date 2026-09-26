#include "pch.h"

#include <ActivePictureEvidence.h>
#include <LocalBoundaryDiagnostics.h>
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
void WriteBoundaryCode(uint8_t* target, int code)
{
    const uint16_t packed = static_cast<uint16_t>(code << 6);
    target[0] = static_cast<uint8_t>(packed);
    target[1] = static_cast<uint8_t>(packed >> 8);
}

// Raw source pixels, with even rectangle coordinates so equivalent 4:2:0,
// 4:2:2 planar and packed-v210 representations retain identical information.
struct BoundaryFrame
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;

    BoundaryFrame(int w = 384, int h = 216, bool fullHeightChroma = true)
        : width(w), height(h), p210(fullHeightChroma), pitch(size_t(w) * 2 + 16),
          bytes(pitch * (h + (fullHeightChroma ? h : h / 2)), 0)
    {
        Rectangle(0, 0, width, height, 64, 512, 512);
    }

    void Rectangle(int left, int top, int right, int bottom,
        int y, int u = 512, int v = 512)
    {
        for (int row = top; row < bottom; ++row)
            for (int x = left; x < right; ++x)
                WriteBoundaryCode(bytes.data() + size_t(row) * pitch + size_t(x) * 2, y);
        for (int row = p210 ? top : top / 2; row < (p210 ? bottom : (bottom + 1) / 2); ++row)
            for (int x = left & ~1; x < right; x += 2)
            {
                auto* pixel = bytes.data() + pitch * height + size_t(row) * pitch + size_t(x) * 2;
                WriteBoundaryCode(pixel, u);
                WriteBoundaryCode(pixel + 2, v);
            }
    }

    void LocalPicture(int left = 90, int right = 114, int top = 28, int bottom = 188)
    {
        Rectangle(left, top, right, bottom, 144, 485, 528);
        // Additional top support does not invent an independent bottom region.
        Rectangle(192, top, 342, top + 8, 144, 485, 528);
    }

    AnalysisLumaSource Source() const
    {
        return { bytes.data(), bytes.size(), width, height, pitch, pitch,
            p210 ? AnalysisLumaFormat::P210 : AnalysisLumaFormat::P010,
            VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
    }
};

struct PackedBoundaryFrame
{
    int width, height;
    size_t pitch;
    std::vector<uint8_t> bytes;

    explicit PackedBoundaryFrame(const AnalysisLumaSource& source)
        : width(source.width), height(source.height), pitch(size_t(width / 6) * 16 + 16),
          bytes(pitch * height, 0)
    {
        Assert::AreEqual(0, width % 6);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; x += 6)
            {
                AnalysisLumaSample p[6];
                for (int i = 0; i < 6; ++i)
                    Assert::IsTrue(source.Sample(x + i, y, p[i]));
                const uint32_t words[] = {
                    uint32_t(p[0].chromaU) | uint32_t(p[0].luma) << 10 | uint32_t(p[0].chromaV) << 20,
                    uint32_t(p[1].luma) | uint32_t(p[2].chromaU) << 10 | uint32_t(p[2].luma) << 20,
                    uint32_t(p[2].chromaV) | uint32_t(p[3].luma) << 10 | uint32_t(p[4].chromaU) << 20,
                    uint32_t(p[4].luma) | uint32_t(p[4].chromaV) << 10 | uint32_t(p[5].luma) << 20 };
                auto* target = bytes.data() + size_t(y) * pitch + size_t(x / 6) * 16;
                for (int word = 0; word < 4; ++word)
                    for (int b = 0; b < 4; ++b)
                        target[word * 4 + b] = static_cast<uint8_t>(words[word] >> (b * 8));
            }
    }

    AnalysisLumaSource Source() const
    {
        return { bytes.data(), bytes.size(), width, height, pitch, 0,
            AnalysisLumaFormat::NativeYuv422, VideoFrameEncoding::V210, ColorSpace::REC_709, 1 };
    }
};

void AssertSameExistingEvidence(const ActivePictureEvidence& before, const ActivePictureEvidence& after)
{
    Assert::AreEqual(before.available, after.available);
    Assert::AreEqual(int(before.classification), int(after.classification));
    Assert::AreEqual(before.proposedBounds.left, after.proposedBounds.left);
    Assert::AreEqual(before.proposedBounds.top, after.proposedBounds.top);
    Assert::AreEqual(before.proposedBounds.right, after.proposedBounds.right);
    Assert::AreEqual(before.proposedBounds.bottom, after.proposedBounds.bottom);
    Assert::AreEqual(before.trustedBounds.left, after.trustedBounds.left);
    Assert::AreEqual(before.trustedBounds.top, after.trustedBounds.top);
    Assert::AreEqual(before.trustedBounds.right, after.trustedBounds.right);
    Assert::AreEqual(before.trustedBounds.bottom, after.trustedBounds.bottom);
    Assert::AreEqual(int(before.trustedBounds.trustedBarAxes), int(after.trustedBounds.trustedBarAxes));
    Assert::AreEqual(before.lumaSamples, after.lumaSamples);
    Assert::AreEqual(before.chromaSamples, after.chromaSamples);
    Assert::IsTrue(before.reason == after.reason);
}

} // namespace

TEST_CLASS(LocalBoundaryDiagnosticsTests)
{
public:
    TEST_METHOD(SourcePixelsExposeLocalOpposingRunsWithoutPolicyAuthority)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
        Assert::IsTrue(result.available && result.candidateAvailable);
        Assert::IsTrue(result.ambiguous);
        Assert::IsTrue(result.sourcePrecisionKnown && result.precisionSupported);
        Assert::AreEqual(28, result.top);
        Assert::AreEqual(188, result.bottom);
        Assert::AreEqual(2, result.topEdge.connectedRuns);
        Assert::AreEqual(1, result.bottomEdge.connectedRuns);
        Assert::IsTrue(result.bottomEdge.supportedSamples >= 3);
        Assert::IsTrue(result.bottomEdge.supportedSamples < 12);
        Assert::IsTrue(result.sampledBarsClean);
        Assert::AreEqual(size_t(0), result.excludedViolations);
        Assert::IsTrue(result.topEdge.inwardContinuity > 0.99);
        Assert::IsTrue(result.bottomEdge.inwardContinuity > 0.99);
        Assert::IsTrue(result.sampleCount > 0 && result.sampleCount <= LOCAL_BOUNDARY_MAX_SAMPLES);
        static_assert(!std::is_convertible<LocalBoundaryDiagnosticResult, ActivePictureEvidence>::value,
            "Diagnostic measurements must not become acquisition evidence by implicit conversion.");
        static_assert(!std::is_convertible<LocalBoundaryDiagnosticResult, ActivePictureBounds>::value,
            "A diagnostic interval is not a crop geometry certificate.");
    }

    TEST_METHOD(EquivalentP010P210AndNativeV210ProduceTheSameMeasurements)
    {
        BoundaryFrame p010(384, 216, false), p210;
        p010.LocalPicture();
        p210.LocalPicture();
        PackedBoundaryFrame native(p210.Source());
        const auto reference = SampleLocalBoundaryDiagnostics(p210.Source());
        for (const auto& source : {p010.Source(), native.Source()})
        {
            const auto result = SampleLocalBoundaryDiagnostics(source);
            Assert::IsTrue(result.available && result.candidateAvailable);
            Assert::AreEqual(reference.top, result.top);
            Assert::AreEqual(reference.bottom, result.bottom);
            Assert::AreEqual(reference.fingerprint, result.fingerprint);
            Assert::AreEqual(reference.sampleCount, result.sampleCount);
            Assert::AreEqual(reference.topEdge.supportedSamples, result.topEdge.supportedSamples);
            Assert::AreEqual(reference.bottomEdge.supportedSamples, result.bottomEdge.supportedSamples);
            Assert::IsTrue(reference.topEdge.supportMask == result.topEdge.supportMask);
            Assert::IsTrue(reference.bottomEdge.supportMask == result.bottomEdge.supportMask);
            Assert::AreEqual(reference.topEdge.maxLumaDelta, result.topEdge.maxLumaDelta);
            Assert::AreEqual(reference.bottomEdge.maxChromaDelta, result.bottomEdge.maxChromaDelta);
        }
    }

    TEST_METHOD(LumaAndColorWitnessesAreReportedSeparately)
    {
        BoundaryFrame neutral, colored;
        neutral.Rectangle(90, 28, 114, 188, 144);
        colored.Rectangle(90, 28, 114, 188, 64, 485, 528);
        const auto luma = SampleLocalBoundaryDiagnostics(neutral.Source());
        const auto color = SampleLocalBoundaryDiagnostics(colored.Source());
        Assert::IsTrue(luma.candidateAvailable && color.candidateAvailable);
        Assert::IsTrue(luma.topEdge.lumaOnlySamples >= 3);
        Assert::AreEqual(0, luma.topEdge.colorOnlySamples);
        Assert::AreEqual(0, luma.topEdge.lumaAndColorSamples);
        Assert::IsTrue(color.topEdge.colorOnlySamples >= 3);
        Assert::AreEqual(0, color.topEdge.lumaOnlySamples);
        Assert::AreEqual(0, color.topEdge.lumaAndColorSamples);
        Assert::IsTrue(luma.ambiguous && color.ambiguous);
    }

    TEST_METHOD(AllBlackAndInvalidSourcesCannotProduceAUsefulPair)
    {
        BoundaryFrame black(3840, 2160);
        const auto measured = SampleLocalBoundaryDiagnostics(black.Source());
        Assert::IsTrue(measured.available);
        Assert::IsFalse(measured.candidateAvailable);
        Assert::IsTrue(measured.sampleCount > 0 && measured.sampleCount <= size_t(128 * 270));
        auto invalid = black.Source();
        invalid.dataBytes = 1;
        const auto rejected = SampleLocalBoundaryDiagnostics(invalid);
        Assert::IsFalse(rejected.available);
        Assert::IsFalse(rejected.candidateAvailable);
        Assert::AreEqual(size_t(0), rejected.sampleCount);
        Assert::IsFalse(SampleLocalBoundaryDiagnostics({}).available);
        ActivePictureDiagnosticGrid malformed;
        malformed.columns = 128;
        malformed.rows = 270;
        malformed.samples.resize(1);
        Assert::IsFalse(AnalyzeLocalBoundaryGrid(malformed, 3840, 2160).available);
    }

    TEST_METHOD(ThinBrightLineDoesNotClaimInwardPersistence)
    {
        BoundaryFrame frame;
        frame.Rectangle(90, 28, 114, 30, 144, 485, 528);
        frame.Rectangle(90, 186, 114, 188, 144, 485, 528);
        const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
        Assert::IsTrue(result.candidateAvailable);
        Assert::IsTrue(result.topEdge.inwardContinuity < 0.5);
        Assert::IsTrue(result.bottomEdge.inwardContinuity < 0.5);
        Assert::IsTrue(result.ambiguous);
    }

    TEST_METHOD(NarrowExteriorLumaOrChromaObstructionInvalidatesSampledBars)
    {
        for (bool chromaOnly : {false, true})
        {
            BoundaryFrame frame;
            frame.LocalPicture();
            // Only two grid columns: too short to redefine the candidate edge.
            frame.Rectangle(0, 8, 6, 10, chromaOnly ? 64 : 144,
                chromaOnly ? 400 : 512, chromaOnly ? 620 : 512);
            const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
            Assert::IsTrue(result.candidateAvailable);
            Assert::AreEqual(28, result.top);
            Assert::AreEqual(188, result.bottom);
            Assert::IsFalse(result.sampledBarsClean);
            Assert::IsTrue(result.excludedViolations > 0);
            Assert::IsTrue(result.topExcludedViolations > 0);
            Assert::AreEqual(size_t(0), result.bottomExcludedViolations);
        }
    }

    TEST_METHOD(DiagnosticsDoNotMutateSourcePixelsOrExistingDetectorDecisions)
    {
        for (int fixture = 0; fixture < 4; ++fixture)
        {
            BoundaryFrame frame;
            if (fixture == 1) frame.LocalPicture();
            if (fixture == 2) frame.Rectangle(0, 28, 384, 188, 300);
            if (fixture == 3)
            {
                frame.LocalPicture();
                frame.Rectangle(0, 8, 6, 10, 900);
            }
            const auto originalBytes = frame.bytes;
            const auto before = ExtractActivePictureEvidence(frame.Source());
            const auto nearBlackBefore = EvaluateActivePictureGlobalNearBlack(frame.Source());
            LocalBoundaryDiagnosticHistory history;
            LocalBoundaryDiagnosticContext context;
            for (uint64_t sequence = 1; sequence <= 4; ++sequence)
            {
                context.acceptedSequence = sequence;
                context.timestampMs = sequence * 100;
                const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
                const auto summary = history.Observe(result, context);
                Assert::IsTrue(result.ambiguous && summary.ambiguous);
            }
            const auto after = ExtractActivePictureEvidence(frame.Source());
            const auto nearBlackAfter = EvaluateActivePictureGlobalNearBlack(frame.Source());
            Assert::IsTrue(originalBytes == frame.bytes);
            AssertSameExistingEvidence(before, after);
            Assert::AreEqual(nearBlackBefore.nearBlack, nearBlackAfter.nearBlack);
            Assert::AreEqual(int(ConstrainNearBlackCropAcquisition(before, nearBlackBefore.nearBlack).classification),
                int(ConstrainNearBlackCropAcquisition(after, nearBlackAfter.nearBlack).classification));
        }
    }

    TEST_METHOD(MovingYellowObjectPoolsCoverageButRemainsExplicitlyAmbiguous)
    {
        // Full-raster art on an intentional black background is the negative
        // semantic interpretation. These pixels also resemble fixed bars.
        // Preserve the ambiguity; do not tune the detector to reject this fixture.
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        LocalBoundaryDiagnosticHistorySummary summary;
        size_t firstBottomSupport = 0;
        for (int index = 0; index < 6; ++index)
        {
            BoundaryFrame frame;
            frame.LocalPicture(18 + index * 54, 42 + index * 54);
            const auto before = ExtractActivePictureEvidence(frame.Source());
            const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
            Assert::IsTrue(result.candidateAvailable && result.ambiguous);
            Assert::AreEqual(1, result.bottomEdge.connectedRuns);
            if (index == 0) firstBottomSupport = result.bottomEdge.supportMask.count();
            context.acceptedSequence = index + 1;
            context.timestampMs = 100 + index * 100;
            summary = history.Observe(result, context);
            Assert::IsTrue(summary.available && summary.ambiguous);
            AssertSameExistingEvidence(before, ExtractActivePictureEvidence(frame.Source()));
        }
        Assert::AreEqual(size_t(6), summary.distinctCount);
        Assert::IsTrue(summary.bottomUnionSamples > firstBottomSupport * 4);
        Assert::AreEqual(28, summary.anchorTop);
        Assert::AreEqual(188, summary.anchorBottom);
    }

    TEST_METHOD(RepeatedPictureFingerprintsNeverBecomeDistinctEvidence)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        for (uint64_t sequence = 1; sequence <= 20; ++sequence)
        {
            context.acceptedSequence = sequence;
            context.timestampMs = sequence * 100;
            const auto summary = history.Observe(result, context);
            Assert::IsTrue(summary.available && summary.ambiguous);
            Assert::AreEqual(size_t(1), summary.distinctCount);
            Assert::AreEqual(result.bottomEdge.supportMask.count(), summary.bottomUnionSamples);
        }
        context.timestampMs += 100;
        const auto repeatedSequence = history.Observe(result, context);
        Assert::AreEqual(size_t(20), repeatedSequence.frameCount);
        Assert::AreEqual(size_t(1), repeatedSequence.distinctCount);
    }

    TEST_METHOD(CurrentObstructionClearsHistoryAndCleanRecoveryStartsFresh)
    {
        BoundaryFrame first, second, obstructed;
        first.LocalPicture(18, 42);
        second.LocalPicture(90, 114);
        obstructed.LocalPicture(162, 186);
        obstructed.Rectangle(0, 8, 6, 10, 900);
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        context.acceptedSequence = 1; context.timestampMs = 100;
        history.Observe(SampleLocalBoundaryDiagnostics(first.Source()), context);
        context.acceptedSequence = 2; context.timestampMs = 200;
        Assert::AreEqual(size_t(2), history.Observe(SampleLocalBoundaryDiagnostics(second.Source()), context).distinctCount);
        context.acceptedSequence = 3; context.timestampMs = 300;
        const auto blocked = history.Observe(SampleLocalBoundaryDiagnostics(obstructed.Source()), context);
        Assert::IsTrue(blocked.reset);
        Assert::IsFalse(blocked.available);
        Assert::AreEqual(size_t(0), blocked.frameCount);
        context.acceptedSequence = 4; context.timestampMs = 400;
        const auto resumed = history.Observe(SampleLocalBoundaryDiagnostics(second.Source()), context);
        Assert::AreEqual(size_t(1), resumed.distinctCount);
        Assert::AreEqual(size_t(1), resumed.frameCount);
    }

    TEST_METHOD(EveryContextGenerationInvalidatesOldSpatialCoverage)
    {
        using Generation = uint64_t LocalBoundaryDiagnosticContext::*;
        const Generation fields[] = {
            &LocalBoundaryDiagnosticContext::sourceGeneration,
            &LocalBoundaryDiagnosticContext::rendererGeneration,
            &LocalBoundaryDiagnosticContext::viewportGeneration,
            &LocalBoundaryDiagnosticContext::sourceFormatGeneration,
            &LocalBoundaryDiagnosticContext::policyGeneration,
            &LocalBoundaryDiagnosticContext::continuityGeneration };
        BoundaryFrame first, second;
        first.LocalPicture(18, 42); second.LocalPicture(90, 114);
        const auto a = SampleLocalBoundaryDiagnostics(first.Source());
        const auto b = SampleLocalBoundaryDiagnostics(second.Source());
        for (Generation field : fields)
        {
            LocalBoundaryDiagnosticHistory history;
            LocalBoundaryDiagnosticContext context;
            context.acceptedSequence = 1; context.timestampMs = 100;
            history.Observe(a, context);
            context.*field += 1;
            context.acceptedSequence = 2; context.timestampMs = 200;
            const auto summary = history.Observe(b, context);
            Assert::IsTrue(summary.reset);
            Assert::AreEqual(size_t(1), summary.distinctCount);
            Assert::AreEqual(b.bottomEdge.supportMask.count(), summary.bottomUnionSamples);
            Assert::IsTrue(summary.bottomMask == b.bottomEdge.supportMask);
        }
    }

    TEST_METHOD(TimeRollbackSequenceRollbackAndObservationGapDiscardHistory)
    {
        BoundaryFrame first, second;
        first.LocalPicture(18, 42); second.LocalPicture(90, 114);
        const auto a = SampleLocalBoundaryDiagnostics(first.Source());
        const auto b = SampleLocalBoundaryDiagnostics(second.Source());
        for (int discontinuity = 0; discontinuity < 3; ++discontinuity)
        {
            LocalBoundaryDiagnosticHistory history;
            LocalBoundaryDiagnosticContext context;
            context.acceptedSequence = 10; context.timestampMs = 1000;
            history.Observe(a, context);
            context.acceptedSequence = discontinuity == 1 ? 9 : 11;
            context.timestampMs = discontinuity == 0 ? 999 : discontinuity == 2 ? 1501 : 1100;
            const auto summary = history.Observe(b, context);
            Assert::IsTrue(summary.reset);
            Assert::AreEqual(size_t(1), summary.distinctCount);
            Assert::IsTrue(summary.bottomMask == b.bottomEdge.supportMask);
        }
    }

    TEST_METHOD(HistoryRetainsAtMost32ObservationsAndOnlyThreeSeconds)
    {
        BoundaryFrame first, later;
        first.LocalPicture(18, 42); later.LocalPicture(90, 114);
        const auto a = SampleLocalBoundaryDiagnostics(first.Source());
        auto b = SampleLocalBoundaryDiagnostics(later.Source());
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        context.acceptedSequence = 1; context.timestampMs = 100;
        history.Observe(a, context);
        LocalBoundaryDiagnosticHistorySummary summary;
        // Narrow observation cadence hits the count bound before the age bound.
        for (uint64_t sequence = 2; sequence <= 40; ++sequence)
        {
            context.acceptedSequence = sequence;
            context.timestampMs = 100 + sequence * 25;
            summary = history.Observe(b, context);
            Assert::IsTrue(summary.frameCount <= size_t(32));
            Assert::IsTrue(summary.windowAgeMs <= uint64_t(3000));
        }
        Assert::AreEqual(size_t(32), summary.frameCount);
        Assert::IsTrue(summary.bottomMask == b.bottomEdge.supportMask);
        // Lower cadence hits the time bound without exceeding the count bound.
        history.Reset();
        context.acceptedSequence = 1; context.timestampMs = 100;
        history.Observe(a, context);
        for (uint64_t sequence = 2; sequence <= 12; ++sequence)
        {
            context.acceptedSequence = sequence;
            context.timestampMs = 100 + (sequence - 1) * 300;
            summary = history.Observe(b, context);
            Assert::IsTrue(summary.windowAgeMs <= uint64_t(3000));
        }
        Assert::IsTrue(summary.bottomMask == b.bottomEdge.supportMask);
        Assert::AreEqual(size_t(1), summary.distinctCount);
    }

    TEST_METHOD(SmallBoundaryChangesCannotWalkTheOriginalAnchor)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        auto result = SampleLocalBoundaryDiagnostics(frame.Source());
        Assert::AreEqual(1, result.rowStep);
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        context.acceptedSequence = 1; context.timestampMs = 100;
        const auto original = history.Observe(result, context);
        result.top += 1; result.bottom -= 1; result.fingerprint += 1;
        context.acceptedSequence = 2; context.timestampMs = 200;
        const auto jitter = history.Observe(result, context);
        Assert::AreEqual(original.anchorTop, jitter.anchorTop);
        Assert::AreEqual(original.anchorBottom, jitter.anchorBottom);
        result.top += 1; result.bottom -= 1; result.fingerprint += 1;
        context.acceptedSequence = 3; context.timestampMs = 300;
        const auto moved = history.Observe(result, context);
        Assert::IsTrue(moved.reset);
        Assert::AreEqual(size_t(1), moved.distinctCount);
        Assert::AreEqual(result.top, moved.anchorTop);
        Assert::AreEqual(result.bottom, moved.anchorBottom);
    }

    TEST_METHOD(RealExpansionAndGradualAspectRampsCannotReuseScopeHistory)
    {
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        for (int index = 0; index < 5; ++index)
        {
            BoundaryFrame frame;
            const int inset = 28 - index * 4;
            frame.LocalPicture(90, 114, inset, 216 - inset);
            context.acceptedSequence = index + 1;
            context.timestampMs = 100 + index * 100;
            const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
            Assert::IsTrue(result.candidateAvailable);
            const auto summary = history.Observe(result, context);
            Assert::AreEqual(size_t(1), summary.distinctCount);
            Assert::AreEqual(result.top, summary.anchorTop);
            Assert::AreEqual(result.bottom, summary.anchorBottom);
            if (index != 0) Assert::IsTrue(summary.reset);
        }
        BoundaryFrame imax;
        imax.Rectangle(0, 8, 384, 208, 300);
        context.acceptedSequence = 6; context.timestampMs = 600;
        const auto result = SampleLocalBoundaryDiagnostics(imax.Source());
        const auto summary = history.Observe(result, context);
        Assert::IsTrue(summary.reset);
        Assert::AreEqual(size_t(1), summary.distinctCount);
        Assert::AreEqual(8, summary.anchorTop);
        Assert::AreEqual(208, summary.anchorBottom);
    }

    TEST_METHOD(InvalidOrMissingCandidateAndGridChangesCannotCarryHistory)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        const auto valid = SampleLocalBoundaryDiagnostics(frame.Source());
        for (int change = 0; change < 4; ++change)
        {
            LocalBoundaryDiagnosticHistory history;
            LocalBoundaryDiagnosticContext context;
            context.acceptedSequence = 1; context.timestampMs = 100;
            history.Observe(valid, context);
            auto changed = valid;
            if (change == 0) changed.available = false;
            if (change == 1) changed.candidateAvailable = false;
            if (change == 2) changed.width += 2;
            if (change == 3) changed.rows -= 1;
            context.acceptedSequence = 2; context.timestampMs = 200;
            const auto summary = history.Observe(changed, context);
            Assert::IsTrue(summary.reset);
            Assert::AreEqual(change < 2 ? size_t(0) : size_t(1), summary.frameCount);
            Assert::IsTrue(summary.ambiguous);
        }
    }

    TEST_METHOD(SameSequencePollingCannotKeepEvidenceAliveBeyondThreeSeconds)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        const auto result = SampleLocalBoundaryDiagnostics(frame.Source());
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        context.acceptedSequence = 1;
        context.timestampMs = 100;
        Assert::IsTrue(history.Observe(result, context).available);
        LocalBoundaryDiagnosticHistorySummary summary;
        for (uint64_t now = 200; now <= 3400; now += 100)
        {
            context.timestampMs = now;
            summary = history.Observe(result, context);
            Assert::IsTrue(summary.frameCount <= size_t(1));
            Assert::IsTrue(summary.windowAgeMs <= uint64_t(3000));
        }
        Assert::IsFalse(summary.available);
        Assert::AreEqual(size_t(0), summary.frameCount);
        Assert::AreEqual(size_t(0), summary.distinctCount);
        Assert::AreEqual(size_t(0), summary.bottomUnionSamples);
    }

    TEST_METHOD(PrecisionMetadataCannotPromoteUnknownOrEightBitSourcePrecision)
    {
        BoundaryFrame frame;
        frame.LocalPicture();
        auto source = frame.Source();
        source.encoding = VideoFrameEncoding::UNKNOWN;
        const auto unknown = SampleLocalBoundaryDiagnostics(source);
        Assert::IsTrue(unknown.available && unknown.ambiguous);
        Assert::IsFalse(unknown.sourcePrecisionKnown);
        Assert::IsFalse(unknown.precisionSupported);
        source.encoding = VideoFrameEncoding::UYVY;
        const auto eightBitOrigin = SampleLocalBoundaryDiagnostics(source);
        Assert::IsTrue(eightBitOrigin.available && eightBitOrigin.ambiguous);
        Assert::IsTrue(eightBitOrigin.sourcePrecisionKnown);
        Assert::IsFalse(eightBitOrigin.precisionSupported);
        Assert::AreEqual(unknown.fingerprint, eightBitOrigin.fingerprint);
    }

};
} // namespace VideoProcessorTest
