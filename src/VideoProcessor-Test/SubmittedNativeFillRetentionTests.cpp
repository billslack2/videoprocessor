#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
TEST_CLASS(SubmittedNativeFillRetentionTests)
{
    static ActivePictureBounds NativeScope()
    {
        return {0,278,3840,1880,3840,2160,3840.0/1602.0,
            ActivePictureBounds::BarAxes::TOP_BOTTOM};
    }
    static void AssertSameBounds(const ActivePictureBounds& expected,
        const ActivePictureBounds& actual)
    {
        Assert::AreEqual(expected.left,actual.left);
        Assert::AreEqual(expected.top,actual.top);
        Assert::AreEqual(expected.right,actual.right);
        Assert::AreEqual(expected.bottom,actual.bottom);
        Assert::AreEqual(expected.rasterWidth,actual.rasterWidth);
        Assert::AreEqual(expected.rasterHeight,actual.rasterHeight);
    }
    static void AssertLoggedFill(const AspectLimitFillDecision& result)
    {
        Assert::IsTrue(result.applied);
        Assert::AreEqual(38,result.sourceBounds.left);
        Assert::AreEqual(278,result.sourceBounds.top);
        Assert::AreEqual(3802,result.sourceBounds.right);
        Assert::AreEqual(1880,result.sourceBounds.bottom);
    }
    static SubtitleCutPasteGeometry MappedGlyphs(const ActivePictureBounds& glyphs,
        int targetLeft)
    {
        SubtitleCutPasteGeometry moved;moved.valid=true;
        // The panel may cover the whole raster. Only the mapped owned glyphs
        // determine whether the intentional horizontal trim clips a caption.
        moved.source={0,glyphs.top,3840,glyphs.bottom};
        moved.destination={0,1660,3840,1660+glyphs.bottom-glyphs.top};
        moved.content={glyphs.left,glyphs.top,glyphs.right,glyphs.bottom};
        moved.glyphLines[0]=moved.content;
        moved.centeredGlyphMapping=true;moved.glyphScale=1.0f;
        moved.glyphTranslateX=float(targetLeft-glyphs.left);
        moved.glyphTranslateY=float(1660-glyphs.top);
        return moved;
    }
    struct Fixture
    {
        SubmittedNativeScopeAnchor anchor;
        CropPresentationAdmissionState admission;

        FinalAspectLimitFillInput Frame(uint64_t sequence,bool unknown=false,
            bool recovery=false) const
        {
            FinalAspectLimitFillInput input;
            auto& crop=input.crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            crop.geometry=NativeScope();
            crop.frameSourceGeneration=crop.geometrySourceGeneration=7;
            crop.frameSourceSequence=sequence;crop.framePresentationEpoch=13;
            crop.rasterWidth=3840;crop.rasterHeight=2160;
            crop.latestObservationSupportsCrop=!recovery;
            crop.latestObservationIsUnavailable=unknown;
            crop.latestObservationIsProvisional=recovery && !unknown;
            crop.latestObservationClassification=unknown?ActivePictureClassification::UNAVAILABLE:
                recovery?ActivePictureClassification::PROVISIONAL:ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.nearBlackEpisodeRetainCrop=recovery;
            crop.frameLocalPresentationRetentionEvaluated=!unknown;
            crop.frameLocalPresentationRetentionSafe=!unknown;
            input.presentation=Evaluate(crop);
            auto& evidence=input.evidence;
            evidence.crop=crop;evidence.candidate=input.presentation;
            evidence.previousAdmission=admission;evidence.submittedScopeAnchor=anchor;
            evidence.presentationEpoch=crop.framePresentationEpoch;
            evidence.measurementCurrent=evidence.retentionEvaluated=evidence.observationAvailable=!unknown;
            evidence.excludedBandsPixelSafe=!unknown;
            evidence.retentionBounds=crop.geometry;
            evidence.retentionSourceGeneration=unknown?0:crop.frameSourceGeneration;
            evidence.retentionSourceSequence=unknown?0:sequence;
            evidence.observationClassification=crop.latestObservationClassification;
            evidence.observation=unknown?ActivePictureBounds{}:crop.geometry;
            evidence.observedTrustedCrop=unknown?ActivePictureBounds{}:crop.geometry;
            evidence.nearBlackEvaluated=recovery;evidence.globalNearBlack=recovery;
            input.configuredScreenActive=true;
            input.configured.screenAspect=2.35;
            input.configured.cropNarrowerContentToFillScreen=true;
            input.configured.narrowerLimitConfigured=true;input.configured.narrowerAspectLimit=2.32;
            input.configured.cropWiderContentToFillScreen=true;
            input.configured.widerLimitConfigured=true;input.configured.widerAspectLimit=2.42;
            input.currentConfirmedPresentationConflict=false;
            input.samplingCompetingPresentation=false;
            input.nearBlackRetainedScopeFallback=recovery;
            return input;
        }
        SubmittedNativeScopeAnchorUpdateInput Selected(const FinalAspectLimitFillInput& input,
            const AspectLimitFillDecision& fill,bool submitted=true,
            SubtitlePreviewMode mode=SubtitlePreviewMode::None,bool captionHeld=false)
        {
            const auto admitted=AdmitCropPresentation(admission,input.crop,
                input.presentation,input.crop.framePresentationEpoch);
            Assert::IsFalse(admitted.blocked);
            auto selected=admitted.state;
            selected.optionalFillApplied=CanRecordSubtitleOptionalAspectFill(input.crop,
                input.presentation,fill,mode,captionHeld,input.fixedCropAspectConfigured,
                input.nlsOwnsPresentationGeometry,input.protectedCaptionFit,
                input.recoveryBoundedPresentation || input.nearBlackRetainedScopeFallback,
                input.crop.authorityOrigin);
            selected.optionalFillRetained=selected.optionalFillApplied && fill.retainedSubmittedFill;
            selected.optionalFillSourceBounds=selected.optionalFillApplied?fill.sourceBounds:ActivePictureBounds{};
            selected.optionalFillSourceSequence=input.crop.frameSourceSequence;
            selected.optionalFillScreenAspect=input.configured.screenAspect;
            admission=selected;
            SubmittedNativeScopeAnchorUpdateInput commit;
            commit.previous=anchor;commit.selectedAdmission=selected;
            commit.nativeCropAvailable=admitted.state.available;commit.nativeCrop=admitted.state.trustedCrop;
            commit.submittedBounds=fill.applied?fill.sourceBounds:input.presentation.sourceBounds;
            commit.authorityOrigin=input.crop.authorityOrigin;commit.selectedOwner=input.presentation.owner;
            commit.sourceGeneration=input.crop.frameSourceGeneration;commit.sourceSequence=input.crop.frameSourceSequence;
            commit.presentationEpoch=input.crop.framePresentationEpoch;
            commit.fullRasterAuthority=input.crop.fullRasterPresentationAuthoritative;
            commit.frameSubmitted=submitted;
            return commit;
        }
        void Submit(const FinalAspectLimitFillInput& input,const AspectLimitFillDecision& fill,
            bool submitted=true,SubtitlePreviewMode mode=SubtitlePreviewMode::None,bool captionHeld=false)
        {
            anchor=CommitSubmittedNativeScopeAnchor(Selected(input,fill,submitted,mode,captionHeld));
        }
        void Seed()
        {
            const auto input=Frame(100);
            Assert::IsTrue(input.presentation.applyCrop);
            const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
            Assert::IsFalse(fill.retainedSubmittedFill);
            Submit(input,fill);
            Assert::IsTrue(anchor.available && anchor.submittedOptionalFillAvailable);
            AssertSameBounds(NativeScope(),anchor.crop);
            AssertSameBounds(fill.sourceBounds,anchor.submittedOptionalFill);
            Assert::AreEqual(uint64_t(100),anchor.lastSubmittedSourceSequence);
            Assert::AreEqual(uint64_t(100),anchor.submittedOptionalFillSourceSequence);
        }
    };
public:
    TEST_METHOD(LoggedNativeFillSurvivesThreeMeasuredSkippedNearBlackCyclesWithoutRenewingAr)
    {
        Fixture fixture;fixture.Seed();
        for(uint64_t sequence=101;sequence<=106;++sequence)
        {
            auto input=fixture.Frame(sequence,sequence%2==0,true);
            Assert::AreEqual(int(DecisionOwner::NEAR_BLACK_EPISODE),int(input.presentation.owner));
            const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
            Assert::IsTrue(fill.retainedSubmittedFill);
            fixture.Submit(input,fill);
            Assert::IsTrue(fixture.anchor.submittedOptionalFillAvailable);
            Assert::AreEqual(uint64_t(100),fixture.anchor.lastSubmittedSourceSequence,
                L"Retaining displayed fill must never renew native picture authority.");
            Assert::AreEqual(sequence,fixture.anchor.lastSubmittedPresentationSourceSequence);
            Assert::AreEqual(sequence,fixture.anchor.submittedOptionalFillSourceSequence);
            AssertSameBounds(NativeScope(),fixture.anchor.crop);
        }
    }
    TEST_METHOD(MeasuredUncertainRetentionAndSkippedRecoveryUseTheSameSubmittedFill)
    {
        Fixture fixture;fixture.Seed();
        for(uint64_t sequence=101;sequence<=108;++sequence)
        {
            auto input=fixture.Frame(sequence,sequence%2==0,true);
            input.uncertainPicture.retained=sequence%2!=0;
            input.uncertainPicture.nativeCrop=NativeScope();input.uncertainPicture.presentation=input.presentation;
            const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
            Assert::IsTrue(fill.retainedSubmittedFill);
            fixture.Submit(input,fill);
            Assert::AreEqual(uint64_t(100),fixture.anchor.lastSubmittedSourceSequence);
        }
    }
    TEST_METHOD(RawProvisionalOccupancyAndHeldCaptionFlagsDoNotRevokeSubmittedSideFill)
    {
        Fixture fixture;fixture.Seed();
        auto input=fixture.Frame(101,false,true);
        input.samplingCompetingPresentation=true;
        input.crop.currentVisibleBoundsAvailable=true;input.crop.currentVisibleBase=NativeScope();
        input.crop.currentVisibleSourceGeneration=7;input.crop.currentVisibleSourceSequence=101;
        input.crop.currentVisibleBounds={0,0,3840,2160,3840,2160};
        input.evidence.observation=input.crop.currentVisibleBounds;
        input.evidence.crop=input.crop;
        const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
        Assert::IsTrue(fill.retainedSubmittedFill);
        AssertSameBounds(NativeScope(),input.presentation.sourceBounds);
    }
    TEST_METHOD(ConfirmedConflictCannotBypassTheVetoThroughMeasuredUncertainRetention)
    {
        for(bool measured:{false,true})for(bool retained:{false,true})
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,!measured,true);
            input.uncertainPicture.retained=retained;
            input.uncertainPicture.nativeCrop=NativeScope();input.uncertainPicture.presentation=input.presentation;
            input.currentConfirmedPresentationConflict=true;
            const auto fill=EvaluateFinalAspectLimitFill(input);
            Assert::IsFalse(fill.applied);Assert::IsFalse(fill.retainedSubmittedFill);
        }
    }
    TEST_METHOD(CurrentScreenLimitsAndFillTogglesTakeEffectImmediately)
    {
        for(int variant=0;variant<6;++variant)
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,true,true);
            if(variant==0)input.configured.screenAspect=2.36;
            if(variant==1)input.configuredScreenActive=false;
            if(variant==2)input.configured.cropWiderContentToFillScreen=false;
            if(variant==3)input.configured.widerAspectLimit=2.38;
            if(variant==4)input.configured.screenAspect=16.0/9.0;
            if(variant==5)input.configured.screenAspect=0.0;
            const auto fill=EvaluateFinalAspectLimitFill(input);
            Assert::IsFalse(fill.applied);Assert::IsFalse(fill.retainedSubmittedFill);
        }
    }
    TEST_METHOD(ChangedSourceEpochNativeBoundsAndRasterCannotBorrowTheCertificate)
    {
        for(int variant=0;variant<7;++variant)
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,true,true);
            if(variant==0)++input.crop.frameSourceGeneration;
            if(variant==1)++input.crop.framePresentationEpoch;
            if(variant==2)input.crop.geometry.top+=2;
            if(variant==3)input.crop.geometry.bottom-=2;
            if(variant==4)input.crop.rasterWidth=1920;
            if(variant==5)input.crop.geometrySourceGeneration=6;
            if(variant==6)++input.evidence.presentationEpoch;
            const auto fill=EvaluateFinalAspectLimitFill(input);
            Assert::IsFalse(fill.retainedSubmittedFill);
            Assert::IsFalse(fill.applied);
        }
    }
    TEST_METHOD(ProposalAndFailedSubmissionCannotSeedAnOptionalFillWitness)
    {
        for(int variant=0;variant<3;++variant)
        {
            Fixture fixture;const auto input=fixture.Frame(100);
            auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
            if(variant==0)fixture.Submit(input,fill,false);
            if(variant==1){fill.applied=false;fixture.Submit(input,fill);}
            if(variant==2)
            {
                auto commit=fixture.Selected(input,fill);
                commit.selectedAdmission.optionalFillRetained=true;
                fixture.anchor=CommitSubmittedNativeScopeAnchor(commit);
            }
            Assert::IsFalse(fixture.anchor.submittedOptionalFillAvailable);
            const auto held=EvaluateFinalAspectLimitFill(fixture.Frame(101,true,true));
            Assert::IsFalse(held.applied);Assert::IsFalse(held.retainedSubmittedFill);
        }
    }
    TEST_METHOD(SelectedWitnessMustMatchSourceAdmissionAndActualCenteredOutput)
    {
        for(int variant=0;variant<8;++variant)
        {
            Fixture fixture;const auto input=fixture.Frame(100);
            const auto fill=EvaluateFinalAspectLimitFill(input);
            auto commit=fixture.Selected(input,fill);
            if(variant==0)commit.selectedAdmission.optionalFillApplied=false;
            if(variant==1)--commit.selectedAdmission.optionalFillSourceSequence;
            if(variant==2)++commit.selectedAdmission.sourceGeneration;
            if(variant==3)++commit.selectedAdmission.presentationEpoch;
            if(variant==4)commit.selectedAdmission.trustedCrop.top+=2;
            if(variant==5){commit.submittedBounds.left+=2;commit.selectedAdmission.optionalFillSourceBounds=commit.submittedBounds;}
            if(variant==6)commit.selectedAdmission.optionalFillScreenAspect=std::numeric_limits<double>::quiet_NaN();
            if(variant==7)commit.selectedAdmission.optionalFillRetained=true;
            const auto anchor=CommitSubmittedNativeScopeAnchor(commit);
            Assert::IsFalse(anchor.submittedOptionalFillAvailable);
        }
    }
    TEST_METHOD(FailedRetainedSubmissionDoesNotAdvanceNativeOrFillWatermarks)
    {
        Fixture fixture;fixture.Seed();const auto prior=fixture.anchor;
        const auto input=fixture.Frame(101,true,true);
        const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
        fixture.Submit(input,fill,false);
        Assert::AreEqual(prior.lastSubmittedSourceSequence,fixture.anchor.lastSubmittedSourceSequence);
        Assert::AreEqual(prior.lastSubmittedPresentationSourceSequence,fixture.anchor.lastSubmittedPresentationSourceSequence);
        Assert::AreEqual(prior.submittedOptionalFillSourceSequence,fixture.anchor.submittedOptionalFillSourceSequence);
        AssertSameBounds(prior.submittedOptionalFill,fixture.anchor.submittedOptionalFill);
        Assert::IsTrue(EvaluateFinalAspectLimitFill(fixture.Frame(102,true,true)).retainedSubmittedFill);
    }
    TEST_METHOD(SuccessfulSupersedingOutputRetiresTheFillCertificate)
    {
        for(int variant=0;variant<4;++variant)
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,false,true);
            auto selected=fixture.Selected(input,{});
            selected.selectedAdmission.optionalFillApplied=selected.selectedAdmission.optionalFillRetained=false;
            selected.submittedBounds=NativeScope();
            if(variant==1){selected.selectedOwner=DecisionOwner::OUTWARD_FIT;selected.submittedBounds.bottom=2000;}
            if(variant==2){selected.selectedOwner=DecisionOwner::VERTICAL_TRANSLATION;selected.submittedBounds.top+=40;selected.submittedBounds.bottom+=40;}
            if(variant==3){selected.fullRasterAuthority=true;selected.selectedOwner=DecisionOwner::FULL_RASTER;selected.submittedBounds={0,0,3840,2160,3840,2160};}
            fixture.anchor=CommitSubmittedNativeScopeAnchor(selected);
            Assert::IsFalse(fixture.anchor.submittedOptionalFillAvailable);
            const auto result=EvaluateFinalAspectLimitFill(fixture.Frame(102,true,true));
            Assert::IsFalse(result.retainedSubmittedFill);
        }
    }
    TEST_METHOD(RealExpansionTranslationMovingNlsAndProtectedOwnersAreNeverExempted)
    {
        for(int variant=0;variant<10;++variant)
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,true,true);
            if(variant==0){input.presentation.outwardExpanded=true;input.presentation.sourceBounds.bottom=2000;}
            if(variant==1){input.presentation.verticallyTranslated=true;input.presentation.sourceBounds.top+=40;input.presentation.sourceBounds.bottom+=40;}
            if(variant==2)input.presentation.owner=DecisionOwner::OUTWARD_FIT;
            if(variant==3)input.crop.movingPictureTransition=true;
            if(variant==4)input.nlsOwnsPresentationGeometry=true;
            if(variant==5)input.nlsPresentationFailOpen=true;
            if(variant==6)input.protectedCaptionFit=true;
            if(variant==7)input.crop.fullRasterPresentationAuthoritative=true;
            if(variant==8)input.crop.barCropRefinementHorizontalConflict=true;
            if(variant==9)input.uncertainPicture.expanded=true;
            Assert::IsFalse(EvaluateFinalAspectLimitFill(input).retainedSubmittedFill);
        }
    }
    TEST_METHOD(NoCaptionMovedCueAndHeldUnknownSnapshotKeepTheSameNativeFill)
    {
        for(const auto mode:{SubtitlePreviewMode::None,SubtitlePreviewMode::BlackBackground,
            SubtitlePreviewMode::GeneratedGrayBackground})
        {
            Fixture fixture;fixture.Seed();
            for(uint64_t sequence=101;sequence<=106;++sequence)
            {
                const bool measured=sequence<104,confirmed=sequence!=101 && measured;
                const bool held=sequence>=104;
                const ActivePictureBounds caption={1400,1820,2440,1980,3840,2160};
                const auto relocated=MappedGlyphs(caption,1450);
                auto input=fixture.Frame(sequence,!measured,true);
                input.currentConfirmedPresentationConflict=ConfirmedSubtitleConflictsWithHorizontalFill(
                    confirmed,measured,mode,mode!=SubtitlePreviewMode::None,caption,relocated,
                    fixture.anchor.submittedOptionalFill);
                Assert::IsFalse(input.currentConfirmedPresentationConflict);
                const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
                Assert::IsTrue(fill.retainedSubmittedFill);
                fixture.Submit(input,fill,true,mode,held);
                Assert::IsTrue(fixture.anchor.submittedOptionalFillAvailable);
                Assert::AreEqual(uint64_t(100),fixture.anchor.lastSubmittedSourceSequence);
                Assert::AreEqual(sequence,fixture.anchor.submittedOptionalFillSourceSequence);
            }
        }
    }
    TEST_METHOD(OnlyFreshConfirmedDisplayedGlyphFootprintsVetoHorizontalFill)
    {
        const ActivePictureBounds nativeOutside={4,1840,400,1970,3840,2160};
        const ActivePictureBounds nativeInside={1400,1840,2440,1970,3840,2160};
        const ActivePictureBounds filled={38,278,3802,1880,3840,2160};
        auto moved=MappedGlyphs(nativeOutside,1400);
        Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,
            SubtitlePreviewMode::None,false,nativeOutside,moved,filled));
        for(const auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
        {
            moved=MappedGlyphs(nativeOutside,1400);
            Assert::IsFalse(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,nativeOutside,moved,filled),
                L"A successfully erased native source card must not veto safely centered moved glyphs.");
            moved=MappedGlyphs(nativeInside,4);
            Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,nativeInside,moved,filled));
            moved=MappedGlyphs(nativeInside,2800);
            Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,nativeInside,moved,filled));
            moved=MappedGlyphs(nativeOutside,1400);
            Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,false,nativeOutside,moved,filled));
            Assert::IsFalse(ConfirmedSubtitleConflictsWithHorizontalFill(false,true,mode,false,nativeOutside,moved,filled));
            Assert::IsFalse(ConfirmedSubtitleConflictsWithHorizontalFill(true,false,mode,false,nativeOutside,moved,filled));
        }
    }
    TEST_METHOD(BlankFullWidthPanelsDoNotVetoSafeGlyphsButMappedGlyphsOutsideTheTrimDo)
    {
        const ActivePictureBounds inside={100,1840,3740,1970,3840,2160};
        const ActivePictureBounds nearTrim={60,1840,300,1970,3840,2160};
        const ActivePictureBounds filled={38,278,3802,1880,3840,2160};
        for(const auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
        {
            auto moved=MappedGlyphs(inside,100);
            Assert::AreEqual(0,moved.destination.left);Assert::AreEqual(3840,moved.destination.right);
            Assert::IsFalse(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,inside,moved,filled));
            moved.centeredGlyphMapping=false;
            Assert::IsFalse(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,inside,moved,filled));
            moved=MappedGlyphs(nearTrim,10);
            Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,nearTrim,moved,filled));
            moved=MappedGlyphs(nearTrim,60);moved.centeredGlyphMapping=false;
            moved.destination.left=-50;moved.destination.right=3790;
            Assert::IsTrue(ConfirmedSubtitleConflictsWithHorizontalFill(true,true,mode,true,nearTrim,moved,filled));
        }
    }
    TEST_METHOD(AdvancedLogicalTranslationWithNativeActualBoundsRetainsFillButRealShiftDoesNot)
    {
        for(const auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
        {
            Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,true,true);
            input.presentation.owner=DecisionOwner::VERTICAL_TRANSLATION;
            input.presentation.verticallyTranslated=true;input.presentation.verticalTranslationPixels=32;
            input.crop.verticalTranslationActive=true;input.crop.verticalTranslationPixels=32;
            input.crop.verticalTranslationSourceGeneration=7;input.crop.verticalTranslationBase=NativeScope();
            const auto fill=EvaluateFinalAspectLimitFill(input);AssertLoggedFill(fill);
            Assert::IsTrue(fill.retainedSubmittedFill);
            Assert::IsTrue(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,
                SubtitlePreviewMode::None,false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            auto shifted=input;shifted.presentation.sourceBounds.top+=32;shifted.presentation.sourceBounds.bottom+=32;
            Assert::IsFalse(EvaluateFinalAspectLimitFill(shifted).retainedSubmittedFill);
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(shifted.crop,shifted.presentation,fill,mode,
                false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
        }
    }
    TEST_METHOD(QueuedRetainedFillRevalidatesTheLiveSelectedCertificateBeforeUsingItsGeometry)
    {
        for(bool supersedingTranslation:{false,true})
        {
            Fixture live;live.Seed();
            // N was prepared using successfully submitted K. Its immutable
            // admission packet does not acquire the right to overrule N-1.
            auto preparation=live;
            const auto preparedFrame=preparation.Frame(102,true,true);
            const auto preparedFill=EvaluateFinalAspectLimitFill(preparedFrame);AssertLoggedFill(preparedFill);
            Assert::IsTrue(preparedFill.retainedSubmittedFill);
            auto queued=preparation.Selected(preparedFrame,preparedFill,false);
            const auto allowed=RevalidateRetainedNativeFillSelection(queued);
            Assert::IsFalse(allowed.deauthorized);
            AssertSameBounds(preparedFill.sourceBounds,allowed.sourceBounds);
            Assert::IsTrue(allowed.selectedAdmission.optionalFillApplied &&
                allowed.selectedAdmission.optionalFillRetained && allowed.nativeCropAvailable);
            Assert::AreEqual(uint64_t(100),live.anchor.submittedOptionalFillSourceSequence,
                L"A geometry-only dry run must not publish its candidate to live submission state.");

            auto interveningFrame=live.Frame(101,true,true);
            auto intervening=live.Selected(interveningFrame,{});
            if(supersedingTranslation)
            {
                intervening.selectedOwner=DecisionOwner::VERTICAL_TRANSLATION;
                intervening.submittedBounds.top+=40;intervening.submittedBounds.bottom+=40;
            }
            live.anchor=CommitSubmittedNativeScopeAnchor(intervening);
            Assert::IsFalse(live.anchor.submittedOptionalFillAvailable);
            const auto nativeWatermark=live.anchor.lastSubmittedSourceSequence;

            // Selection must consult the live post-N-1 anchor, not the lease
            // copy held by N's earlier analysis/admission snapshot.
            queued.previous=live.anchor;
            const auto rejected=RevalidateRetainedNativeFillSelection(queued);
            Assert::IsTrue(rejected.deauthorized);
            AssertSameBounds(NativeScope(),rejected.sourceBounds);
            Assert::IsFalse(rejected.selectedAdmission.optionalFillApplied);
            Assert::IsFalse(rejected.selectedAdmission.optionalFillRetained);
            Assert::IsFalse(rejected.nativeCropAvailable);
            queued.submittedBounds=rejected.sourceBounds;
            queued.selectedAdmission=rejected.selectedAdmission;
            queued.nativeCropAvailable=rejected.nativeCropAvailable;
            queued.frameSubmitted=true;
            const auto afterFallback=CommitSubmittedNativeScopeAnchor(queued);
            Assert::IsFalse(afterFallback.submittedOptionalFillAvailable);
            Assert::AreEqual(nativeWatermark,afterFallback.lastSubmittedSourceSequence,
                L"A stale geometry fallback cannot renew native AR merely because it displays the native rectangle.");

            auto fresh=queued;fresh.nativeCropAvailable=true;
            fresh.selectedAdmission.optionalFillRetained=false;
            fresh.selectedAdmission.optionalFillApplied=true;
            fresh.submittedBounds=preparedFill.sourceBounds;
            const auto ordinary=RevalidateRetainedNativeFillSelection(fresh);
            Assert::IsFalse(ordinary.deauthorized,
                L"The selected-lease guard leaves ordinary fresh-fill admission to its existing contract.");
            AssertSameBounds(fresh.submittedBounds,ordinary.sourceBounds);
            Assert::IsTrue(ordinary.selectedAdmission.optionalFillApplied && ordinary.nativeCropAvailable);
        }
    }
    TEST_METHOD(RetainedWitnessRecordingDoesNotPermitNewFillDuringRecoveryOrUnsafePlacement)
    {
        Fixture fixture;fixture.Seed();auto input=fixture.Frame(101,true,true);
        auto fill=EvaluateFinalAspectLimitFill(input);Assert::IsTrue(fill.retainedSubmittedFill);
        for(const auto mode:{SubtitlePreviewMode::None,SubtitlePreviewMode::BlackBackground,
            SubtitlePreviewMode::GeneratedGrayBackground})
        {
            Assert::IsTrue(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsTrue(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                true,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            auto fresh=fill;fresh.retainedSubmittedFill=false;
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fresh,mode,
                false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fresh,mode,
                true,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                false,true,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                false,false,true,false,true,ActivePictureAuthorityOrigin::NATIVE));
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,input.presentation,fill,mode,
                false,false,false,true,true,ActivePictureAuthorityOrigin::NATIVE));
            auto expanded=input.presentation;expanded.outwardExpanded=true;expanded.sourceBounds.bottom+=20;
            Assert::IsFalse(CanRecordSubtitleOptionalAspectFill(input.crop,expanded,fill,mode,
                false,false,false,false,true,ActivePictureAuthorityOrigin::NATIVE));
        }
    }
};
}
