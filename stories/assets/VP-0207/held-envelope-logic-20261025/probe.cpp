#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <vprenderer/SubtitleAspectRetention.h>
using namespace AlphaSourceCrop;
ActivePictureBounds Bounds(int t,int b,bool native=false) {
 return {0,t,3840,b,3840,2160,3840.0/(b-t),native?ActivePictureBounds::BarAxes::TOP_BOTTOM:ActivePictureBounds::BarAxes::NONE};
}
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 const auto base=Bounds(278,1882,true), expanded=Bounds(270,1960);
 auto history=UpdatePresentationEnvelopeExtents({},base,expanded,2,100,1000,2000);
 for(int i=0;i<3;++i){
  const uint64_t seq=101+i,now=1040+i*40;
  history=UpdatePresentationEnvelopeExtents(history,base,base,2,seq,now,2000);
  ActivePictureFrameIdentity id{};id.transportGeneration=2;id.acceptedSequence=seq;id.viewportGeneration=4;
  QualifiedSubtitlePresentationEvidence proof{};
  proof.identity=id;proof.physicalBase=base;
  proof.qualified=i!=1;proof.status=i!=1?QualifiedSubtitlePresentationStatus::Qualified:QualifiedSubtitlePresentationStatus::NotApplicable;
  proof.retention.analysisValid=true;proof.retention.excludedBandsPixelSafe=true;
  auto view=ProjectQualifiedSubtitlePresentationEvidence(proof,history,base,base,id,{});
  PresentationEnvelopeInput e{};e.envelopeAvailable=view.envelope.available;e.effectiveGeometryAvailable=true;e.baseMatchesEffectiveGeometry=true;
  e.detectedSourceSequence=view.envelope.sourceSequence;e.currentSourceSequence=seq;
  e.evidenceSourceGeneration=view.envelope.sourceGeneration;e.frameSourceGeneration=2;
  e.lastDetectionTick=view.envelope.lastDetectionTick;e.currentTick=now;e.holdMs=2000;
  auto active=EvaluatePresentationEnvelope(e);
  PresentationEnvelopeSelectionInput s{};s.history=view.envelope;s.effectiveBase=base;s.currentBounds=base;
  s.frameGeneration=s.currentGeneration=2;s.frameSequence=s.currentSequence=seq;s.envelopeActive=active.active;
  auto chosen=SelectPresentationEnvelopeBounds(s);
  Input c{};c.automaticCropEnabled=c.sharedGeometryAvailable=true;c.geometry=base;
  c.classification=c.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
  c.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;c.geometrySourceGeneration=c.frameSourceGeneration=2;
  c.frameSourceSequence=seq;c.framePresentationEpoch=4;c.rasterWidth=3840;c.rasterHeight=2160;
  c.frameLocalPresentationRetentionEvaluated=true;c.frameLocalPresentationRetentionSafe=true;
  c.verticalTranslationConfirmationPending=true;c.verticalTranslationBase=base;c.verticalTranslationSourceGeneration=2;
  // Conditional routing fixture: an existing FIT owner lets the held detector envelope through.
  c.outwardPresentationActive=c.outwardExpansionAvailable=active.active;c.outwardExpansion=chosen;c.outwardExpansionSourceGeneration=2;
  c.preparedSubtitle=QualifyPreparedSubtitleCropPresentation(proof,id,base);
  auto d=Evaluate(c);
  auto final=SubtitlePresentationBounds(d,SubtitlePreviewMode::GeneratedGrayBackground,proof.qualified,i==1);
  Require(d.applyCrop,"crop unexpectedly rejected");
  Require(final.top==(i==1?270:278) && final.bottom==(i==1?1960:1882),"unexpected output bounds");
  Require(history.available && history.bounds.bottom==1960,"raw held envelope was retired");
  Require(i!=1 || d.owner==DecisionOwner::TRANSLATION_CONFIRMATION,"confirmation owner not exercised");
  std::cout<<"seq="<<seq<<" proof="<<proof.qualified<<" raw_held="<<history.available<<" envelope_active="<<active.active<<" expanded="<<d.outwardExpanded<<" final="<<final.top<<".."<<final.bottom<<" reason="<<d.reason<<'\n';
 }
 PresentationEnvelopeCompositionInput composition{};composition.trustedPicture=base;
 composition.detectorContent.bounds=Bounds(270,1948);composition.detectorContent.expandTop=composition.detectorContent.expandBottom=true;
 composition.denseContent.bounds=Bounds(278,1916);composition.denseContent.expandBottom=true;
 composition.horizontalPadding=composition.verticalPadding=44;
 auto composed=BuildComposedPresentationEnvelope(composition);
 Require(composed.valid && composed.bounds.top==270 && composed.bounds.bottom==1960,"incident composition mismatch");
 std::cout<<"PASS: incident detector 270..1948 plus retained dense bottom 1916 +44 produces 270..1960.\n";
 VerticalBarPresentationState fit{};fit.action=VerticalBarPresentationAction::FIT;fit.lastDetectionTick=1000;fit.sourceSequence=100;
 Require(!IsVerticalBarPresentationActive(fit,10000,2000,200),"timer should expire");
 Require(IsVerticalBarPresentationActiveForFrame(fit,10000,2000,200,false,true,2,2),"skipped analysis did not bypass expiry");
 Require(!IsVerticalBarPresentationActiveForFrame(fit,10000,2000,200,true,true,2,2),"current analysis control should expire");
 Require(!IsVerticalBarPresentationActiveForFrame(fit,10000,2000,200,false,true,2,3),"generation control should expire");
 std::cout<<"PASS: FIT at age 9000ms with 2000ms hold remains active if dense analysis skipped; evaluated-frame and generation-change controls expire.\n";
 auto expired=UpdatePresentationEnvelopeExtents(history,base,base,2,104,3001,2000);
 Require(!expired.available,"history failed to expire");
 std::cout<<"PASS: production projection/policy reproduces native-expanded-native without threads; expiry control passes. Synthetic proof and FIT routing, not image/end-to-end reproduction.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
