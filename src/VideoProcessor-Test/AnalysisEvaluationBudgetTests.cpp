#include "pch.h"
#include <ActivePictureEvidence.h>
#include <AnalysisEvaluationBudget.h>
#include "CppUnitTest.h"
#include <algorithm>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        struct OwnedMeasurementFrame
        {
            std::shared_ptr<const std::vector<uint8_t>> pixels;
            int width = 320, height = 180;
            size_t pitch = 656;

            // Construct each immutable backing before handing it to the cache.
            explicit OwnedMeasurementFrame(int pattern = 0)
            {
                std::vector<uint8_t> bytes(pitch * height * 2 + 32, 0);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const bool bar = pattern == 0 && (y < 22 || y >= 158);
                        const int code = pattern == 1 || bar ? 64 :
                            300 + (x % 16) * 4 + (y % 8) * 3;
                        Write(bytes, static_cast<size_t>(y) * pitch + x * 2, code);
                    }
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        Write(bytes, pitch * height + static_cast<size_t>(y) * pitch + x * 2, 512);
                pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
            }

            static void Write(std::vector<uint8_t>& bytes, size_t offset, int code)
            {
                const auto packed = static_cast<uint16_t>(code << 6);
                bytes[offset] = static_cast<uint8_t>(packed);
                bytes[offset + 1] = static_cast<uint8_t>(packed >> 8);
            }

            AnalysisLumaSource Source() const
            {
                AnalysisLumaSource source{ pixels->data(), pixels->size(), width, height,
                    pitch, pitch, AnalysisLumaFormat::P010, VideoFrameEncoding::V210,
                    ColorSpace::REC_709, 17 };
                source.backingOwner = pixels;
                source.range = AnalysisLumaRange::Limited;
                return source;
            }
        };

        ActivePictureBounds Scope()
        {
            return { 0, 22, 320, 158, 320, 180, 320.0 / 136.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM };
        }

#define CHECK_FIELD(field) Assert::IsTrue(actual.field == expected.field)
        void SameBounds(const ActivePictureBounds& actual, const ActivePictureBounds& expected)
        {
            CHECK_FIELD(left); CHECK_FIELD(top); CHECK_FIELD(right); CHECK_FIELD(bottom);
            CHECK_FIELD(rasterWidth); CHECK_FIELD(rasterHeight); CHECK_FIELD(aspectRatio);
            CHECK_FIELD(trustedBarAxes);
        }

        void SameEdge(const ActivePictureEdgeEvidence& actual, const ActivePictureEdgeEvidence& expected)
        {
            CHECK_FIELD(barPixels); CHECK_FIELD(distributedSupportDepthRowsByQuartile);
            CHECK_FIELD(blackFraction); CHECK_FIELD(lumaFloor); CHECK_FIELD(lumaP90);
            CHECK_FIELD(lumaDispersion); CHECK_FIELD(texture); CHECK_FIELD(neutralChromaFraction);
            CHECK_FIELD(innerBoundaryContrast); CHECK_FIELD(continuity); CHECK_FIELD(confidence);
            CHECK_FIELD(trusted);
        }

        void SamePicture(const ActivePictureEvidence& actual, const ActivePictureEvidence& expected)
        {
            CHECK_FIELD(available); CHECK_FIELD(classification); CHECK_FIELD(axisEvidence);
            CHECK_FIELD(lumaSamples); CHECK_FIELD(chromaSamples); CHECK_FIELD(reason);
            CHECK_FIELD(authorityOrigin);
            SameBounds(actual.proposedBounds, expected.proposedBounds);
            SameBounds(actual.trustedBounds, expected.trustedBounds);
            SameEdge(actual.left, expected.left); SameEdge(actual.top, expected.top);
            SameEdge(actual.right, expected.right); SameEdge(actual.bottom, expected.bottom);
            CHECK_FIELD(verticalBarProfile.evaluated); CHECK_FIELD(verticalBarProfile.completed);
            CHECK_FIELD(verticalBarProfile.clean); CHECK_FIELD(verticalBarProfile.samples);
            CHECK_FIELD(verticalBarGuardProfile.evaluated); CHECK_FIELD(verticalBarGuardProfile.clean);
        }

        void SameGlobal(const ActivePictureGlobalNearBlackEvidence& actual,
            const ActivePictureGlobalNearBlackEvidence& expected)
        {
            CHECK_FIELD(evaluated); CHECK_FIELD(lumaP90); CHECK_FIELD(lumaCutoff);
            CHECK_FIELD(nearBlack); CHECK_FIELD(lumaSamples);
        }

        void SameRetention(const ActivePicturePresentationRetentionEvidence& actual,
            const ActivePicturePresentationRetentionEvidence& expected)
        {
            CHECK_FIELD(analysisValid); CHECK_FIELD(presentationValid); CHECK_FIELD(globalLumaP90);
            CHECK_FIELD(globalNearBlack); CHECK_FIELD(distributedDarkPicture);
            CHECK_FIELD(proposedBoundsAvailable); CHECK_FIELD(proposedBoundsContained);
            CHECK_FIELD(samplingReaffirmed); CHECK_FIELD(partialSamplingEvaluated);
            CHECK_FIELD(partialSamplingReaffirmed); CHECK_FIELD(samplingEquivalent);
            CHECK_FIELD(samplingStripPeakY); CHECK_FIELD(samplingStripPeakChromaDelta);
            CHECK_FIELD(samplingStripConflict); CHECK_FIELD(presentationBlackFloor);
            CHECK_FIELD(presentationBlackThreshold); CHECK_FIELD(relocatedOverlayExclusionEvaluated);
            CHECK_FIELD(relocatedOverlayResidualsExcluded); CHECK_FIELD(excludedBandsPixelSafe);
            CHECK_FIELD(excludedHorizontalBandsPixelSafe); CHECK_FIELD(excludedVerticalBandsPixelSafe);
            CHECK_FIELD(expansionStripsAvailable); CHECK_FIELD(currentlyPixelSafe);
            CHECK_FIELD(outwardVisibleBoundsAvailable); CHECK_FIELD(lumaSamples); CHECK_FIELD(chromaSamples);
            CHECK_FIELD(reason);
            Assert::IsTrue(actual.CanRetainPresentation() == expected.CanRetainPresentation());
            SameBounds(actual.expansionBase, expected.expansionBase);
            SameBounds(actual.expansionCandidate, expected.expansionCandidate);
            SameBounds(actual.outwardVisibleBounds, expected.outwardVisibleBounds);
            SamePicture(actual.activePicture, expected.activePicture);
            SameEdge(actual.excludedLeft, expected.excludedLeft); SameEdge(actual.excludedTop, expected.excludedTop);
            SameEdge(actual.excludedRight, expected.excludedRight); SameEdge(actual.excludedBottom, expected.excludedBottom);
            SameEdge(actual.expandingLeft, expected.expandingLeft); SameEdge(actual.expandingTop, expected.expandingTop);
            SameEdge(actual.expandingRight, expected.expandingRight); SameEdge(actual.expandingBottom, expected.expandingBottom);
        }
#undef CHECK_FIELD
        struct CancellationClock
        {
            size_t calls = 0, stop = 0;
            static bool Expired(void* context)
            {
                auto& clock = *static_cast<CancellationClock*>(context);
                ++clock.calls;
                return clock.stop && clock.calls >= clock.stop;
            }
        };
        template<typename Action>
        void RequireCancelled(Action action)
        {
            bool cancelled = false;
            try { action(); }
            catch (const AnalysisEvaluationCancelled&) { cancelled = true; }
            Assert::IsTrue(cancelled,L"Incomplete optional proof must unwind instead of becoming pixel evidence.");
        }
    }

    TEST_CLASS(AnalysisEvaluationBudgetTests)
    {
    public:
        TEST_METHOD(ExplicitBudgetCancellationIsStickyAndNeverReadsClockAgain)
        {
            CancellationClock clock{0,2};
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            budget.Checkpoint();
            RequireCancelled([&] { budget.Checkpoint(); });
            Assert::IsTrue(budget.Cancelled());
            clock.stop = 0;
            RequireCancelled([&] { budget.Checkpoint(); });
            Assert::AreEqual(size_t{2},clock.calls);
            CheckAnalysisEvaluationBudget(nullptr);
        }

        TEST_METHOD(NonExpiredBudgetPreservesRawGlobalAndDarknessEvidence)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            CancellationClock clock;
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            const auto raw = ExtractActivePictureEvidence(source);
            SamePicture(ExtractActivePictureEvidence(source,&budget),raw);
            const auto global = EvaluateActivePictureGlobalNearBlack(source);
            SameGlobal(EvaluateActivePictureGlobalNearBlack(source,&budget),global);
            const auto expected = EvaluateActivePictureArDarkness(source,raw,global);
            const auto actual = EvaluateActivePictureArDarkness(source,raw,global,&budget);
            SameGlobal(actual.global,expected.global);
            Assert::IsTrue(actual.distributedDarkPicture == expected.distributedDarkPicture);
            Assert::AreEqual(expected.lumaSamples,actual.lumaSamples);
            Assert::IsTrue(clock.calls > 2 && !budget.Cancelled());
        }

        TEST_METHOD(CancelledRawExtractionCannotReturnBlackOrPartialAuthorityAndMandatoryRetryWorks)
        {
            const OwnedMeasurementFrame frame(2);
            const auto source = frame.Source();
            const auto expected = ExtractActivePictureEvidence(source);
            Assert::IsTrue(expected.available);
            CancellationClock clock{0,4};
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            ActivePictureEvidence result;
            RequireCancelled([&] { result = ExtractActivePictureEvidence(source,&budget); });
            Assert::IsFalse(result.available);
            Assert::IsTrue(result.classification == ActivePictureClassification::UNAVAILABLE);
            Assert::AreEqual(size_t{4},clock.calls);
            SamePicture(ExtractActivePictureEvidence(source),expected);
        }

        TEST_METHOD(CancelledGlobalGridCannotReturnNearBlackEvidenceAndMandatoryRetryWorks)
        {
            const OwnedMeasurementFrame frame(2);
            const auto source = frame.Source();
            CancellationClock clock{0,3};
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            ActivePictureGlobalNearBlackEvidence result;
            RequireCancelled([&] { result = EvaluateActivePictureGlobalNearBlack(source,&budget); });
            Assert::IsFalse(result.evaluated);
            SameGlobal(EvaluateActivePictureGlobalNearBlack(source),
                EvaluateActivePictureGlobalNearBlack(source,nullptr));
        }

        TEST_METHOD(SuppliedExactMeasurementsPreserveAllRetentionValuesAndSampleTotals)
        {
            for (int pattern = 0; pattern < 3; ++pattern)
            {
                const OwnedMeasurementFrame frame(pattern);
                const auto source = frame.Source();
                const auto raw = ExtractActivePictureEvidence(source);
                const auto global = EvaluateActivePictureGlobalNearBlack(source);
                const auto darkness = EvaluateActivePictureArDarkness(source,raw,global);
                for (int inset : {0,2,8})
                {
                    auto base = Scope();base.top += inset;base.bottom -= inset;
                    base.aspectRatio = double(base.right-base.left)/(base.bottom-base.top);
                    SameRetention(EvaluateActivePicturePresentationRetention(source,base,raw,global,darkness),
                        EvaluateActivePicturePresentationRetention(source,base));
                }
            }
        }

        TEST_METHOD(SuppliedRefinedGeometryWithoutCalibrationRebuildsOnlyCurrentReference)
        {
            const OwnedMeasurementFrame frame;
            for (const auto range : {AnalysisLumaRange::Limited,AnalysisLumaRange::Full})
            {
                auto source = frame.Source(); source.range = range;
                const auto raw = ExtractActivePictureEvidence(source);
                const auto global = EvaluateActivePictureGlobalNearBlack(source);
                const auto darkness = EvaluateActivePictureArDarkness(source,raw,global);
                auto refined = raw;
                refined.blackReference = {};
                refined.reason = "independently refined geometry";
                const auto expected = EvaluateActivePicturePresentationRetention(source,Scope(),raw,global,darkness);
                const auto actual = EvaluateActivePicturePresentationRetention(source,Scope(),refined,global,darkness);
                Assert::IsTrue(actual.analysisValid && actual.blackReference.evaluated);
                Assert::AreEqual(expected.presentationBlackFloor,actual.presentationBlackFloor);
                Assert::AreEqual(expected.presentationBlackThreshold,actual.presentationBlackThreshold);
                Assert::AreEqual(expected.excludedBandsPixelSafe,actual.excludedBandsPixelSafe);
                SameBounds(refined.trustedBounds,actual.activePicture.trustedBounds);
                Assert::AreEqual(refined.reason,actual.activePicture.reason);
                Assert::AreEqual(expected.lumaSamples+raw.lumaSamples,actual.lumaSamples);
                Assert::AreEqual(expected.chromaSamples+raw.chromaSamples,actual.chromaSamples);
                CancellationClock clock{0,3};
                AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
                ActivePicturePresentationRetentionEvidence incomplete;
                RequireCancelled([&] { incomplete = EvaluateActivePicturePresentationRetention(
                    source,Scope(),refined,global,darkness,&budget); });
                Assert::IsFalse(incomplete.analysisValid || incomplete.excludedBandsPixelSafe);
            }
        }

        TEST_METHOD(CancelledSuppliedRetentionCannotPublishPartialSafetyAndMandatoryRetryWorks)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            const auto raw = ExtractActivePictureEvidence(source);
            const auto global = EvaluateActivePictureGlobalNearBlack(source);
            const auto darkness = EvaluateActivePictureArDarkness(source,raw,global);
            const auto expected = EvaluateActivePicturePresentationRetention(source,Scope());
            CancellationClock clock{0,4};
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            ActivePicturePresentationRetentionEvidence result;
            RequireCancelled([&] {
                result = EvaluateActivePicturePresentationRetention(source,Scope(),raw,global,darkness,&budget);
            });
            Assert::IsFalse(result.analysisValid);
            Assert::IsFalse(result.excludedBandsPixelSafe);
            Assert::IsFalse(result.outwardVisibleBoundsAvailable);
            Assert::IsFalse(result.CanRetainPresentation());
            SameRetention(EvaluateActivePicturePresentationRetention(source,Scope(),raw,global,darkness),expected);
            SamePicture(raw,ExtractActivePictureEvidence(source));
        }

        TEST_METHOD(DenseVisibleExtentChecksBudgetBeforeReturningCompletedRetention)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            const auto raw = ExtractActivePictureEvidence(source);
            const auto global = EvaluateActivePictureGlobalNearBlack(source);
            const auto darkness = EvaluateActivePictureArDarkness(source,raw,global);
            const auto expected = EvaluateActivePicturePresentationRetention(source,Scope());
            Assert::IsTrue(expected.analysisValid && expected.excludedBandsPixelSafe);
            // The old direct-sampling paths made only about 26 checkpoints and
            // returned a completed proof. Forty reaches the first dense visible
            // extent only when its actual pixel reads honor the shared budget.
            CancellationClock clock{0,40};
            AnalysisEvaluationBudget budget(&CancellationClock::Expired,&clock);
            ActivePicturePresentationRetentionEvidence result;
            RequireCancelled([&] {
                result = EvaluateActivePicturePresentationRetention(
                    source,Scope(),raw,global,darkness,&budget);
            });
            Assert::AreEqual(size_t{40},clock.calls);
            Assert::IsFalse(result.analysisValid);
            Assert::IsFalse(result.excludedBandsPixelSafe);
            Assert::IsFalse(result.outwardVisibleBoundsAvailable);
            Assert::IsFalse(result.CanRetainPresentation());
            SameRetention(EvaluateActivePicturePresentationRetention(
                source,Scope(),raw,global,darkness),expected);
        }

        TEST_METHOD(SuppliedRetentionKeepsInvalidSourceAndBaseBehavior)
        {
            const OwnedMeasurementFrame frame;
            for (bool invalidSource : {false,true})
            {
                auto source = frame.Source();
                if (invalidSource) source.data = nullptr;
                const auto raw = ExtractActivePictureEvidence(source);
                const auto global = EvaluateActivePictureGlobalNearBlack(source);
                const auto darkness = EvaluateActivePictureArDarkness(source,raw,global);
                auto base = Scope();base.rasterWidth += 2;
                SameRetention(EvaluateActivePicturePresentationRetention(source,base,raw,global,darkness),
                    EvaluateActivePicturePresentationRetention(source,base));
            }
        }
    };
}
