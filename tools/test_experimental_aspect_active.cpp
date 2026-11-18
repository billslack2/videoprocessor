#include "../src/VideoProcessor-Lib/vprenderer/ExperimentalAspectActive.h"
#include "../src/VideoProcessor-Lib/vprenderer/ActivePictureMeasurementCache.h"
#include <vector>
#include <cstdio>
using namespace ExperimentalAspect;
int main(){
 int failures=0;auto check=[&](bool b,const char*s){std::printf("%s %s\n",b?"PASS":"FAIL",s);if(!b)++failures;};
 SetEnvironmentVariableA("VP_AR_EXPERIMENT","active");
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
 auto darkness=EvaluateActivePictureArDarkness(source,evidence);
 auto live=source;live.generation=7;
 ActivePictureMeasurementCache cache;cache.Begin(live,id,1,1);
 check(cache.SeedQueued(source,id,evidence,darkness)&&cache.Picture().reason==evidence.reason,"exact-source worker evidence imported by real renderer cache");
 auto wrong=id;++wrong.acceptedSequence;
 check(!cache.SeedQueued(source,wrong,evidence,darkness),"different source cannot import experiment evidence");
 auto retention=cache.Retention(evidence.trustedBounds);
 check(retention.activePicture.trustedBounds.top==90,"existing retention receives new measured geometry");
 ActivePictureTransitionModel model;ActivePictureTransitionDecision decision;
 for(uint64_t n=1;n<=30;++n)decision=model.Observe(MakeActivePictureObservation(evidence,n,24));
 check(decision.stable&&decision.stableBounds.top==90,"existing transition model accepts measured native geometry");
 MeasuredHistory history;history.Hint(id,1280,720,1000);history.Observe(measured,1280,720,1000);
 ++id.acceptedSequence;check(history.Hint(id,1280,720,1100).valid,"current context may nominate established measurement");
 ++id.viewportGeneration;++id.acceptedSequence;check(!history.Hint(id,1280,720,1200).valid,"viewport reset invalidates history");
 history.Observe(measured,1280,720,1200);++id.acceptedSequence;check(!history.Hint(id,1280,720,3301).valid,"history expires without fresh strong measurements");
 history.Observe(measured,1280,720,3301);check(!history.Hint(id,1280,720,3302).valid,"repeated or out-of-order sources cannot borrow history");
 Result unknown;check(!EvidenceFromMeasurement(source,unknown).available,"unknown cannot become native authority");
 Result full;full.kind=Kind::FullRaster;full.complete=true;check(!EvidenceFromMeasurement(source,full).available,"sparse full-raster hypothesis cannot override production detection");
 for(int y=0;y<720;++y)for(int x=0;x<1280;++x)(*pixels)[y*1280+x]=64<<6;
 ++id.acceptedSequence;const auto fallback=MeasureActiveTrial(source,id);const auto legacy=ExtractActivePictureEvidence(source);
 check(fallback.classification==legacy.classification&&fallback.reason==legacy.reason,"uncertain active trial uses current-frame legacy evidence");
 return failures?1:0;
}
