#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        ActivePictureBounds IncidentScope()
        {
            return {0,278,3840,1880,3840,2160,3840.0/1602.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
        }
        ActivePictureBounds VisibleBounds(int top,int bottom)
        {
            return {0,top,3840,bottom,3840,2160,3840.0/(bottom-top),
                ActivePictureBounds::BarAxes::NONE};
        }
        void AssertBounds(const ActivePictureBounds& expected,const ActivePictureBounds& actual)
        {
            Assert::AreEqual(expected.left,actual.left);
            Assert::AreEqual(expected.top,actual.top);
            Assert::AreEqual(expected.right,actual.right);
            Assert::AreEqual(expected.bottom,actual.bottom);
            Assert::AreEqual(expected.rasterWidth,actual.rasterWidth);
            Assert::AreEqual(expected.rasterHeight,actual.rasterHeight);
        }
        PresentationRecoveryInput UnknownIncident(uint64_t sequence=9426,bool nearBlack=false)
        {
            PresentationRecoveryInput input;
            auto& crop=input.crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.geometry=IncidentScope();
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=2;
            crop.frameSourceSequence=sequence;crop.framePresentationEpoch=9;
            crop.rasterWidth=3840;crop.rasterHeight=2160;
            crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=true;
            crop.currentVisibleBoundsAvailable=true;
            crop.currentVisibleBase=crop.geometry;
            crop.currentVisibleBounds=VisibleBounds(0,2160);
            crop.currentVisibleSourceGeneration=2;crop.currentVisibleSourceSequence=sequence;
            crop.verticalTranslationBase=crop.geometry;
            crop.verticalTranslationSourceGeneration=2;
            crop.nearBlackEpisodeRetainCrop=nearBlack;
            input.previousAdmission.available=true;
            input.previousAdmission.trustedCrop=crop.geometry;
            input.previousAdmission.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            input.previousAdmission.sourceGeneration=2;input.previousAdmission.presentationEpoch=9;
            input.submittedScopeAnchor.available=true;
            input.submittedScopeAnchor.crop=crop.geometry;
            input.submittedScopeAnchor.sourceGeneration=2;
            input.submittedScopeAnchor.presentationEpoch=9;
            input.submittedScopeAnchor.lastSubmittedSourceSequence=sequence-1;
            input.submittedScopeAnchor.lastSubmittedPresentationSourceSequence=sequence-1;
            input.measurementCurrent=input.retentionEvaluated=input.nearBlackEvaluated=true;
            input.observationAvailable=true;
            input.observationClassification=ActivePictureClassification::PROVISIONAL;
            input.observation={1138,280,3594,2160,3840,2160,2456.0/1880.0,
                ActivePictureBounds::BarAxes::NONE};
            input.retentionBounds=crop.geometry;input.retentionSourceGeneration=2;
            input.retentionSourceSequence=sequence;
            input.globalNearBlack=nearBlack;input.presentationEpoch=9;input.framesPerSecond=23.976;
            input.candidate.sourceBounds=VisibleBounds(0,2160);
            input.candidate.reason="unresolved observation has no independent presentation owner";
            return input;
        }
        NearBlackPresentationEpisodeState RetainedEpisode(const PresentationRecoveryInput& input)
        {
            NearBlackPresentationEpisodeState episode;
            episode.mode=NearBlackPresentationMode::RETAIN_CROP;
            episode.sourceGeneration=input.crop.frameSourceGeneration;
            episode.presentationEpoch=input.presentationEpoch;
            episode.startedSourceSequence=input.crop.frameSourceSequence-6;
            episode.entryTrustedCropAvailable=true;
            episode.entryTrustedCrop=input.crop.geometry;
            episode.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;
            return episode;
        }
        Decision ResolveIncident(PresentationRecoveryInput input)
        {
            auto candidate=input.candidate;
            if(input.crop.nearBlackEpisodeRetainCrop)
                candidate=EvaluateNearBlackBoundedPresentation(RetainedEpisode(input),input).presentation;
            return ResolveUncertainPicturePresentation(input,candidate).presentation;
        }
        void MakeNativeCurrent(PresentationRecoveryInput& input,const ActivePictureBounds& bounds)
        {
            auto& crop=input.crop;
            crop.geometry=bounds;crop.latestObservationSupportsCrop=true;
            crop.latestObservationIsProvisional=crop.latestObservationIsUnavailable=false;
            crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.currentVisibleBase=bounds;crop.currentVisibleBounds=bounds;
            input.retentionBounds=bounds;input.observation=bounds;
            input.observationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
        }
    }

    TEST_CLASS(RetainedScopeUnknownPresentationTests)
    {
    public:
        TEST_METHOD(LoggedPartialAndFullExtentsCannotOriginateAnUnknownFallbackEnvelope)
        {
            // 9425 exposed top=0..1992; 9426 and 9455 exposed the whole raster.
            // In all three cases the same native Scope remained established.
            for(bool nearBlack:{false,true})
            for(bool pendingFit:{false,true})
            for(int extent:{0,1,2})
            for(int evidence:{0,1,2})
            {
                auto input=UnknownIncident(extent==2?9455:9425+extent,nearBlack);
                input.crop.currentVisibleBounds=extent==0?VisibleBounds(0,1992):
                    extent==1?VisibleBounds(0,2160):VisibleBounds(0,2160);
                input.crop.verticalFitConfirmationPending=pendingFit;
                if(evidence==1)
                {
                    input.crop.latestObservationIsUnavailable=true;
                    input.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                    input.observationAvailable=false;
                    input.observationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                if(evidence==2)
                {
                    --input.crop.currentVisibleSourceSequence;
                    input.measurementCurrent=false;
                }
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop,
                    L"Unknown or stale visible pixels cannot turn an established native Scope into full raster.");
                AssertBounds(IncidentScope(),output.sourceBounds);
                Assert::IsFalse(output.outwardExpanded,
                    L"A partial near-full envelope is also an AR jump, even when it is not exactly full raster.");
                const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,output,input.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                AssertBounds(IncidentScope(),admitted.presentation.sourceBounds);
                AssertBounds(IncidentScope(),admitted.state.trustedCrop);
                Assert::IsFalse(admitted.state.outwardPresentationAvailable,
                    L"Unconfirmed visible extent may not become a persistent outward FIT owner.");
            }
        }

        TEST_METHOD(UnknownFrameCanRetainAnOwnedSubmittedEnvelopeWithoutGrowingIt)
        {
            for(bool nearBlack:{false,true})
            {
                auto input=UnknownIncident(9455,nearBlack);
                const auto owned=VisibleBounds(278,1992);
                auto& anchor=input.submittedScopeAnchor;
                anchor.lastSubmittedSourceSequence=9453;
                anchor.lastSubmittedPresentationSourceSequence=9454;
                anchor.submittedEnvelopeAvailable=true;anchor.submittedEnvelope=owned;
                anchor.submittedEnvelopeSourceSequence=9454;
                input.previousAdmission.outwardPresentationAvailable=true;
                input.previousAdmission.outwardPresentation=owned;
                input.previousAdmission.presentationSourceSequence=9454;
                // A new raw full extent must not grow the existing owned envelope.
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsTrue(output.retained && output.expanded && output.presentation.applyCrop);
                AssertBounds(owned,output.presentation.sourceBounds);
                AssertBounds(owned,ResolveIncident(input).sourceBounds);
                AssertBounds(IncidentScope(),output.nativeCrop);
                Assert::IsFalse(output.currentPixelExtent,
                    L"Retention of an already submitted envelope is not a new current-pixel expansion certificate.");
                Assert::AreEqual(uint64_t(9453),anchor.lastSubmittedSourceSequence,
                    L"Presentation retention cannot renew the native AR acquisition watermark.");
                Assert::AreEqual(uint64_t(9454),anchor.submittedEnvelopeSourceSequence);
            }
        }

        TEST_METHOD(PendingFitOrAConfirmationFlagCannotPromoteADifferentUnownedCandidate)
        {
            auto input=UnknownIncident();
            input.crop.verticalFitConfirmationPending=true;
            input.confirmedPresentationResolved=true;
            input.candidate.applyCrop=true;input.candidate.outwardExpanded=true;
            input.candidate.owner=DecisionOwner::OUTWARD_FIT;
            input.candidate.sourceBounds=VisibleBounds(200,1996);
            Decision different;
            different.sourceBounds=VisibleBounds(0,2160);
            different.reason="the actually admitted candidate has no matching confirmed owner";
            const auto output=ResolveUncertainPicturePresentation(input,different);
            Assert::IsTrue(output.retained && output.presentation.applyCrop);
            AssertBounds(IncidentScope(),output.presentation.sourceBounds);
        }

        TEST_METHOD(UnconfirmedVerticalFitCannotBypassScopeRetentionViaTheCroppedFastPath)
        {
            for(int extent:{0,1,2,3})
            {
                auto input=UnknownIncident();
                const auto proposed=extent==0?VisibleBounds(60,2160):
                    extent==1?VisibleBounds(0,1992):
                    extent==2?VisibleBounds(200,1996):VisibleBounds(0,2160);
                input.crop.outwardPresentationActive=input.crop.outwardExpansionAvailable=true;
                input.crop.outwardExpansion=proposed;
                input.crop.outwardExpansionSourceGeneration=input.crop.frameSourceGeneration;
                input.candidate=Evaluate(input.crop);
                Assert::IsTrue(input.candidate.applyCrop && input.candidate.owner==DecisionOwner::OUTWARD_FIT);
                Assert::IsFalse(input.confirmedPresentationResolved);
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsTrue(output.retained && output.presentation.applyCrop);
                AssertBounds(IncidentScope(),output.presentation.sourceBounds);
                Assert::IsFalse(output.presentation.outwardExpanded,
                    L"An unconfirmed label and a valid partial crop cannot promote unknown bar occupancy into a new vertical picture shape.");
            }
        }

        TEST_METHOD(CurrentPixelBoundedSideFitKeepsItsIndependentHorizontalOwner)
        {
            auto input=UnknownIncident();
            const ActivePictureBounds window{100,278,3740,1880,3840,2160,3640.0/1602.0,
                ActivePictureBounds::BarAxes::BOTH};
            auto& crop=input.crop;
            crop.geometry=window;crop.currentVisibleBase=window;
            crop.currentVisibleBounds=VisibleBounds(278,1880);
            crop.barCropRefinementHorizontalConflict=true;
            crop.outwardPresentationActive=crop.outwardExpansionAvailable=true;
            crop.outwardExpansion=crop.currentVisibleBounds;
            crop.outwardExpansionSourceGeneration=crop.frameSourceGeneration;
            input.retentionBounds=window;input.previousAdmission.trustedCrop=window;
            input.submittedScopeAnchor.crop=window;
            input.candidate=Evaluate(crop);
            Assert::IsTrue(input.candidate.applyCrop && input.candidate.horizontalExpansionPixelBounded,
                L"The positive fixture uses the existing current-source horizontal pixel certificate.");
            Assert::IsTrue(input.candidate.owner==DecisionOwner::OUTWARD_FIT);
            Assert::IsFalse(input.confirmedPresentationResolved);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            Assert::IsTrue(output.presentation.owner==DecisionOwner::OUTWARD_FIT);
            AssertBounds(crop.currentVisibleBounds,output.presentation.sourceBounds);
        }
        TEST_METHOD(CurrentConfirmedCaptionFitKeepsItsActualPartialOrFullPresentation)
        {
            for(bool nearBlack:{false,true})
            for(bool full:{false,true})
            {
                auto input=UnknownIncident(9455,nearBlack);
                input.crop.outwardPresentationActive=input.crop.outwardExpansionAvailable=true;
                input.crop.outwardExpansion=full?VisibleBounds(0,2160):VisibleBounds(200,1996);
                input.crop.outwardExpansionSourceGeneration=input.crop.frameSourceGeneration;
                // The confirmed dense caption owner is an independent accepted
                // presentation. It is not inferred from currentVisibleBounds.
                auto evaluated=input.crop;
                evaluated.nearBlackEpisodeRetainCrop=false;
                input.candidate=Evaluate(evaluated);
                Assert::IsTrue(input.candidate.applyCrop);
                Assert::IsTrue(input.candidate.owner==DecisionOwner::OUTWARD_FIT);
                AssertBounds(input.crop.outwardExpansion,input.candidate.sourceBounds);
                input.confirmedPresentationResolved=true;
                const auto output=ResolveIncident(input);
                AssertBounds(input.candidate.sourceBounds,output.sourceBounds);
                Assert::IsTrue(output.owner==DecisionOwner::OUTWARD_FIT);
                const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,output,input.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                AssertBounds(input.candidate.sourceBounds,admitted.presentation.sourceBounds);
            }
        }

        TEST_METHOD(CurrentConfirmedTranslationKeepsItsDimensionsAndDisplacement)
        {
            auto input=UnknownIncident();
            input.crop.verticalTranslationActive=true;
            input.crop.verticalTranslationPixels=112;
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.applyCrop && input.candidate.verticallyTranslated);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
            input.confirmedPresentationResolved=true;
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
            Assert::AreEqual(112,output.presentation.verticalTranslationPixels);
        }

        TEST_METHOD(LegacyTranslationFlagAloneCannotAuthorizeAnUnknownNearBlackEnvelope)
        {
            // Defensive pure-policy contract: the renderer normally suppresses
            // this flag during retained near-black episodes. The flag itself
            // must nevertheless never count as an actually accepted owner.
            for(int fault:{0,1,2,3,4})
            {
                auto input=UnknownIncident(9425,true);
                input.crop.currentVisibleBounds=VisibleBounds(0,1992);
                input.crop.verticalTranslationActive=true;
                input.crop.verticalTranslationPixels=112;
                if(fault!=0)
                {
                    input.candidate=Evaluate(input.crop);
                    Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
                    if(fault==1)input.candidate.sourceBounds=VisibleBounds(0,1992);
                    if(fault==2)++input.crop.verticalTranslationSourceGeneration;
                    if(fault==3)input.crop.verticalTranslationBase.top-=2;
                    if(fault==4)++input.candidate.sourceBounds.top;
                }
                Assert::IsFalse(input.confirmedPresentationResolved);
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop);
                AssertBounds(IncidentScope(),output.sourceBounds);
                Assert::IsFalse(output.outwardExpanded || output.verticallyTranslated);
            }
        }

        TEST_METHOD(ActuallyAcceptedTranslationSurvivesNearBlackMeasurementGaps)
        {
            for(bool missingMeasurement:{false,true})
            {
                auto input=UnknownIncident(9455,true);
                input.crop.verticalTranslationActive=true;
                input.crop.verticalTranslationPixels=112;
                input.candidate=Evaluate(input.crop);
                Assert::IsTrue(input.candidate.applyCrop && input.candidate.verticallyTranslated);
                Assert::IsTrue(input.candidate.owner==DecisionOwner::VERTICAL_TRANSLATION);
                // No new dense confirmation: the actual same-base translation
                // already owns this source presentation through its hold.
                Assert::IsFalse(input.confirmedPresentationResolved);
                if(missingMeasurement)
                {
                    input.measurementCurrent=input.retentionEvaluated=false;
                    input.observationAvailable=false;
                    input.crop.latestObservationIsUnavailable=true;
                    input.crop.latestObservationClassification=ActivePictureClassification::UNAVAILABLE;
                }
                const auto output=ResolveIncident(input);
                AssertBounds(input.candidate.sourceBounds,output.sourceBounds);
                Assert::IsTrue(output.owner==DecisionOwner::VERTICAL_TRANSLATION);
                Assert::IsTrue(output.applyCrop && output.verticallyTranslated);
                Assert::AreEqual(112,output.verticalTranslationPixels);
            }
        }
        TEST_METHOD(RealFullRasterAuthorityAndPublishedNativeCropStillApplyImmediately)
        {
            for(bool committed:{false,true})
            {
                auto input=UnknownIncident();
                if(committed)
                {
                    input.crop.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
                    input.crop.geometry=VisibleBounds(0,2160);
                }
                else input.crop.fullRasterPresentationAuthoritative=true;
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(VisibleBounds(0,2160),output.presentation.sourceBounds);
                Assert::IsFalse(output.presentation.applyCrop);
            }
            auto input=UnknownIncident();
            const ActivePictureBounds expanded{0,96,3840,2064,3840,2160,3840.0/1968.0,
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            MakeNativeCurrent(input,expanded);
            input.candidate=Evaluate(input.crop);
            Assert::IsTrue(input.candidate.owner==DecisionOwner::TRUSTED_CROP);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsFalse(output.retained);
            AssertBounds(expanded,output.presentation.sourceBounds);
            const auto admitted=AdmitCropPresentation(input.previousAdmission,input.crop,
                output.presentation,input.presentationEpoch);
            Assert::IsFalse(admitted.blocked);
            AssertBounds(expanded,admitted.presentation.sourceBounds);
        }

        TEST_METHOD(CurrentPictureHandoffAndMovingHoldAreNotConvertedToFallbackOwners)
        {
            for(bool moving:{false,true})
            {
                auto input=UnknownIncident();
                auto& crop=input.crop;
                if(moving)
                {
                    crop.movingPictureTransition=true;
                    crop.movingPictureHold.base=crop.geometry;
                    crop.movingPictureHold.sourceGeneration=crop.frameSourceGeneration;
                    crop.movingPictureHold.sourceSequence=crop.frameSourceSequence;
                    crop.movingPictureHold.presentationEpoch=input.presentationEpoch;
                }
                else
                {
                    crop.latestObservationIsProvisional=false;
                    crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                    crop.pictureTransitionHandoff.active=true;
                    crop.pictureTransitionHandoff.trustedBase=crop.geometry;
                    crop.pictureTransitionHandoff.sourceGeneration=crop.frameSourceGeneration;
                    crop.pictureTransitionHandoff.sourceSequence=crop.frameSourceSequence;
                    crop.pictureTransitionHandoff.presentationEpoch=input.presentationEpoch;
                }
                input.candidate=Evaluate(crop);
                Assert::IsTrue(input.candidate.applyCrop);
                Assert::IsTrue(input.candidate.owner==(moving?DecisionOwner::MOVING_PICTURE_HOLD:
                    DecisionOwner::PICTURE_CONFIRMATION));
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
                Assert::IsTrue(output.presentation.owner==input.candidate.owner);
            }
        }

        TEST_METHOD(IndependentOneScanStepSamplingGuardStillPreservesMeasuredBoundaryRows)
        {
            auto input=UnknownIncident();
            input.crop.currentVisibleBoundsAvailable=false;
            input.excludedBandsPixelSafe=true;
            input.observation=VisibleBounds(274,1884);
            const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
            Assert::IsTrue(output.retained && output.currentPixelExtent && output.expanded);
            AssertBounds(VisibleBounds(274,1884),output.presentation.sourceBounds);
            AssertBounds(IncidentScope(),output.nativeCrop);
        }

        TEST_METHOD(NearBlackPipelinePreservesOnlyExactCurrentIndependentTinySamplingGuard)
        {
            for(int fault:{0,1,2,3,4,5,6})
            {
                auto input=UnknownIncident(9426,true);
                input.crop.currentVisibleBoundsAvailable=false;
                input.excludedBandsPixelSafe=true;
                input.observation=VisibleBounds(274,1884);
                // This exception is independent exact-source strip proof, not
                // a generic visible extent or a confirmed caption/FIT owner.
                Assert::IsFalse(input.confirmedPresentationResolved);
                if(fault==1)input.measurementCurrent=false;
                if(fault==2)--input.retentionSourceSequence;
                if(fault==3)++input.retentionSourceGeneration;
                if(fault==4)input.retentionBounds.top-=2;
                if(fault==5)input.excludedBandsPixelSafe=false;
                if(fault==6)input.observation.top=270;
                const auto output=ResolveIncident(input);
                Assert::IsTrue(output.applyCrop);
                AssertBounds(fault==0?VisibleBounds(274,1884):IncidentScope(),output.sourceBounds);
                if(fault!=0)Assert::IsFalse(output.outwardExpanded,
                    L"A stale, mismatched, unsafe or larger proposal cannot borrow the independent one-step sampling exception.");
            }
        }
        TEST_METHOD(NewCropAcquisitionStillRejectsUnownedCurrentVisiblePixels)
        {
            for(bool confirmedNative:{false,true})
            {
                auto input=UnknownIncident();
                input.previousAdmission={};input.submittedScopeAnchor={};
                input.crop.latestObservationSupportsCrop=confirmedNative;
                input.candidate={};input.candidate.applyCrop=true;
                input.candidate.sourceBounds=IncidentScope();
                input.candidate.owner=confirmedNative?DecisionOwner::TRUSTED_CROP:
                    DecisionOwner::NEAR_BLACK_EPISODE;
                const auto retained=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(retained.retained);
                const auto admitted=AdmitCropPresentation({},input.crop,
                    retained.presentation,input.presentationEpoch);
                Assert::IsTrue(admitted.blocked,
                    L"The continuity fix applies only to an actually submitted native crop; visible pixels still veto new acquisition.");
                Assert::IsFalse(admitted.presentation.applyCrop);
                AssertBounds(VisibleBounds(0,2160),admitted.presentation.sourceBounds);
            }
        }

        TEST_METHOD(SourceViewportAndOrderingChangesCannotBorrowASubmittedScopeOrEnvelope)
        {
            for(int changed:{0,1,2})
            {
                auto input=UnknownIncident();
                input.submittedScopeAnchor.submittedEnvelopeAvailable=true;
                input.submittedScopeAnchor.submittedEnvelope=VisibleBounds(278,1992);
                input.submittedScopeAnchor.submittedEnvelopeSourceSequence=input.crop.frameSourceSequence-1;
                if(changed==0)++input.crop.frameSourceGeneration;
                if(changed==1)++input.presentationEpoch;
                if(changed==2)input.submittedScopeAnchor.lastSubmittedPresentationSourceSequence=
                    input.crop.frameSourceSequence+1;
                const auto output=ResolveUncertainPicturePresentation(input,input.candidate);
                Assert::IsFalse(output.retained);
                AssertBounds(input.candidate.sourceBounds,output.presentation.sourceBounds);
            }
        }
    };
}