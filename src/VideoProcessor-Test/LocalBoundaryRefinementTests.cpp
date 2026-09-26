#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <LocalBoundaryDiagnostics.h>
#include <LocalBoundaryRefinement.h>
#include <SparseBoundaryCropExperiment.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
namespace
{
struct RefinementPixels
{
    int width, height;
    bool p210;
    size_t pitch;
    std::vector<uint16_t> pixels;
    RefinementPixels(int w = 3840, int h = 2160, bool fullChroma = true)
        : width(w), height(h), p210(fullChroma), pitch(size_t(w) + 8),
          pixels(pitch * (h + (fullChroma ? h : h / 2)), uint16_t(512 << 6))
    { Rectangle(0, 0, width, height, 64); }
    void Rectangle(int left, int top, int right, int bottom, int y, int u = 512, int v = 512)
    {
        for (int row = top; row < bottom; ++row)
            std::fill(pixels.begin() + row * pitch + left, pixels.begin() + row * pitch + right, uint16_t(y << 6));
        for (int row = p210 ? top : top / 2; row < (p210 ? bottom : (bottom + 1) / 2); ++row)
            for (int x = left & ~1; x < right; x += 2)
            {
                pixels[pitch * height + row * pitch + x] = uint16_t(u << 6);
                pixels[pitch * height + row * pitch + x + 1] = uint16_t(v << 6);
            }
    }
    int X(int column) const { return int(int64_t(column) * (width - 1) / 127); }
    void Picture(int phase = 0, bool weakTop = false, int inset = 280, int thinWidth = 16)
    {
        Rectangle(0, 0, width, height, 64);
        const int top = inset, bottom = height - inset;
        Rectangle(0, height / 3, width, height * 2 / 3, 300);
        // Broad texture stops before the weak boundary. A real narrow detail
        // continues to it but occupies only one of the 128 coarse columns.
        const int x = (X(15 + phase) - thinWidth / 2) & ~1;
        Rectangle(x, top, x + thinWidth, bottom, 144, 485, 528);
        if (weakTop)
        {
            Rectangle(width * 9 / 16, bottom - 96, width * 15 / 16, bottom, 144, 485, 528);
            Rectangle(0, top + 120, 300, height / 3, 144, 485, 528);
        }
        else
        {
            Rectangle(width * 9 / 16, top, width * 15 / 16, top + 96, 144, 485, 528);
            Rectangle(0, height * 2 / 3, 300, bottom - 120, 144, 485, 528);
        }
    }
    AnalysisLumaSource Source() const
    {
        return { reinterpret_cast<const uint8_t*>(pixels.data()), pixels.size() * 2,
            width, height, pitch * 2, pitch * 2, p210 ? AnalysisLumaFormat::P210 : AnalysisLumaFormat::P010,
            VideoFrameEncoding::V210, ColorSpace::REC_709, 7 };
    }
};
struct PackedRefinementPixels
{
    int width, height;
    size_t pitch;
    std::vector<uint8_t> pixels;
    explicit PackedRefinementPixels(const AnalysisLumaSource& source)
        : width(source.width), height(source.height), pitch(size_t(width / 6) * 16 + 16), pixels(pitch * height)
    {
        Assert::AreEqual(0, width % 6);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; x += 6)
            {
                AnalysisLumaSample p[6];
                for (int i = 0; i < 6; ++i) Assert::IsTrue(source.Sample(x + i, y, p[i]));
                const uint32_t words[] = {
                    uint32_t(p[0].chromaU) | uint32_t(p[0].luma) << 10 | uint32_t(p[0].chromaV) << 20,
                    uint32_t(p[1].luma) | uint32_t(p[2].chromaU) << 10 | uint32_t(p[2].luma) << 20,
                    uint32_t(p[2].chromaV) | uint32_t(p[3].luma) << 10 | uint32_t(p[4].chromaU) << 20,
                    uint32_t(p[4].luma) | uint32_t(p[4].chromaV) << 10 | uint32_t(p[5].luma) << 20 };
                for (int word = 0; word < 4; ++word)
                    for (int byte = 0; byte < 4; ++byte)
                        pixels[y * pitch + (x / 6) * 16 + word * 4 + byte] = uint8_t(words[word] >> (8 * byte));
            }
    }
    AnalysisLumaSource Source() const
    {
        return { pixels.data(), pixels.size(), width, height, pitch, 0,
            AnalysisLumaFormat::NativeYuv422, VideoFrameEncoding::V210, ColorSpace::REC_709, 7 };
    }
};
LocalBoundaryRefinementResult RefinePixels(const AnalysisLumaSource& source)
{
    return RefineLocalBoundaryEdges(source, SampleActivePictureDiagnosticGrid(source),
        SampleLocalBoundaryDiagnostics(source));
}
void AdvanceRefinement(SparseBoundaryCropExperimentInput& input)
{
    ++input.context.acceptedSequence;
    input.context.timestampMs += 200;
}
SparseBoundaryCropExperimentInput RefinementInput()
{
    SparseBoundaryCropExperimentInput input;
    input.enabled = input.startupEligible = true;
    input.context.sourceGeneration = 7;
    input.context.rendererGeneration = 11;
    input.context.viewportGeneration = 13;
    input.context.sourceFormatGeneration = 17;
    input.context.policyGeneration = 19;
    input.context.continuityGeneration = 23;
    input.context.sceneGeneration = 29;
    input.context.acceptedSequence = 1;
    input.context.timestampMs = 100;
    return input;
}
}

TEST_CLASS(LocalBoundaryRefinementTests)
{
public:
    TEST_METHOD(PairedShadowRefinesMovingEdgeWithoutChangingCoarseOrSource)
    {
        RefinementPixels pixels;
        LocalBoundaryDiagnosticComparison comparison;
        LocalBoundaryDiagnosticSession baseline;
        auto input = RefinementInput();
        bool refinedAnchor = false;
        for (int phase = 0; phase < 14; ++phase)
        {
            pixels.Picture(phase);
            const auto before = pixels.pixels;
            const auto source = pixels.Source();
            const auto expected = baseline.Observe(source, input.context);
            const auto observed = comparison.Observe(source, input.context);
            Assert::IsTrue(before == pixels.pixels);
            Assert::AreEqual(expected.observation.top, observed.coarse.observation.top);
            Assert::AreEqual(expected.observation.bottom, observed.coarse.observation.bottom);
            Assert::IsTrue(expected.observation.topEdge.supportMask == observed.coarse.observation.topEdge.supportMask);
            Assert::IsTrue(expected.observation.bottomEdge.supportMask == observed.coarse.observation.bottomEdge.supportMask);
            Assert::AreEqual(expected.history.frameCount, observed.coarse.history.frameCount);
            Assert::AreEqual(expected.anchor.active, observed.coarse.anchor.active);
            Assert::IsFalse(observed.coarse.refinementEvaluated);
            Assert::IsTrue(observed.refined.refinementApplied);
            Assert::IsTrue(observed.refined.refinementSamples <= LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES);
            Assert::IsTrue(observed.refined.observation.bottom > observed.coarse.observation.bottom + 100);
            refinedAnchor = refinedAnchor || observed.refined.anchor.active;
            AdvanceRefinement(input);
        }
        Assert::IsTrue(refinedAnchor);
    }

    TEST_METHOD(PairedShadowPausedAndRepeatedFramesDoNotManufactureMotion)
    {
        RefinementPixels pixels;
        pixels.Picture();
        LocalBoundaryDiagnosticComparison comparison;
        auto input = RefinementInput();
        for (int frame = 0; frame < 18; ++frame)
        {
            const auto result = comparison.Observe(pixels.Source(), input.context);
            Assert::IsTrue(result.refined.refinementApplied);
            Assert::IsFalse(result.coarse.anchor.active || result.refined.anchor.active);
            Assert::IsTrue(result.refined.history.distinctCount <= 1);
            const auto repeat = comparison.Observe(pixels.Source(), input.context);
            Assert::IsFalse(repeat.refined.history.freshObservation);
            AdvanceRefinement(input);
        }
    }

    TEST_METHOD(PairedShadowResetAndGenerationChangeDiscardRefinedAnchor)
    {
        RefinementPixels pixels;
        LocalBoundaryDiagnosticComparison comparison;
        auto input = RefinementInput();
        LocalBoundaryDiagnosticComparisonTelemetry result;
        for (int phase = 0; phase < 14; ++phase)
        {
            pixels.Picture(phase);
            result = comparison.Observe(pixels.Source(), input.context);
            AdvanceRefinement(input);
        }
        Assert::IsTrue(result.refined.anchor.active);
        ++input.context.sceneGeneration;
        result = comparison.Observe(pixels.Source(), input.context);
        Assert::IsFalse(result.refined.anchor.active);
        Assert::IsTrue(result.refined.anchor.released);
        comparison.Reset();
        AdvanceRefinement(input);
        result = comparison.Observe(pixels.Source(), input.context);
        Assert::IsFalse(result.coarse.anchor.active || result.refined.anchor.active);
        Assert::IsTrue(result.refined.history.frameCount <= 1);
    }

    TEST_METHOD(PairedShadowReadsP010P210AndNativeV210Consistently)
    {
        RefinementPixels p210, p010(3840, 2160, false);
        p210.Picture(); p010.Picture();
        PackedRefinementPixels packed(p210.Source());
        LocalBoundaryDiagnosticComparison a, b, c;
        const auto context = RefinementInput().context;
        const auto x = a.Observe(p210.Source(), context);
        const auto y = b.Observe(p010.Source(), context);
        const auto z = c.Observe(packed.Source(), context);
        Assert::IsTrue(x.refined.refinementApplied && y.refined.refinementApplied && z.refined.refinementApplied);
        Assert::AreEqual(x.refined.observation.top, y.refined.observation.top);
        Assert::AreEqual(x.refined.observation.bottom, y.refined.observation.bottom);
        Assert::AreEqual(x.refined.observation.top, z.refined.observation.top);
        Assert::AreEqual(x.refined.observation.bottom, z.refined.observation.bottom);
        Assert::IsTrue(x.refined.observation.bottomEdge.supportMask == y.refined.observation.bottomEdge.supportMask);
        Assert::IsTrue(x.refined.observation.bottomEdge.supportMask == z.refined.observation.bottomEdge.supportMask);
    }

    TEST_METHOD(NarrowMovingOpposingEdgeAcquiresOnlyAfterRealSpatialCoverage)
    {
        RefinementPixels pixels;
        SparseBoundaryCropExperiment experiment;
        auto input = RefinementInput();
        SparseBoundaryCropExperimentResult result;
        for (int phase = 0; phase < 14; ++phase)
        {
            pixels.Picture(phase);
            const auto source = pixels.Source();
            const auto coarse = SampleLocalBoundaryDiagnostics(source);
            Assert::IsTrue(coarse.candidateAvailable);
            Assert::IsTrue(coarse.bottom < 1880 - 100);
            Assert::IsTrue(coarse.bottomExcludedViolations > 0,
                L"The old grid sees the narrow picture but cannot locate its bottom; clipping it must remain forbidden.");
            const auto raw = ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification == ActivePictureClassification::PROVISIONAL);
            result = experiment.Observe(source, raw, input);
            if (phase < 12)
                Assert::IsFalse(result.candidateAvailable,
                    L"Dense neighboring pixels cannot pretend to be independent coarse-width spatial coverage.");
            ++input.context.acceptedSequence;
            input.context.timestampMs += 200;
        }
        Assert::IsTrue(result.candidateAvailable,
            L"A source-connected narrow bottom moving across13 real spatial bins should mature startup evidence.");
        Assert::IsTrue(result.referenceBounds.top <= 280 && result.referenceBounds.bottom >= 1880);
        Assert::AreEqual(0, result.referenceBounds.left);
        Assert::AreEqual(3840, result.referenceBounds.right);
        Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT), int(result.evidence.authorityOrigin));
    }

    TEST_METHOD(DenseSourceRunRefinesEitherOpposingEdgeWithoutInflatingCoverage)
    {
        for (bool weakTop : { false, true })
            for (int inset : { 216, 280, 344 })
            {
                RefinementPixels pixels;
                pixels.Picture(0, weakTop, inset);
                const auto source = pixels.Source();
                const auto coarse = SampleLocalBoundaryDiagnostics(source);
                const auto refined = RefinePixels(source);
                Assert::IsTrue(refined.evaluated && refined.applied);
                Assert::IsTrue(refined.sourceSamples > 0 && refined.sourceSamples <= LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES);
                const auto& edge = weakTop ? refined.observation.topEdge : refined.observation.bottomEdge;
                Assert::AreEqual(size_t(1), edge.refinedMask.count());
                Assert::AreEqual(size_t(1), edge.supportMask.count());
                Assert::AreEqual(1, edge.supportedSamples);
                Assert::IsTrue(edge.refinedMinimumRun >= 7);
                Assert::IsTrue(edge.inwardContinuity >= 0.75);
                Assert::IsTrue(refined.observation.sampledBarsClean);
                Assert::AreEqual(size_t(0), refined.observation.excludedViolations);
                Assert::IsTrue(refined.observation.ambiguous);
                Assert::AreEqual(coarse.fingerprint, refined.observation.fingerprint);
                Assert::IsTrue(refined.observation.top >= inset && refined.observation.top <= inset + coarse.rowStep);
                Assert::IsTrue(refined.observation.bottom <= pixels.height - inset &&
                    refined.observation.bottom >= pixels.height - inset - coarse.rowStep);
            }
    }

    TEST_METHOD(IsolatedStarsAndThinHorizontalMarksCannotBecomeEdges)
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            RefinementPixels pixels;
            pixels.Picture(0, false, 280, kind == 0 ? 2 : 16);
            if (kind == 1)
            {
                // Keep one boundary-row stripe, remove every inward sample.
                pixels.Rectangle(440, 1840, 468, 1878, 64);
            }
            if (kind == 2)
            {
                // A stable top alone nominates a place to inspect, not a bottom.
                pixels.Rectangle(440, 1760, 468, 1880, 64);
            }
            const auto refined = RefinePixels(pixels.Source());
            Assert::IsFalse(refined.applied);
            Assert::IsTrue(refined.sourceSamples <= LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES);
        }
    }

    TEST_METHOD(ActualAsymmetryAndTwoWeakEdgesDoNotInventAReflectedAperture)
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            RefinementPixels pixels;
            pixels.Picture();
            if (kind == 0) pixels.Rectangle(440, 1820, 468, 1880, 64);
            if (kind == 1)
            {
                // Remove both strong boundary regions; only the narrow detail
                // reaches either genuine edge, so there is no strong nominee.
                pixels.Rectangle(2160, 280, 3600, 376, 64);
                pixels.Rectangle(0, 1440, 300, 1760, 64);
            }
            if (kind == 2)
            {
                // Full-raster occupied margins are not bars, even when the
                // center happens to contain a matching dark composition.
                pixels.Rectangle(0, 0, pixels.width, 32, 144, 485, 528);
                pixels.Rectangle(0, pixels.height - 32, pixels.width, pixels.height, 144, 485, 528);
            }
            const auto refined = RefinePixels(pixels.Source());
            Assert::IsFalse(refined.applied);
        }
    }

    TEST_METHOD(CurrentExteriorLumaOrChromaObstructionPreventsRefinement)
    {
        for (int kind = 0; kind < 2; ++kind)
        {
            RefinementPixels pixels;
            pixels.Picture();
            const int x = pixels.X(80) & ~1;
            // An isolated coarse-column mark avoids masquerading as a new
            // three-column picture edge but still contaminates the exterior.
            pixels.Rectangle(x - 2, 96, x + 4, 120, kind ? 64 : 200, kind ? 560 : 512);
            const auto refined = RefinePixels(pixels.Source());
            Assert::IsFalse(refined.applied);
            Assert::IsTrue(refined.observation.excludedViolations > 0);
        }
    }

    TEST_METHOD(PlanarConversionAndPackedV210KeepTheSameRefinedSourceCoordinates)
    {
        for (bool weakTop : { false, true })
        {
            RefinementPixels p210(1920, 1080), p010(1920, 1080, false);
            p210.Picture(0, weakTop, 144, 8);
            p010.Picture(0, weakTop, 144, 8);
            PackedRefinementPixels native(p210.Source());
            const auto expected = RefinePixels(p210.Source());
            Assert::IsTrue(expected.applied);
            for (const auto& source : { p010.Source(), native.Source() })
            {
                const auto result = RefinePixels(source);
                Assert::IsTrue(result.applied);
                Assert::AreEqual(expected.observation.top, result.observation.top);
                Assert::AreEqual(expected.observation.bottom, result.observation.bottom);
                Assert::AreEqual(expected.sourceSamples, result.sourceSamples);
                Assert::IsTrue(expected.observation.topEdge.supportMask == result.observation.topEdge.supportMask);
                Assert::IsTrue(expected.observation.bottomEdge.supportMask == result.observation.bottomEdge.supportMask);
            }
        }
    }

    TEST_METHOD(RefinementIsBoundedAndCannotMutateSourceOrNativeEvidence)
    {
        RefinementPixels pixels;
        pixels.Picture();
        const auto before = pixels.pixels;
        const auto source = pixels.Source();
        const auto raw = ExtractActivePictureEvidence(source);
        const auto coarse = SampleLocalBoundaryDiagnostics(source);
        const auto refined = RefinePixels(source);
        Assert::IsTrue(refined.applied);
        Assert::IsTrue(before == pixels.pixels);
        const auto after = ExtractActivePictureEvidence(source);
        Assert::AreEqual(int(raw.classification), int(after.classification));
        Assert::AreEqual(raw.proposedBounds.top, after.proposedBounds.top);
        Assert::AreEqual(raw.proposedBounds.bottom, after.proposedBounds.bottom);
        Assert::IsTrue(raw.axisEvidence == after.axisEvidence);
        for (int invalid = 0; invalid < 4; ++invalid)
        {
            auto broken = source;
            if (invalid == 0) broken.dataBytes = 8;
            if (invalid == 1) broken.data = nullptr;
            if (invalid == 2) broken.encoding = VideoFrameEncoding::UNKNOWN;
            if (invalid == 3) broken.encoding = VideoFrameEncoding::UYVY;
            const auto result = RefineLocalBoundaryEdges(broken, SampleActivePictureDiagnosticGrid(source), coarse);
            Assert::IsFalse(result.applied);
            Assert::IsTrue(result.sourceSamples <= LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES);
            Assert::AreEqual(coarse.top, result.observation.top);
            Assert::AreEqual(coarse.bottom, result.observation.bottom);
        }
    }

    TEST_METHOD(ExhaustedProbeBudgetReturnsUnchangedCoarseObservation)
    {
        RefinementPixels pixels;
        pixels.Picture();
        pixels.Rectangle(0, 1760, pixels.width, 1880, 64);
        for (int column = 2; column < 127; column += 2)
        {
            const int x = (pixels.X(column) - 4) & ~1;
            pixels.Rectangle(x, 1840, x + 8, 1880, 144, 485, 528);
        }
        const auto source = pixels.Source();
        const auto coarse = SampleLocalBoundaryDiagnostics(source);
        const auto result = RefinePixels(source);
        Assert::IsTrue(result.evaluated);
        Assert::IsFalse(result.applied);
        Assert::IsTrue(result.sourceSamples > 7000 && result.sourceSamples <= LOCAL_BOUNDARY_REFINEMENT_MAX_SAMPLES);
        Assert::IsTrue(std::string(result.reason) == "refinement-budget-exhausted");
        Assert::AreEqual(coarse.top, result.observation.top);
        Assert::AreEqual(coarse.bottom, result.observation.bottom);
        Assert::IsTrue(result.observation.bottomEdge.refinedMask.none());
    }

    TEST_METHOD(SourceChromaCanSupplyARealConnectedEdgeWithoutLumaContrast)
    {
        RefinementPixels pixels;
        pixels.Picture();
        const int x = (pixels.X(15) - 8) & ~1;
        pixels.Rectangle(x, 1760, x + 16, 1880, 64, 485, 528);
        const auto refined = RefinePixels(pixels.Source());
        Assert::IsTrue(refined.applied);
        Assert::AreEqual(0, refined.observation.bottomEdge.lumaOnlySamples);
        Assert::AreEqual(1, refined.observation.bottomEdge.colorOnlySamples);
        Assert::AreEqual(0, refined.observation.bottomEdge.lumaAndColorSamples);
        Assert::IsTrue(refined.observation.bottomEdge.maxChromaDelta >= 8);
        Assert::AreEqual(0, refined.observation.bottomEdge.maxLumaDelta);
    }

    TEST_METHOD(PausedNoiseAndSubBinMotionCannotManufactureTemporalCoverage)
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            RefinementPixels pixels;
            SparseBoundaryCropExperiment experiment;
            auto input = RefinementInput();
            for (int frame = 0; frame < 18; ++frame)
            {
                pixels.Picture(0, false, 280, kind == 2 ? 16 + 2 * (frame % 2) : 16);
                if (kind == 1) pixels.Rectangle(1000, 1000, 1010, 1008, 300 + frame);
                const auto source = pixels.Source();
                const auto result = experiment.Observe(source, ExtractActivePictureEvidence(source), input);
                Assert::IsFalse(result.candidateAvailable || result.referenceAvailable);
                AdvanceRefinement(input);
            }
        }
    }

    TEST_METHOD(RepeatedFramesAndMovingVerticalEndpointsCannotMatureAnAnchor)
    {
        for (int kind = 0; kind < 4; ++kind)
        {
            RefinementPixels pixels;
            SparseBoundaryCropExperiment experiment;
            auto input = RefinementInput();
            for (int phase = 0; phase < 15; ++phase)
            {
                pixels.Picture(phase, false, kind == 2 ? 216 + phase * 12 : 280);
                input.context.cadenceRepeat = kind == 1;
                if (kind == 3 && phase % 3 == 0) ++input.context.sceneGeneration;
                const auto source = pixels.Source();
                const auto result = experiment.Observe(source, ExtractActivePictureEvidence(source), input);
                Assert::IsFalse(result.candidateAvailable || result.referenceAvailable);
                input.context.timestampMs += 200;
                if (kind != 0) ++input.context.acceptedSequence;
            }
        }
    }

    TEST_METHOD(WeakTopUsesTheSameMovingEvidenceAndDefaultOffRemainsUnchanged)
    {
        for (bool enabled : { false, true })
        {
            RefinementPixels pixels;
            SparseBoundaryCropExperiment experiment;
            auto input = RefinementInput();
            input.enabled = enabled;
            SparseBoundaryCropExperimentResult result;
            for (int phase = 0; phase < 14; ++phase)
            {
                pixels.Picture(phase, true);
                const auto source = pixels.Source();
                const auto raw = ExtractActivePictureEvidence(source);
                result = experiment.Observe(source, raw, input);
                if (!enabled)
                {
                    Assert::IsFalse(result.candidateAvailable || result.discoverySampled);
                    Assert::AreEqual(int(raw.classification), int(result.evidence.classification));
                    Assert::AreEqual(raw.proposedBounds.top, result.evidence.proposedBounds.top);
                    Assert::AreEqual(raw.proposedBounds.bottom, result.evidence.proposedBounds.bottom);
                }
                AdvanceRefinement(input);
            }
            Assert::AreEqual(enabled, result.candidateAvailable);
        }
    }

    TEST_METHOD(MovingNearBlackWitnessesCannotBypassOrdinaryAcquisitionGuard)
    {
        RefinementPixels pixels;
        SparseBoundaryCropExperiment experiment;
        ActivePictureTransitionModel model;
        auto input = RefinementInput();
        bool measuredReference = false;
        for (int phase = 0; phase < 15; ++phase)
        {
            pixels.Picture(phase);
            pixels.Rectangle(0, pixels.height / 3, pixels.width, pixels.height * 2 / 3, 64);
            const auto source = pixels.Source();
            const auto darkness = EvaluateActivePictureGlobalNearBlack(source);
            Assert::IsTrue(darkness.evaluated && darkness.nearBlack);
            const auto result = experiment.Observe(source, ExtractActivePictureEvidence(source), input);
            measuredReference = measuredReference || result.referenceAvailable;
            Assert::IsFalse(result.candidateAvailable);
            const auto constrained = ConstrainNearBlackCropAcquisition(result.evidence, darkness.nearBlack);
            Assert::IsFalse(model.Observe(MakeActivePictureObservation(constrained,
                input.context.acceptedSequence, 25)).publish);
            AdvanceRefinement(input);
        }
        Assert::IsTrue(measuredReference, L"Measurements may improve while the independent near-black crop veto remains in force.");
    }

    TEST_METHOD(CurrentCaptionBetweenDiscoverySamplesWithdrawsRefinedReference)
    {
        RefinementPixels pixels;
        SparseBoundaryCropExperiment experiment;
        auto input = RefinementInput();
        SparseBoundaryCropExperimentResult result;
        for (int phase = 0; phase < 14; ++phase)
        {
            pixels.Picture(phase);
            const auto source = pixels.Source();
            result = experiment.Observe(source, ExtractActivePictureEvidence(source), input);
            if (phase != 13) AdvanceRefinement(input);
        }
        Assert::IsTrue(result.candidateAvailable);
        ++input.context.acceptedSequence;
        input.context.timestampMs += 40;
        pixels.Rectangle(960, 1960, 2880, 2040, 700);
        const auto source = pixels.Source();
        const auto caption = experiment.Observe(source, ExtractActivePictureEvidence(source), input);
        Assert::IsFalse(caption.discoverySampled);
        Assert::IsFalse(caption.candidateAvailable || caption.referenceAvailable);
        Assert::IsTrue(caption.reset);
        Assert::IsTrue(caption.fixedExterior.bottomViolations > 0);
    }

    TEST_METHOD(EligibleTransitionAndDefaultShadowSessionDoNotRunStartupRefinement)
    {
        RefinementPixels pixels;
        pixels.Rectangle(0, 0, pixels.width, pixels.height, 300);
        const auto native = ExtractActivePictureEvidence(pixels.Source());
        Assert::IsTrue(native.classification == ActivePictureClassification::FULL_RASTER_TRUSTED);
        ActivePictureTransitionModel model;
        bool nativePublished = false;
        for (uint64_t sequence = 1; sequence <= 4; ++sequence)
            nativePublished = model.Observe(MakeActivePictureObservation(native, sequence, 25)).publish || nativePublished;
        Assert::IsTrue(nativePublished);
        auto input = RefinementInput();
        input.startupEligible = false;
        input.transitionEligible = true;
        input.establishedBase = native.trustedBounds;
        input.establishedBaseSourceGeneration = pixels.Source().generation;
        input.establishedBaseOrigin = ActivePictureAuthorityOrigin::NATIVE;
        SparseBoundaryCropExperiment experiment;
        LocalBoundaryDiagnosticSession shadow;
        size_t actualDiscoveryCount = 0;
        for (int phase = 0; phase < 14; ++phase)
        {
            pixels.Picture(phase);
            const auto source = pixels.Source();
            const auto raw = ExtractActivePictureEvidence(source);
            Assert::IsTrue(raw.classification == ActivePictureClassification::PROVISIONAL);
            Assert::IsTrue(raw.axisEvidence.horizontal.scanComplete);
            Assert::IsFalse(raw.axisEvidence.horizontal.barCandidate || raw.axisEvidence.horizontal.FailedBar());
            Assert::AreEqual(0, raw.proposedBounds.left);
            Assert::AreEqual(pixels.width, raw.proposedBounds.right);
            Assert::IsFalse(EvaluateActivePictureGlobalNearBlack(source).nearBlack);
            const auto coarse = SampleLocalBoundaryDiagnostics(source);
            const auto result = experiment.Observe(source, raw, input);
            actualDiscoveryCount += result.discoverySampled ? 1 : 0;
            Assert::IsTrue(result.discoverySampled, L"The mode gate must be exercised after genuinely eligible side evidence.");
            Assert::IsFalse(result.diagnostic.refinementEvaluated || result.diagnostic.refinementApplied);
            Assert::AreEqual(size_t(0), result.diagnostic.refinementSamples);
            Assert::IsFalse(result.candidateAvailable || result.referenceAvailable);
            Assert::AreEqual(coarse.bottom, result.diagnostic.observation.bottom);
            const auto observed = shadow.Observe(source, input.context);
            Assert::IsFalse(observed.refinementEvaluated || observed.refinementApplied || observed.anchor.active);
            Assert::AreEqual(size_t(0), observed.refinementSamples);
            Assert::AreEqual(coarse.top, observed.observation.top);
            Assert::AreEqual(coarse.bottom, observed.observation.bottom);
            Assert::IsTrue(observed.observation.bottomEdge.refinedMask.none());
            AdvanceRefinement(input);
        }
        Assert::AreEqual(size_t(14), actualDiscoveryCount);
    }
};
}
