#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/TemporalPresentationLedger.h>
#include <vprenderer/TemporalPresentationPose.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <vprenderer/TemporalRenderer.h>
#include <vprenderer/SubmittedOutputGeometryTrace.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
TEST_CLASS(TemporalPresentationLedgerTests)
{
public:
    TEST_METHOD(DelayedSubtitleOutputKeepsItsOwnCompositionCropAcrossLaterFailure)
    {
        struct Saved { ActivePictureBounds crop; uint64_t cue=0; bool replacement=false,noCaptionConfirmed=false,classicPoseEligible=false; };
        TemporalPresentationLedger<Saved> ledger;
        AlphaSourceCrop::Decision shared;
        shared.applyCrop=true;shared.verticallyTranslated=true;
        shared.verticalTranslationPixels=180;
        shared.sourceBounds={0,456,3840,2064,3840,2160};
        const auto remember=[&](uint64_t sequence,bool prepared,bool noCaptionConfirmed,uint64_t cue) {
            return ledger.Ensure({2,sequence,4,9},[&](Saved& saved) {
                saved.crop=SubtitlePresentationBounds(shared,
                    SubtitlePreviewMode::GeneratedGrayBackground,prepared,noCaptionConfirmed);
                saved.cue=cue;saved.replacement=prepared;
                saved.noCaptionConfirmed=noCaptionConfirmed;
                saved.classicPoseEligible=SubtitlePresentationKeepsClassicTranslation(shared,saved.crop);
                return true;
            });
        };
        Assert::IsTrue(remember(70,true,false,100));
        Assert::IsTrue(remember(71,false,false,0));
        Assert::IsTrue(remember(72,false,true,0));
        // A delayed/interlaced output selecting 70 must not use 71's failure
        // or crop. Repeated display outputs select the same immutable packet.
        for(int repeat=0;repeat<3;++repeat) {
            const auto* selected=ledger.Find({2,70,4,9});
            Assert::IsNotNull(selected);
            Assert::AreEqual(1884,selected->crop.bottom);
            Assert::AreEqual(uint64_t(100),selected->cue);
            Assert::IsTrue(selected->replacement);
            Assert::IsFalse(selected->classicPoseEligible,
                L"A selected advanced replacement must not replay Classic motion pose");
        }
        const auto* fallback=ledger.Find({2,71,4,9});
        Assert::IsNotNull(fallback);
        Assert::IsFalse(fallback->replacement);
        Assert::IsFalse(fallback->classicPoseEligible,
            L"Advanced mode must not inherit Classic picture translation on a missing replacement");
        Assert::AreEqual(1884,fallback->crop.bottom,
            L"Selected-frame picture pose stays physical across cue availability changes.");
        const auto* confirmedBlank=ledger.Find({2,72,4,9});
        Assert::IsNotNull(confirmedBlank);
        Assert::IsFalse(confirmedBlank->replacement);
        Assert::IsTrue(confirmedBlank->noCaptionConfirmed);
        Assert::IsFalse(confirmedBlank->classicPoseEligible,
            L"A selected blank cue must not replay Classic motion pose");
        Assert::AreEqual(1884,confirmedBlank->crop.bottom,
            L"A completed blank scan restores the ordinary physical picture pose.");
        Assert::IsNull(ledger.Find({2,70,5,9}));
    }
    TEST_METHOD(ZeroOutputSourceIsMappedBeforeLaterOutputSelectsIt)
    {
        TemporalPresentationLedger<int> ledger;
        int calls = 0;
        auto map = [&](uint64_t sequence, int cropTop) {
            const TemporalPresentationKey key{2, sequence, 4, 9};
            Assert::IsTrue(ledger.Ensure(key, [&](int& saved) {
                ++calls;
                saved = cropTop;
                return true;
            }));
        };
        map(70, 276);
        map(71, 270);
        map(72, 280);
        const TemporalPresentationKey selected{2, 71, 4, 9};
        Assert::IsNotNull(ledger.Find(selected));
        Assert::AreEqual(270, *ledger.Find(selected));
        Assert::AreEqual(3, calls);
        map(72, 999);
        Assert::AreEqual(3, calls);
        Assert::AreEqual(280, *ledger.Find({2, 72, 4, 9}));
    }

    TEST_METHOD(SourceEvidenceAdvancesOnceAcrossPresentationContracts)
    {
        TemporalSourceEvidenceLedger<int> evidence;
        TemporalPresentationLedger<int> mapping;
        const TemporalSourceEvidenceKey source{2, 81};
        int advances = 0;
        int maps = 0;
        auto remap = [&](uint64_t epoch, uint64_t contract) {
            Assert::IsTrue(evidence.Ensure(source, [&](int& value) {
                value = ++advances;
                return true;
            }));
            Assert::IsTrue(mapping.Ensure({2, 81, epoch, contract},
                [&](int& crop) {
                    ++maps;
                    crop = 270 + *evidence.Find(source);
                    return true;
                }));
        };
        remap(4, 9);
        remap(4, 10);
        remap(5, 10);
        remap(5, 10);
        Assert::AreEqual(1, advances);
        Assert::AreEqual(3, maps);
        Assert::AreEqual(271, *mapping.Find({2, 81, 5, 10}));
        Assert::IsNotNull(mapping.Find({2, 81, 4, 9}));
        Assert::AreEqual(271, *mapping.Find({2, 81, 4, 9}));
    }

    TEST_METHOD(LateCompleteSourceRevisionPreservesFailuresAndAdvancesOnlyOnce)
    {
        TemporalSourceEvidenceLedger<int,3> source;
        Assert::IsTrue(source.Ensure({2,70},[](int& p){p=270;return true;}));
        int revisions=0;
        Assert::IsTrue(source.CanReviseSucceeded({2,70}));
        Assert::IsFalse(source.ReviseSucceeded({2,70},[&](int& p){++revisions;p=999;return false;}));
        Assert::AreEqual(270,*source.Find({2,70}));
        Assert::IsTrue(source.CanReviseSucceeded({2,70}));
        Assert::IsTrue(source.ReviseSucceeded({2,70},[&](int& p){++revisions;p=276;return true;}));
        Assert::AreEqual(276,*source.Find({2,70}));
        Assert::IsFalse(source.CanReviseSucceeded({2,70}));
        Assert::IsFalse(source.ReviseSucceeded({2,70},[&](int&){++revisions;return true;}));
        Assert::AreEqual(2,revisions);
        Assert::IsFalse(source.Ensure({2,71},[](int&){return false;}));
        Assert::IsFalse(source.CanReviseSucceeded({2,71}));
        Assert::IsFalse(source.ReviseSucceeded({2,71},[&](int&){++revisions;return true;}));
        Assert::IsTrue(source.Failed({2,71}));
        Assert::IsFalse(source.CanReviseSucceeded({2,72}));
        Assert::IsTrue(source.Ensure({2,72},[](int& p){p=280;return true;}));
        Assert::IsTrue(source.Ensure({2,73},[](int& p){p=281;return true;}));
        Assert::IsFalse(source.CanReviseSucceeded({2,72}),L"An older source cannot advance policy after a newer source");
        Assert::IsFalse(source.ReviseSucceeded({2,70},[&](int&){++revisions;return true;}));
        Assert::IsFalse(source.Ensure({2,70},[&](int&){++revisions;return true;}));
        Assert::AreEqual(2,revisions);
    }

    TEST_METHOD(FailedLateGpuRevisionCannotReacquireOldSourceMapping)
    {
        TemporalSourceEvidenceLedger<int> source;
        Assert::IsTrue(source.Ensure({2,70},[](int& p){p=270;return true;}));
        Assert::IsFalse(source.ReviseSucceeded({2,70},[](int&)->bool{throw 1;}));
        Assert::AreEqual(270,*source.Find({2,70}));
        // A GPU replacement was already committed before a later host failure.
        source.TombstoneSucceeded({2,70});
        Assert::IsTrue(source.Failed({2,70}));
        Assert::IsNull(source.Find({2,70}));
        int retries=0;
        Assert::IsFalse(source.Ensure({2,70},[&](int&){++retries;return true;}));
        Assert::IsFalse(source.ReviseSucceeded({2,70},[&](int&){++retries;return true;}));
        Assert::AreEqual(0,retries);
        Assert::IsTrue(source.Ensure({2,71},[](int& p){p=276;return true;}));
        Assert::AreEqual(276,*source.Find({2,71}));
    }

    TEST_METHOD(LateRevisionScopeRetiresEarlyReturnAndExceptionOnlyUntilCommit)
    {
        for(int failure=0;failure<3;++failure) {
            TemporalSourceEvidenceLedger<int> source;
            TemporalPresentationLedger<int> mapping;
            Assert::IsTrue(source.Ensure({2,70},[](int& p){p=270;return true;}));
            Assert::IsTrue(mapping.Ensure({2,70,4,9},[](int& p){p=270;return true;}));
            bool packet=true,receipt=true;int rollbacks=0;
            const auto attempt=[&] {
                auto retire=[&] {
                    ++rollbacks;packet=receipt=false;source.TombstoneSucceeded({2,70});
                    mapping.InvalidateSuccessfulSource(2,70);
                };
                TemporalRevisionRollback<decltype(retire)> transaction(retire);
                transaction.Arm();
                if(failure==0)return;
                if(failure==1)throw 1;
                Assert::IsTrue(source.ReviseSucceeded({2,70},[](int& p){p=276;return true;}));
                mapping.InvalidateSuccessfulSource(2,70);
                Assert::IsTrue(mapping.Ensure({2,70,4,9},[](int& p){p=276;return true;}));
                transaction.Commit();
            };
            try{attempt();}catch(int){}
            Assert::AreEqual(failure==2?0:1,rollbacks);
            Assert::AreEqual(failure==2,packet&&receipt);
            if(failure==2)Assert::AreEqual(276,*mapping.Find({2,70,4,9}));
            else {
                Assert::IsTrue(source.Failed({2,70}));
                Assert::IsNull(mapping.Find({2,70,4,9}));
                Assert::IsFalse(source.Ensure({2,70},[](int&){return true;}));
            }
        }
    }

    TEST_METHOD(LateCompleteSourceInvalidatesOnlyItsSuccessfulDerivedMappings)
    {
        TemporalPresentationLedger<int> mapping;
        Assert::IsTrue(mapping.Ensure({2,70,4,9},[](int& p){p=270;return true;}));
        Assert::IsTrue(mapping.Ensure({2,70,4,10},[](int& p){p=271;return true;}));
        Assert::IsFalse(mapping.Ensure({2,70,5,10},[](int&){return false;}));
        Assert::IsTrue(mapping.Ensure({2,71,4,10},[](int& p){p=280;return true;}));
        mapping.InvalidateSuccessfulSource(2,70);
        Assert::IsNull(mapping.Find({2,70,4,9}));
        Assert::IsNull(mapping.Find({2,70,4,10}));
        Assert::IsTrue(mapping.Failed({2,70,5,10}));
        Assert::AreEqual(280,*mapping.Find({2,71,4,10}));
        Assert::IsTrue(mapping.Ensure({2,70,4,9},[](int& p){p=276;return true;},true));
        Assert::AreEqual(276,*mapping.Find({2,70,4,9}));
        int calls=0;
        Assert::IsFalse(mapping.Ensure({2,70,5,10},[&](int&){++calls;return true;},true));
        Assert::AreEqual(0,calls);
    }

    TEST_METHOD(FailedBuilderIsTombstonedWithoutPartialPayload)
    {
        TemporalPresentationLedger<int> ledger;
        const TemporalPresentationKey key{2, 90, 4, 9};
        int effects = 0;
        Assert::IsFalse(ledger.Ensure(key, [&](int& payload) {
            ++effects;
            payload = 270;
            return false;
        }));
        Assert::IsTrue(ledger.Seen(key));
        Assert::IsTrue(ledger.Failed(key));
        Assert::IsNull(ledger.Find(key));
        Assert::IsFalse(ledger.Ensure(key, [&](int&) {
            ++effects;
            return true;
        }));
        Assert::AreEqual(1, effects);
        ledger.Clear();
        Assert::IsTrue(ledger.Ensure(key, [&](int& payload) {
            ++effects;
            payload = 276;
            return true;
        }));
        Assert::AreEqual(2, effects);
        Assert::AreEqual(276, *ledger.Find(key));
    }

    TEST_METHOD(OutputPtsAdvancesSelectedPoseWithoutRenewingSourceEvidence)
    {
        TemporalSourceEvidenceLedger<TemporalPresentationPoseInput> ledger;
        const TemporalSourceEvidenceKey key{2, 77};
        int sourceAdvances=0;
        auto build=[&](TemporalPresentationPoseInput& pose) {
            ++sourceAdvances;
            pose.drift.Resolve(182.0f,1000,500);
            pose.sourceCrop={0,276,3840,1884};
            pose.verticalInput.authoritativeTop=276;
            pose.verticalInput.authoritativeBottom=1884;
            pose.verticalInput.rasterHeight=2160;
            pose.sourceTick=1000;
            pose.driftDurationMs=500;
            pose.requestedTranslation=182.0f;
            pose.translationRequested=true;
            pose.sourcePts=1.0;
            pose.sourceDurationSeconds=1.0/24.0;
            return true;
        };
        Assert::IsTrue(ledger.Ensure(key,build));
        auto sample=[&](double outputPts) {
            Assert::IsTrue(ledger.Ensure(key,build));
            auto pose=*ledger.Find(key);
            pose.outputPts=outputPts;
            return ResolveTemporalPresentationCrop(pose);
        };
        const auto sourcePose=sample(1.0);
        const auto tenMs=sample(1.010);
        const auto thirtyMs=sample(1.030);
        const auto durationEnd=sample(1.0+1.0/24.0);
        const auto muchLater=sample(2.0);
        Assert::AreEqual(1,sourceAdvances);
        Assert::IsTrue(sourcePose.y0==276.0f);
        Assert::IsTrue(tenMs.y0>sourcePose.y0);
        Assert::IsTrue(thirtyMs.y0>tenMs.y0);
        Assert::IsTrue(durationEnd.y0>=thirtyMs.y0);
        Assert::IsTrue(muchLater.y0==durationEnd.y0,
            L"Output-time drift must stop at the selected source duration");
        Assert::IsTrue(tenMs.y1-tenMs.y0==1608.0f);
        Assert::IsTrue(ledger.Find(key)->sourceCrop.y0==276.0f,
            L"Output pose sampling mutated frozen source geometry");
    }
    TEST_METHOD(SparseLiveSequencesWithSameModuloSlotRemainAvailable)
    {
        TemporalPresentationLedger<int, 16> ledger;
        const TemporalPresentationKey first{4, 100, 5, 9};
        const TemporalPresentationKey second{4, 116, 5, 9};
        const TemporalPresentationKey third{4, 132, 5, 9};
        Assert::IsTrue(ledger.Ensure(first, [](int& crop) { crop = 270; return true; }));
        Assert::IsTrue(ledger.Ensure(second, [](int& crop) { crop = 276; return true; }));
        Assert::IsTrue(ledger.Ensure(third, [](int& crop) { crop = 280; return true; }));
        Assert::IsNotNull(ledger.Find(first));
        Assert::IsNotNull(ledger.Find(second));
        Assert::IsNotNull(ledger.Find(third));
        Assert::AreEqual(270, *ledger.Find(first));
        Assert::AreEqual(276, *ledger.Find(second));
        Assert::AreEqual(280, *ledger.Find(third));
    }
    TEST_METHOD(EvictedOldSourceAndInvalidIdentityCannotRenewEvidence)
    {
        TemporalSourceEvidenceLedger<int, 2> ledger;
        int calls = 0;
        auto advance = [&](uint64_t sequence) {
            return ledger.Ensure({4, sequence}, [&](int& value) {
                value = ++calls;
                return true;
            });
        };
        Assert::IsFalse(advance(0));
        Assert::IsTrue(advance(1));
        Assert::IsTrue(advance(2));
        Assert::IsTrue(advance(3));
        Assert::IsNull(ledger.Find({4, 1}));
        Assert::IsFalse(advance(1));
        Assert::AreEqual(3, calls);
        ledger.Clear();
        Assert::IsTrue(advance(1));
        Assert::AreEqual(4, calls);
    }
};

TEST_CLASS(TemporalPresentationGeometrySafetyTests)
{
    static TemporalRenderer::PresentationGeometry Geometry(bool cleaned)
    {
        TemporalRenderer::PresentationGeometry result;
        result.valid=true;result.viewportEpoch=7;result.owner=3;
        result.familyBase=result.sourceCrop={0,276,3840,1884};
        result.cleaned=cleaned;
        return result;
    }
public:
    TEST_METHOD(AdvancedModeAloneCannotCertifyCleanPixels)
    {
        Assert::IsFalse(TemporalRenderer::HasCleanPresentation(true,false,false),
            L"Rejected, pending, ambiguous and failed composition source packets retain raw pixels.");
        Assert::IsTrue(TemporalRenderer::HasCleanPresentation(true,true,false));
        Assert::IsTrue(TemporalRenderer::HasCleanPresentation(true,false,true));
        Assert::IsFalse(TemporalRenderer::HasCleanPresentation(false,true,true),
            L"Classic source pixels have not passed through the advanced cleanup contract.");
    }
    TEST_METHOD(CleanAndRawPacketsCannotBlendEvenWithIdenticalCrops)
    {
        const auto clean=Geometry(true),raw=Geometry(false);
        const pl_rect2df crop={0,276,3840,1884};
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(clean,raw,crop));
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(raw,clean,crop));
        auto failed=raw; failed.sourceCrop={0,350,3840,1958};
        failed.visibleSafe=failed.emptyBandsSafe=true;
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(clean,failed,crop),
            L"Safety flags cannot relabel an unrepaired source card as clean.");
    }
    TEST_METHOD(CertifiedCleanPacketsBlendOnlyWithinSamePictureFamilyAndEpoch)
    {
        const auto clean=Geometry(true);
        auto other=clean;other.sourceCrop={0,350,3840,1958};
        const pl_rect2df crop={0,276,3840,1884};
        Assert::IsTrue(TemporalRenderer::CompatiblePresentationGeometry(clean,other,crop));
        ++other.viewportEpoch;
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(clean,other,crop));
        other=clean;++other.familyBase.y0;
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(clean,other,crop));
        other=clean;++other.owner;
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(clean,other,crop));
    }
    TEST_METHOD(ClassicRawPixelsKeepExistingCropAndVisibleExtentProtection)
    {
        const auto raw=Geometry(false);
        auto other=raw;
        const pl_rect2df crop={0,276,3840,1884};
        Assert::IsTrue(TemporalRenderer::CompatiblePresentationGeometry(raw,other,crop));
        other.sourceCrop={0,350,3840,1958};
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(raw,other,crop));
        other.visibleSafe=true;other.verifiedVisible={1500,1900,2300,1950};
        Assert::IsFalse(TemporalRenderer::CompatiblePresentationGeometry(raw,other,crop));
        other.verifiedVisible={1500,1800,2300,1850};
        Assert::IsTrue(TemporalRenderer::CompatiblePresentationGeometry(raw,other,crop));
    }
    TEST_METHOD(SubmittedGeometryRecordsOneOutputJumpAndReturnWithoutThrottle)
    {
        SubmittedOutputGeometryTrace trace;
        const SubmittedOutputGeometryTrace::Rect base={0,276,3840,1884},
            target={0,0,1920,816},jump={0,350,3840,1958};
        Assert::IsTrue(trace.Observe(true,7,8,base,target));
        Assert::IsFalse(trace.Observe(true,7,8,base,target));
        Assert::IsTrue(trace.Observe(true,7,8,jump,target));
        Assert::IsTrue(trace.Observe(true,7,8,base,target),
            L"Returning one refresh later must not disappear inside a 250ms throttle.");
        Assert::IsFalse(trace.Observe(false,7,8,jump,target));
        Assert::IsFalse(trace.Observe(true,7,8,base,target),
            L"A failed render never changes the last actually submitted rectangle.");
        auto shiftedTarget=target;shiftedTarget[1]+=1;shiftedTarget[3]+=1;
        Assert::IsTrue(trace.Observe(true,7,8,base,shiftedTarget));
        Assert::IsTrue(trace.Observe(true,7,9,base,shiftedTarget));
        Assert::IsTrue(trace.Observe(true,8,9,base,shiftedTarget));
    }
    TEST_METHOD(SubmittedGeometryAccumulatesSubpixelMovementWithoutPerFrameNoise)
    {
        SubmittedOutputGeometryTrace trace;
        const SubmittedOutputGeometryTrace::Rect base={0,276,3840,1884},target={0,0,1920,816};
        Assert::IsTrue(trace.Observe(true,7,8,base,target));
        auto fractional=base;fractional[1]+=0.25f;fractional[3]+=0.25f;
        Assert::IsFalse(trace.Observe(true,7,8,fractional,target));
        fractional[1]+=0.25f;fractional[3]+=0.25f;
        Assert::IsTrue(trace.Observe(true,7,8,fractional,target));
        Assert::IsTrue(trace.Observe(true,7,8,base,target));
    }
};
}
