#include "pch.h"
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>
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
                    ColorSpace::REC_709, 1 };
                source.backingOwner = pixels;
                source.range = AnalysisLumaRange::Limited;
                return source;
            }
        };

        ActivePictureFrameIdentity Identity()
        {
            return { 1, 42, 1042, 17500000, 2, 3, 4, 5 };
        }

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

        void SameSideProbe(const ActivePictureSideProbe& actual, const ActivePictureSideProbe& expected)
        {
            CHECK_FIELD(evaluated);
            for (size_t i = 0; i < actual.cells.size(); ++i)
            {
                CHECK_FIELD(cells[i].strong); CHECK_FIELD(cells[i].nonBlack);
                CHECK_FIELD(cells[i].meanLuma); CHECK_FIELD(cells[i].peakLuma);
            }
        }

        void SameVerticalProfile(const ActivePictureVerticalBarProfile& actual,
            const ActivePictureVerticalBarProfile& expected)
        {
            CHECK_FIELD(evaluated); CHECK_FIELD(completed); CHECK_FIELD(clean);
            CHECK_FIELD(apertureTop); CHECK_FIELD(apertureBottom); CHECK_FIELD(samples);
            CHECK_FIELD(referenceY); CHECK_FIELD(referenceU); CHECK_FIELD(referenceV);
            CHECK_FIELD(firstMismatchX); CHECK_FIELD(firstMismatchY); CHECK_FIELD(maxLumaDelta);
            CHECK_FIELD(maxChromaDelta); CHECK_FIELD(mismatchSamples); CHECK_FIELD(boundaryMismatchSamples);
            CHECK_FIELD(deepMismatchSamples); CHECK_FIELD(topFirstMismatchY); CHECK_FIELD(topLastMismatchY);
            CHECK_FIELD(bottomFirstMismatchY); CHECK_FIELD(bottomLastMismatchY); CHECK_FIELD(reason);
        }

        void SameNomination(const RememberedEdgeReturnNomination& actual,
            const RememberedEdgeReturnNomination& expected)
        {
            CHECK_FIELD(available); CHECK_FIELD(historyId); CHECK_FIELD(historyRevision);
            CHECK_FIELD(sourceGeneration); CHECK_FIELD(confirmedSceneCount);
            CHECK_FIELD(lastIndependentNativeSequence); CHECK_FIELD(lastIndependentNativeTickMs);
            CHECK_FIELD(matchedEdge); CHECK_FIELD(observedEdgeCoordinate); CHECK_FIELD(sceneId);
            CHECK_FIELD(sourceSequence); CHECK_FIELD(timestampMs); CHECK_FIELD(rendererGeneration);
            CHECK_FIELD(viewportGeneration); CHECK_FIELD(sourceFormatGeneration); CHECK_FIELD(policyGeneration);
            CHECK_FIELD(shadowOnly); CHECK_FIELD(guarded);
            SameBounds(actual.establishedBase, expected.establishedBase);
            SameBounds(actual.rememberedBounds, expected.rememberedBounds);
        }
        void SamePhysical(const PhysicalPictureEdgeObservation& actual,const PhysicalPictureEdgeObservation& expected)
        {
            CHECK_FIELD(evaluated);CHECK_FIELD(estimateAvailable);CHECK_FIELD(confidence);
            CHECK_FIELD(sourceGeneration);CHECK_FIELD(samples);
            SameBounds(actual.estimate,expected.estimate);SameBounds(actual.conservativeBounds,expected.conservativeBounds);
            Assert::IsTrue(std::string(actual.reason?actual.reason:"")==std::string(expected.reason?expected.reason:""));
        }
        void SamePicture(const ActivePictureEvidence& actual, const ActivePictureEvidence& expected)
        {
            CHECK_FIELD(available); CHECK_FIELD(classification); CHECK_FIELD(axisEvidence);CHECK_FIELD(detector);
            CHECK_FIELD(sourceCandidateVerificationComplete);
            SamePhysical(actual.physicalEdges,expected.physicalEdges);
            CHECK_FIELD(lumaSamples); CHECK_FIELD(chromaSamples); CHECK_FIELD(reason);
            CHECK_FIELD(authorityOrigin);
            SameBounds(actual.proposedBounds, expected.proposedBounds);
            SameBounds(actual.trustedBounds, expected.trustedBounds);
            SameEdge(actual.left, expected.left); SameEdge(actual.top, expected.top);
            SameEdge(actual.right, expected.right); SameEdge(actual.bottom, expected.bottom);
            SameSideProbe(actual.leftSideProbe, expected.leftSideProbe);
            SameSideProbe(actual.rightSideProbe, expected.rightSideProbe);
            SameVerticalProfile(actual.verticalBarProfile, expected.verticalBarProfile);
            SameVerticalProfile(actual.verticalBarGuardProfile, expected.verticalBarGuardProfile);
            CHECK_FIELD(sparseTransitionProof.available); CHECK_FIELD(sparseTransitionProof.referenceId);
            CHECK_FIELD(sparseTransitionProof.sourceGeneration); CHECK_FIELD(sparseTransitionProof.sourceSequence);
            SameBounds(actual.sparseTransitionProof.establishedBase, expected.sparseTransitionProof.establishedBase);
            SameBounds(actual.sparseTransitionProof.guardedBounds, expected.sparseTransitionProof.guardedBounds);
            CHECK_FIELD(rememberedEdgeReturnProof.available); CHECK_FIELD(rememberedEdgeReturnProof.sourceSequence);
            CHECK_FIELD(rememberedEdgeReturnProof.timestampMs); CHECK_FIELD(rememberedEdgeReturnProof.guarded);
            SameNomination(actual.rememberedEdgeReturnProof.nomination, expected.rememberedEdgeReturnProof.nomination);
            SameBounds(actual.rememberedEdgeReturnProof.presentationBounds, expected.rememberedEdgeReturnProof.presentationBounds);
        }

        void SameGlobal(const ActivePictureGlobalNearBlackEvidence& actual,
            const ActivePictureGlobalNearBlackEvidence& expected)
        {
            CHECK_FIELD(evaluated); CHECK_FIELD(lumaP90); CHECK_FIELD(lumaCutoff);
            CHECK_FIELD(nearBlack); CHECK_FIELD(lumaSamples);
        }

        void SameDarkness(const ActivePictureArDarknessEvidence& actual,
            const ActivePictureArDarknessEvidence& expected)
        {
            SameGlobal(actual.global, expected.global);
            CHECK_FIELD(distributedDarkPicture); CHECK_FIELD(texturedTiles); CHECK_FIELD(texturedQuadrants);
            CHECK_FIELD(barFloor); CHECK_FIELD(pictureMedian); CHECK_FIELD(tileMedianSpread); CHECK_FIELD(lumaSamples);
            Assert::IsTrue(actual.EffectiveNearBlack() == expected.EffectiveNearBlack());
        }

        void SameVisibleExtent(const ActivePictureVisibleExtentDiagnostic& actual,
            const ActivePictureVisibleExtentDiagnostic& expected)
        {
            CHECK_FIELD(available); CHECK_FIELD(coordinate); CHECK_FIELD(firstX); CHECK_FIELD(firstY);
            CHECK_FIELD(firstLuma); CHECK_FIELD(firstU); CHECK_FIELD(firstV); CHECK_FIELD(firstReason);
            CHECK_FIELD(firstLine); CHECK_FIELD(secondLine); CHECK_FIELD(firstLineSupport); CHECK_FIELD(secondLineSupport);
            CHECK_FIELD(peakLuma); CHECK_FIELD(peakChromaDelta); CHECK_FIELD(sampleStep); CHECK_FIELD(depthSamples);
            CHECK_FIELD(lumaCutoff); CHECK_FIELD(presentationMargin);
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
            SameVisibleExtent(actual.visibleLeft, expected.visibleLeft); SameVisibleExtent(actual.visibleTop, expected.visibleTop);
            SameVisibleExtent(actual.visibleRight, expected.visibleRight); SameVisibleExtent(actual.visibleBottom, expected.visibleBottom);
            SameEdge(actual.excludedLeft, expected.excludedLeft); SameEdge(actual.excludedTop, expected.excludedTop);
            SameEdge(actual.excludedRight, expected.excludedRight); SameEdge(actual.excludedBottom, expected.excludedBottom);
            SameEdge(actual.expandingLeft, expected.expandingLeft); SameEdge(actual.expandingTop, expected.expandingTop);
            SameEdge(actual.expandingRight, expected.expandingRight); SameEdge(actual.expandingBottom, expected.expandingBottom);
        }
#undef CHECK_FIELD

        struct Owned4kMeasurementFrame
        {
            static constexpr int Width = 3840, Height = 2160, Top = 260, Bottom = 1900;
            std::shared_ptr<const std::vector<uint8_t>> pixels;
            AnalysisLumaFormat format;
            size_t pitch;
            Owned4kMeasurementFrame(AnalysisLumaFormat representation, int pattern) : format(representation),
                pitch(size_t(Width) * (format == AnalysisLumaFormat::NativeRgb ? 4 : 2))
            {
                const int chromaRows = format == AnalysisLumaFormat::P010 ? Height / 2 : Height;
                std::vector<uint8_t> bytes(pitch * (format == AnalysisLumaFormat::NativeRgb ? Height : Height + chromaRows));
                for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x)
                {
                    const int top = pattern == 4 ? 200 : Top, bottom = pattern == 4 ? 1960 : Bottom;
                    const bool picture = pattern == 1 || (y >= top && y < bottom);
                    int code = picture ? 300 + (x % 16) * 4 + (y % 8) * 3 : 64;
                    if (pattern == 2 && picture)
                    {
                        const int innerTop = Top + (Bottom - Top) / 10;
                        const int innerHeight = (Bottom - Top) * 8 / 10;
                        const int gridX = (std::max)(0, (std::min)(23, (x - Width / 20) * 24 / (Width * 9 / 10)));
                        const int gridY = (std::max)(0, (std::min)(23, (y - innerTop) * 24 / innerHeight));
                        const bool lower = (gridX / 6 == 0 && gridY / 6 == 0) || (gridX / 6 == 3 && gridY / 6 == 3);
                        code = gridX % 6 < 4 ? (lower ? 80 : 96) : (lower ? 68 : 84);
                    }
                    if (pattern == 3 && x >= 1360 && x < 2480 && y >= 1740 && y < Bottom) code = 64;
                    if (pattern == 3 && x >= 1420 && x < 2420 && y >= 1860 && y < 1950 && (x / 16) % 2 == 0) code = 700;
                    const size_t offset = size_t(y) * pitch + size_t(x) * (format == AnalysisLumaFormat::NativeRgb ? 4 : 2);
                    if (format == AnalysisLumaFormat::NativeRgb)
                    {
                        const auto gray = static_cast<uint8_t>((std::max)(0, (std::min)(255, (code - 64) * 255 / 876)));
                        bytes[offset] = bytes[offset + 1] = bytes[offset + 2] = gray; bytes[offset + 3] = 255;
                    }
                    else OwnedMeasurementFrame::Write(bytes, offset, code);
                }
                if (format != AnalysisLumaFormat::NativeRgb)
                    for (int y = 0; y < chromaRows; ++y) for (int x = 0; x < Width; ++x)
                        OwnedMeasurementFrame::Write(bytes, pitch * Height + size_t(y) * pitch + size_t(x) * 2, 512);
                pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
            }
            AnalysisLumaSource Source() const
            {
                AnalysisLumaSource source{ pixels->data(), pixels->size(), Width, Height, pitch,
                    format == AnalysisLumaFormat::NativeRgb ? 0 : pitch, format,
                    format == AnalysisLumaFormat::NativeRgb ? VideoFrameEncoding::BGRA_8BIT : VideoFrameEncoding::V210,
                    ColorSpace::REC_709, Identity().transportGeneration };
                source.range = format == AnalysisLumaFormat::NativeRgb ? AnalysisLumaRange::Full : AnalysisLumaRange::Limited;
                source.backingOwner = pixels; return source;
            }
            static ActivePictureBounds Bounds()
            { return { 0, Top, Width, Bottom, Width, Height, double(Width) / (Bottom - Top), ActivePictureBounds::BarAxes::TOP_BOTTOM }; }
        };

    }

    namespace {
        ActivePictureEvidence WorkerFacts(const AnalysisLumaSource& source) {
            return DetectSourcePictureForWorker(source).evidence;
        }
        bool SeedWorker(ActivePictureMeasurementCache& cache,const AnalysisLumaSource& live,
            const ActivePictureFrameIdentity& identity) {
            auto queued=live;queued.generation=identity.sourceFormatGeneration;
            const auto facts=WorkerFacts(queued);
            return cache.SeedQueued(queued,identity,facts,EvaluateActivePictureArDarkness(queued,facts));
        }
        void BeginWorker(ActivePictureMeasurementCache& cache,const AnalysisLumaSource& source,
            const ActivePictureFrameIdentity& identity=Identity(),uint64_t policy=7,uint64_t continuity=8) {
            cache.Begin(source,identity,policy,continuity);
            Assert::IsTrue(SeedWorker(cache,source,identity));
        }
        void Pending(ActivePictureMeasurementCache& cache) {
            const auto value=cache.Picture();
            Assert::IsFalse(value.available);Assert::IsFalse(value.physicalEdges.evaluated);
            Assert::IsFalse(cache.PhysicalEdges().estimateAvailable);
            Assert::AreEqual(size_t(0),cache.Snapshot().pictureScans);
        }
        void CheckWorker(ActivePictureMeasurementCache& cache,const AnalysisLumaSource& source,
            const ActivePictureBounds& bounds=Scope()) {
            const auto raw=WorkerFacts(source);const auto global=EvaluateActivePictureGlobalNearBlack(source);
            const auto dark=EvaluateActivePictureArDarkness(source,raw,global);
            SamePicture(cache.Picture(),raw);SameGlobal(cache.GlobalNearBlack(),global);
            SameDarkness(cache.ArDarkness(),dark);
            SameRetention(cache.Retention(bounds),EvaluateActivePicturePresentationRetention(source,bounds,raw,global,dark));
            Assert::AreEqual(size_t(0),cache.Snapshot().pictureScans);
        }
    }

    TEST_CLASS(ActivePictureMeasurementCacheTests)
    {
    public:
        TEST_METHOD(ProductionForegroundCannotSelectLegacyOrPublishMissingAsMeasured)
        {

            Assert::IsTrue(SourcePictureDetectorEnabled());
            const OwnedMeasurementFrame frame;ActivePictureMeasurementCache cache;
            cache.Begin(frame.Source(),Identity(),7,8);Pending(cache);
            auto legacy=ExtractActivePictureEvidence(frame.Source());
            Assert::IsFalse(PreparedSourceDetectorCurrent(legacy));
            for(int repeat=0;repeat<4;++repeat)Pending(cache);
            BeginWorker(cache,frame.Source());CheckWorker(cache,frame.Source());
        }
        TEST_METHOD(SingleOutputReusesRawGlobalAndArAcrossCurrentRetentionBases)
        {

            const OwnedMeasurementFrame frame;const auto source=frame.Source();ActivePictureMeasurementCache cache;
            BeginWorker(cache,source);CheckWorker(cache,source);
            auto changed=Scope();changed.top+=2;changed.bottom-=2;
            CheckWorker(cache,source,changed);CheckWorker(cache,source,changed);
            Assert::AreEqual(size_t(0),cache.Snapshot().globalScans);
            Assert::AreEqual(size_t(0),cache.Snapshot().darknessScans);
            Assert::AreEqual(size_t(2),cache.Snapshot().retentionScans);
            Assert::AreEqual(size_t(1),cache.Snapshot().retentionHits);
        }
        TEST_METHOD(PublishedGeometryHandoffRechecksBandsWithoutReplacingWorkerFacts)
        {
            const OwnedMeasurementFrame frame;const auto source=frame.Source();
            ActivePictureMeasurementCache cache;BeginWorker(cache,source);
            const auto facts=cache.Picture();auto oldBase=Scope();oldBase.top+=10;oldBase.bottom-=10;
            const auto oldRetention=cache.Retention(oldBase);
            Assert::IsFalse(oldRetention.excludedBandsPixelSafe);
            const auto handoff=cache.RetentionHandoff(oldBase,oldRetention,Scope());
            Assert::IsTrue(handoff.refreshed);Assert::IsTrue(handoff.evidence.excludedBandsPixelSafe);
            SamePicture(handoff.evidence.activePicture,facts);SamePhysical(cache.PhysicalEdges(),facts.physicalEdges);
            Assert::IsTrue(handoff.evidence.activePicture.detector==ActivePictureDetectorKind::SourceSpatialMeasured);
            const auto repeat=cache.RetentionHandoff(handoff.bounds,handoff.evidence,Scope());
            Assert::IsFalse(repeat.refreshed);SamePicture(repeat.evidence.activePicture,facts);
            Assert::AreEqual(size_t(2),cache.Snapshot().retentionScans);
            Assert::AreEqual(size_t(0),cache.Snapshot().pictureScans);
        }
        TEST_METHOD(QueuedNativeSeedRebindsOnlyGenerationAndKeepsReturnedRefinementsLocal)
        {

            const OwnedMeasurementFrame frame;const auto source=frame.Source();ActivePictureMeasurementCache cache;
            BeginWorker(cache,source);const auto original=cache.Picture();auto changed=original;
            changed.trustedBounds={};changed.physicalEdges.estimate={};changed.reason="caller-refinement";
            SamePicture(cache.Picture(),original);SameBounds(cache.PhysicalEdges().estimate,original.physicalEdges.estimate);
            Assert::AreEqual(source.generation,cache.PhysicalEdges().sourceGeneration);
            cache.Begin(source,Identity(),9,8);Pending(cache);Assert::IsTrue(SeedWorker(cache,source,Identity()));
            CheckWorker(cache,source);cache.Begin(source,Identity(),9,10);Pending(cache);
        }
        TEST_METHOD(QueuedPhysicalObservationRebindsOnlyAnExactCurrentSource)
        {

            const OwnedMeasurementFrame frame;const auto source=frame.Source();const auto id=Identity();
            for(int field=0;field<6;++field) {
                ActivePictureMeasurementCache cache;BeginWorker(cache,source,id);
                auto next=id;auto pixels=source;uint64_t policy=7,continuity=8;
                switch(field){case 0:++next.acceptedSequence;break;case 1:++next.sourceFormatGeneration;break;
                case 2:++next.analysisDomainKey;break;case 3:++policy;break;case 4:++continuity;break;
                case 5:pixels.backingOwner=std::make_shared<const std::vector<uint8_t>>(*frame.pixels);break;}
                cache.Begin(pixels,next,policy,continuity);Pending(cache);
                Assert::IsTrue(SeedWorker(cache,pixels,next));CheckWorker(cache,pixels);
            }
        }
        TEST_METHOD(ForeignPhysicalGenerationCannotBecomeCurrentThroughAValidNativeSeed)
        {

            const OwnedMeasurementFrame frame;const auto source=frame.Source();auto queued=source;
            queued.generation=Identity().sourceFormatGeneration;auto facts=WorkerFacts(queued);
            Assert::IsTrue(facts.physicalEdges.estimateAvailable);++facts.physicalEdges.sourceGeneration;
            ActivePictureMeasurementCache cache;cache.Begin(source,Identity(),7,8);
            Assert::IsTrue(cache.SeedQueued(queued,Identity(),facts,EvaluateActivePictureArDarkness(queued,facts)));
            Assert::IsFalse(cache.Picture().physicalEdges.evaluated);Assert::IsFalse(cache.PhysicalEdges().estimateAvailable);
        }
        TEST_METHOD(QueuedSeedRejectsEveryIdentityFieldAndStorageInterpretationMismatch)
        {

            const OwnedMeasurementFrame frame;const auto live=frame.Source();const auto id=Identity();auto queued=live;
            queued.generation=id.sourceFormatGeneration;const auto facts=WorkerFacts(queued);
            const auto darkness=EvaluateActivePictureArDarkness(queued,facts);
            for(int field=0;field<8;++field) {
                auto wrong=id;switch(field){case 0:++wrong.transportGeneration;break;case 1:++wrong.acceptedSequence;break;
                case 2:++wrong.sourceFrameNumber;break;case 3:++wrong.captureTimestamp;break;case 4:++wrong.sourceFormatGeneration;break;
                case 5:++wrong.viewportGeneration;break;case 6:++wrong.rendererGeneration;break;case 7:++wrong.analysisDomainKey;break;}
                ActivePictureMeasurementCache cache;cache.Begin(live,id,7,8);
                Assert::IsFalse(cache.SeedQueued(queued,wrong,facts,darkness));Pending(cache);
            }
            for(int field=0;field<15;++field) {
                auto wrong=queued;const auto owner=queued.backingOwner;
                switch(field){case 0:wrong.data+=2;break;case 1:wrong.dataBytes-=2;break;case 2:wrong.width-=2;break;
                case 3:wrong.height-=2;break;case 4:wrong.rowBytes-=4;break;case 5:wrong.chromaRowBytes-=4;break;
                case 6:wrong.format=AnalysisLumaFormat::P210;break;case 7:wrong.encoding=VideoFrameEncoding::UYVY;break;
                case 8:wrong.colorspace=ColorSpace::BT_2020;break;case 9:++wrong.generation;break;case 10:wrong.rgbBitAlignment=1;break;
                case 11:wrong.range=AnalysisLumaRange::Full;break;case 12:wrong.backingOwner.reset();break;
                case 13:wrong.backingOwner=std::shared_ptr<const void>(owner.get(),[owner](const void*){});break;
                case 14:wrong.backingOwner=std::shared_ptr<const void>(owner,queued.data);break;}
                ActivePictureMeasurementCache cache;cache.Begin(live,id,7,8);
                Assert::IsFalse(cache.SeedQueued(wrong,id,facts,darkness));Pending(cache);
            }
            auto wrongLive=live;++wrongLive.generation;ActivePictureMeasurementCache wrongGeneration;
            wrongGeneration.Begin(wrongLive,id,7,8);
            Assert::IsFalse(wrongGeneration.SeedQueued(queued,id,facts,darkness),
                L"Only matching format-generation to transport-generation adaptation is allowed.");
            Pending(wrongGeneration);
        }
        TEST_METHOD(PairedWarmed4kMeasurementsReportCostAndPreserveEveryEvidenceField)
        {

            for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::NativeRgb})
            for(int pattern=0;pattern<5;++pattern) {
                const Owned4kMeasurementFrame frame(format,pattern);const auto source=frame.Source();
                ActivePictureMeasurementCache cache;BeginWorker(cache,source);
                CheckWorker(cache,source,Owned4kMeasurementFrame::Bounds());
                CheckWorker(cache,source,Owned4kMeasurementFrame::Bounds());
                Assert::AreEqual(size_t(1),cache.Snapshot().workerImports);
                Assert::AreEqual(size_t(1),cache.Snapshot().retentionScans);
            }
        }
        TEST_METHOD(ThreeRepeatedOutputsReuseExactScopeDarkAndFullRasterMeasurements)
        {

            for(int pattern=0;pattern<3;++pattern) {
                const OwnedMeasurementFrame frame(pattern);ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());
                for(int repeat=0;repeat<3;++repeat){cache.Begin(frame.Source(),Identity(),7,8);CheckWorker(cache,frame.Source());}
                Assert::AreEqual(size_t(1),cache.Snapshot().workerImports);
                Assert::AreEqual(size_t(3),cache.Snapshot().pictureHits);
                Assert::AreEqual(size_t(2),cache.Snapshot().retentionHits);
            }
        }
        TEST_METHOD(RefiningReturnedCopiesCannotPoisonCachedPixelEvidence)
        {

            const OwnedMeasurementFrame frame;ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());
            auto global=cache.GlobalNearBlack();auto picture=cache.Picture();auto retention=cache.Retention(Scope());
            global.nearBlack=!global.nearBlack;picture.trustedBounds={};picture.reason="changed";
            retention.currentlyPixelSafe=!retention.currentlyPixelSafe;retention.activePicture=picture;
            CheckWorker(cache,frame.Source());
        }
        TEST_METHOD(EverySourceIdentityFieldAndPolicyOrContinuityChangeInvalidates)
        {

            const OwnedMeasurementFrame frame;
            for(int field=0;field<10;++field) {
                ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());auto id=Identity();uint64_t policy=7,continuity=8;
                switch(field){case 0:++id.transportGeneration;break;case 1:++id.acceptedSequence;break;case 2:++id.sourceFrameNumber;break;
                case 3:++id.captureTimestamp;break;case 4:++id.sourceFormatGeneration;break;case 5:++id.viewportGeneration;break;
                case 6:++id.rendererGeneration;break;case 7:++id.analysisDomainKey;break;case 8:++policy;break;case 9:++continuity;break;}
                cache.Begin(frame.Source(),id,policy,continuity);Pending(cache);
            }
        }
        TEST_METHOD(PixelLayoutRangeAndColorInterpretationChangesInvalidate)
        {

            const OwnedMeasurementFrame frame;
            for(int field=0;field<12;++field) {
                ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());auto changed=frame.Source();
                switch(field){case 0:changed.data+=2;changed.dataBytes-=2;break;case 1:changed.dataBytes-=2;break;
                case 2:changed.width-=2;break;case 3:changed.height-=2;break;case 4:changed.rowBytes-=4;break;case 5:changed.chromaRowBytes-=4;break;
                case 6:changed.format=AnalysisLumaFormat::P210;break;case 7:changed.encoding=VideoFrameEncoding::UYVY;break;
                case 8:changed.colorspace=ColorSpace::BT_2020;break;case 9:++changed.generation;break;
                case 10:changed.rgbBitAlignment=1;break;case 11:changed.range=AnalysisLumaRange::Full;break;}
                cache.Begin(changed,Identity(),7,8);Pending(cache);
            }
        }
        TEST_METHOD(RetentionRequiresExactCropBaseButKeepsOtherMeasurements)
        {

            const OwnedMeasurementFrame frame;
            for(int field=0;field<8;++field) {
                ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());auto changed=Scope();
                switch(field){case 0:++changed.left;break;case 1:++changed.top;break;case 2:--changed.right;break;case 3:--changed.bottom;break;
                case 4:++changed.rasterWidth;break;case 5:++changed.rasterHeight;break;case 6:changed.aspectRatio+=.01;break;
                case 7:changed.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;break;}
                CheckWorker(cache,frame.Source());CheckWorker(cache,frame.Source(),changed);CheckWorker(cache,frame.Source(),changed);
                Assert::AreEqual(size_t(2),cache.Snapshot().retentionScans);Assert::AreEqual(size_t(1),cache.Snapshot().retentionHits);
            }
        }
        TEST_METHOD(BorrowedPixelsAndIncompleteIdentitiesAreNeverReused)
        {

            const OwnedMeasurementFrame frame;
            for(int missing=0;missing<6;++missing){auto source=frame.Source();auto id=Identity();
                switch(missing){case 0:source.backingOwner.reset();break;case 1:source.generation=0;break;case 2:id.transportGeneration=0;break;
                case 3:id.acceptedSequence=0;break;case 4:id.analysisDomainKey=0;break;case 5:source.data=nullptr;break;}
                ActivePictureMeasurementCache cache;cache.Begin(source,id,7,8);
                Assert::IsFalse(SeedWorker(cache,source,id));for(int repeat=0;repeat<3;++repeat)Pending(cache);
            }
        }
        TEST_METHOD(BorrowedCurrentRawAndGlobalDoNotGetExtractedAgainForDarkness)
        {

            const OwnedMeasurementFrame frame;auto source=frame.Source();source.backingOwner.reset();
            ActivePictureMeasurementCache cache;cache.Begin(source,Identity(),7,8);
            const auto raw=cache.Picture();const auto global=cache.GlobalNearBlack();
            SameDarkness(cache.ArDarkness(raw,global),EvaluateActivePictureArDarkness(source,raw,global));
            Assert::AreEqual(size_t(0),cache.Snapshot().pictureScans);Assert::AreEqual(size_t(1),cache.Snapshot().globalScans);
        }
        TEST_METHOD(DisabledCachePreservesDirectSingleOutputMeasurements)
        {

            const OwnedMeasurementFrame frame;ActivePictureMeasurementCache cache(false);
            cache.Begin(frame.Source(),Identity(),7,8);Assert::IsFalse(SeedWorker(cache,frame.Source(),Identity()));
            for(int repeat=0;repeat<3;++repeat){Pending(cache);SameGlobal(cache.GlobalNearBlack(),EvaluateActivePictureGlobalNearBlack(frame.Source()));}
            Assert::AreEqual(size_t(3),cache.Snapshot().globalScans);
        }
        TEST_METHOD(ExactOwnerControlBlockIsPartOfImmutableSourceIdentity)
        {

            const OwnedMeasurementFrame frame;const auto owner=frame.Source().backingOwner;
            for(bool alias:{false,true}) {ActivePictureMeasurementCache cache;BeginWorker(cache,frame.Source());auto changed=frame.Source();
                changed.backingOwner=alias?std::shared_ptr<const void>(owner,changed.data):std::shared_ptr<const void>(owner.get(),[owner](const void*){});
                cache.Begin(changed,Identity(),7,8);Pending(cache);}
        }
        TEST_METHOD(CacheRetainsBackingUntilSourceReplacement)
        {

            ActivePictureMeasurementCache cache;std::weak_ptr<const std::vector<uint8_t>> owner;
            {const OwnedMeasurementFrame frame;owner=frame.pixels;BeginWorker(cache,frame.Source());}
            Assert::IsFalse(owner.expired());cache.Begin({},Identity(),7,8);Assert::IsTrue(owner.expired());
        }
        TEST_METHOD(NewCapturedFrameMeasuresItsOwnPixels)
        {

            const OwnedMeasurementFrame scope(0),full(2);ActivePictureMeasurementCache cache;
            BeginWorker(cache,scope.Source());const auto before=cache.Picture();auto id=Identity();++id.acceptedSequence;++id.sourceFrameNumber;
            cache.Begin(full.Source(),id,7,8);Pending(cache);Assert::IsTrue(SeedWorker(cache,full.Source(),id));
            const auto after=cache.Picture();SamePicture(after,WorkerFacts(full.Source()));
            Assert::IsTrue(before.trustedBounds.top>after.trustedBounds.top);
        }
        TEST_METHOD(CompletedExactSourceReplacesPendingFactsWithoutMissingDowngrade)
        {

            const OwnedMeasurementFrame frame;auto queued=frame.Source();queued.generation=Identity().sourceFormatGeneration;
            const auto missing=MissingSourcePictureEvidence(queued,"measurement-budget-exhausted");
            ActivePictureMeasurementCache cache;cache.Begin(frame.Source(),Identity(),7,8);
            Assert::IsTrue(cache.SeedQueued(queued,Identity(),missing,EvaluateActivePictureArDarkness(queued,missing)));
            Pending(cache);cache.Retention(Scope());
            Assert::IsTrue(SeedWorker(cache,frame.Source(),Identity()));CheckWorker(cache,frame.Source());
            const auto complete=cache.Picture();
            Assert::IsTrue(cache.SeedQueued(queued,Identity(),missing,EvaluateActivePictureArDarkness(queued,missing)));
            SamePicture(cache.Picture(),complete);Assert::IsTrue(cache.PhysicalEdges().estimateAvailable);
            Assert::AreEqual(size_t(2),cache.Snapshot().retentionScans);
        }
        TEST_METHOD(CompletedVerificationUpgradesOnlyTheSameMeasuredGeometry)
        {
            const OwnedMeasurementFrame frame;auto queued=frame.Source();queued.generation=Identity().sourceFormatGeneration;
            const auto complete=WorkerFacts(queued);
            Assert::IsTrue(complete.available);Assert::IsTrue(complete.physicalEdges.estimateAvailable);
            Assert::IsTrue(complete.candidateVerification.vertical==ActivePictureCandidateVerificationState::VERIFIED);
            auto pending=MissingSourcePictureEvidence(queued,"supplied candidate verification budget exhausted");
            pending.detector=complete.detector;pending.physicalEdges=complete.physicalEdges;
            pending.physicalEdges.conservativeBounds={};
            ActivePictureMeasurementCache cache;cache.Begin(frame.Source(),Identity(),7,8);
            const auto seed=[&](const ActivePictureEvidence& facts) {
                return cache.SeedQueued(queued,Identity(),facts,EvaluateActivePictureArDarkness(queued,facts));
            };
            Assert::IsTrue(seed(pending));Assert::IsFalse(cache.Picture().available);
            const auto before=cache.Picture();cache.Retention(Scope());
            auto unfinished=complete;unfinished.sourceCandidateVerificationComplete=false;
            Assert::IsTrue(seed(unfinished));SamePicture(cache.Picture(),before);
            auto changed=complete;++changed.physicalEdges.estimate.top;
            Assert::IsTrue(seed(changed));SamePicture(cache.Picture(),before);
            auto foreign=complete;++foreign.physicalEdges.sourceGeneration;
            Assert::IsTrue(seed(foreign));SamePicture(cache.Picture(),before);
            Assert::IsTrue(seed(complete));CheckWorker(cache,frame.Source());
            const auto verified=cache.Picture();Assert::IsTrue(seed(pending));SamePicture(cache.Picture(),verified);
            Assert::AreEqual(size_t(2),cache.Snapshot().retentionScans);
        }
        TEST_METHOD(VerificationCompletionPublishesNegativeAndFullRasterWithoutGrantingCrop)
        {
            const OwnedMeasurementFrame frame(2);auto queued=frame.Source();queued.generation=Identity().sourceFormatGeneration;
            for(bool negative:{false,true}) {
                auto complete=WorkerFacts(queued);
                if(negative) {
                    // Exercise the completed rejection of a supplied crop into picture.
                    complete=VerifySuppliedActivePictureCandidate(queued,Scope());
                    Assert::IsTrue(complete.classification==ActivePictureClassification::PROVISIONAL);
                    complete.detector=ActivePictureDetectorKind::SourceSpatialMeasured;
                    complete.physicalEdges.evaluated=complete.physicalEdges.estimateAvailable=true;
                    complete.physicalEdges.sourceGeneration=queued.generation;
                    complete.physicalEdges.estimate=Scope();
                    complete.physicalEdges.conservativeBounds=complete.trustedBounds;
                }
                Assert::IsTrue(complete.sourceCandidateVerificationComplete);
                auto pending=MissingSourcePictureEvidence(queued,"supplied candidate verification budget exhausted");
                pending.detector=complete.detector;pending.physicalEdges=complete.physicalEdges;
                pending.physicalEdges.conservativeBounds={};
                ActivePictureMeasurementCache cache;cache.Begin(frame.Source(),Identity(),7,8);
                Assert::IsTrue(cache.SeedQueued(queued,Identity(),pending,EvaluateActivePictureArDarkness(queued,pending)));
                Assert::IsFalse(cache.Picture().sourceCandidateVerificationComplete);
                Assert::IsTrue(cache.SeedQueued(queued,Identity(),complete,EvaluateActivePictureArDarkness(queued,complete)));
                auto rebound=complete;rebound.physicalEdges.sourceGeneration=frame.Source().generation;
                SamePicture(cache.Picture(),rebound);
                Assert::AreEqual(0,cache.Picture().trustedBounds.top);
                Assert::AreEqual(frame.height,cache.Picture().trustedBounds.bottom);
            }
            AnalysisEvaluationBudget expired([](void*){return true;},nullptr);
            const auto cancelled=VerifySuppliedActivePictureCandidate(queued,Scope(),&expired);
            Assert::IsFalse(cancelled.sourceCandidateVerificationComplete);
        }
        TEST_METHOD(OptionalObserverCannotRescanOrOverwriteReadySourceFacts)
        {

            const OwnedMeasurementFrame frame;auto facts=WorkerFacts(frame.Source());const auto original=facts;
            int checks=0;AnalysisEvaluationBudget expired([](void* p){++*static_cast<int*>(p);return true;},&checks);
            Assert::IsFalse(ActivePictureMeasurementCache::CompletePhysicalObservationIfMissing(frame.Source(),facts,&expired));
            SamePicture(facts,original);SameBounds(ActivePictureMeasurementCache::MeasurePhysicalEdgesOptional(frame.Source(),facts).estimate,original.physicalEdges.estimate);
            Assert::AreEqual(0,checks);auto missing=MissingSourcePictureEvidence(frame.Source());
            Assert::IsFalse(ActivePictureMeasurementCache::CompletePhysicalObservationIfMissing(frame.Source(),missing,&expired));
            Assert::IsFalse(missing.physicalEdges.evaluated);Assert::AreEqual(0,checks);
        }
        TEST_METHOD(ScrubLineAndSeparatedTimeLabelsOwnCompleteBoundedOffFitAndReleaseHold)
        {
            using namespace AlphaSourceCrop;
            for(bool top:{false,true})for(int shape:{0,1,2}) {
                const int width=3840,height=2160;
                auto pixels=std::make_shared<std::vector<uint16_t>>(size_t(width)*height*3/2,uint16_t(512<<6));
                std::fill(pixels->begin(),pixels->begin()+size_t(width)*height,uint16_t(64<<6));
                const auto paint=[&](int left,int first,int right,int last,int code) {
                    for(int y=first;y<last;++y)for(int x=left;x<right;++x)
                        (*pixels)[size_t(top?height-1-y:y)*width+x]=uint16_t(code<<6);
                };
                paint(0,384,width,1776,300);
                if(shape==2)paint(128,1806,3712,2068,500);
                else {
                    paint(128,1806,3712,1826,500);
                    if(shape==1)paint(2700,1826,3470,1974,500); // Preview plus line and labels: 190 rows.
                    paint(2920,2046,3220,2068,700);paint(3470,2046,3650,2068,700);
                }
                AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels->data());
                source.dataBytes=pixels->size()*2;source.width=width;source.height=height;
                source.rowBytes=source.chromaRowBytes=width*2;source.format=AnalysisLumaFormat::P010;
                source.generation=17;source.backingOwner=pixels;
                const ActivePictureBounds native={0,384,width,1776,width,height,3840.0/1392.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
                ActivePictureMeasurementCache cache;cache.Begin(source,Identity(),7,8);
                const auto measured=cache.SubtitleContent(native,64);
                Assert::IsTrue(top?measured.upper:measured.lower);
                const int first=top?measured.upperTop:measured.lowerTop;
                const int last=top?measured.upperBottom:measured.lowerBottom;
                const int occupied=top?measured.upperOccupiedRows:measured.lowerOccupiedRows;
                Assert::AreEqual(262,last-first);Assert::AreEqual(shape==2?262:(shape==1?190:42),occupied);
                VerticalBarContentInput input;input.sampledColumns=1800;
                input.upperContent=measured.upper;input.lowerContent=measured.lower;
                input.upperBarPixels=input.lowerBarPixels=384;
                input.upperOccupiedDepth=measured.upperBottom-measured.upperTop;
                input.lowerOccupiedDepth=measured.lowerBottom-measured.lowerTop;
                input.upperOccupiedRows=measured.upperOccupiedRows;input.lowerOccupiedRows=measured.lowerOccupiedRows;
                input.upperPeakSamples=measured.upperPeak;input.lowerPeakSamples=measured.lowerPeak;
                input.upperRequiredShift=top?272.0f:0.0f;input.lowerRequiredShift=top?0.0f:292.0f;
                const auto legacy=EvaluateVerticalBarContent(input);
                Assert::IsFalse(top?legacy.upperOverlayLike:legacy.lowerOverlayLike);
                const auto classic=SelectVerticalBarContentPresentation(legacy,true,&input);
                Assert::IsFalse(top?classic.upperOverlayLike:classic.lowerOverlayLike);
                const auto off=SelectVerticalBarContentPresentation(legacy,false,&input);
                Assert::IsFalse(top?off.upperOverlayLike:off.lowerOverlayLike);
                Assert::IsTrue(off.action==VerticalBarPresentationAction::FIT);
                const auto immediate=ConfirmVerticalFit({},off,42,false);
                Assert::IsFalse(immediate.pending);
                Assert::IsTrue(immediate.effective.action==VerticalBarPresentationAction::FIT);
                Assert::IsTrue(ConfirmVerticalFit({},classic,42).pending);
                PresentationEnvelopeGeometryInput envelope;envelope.trustedPicture=native;envelope.observedContent=native;
                envelope.observedContent.top=top?first:native.top;envelope.observedContent.bottom=top?native.bottom:last;
                envelope.observedContentAvailable=true;envelope.expandTop=top;envelope.expandBottom=!top;envelope.verticalPadding=24;
                const auto fitted=BuildPresentationEnvelope(envelope);Assert::IsTrue(fitted.valid && fitted.expanded);
                for(bool placeholder:{false,true}) {
                    PresentationEnvelopeCompositionInput composed;composed.trustedPicture=native;
                    composed.denseContent.bounds=envelope.observedContent;
                    composed.denseContent.expandTop=top;composed.denseContent.expandBottom=!top;
                    composed.verticalPadding=24;composed.measuredVerticalFit=true;
                    composed.detectorContent.bounds=native;
                    composed.detectorContent.bounds.top=placeholder?0:382;
                    composed.detectorContent.bounds.bottom=placeholder?2160:1778;
                    composed.detectorContent.expandTop=composed.detectorContent.expandBottom=true;
                    const auto presented=BuildComposedPresentationEnvelope(composed);
                    Assert::IsTrue(presented.valid && presented.expanded);
                    Assert::AreEqual(fitted.bounds.top,presented.bounds.top);
                    Assert::AreEqual(fitted.bounds.bottom,presented.bounds.bottom);
                }
                ConfirmedFitOwnershipInput proof;proof.current=proof.confirmed=true;proof.origin=ConfirmedFitOwnershipOrigin::CURRENT_BAR_CONTENT;
                proof.nativeBase=native;proof.approvedBounds=fitted.bounds;proof.evidenceBounds=envelope.observedContent;
                proof.sourceGeneration=17;proof.sourceSequence=42;proof.upperContent=top;proof.lowerContent=!top;
                proof.upperOverlayLike=off.upperOverlayLike;proof.lowerOverlayLike=off.lowerOverlayLike;
                proof.verticalPadding=24;
                Assert::IsTrue(QualifyConfirmedFitOwnership(proof).available);
                proof.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
                Assert::IsFalse(QualifyConfirmedFitOwnership(proof).available, L"Classic keeps its overlay-shape requirement");
                Assert::IsTrue(fitted.bounds.top>0 && fitted.bounds.bottom<height);
                VerticalBarPresentationUpdateInput update;update.current=immediate.effective;update.upperContent=top;update.lowerContent=!top;
                update.upperContentTop=measured.upperTop;update.lowerContentBottom=measured.lowerBottom;
                update.currentTick=1000;update.currentSourceSequence=42;update.holdMs=2000;update.translationEnabled=false;
                update.previous=UpdateVerticalBarPresentation(update);
                update.current={};update.upperContent=update.lowerContent=false;update.currentSourceSequence=43;update.currentTick=2999;
                const auto held=UpdateVerticalBarPresentation(update);
                Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::FIT),static_cast<int>(held.action));
                Assert::AreEqual(top?first:last,top?held.detectedTop:held.detectedBottom);
                update.previous=held;update.currentSourceSequence=44;update.currentTick=3001;
                Assert::AreEqual(static_cast<int>(VerticalBarPresentationAction::NONE),static_cast<int>(UpdateVerticalBarPresentation(update).action));
                const auto cached=cache.SubtitleContent(native,64);
                Assert::AreEqual(occupied,top?cached.upperOccupiedRows:cached.lowerOccupiedRows);
            }
        }
    };
}
