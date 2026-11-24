#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectActive.h"
#include "../src/VideoProcessor-Lib/vprenderer/ActivePictureMeasurementCache.h"
#include <vector>
#include <cstdio>
using namespace ExperimentalAspect;
#include "test_experimental_aspect_sequences.h"
#include "test_experimental_aspect_oscillation.h"
int main(){
 int failures=0;auto check=[&](bool b,const char*s){std::printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++failures;};
 SetEnvironmentVariableA("VP_AR_EXPERIMENT",nullptr);
 auto pixels=std::make_shared<std::vector<uint16_t>>(1280*720*3/2,512<<6);
 for(int y=0;y<720;++y)for(int x=0;x<1280;++x)(*pixels)[y*1280+x]=(y<91||y>=629?64:300)<<6;
 AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels->data());source.dataBytes=pixels->size()*2;
 source.width=1280;source.height=720;source.rowBytes=source.chromaRowBytes=2560;source.range=AnalysisLumaRange::Limited;
 source.backingOwner=pixels;source.generation=10;
 ActivePictureFrameIdentity id;id.transportGeneration=7;id.sourceFormatGeneration=10;id.acceptedSequence=1;id.analysisDomainKey=42;id.rendererGeneration=1;
 auto measured=Measure(1280,720,64,[&](int x,int y,int&v){AnalysisLumaSample p;if(!source.Sample(x,y,p))return false;v=p.luma;return true;},[]{return true;});
 auto evidence=MeasureActiveTrial(source,id);
 check(evidence.available&&evidence.reason=="experimental joint current-pixel boundary measurement","active route emits independent native measurement");
 check(evidence.proposedBounds.top==91&&evidence.proposedBounds.bottom==629&&evidence.trustedBounds.top==90&&evidence.trustedBounds.bottom==630,"odd boundaries retain chroma pair outward");
 check(!evidence.axisEvidence.verticalCropGlyphExclusions&&!evidence.axisEvidence.verticalCropProfileClean&&!evidence.blackReference.evaluated,"no fabricated subtitle or strict matte certificates");
 check(evidence.top.lumaFloor==64&&evidence.bottom.lumaP90==64,"adapter preserves measured matte levels for downstream checks");
 check(evidence.blackReference.floor==64&&evidence.blackReference.threshold==88&&
       !evidence.blackReference.evaluated&&!evidence.blackReference.qualifiedEdgeMask&&!evidence.blackReference.qualifiedAxes,
       "limited source provides nominal retention threshold without matte certification");
 auto fullRange=source;fullRange.range=AnalysisLumaRange::Full;
 auto fullReference=EvidenceFromMeasurement(fullRange,measured);
 check(fullReference.blackReference.floor==0&&fullReference.blackReference.threshold==24&&!fullReference.blackReference.evaluated,
       "full range never borrows limited-range black64 threshold");
 auto unknownBarRange=source;unknownBarRange.range=AnalysisLumaRange::Unknown;
 check(!EvidenceFromMeasurement(unknownBarRange,measured).available,
       "unknown range cannot nominate bars with an invented nominal reference");
 check(evidence.strongCurrentBarMeasurement,"completed strongly paired measurement nominates current expansion");
 auto weakPair=measured;weakPair.edges[3].support=6.0/9;
 check(!EvidenceFromMeasurement(source,weakPair).strongCurrentBarMeasurement,"weaker opposite boundary retains ordinary admission");
 auto incompletePair=measured;incompletePair.complete=false;
 check(!EvidenceFromMeasurement(source,incompletePair).strongCurrentBarMeasurement,"incomplete boundary measurement cannot nominate immediate expansion");
 auto missingPair=measured;missingPair.acceptedPairAttempt[1]=0;
 check(!EvidenceFromMeasurement(source,missingPair).strongCurrentBarMeasurement,"unaccepted pair cannot nominate immediate expansion");
 auto darkness=EvaluateActivePictureArDarkness(source,evidence);
 auto live=source;live.generation=7;
 ActivePictureMeasurementCache cache;cache.Begin(live,id,1,1);
 check(cache.SeedQueued(source,id,evidence,darkness)&&cache.Picture().reason==evidence.reason,"exact-source worker evidence imported by real renderer cache");
 auto wrong=id;++wrong.acceptedSequence;
 check(!cache.SeedQueued(source,wrong,evidence,darkness),"different source cannot import experiment evidence");
 auto retention=cache.Retention(evidence.trustedBounds);
 check(retention.activePicture.trustedBounds.top==90,"existing retention receives new measured geometry");
 check(retention.presentationBlackFloor==64&&retention.presentationBlackThreshold==88&&
       retention.excludedBandsPixelSafe&&!retention.outwardVisibleBoundsAvailable,
       "production cache retention treats limited black bars as dark rather than full-raster visible pixels");
 ActivePictureTransitionModel model;ActivePictureTransitionDecision decision;
 for(uint64_t n=1;n<=30;++n)decision=model.Observe(MakeActivePictureObservation(evidence,n,24));
 check(decision.stable&&decision.stableBounds.top==90,"existing transition model accepts measured native geometry");
 // Adapter-contract fixture: detector qualification is tested independently on
 // pixels. Three supported edges must not invent support at the dark left edge.
 Result qualified;qualified.kind=Kind::FullRaster;qualified.complete=true;
 qualified.left=qualified.top=0;qualified.right=1280;qualified.bottom=720;
 qualified.fullExtentQualified=true;qualified.fullExtentScore=0.9;
 qualified.pictureEdgeSupported[1]=qualified.pictureEdgeSupported[2]=qualified.pictureEdgeSupported[3]=true;
 auto extent=EvidenceFromMeasurement(source,qualified);
 check(extent.available&&extent.classification==ActivePictureClassification::FULL_RASTER_TRUSTED&&
       extent.trustedBounds.left==0&&extent.trustedBounds.top==0&&extent.trustedBounds.right==1280&&extent.trustedBounds.bottom==720,
       "qualified full extent maps exact raster without a forced standard ratio");
 check(extent.axisEvidence.horizontal.state==ActivePictureAxisState::UNKNOWN&&
       extent.axisEvidence.vertical.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED&&
       !extent.axisEvidence.horizontal.barCandidate,
       "three-edge extent preserves uncertain left edge and only supported opposing axis");
 check(!extent.left.trusted&&!extent.top.trusted&&!extent.right.trusted&&!extent.bottom.trusted&&
       !extent.blackReference.evaluated&&!extent.axisEvidence.verticalCropProfileEvaluated&&
       !extent.axisEvidence.verticalCropGlyphExclusions&&extent.trustedBounds.trustedBarAxes==ActivePictureBounds::BarAxes::NONE,
       "full extent invents no matte, bar, strict profile or subtitle certificate");
 ActivePictureTransitionModel startup;
 auto unavailable=MakeActivePictureObservation(ActivePictureEvidence{},1,24);
 startup.Observe(unavailable);
 auto startFull=startup.Observe(MakeActivePictureObservation(extent,2,24));
 check(startFull.publish&&startFull.stable&&startFull.authoritativeClassification==ActivePictureClassification::FULL_RASTER_TRUSTED,
       "real transition model acquires qualified full raster after startup unknown");
 auto pendingFull=model.Observe(MakeActivePictureObservation(extent,31,24));
 check(!pendingFull.publish&&pendingFull.stableBounds.top==90,
       "scope to full keeps existing adjacent confirmation requirement");
 auto confirmedFull=model.Observe(MakeActivePictureObservation(extent,32,24));
 check(confirmedFull.publish&&confirmedFull.bounds.top==0&&confirmedFull.bounds.bottom==720,
       "real transition model confirms scope to qualified full raster with ambiguous left edge");
 bool returnedScope=false;
 for(uint64_t n=33;n<=180;++n){auto returned=model.Observe(MakeActivePictureObservation(evidence,n,24));
     returnedScope=returnedScope||(returned.publish&&returned.bounds.top==90&&returned.bounds.bottom==630);}
 check(returnedScope,"real transition model returns from qualified full raster to measured scope");
 auto incomplete=qualified;incomplete.complete=false;
 check(!EvidenceFromMeasurement(source,incomplete).available,"budget-interrupted full extent cannot gain authority");
 auto brightnessOnly=qualified;brightnessOnly.fullExtentQualified=false;
 check(!EvidenceFromMeasurement(source,brightnessOnly).available,"unqualified bright outer pixels cannot gain full extent authority");
 auto wrongRaster=qualified;wrongRaster.right=1278;
 check(!EvidenceFromMeasurement(source,wrongRaster).available,"qualified result must match current full input raster");
 auto unknownRange=source;unknownRange.range=AnalysisLumaRange::Unknown;
 check(!EvidenceFromMeasurement(unknownRange,qualified).available,"unknown code range cannot import full extent authority");
 auto absentSource=source;absentSource.data=nullptr;
 check(!EvidenceFromMeasurement(absentSource,qualified).available,"missing source cannot import full extent authority");
 ActivePictureMeasurementCache fullCache;fullCache.Begin(live,id,1,1);
 check(fullCache.SeedQueued(source,id,extent,EvaluateActivePictureArDarkness(source,extent)),
       "qualified full extent uses existing exact-source worker cache");
 auto stale=id;++stale.rendererGeneration;
 check(!fullCache.SeedQueued(source,stale,extent,darkness),"full extent from stale renderer context cannot import");
 stale=id;++stale.analysisDomainKey;
 check(!fullCache.SeedQueued(source,stale,extent,darkness),"full extent from another pixel domain cannot import");
 MeasuredHistory history;history.Hint(id,1280,720,1000);history.Observe(measured,1280,720,1000);
 ++id.acceptedSequence;check(history.Hint(id,1280,720,1100).valid,"current context may nominate established measurement");
 ++id.viewportGeneration;++id.acceptedSequence;check(!history.Hint(id,1280,720,1200).valid,"viewport reset invalidates history");
 history.Observe(measured,1280,720,1200);++id.acceptedSequence;check(!history.Hint(id,1280,720,3301).valid,"history expires without fresh strong measurements");
 history.Observe(measured,1280,720,3301);check(!history.Hint(id,1280,720,3302).valid,"repeated or out-of-order sources cannot borrow history");
 Result unknown;check(!EvidenceFromMeasurement(source,unknown).available,"unknown cannot become native authority");
 Result full;full.kind=Kind::FullRaster;full.complete=true;check(!EvidenceFromMeasurement(source,full).available,"sparse full-raster hypothesis cannot override production detection");
 // An interrupted active worker must not substitute the legacy detector's
 // full-raster classification on a current frame with raised matte.
 for(int y=0;y<720;++y)for(int x=0;x<1280;++x)
     (*pixels)[y*1280+x]=static_cast<uint16_t>((x<210||x>=1070?144+(3*x+5*y)%9:450+x/12)<<6);
 const auto expired=Measure(1280,720,64,[&](int x,int y,Pixel& v){
     AnalysisLumaSample p;if(!source.Sample(x,y,p))return false;
     v={p.luma,p.chromaU,p.chromaV};return true;
 },[]{return false;});
 const auto expiredFallback=CurrentFallback(source,expired);
 check(expired.budgetExhausted && !expired.complete && !expiredFallback.available &&
       expiredFallback.classification==ActivePictureClassification::UNAVAILABLE &&
       ExtractActivePictureEvidence(source).classification==ActivePictureClassification::FULL_RASTER_TRUSTED,
       "expired measurement on raised matte cannot publish legacy full-raster authority");
 for(int y=0;y<720;++y)for(int x=0;x<1280;++x)(*pixels)[y*1280+x]=64<<6;
 ++id.acceptedSequence;const auto fallback=MeasureActiveTrial(source,id);const auto legacy=ExtractActivePictureEvidence(source);
 check(fallback.classification==legacy.classification&&fallback.reason==legacy.reason,"uncertain active trial uses current-frame legacy evidence");
 check(RunTemporalAspectSequences(24000.0/1001,30),"23.976 fps temporal sequences");
 check(RunTemporalAspectSequences(60000.0/1001,31),"59.94 fps temporal sequences");
 check(RunAspectOscillationSequence(24000.0/1001), "23.976 fps rapid aspect reversals");
 check(RunAspectOscillationSequence(60000.0/1001), "59.94 fps rapid aspect reversals");
 return failures?1:0;
}
