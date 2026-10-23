#include "pch.h"
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include "CppUnitTest.h"

#include <array>
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

        void SameAsDirect(ActivePictureMeasurementCache& cache, const AnalysisLumaSource& source,
            const ActivePictureBounds& bounds = Scope())
        {
            SameGlobal(cache.GlobalNearBlack(), EvaluateActivePictureGlobalNearBlack(source));
            SamePicture(cache.Picture(), ExtractActivePictureEvidence(source));
            SameRetention(cache.Retention(bounds), EvaluateActivePicturePresentationRetention(source, bounds));
        }

        void Counts(const ActivePictureMeasurementCache& cache, size_t requests, size_t hits)
        {
            const auto& stats = cache.Snapshot();
            Assert::IsTrue(stats.globalRequests == requests && stats.globalHits == hits);
            Assert::IsTrue(stats.pictureRequests == requests && stats.pictureHits == hits);
            Assert::IsTrue(stats.retentionRequests == requests && stats.retentionHits == hits);
        }

        void ChangedContextIsFresh(const AnalysisLumaSource& original,
            const AnalysisLumaSource& changed, const ActivePictureFrameIdentity& identity,
            const ActivePictureFrameIdentity& changedIdentity, uint64_t policy = 7, uint64_t continuity = 8)
        {
            ActivePictureMeasurementCache cache;
            cache.Begin(original, identity, 7, 8);
            SameAsDirect(cache, original);
            cache.Begin(changed, changedIdentity, policy, continuity);
            SameAsDirect(cache, changed);
            Counts(cache, 2, 0);
        }
    }

    TEST_CLASS(ActivePictureMeasurementCacheTests)
    {
    public:
        TEST_METHOD(ThreeRepeatedOutputsReuseExactScopeDarkAndFullRasterMeasurements)
        {
            for (int pattern = 0; pattern < 3; ++pattern)
            {
                const OwnedMeasurementFrame frame(pattern);
                const auto source = frame.Source();
                const auto direct = ExtractActivePictureEvidence(source);
                if (pattern != 1) Assert::IsTrue(direct.available);
                if (pattern == 0)
                    Assert::IsTrue(direct.classification == ActivePictureClassification::BAR_CROP_TRUSTED);
                if (pattern == 1)
                    Assert::IsTrue(EvaluateActivePictureGlobalNearBlack(source).nearBlack);
                ActivePictureMeasurementCache cache;
                for (int output = 0; output < 3; ++output)
                {
                    cache.Begin(source, Identity(), 7, 8);
                    SameAsDirect(cache, source);
                }
                Counts(cache, 3, 2);
            }
        }

        TEST_METHOD(RefiningReturnedCopiesCannotPoisonCachedPixelEvidence)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            ActivePictureMeasurementCache cache;
            cache.Begin(source, Identity(), 7, 8);
            auto global = cache.GlobalNearBlack();
            auto picture = cache.Picture();
            auto retention = cache.Retention(Scope());
            global.nearBlack = !global.nearBlack;
            picture.classification = ActivePictureClassification::UNAVAILABLE;
            picture.trustedBounds = {};
            picture.reason = "caller-only refinement";
            retention.currentlyPixelSafe = !retention.currentlyPixelSafe;
            retention.activePicture = picture;
            retention.reason = "caller-only refinement";
            SameAsDirect(cache, source);
            Counts(cache, 2, 1);
        }

        TEST_METHOD(EverySourceIdentityFieldAndPolicyOrContinuityChangeInvalidates)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            const auto identity = Identity();
            for (int field = 0; field < 8; ++field)
            {
                auto changed = identity;
                switch (field)
                {
                case 0: ++changed.transportGeneration; break;
                case 1: ++changed.acceptedSequence; break;
                case 2: ++changed.sourceFrameNumber; break;
                case 3: ++changed.captureTimestamp; break;
                case 4: ++changed.sourceFormatGeneration; break;
                case 5: ++changed.viewportGeneration; break;
                case 6: ++changed.rendererGeneration; break;
                case 7: ++changed.analysisDomainKey; break;
                }
                ChangedContextIsFresh(source, source, identity, changed);
            }
            ChangedContextIsFresh(source, source, identity, identity, 9, 8);
            ChangedContextIsFresh(source, source, identity, identity, 7, 9);
        }

        TEST_METHOD(PixelLayoutRangeAndColorInterpretationChangesInvalidate)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            for (int field = 0; field < 12; ++field)
            {
                auto changed = source;
                switch (field)
                {
                case 0: changed.data += 2; changed.dataBytes -= 2; break;
                case 1: changed.dataBytes -= 2; break;
                case 2: changed.width -= 2; break;
                case 3: changed.height -= 2; break;
                case 4: changed.rowBytes -= 4; break;
                case 5: changed.chromaRowBytes -= 4; break;
                case 6: changed.format = AnalysisLumaFormat::P210; break;
                case 7: changed.encoding = VideoFrameEncoding::UYVY; break;
                case 8: changed.colorspace = ColorSpace::BT_2020; break;
                case 9: ++changed.generation; break;
                case 10: changed.rgbBitAlignment = 1; break;
                case 11: changed.range = AnalysisLumaRange::Full; break;
                }
                ChangedContextIsFresh(source, changed, Identity(), Identity());
            }
        }

        TEST_METHOD(RetentionRequiresExactCropBaseButKeepsOtherMeasurements)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            const auto bounds = Scope();
            for (int field = 0; field < 8; ++field)
            {
                auto changed = bounds;
                switch (field)
                {
                case 0: ++changed.left; break;
                case 1: ++changed.top; break;
                case 2: --changed.right; break;
                case 3: --changed.bottom; break;
                case 4: ++changed.rasterWidth; break;
                case 5: ++changed.rasterHeight; break;
                case 6: changed.aspectRatio += 0.01; break;
                case 7: changed.trustedBarAxes = ActivePictureBounds::BarAxes::NONE; break;
                }
                ActivePictureMeasurementCache cache;
                cache.Begin(source, Identity(), 7, 8);
                SameAsDirect(cache, source, bounds);
                SameAsDirect(cache, source, changed);
                SameAsDirect(cache, source, changed);
                const auto& stats = cache.Snapshot();
                Assert::IsTrue(stats.globalRequests == 3 && stats.globalHits == 2);
                Assert::IsTrue(stats.pictureRequests == 3 && stats.pictureHits == 2);
                Assert::IsTrue(stats.retentionRequests == 3 && stats.retentionHits == 1);
            }
        }

        TEST_METHOD(BorrowedPixelsAndIncompleteIdentitiesAreNeverReused)
        {
            const OwnedMeasurementFrame frame;
            for (int missing = 0; missing < 6; ++missing)
            {
                auto source = frame.Source();
                auto identity = Identity();
                switch (missing)
                {
                case 0: source.backingOwner.reset(); break;
                case 1: source.generation = 0; break;
                case 2: identity.transportGeneration = 0; break;
                case 3: identity.acceptedSequence = 0; break;
                case 4: identity.analysisDomainKey = 0; break;
                case 5: source.data = nullptr; break;
                }
                ActivePictureMeasurementCache cache;
                for (int output = 0; output < 3; ++output)
                {
                    cache.Begin(source, identity, 7, 8);
                    SameAsDirect(cache, source);
                }
                Counts(cache, 3, 0);
            }
        }

        TEST_METHOD(DisabledCachePreservesDirectSingleOutputMeasurements)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            ActivePictureMeasurementCache cache(false);
            for (int output = 0; output < 3; ++output)
            {
                cache.Begin(source, Identity(), 7, 8);
                SameAsDirect(cache, source);
            }
            Counts(cache, 3, 0);
        }
        TEST_METHOD(ExactOwnerControlBlockIsPartOfImmutableSourceIdentity)
        {
            const OwnedMeasurementFrame frame;
            const auto source = frame.Source();
            auto changed = source;
            const auto owner = source.backingOwner;
            changed.backingOwner = std::shared_ptr<const void>(owner.get(),
                [owner](const void*) { /* The distinct owner still retains the same immutable pixels. */ });
            Assert::IsTrue(source.backingOwner.get() == changed.backingOwner.get());
            Assert::IsTrue(source.backingOwner.owner_before(changed.backingOwner) ||
                changed.backingOwner.owner_before(source.backingOwner));
            ChangedContextIsFresh(source, changed, Identity(), Identity());
            // An alias with the same control block but a different owner pointer
            // also represents a changed retention contract for this source view.
            changed.backingOwner = std::shared_ptr<const void>(owner, source.data);
            Assert::IsTrue(source.backingOwner.get() != changed.backingOwner.get());
            Assert::IsFalse(source.backingOwner.owner_before(changed.backingOwner) ||
                changed.backingOwner.owner_before(source.backingOwner));
            ChangedContextIsFresh(source, changed, Identity(), Identity());
        }

        TEST_METHOD(CacheRetainsBackingUntilSourceReplacement)
        {
            ActivePictureMeasurementCache cache;
            std::weak_ptr<const std::vector<uint8_t>> lifetime;
            {
                const OwnedMeasurementFrame frame;
                lifetime = frame.pixels;
                cache.Begin(frame.Source(), Identity(), 7, 8);
                cache.Picture();
            }
            Assert::IsFalse(lifetime.expired());
            cache.Begin({}, Identity(), 7, 8);
            Assert::IsTrue(lifetime.expired());
        }

        TEST_METHOD(ScrubLineAndSeparatedTimeLabelsOwnCompleteBoundedOffFitAndReleaseHold)
        {
            using namespace AlphaSourceCrop;
            for(bool top:{false,true})for(bool broad:{false,true}) {
                const int width=3840,height=2160;
                auto pixels=std::make_shared<std::vector<uint16_t>>(size_t(width)*height*3/2,uint16_t(512<<6));
                std::fill(pixels->begin(),pixels->begin()+size_t(width)*height,uint16_t(64<<6));
                const auto paint=[&](int left,int first,int right,int last,int code) {
                    for(int y=first;y<last;++y)for(int x=left;x<right;++x)
                        (*pixels)[size_t(top?height-1-y:y)*width+x]=uint16_t(code<<6);
                };
                paint(0,384,width,1776,300);
                if(broad)paint(128,1806,3712,2068,500);
                else {
                    paint(128,1806,3712,1826,500);
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
                Assert::AreEqual(262,last-first);Assert::AreEqual(broad?262:42,occupied);
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
                Assert::AreEqual(!broad,top?off.upperOverlayLike:off.lowerOverlayLike);
                PresentationEnvelopeGeometryInput envelope;envelope.trustedPicture=native;envelope.observedContent=native;
                envelope.observedContent.top=top?first:native.top;envelope.observedContent.bottom=top?native.bottom:last;
                envelope.observedContentAvailable=true;envelope.expandTop=top;envelope.expandBottom=!top;envelope.verticalPadding=24;
                const auto fitted=BuildPresentationEnvelope(envelope);Assert::IsTrue(fitted.valid && fitted.expanded);
                ConfirmedFitOwnershipInput proof;proof.current=proof.confirmed=true;proof.origin=ConfirmedFitOwnershipOrigin::CURRENT_UI_OVERLAY;
                proof.nativeBase=native;proof.approvedBounds=fitted.bounds;proof.evidenceBounds=envelope.observedContent;
                proof.sourceGeneration=17;proof.sourceSequence=42;proof.upperContent=top;proof.lowerContent=!top;
                proof.upperOverlayLike=off.upperOverlayLike;proof.lowerOverlayLike=off.lowerOverlayLike;
                Assert::AreEqual(!broad,QualifyConfirmedFitOwnership(proof).available);
                if(broad)continue;
                Assert::IsTrue(fitted.bounds.top>0 && fitted.bounds.bottom<height);
                VerticalBarPresentationUpdateInput update;update.current=off;update.upperContent=top;update.lowerContent=!top;
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

        TEST_METHOD(NewCapturedFrameMeasuresItsOwnPixels)
        {
            const OwnedMeasurementFrame scope(0), fullRaster(2);
            auto next = Identity();
            ++next.acceptedSequence; ++next.sourceFrameNumber; ++next.captureTimestamp;
            ActivePictureMeasurementCache cache;
            cache.Begin(scope.Source(), Identity(), 7, 8);
            const auto before = cache.Picture();
            cache.Begin(fullRaster.Source(), next, 7, 8);
            const auto after = cache.Picture();
            SamePicture(after, ExtractActivePictureEvidence(fullRaster.Source()));
            Assert::IsTrue(before.trustedBounds.top > after.trustedBounds.top);
            Assert::IsTrue(cache.Snapshot().pictureRequests == 2 && cache.Snapshot().pictureHits == 0);
        }
    };
}
