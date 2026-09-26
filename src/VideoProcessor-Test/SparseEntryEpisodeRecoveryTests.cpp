#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <ActivePictureTransitionModel.h>
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace VideoProcessorTest
{
namespace
{
ActivePictureBounds SparseEntry()
{
    return {0,272,3840,1888,3840,2160,3840.0/1616.0,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
ActivePictureBounds NativeRecurrence()
{
    return {0,280,3840,1880,3840,2160,2.4,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
ActivePictureObservation Observation(const ActivePictureBounds& bounds, uint64_t sequence,
    ActivePictureAuthorityOrigin origin=ActivePictureAuthorityOrigin::NATIVE)
{
    ActivePictureObservation result;
    result.bounds=bounds; result.frameNumber=sequence; result.available=true;
    result.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
    result.framesPerSecond=23.976; result.authorityOrigin=origin;
    result.axisEvidence.horizontal={ActivePictureAxisState::FULL_EXTENT_SUPPORTED,
        ActivePictureAxisReason::FULL_EXTENT_SUPPORTED,true,false};
    result.axisEvidence.vertical={ActivePictureAxisState::TRUSTED_BARS,
        ActivePictureAxisReason::BAR_CONFIRMED,true,true};
    return result;
}
void Establish(ActivePictureTransitionModel& model,
    ActivePictureAuthorityOrigin origin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT)
{
    model.SetStableGeometryDeadbandPercent(2.0);
    ActivePictureTransitionDecision decision;
    for(uint64_t sequence=1;sequence<=4;++sequence)
        decision=model.Observe(Observation(SparseEntry(),sequence,origin));
    Assert::IsTrue(decision.publish && decision.stable);
    Assert::AreEqual(int(origin),int(decision.authorityOrigin));
}
void SetSequence(NearBlackPresentationEpisodeInput& input,uint64_t sequence)
{
    input.sourceSequence=input.retentionSourceSequence=sequence;
    input.currentTick=sequence*42;
}
NearBlackPresentationEpisodeInput StartEpisode(bool outward)
{
    NearBlackPresentationEpisodeInput input;
    input.measurementCurrent=input.nearBlackEvaluated=input.globalNearBlack=true;
    input.trustedCropAvailable=true; input.trustedCrop=SparseEntry();
    input.trustedCropOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
    input.sourceGeneration=input.retentionSourceGeneration=7;
    input.presentationEpoch=19; input.framesPerSecond=23.976;
    input.retentionEvaluated=input.retentionSafe=input.retentionExcludedBandsPixelSafe=true;
    input.retentionBounds=SparseEntry();
    SetSequence(input,5);
    auto decision=EvaluateNearBlackPresentationEpisode(input);
    Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(decision.state.mode));
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),
        int(decision.state.entryTrustedCropOrigin));
    input.previous=decision.state;
    if(outward)
    {
        SetSequence(input,6);
        input.boundedVisibleContentOutsideCrop=true;
        input.retentionSafe=input.retentionExcludedBandsPixelSafe=false;
        decision=EvaluateNearBlackPresentationEpisode(input);
        Assert::IsTrue(decision.changedToFullRaster);
        Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),int(decision.state.mode));
        input.previous=decision.state;
    }
    input.globalNearBlack=input.boundedVisibleContentOutsideCrop=false;
    input.retentionSafe=input.retentionExcludedBandsPixelSafe=true;
    return input;
}
void CurrentNativeProof(ActivePictureTransitionModel& model,
    NearBlackPresentationEpisodeInput& input,uint64_t sequence)
{
    SetSequence(input,sequence);
    auto raw=Observation(NativeRecurrence(),sequence);
    raw.axisEvidence.horizontal={ActivePictureAxisState::UNKNOWN,
        ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT,true,false};
    input.currentNativeObservationReaffirmsSparseEntry=
        model.NativeObservationReaffirmsSparseEntry(raw,input.previous.entryTrustedCrop);
    input.currentObservationAvailable=raw.available;
    input.currentObservationClassification=raw.classification;
    input.currentObservation=raw.bounds;
    // Exercise the actual loop: the current native evidence is suppressed while
    // the episode owns presentation; the model consequently retains sparse origin.
    ActivePictureEvidence evidence;
    evidence.available=raw.available; evidence.classification=raw.classification;
    evidence.trustedBounds=raw.bounds; evidence.authorityOrigin=raw.authorityOrigin;
    const auto suppressed=ConstrainNearBlackCropAcquisition(evidence,true);
    auto observed=raw;
    observed.classification=suppressed.classification;
    observed.bounds=suppressed.proposedBounds;
    const auto held=model.Observe(observed);
    Assert::IsTrue(held.stable);
    Assert::IsFalse(held.publish);
    Assert::AreEqual(int(ActivePictureClassification::PROVISIONAL),int(observed.classification));
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::NATIVE),int(held.authorityOrigin));
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(held.stableAuthorityOrigin));
    input.knownTrustedGeometryReacquired=input.reacquisitionIsCurrentAssociation=false;
}
void AssertSparseHistoryUnchanged(ActivePictureTransitionModel& model,uint64_t sequence)
{
    // A normal native recurrence differs by eight pixels on each edge. Existing
    // deadband deliberately retains the sparse rectangle and its honest provenance.
    const auto held=model.Observe(Observation(NativeRecurrence(),sequence));
    Assert::IsFalse(held.publish);
    Assert::AreEqual(int(ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT),int(held.authorityOrigin));
    Assert::AreEqual(272,held.stableBounds.top);
    Assert::AreEqual(1888,held.stableBounds.bottom);
    ActivePictureBounds remembered;
    Assert::IsFalse(model.FindRecentTrustedBarGeometry(SparseEntry(),remembered));
    Assert::IsFalse(model.FindRecentTrustedBarGeometry(NativeRecurrence(),remembered));
}
}

TEST_CLASS(SparseEntryEpisodeRecoveryTests)
{
public:
    TEST_METHOD(LoggedAlienSparseCropRecoversAfterDarkOverlayWithoutWindowReset)
    {
        ActivePictureTransitionModel model; Establish(model);
        auto input=StartEpisode(true);
        for(uint64_t sequence=7;sequence<=13;++sequence)
        {
            CurrentNativeProof(model,input,sequence);
            Assert::IsTrue(input.currentNativeObservationReaffirmsSparseEntry);
            const auto decision=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(7u,decision.revalidationSamplesRequired);
            Assert::AreEqual(sequence==13,decision.releasedToTrustedCrop);
            Assert::IsFalse(decision.bootstrapReleased || decision.resetTransitionEvidence);
            if(sequence<13)
            {
                Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),int(decision.state.mode));
                Assert::AreEqual(272,decision.state.entryTrustedCrop.top);
                Assert::AreEqual(1888,decision.state.entryTrustedCrop.bottom);
            }
            else Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE),int(decision.state.mode));
            input.previous=decision.state;
        }
        AssertSparseHistoryUnchanged(model,14);
    }

    TEST_METHOD(RetainedSparseEpisodeEndsAfterFreshNativeReaffirmation)
    {
        ActivePictureTransitionModel model; Establish(model);
        auto input=StartEpisode(false);
        for(uint64_t sequence=6;sequence<=12;++sequence)
        {
            CurrentNativeProof(model,input,sequence);
            const auto decision=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(sequence==12,decision.releasedToTrustedCrop);
            Assert::IsFalse(decision.changedToFullRaster || decision.bootstrapReleased);
            Assert::AreEqual(int(sequence==12?NearBlackPresentationMode::INACTIVE:
                NearBlackPresentationMode::RETAIN_CROP),int(decision.state.mode));
            input.previous=decision.state;
        }
        AssertSparseHistoryUnchanged(model,13);
    }

    TEST_METHOD(NativeReaffirmationDoesNotRequireBrightFullWidthSideProof)
    {
        ActivePictureTransitionModel model; Establish(model);
        auto raw=Observation(NativeRecurrence(),10);
        raw.axisEvidence.horizontal={ActivePictureAxisState::UNKNOWN,
            ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT,true,false};
        Assert::IsTrue(model.NativeObservationReaffirmsSparseEntry(raw,SparseEntry()));
        // A failed side-bar proposal is different from absent full-width proof.
        raw.axisEvidence.horizontal.barCandidate=true;
        raw.axisEvidence.horizontal.reason=ActivePictureAxisReason::BAR_EDGE_REJECTED;
        Assert::IsFalse(model.NativeObservationReaffirmsSparseEntry(raw,SparseEntry()));
    }

    TEST_METHOD(CompletedSparseEpisodeRendersSavedCropWithoutAnotherRecoveryDwell)
    {
        for(bool outward : {false,true})
        {
            ActivePictureTransitionModel model; Establish(model);
            auto input=StartEpisode(outward);
            NearBlackPresentationEpisodeDecision episode;
            for(uint64_t sequence=7;sequence<=13;++sequence)
            {
                CurrentNativeProof(model,input,sequence);
                episode=EvaluateNearBlackPresentationEpisode(input);
                input.previous=episode.state;
            }
            Assert::IsTrue(episode.releasedToTrustedCrop);
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.latestObservationSupportsCrop=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.geometry=SparseEntry();
            crop.geometrySourceGeneration=crop.frameSourceGeneration=input.sourceGeneration;
            crop.rasterWidth=3840;crop.rasterHeight=2160;crop.frameSourceSequence=4;
            const auto prior=AdmitCropPresentation({},crop,Evaluate(crop),input.presentationEpoch).state;
            Assert::IsTrue(prior.available);
            // The frame that finishes the episode was already analyzed under
            // suppression. Its independent proof must reach actual presentation.
            crop.frameSourceSequence=input.sourceSequence;
            crop.latestObservationSupportsCrop=false;
            crop.latestObservationIsProvisional=true;
            crop.latestObservationClassification=ActivePictureClassification::PROVISIONAL;
            crop.frameLocalPresentationRetentionEvaluated=input.retentionEvaluated;
            crop.frameLocalPresentationRetentionSafe=input.retentionSafe;
            crop.nearBlackEpisodeRetainCrop=episode.state.mode==NearBlackPresentationMode::RETAIN_CROP;
            crop.nearBlackEpisodeFullRaster=episode.state.mode==NearBlackPresentationMode::FULL_RASTER;
            const auto candidate=Evaluate(crop);
            Assert::AreEqual(int(DecisionOwner::PIXEL_SAFE_RETENTION),int(candidate.owner));
            for(bool recoveryActive : {false,true})
            {
                PresentationRecoveryInput recovery;
                recovery.crop=crop;recovery.candidate=candidate;recovery.previousAdmission=prior;
                recovery.presentationEpoch=input.presentationEpoch;
                recovery.previous.active=recoveryActive;
                recovery.previous.sourceGeneration=input.sourceGeneration;
                recovery.previous.presentationEpoch=input.presentationEpoch;
                recovery.previous.trustedCrop=SparseEntry();
                recovery.measurementCurrent=input.measurementCurrent;
                recovery.retentionEvaluated=input.retentionEvaluated;
                recovery.retentionBounds=input.retentionBounds;
                recovery.retentionSourceGeneration=input.retentionSourceGeneration;
                recovery.retentionSourceSequence=input.retentionSourceSequence;
                recovery.excludedBandsPixelSafe=input.retentionExcludedBandsPixelSafe;
                recovery.nearBlackEvaluated=input.nearBlackEvaluated;
                recovery.observationAvailable=input.currentObservationAvailable;
                recovery.observationClassification=input.currentObservationClassification;
                recovery.observation=recovery.observedTrustedCrop=input.currentObservation;
                recovery.confirmedPresentationResolved=episode.releasedToTrustedCrop;
                const auto handedOff=EvaluatePresentationRecovery(recovery);
                Assert::IsFalse(handedOff.state.active);
                const auto admitted=AdmitCropPresentation(prior,crop,handedOff.presentation,input.presentationEpoch);
                Assert::IsFalse(admitted.blocked);
                Assert::IsTrue(admitted.presentation.applyCrop);
                Assert::AreEqual(0,admitted.presentation.sourceBounds.left);
                Assert::AreEqual(272,admitted.presentation.sourceBounds.top);
                Assert::AreEqual(3840,admitted.presentation.sourceBounds.right);
                Assert::AreEqual(1888,admitted.presentation.sourceBounds.bottom);
            }
            // On the next native frame the existing rectangle remains stable.
            ++crop.frameSourceSequence;
            crop.latestObservationSupportsCrop=true;crop.latestObservationIsProvisional=false;
            crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            const auto next=AdmitCropPresentation(prior,crop,Evaluate(crop),input.presentationEpoch);
            Assert::IsFalse(next.blocked);
            Assert::IsTrue(next.presentation.applyCrop);
            Assert::AreEqual(272,next.presentation.sourceBounds.top);
            Assert::AreEqual(1888,next.presentation.sourceBounds.bottom);
            AssertSparseHistoryUnchanged(model,14);
        }
    }
    TEST_METHOD(NativeReaffirmationRejectsUntrustedInferredOrUnrelatedMeasurements)
    {
        ActivePictureTransitionModel model; Establish(model);
        for(int failure=0;failure<15;++failure)
        {
            auto raw=Observation(NativeRecurrence(),10);
            auto entry=SparseEntry();
            switch(failure)
            {
            case 0: raw.available=false; break;
            case 1: raw.classification=ActivePictureClassification::PROVISIONAL; break;
            case 2: raw.classification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
            case 3: raw.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT; break;
            case 4: raw.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT; break;
            case 5: raw.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN; break;
            case 6: raw.bounds.top=270; break; // even a small outward picture vetoes restoration
            case 7: raw.bounds.bottom=1890; break;
            case 8: raw.bounds.rasterWidth=1920; break;
            case 9: raw.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH; break;
            case 10: raw.bounds.top=400;raw.bounds.bottom=1760;raw.bounds.aspectRatio=3840.0/1360.0;break;
            case 11: raw.axisEvidence.vertical={ActivePictureAxisState::UNKNOWN,
                ActivePictureAxisReason::BAR_EDGE_REJECTED,true,true};break;
            case 12: raw.axisEvidence.horizontal={ActivePictureAxisState::UNKNOWN,
                ActivePictureAxisReason::BAR_ASYMMETRY,true,true};break;
            case 13: raw.transitionDeferred=true;break;
            case 14: entry.top+=2;break; // certificate is for the exact saved contract
            }
            Assert::IsFalse(model.NativeObservationReaffirmsSparseEntry(raw,entry));
        }
        AssertSparseHistoryUnchanged(model,11);
    }

    TEST_METHOD(NativeReaffirmationRequiresEstablishedSparseStartupAuthority)
    {
        ActivePictureTransitionModel empty;
        Assert::IsFalse(empty.NativeObservationReaffirmsSparseEntry(
            Observation(NativeRecurrence(),10),SparseEntry()));
        ActivePictureTransitionModel native; Establish(native,ActivePictureAuthorityOrigin::NATIVE);
        Assert::IsFalse(native.NativeObservationReaffirmsSparseEntry(
            Observation(NativeRecurrence(),10),SparseEntry()));
        ActivePictureTransitionModel sparse; Establish(sparse);
        Assert::IsTrue(sparse.NativeObservationReaffirmsSparseEntry(
            Observation(NativeRecurrence(),10),SparseEntry()));
        sparse.Reset();
        Assert::IsFalse(sparse.NativeObservationReaffirmsSparseEntry(
            Observation(NativeRecurrence(),11),SparseEntry()));
    }

    TEST_METHOD(NativeReaffirmationUsesGeometryDeadbandNotBroaderAspectTolerance)
    {
        ActivePictureTransitionModel model; Establish(model);
        auto raw=Observation(NativeRecurrence(),10);
        raw.bounds.top=296;raw.bounds.bottom=1864;raw.bounds.aspectRatio=3840.0/1568.0;
        //48 pixels of total height change exceeds the configured2% raster-height
        //geometry allowance, despite an aspect difference below5%.
        Assert::IsFalse(model.NativeObservationReaffirmsSparseEntry(raw,SparseEntry()));
        model.SetStableGeometryDeadbandPercent(0.0);
        Assert::IsFalse(model.NativeObservationReaffirmsSparseEntry(
            Observation(NativeRecurrence(),11),SparseEntry()));
    }

    TEST_METHOD(SparseEpisodeRecoveryKeepsCurrentPixelBrightnessAndVisibilityVetoes)
    {
        for(bool outward : {false,true})
        for(int failure=0;failure<16;++failure)
        {
            ActivePictureTransitionModel model; Establish(model);
            auto input=StartEpisode(outward);
            for(uint64_t sequence=7;sequence<=20;++sequence)
            {
                CurrentNativeProof(model,input,sequence);
                auto invalid=input;
                switch(failure)
                {
                case 0: invalid.measurementCurrent=false;break;
                case 1: invalid.retentionEvaluated=false;break;
                case 2: invalid.retentionSafe=invalid.retentionExcludedBandsPixelSafe=false;break;
                case 3: --invalid.retentionSourceSequence;break;
                case 4: ++invalid.retentionSourceGeneration;break;
                case 5: invalid.retentionBounds.top+=2;break;
                case 6: invalid.currentObservationAvailable=false;break;
                case 7: invalid.currentObservation.bottom=1900;break;
                case 8: invalid.globalNearBlack=true;break;
                case 9: invalid.nearBlackEvaluated=false;break;
                case 10: invalid.boundedVisibleContentOutsideCrop=true;break;
                case 11: invalid.currentNativeObservationReaffirmsSparseEntry=false;break;
                case 12: invalid.trustedCropAvailable=false;break;
                case 13: invalid.trustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;break;
                case 14: invalid.trustedCrop.top+=2;break;
                case 15: invalid.retentionExcludedBandsPixelSafe=false;break;
                }
                const auto decision=EvaluateNearBlackPresentationEpisode(invalid);
                Assert::IsFalse(decision.releasedToTrustedCrop || decision.bootstrapReleased);
                Assert::AreEqual(0u,decision.revalidationSamples);
                input.previous=decision.state;
            }
        }
    }

    TEST_METHOD(SparseRecoveryProofCannotAuthorizeNativeEntriesOrCrossContext)
    {
        for(int failure=0;failure<5;++failure)
        {
            ActivePictureTransitionModel model; Establish(model);
            auto input=StartEpisode(true);
            for(uint64_t sequence=7;sequence<=9;++sequence)
            {
                CurrentNativeProof(model,input,sequence);
                input.previous=EvaluateNearBlackPresentationEpisode(input).state;
            }
            Assert::AreEqual(3u,input.previous.revalidationSamples);
            CurrentNativeProof(model,input,10);
            switch(failure)
            {
            case 0: input.previous.entryTrustedCropOrigin=ActivePictureAuthorityOrigin::NATIVE;break;
            case 1: ++input.sourceGeneration;break;
            case 2: ++input.presentationEpoch;break;
            case 3: input.fullRasterAuthorityAvailable=true;break;
            case 4: input.sceneBoundary=true;break;
            }
            const auto decision=EvaluateNearBlackPresentationEpisode(input);
            Assert::IsFalse(decision.releasedToTrustedCrop || decision.bootstrapReleased);
            Assert::AreEqual(0u,decision.state.revalidationSamples);
        }
    }

    TEST_METHOD(SparseRecoveryCountsDistinctFramesAndRestartsAfterSequenceGap)
    {
        ActivePictureTransitionModel model; Establish(model);
        auto input=StartEpisode(true);
        for(uint64_t sequence=7;sequence<=9;++sequence)
        {
            CurrentNativeProof(model,input,sequence);
            input.previous=EvaluateNearBlackPresentationEpisode(input).state;
        }
        Assert::AreEqual(3u,input.previous.revalidationSamples);
        input.cadenceRepeat=true;
        for(int repeat=0;repeat<20;++repeat)
        {
            input.currentTick+=42;
            const auto decision=EvaluateNearBlackPresentationEpisode(input);
            Assert::IsFalse(decision.releasedToTrustedCrop || decision.bootstrapReleased);
            Assert::AreEqual(3u,decision.revalidationSamples);
            input.previous=decision.state;
        }
        input.cadenceRepeat=false;
        for(uint64_t sequence=12;sequence<=18;++sequence)
        {
            CurrentNativeProof(model,input,sequence);
            const auto decision=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(sequence==18,decision.releasedToTrustedCrop);
            if(sequence==12)Assert::AreEqual(1u,decision.revalidationSamples);
            input.previous=decision.state;
        }
    }
};
}
