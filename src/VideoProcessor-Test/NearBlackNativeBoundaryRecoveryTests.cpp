#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/AlphaSourceCropPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace VideoProcessorTest
{
namespace
{
ActivePictureBounds OldFoxCrop()
{
    return {0,280,3840,1872,3840,2160,3840.0/1592.0,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
ActivePictureBounds CurrentFoxCrop()
{
    return {0,280,3840,1880,3840,2160,2.4,
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
void SetFrame(NearBlackPresentationEpisodeInput& input,uint64_t sequence)
{
    input.sourceSequence=input.retentionSourceSequence=input.reacquiredSourceSequence=
        input.nativeBootstrapSourceSequence=sequence;
    input.currentTick=sequence*42;
}
NearBlackPresentationEpisodeInput FoxEpisode(bool startedFull=false)
{
    NearBlackPresentationEpisodeInput input;
    input.measurementCurrent=input.nearBlackEvaluated=input.globalNearBlack=true;
    input.trustedCropAvailable=true;input.trustedCrop=OldFoxCrop();
    input.sourceGeneration=input.retentionSourceGeneration=input.reacquiredSourceGeneration=
        input.nativeBootstrapSourceGeneration=3;
    input.presentationEpoch=input.reacquiredPresentationEpoch=input.nativeBootstrapPresentationEpoch=1;
    input.framesPerSecond=23.976;
    input.retentionEvaluated=true;input.retentionBounds=OldFoxCrop();
    input.retentionSafe=input.retentionExcludedBandsPixelSafe=!startedFull;
    input.boundedVisibleContentOutsideCrop=startedFull;
    SetFrame(input,100);
    auto episode=EvaluateNearBlackPresentationEpisode(input);
    input.previous=episode.state;
    if(!startedFull)
    {
        Assert::AreEqual(int(NearBlackPresentationMode::RETAIN_CROP),int(episode.state.mode));
        SetFrame(input,101);
        input.boundedVisibleContentOutsideCrop=true;
        input.retentionSafe=input.retentionExcludedBandsPixelSafe=false;
        episode=EvaluateNearBlackPresentationEpisode(input);
        input.previous=episode.state;
    }
    Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),int(episode.state.mode));
    Assert::AreEqual(startedFull,episode.state.startedAtFullRaster);
    input.globalNearBlack=false;
    input.knownTrustedGeometryReacquired=input.reacquisitionIsCurrentAssociation=true;
    input.reacquiredTrustedClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
    input.reacquiredTrustedGeometry=OldFoxCrop();
    input.currentObservationAvailable=true;
    input.currentObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
    input.currentObservation=CurrentFoxCrop();
    // The old eight-row exclusion really is unsafe. Only the new candidate's
    // own black bands are certified; the fix must not relabel old pixels safe.
    input.nativeBootstrapContractAvailable=true;input.nativeBootstrapContract=CurrentFoxCrop();
    input.nativeBootstrapRetentionEvaluated=input.nativeBootstrapRetentionSafe=true;
    return input;
}
NearBlackPresentationEpisodeDecision Feed(NearBlackPresentationEpisodeInput& input,uint64_t sequence)
{
    SetFrame(input,sequence);
    const auto result=EvaluateNearBlackPresentationEpisode(input);
    input.previous=result.state;
    return result;
}
}
TEST_CLASS(NearBlackNativeBoundaryRecoveryTests)
{
public:
    TEST_METHOD(FoxEightVisibleRowsRecoverOnlyThroughFreshOwnBoundaryProof)
    {
        auto input=FoxEpisode();
        for(uint64_t sequence=102;sequence<=111;++sequence)
        {
            const auto result=Feed(input,sequence);
            Assert::AreEqual(10u,result.bootstrapSamplesRequired);
            Assert::IsFalse(result.releasedToTrustedCrop);
            Assert::AreEqual(0u,result.revalidationSamples);
            Assert::AreEqual(sequence==111,result.bootstrapReleased);
            Assert::AreEqual(sequence==111,result.resetTrustedGeometry);
            if(sequence<111)
                Assert::AreEqual(int(NearBlackPresentationMode::FULL_RASTER),int(result.state.mode));
            else
            {
                Assert::IsTrue(result.resetTransitionEvidence && result.ended);
                Assert::AreEqual(int(NearBlackPresentationMode::INACTIVE),int(result.state.mode));
            }
            Assert::IsFalse(input.retentionSafe || input.retentionExcludedBandsPixelSafe);
        }
    }
    TEST_METHOD(FoxSimultaneousFullEntryAlsoWithdrawsUndersizedTrustedGeometry)
    {
        auto input=FoxEpisode(true);
        NearBlackPresentationEpisodeDecision result;
        for(uint64_t sequence=102;sequence<=111;++sequence)result=Feed(input,sequence);
        Assert::IsTrue(result.bootstrapReleased && result.resetTrustedGeometry);
        Assert::IsFalse(result.releasedToTrustedCrop);
    }
    TEST_METHOD(SamplingExpansionRequiresStrictOutwardAndBoundsTotalSize)
    {
        const auto entry=OldFoxCrop();
        Assert::IsTrue(IsNearBlackNativeSamplingExpansion(entry,CurrentFoxCrop()));
        auto candidate=entry;candidate.top-=4;candidate.bottom+=4;
        Assert::IsTrue(IsNearBlackNativeSamplingExpansion(entry,candidate));
        for(int failure=0;failure<10;++failure)
        {
            auto invalid=CurrentFoxCrop();
            auto old=entry;
            switch(failure)
            {
            case 0: invalid=entry;break;
            case 1: invalid.top+=2;break;
            case 2: invalid.bottom=entry.bottom-2;break;
            case 3: invalid.bottom=entry.bottom+10;break;
            case 4: invalid.top-=8;break; //8+8 exceeds total allowance
            case 5: invalid.rasterWidth=1920;break;
            case 6: invalid.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH;break;
            case 7: old.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;break;
            case 8: invalid.left=-2;break;
            case 9: old.top=2;old.bottom=2158;invalid.top=0;invalid.bottom=2160;break;
            }
            Assert::IsFalse(IsNearBlackNativeSamplingExpansion(old,invalid));
        }
    }
    TEST_METHOD(FoxOwnBoundaryRecoveryKeepsCurrentSafetyAndContextVetoes)
    {
        for(int failure=0;failure<10;++failure)
        {
            auto input=FoxEpisode();
            for(uint64_t sequence=102;sequence<=120;++sequence)
            {
                SetFrame(input,sequence);
                auto invalid=input;
                switch(failure)
                {
                case 0: invalid.measurementCurrent=false;break;
                case 1: invalid.nativeBootstrapContractAvailable=false;break;
                case 2: invalid.nativeBootstrapRetentionEvaluated=false;break;
                case 3: invalid.nativeBootstrapRetentionSafe=false;break;
                case 4: invalid.nativeBootstrapOutwardVisible=true;break;
                case 5: invalid.globalNearBlack=true;break;
                case 6: invalid.nearBlackEvaluated=false;break;
                case 7: --invalid.nativeBootstrapSourceSequence;break;
                case 8: ++invalid.nativeBootstrapSourceGeneration;break;
                case 9: ++invalid.nativeBootstrapPresentationEpoch;break;
                }
                const auto result=EvaluateNearBlackPresentationEpisode(invalid);
                Assert::IsFalse(result.bootstrapReleased || result.resetTrustedGeometry || result.releasedToTrustedCrop);
                Assert::AreEqual(0u,result.bootstrapSamples);
                input.previous=result.state;
            }
        }
    }
    TEST_METHOD(FoxRecoveryCannotUseLargerInwardOrMissingCropHypotheses)
    {
        for(int failure=0;failure<4;++failure)
        {
            auto input=FoxEpisode();
            switch(failure)
            {
            case 0: input.nativeBootstrapContract.top=276;input.nativeBootstrapContract.bottom=1880;break;
            case 1: input.nativeBootstrapContract.bottom=1870;break;
            case 2: input.nativeBootstrapContract.top=282;break;
            case 3: input.nativeBootstrapContract.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;break;
            }
            for(uint64_t sequence=102;sequence<=120;++sequence)
            {
                const auto result=Feed(input,sequence);
                Assert::IsFalse(result.bootstrapReleased || result.resetTrustedGeometry);
                Assert::AreEqual(0u,result.bootstrapSamples);
            }
        }
    }
    TEST_METHOD(FoxRecoveryRestartsWhenExactCandidateChangesOrEligibilityCeases)
    {
        auto input=FoxEpisode();
        for(uint64_t sequence=102;sequence<=105;++sequence)Feed(input,sequence);
        Assert::AreEqual(4u,input.previous.bootstrapSamples);
        input.nativeBootstrapContract.bottom=1878;
        auto result=Feed(input,106);
        Assert::AreEqual(1u,result.bootstrapSamples);
        input.nativeBootstrapContract.bottom=1880;
        result=Feed(input,107);
        Assert::AreEqual(1u,result.bootstrapSamples);
        input.nativeBootstrapContract=OldFoxCrop(); // no longer an outward correction
        result=Feed(input,108);
        Assert::AreEqual(0u,result.bootstrapSamples);
        Assert::IsFalse(result.state.bootstrapCandidateAvailable);
        input.nativeBootstrapContract=CurrentFoxCrop();
        for(uint64_t sequence=109;sequence<=118;++sequence)
        {
            result=Feed(input,sequence);
            Assert::AreEqual(sequence==118,result.resetTrustedGeometry);
        }
    }
    TEST_METHOD(FoxPausedRecoveryRequiresFreshSeedContinuousExactFrameAndSafeBands)
    {
        auto input=FoxEpisode();
        SetFrame(input,102);input.cadenceRepeat=true;
        auto result=EvaluateNearBlackPresentationEpisode(input);
        Assert::AreEqual(0u,result.bootstrapSamples);
        Assert::IsFalse(result.resetTrustedGeometry);
        input.previous=result.state;input.cadenceRepeat=false;
        result=EvaluateNearBlackPresentationEpisode(input);
        input.previous=result.state;
        Assert::AreEqual(1u,result.bootstrapSamples);
        input.cadenceRepeat=true;
        const auto start=input.currentTick;
        for(unsigned repeat=1;repeat<=10;++repeat)
        {
            input.currentTick=start+50*repeat;
            result=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(repeat==10,result.resetTrustedGeometry);
            Assert::IsFalse(result.releasedToTrustedCrop);
            input.previous=result.state;
        }
    }
    TEST_METHOD(FoxPausedRecoveryDropsProofAfterUnsafeSampleOrTimingGap)
    {
        for(bool timingGap : {false,true})
        {
            auto input=FoxEpisode();Feed(input,102);
            Assert::AreEqual(1u,input.previous.bootstrapSamples);
            input.cadenceRepeat=true;
            input.currentTick+=timingGap?101:50;
            input.nativeBootstrapRetentionSafe=timingGap;
            auto result=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(0u,result.bootstrapSamples);
            Assert::IsFalse(result.state.bootstrapCandidateAvailable);
            input.previous=result.state;input.nativeBootstrapRetentionSafe=true;
            for(unsigned repeat=0;repeat<15;++repeat)
            {
                input.currentTick+=50;
                result=EvaluateNearBlackPresentationEpisode(input);
                Assert::IsFalse(result.resetTrustedGeometry || result.bootstrapReleased);
                input.previous=result.state;
            }
        }
    }
    TEST_METHOD(InferredOriginsCannotWithdrawTrustedGeometryThroughNativeCorrection)
    {
        for(const auto origin : {ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT,
            ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT,
            ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN})
        for(bool inferredEntry : {false,true})
        for(bool startedFull : {false,true})
        {
            auto input=FoxEpisode(startedFull);
            if(inferredEntry)
            {
                input.previous.entryTrustedCropOrigin=origin;
                input.trustedCropOrigin=origin;
            }
            else input.nativeBootstrapOrigin=origin;
            NearBlackPresentationEpisodeDecision result;
            for(uint64_t sequence=102;sequence<=111;++sequence)
            {
                result=Feed(input,sequence);
                Assert::IsFalse(result.resetTrustedGeometry,
                    L"Neither inferred entry nor inferred candidate can authorize native withdrawal.");
            }
            Assert::AreEqual(startedFull,result.bootstrapReleased,
                L"The existing full-entry bootstrap remains separate from native correction.");
        }
    }
    TEST_METHOD(NativeCorrectionCannotBorrowNineInferredBootstrapSamples)
    {
        for(const auto origin : {ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT,
            ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT,
            ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN})
        {
            auto input=FoxEpisode(true);
            input.nativeBootstrapOrigin=origin;
            for(uint64_t sequence=102;sequence<=110;++sequence)
                Assert::IsFalse(Feed(input,sequence).bootstrapReleased);
            Assert::AreEqual(9u,input.previous.bootstrapSamples);
            input.nativeBootstrapOrigin=ActivePictureAuthorityOrigin::NATIVE;
            auto result=Feed(input,111);
            Assert::AreEqual(1u,result.bootstrapSamples,
                L"Identical geometry with changed origin must restart proof.");
            Assert::IsFalse(result.bootstrapReleased || result.resetTrustedGeometry);
            for(uint64_t sequence=112;sequence<=120;++sequence)
            {
                result=Feed(input,sequence);
                Assert::AreEqual(sequence==120,result.resetTrustedGeometry);
            }
        }
    }
    TEST_METHOD(PausedOriginChangeCannotCompleteOrReseedNativeBoundaryProof)
    {
        auto input=FoxEpisode(true);
        input.nativeBootstrapOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
        Feed(input,102);
        input.cadenceRepeat=true;
        for(unsigned repeat=0;repeat<9;++repeat)
        {
            input.currentTick+=50;
            const auto result=EvaluateNearBlackPresentationEpisode(input);
            Assert::IsFalse(result.bootstrapReleased);
            input.previous=result.state;
        }
        input.nativeBootstrapOrigin=ActivePictureAuthorityOrigin::NATIVE;
        for(unsigned repeat=0;repeat<12;++repeat)
        {
            input.currentTick+=50;
            const auto result=EvaluateNearBlackPresentationEpisode(input);
            Assert::AreEqual(0u,result.bootstrapSamples);
            Assert::IsFalse(result.bootstrapReleased || result.resetTrustedGeometry);
            input.previous=result.state;
        }
    }
    TEST_METHOD(FoxBoundaryCorrectionDoesNotChangeExistingStartupBootstrap)
    {
        auto input=FoxEpisode();input.previous.entryTrustedCropAvailable=false;
        input.previous.entryTrustedCrop={};input.previous.startedAtFullRaster=true;
        input.trustedCropAvailable=false;input.trustedCrop={};
        NearBlackPresentationEpisodeDecision result;
        for(uint64_t sequence=102;sequence<=111;++sequence)result=Feed(input,sequence);
        Assert::IsTrue(result.bootstrapReleased && result.resetTransitionEvidence);
        Assert::IsFalse(result.resetTrustedGeometry || result.releasedToTrustedCrop);
    }
};
}
