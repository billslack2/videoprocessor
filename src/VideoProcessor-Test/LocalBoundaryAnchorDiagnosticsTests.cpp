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
void WriteAnchorCode(uint8_t* target, int code)
{
    const uint16_t packed = static_cast<uint16_t>(code << 6);
    target[0] = static_cast<uint8_t>(packed);
    target[1] = static_cast<uint8_t>(packed >> 8);
}

// Raw source pixels, with even rectangle coordinates so equivalent 4:2:0,
// 4:2:2 planar and packed-v210 representations retain identical information.
struct AnchorFrame
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> bytes;

    AnchorFrame(int w = 384, int h = 216, bool fullHeightChroma = true)
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
                WriteAnchorCode(bytes.data() + size_t(row) * pitch + size_t(x) * 2, y);
        for (int row = p210 ? top : top / 2; row < (p210 ? bottom : (bottom + 1) / 2); ++row)
            for (int x = left & ~1; x < right; x += 2)
            {
                auto* pixel = bytes.data() + pitch * height + size_t(row) * pitch + size_t(x) * 2;
                WriteAnchorCode(pixel, u);
                WriteAnchorCode(pixel + 2, v);
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

struct PackedAnchorFrame
{
    int width, height;
    size_t pitch;
    std::vector<uint8_t> bytes;

    explicit PackedAnchorFrame(const AnalysisLumaSource& source)
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


// Endpoint-sampled4K grid; each fixture describes only measured samples.
// It deliberately makes no assertion about the unsampled physical gaps.
ActivePictureDiagnosticGrid AnchorGrid(int phase = 0, int top = 280, int bottom = 1880)
{
    ActivePictureDiagnosticGrid grid;
    grid.columns = 128;
    grid.rows = 270;
    grid.samples.resize(size_t(grid.columns) * grid.rows, {64, 512, 512});
    int firstRow = -1;
    for (int row = 0; row < grid.rows; ++row)
    {
        const int y = row * 2159 / 269;
        if (y < top || y >= bottom) continue;
        if (firstRow < 0) firstRow = row;
        const int stripeFirst = 6 + (phase % 8) * 12;
        for (int column = 0; column < grid.columns; ++column)
            if ((column >= stripeFirst && column < stripeFirst + 8) ||
                (row < firstRow + 4 && column >= 72 && column < 120))
                grid.samples[size_t(row) * grid.columns + column] = {144, 485, 528};
    }
    return grid;
}

void SetAnchorGridRow(ActivePictureDiagnosticGrid& grid, int coordinate,
    AnalysisLumaSample sample, int firstColumn = 0, int endColumn = 128)
{
    bool found = false;
    for (int row = 0; row < grid.rows; ++row)
        if (row * 2159 / 269 == coordinate)
        {
            found = true;
            for (int column = firstColumn; column < endColumn; ++column)
                grid.samples[size_t(row) * grid.columns + column] = sample;
        }
    Assert::IsTrue(found, L"Fixture coordinate must be an actual sampled row.");
}

ActivePictureDiagnosticGrid DarkAnchorGrid()
{
    auto grid = AnchorGrid();
    std::fill(grid.samples.begin(), grid.samples.end(), AnalysisLumaSample{64, 512, 512});
    return grid;
}

LocalBoundaryDiagnosticTelemetry AcquireGridAnchor(LocalBoundaryDiagnosticSession& session,
    LocalBoundaryDiagnosticContext& context)
{
    LocalBoundaryDiagnosticTelemetry telemetry;
    for (int phase = 0; phase < 4; ++phase)
    {
        context.acceptedSequence = phase + 1;
        context.timestampMs = 100 + phase * 200;
        telemetry = session.ObserveGrid(AnchorGrid(phase), 3840, 2160,
            VideoFrameEncoding::V210, context);
    }
    Assert::IsTrue(telemetry.anchor.active && telemetry.anchor.created,
        L"Moving local witnesses with clean symmetric exteriors should acquire a diagnostic anchor.");
    return telemetry;
}

void NextAnchorObservation(LocalBoundaryDiagnosticContext& context)
{
    ++context.acceptedSequence;
    context.timestampMs += 200;
}

} // namespace

TEST_CLASS(LocalBoundaryAnchorDiagnosticsTests)
{
public:
    TEST_METHOD(MovingLocalWitnessesAcquireOnlyAfterTemporalCoverageAndStayAmbiguous)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        LocalBoundaryDiagnosticTelemetry telemetry;
        for (int phase = 0; phase < 4; ++phase)
        {
            context.acceptedSequence = phase + 1;
            context.timestampMs = 100 + phase * 200;
            telemetry = session.ObserveGrid(AnchorGrid(phase), 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsTrue(telemetry.observation.candidateAvailable);
            Assert::IsTrue(telemetry.observation.ambiguous && telemetry.hypothesis.ambiguous && telemetry.anchor.ambiguous);
            if (phase < 3)
            {
                Assert::IsFalse(telemetry.hypothesis.qualifies);
                Assert::IsFalse(telemetry.anchor.active);
            }
        }
        Assert::IsTrue(telemetry.hypothesis.qualifies);
        Assert::IsTrue(telemetry.anchor.active && telemetry.anchor.created);
        Assert::AreEqual(280, telemetry.anchor.top);
        Assert::AreEqual(1879, telemetry.anchor.bottom);
        Assert::AreEqual(64, telemetry.anchor.blackFloor);
        Assert::AreEqual(88, telemetry.anchor.blackThreshold);
        Assert::AreEqual(uint64_t(700), telemetry.anchor.createdAtMs);
        Assert::IsTrue(telemetry.history.bottomDistinctMasks >= size_t(3));
        Assert::IsTrue(telemetry.hypothesis.bottomNewCoverage >= 0.06);
        // The same pixels can describe moving yellow objects on intentional
        // full-raster black artwork. Passing this experiment does NOT decide
        // that semantic ambiguity and must not manufacture crop authority.
        static_assert(!std::is_convertible<LocalBoundaryAnchorTelemetry, ActivePictureBounds>::value,
            "A diagnostic anchor is not crop geometry.");
        static_assert(!std::is_convertible<LocalBoundaryAnchorHypothesis, ActivePictureEvidence>::value,
            "A qualifying diagnostic hypothesis is not acquisition authority.");
    }

    TEST_METHOD(CapturedAsymmetricCandidateShapesCannotAcquireAnchors)
    {
        for (int shape = 0; shape < 2; ++shape)
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            const int top = shape == 0 ? 48 : 208;
            const int bottom = shape == 0 ? 2047 : 1879;
            for (int phase = 0; phase < 12; ++phase)
            {
                context.acceptedSequence = phase + 1;
                context.timestampMs = 100 + phase * 200;
                const auto telemetry = session.ObserveGrid(AnchorGrid(phase, top, bottom),
                    3840, 2160, VideoFrameEncoding::V210, context);
                Assert::AreEqual(top, telemetry.observation.top);
                Assert::AreEqual(bottom, telemetry.observation.bottom);
                Assert::IsTrue(telemetry.observation.sampledBarsClean);
                Assert::IsFalse(telemetry.hypothesis.qualifies);
                Assert::IsFalse(telemetry.anchor.active);
            }
        }
    }

    TEST_METHOD(PausedInteriorNoiseCannotManufactureMovingEdgeCoverage)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        LocalBoundaryDiagnosticTelemetry telemetry;
        for (int frame = 0; frame < 12; ++frame)
        {
            auto grid = AnchorGrid();
            // Deep interior only: distinct fingerprints, identical edge masks.
            grid.samples[size_t(120) * grid.columns + 60].luma = uint16_t(100 + frame);
            context.acceptedSequence = frame + 1;
            context.timestampMs = 100 + frame * 200;
            telemetry = session.ObserveGrid(grid, 3840, 2160, VideoFrameEncoding::V210, context);
            Assert::IsTrue(telemetry.observation.candidateAvailable);
            Assert::IsFalse(telemetry.hypothesis.qualifies);
            Assert::IsFalse(telemetry.anchor.active);
        }
        Assert::IsTrue(telemetry.history.distinctCount > size_t(1));
        Assert::AreEqual(size_t(1), telemetry.history.topDistinctMasks);
        Assert::AreEqual(size_t(1), telemetry.history.bottomDistinctMasks);
        Assert::AreEqual(0.0, telemetry.hypothesis.bottomNewCoverage);
    }

    TEST_METHOD(CadenceRepeatCannotSupplyTheFourthAcquisitionObservation)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        for (int phase = 0; phase < 4; ++phase)
        {
            context.acceptedSequence = phase + 1;
            context.timestampMs = 100 + phase * 200;
            context.cadenceRepeat = phase == 3;
            const auto telemetry = session.ObserveGrid(AnchorGrid(phase), 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsFalse(telemetry.anchor.active);
            Assert::IsFalse(telemetry.hypothesis.qualifies);
        }
    }

    TEST_METHOD(CurrentOpposingEvidenceAndPrecisionRemainMandatoryDespiteMatureHistory)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        const auto ready = AcquireGridAnchor(session, context);
        for (int missing = 0; missing < 8; ++missing)
        {
            auto result = ready.observation;
            if (missing == 0) result.topEdge.supportedSamples = 2;
            if (missing == 1) result.bottomEdge.supportedSamples = 2;
            if (missing == 2) result.topEdge.inwardContinuity = 0.5;
            if (missing == 3) result.bottomEdge.inwardContinuity = 0.5;
            if (missing == 4) result.sourcePrecisionKnown = false;
            if (missing == 5) result.precisionSupported = false;
            if (missing == 6) result.sampledBarsClean = false;
            if (missing == 7) result.excludedViolations = 1;
            const auto hypothesis = EvaluateLocalBoundaryAnchorHypothesis(result, ready.history);
            Assert::IsFalse(hypothesis.qualifies);
            Assert::IsTrue(hypothesis.ambiguous);
        }
    }

    TEST_METHOD(ShrinkingOrDisappearingFreshCandidateDoesNotEraseCleanOriginalExterior)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        const auto acquired = AcquireGridAnchor(session, context);
        for (int inset = 320; inset <= 440; inset += 40)
        {
            NextAnchorObservation(context);
            const auto telemetry = session.ObserveGrid(AnchorGrid(4, inset, 2160 - inset),
                3840, 2160, VideoFrameEncoding::V210, context);
            Assert::IsTrue(telemetry.observation.top > acquired.anchor.top);
            Assert::IsTrue(telemetry.anchor.active && telemetry.anchor.evaluated && telemetry.anchor.current);
            Assert::IsFalse(telemetry.anchor.created);
            Assert::AreEqual(acquired.anchor.top, telemetry.anchor.top);
            Assert::AreEqual(acquired.anchor.bottom, telemetry.anchor.bottom);
            Assert::AreEqual(acquired.anchor.createdAtMs, telemetry.anchor.createdAtMs);
            Assert::AreEqual(size_t(0), telemetry.anchor.topViolations + telemetry.anchor.bottomViolations);
        }
        NextAnchorObservation(context);
        const auto dark = session.ObserveGrid(DarkAnchorGrid(), 3840, 2160,
            VideoFrameEncoding::V210, context);
        Assert::IsFalse(dark.observation.candidateAvailable);
        Assert::IsTrue(dark.anchor.active && dark.anchor.evaluated && dark.anchor.current);
        Assert::AreEqual(acquired.anchor.anchorId, dark.anchor.anchorId);
        Assert::AreEqual(acquired.anchor.top, dark.anchor.top);
        Assert::AreEqual(acquired.anchor.bottom, dark.anchor.bottom);
    }

    TEST_METHOD(ExpansionContaminatesOldAnchorEvenWhenFreshCandidateBarsAreClean)
    {
        for (bool fullRaster : {false, true})
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            const auto acquired = AcquireGridAnchor(session, context);
            NextAnchorObservation(context);
            auto expanded = AnchorGrid(4, fullRaster ? 0 : 208, fullRaster ? 2160 : 1952);
            if (fullRaster)
                std::fill(expanded.samples.begin(), expanded.samples.end(), AnalysisLumaSample{144, 485, 528});
            const auto telemetry = session.ObserveGrid(expanded, 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsTrue(telemetry.observation.candidateAvailable);
            if (!fullRaster)
            {
                Assert::IsTrue(telemetry.observation.sampledBarsClean);
                Assert::AreEqual(size_t(0), telemetry.observation.excludedViolations);
            }
            Assert::IsTrue(telemetry.anchor.released && telemetry.anchor.evaluated);
            Assert::IsFalse(telemetry.anchor.active);
            Assert::IsTrue(telemetry.anchor.topViolations > 0 && telemetry.anchor.bottomViolations > 0);
            Assert::IsTrue(telemetry.history.frameCount <= size_t(1));
            Assert::IsFalse(telemetry.hypothesis.qualifies);
        }
    }

    TEST_METHOD(AdjacentMeasuredExteriorRowsAreCheckedForLumaAndChromaIntrusions)
    {
        for (bool bottom : {false, true})
            for (bool chromaOnly : {false, true})
            {
                LocalBoundaryDiagnosticSession session;
                LocalBoundaryDiagnosticContext context;
                AcquireGridAnchor(session, context);
                auto grid = AnchorGrid(4);
                SetAnchorGridRow(grid, bottom ? 1886 : 272,
                    chromaOnly ? AnalysisLumaSample{64, 400, 620} : AnalysisLumaSample{144, 512, 512}, 0, 1);
                NextAnchorObservation(context);
                const auto telemetry = session.ObserveGrid(grid, 3840, 2160,
                    VideoFrameEncoding::V210, context);
                Assert::AreEqual(280, telemetry.observation.top);
                Assert::AreEqual(1879, telemetry.observation.bottom);
                Assert::IsTrue(telemetry.anchor.released && telemetry.anchor.evaluated);
                Assert::IsFalse(telemetry.anchor.active);
                Assert::AreEqual(size_t(1), bottom ? telemetry.anchor.bottomViolations : telemetry.anchor.topViolations);
                Assert::AreEqual(size_t(0), bottom ? telemetry.anchor.topViolations : telemetry.anchor.bottomViolations);
            }
    }

    TEST_METHOD(BoundaryWitnessRowsAreNotMisclassifiedAsExteriorAndUnknownGapsAreExplicit)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        AcquireGridAnchor(session, context);
        auto grid = AnchorGrid(4);
        SetAnchorGridRow(grid, 280, {300, 400, 620});
        SetAnchorGridRow(grid, 1878, {300, 400, 620});
        NextAnchorObservation(context);
        const auto telemetry = session.ObserveGrid(grid, 3840, 2160, VideoFrameEncoding::V210, context);
        Assert::IsTrue(telemetry.anchor.active && telemetry.anchor.evaluated);
        Assert::AreEqual(size_t(0), telemetry.anchor.topViolations + telemetry.anchor.bottomViolations);
        Assert::AreEqual(272, telemetry.anchor.topExteriorLastY);
        Assert::AreEqual(1886, telemetry.anchor.bottomExteriorFirstY);
        Assert::AreEqual(273, telemetry.anchor.topUnknownFirstY);
        Assert::AreEqual(279, telemetry.anchor.topUnknownLastY);
        Assert::AreEqual(1879, telemetry.anchor.bottomUnknownFirstY);
        Assert::AreEqual(1885, telemetry.anchor.bottomUnknownLastY);
    }

    TEST_METHOD(OriginalBlackCutoffCannotDriftUpToAdmitNewExteriorContent)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        const auto acquired = AcquireGridAnchor(session, context);
        auto raised = AnchorGrid(4);
        // Raise the entire neutral background, including left/right perimeter
        // inside the picture. Raising only top/bottom margins leaves the
        // perimeter's10th percentile at64 and cannot exercise cutoff drift.
        for (auto& sample : raised.samples)
            if (sample.luma == 64 && sample.chromaU == 512 && sample.chromaV == 512)
                sample = {100, 512, 512};
        NextAnchorObservation(context);
        const auto telemetry = session.ObserveGrid(raised, 3840, 2160, VideoFrameEncoding::V210, context);
        Assert::AreEqual(104, telemetry.observation.blackThreshold);
        Assert::IsTrue(telemetry.observation.sampledBarsClean);
        Assert::AreEqual(88, telemetry.anchor.blackThreshold);
        Assert::IsTrue(telemetry.anchor.topViolations > 0 && telemetry.anchor.bottomViolations > 0);
        Assert::IsFalse(telemetry.anchor.active);
        Assert::IsTrue(telemetry.anchor.released);
    }

    TEST_METHOD(HardExpiryCannotRenewFromAnAlwaysQualifyingOldWindow)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        const auto acquired = AcquireGridAnchor(session, context);
        const uint64_t deadline = acquired.anchor.createdAtMs + LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS;
        int phase = 4;
        while (context.timestampMs + 200 < deadline)
        {
            NextAnchorObservation(context);
            const auto telemetry = session.ObserveGrid(AnchorGrid(phase++), 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsTrue(telemetry.anchor.active);
            Assert::IsFalse(telemetry.anchor.created);
            Assert::AreEqual(acquired.anchor.createdAtMs, telemetry.anchor.createdAtMs);
            Assert::AreEqual(acquired.anchor.anchorId, telemetry.anchor.anchorId);
        }
        ++context.acceptedSequence;
        context.timestampMs = deadline + 1;
        const auto expired = session.ObserveGrid(AnchorGrid(phase++), 3840, 2160,
            VideoFrameEncoding::V210, context);
        Assert::IsFalse(expired.anchor.active);
        Assert::IsTrue(expired.anchor.released);
        Assert::IsFalse(expired.anchor.created);
        Assert::IsTrue(expired.history.frameCount <= size_t(1));
        NextAnchorObservation(context);
        const auto next = session.ObserveGrid(AnchorGrid(phase), 3840, 2160,
            VideoFrameEncoding::V210, context);
        Assert::IsFalse(next.anchor.active);
    }

    TEST_METHOD(SameSequenceOrFingerprintCannotExtendAnchorLifetime)
    {
        for (int repeatMode = 0; repeatMode < 3; ++repeatMode)
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            const auto acquired = AcquireGridAnchor(session, context);
            const auto paused = AnchorGrid(3);
            const uint64_t deadline = acquired.anchor.createdAtMs + LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS;
            while (context.timestampMs + 200 < deadline)
            {
                context.timestampMs += 200;
                if (repeatMode != 0) ++context.acceptedSequence;
                context.cadenceRepeat = repeatMode == 2;
                const auto telemetry = session.ObserveGrid(paused, 3840, 2160, VideoFrameEncoding::V210, context);
                Assert::IsTrue(telemetry.anchor.active);
                Assert::AreEqual(acquired.anchor.createdAtMs, telemetry.anchor.createdAtMs);
            }
            context.timestampMs = deadline + 1;
            if (repeatMode != 0) ++context.acceptedSequence;
            const auto expired = session.ObserveGrid(paused, 3840, 2160, VideoFrameEncoding::V210, context);
            Assert::IsFalse(expired.anchor.active);
            Assert::IsTrue(expired.anchor.released);
        }
    }

    TEST_METHOD(EveryIdentityAndSceneChangeClearsTheOldAnchorAndAdmissionHistory)
    {
        using Generation = uint64_t LocalBoundaryDiagnosticContext::*;
        const Generation fields[] = {
            &LocalBoundaryDiagnosticContext::sourceGeneration,
            &LocalBoundaryDiagnosticContext::rendererGeneration,
            &LocalBoundaryDiagnosticContext::viewportGeneration,
            &LocalBoundaryDiagnosticContext::sourceFormatGeneration,
            &LocalBoundaryDiagnosticContext::policyGeneration,
            &LocalBoundaryDiagnosticContext::continuityGeneration,
            &LocalBoundaryDiagnosticContext::sceneGeneration };
        for (Generation field : fields)
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            AcquireGridAnchor(session, context);
            NextAnchorObservation(context);
            context.*field += 1;
            const auto telemetry = session.ObserveGrid(AnchorGrid(4), 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsFalse(telemetry.anchor.active);
            Assert::IsTrue(telemetry.anchor.released);
            Assert::IsTrue(telemetry.history.frameCount <= size_t(1));
            Assert::IsFalse(telemetry.hypothesis.qualifies);
        }
    }

    TEST_METHOD(GapRollbackInvalidSourceAndGridChangeClearAnchors)
    {
        for (int failure = 0; failure < 5; ++failure)
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            AcquireGridAnchor(session, context);
            auto grid = AnchorGrid(4);
            if (failure == 0) context.timestampMs += LOCAL_BOUNDARY_HISTORY_MAX_GAP_MS + 1;
            else if (failure == 1) --context.timestampMs;
            else context.timestampMs += 200;
            if (failure == 2) --context.acceptedSequence;
            else ++context.acceptedSequence;
            if (failure == 3) grid.samples.pop_back();
            const auto telemetry = session.ObserveGrid(grid, failure == 4 ? 3838 : 3840, 2160,
                VideoFrameEncoding::V210, context);
            Assert::IsFalse(telemetry.anchor.active);
            Assert::IsTrue(telemetry.anchor.released);
            Assert::IsTrue(telemetry.history.frameCount <= size_t(1));
            Assert::IsFalse(telemetry.hypothesis.qualifies);
        }
    }

    TEST_METHOD(EquivalentNativeAndPlanarPixelsAgreeWithReusedGridAtBoundedCost)
    {
        LocalBoundaryDiagnosticSession p010Session, p210Session, nativeSession, gridSession;
        LocalBoundaryDiagnosticContext context;
        for (int phase = 0; phase < 4; ++phase)
        {
            AnchorFrame p010(384, 216, false), p210;
            p010.LocalPicture(18 + phase * 54, 42 + phase * 54);
            p210.LocalPicture(18 + phase * 54, 42 + phase * 54);
            const auto originalBytes = p210.bytes;
            PackedAnchorFrame native(p210.Source());
            const auto grid = SampleActivePictureDiagnosticGrid(p210.Source());
            context.acceptedSequence = phase + 1;
            context.timestampMs = 100 + phase * 200;
            const auto a = p010Session.Observe(p010.Source(), context);
            const auto b = p210Session.Observe(p210.Source(), context);
            const auto c = nativeSession.Observe(native.Source(), context);
            const auto d = gridSession.ObserveGrid(grid, 384, 216, VideoFrameEncoding::V210, context);
            for (const auto& other : {a, c, d})
            {
                Assert::AreEqual(b.observation.fingerprint, other.observation.fingerprint);
                Assert::AreEqual(b.hypothesis.qualifies, other.hypothesis.qualifies);
                Assert::AreEqual(b.anchor.active, other.anchor.active);
                Assert::AreEqual(b.anchor.top, other.anchor.top);
                Assert::AreEqual(b.anchor.bottom, other.anchor.bottom);
                Assert::AreEqual(b.anchor.topViolations, other.anchor.topViolations);
                Assert::AreEqual(b.anchor.bottomViolations, other.anchor.bottomViolations);
                Assert::AreEqual(grid.samples.size(), other.observation.sampleCount);
                Assert::IsTrue(other.observation.sampleCount <= LOCAL_BOUNDARY_MAX_SAMPLES);
            }
            Assert::IsTrue(originalBytes == p210.bytes);
            if (phase == 3) Assert::IsTrue(b.anchor.active);
        }
        LocalBoundaryDiagnosticSession fourK;
        const auto fullGrid = AnchorGrid();
        const auto result = fourK.ObserveGrid(fullGrid, 3840, 2160, VideoFrameEncoding::V210, context);
        Assert::AreEqual(size_t(128 * 270), result.observation.sampleCount);
    }

    TEST_METHOD(UnsupportedPrecisionCannotAcquireEvenWithMovingEdgeCoverage)
    {
        for (VideoFrameEncoding encoding : {VideoFrameEncoding::UNKNOWN, VideoFrameEncoding::UYVY})
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            for (int phase = 0; phase < 8; ++phase)
            {
                context.acceptedSequence = phase + 1;
                context.timestampMs = 100 + phase * 200;
                const auto telemetry = session.ObserveGrid(AnchorGrid(phase), 3840, 2160, encoding, context);
                Assert::IsTrue(telemetry.observation.available);
                Assert::IsFalse(telemetry.hypothesis.qualifies);
                Assert::IsFalse(telemetry.anchor.active);
                Assert::IsTrue(telemetry.anchor.ambiguous);
            }
        }
    }

    TEST_METHOD(ActiveAnchorDoesNotMutatePixelsOrExistingDetectorAcrossRetentionAndRelease)
    {
        LocalBoundaryDiagnosticSession session;
        LocalBoundaryDiagnosticContext context;
        for (int phase = 0; phase < 4; ++phase)
        {
            AnchorFrame frame;
            frame.LocalPicture(18 + phase * 54, 42 + phase * 54);
            context.acceptedSequence = phase + 1;
            context.timestampMs = 100 + phase * 200;
            const auto telemetry = session.Observe(frame.Source(), context);
            if (phase == 3) Assert::IsTrue(telemetry.anchor.active);
        }
        for (int change = 0; change < 4; ++change)
        {
            AnchorFrame frame;
            if (change == 0) frame.LocalPicture(234, 258);
            if (change == 1) frame.LocalPicture(90, 114, 48, 168);
            // change2 is a complete fade: the old excluded region stays black.
            if (change == 3) frame.LocalPicture(90, 114, 8, 208);
            const auto pixels = frame.bytes;
            const auto before = ExtractActivePictureEvidence(frame.Source());
            const auto darkBefore = EvaluateActivePictureGlobalNearBlack(frame.Source());
            NextAnchorObservation(context);
            const auto telemetry = session.Observe(frame.Source(), context);
            if (change < 3) Assert::IsTrue(telemetry.anchor.active);
            else Assert::IsTrue(telemetry.anchor.released && !telemetry.anchor.active);
            const auto after = ExtractActivePictureEvidence(frame.Source());
            const auto darkAfter = EvaluateActivePictureGlobalNearBlack(frame.Source());
            Assert::IsTrue(pixels == frame.bytes);
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
            Assert::AreEqual(darkBefore.nearBlack, darkAfter.nearBlack);
            Assert::AreEqual(int(ConstrainNearBlackCropAcquisition(before, darkBefore.nearBlack).classification),
                int(ConstrainNearBlackCropAcquisition(after, darkAfter.nearBlack).classification));
        }
    }

    TEST_METHOD(IneligibleEarlyEntriesCannotContributeToLaterAcquisition)
    {
        for (int failure = 0; failure < 3; ++failure)
        {
            LocalBoundaryDiagnosticSession session;
            LocalBoundaryDiagnosticContext context;
            auto first = AnchorGrid();
            if (failure == 0)
                for (int y : {288, 296, 304}) SetAnchorGridRow(first, y, {64, 512, 512});
            if (failure == 1)
                for (int y : {1870, 1862, 1854}) SetAnchorGridRow(first, y, {64, 512, 512});
            context.acceptedSequence = 1;
            context.timestampMs = 100;
            const auto ineligible = session.ObserveGrid(first, 3840, 2160,
                failure == 2 ? VideoFrameEncoding::UNKNOWN : VideoFrameEncoding::V210, context);
            Assert::IsTrue(ineligible.observation.candidateAvailable);
            Assert::IsFalse(ineligible.hypothesis.qualifies);
            Assert::IsFalse(ineligible.anchor.active);
            for (int phase = 1; phase <= 4; ++phase)
            {
                NextAnchorObservation(context);
                const auto telemetry = session.ObserveGrid(AnchorGrid(phase), 3840, 2160,
                    VideoFrameEncoding::V210, context);
                if (phase < 4)
                {
                    Assert::IsFalse(telemetry.anchor.active);
                    Assert::IsFalse(telemetry.hypothesis.qualifies);
                }
                else
                {
                    Assert::IsTrue(telemetry.anchor.active && telemetry.anchor.created);
                    Assert::AreEqual(uint64_t(900), telemetry.anchor.createdAtMs);
                }
            }
        }
        // Feature-only replay can carry an ineligible first observation without
        // Session's eager clear. The evaluator must reject that mixed window too.
        LocalBoundaryDiagnosticHistory history;
        LocalBoundaryDiagnosticContext context;
        LocalBoundaryDiagnosticHistorySummary summary;
        LocalBoundaryDiagnosticResult result;
        for (int phase = 0; phase < 4; ++phase)
        {
            auto grid = AnchorGrid(phase);
            if (phase == 0)
                for (int y : {288, 296, 304}) SetAnchorGridRow(grid, y, {64, 512, 512});
            result = AnalyzeLocalBoundaryGrid(grid, 3840, 2160);
            result.sourcePrecisionKnown = result.precisionSupported = true;
            context.acceptedSequence = phase + 1;
            context.timestampMs = 100 + phase * 200;
            summary = history.Observe(result, context);
        }
        Assert::AreEqual(size_t(4), summary.frameCount);
        Assert::IsFalse(summary.hypothesisWindowEligible);
        Assert::IsFalse(EvaluateLocalBoundaryAnchorHypothesis(result, summary).qualifies);
    }

};
} // namespace VideoProcessorTest
