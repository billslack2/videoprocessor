#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/PreparedPictureEvidence.h>
#include <vprenderer/PreparedOutwardWindow.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;
namespace {
LARGE_INTEGER qpcFrequency{};
long long Now() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
double Ms(long long ticks) { return double(ticks)*1000.0/double(qpcFrequency.QuadPart); }
unsigned long long Cycles() { ULONG64 cycles=0; QueryThreadCycleTime(GetCurrentThread(),&cycles);return cycles; }
constexpr int W=3840,H=2160,BaseTop=276,BaseBottom=1884;
constexpr uint64_t Policy=31,Continuity=37;

std::string OutputDirectory() {
 const char* name="VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR";
 const DWORD required=GetEnvironmentVariableA(name,nullptr,0);
 if(required) {
  std::vector<char> value(required);const DWORD copied=GetEnvironmentVariableA(name,value.data(),required);
  Assert::IsTrue(copied>0&&copied<required,L"measurement output directory changed while reading it");
  std::string result(value.data(),copied);if(result.back()!='\\'&&result.back()!='/')result+='\\';return result;
 }
 const std::string source=__FILE__;const auto separator=source.find_last_of("\\/");
 Assert::IsTrue(separator!=std::string::npos,L"compile with an absolute source path or set VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR");
 return source.substr(0,separator+1);
}

ActivePictureBounds Base() { return {0,BaseTop,W,BaseBottom,W,H,double(W)/(BaseBottom-BaseTop),ActivePictureBounds::BarAxes::TOP_BOTTOM}; }
struct Raw { AnalysisLumaSource source; ActivePictureFrameIdentity identity; uint64_t domainKey=0; ActivePictureEvidence evidence; ActivePictureArDarknessEvidence darkness; };
struct Frame {
 std::shared_ptr<const std::vector<uint8_t>> pixels;
 PreparedAnalysisFrame prepared;
 ActivePictureFrameIdentity identity;
 std::shared_ptr<const Raw> raw;
 std::shared_ptr<OptionalAnalysisWorker<Raw>::Slot> rawSlot=std::make_shared<OptionalAnalysisWorker<Raw>::Slot>();
 PreparedPictureEvidenceKey key;
 PreparedPictureEvidenceCache cache;
 ActivePicturePresentationRetentionEvidence reference;
 Frame(AnalysisLumaFormat format,int pattern,int index) {
  const size_t pitch=size_t(W)*(format==AnalysisLumaFormat::NativeRgb?4:2);
  const int chromaRows=format==AnalysisLumaFormat::P010?H/2:H;
  std::vector<uint8_t> bytes(pitch*(format==AnalysisLumaFormat::NativeRgb?H:H+chromaRows),0);
  for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
   const int top=pattern==2||pattern==4?BaseTop:68,bottom=pattern==2||pattern==4?BaseBottom:2092;
   const bool picture=y>=top&&y<bottom;
   int code=picture?512+index*3:64;
   if(pattern==1&&picture)code=300+((x/64)%8)*10+((y/16)%8)*6+index*3;
   if(pattern==3&&picture)code=76+(x%2)*4+((y/16)%2)*2+index;
   if(pattern==4&&x>=1360&&x<1488&&((y>=110&&y<140)||(y>=2020&&y<2050)))code=700;
   const size_t offset=size_t(y)*pitch+size_t(x)*(format==AnalysisLumaFormat::NativeRgb?4:2);
   if(format==AnalysisLumaFormat::NativeRgb) {
    const auto gray=static_cast<uint8_t>((std::max)(0,(std::min)(255,(code-64)*255/876)));
    bytes[offset]=bytes[offset+1]=bytes[offset+2]=gray;bytes[offset+3]=255;
   } else {
    const auto packed=static_cast<uint16_t>(code<<6);bytes[offset]=uint8_t(packed);bytes[offset+1]=uint8_t(packed>>8);
   }
  }
  if(format!=AnalysisLumaFormat::NativeRgb)for(int y=0;y<chromaRows;++y)for(int x=0;x<W;++x) {
   const size_t offset=pitch*H+size_t(y)*pitch+size_t(x)*2;const auto packed=uint16_t(512<<6);
   bytes[offset]=uint8_t(packed);bytes[offset+1]=uint8_t(packed>>8);
  }
  pixels=std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
  if(format==AnalysisLumaFormat::NativeRgb) {
   AnalysisFrameInput input;input.nativeData=pixels->data();input.nativeBytes=pixels->size();input.nativeRowBytes=pitch;input.nativeOwner=pixels;
   input.width=W;input.height=H;input.encoding=VideoFrameEncoding::BGRA_8BIT;input.colorspace=ColorSpace::REC_709;input.transferKey=24;input.sourceGeneration=17;
   input.profile=AnalysisSourceProfile::QueuedCapture;prepared=PrepareAnalysisFrame(input);
  } else {
   // Synthetic prepared planar contracts. Ordinary captured-source factory
   // selection is native YUV422, so do not call these recorded capture formats.
   prepared.source={pixels->data(),pixels->size(),W,H,pitch,pitch,format,VideoFrameEncoding::V210,ColorSpace::REC_709,17};
   prepared.source.range=AnalysisLumaRange::Limited;prepared.source.backingOwner=pixels;prepared.sourceOwner=pixels;
   prepared.domain=format==AnalysisLumaFormat::P010?AnalysisFrameDomain::ConvertedP010:AnalysisFrameDomain::ConvertedP210;
   prepared.profile=AnalysisSourceProfile::QueuedCapture;prepared.transferKey=24;prepared.sampleRange=VideoFrameSampleRange::LIMITED;
   prepared.colorDepth=10;prepared.bitShift=6;prepared.usesConverted=true;prepared.domainKey=0xA500+uint64_t(format);
  }
  Assert::IsTrue(prepared.IsValid());
  identity={7,uint64_t(100+index),uint64_t(1000+index),uint64_t(17500000+index*417083),17,19,23,prepared.domainKey};
  auto measured=std::make_shared<Raw>();measured->source=prepared.source;measured->identity=identity;measured->domainKey=prepared.domainKey;measured->evidence=ExtractActivePictureEvidence(prepared.source);
  measured->darkness=EvaluateActivePictureArDarkness(prepared.source,measured->evidence);raw=measured;
  key=MakePreparedPictureEvidenceKey(PreparedPictureEvidenceKind::Retention,prepared,identity,Base(),Policy,Continuity,rawSlot);
  Assert::IsTrue(ValidPreparedPictureEvidenceKey(key));
  reference=EvaluateActivePicturePresentationRetention(prepared.source,Base(),raw->evidence,raw->darkness.global,raw->darkness);
 }
};
using Frames=std::array<std::unique_ptr<Frame>,3>;
using Retentions=std::array<ActivePicturePresentationRetentionEvidence,3>;
struct Certificate { BufferedPictureExpansionProof proof; BufferedPictureExpansionDiagnostic diagnostic; };
Certificate Build(const Frames& frames,const Retentions& retention) {
 std::array<BufferedPictureExpansionSample,3> samples;
 for(size_t i=0;i<3;++i) {
  const auto& f=*frames[i];samples[i]=MakeBufferedPictureSample(f.identity,f.raw->evidence,23.976,true,f.raw->darkness.EffectiveNearBlack());
  samples[i].retention=retention[i];samples[i].nearBlackEvaluated=f.raw->darkness.global.evaluated;
 }
 Certificate c;c.proof=BuildBufferedPictureExpansion(samples.data(),samples.size(),Base(),2,2,Continuity,Policy,&c.diagnostic);return c;
}
void FingerprintIdentity(std::ostringstream& out,const ActivePictureFrameIdentity& id) {
 out<<id.transportGeneration<<','<<id.acceptedSequence<<','<<id.sourceFrameNumber<<','<<id.captureTimestamp<<','<<id.sourceFormatGeneration<<','<<id.viewportGeneration<<','<<id.rendererGeneration<<','<<id.analysisDomainKey<<';';
}
void FingerprintBounds(std::ostringstream& out,const ActivePictureBounds& b) {
 out<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<','<<b.rasterWidth<<','<<b.rasterHeight<<','<<std::setprecision(17)<<b.aspectRatio<<','<<int(b.trustedBarAxes)<<';';
}
std::string Fingerprint(const Certificate& c) {
 std::ostringstream out;const auto& p=c.proof;const auto& d=p.decision;const auto& t=d.transition;
 out<<p.valid<<','<<p.relativeContrast<<','<<p.progressiveNativeEdges<<';';
 FingerprintIdentity(out,d.observationIdentity);FingerprintIdentity(out,d.effectiveIdentity);FingerprintBounds(out,t.bounds);FingerprintBounds(out,t.stableBounds);
 out<<int(t.state)<<','<<t.publish<<','<<t.stable<<','<<t.diagnostic<<','<<t.clearTransition<<','<<int(t.authoritativeClassification)<<','<<t.knownTrustedGeometryReacquired<<','<<int(t.matchingCandidates)<<','<<int(t.contradictoryCandidates)<<','<<int(t.candidateReversals)<<','<<std::setprecision(17)<<t.confidence<<','<<t.firstContradictoryFrame<<','<<t.decisionLatencyFrames<<','<<t.reason<<','<<int(t.authorityOrigin)<<','<<int(t.stableAuthorityOrigin)<<';';
 out<<d.presentation.targetAspect<<','<<d.presentation.viewportGeneration<<','<<int(d.presentation.nlsMode)<<','<<d.presentation.horizontalStretch<<','<<d.presentation.stretchFactor<<';';
 out<<int(d.configuredLookahead)<<','<<int(d.availableLookahead)<<','<<int(d.effectiveLookahead)<<','<<int(d.proofFrameCount)<<','<<d.continuityGeneration<<','<<d.lookaheadPolicyGeneration<<','<<int(d.association)<<','<<int(d.inwardProof)<<','<<d.late;return out.str();
}
struct Deadline { long long end;uint64_t checks=0;static bool Expired(void* context) {auto& d=*static_cast<Deadline*>(context);++d.checks;return Now()>=d.end;} };
struct Run {
 double wallMs=0,lateMs=0;unsigned long long cycles=0;uint64_t startedScans=0,completedScans=0,lookups=0,hits=0,checks=0;
 bool complete=false,cancelled=false,hasDiagnostic=true,noChange=false;uint64_t rawComputed=0,rawReused=0,retentionComputed=0,retentionReused=0;Retentions retention;Certificate certificate;
};
Run Direct(const Frames& frames,double allowanceMs) {
 Run r;const auto cycleStart=Cycles();const auto start=Now();Deadline deadline{start+static_cast<long long>(double(qpcFrequency.QuadPart)*allowanceMs/1000.0)};AnalysisEvaluationBudget budget(&Deadline::Expired,&deadline);
 try {
  for(size_t i=0;i<3;++i) {
   budget.Checkpoint();++r.startedScans;const auto& f=*frames[i];
   r.retention[i]=EvaluateActivePicturePresentationRetention(f.prepared.source,Base(),f.raw->evidence,f.raw->darkness.global,f.raw->darkness,&budget);++r.completedScans;
  }
  budget.Checkpoint();r.certificate=Build(frames,r.retention);budget.Checkpoint();r.complete=true;
 } catch(const AnalysisEvaluationCancelled&) {r.cancelled=true;}
 r.wallMs=Ms(Now()-start);r.cycles=Cycles()-cycleStart;r.checks=deadline.checks;r.lateMs=(std::max)(0.0,r.wallMs-allowanceMs);
 Assert::IsFalse(r.complete&&r.cancelled);return r;
}
Run ReadyOnly(const Frames& frames) {
 Run r;const auto cycleStart=Cycles();const auto start=Now();
 for(size_t i=0;i<3;++i) {
  ++r.lookups;const auto cached=frames[i]->cache.Lookup(frames[i]->key);if(!cached)break;
  ++r.hits;r.retention[i]=cached->retention;
 }
 if(r.hits==3){r.certificate=Build(frames,r.retention);r.complete=true;}
 r.wallMs=Ms(Now()-start);r.cycles=Cycles()-cycleStart;return r;
}
struct PrecomputeResult {double observedReadyWallMs=0,serviceWallMs=0,workerSnapshotWallMs=0;unsigned long long cycles=0;uint64_t submitted=0,completed=0,scans=0;};
PrecomputeResult Precompute(Frames& frames,PreparedPictureEvidenceWorker& worker) {
 for(auto& f:frames){f->cache.Clear();std::atomic_store(&f->rawSlot->ready,f->raw);}
 std::array<double,3> serviceWall{};std::array<unsigned long long,3> serviceCycles{};
 const auto before=worker.Snapshot();const auto start=Now();
 for(size_t i=0;i<3;++i) {
  auto& f=*frames[i];const auto slot=f.cache.Request(f.key);Assert::IsNotNull(slot.get());
  const auto key=f.key;const auto raw=f.raw;
  Assert::IsTrue(worker.Submit(slot,f.identity.transportGeneration,f.identity.acceptedSequence,100,[&,i,key,raw] {
   const auto cycles=Cycles();const auto begin=Now();auto result=EvaluatePreparedPictureEvidence(key,raw->evidence,raw->darkness);
   serviceWall[i]=Ms(Now()-begin);serviceCycles[i]=Cycles()-cycles;return result;
  }));
 }
 // Waiting is ONLY in harness preparation, outside caller delivery timers.
 // The end-to-end observation includes up to1ms polling quantization.
 while(true) {
  bool ready=true;for(const auto& f:frames)ready=ready&&bool(f->cache.Lookup(f->key));
  if(ready)break;Assert::IsTrue(Ms(Now()-start)<5000,L"worker preparation timed out");Sleep(1);
 }
 // Ready publication precedes completed metrics/pending=false by a few writes.
 while(true) {bool pending=false;for(const auto& f:frames){auto slot=f->cache.Request(f->key);pending=pending||slot->pending.load();}if(!pending)break;SwitchToThread();}
 PrecomputeResult r;r.observedReadyWallMs=Ms(Now()-start);const auto after=worker.Snapshot();r.submitted=after.submitted-before.submitted;r.completed=after.completed-before.completed;r.workerSnapshotWallMs=double(after.wallUs-before.wallUs)/1000.0;r.scans=3;
 for(size_t i=0;i<3;++i){r.serviceWallMs+=serviceWall[i];r.cycles+=serviceCycles[i];}
 Assert::IsTrue(r.submitted==3&&r.completed==3);return r;
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
        void SamePicture(const ActivePictureEvidence& actual, const ActivePictureEvidence& expected)
        {
            CHECK_FIELD(available); CHECK_FIELD(classification); CHECK_FIELD(axisEvidence);
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


PreparedOutwardWindowRequest<Raw> OutwardRequest(Frames& frames) {
 PreparedOutwardWindowRequest<Raw> request;request.context.base=Base();request.context.subtitleBase=Base();
 request.context.policyGeneration=Policy;request.context.continuityGeneration=Continuity;request.context.owner=VerticalBarPresentationAction::NONE;
 for(size_t i=0;i<3;++i) {
  auto& f=*frames[i];request.frames[i]=f.prepared;request.identities[i]=request.context.contributors[i]=f.identity;
  request.rawSlots[i]=f.rawSlot;request.retentionKeys[i]=f.key;request.retentionSlots[i]=f.cache.Request(f.key);request.sourceRates[i]=23.976;
 }
 return request;
}
struct WholeRun {Run service;double observedReadyWallMs=0,snapshotWorkerWallMs=0;};
WholeRun WholeWorker(Frames& frames,OptionalAnalysisWorker<PreparedOutwardWindowResult>& worker,bool cold) {
 if(cold)for(auto& f:frames) {f->cache.Clear();std::shared_ptr<const Raw> empty;std::atomic_store(&f->rawSlot->ready,empty);}
 const auto request=OutwardRequest(frames);auto slot=std::make_shared<OptionalAnalysisWorker<PreparedOutwardWindowResult>::Slot>();
 WholeRun all;const auto before=worker.Snapshot();const auto observedStart=Now();
 Assert::IsTrue(worker.Submit(slot,7,100,100,[&all,request] {
  const auto cycleStart=Cycles();const auto start=Now();auto result=EvaluatePreparedOutwardWindow(request);
  all.service.wallMs=Ms(Now()-start);all.service.cycles=Cycles()-cycleStart;return result;
 }));
 while(!OptionalAnalysisWorker<PreparedOutwardWindowResult>::Ready(slot)) {Assert::IsTrue(Ms(Now()-observedStart)<5000);Sleep(1);}
 while(slot->pending.load())SwitchToThread();
 all.observedReadyWallMs=Ms(Now()-observedStart);const auto after=worker.Snapshot();all.snapshotWorkerWallMs=double(after.wallUs-before.wallUs)/1000.0;
 const auto result=OptionalAnalysisWorker<PreparedOutwardWindowResult>::Ready(slot);Assert::IsNotNull(result.get());
 all.service.complete=true;all.service.hasDiagnostic=false;all.service.certificate.proof=result->proof;all.service.noChange=result->noChange;
 all.service.rawComputed=result->rawComputed;all.service.rawReused=result->rawReused;all.service.retentionComputed=result->retentionComputed;all.service.retentionReused=result->retentionReused;
 for(size_t i=0;i<3;++i){const auto cached=frames[i]->cache.Lookup(frames[i]->key);Assert::IsNotNull(cached.get());all.service.retention[i]=cached->retention;}
 if(cold)Assert::IsTrue(result->rawComputed==3&&result->retentionComputed==3&&result->rawReused==0&&result->retentionReused==0);
 else Assert::IsTrue(result->rawComputed==0&&result->retentionComputed==0&&result->rawReused==3&&result->retentionReused==3);
 return all;
}

void Parity(const Frames& frames,const Run& run,const Certificate& reference) {
 if(!run.complete)return;
 for(size_t i=0;i<3;++i)SameRetention(run.retention[i],frames[i]->reference);
 Assert::IsTrue(Fingerprint(run.certificate)==Fingerprint(reference),L"same inputs must deliver the same complete certificate");
 if(run.hasDiagnostic)Assert::IsTrue(std::string(run.certificate.diagnostic.reason)==reference.diagnostic.reason&&run.certificate.diagnostic.failedSample==reference.diagnostic.failedSample);
 if(run.certificate.proof.valid) {
  Assert::IsTrue(SameAnalysisFrameIdentity(run.certificate.proof.decision.effectiveIdentity,frames[0]->identity));
  Assert::IsTrue(SameAnalysisFrameIdentity(run.certificate.proof.decision.observationIdentity,frames[2]->identity));
 }
}
double Quantile(std::vector<double> values,double q) {std::sort(values.begin(),values.end());return values[size_t(std::ceil(double(values.size()-1)*q))];}
void Summary(std::ostream& out,const std::vector<double>& values) {out<<Quantile(values,.5)<<','<<Quantile(values,.95)<<','<<Quantile(values,.99)<<','<<Quantile(values,1);}
struct SummaryRows {
 std::vector<double> wall,late,cycles;uint64_t complete=0,valid=0,cancelled=0,started=0,finished=0,lookups=0,hits=0;uint64_t first=0,last=0;
 void Add(const Run& r) {wall.push_back(r.wallMs);late.push_back(r.lateMs);cycles.push_back(double(r.cycles));complete+=r.complete;valid+=r.complete&&r.certificate.proof.valid;cancelled+=r.cancelled;started+=r.startedScans;finished+=r.completedScans;lookups+=r.lookups;hits+=r.hits;if(r.complete&&r.certificate.proof.valid){first=r.certificate.proof.decision.effectiveIdentity.acceptedSequence;last=r.certificate.proof.decision.observationIdentity.acceptedSequence;}}
};
}
namespace ProofDeliveryMeasurements {
 TEST_CLASS(MatchedPreparedProofDelivery) {
 public:
 TEST_METHOD(ThreeDistinctOwnedFramesDeliverTheSameCertificate) {
 QueryPerformanceFrequency(&qpcFrequency);
 const std::string dir=OutputDirectory();
 std::ofstream summary(dir+"proof-delivery-summary.csv"),raw(dir+"proof-delivery-raw.csv"),workerCsv(dir+"proof-precompute-summary.csv"),fixtureCsv(dir+"proof-fixtures.csv"),wholeCsv(dir+"whole-window-worker-summary.csv"),wholeRaw(dir+"whole-window-worker-raw.csv");
 Assert::IsTrue(summary.good()&&raw.good()&&workerCsv.good()&&fixtureCsv.good()&&wholeCsv.good()&&wholeRaw.good());
 wholeCsv<<"format,pattern,path,n,complete,valid_certificate,no_change,raw_computed,raw_reused,retention_computed,retention_reused,service_p50_ms,service_p95_ms,service_p99_ms,service_max_ms,snapshot_worker_p50_ms,snapshot_worker_p95_ms,snapshot_worker_p99_ms,snapshot_worker_max_ms,observed_ready_p50_ms,observed_ready_p95_ms,observed_ready_p99_ms,observed_ready_max_ms,cycles_p50,cycles_p95,cycles_p99,cycles_max,effective_first_sequence,observation_last_sequence\n";
 wholeRaw<<"format,pattern,path,trial,service_ms,snapshot_worker_ms,observed_ready_ms,thread_cycles,valid_certificate,no_change,raw_computed,raw_reused,retention_computed,retention_reused,effective_first_sequence,observation_last_sequence\n";
 wholeCsv<<std::fixed<<std::setprecision(6);wholeRaw<<std::fixed<<std::setprecision(6);
 summary<<"format,pattern,result,path,new_pixel_allowance_ms,n,complete,valid_certificate,cancelled,effective_first_sequence,observation_last_sequence,wall_p50_ms,wall_p95_ms,wall_p99_ms,wall_max_ms,late_p50_ms,late_p95_ms,late_p99_ms,late_max_ms,cycles_p50,cycles_p95,cycles_p99,cycles_max,retention_scans_started,retention_scans_completed,cache_lookups,cache_hits\n";
 raw<<"format,pattern,path,new_pixel_allowance_ms,trial,wall_ms,late_ms,thread_cycles,complete,valid_certificate,cancelled,retention_scans_started,retention_scans_completed,cache_lookups,cache_hits,checkpoints,effective_first_sequence,observation_last_sequence,diagnostic_reason\n";
 workerCsv<<"format,pattern,n,service_p50_ms,service_p95_ms,service_p99_ms,service_max_ms,snapshot_worker_p50_ms,snapshot_worker_p95_ms,snapshot_worker_p99_ms,snapshot_worker_max_ms,observed_ready_p50_ms,observed_ready_p95_ms,observed_ready_p99_ms,observed_ready_max_ms,cycles_p50,cycles_p95,cycles_p99,cycles_max,retention_scans,submitted,completed\n";
 fixtureCsv<<"format,pattern,classification,expected_valid,reason,failed_sample,first_sequence,last_sequence,unique_backings,fixture_prepare_ms_including_pixels_raw_reference,domain_key,range,source_format_generation,transport_generation,policy,continuity\n";
 summary<<std::fixed<<std::setprecision(6);raw<<std::fixed<<std::setprecision(6);workerCsv<<std::fixed<<std::setprecision(6);fixtureCsv<<std::fixed<<std::setprecision(6);
 const AnalysisLumaFormat formats[]={AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::NativeRgb};const char* names[]={"P010-synthetic","P210-synthetic","BGRA8-native"};
 const char* patterns[]={"bright-expansion","textured-expansion","scope-no-change","dark-textured","sparse-caption"};const char* paths[]={"direct-cooperative","direct-cooperative","direct-cooperative","ready-cache-only"};const double allowances[]={2,3,4,0};
 uint64_t validFixtures=0,parityComplete=0;
 for(int format=0;format<3;++format)for(int pattern=0;pattern<5;++pattern) {
  const auto fixtureStart=Now();Frames frames{std::make_unique<Frame>(formats[format],pattern,0),std::make_unique<Frame>(formats[format],pattern,1),std::make_unique<Frame>(formats[format],pattern,2)};const auto fixtureMs=Ms(Now()-fixtureStart);
  Assert::IsTrue(frames[0]->pixels.get()!=frames[1]->pixels.get()&&frames[1]->pixels.get()!=frames[2]->pixels.get()&&frames[0]->pixels.get()!=frames[2]->pixels.get());
  Retentions retention;for(size_t i=0;i<3;++i)retention[i]=frames[i]->reference;const auto reference=Build(frames,retention);
  if(pattern<2)Assert::IsTrue(reference.proof.valid,L"bright expanded frames must form a real valid broad-picture certificate");
  validFixtures+=reference.proof.valid;
  bool unchanged=true;for(const auto& f:frames)unchanged=unchanged&&f->raw->evidence.available&&
   f->raw->evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED&&SamePreparedPictureBounds(f->raw->evidence.trustedBounds,Base());
  const char* result=reference.proof.valid?"valid-expansion":unchanged?"no-change":"invalid-evidence";
  fixtureCsv<<names[format]<<','<<patterns[pattern]<<','<<result<<','<<reference.proof.valid<<','<<reference.diagnostic.reason<<','<<reference.diagnostic.failedSample<<",100,102,3,"<<fixtureMs<<','<<frames[0]->prepared.domainKey<<','<<int(frames[0]->prepared.sampleRange)<<",17,7,31,37\n";
  PreparedPictureEvidenceWorker worker;
  for(int warm=0;warm<4;++warm){Precompute(frames,worker);for(int mode=0;mode<4;++mode){const auto r=mode==3?ReadyOnly(frames):Direct(frames,allowances[mode]);Parity(frames,r,reference);}}
  std::array<SummaryRows,4> groups;std::vector<double> service,snapshotWall,observedReady,workerCycles;uint64_t workerScans=0,submitted=0,completed=0;
  for(int trial=0;trial<101;++trial) {
   const auto prepared=Precompute(frames,worker);service.push_back(prepared.serviceWallMs);snapshotWall.push_back(prepared.workerSnapshotWallMs);observedReady.push_back(prepared.observedReadyWallMs);workerCycles.push_back(double(prepared.cycles));workerScans+=prepared.scans;submitted+=prepared.submitted;completed+=prepared.completed;
   for(int order=0;order<4;++order) {
    const int mode=(trial+order)%4;const auto r=mode==3?ReadyOnly(frames):Direct(frames,allowances[mode]);Parity(frames,r,reference);parityComplete+=r.complete;
    groups[mode].Add(r);raw<<names[format]<<','<<patterns[pattern]<<','<<paths[mode]<<','<<allowances[mode]<<','<<trial<<','<<r.wallMs<<','<<r.lateMs<<','<<r.cycles<<','<<r.complete<<','<<(r.complete&&r.certificate.proof.valid)<<','<<r.cancelled<<','<<r.startedScans<<','<<r.completedScans<<','<<r.lookups<<','<<r.hits<<','<<r.checks<<','<<(r.complete?r.certificate.proof.decision.effectiveIdentity.acceptedSequence:0)<<','<<(r.complete?r.certificate.proof.decision.observationIdentity.acceptedSequence:0)<<','<<(r.complete?r.certificate.diagnostic.reason:"cancelled")<<'\n';
    if(mode==3)Assert::IsTrue(r.complete&&r.startedScans==0&&r.completedScans==0&&r.lookups==3&&r.hits==3);
   }
  }
  worker.Stop();
  OptionalAnalysisWorker<PreparedOutwardWindowResult> wholeWorker;
  std::array<std::vector<double>,2> wholeService,wholeSnapshot,wholeObserved,wholeCycles;
  uint64_t wholeValid[2]{},wholeNoChange[2]{},rawComputed[2]{},rawReused[2]{},retentionComputed[2]{},retentionReused[2]{};
  for(int warm=0;warm<3;++warm){auto c=WholeWorker(frames,wholeWorker,true);Parity(frames,c.service,reference);auto w=WholeWorker(frames,wholeWorker,false);Parity(frames,w.service,reference);}
  for(int trial=0;trial<101;++trial)for(int mode=0;mode<2;++mode) {
   const auto w=WholeWorker(frames,wholeWorker,mode==0);const auto& r=w.service;Parity(frames,r,reference);Assert::IsTrue(r.noChange==unchanged);
   wholeService[mode].push_back(r.wallMs);wholeSnapshot[mode].push_back(w.snapshotWorkerWallMs);wholeObserved[mode].push_back(w.observedReadyWallMs);wholeCycles[mode].push_back(double(r.cycles));
   wholeValid[mode]+=r.certificate.proof.valid;wholeNoChange[mode]+=r.noChange;rawComputed[mode]+=r.rawComputed;rawReused[mode]+=r.rawReused;retentionComputed[mode]+=r.retentionComputed;retentionReused[mode]+=r.retentionReused;
   wholeRaw<<names[format]<<','<<patterns[pattern]<<','<<(mode?"whole-warm-worker":"whole-cold-worker")<<','<<trial<<','<<r.wallMs<<','<<w.snapshotWorkerWallMs<<','<<w.observedReadyWallMs<<','<<r.cycles<<','<<r.certificate.proof.valid<<','<<r.noChange<<','<<r.rawComputed<<','<<r.rawReused<<','<<r.retentionComputed<<','<<r.retentionReused<<','<<r.certificate.proof.decision.effectiveIdentity.acceptedSequence<<','<<r.certificate.proof.decision.observationIdentity.acceptedSequence<<'\n';
  }
  wholeWorker.Stop();
  for(int mode=0;mode<2;++mode) {
   wholeCsv<<names[format]<<','<<patterns[pattern]<<','<<(mode?"whole-warm-worker":"whole-cold-worker")<<",101,101,"<<wholeValid[mode]<<','<<wholeNoChange[mode]<<','<<rawComputed[mode]<<','<<rawReused[mode]<<','<<retentionComputed[mode]<<','<<retentionReused[mode]<<',';Summary(wholeCsv,wholeService[mode]);wholeCsv<<',';Summary(wholeCsv,wholeSnapshot[mode]);wholeCsv<<',';Summary(wholeCsv,wholeObserved[mode]);wholeCsv<<',';Summary(wholeCsv,wholeCycles[mode]);wholeCsv<<','<<(reference.proof.valid?100:0)<<','<<(reference.proof.valid?102:0)<<'\n';
  }
  for(int mode=0;mode<4;++mode) {
   const auto& s=groups[mode];summary<<names[format]<<','<<patterns[pattern]<<','<<result<<','<<paths[mode]<<','<<allowances[mode]<<",101,"<<s.complete<<','<<s.valid<<','<<s.cancelled<<','<<s.first<<','<<s.last<<',';Summary(summary,s.wall);summary<<',';Summary(summary,s.late);summary<<',';Summary(summary,s.cycles);summary<<','<<s.started<<','<<s.finished<<','<<s.lookups<<','<<s.hits<<'\n';
  }
  workerCsv<<names[format]<<','<<patterns[pattern]<<",101,";Summary(workerCsv,service);workerCsv<<',';Summary(workerCsv,snapshotWall);workerCsv<<',';Summary(workerCsv,observedReady);workerCsv<<',';Summary(workerCsv,workerCycles);workerCsv<<','<<workerScans<<','<<submitted<<','<<completed<<'\n';
 }
 Assert::IsTrue(validFixtures>=6&&parityComplete>0);Logger::WriteMessage("Completed15 three-distinct-owned4K fixtures,101rotated trials; actual prepared component worker cost separate; all completed direct/cache certificate and retention parity passed. No renderer replay or live adoption claim.");
 }
 };
}
