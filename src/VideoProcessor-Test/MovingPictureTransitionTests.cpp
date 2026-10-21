#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/BufferedPictureExpansion.h>
#include <vprenderer/GradualPictureBoundaryRetention.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <SubtitleBoxLookahead.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <limits>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        struct CaptionMotionPixels
        {
            static constexpr int width=3840,height=2160;
            bool p210;
            std::vector<uint16_t> words;
            explicit CaptionMotionPixels(bool fullChroma=false):p210(fullChroma),
                words(size_t(width)*(height+(fullChroma?height:height/2)),uint16_t(512<<6)) {}
            void Rectangle(int left,int top,int right,int bottom,int luma) {
                for(int y=top;y<bottom;++y)
                    std::fill(words.begin()+size_t(y)*width+left,words.begin()+size_t(y)*width+right,uint16_t(luma<<6));
            }
            void Draw(int top,int bottom,bool caption=true,int captionShift=0,bool darkSides=false) {
                for(int y=0;y<height;++y)
                    std::fill(words.begin()+size_t(y)*width,words.begin()+size_t(y+1)*width,
                        uint16_t((y>=top && y<bottom?300:64)<<6));
                if(darkSides) {
                    // Genuine dim picture fades smoothly into both side edges.
                    // A horizontal bar candidate has no trustworthy boundary;
                    // the strict vertical-only proof must preserve all columns.
                    for(int y=top;y<bottom;++y)
                        for(int x=0;x<640;++x) {
                            const auto code=uint16_t((68+(300-68)*x/640)<<6);
                            words[size_t(y)*width+x]=code;
                            words[size_t(y)*width+(width-1-x)]=code;
                        }
                }
                if(!caption)return;
                Rectangle(1190,1916+captionShift,2650,2032+captionShift,64);
                for(int glyph=0;glyph<26;++glyph) {
                    const int x=1210+glyph*52;
                    Rectangle(x,1930+captionShift,x+34,2012+captionShift,800);
                    Rectangle(x+8,1940+captionShift,x+26,2002+captionShift,64);
                }
            }
            AnalysisLumaSource Source() const {
                return {reinterpret_cast<const uint8_t*>(words.data()),words.size()*2,width,height,
                    size_t(width)*2,size_t(width)*2,p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,
                    p210?VideoFrameEncoding::V210:VideoFrameEncoding::UNKNOWN,ColorSpace::REC_709,7};
            }
            SubtitleAssistedVerticalBarProof Proof(const ActivePictureFrameIdentity& identity,
                uint64_t policy=41,uint64_t continuity=9) const {
                const auto source=Source();const auto raw=ExtractActivePictureEvidence(source);
                SubtitleBoxDetector scanner;scanner.SetScriptHint(SubtitleScriptHint::Latin);
                scanner.SetNearBarDistance(20);
                SubtitlePictureAuthority shared;shared.required=true;shared.identity=identity;
                const auto observation=SubtitleBoxLookahead::Measure(scanner,source,raw,identity,false,{},policy,continuity,shared);
                const auto proof=EvaluateSubtitleAssistedVerticalBarProof(source,raw,observation,identity,policy,continuity,SubtitleScriptHint::Latin);
                if(!proof.accepted) {
                    std::ostringstream detail;detail<<"Native caption fixture proof="<<proof.reason
                        <<" detected="<<observation.text.detected<<" detector="<<observation.text.diagnosticReason
                        <<" physical="<<observation.pictureTop<<".."<<observation.pictureBottom
                        <<" independent="<<observation.independentBarAnalysis<<" raw_class="<<int(raw.classification)
                        <<" raw="<<raw.proposedBounds.top<<".."<<raw.proposedBounds.bottom;
                    Logger::WriteMessage(detail.str().c_str());
                }
                return proof;
            }
        };

        struct MotionSequence
        {
            TransitionAdmissionInput input;
            ActivePictureFrameIdentity identity{7,100,100,41708300,11,13,17};
            MovingPictureTransitionState motion;
            explicit MotionSequence(int height=2160,double fps=24)
            {
                const int width=height*16/9, top=(height*128/1000)/2*2;
                input.trustedGeometry={0,top,width,height-top,width,height,
                    double(width)/(height-2*top),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                input.presentationBeforeObservation=input.trustedGeometry;
                input.trustedGeometryAvailable=input.compatiblePresentation=true;
                input.trustedGeneration=input.sourceGeneration=input.presentationEvidenceGeneration=7;
                input.framesPerSecond=fps;
                input.evidence.available=true;
                input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
                input.evidence.axisEvidence.vertical.scanComplete=true;
                input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
                input.evidence.top.trusted=input.evidence.bottom.trusted=true;
                input.retention.analysisValid=input.retention.presentationValid=true;
                input.retention.expansionStripsAvailable=true;
                input.retention.expansionBase=input.trustedGeometry;
                auto& edge=input.retention.expandingTop;
                edge.barPixels=32; edge.blackFraction=.2; edge.continuity=.4; edge.lumaP90=300;
                input.retention.expandingBottom=edge;
                Target(top-8);
            }
            void Target(int top)
            {
                auto b=input.trustedGeometry; b.top=top; b.bottom=b.rasterHeight-top;
                b.aspectRatio=double(b.rasterWidth)/(b.bottom-b.top);
                input.evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
                input.evidence.trustedBounds=input.evidence.proposedBounds=input.outwardCandidate=b;
                input.retention.expansionCandidate=b;
            }
            void Step(bool classicPicturePresentationAllowed=true)
            {
                input.sourceSequence=identity.acceptedSequence;
                const auto tracking=PictureMotionTrackingInput(input,classicPicturePresentationAllowed);
                motion=ObserveMovingPictureTransition(motion,identity,tracking);
                ++identity.acceptedSequence; ++identity.sourceFrameNumber;
                identity.captureTimestamp+=uint64_t(std::llround(10000000.0/input.framesPerSecond));
            }
            void Ramp(int count=12,int step=2)
            {
                const int start=input.trustedGeometry.top-8;
                for (int i=0;i<count;++i) { Target(start-step*i); Step(); }
            }
        };
    }
    TEST_CLASS(MovingPictureTransitionTests)
    {
    public:
        TEST_METHOD(CurrentNativeSubtitleBarsAcquireGradualHoldBeforeIntermediatePublication)
        {
            for(bool p210:{false,true}) {
                CaptionMotionPixels caption(p210),clean(p210);
                MotionSequence s,control;
                ActivePictureTransitionModel model;
                for(uint64_t sequence=1;sequence<=4;++sequence)
                    model.Observe({s.input.trustedGeometry,sequence,true,ActivePictureClassification::BAR_CROP_TRUSTED,23.976});
                Input crop;crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
                crop.geometry=s.input.trustedGeometry;crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.geometrySourceGeneration=crop.frameSourceGeneration=7;crop.frameSourceSequence=99;
                crop.framePresentationEpoch=17;crop.rasterWidth=3840;crop.rasterHeight=2160;
                auto displayed=AdmitCropPresentation({},crop,Evaluate(crop),17);
                Assert::IsFalse(displayed.blocked);
                int acquired=-1,controlAcquired=-1;bool captionBiasedRawSeen=false;
                for(int frame=0;frame<18;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1),bottom=2160-top;
                    caption.Draw(top,bottom);clean.Draw(top,bottom,false);
                    const auto proof=caption.Proof(s.identity);
                    std::ostringstream detail;detail<<"Native caption ramp P210="<<p210<<" frame="<<frame<<" proof="<<proof.reason;
                    Logger::WriteMessage(detail.str().c_str());
                    const auto message=detail.str();const std::wstring wide(message.begin(),message.end());
                    Assert::IsTrue(proof.accepted,wide.c_str());
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.translationDriftActive=true;s.input.previousOutward.verticalPresentationSeen=true;
                    s.input.retention=EvaluateActivePicturePresentationRetention(caption.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(caption.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                    captionBiasedRawSeen|=proof.evidence.proposedBounds.bottom!=proof.candidate.bottom;
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        caption.Source(),proof,41,9,SubtitleScriptHint::Latin);
                    control.Target(top);control.input.evidence=ExtractActivePictureEvidence(clean.Source());
                    control.input.outwardCandidate=control.input.evidence.trustedBounds;
                    control.input.sourceSequence=control.identity.acceptedSequence;
                    control.input.retention=EvaluateActivePicturePresentationRetention(clean.Source(),control.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(clean.Source(),control.input.trustedGeometry,
                        control.input.outwardCandidate,control.input.retention);
                    control.motion=ObserveMovingPictureTransition(control.motion,control.identity,control.input);
                    if(s.motion.active && acquired<0)acquired=frame;
                    if(control.motion.active && controlAcquired<0)controlAcquired=frame;
                    auto admission=EvaluateTransitionAdmission(s.input);
                    ConstrainMovingPictureTransition(s.motion,admission);
                    const auto transition=model.Observe(admission.observation);
                    Assert::IsFalse(transition.publish,L"Captioned continuous motion must not publish intermediate aspects.");
                    if(s.motion.active) {
                        Assert::IsTrue(s.motion.subtitleAssistedBars);
                        crop.frameSourceSequence=s.identity.acceptedSequence;
                        crop.movingPictureTransition=true;crop.movingPictureHold.base=crop.geometry;
                        crop.movingPictureHold.sourceGeneration=7;crop.movingPictureHold.sourceSequence=crop.frameSourceSequence;
                        crop.movingPictureHold.presentationEpoch=17;crop.movingPictureHold.subtitleAssistedBars=s.motion.subtitleAssistedBars;
                        crop.movingPictureHold.verticalFitOnly=true;crop.movingPictureHold.competingPresentation=true;
                        crop.verticalFitConfirmationPending=true;
                        crop.currentVisibleBoundsAvailable=true;crop.currentVisibleBounds=crop.geometry;
                        crop.currentVisibleBounds.bottom=2076;crop.currentVisibleBase=crop.geometry;
                        crop.currentVisibleSourceGeneration=7;crop.currentVisibleSourceSequence=crop.frameSourceSequence;
                        for(bool confirmedFit:{false,true})
                        for(auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground}) {
                            auto routed=crop;
                            routed.movingPictureHold.competingPresentation=confirmedFit;
                            Assert::IsTrue(RouteAdvancedMovingPictureHold(routed,mode),
                                L"Exact current subtitle-assisted motion must supersede only legacy vertical FIT.");
                            const auto actual=AdmitCropPresentation(displayed.state,routed,Evaluate(routed),17);
                            Assert::IsFalse(actual.blocked);Assert::IsTrue(actual.presentation.applyCrop);
                            Assert::IsTrue(actual.presentation.owner==DecisionOwner::MOVING_PICTURE_HOLD);
                            Assert::AreEqual(crop.geometry.top,actual.presentation.sourceBounds.top);
                            Assert::AreEqual(crop.geometry.bottom,actual.presentation.sourceBounds.bottom);
                        }
                        for(int conflict=0;conflict<3;++conflict) {
                            auto blocked=crop;
                            if(conflict==0)blocked.movingPictureHold.subtitleAssistedBars=false;
                            if(conflict==1)blocked.movingPictureHold.verticalFitOnly=false;
                            if(conflict==2)blocked.barCropRefinementHorizontalConflict=true;
                            Assert::IsFalse(RouteAdvancedMovingPictureHold(blocked,SubtitlePreviewMode::GeneratedGrayBackground),
                                L"Generic FIT, missing assisted provenance, and horizontal conflicts retain their competing ownership.");
                            Assert::IsFalse(HasCurrentMovingPictureHold(blocked));
                        }
                    }
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                    ++control.identity.acceptedSequence;++control.identity.sourceFrameNumber;control.identity.captureTimestamp+=417083;
                }
                Assert::IsTrue(captionBiasedRawSeen,L"Fixture must reproduce a raw caption envelope different from the physical picture.");
                Assert::IsTrue(acquired>=0 && acquired<=7,L"Motion must be detected before intermediate crops cross the ordinary geometry deadband.");
                Assert::IsTrue(controlAcquired>=0 && acquired<=controlAcquired,
                    L"A current caption may prove native motion earlier, but must never postpone acquisition compared with the same picture without text.");
            }
        }
        TEST_METHOD(GuardedVerticalSubtitleBarsAcquireMotionDespiteDimUncertainSides)
        {
            for(bool p210:{false,true}) {
                CaptionMotionPixels pixels(p210);MotionSequence s;ActivePictureTransitionModel model;
                for(uint64_t sequence=1;sequence<=4;++sequence)
                    model.Observe({s.input.trustedGeometry,sequence,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
                int acquired=-1;bool sideFailureSeen=false,captionBiasedRawSeen=false;
                for(int frame=0;frame<18;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1),bottom=2160-top;
                    pixels.Draw(top,bottom,true,0,true);const auto source=pixels.Source();
                    const auto raw=ExtractActivePictureEvidence(source);const auto proof=pixels.Proof(s.identity);
                    if(frame==0) {
                        std::ostringstream diagnostic;diagnostic<<"Dim side caption ramp P210="<<p210
                            <<" horizontal="<<int(raw.axisEvidence.horizontal.state)
                            <<" reason="<<int(raw.axisEvidence.horizontal.reason)
                            <<" proposed="<<raw.proposedBounds.left<<","<<raw.proposedBounds.top
                            <<".."<<raw.proposedBounds.right<<","<<raw.proposedBounds.bottom;
                        Logger::WriteMessage(diagnostic.str().c_str());
                    }
                    Assert::IsTrue(proof.accepted,L"Dim uncertain sides must not invalidate independently proved vertical caption bars.");
                    Assert::IsTrue(raw.axisEvidence.horizontal.FailedBar() &&
                        raw.axisEvidence.horizontal.reason==ActivePictureAxisReason::BAR_EDGE_REJECTED,
                        L"The fixture must reproduce the native failed horizontal bar candidate from the recorded regression.");
                    sideFailureSeen=true;
                    Assert::IsTrue(proof.evidence.verticalBarGuardProfile.completed && proof.evidence.verticalBarGuardProfile.clean);
                    Assert::IsTrue(proof.evidence.axisEvidence.HasVerifiedVerticalCropProfile(proof.candidate));
                    Assert::IsTrue(proof.evidence.axisEvidence.verticalCropGlyphExclusions);
                    Assert::AreEqual(top-8,proof.candidate.top);Assert::AreEqual(bottom+8,proof.candidate.bottom);
                    Assert::AreEqual(0,proof.candidate.left);Assert::AreEqual(CaptionMotionPixels::width,proof.candidate.right);
                    captionBiasedRawSeen|=raw.proposedBounds.bottom!=proof.candidate.bottom;
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(source,s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(source,s.input.trustedGeometry,proof.candidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        source,proof,41,9,SubtitleScriptHint::Latin);
                    if(s.motion.active && acquired<0)acquired=frame;
                    auto admission=EvaluateTransitionAdmission(s.input);ConstrainMovingPictureTransition(s.motion,admission);
                    const auto transition=model.Observe(admission.observation);
                    Assert::IsFalse(transition.publish,L"A failed horizontal candidate cannot expose intermediate vertical aspects during a proved ramp.");
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                Assert::IsTrue(sideFailureSeen && captionBiasedRawSeen,
                    L"The fixture must combine failed horizontal bars with a subtitle-biased raw vertical proposal.");
                Assert::IsTrue(acquired>=0 && acquired<=7,
                    L"The exact guarded vertical proof must acquire motion before ordinary geometry publication, despite uncertain sides.");
            }
        }
        TEST_METHOD(SubtitleAssistedMotionRejectsStaleProofAndUnrelatedPictureOwners)
        {
            for(int fault=0;fault<16;++fault) {
                CaptionMotionPixels pixels;
                MotionSequence s;
                for(int frame=0;frame<10;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1);
                    pixels.Draw(top,2160-top);
                    auto source=pixels.Source();auto proof=pixels.Proof(s.identity);
                    Assert::IsTrue(proof.accepted);
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.input.retention=EvaluateActivePicturePresentationRetention(source,s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(source,s.input.trustedGeometry,proof.candidate,s.input.retention);
                    s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    auto selected=s.identity;bool classic=false;
                    switch(fault) {
                    case 0: --proof.identity.acceptedSequence;break;
                    case 1: ++proof.policyGeneration;break;
                    case 2: ++proof.continuityGeneration;break;
                    case 3: proof.scriptHint=SubtitleScriptHint::Thai;break;
                    case 4: ++s.input.sourceGeneration;break;
                    case 5: ++selected.analysisDomainKey;break;
                    case 6: proof.candidate.top-=2;break;
                    case 7: proof.accepted=false;break;
                    case 8: ++source.generation;break;
                    case 9: classic=true;break;
                    case 10: s.input.presentation.action=VerticalBarPresentationAction::FAIL_OPEN;break;
                    case 11: --proof.identity.captureTimestamp;break;
                    case 12: ++proof.identity.sourceFormatGeneration;break;
                    case 13: s.input.retention.globalNearBlack=true;s.input.retention.distributedDarkPicture=false;break;
                    case 14: s.input.retention.analysisValid=false;break;
                    }
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,selected,s.input,classic,
                        source,proof,41,9,SubtitleScriptHint::Latin);
                    Assert::IsFalse(s.motion.active,L"Stale or unrelated proof cannot clear a competing picture owner or acquire motion.");
                    s.input.sourceGeneration=7;
                    s.identity.acceptedSequence+=fault==15?2:1;
                    s.identity.sourceFrameNumber+=fault==15?2:1;
                    s.identity.captureTimestamp+=fault==15?834166:417083;
                }
            }
        }
        TEST_METHOD(AdjacentUnknownSubtitleProofKeepsOnlySeedAnchorUntilFreshMotionIsConfirmed)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            s.input.trustedGeometry.top=274;s.input.trustedGeometry.bottom=1886;
            s.input.trustedGeometry.aspectRatio=3840.0/(1886-274);
            s.input.presentationBeforeObservation=s.input.trustedGeometry;
            ActivePictureTransitionModel model;
            for(uint64_t sequence=1;sequence<=4;++sequence)
                model.Observe({s.input.trustedGeometry,sequence,true,ActivePictureClassification::BAR_CROP_TRUSTED,23.976});
            int acquired=-1;
            for(int frame=0;frame<14;++frame) {
                const int top=266-2*frame;
                pixels.Draw(top,2160-top);
                auto proof=pixels.Proof(s.identity);Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                if(frame==2)proof.accepted=false;
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                if(frame<=4)Assert::IsFalse(s.motion.active,
                    L"An unknown source proof and fewer than three fresh directional samples cannot acquire a picture hold.");
                if(s.motion.active && acquired<0)acquired=frame;
                auto admission=EvaluateTransitionAdmission(s.input);
                ConstrainMovingPictureTransition(s.motion,admission);
                const auto decision=model.Observe(admission.observation);
                Assert::IsFalse(decision.publish,L"An adjacent proof gap cannot expose intermediate ramp geometry before the existing hold is acquired.");
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
            Assert::IsTrue(acquired>=5 && acquired<=6,
                L"Retain the last valid anchor through an adjacent unknown proof, then require fresh directional motion before the geometry deadband.");
        }
        TEST_METHOD(UnknownSubtitleSeedCannotBridgeAnUnseenPictureJump)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            s.input.trustedGeometry.top=274;s.input.trustedGeometry.bottom=1886;
            s.input.trustedGeometry.aspectRatio=3840.0/(1886-274);
            s.input.presentationBeforeObservation=s.input.trustedGeometry;
            const int topRows[]={266,264,262,260,220,218,216,214};
            for(int frame=0;frame<8;++frame) {
                const int top=topRows[frame];pixels.Draw(top,2160-top);
                auto proof=pixels.Proof(s.identity);Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                if(frame==2 || frame==3)proof.accepted=false;
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                Assert::IsFalse(s.motion.active,
                    L"A cut hidden during a proof gap must reset the anchor; three tiny later changes cannot borrow the old displacement.");
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
        }

        TEST_METHOD(ActiveSubtitleMotionUnknownCannotSettleAndRealPictureChangesStillRetireHold)
        {
            for(int ending=0;ending<3;++ending) {
                CaptionMotionPixels pixels;MotionSequence s;
                for(int frame=0;frame<12;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1);
                    pixels.Draw(top,2160-top);const auto proof=pixels.Proof(s.identity);
                    Assert::IsTrue(proof.accepted);
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                Assert::IsTrue(s.motion.active && s.motion.subtitleAssistedBars);
                for(int unknown=0;unknown<10;++unknown) {
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),{},41,9,SubtitleScriptHint::Latin);
                    Assert::IsTrue(s.motion.active,L"Unknown same-context caption proof must not restore legacy FIT mid-ramp.");
                    Assert::AreEqual(uint32_t(0),s.motion.quietSamples,L"Unknown pixels cannot count as a settled endpoint.");
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                if(ending==0) {
                    ++s.identity.analysisDomainKey;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),{},41,9,SubtitleScriptHint::Latin);
                } else {
                    const int top=ending==1?0:96;
                    pixels.Draw(top,2160-top,false);
                    s.input.evidence=ExtractActivePictureEvidence(pixels.Source());
                    s.input.outwardCandidate=s.input.evidence.trustedBounds;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,
                        s.input.outwardCandidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),{},41,9,SubtitleScriptHint::Latin);
                }
                Assert::IsFalse(s.motion.active,L"A new analysis domain, native full raster, or abrupt picture cut must still end the moving hold.");
            }
        }

        TEST_METHOD(SubtitleMovementAloneCannotStartPhysicalPictureMotion)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            for(int frame=0;frame<12;++frame) {
                pixels.Draw(s.input.trustedGeometry.top,s.input.trustedGeometry.bottom,true,-frame*2);
                const auto source=pixels.Source();const auto proof=pixels.Proof(s.identity);
                Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(source,s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(source,s.input.trustedGeometry,proof.candidate,s.input.retention);
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    source,proof,41,9,SubtitleScriptHint::Latin);
                Assert::IsFalse(s.motion.active,L"A moving subtitle inside a stationary physical aperture is not a moving-picture transition.");
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
        }

        TEST_METHOD(BoundedNativeRampAcrossUnknownSubtitleFramesRequiresFreshQuietEndpoint)
        {
            for(bool p210:{false,true}) {
                CaptionMotionPixels pixels(p210);MotionSequence s;
                for(int frame=0;frame<23;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1);
                    pixels.Draw(top,2160-top);auto proof=pixels.Proof(s.identity);
                    Assert::IsTrue(proof.accepted);
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                    const bool unknown=frame>=12 && frame<22;
                    if(unknown)proof.accepted=false;
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                    if(frame>=11)Assert::IsTrue(s.motion.active,
                        L"Ten adjacent unknown caption samples cannot end a real bounded gradual picture ramp.");
                    if(unknown)Assert::AreEqual(uint32_t(0),s.motion.quietSamples);
                    if(frame==22)Assert::IsTrue(s.motion.quietSamples<=1,
                        L"The returning native proof starts fresh quiet evidence after the bounded gap.");
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                const auto endpoint=s.input.outwardCandidate;
                bool reachedQuiet=false;
                for(int quiet=0;quiet<9 && s.motion.active;++quiet) {
                    pixels.Draw(endpoint.top,endpoint.bottom);const auto proof=pixels.Proof(s.identity);
                    Assert::IsTrue(proof.accepted);
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                    reachedQuiet|=s.motion.awaitingPublication;
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                Assert::IsTrue(reachedQuiet && !s.motion.active,L"Only fresh proved stationary edges may settle the preserved ramp.");
            }
        }
        TEST_METHOD(NonadjacentUnknownCaptionGapRetainsPoseAndReseedsNativePosition)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            for(int frame=0;frame<16;++frame) {
                // Twenty real source intervals passed before this unmeasured
                // packet. The picture continued its two-pixel-per-frame ramp.
                if(frame==12) {
                    s.identity.acceptedSequence+=19;s.identity.sourceFrameNumber+=19;
                    s.identity.captureTimestamp+=19*417083;
                }
                const int top=frame<12?s.input.trustedGeometry.top-2*(frame+1):212-2*(frame-12);
                pixels.Draw(top,2160-top);auto proof=pixels.Proof(s.identity);
                Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                const bool unknown=frame==12 || frame==13;
                if(unknown)proof.accepted=false;
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                if(frame>=11)Assert::IsTrue(s.motion.active,
                    L"A source gap invalidates native positions without returning the admitted moving pose to legacy FIT.");
                if(unknown) {
                    Assert::AreEqual(0,s.motion.last.rasterHeight,
                        L"Neither the source gap nor the next unknown packet may retain a pre-gap native jump anchor.");
                    Assert::AreEqual(uint32_t(0),s.motion.quietSamples);
                }
                if(frame==14) {
                    Assert::AreEqual(proof.candidate.top,s.motion.last.top);
                    Assert::AreEqual(uint32_t(0),s.motion.quietSamples,
                        L"The first fresh proof only reseeds native position; it cannot compare with a pre-gap edge or count as quiet.");
                }
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
        }
        TEST_METHOD(SubtitleAssistedQuietEndpointPublishesWithCaptionStillPresent)
        {
            CaptionMotionPixels pixels;MotionSequence s;ActivePictureTransitionModel model;
            for(uint64_t frame=1;frame<=4;++frame)
                model.Observe({s.input.trustedGeometry,frame,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
            bool published=false;bool becameActive=false;
            ActivePictureBounds endpoint;
            for(int frame=0;frame<40 && !published;++frame) {
                const int top=s.input.trustedGeometry.top-2*(std::min)(frame+1,18);
                pixels.Draw(top,2160-top);const auto proof=pixels.Proof(s.identity);
                Assert::IsTrue(proof.accepted,L"The caption remains independently detected throughout endpoint confirmation.");
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;endpoint=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                becameActive|=s.motion.active;
                auto admission=EvaluateTransitionAdmission(s.input);
                ConstrainMovingPictureTransition(s.motion,admission);
                const auto transition=model.Observe(admission.observation);
                s.input.previousOutward=admission.outward.state;
                if(frame<18)Assert::IsFalse(transition.publish,L"Continuous native movement cannot publish an intermediate crop.");
                if(transition.publish) {
                    Assert::IsFalse(s.motion.active,L"Endpoint publication must wait for freshly proved quiet picture edges.");
                    Assert::AreEqual(endpoint.top,transition.bounds.top);Assert::AreEqual(endpoint.bottom,transition.bounds.bottom);
                    CompleteMovingPictureTransition(s.motion,s.input,transition);
                    Assert::IsFalse(HasCurrentMovingPictureTransition(s.motion,7,s.input.sourceSequence));
                    published=true;
                }
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
            Assert::IsTrue(becameActive && published,L"A stable endpoint must publish through normal admission while the subtitle remains present.");
        }
        TEST_METHOD(CurrentNativePictureProofSettlesRampAfterCaptionLeavesTheBar)
        {
            for(bool p210:{false,true})
            for(bool darkSides:{false,true}) {
                CaptionMotionPixels pixels(p210);MotionSequence s;
                bool acquired=false,nativeTakeover=false,settled=false;
                for(int frame=0;frame<94 && !settled;++frame) {
                    const int top=(std::max)(96,250-2*frame),bottom=2160-top;
                    pixels.Draw(top,bottom,true,0,darkSides);const auto source=pixels.Source();
                    const auto raw=ExtractActivePictureEvidence(source);
                    const auto proof=pixels.Proof(s.identity);
                    if(frame<6)Assert::IsTrue(proof.accepted,
                        L"The ramp must begin with a currently detected bar-occluding caption.");
                    s.input.evidence=proof.accepted?proof.evidence:raw;
                    s.input.outwardCandidate=proof.accepted?proof.candidate:raw.trustedBounds;
                    s.input.sourceSequence=s.identity.acceptedSequence;
                    // Legacy FIT can still be present for the original caption.
                    // Only current strict native picture evidence may replace the
                    // independent caption/bar certificate once text is fully inside.
                    s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(source,s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(source,s.input.trustedGeometry,s.input.outwardCandidate,s.input.retention);
                    if(top==96) {
                        Assert::IsFalse(proof.accepted,
                            L"The fixed caption is no longer near or overlapping the measured native black bar.");
                        Assert::IsTrue(raw.available && raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                        const int guard=darkSides?8:0;
                        Assert::AreEqual(top-guard,raw.trustedBounds.top);Assert::AreEqual(bottom+guard,raw.trustedBounds.bottom);
                        Assert::AreEqual(top,raw.proposedBounds.top);Assert::AreEqual(bottom,raw.proposedBounds.bottom);
                        Assert::IsTrue(pixels.words[size_t(1930)*CaptionMotionPixels::width+1210]==uint16_t(800<<6),
                            L"The caption remains visible inside the physical picture while native bars settle.");
                        if(darkSides) {
                            Assert::IsTrue(raw.axisEvidence.horizontal.FailedBar());
                            Assert::IsTrue(raw.axisEvidence.HasVerifiedVerticalCropProfile(raw.trustedBounds));
                            Assert::IsFalse(raw.axisEvidence.verticalCropGlyphExclusions,
                                L"Native takeover must use a current unmasked strict profile, without borrowing subtitle erasure ownership.");
                            Assert::IsTrue(raw.verticalBarGuardProfile.completed && raw.verticalBarGuardProfile.clean);
                            Assert::AreEqual(raw.trustedBounds.top,raw.verticalBarGuardProfile.apertureTop);
                            Assert::AreEqual(raw.trustedBounds.bottom,raw.verticalBarGuardProfile.apertureBottom);
                            if(!nativeTakeover) {
                                for(int fault=0;fault<9;++fault) {
                                    auto invalid=s.input;
                                    switch(fault) {
                                    case 0: invalid.evidence.verticalBarGuardProfile.completed=false;break;
                                    case 1: invalid.evidence.verticalBarGuardProfile.clean=false;break;
                                    case 2: invalid.evidence.verticalBarGuardProfile.apertureTop-=2;break;
                                    case 3: invalid.evidence.axisEvidence.verticalCropProfileEvaluated=false;break;
                                    case 4: invalid.evidence.axisEvidence.verticalCropGlyphExclusions=true;break;
                                    case 5: invalid.evidence.proposedBounds.bottom+=2;break;
                                    case 6: invalid.evidence.axisEvidence.sidePictureTop-=2;break;
                                    case 7: invalid.evidence.top.trusted=false;break;
                                    case 8: invalid.evidence.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;break;
                                    }
                                    const auto rejected=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,invalid,false,
                                        source,proof,41,9,SubtitleScriptHint::Latin);
                                    Assert::IsTrue(rejected.active,L"Invalid guarded metadata may retain the pose, but cannot certify a new position or endpoint.");
                                    Assert::AreEqual(uint32_t(0),rejected.quietSamples,
                                        L"Incomplete, masked, misbound, or non-native vertical profiles cannot contribute quiet evidence.");
                                }
                            }
                        }
                        nativeTakeover=true;
                    }
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        source,proof,41,9,SubtitleScriptHint::Latin);
                    acquired|=s.motion.active && s.motion.subtitleAssistedBars;
                    if(acquired && !s.motion.active) {
                        Assert::AreEqual(96,top,L"A continuing gradual ramp must retain the original presentation until its actual endpoint.");
                        Assert::IsTrue(s.motion.awaitingPublication && nativeTakeover,
                            L"Fresh native picture proof must settle the episode even after caption-assisted bar proof is unavailable.");
                        settled=true;
                    }
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
                Assert::IsTrue(acquired && nativeTakeover && settled,
                    L"Caption ownership may not freeze an old crop after current native bars establish a quiet endpoint.");
            }
        }
        TEST_METHOD(GuardedRawVerticalBarsCannotAcquireSubtitleMotionWithoutAnExistingEpisode)
        {
            for(bool p210:{false,true}) {
                CaptionMotionPixels pixels(p210);MotionSequence s;
                for(int frame=0;frame<12;++frame) {
                    const int top=96-2*frame;pixels.Draw(top,2160-top,true,0,true);
                    const auto source=pixels.Source();const auto raw=ExtractActivePictureEvidence(source);
                    const auto proof=pixels.Proof(s.identity);
                    Assert::IsFalse(proof.accepted);
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                        raw.axisEvidence.HasVerifiedVerticalCropProfile(raw.trustedBounds));
                    Assert::IsFalse(raw.axisEvidence.verticalCropGlyphExclusions);
                    s.input.evidence=raw;s.input.outwardCandidate=raw.trustedBounds;
                    s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(source,s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(source,s.input.trustedGeometry,s.input.outwardCandidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        source,proof,41,9,SubtitleScriptHint::Latin);
                    Assert::IsFalse(s.motion.active || s.motion.awaitingPublication,
                        L"Unmasked guarded raw bars can continue only a previously acquired subtitle-assisted pose.");
                    Assert::AreEqual(0,s.motion.last.rasterHeight,
                        L"The continuation exception may not manufacture a subtitle-motion acquisition seed.");
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
            }
        }
        TEST_METHOD(ClassicNoneDoesNotBorrowSubtitleBarsToReplaceMissingBroadPictureProof)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            for(int frame=0;frame<12;++frame) {
                const int top=s.input.trustedGeometry.top-2*(frame+1);
                pixels.Draw(top,2160-top);const auto proof=pixels.Proof(s.identity);Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::NONE;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                s.input.retention.expansionStripsAvailable=false;
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,true,
                    pixels.Source(),proof,41,9,SubtitleScriptHint::Latin);
                Assert::IsFalse(s.motion.active,L"Classic NONE must retain its original broad native picture proof contract.");
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
        }
        TEST_METHOD(SubtitleRulesRetireActiveMotionWhileContinuityOnlyReseedsProof)
        {
            for(int changed=0;changed<3;++changed) {
                CaptionMotionPixels pixels;MotionSequence s;
                for(int frame=0;frame<13;++frame) {
                    const int top=s.input.trustedGeometry.top-2*(frame+1);
                    const uint64_t policy=changed==0 && frame==12?42:41;
                    const uint64_t continuity=changed==1 && frame==12?10:9;
                    const auto script=changed==2 && frame==12?SubtitleScriptHint::Thai:SubtitleScriptHint::Latin;
                    pixels.Draw(top,2160-top);const auto proof=pixels.Proof(s.identity,policy,continuity);
                    Assert::IsTrue(proof.accepted);
                    s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                    s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                    s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                    ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                    s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                        pixels.Source(),proof,policy,continuity,script);
                    if(frame==11)Assert::IsTrue(s.motion.active);
                    if(frame==12) {
                        if(changed==1) {
                            Assert::IsTrue(s.motion.active && s.motion.subtitleAssistedBars,
                                L"A continuity-only queue or analysis gap must retain an already admitted same-context pose.");
                            Assert::AreEqual(uint32_t(0),s.motion.directionalChanges);
                            Assert::AreEqual(uint32_t(0),s.motion.quietSamples,
                                L"Fresh proof after continuity changes reseeds position without quiet credit.");
                        } else Assert::IsFalse(s.motion.active,
                            L"Changing subtitle policy or script must start fresh motion confirmation.");
                    }
                    ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
                }
            }
        }

        TEST_METHOD(ContinuityChangeCannotPreserveAnUnacquiredSubtitleMotionSeed)
        {
            CaptionMotionPixels pixels;MotionSequence s;
            for(int frame=0;frame<8;++frame) {
                const int top=268-2*frame;
                const uint64_t continuity=frame<4?9:10;
                pixels.Draw(top,2160-top);const auto proof=pixels.Proof(s.identity,41,continuity);
                Assert::IsTrue(proof.accepted);
                s.input.evidence=proof.evidence;s.input.outwardCandidate=proof.candidate;
                s.input.sourceSequence=s.identity.acceptedSequence;s.input.presentation.action=VerticalBarPresentationAction::FIT;
                s.input.retention=EvaluateActivePicturePresentationRetention(pixels.Source(),s.input.trustedGeometry);
                ResampleActivePictureExpansionStrips(pixels.Source(),s.input.trustedGeometry,proof.candidate,s.input.retention);
                s.motion=ObserveSubtitleAssistedMovingPictureTransition(s.motion,s.identity,s.input,false,
                    pixels.Source(),proof,41,continuity,SubtitleScriptHint::Latin);
                Assert::IsFalse(s.motion.active || s.motion.awaitingPublication,
                    L"A pending seed cannot borrow displacement or direction from before a continuity change.");
                if(frame==4)Assert::AreEqual(uint32_t(0),s.motion.directionalChanges);
                ++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;s.identity.captureTimestamp+=417083;
            }
        }

        TEST_METHOD(SustainedOpposingMotionPrecedesTwoPercentDeadbandAcrossRatesAndRasters)
        {
            for (int height : {1080,2160})
            for (double fps : {23.976,24.0,59.94,60.0})
            for (int step : {2,4})
            {
                MotionSequence s(height,fps);
                const int first=s.input.trustedGeometry.top-std::max(4,height/270);
                bool detected=false;
                for (int i=0;i<20 && !detected;++i)
                {
                    s.Target(first-step*i); s.Step(); detected=s.motion.active;
                    if (!detected && step<=std::max(2,height/540))
                        Assert::IsTrue(2*(s.input.trustedGeometry.top-s.input.outwardCandidate.top)<=int(height*.02),
                            L"Movement detection must precede the logged deadband staircase");
                }
                Assert::IsTrue(detected);
            }
        }
        TEST_METHOD(AbruptChangesAndAnchoredScanNoiseNeverBecomeMovement)
        {
            for (int height : {1080,2160})
            for (bool noise : {false,true})
            {
                MotionSequence s(height); const int target=height/32/2*2;
                for (int i=0;i<80;++i)
                {
                    s.Target(target+(noise && i%2 ? std::max(2,height/540) : 0)); s.Step();
                    Assert::IsFalse(s.motion.active);
                }
            }
        }
        TEST_METHOD(IntermediateEndpointRequiresAdjacentQuietSamplesThenNormalAuthority)
        {
            for (double fps : {23.976,24.0,59.94,60.0})
            {
                MotionSequence s(2160,fps); s.Ramp(30,4); Assert::IsTrue(s.motion.active);
                const auto before=s.input.trustedGeometry;
                const auto required=uint32_t(std::ceil(fps*.25)+1);
                uint32_t count=0;
                while (s.motion.active && count<=required) { s.Step(); ++count; }
                Assert::IsFalse(s.motion.active); Assert::IsTrue(count<=required);
                Assert::AreEqual(before.top,s.input.trustedGeometry.top);
                Assert::IsTrue(s.input.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
                const auto admission=EvaluateTransitionAdmission(s.input);
                Assert::IsFalse(admission.deferPartialComposition);
            }
        }
        TEST_METHOD(RepeatedCaptureCannotAgeMotionOrQuietProof)
        {
            MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
            const auto prior=s.motion;
            s.input.sourceSequence=prior.identity.acceptedSequence;
            for (int i=0;i<100;++i)
            {
                const auto repeated=ObserveMovingPictureTransition(prior,prior.identity,s.input);
                Assert::IsTrue(repeated.active);
                Assert::AreEqual(prior.quietSamples,repeated.quietSamples);
                Assert::AreEqual(prior.directionalChanges,repeated.directionalChanges);
            }
        }
        TEST_METHOD(StaleCaptureReplayCannotRewindOrSettleMotion)
        {
            MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
            const auto current=s.motion.identity;
            auto state=s.motion;
            for (uint64_t sequence=current.acceptedSequence-10;sequence<=current.acceptedSequence;++sequence)
            {
                auto stale=current; stale.acceptedSequence=stale.sourceFrameNumber=sequence;
                stale.captureTimestamp=sequence*417083;
                s.input.sourceSequence=sequence;
                s.Target(68); // Even an old hard-cut rectangle cannot retire it.
                state=ObserveMovingPictureTransition(state,stale,s.input);
                Assert::IsTrue(state.active);
                Assert::AreEqual(current.acceptedSequence,state.identity.acceptedSequence);
                Assert::AreEqual(s.motion.quietSamples,state.quietSamples);
            }
            Assert::IsFalse(HasCurrentMovingPictureTransition(state,7,current.acceptedSequence-1));
            Assert::IsFalse(HasCurrentMovingPictureTransition(state,8,current.acceptedSequence));
        }
        TEST_METHOD(StaleCompletionCannotRetirePendingPresentation)
        {
            MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
            s.Target(68); s.Step(); Assert::IsTrue(s.motion.awaitingPublication);
            const auto current=s.motion.identity;
            auto old=current; --old.acceptedSequence; --old.sourceFrameNumber;
            s.input.sourceSequence=old.acceptedSequence;
            s.input.evidence.trustedBounds=s.input.trustedGeometry;
            s.input.retention.excludedBandsPixelSafe=true;
            s.motion=ObserveMovingPictureTransition(s.motion,old,s.input);
            ActivePictureTransitionDecision stale; stale.publish=stale.stable=true;
            CompleteMovingPictureTransition(s.motion,s.input,stale);
            Assert::IsTrue(s.motion.awaitingPublication);
            Assert::AreEqual(current.acceptedSequence,s.motion.identity.acceptedSequence);
        }
        TEST_METHOD(CaptureGapsResetProofWhileContextChangesDiscardMotionHistory)
        {
            for (int fault=0;fault<11;++fault)
            {
                MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
                switch (fault)
                {
                case 0: ++s.identity.transportGeneration; break;
                case 1: ++s.identity.sourceFormatGeneration; break;
                case 2: ++s.identity.viewportGeneration; break;
                case 3: ++s.identity.rendererGeneration; break;
                case 4: ++s.identity.acceptedSequence; break;
                case 5: ++s.identity.sourceFrameNumber; break;
                case 6: s.identity.sourceFrameNumber=s.motion.identity.sourceFrameNumber; break;
                case 7: s.identity.captureTimestamp=s.motion.identity.captureTimestamp; break;
                case 8: s.input.trustedGeometry.top+=2; break;
                case 9: s.input.trustedGeneration=99; break;
                case 10: s.input.compatiblePresentation=false; break;
                }
                s.Step();
                const bool sameContextGap=fault>=4 && fault<=7;
                Assert::AreEqual(sameContextGap,s.motion.active);
                if (sameContextGap) Assert::AreEqual(0u,s.motion.quietSamples);
            }
        }
        TEST_METHOD(AlternatingQuantizedEdgesStillEstablishOpposingMotion)
        {
            MotionSequence s;
            int top=268,bottom=1892;
            for (int i=0;i<12;++i)
            {
                if (i%2) bottom+=4; else top-=4;
                s.Target(top);
                auto b=s.input.outwardCandidate; b.bottom=bottom;
                b.aspectRatio=3840.0/(b.bottom-b.top);
                s.input.evidence.trustedBounds=s.input.evidence.proposedBounds=s.input.outwardCandidate=b;
                s.input.retention.expansionCandidate=b;
                s.Step();
            }
            Assert::IsTrue(s.motion.active);
        }
        TEST_METHOD(ProvisionalRasterEndpointCannotQuietlyRestoreOldScope)
        {
            MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
            s.input.evidence.classification=ActivePictureClassification::PROVISIONAL;
            for (int i=0;i<60;++i) { s.Step(); Assert::IsTrue(s.motion.active); Assert::AreEqual(0u,s.motion.quietSamples); }
            s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            s.Step(); Assert::IsFalse(s.motion.active);
        }
        TEST_METHOD(HardCutsDuringMotionExitOnCurrentSourceFrame)
        {
            for (int target : {0,68,276})
            {
                MotionSequence s; s.Ramp(); Assert::IsTrue(s.motion.active);
                s.Target(target);
                if (target==0) s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
                s.Step(); Assert::IsFalse(s.motion.active);
            }
        }
        TEST_METHOD(SubtitleDarknessAndUncertifiedExpansionsCannotStartMotion)
        {
            for (int fault=0;fault<12;++fault)
            {
                MotionSequence s;
                for (int i=0;i<20;++i)
                {
                    s.Target(264-i*4);
                    switch (fault)
                    {
                    case 0: s.input.retention.globalNearBlack=true; break;
                    case 1: s.input.presentation.action=VerticalBarPresentationAction::TRANSLATE; break;
                    case 2: s.input.presentation.action=VerticalBarPresentationAction::FIT; break;
                    case 3: s.input.translationDriftActive=true; break;
                    case 4: s.input.previousOutward.verticalPresentationSeen=true; break;
                    case 5: s.input.retention.expansionStripsAvailable=false; break;
                    case 6: s.input.retention.expandingBottom.blackFraction=1; break;
                    case 7: s.input.evidence.available=false; break;
                    case 8: s.input.retention.expansionCandidate.top+=2; break;
                    case 9: s.input.outwardCandidate.left=2; break;
                    case 10: s.input.evidence.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::BOTH; break;
                    case 11: s.input.retention.analysisValid=false; break;
                    }
                    s.Step(); Assert::IsFalse(s.motion.active);
                }
            }
        }
        TEST_METHOD(AdvancedSubtitleModeTracksOnlyNativeMotionAcrossClassicFlags)
        {
            MotionSequence advanced;
            advanced.input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
            advanced.input.translationDriftActive=true;
            advanced.input.previousOutward.verticalPresentationSeen=true;
            for(int i=0;i<12;++i)
            {
                advanced.Target(264-i*4);
                advanced.Step(false);
            }
            Assert::IsTrue(advanced.motion.active);
            Assert::IsTrue(advanced.input.presentation.action==VerticalBarPresentationAction::TRANSLATE);
            Assert::IsTrue(advanced.input.translationDriftActive);
            Assert::IsTrue(advanced.input.previousOutward.verticalPresentationSeen);

            MotionSequence classic;
            classic.input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
            classic.input.translationDriftActive=true;
            classic.input.previousOutward.verticalPresentationSeen=true;
            for(int i=0;i<12;++i)
            {
                classic.Target(264-i*4);
                classic.Step(true);
                Assert::IsFalse(classic.motion.active);
            }
        }
        TEST_METHOD(AdvancedModeDoesNotClearFitOrFailOpenPictureOwners)
        {
            for(auto action : {VerticalBarPresentationAction::FIT,
                VerticalBarPresentationAction::FAIL_OPEN})
            {
                MotionSequence s;
                s.input.presentation.action=action;
                s.input.translationDriftActive=true;
                s.input.previousOutward.verticalPresentationSeen=true;
                for(int i=0;i<12;++i)
                {
                    s.Target(264-i*4);
                    const auto tracking=PictureMotionTrackingInput(s.input,false);
                    Assert::IsTrue(tracking.presentation.action==action);
                    Assert::IsTrue(tracking.translationDriftActive);
                    Assert::IsTrue(tracking.previousOutward.verticalPresentationSeen);
                    s.Step(false);
                    Assert::IsFalse(s.motion.active);
                }
            }
        }
        TEST_METHOD(ActiveNativeDistributedRampSettlesOnlyAfterFreshQuietProof)
        {
            MotionSequence s;
            s.Ramp(12,4);
            Assert::IsTrue(s.motion.active);
            const auto broadTop=s.input.retention.expandingTop;
            const auto broadBottom=s.input.retention.expandingBottom;
            const auto distributed=[&](int top) {
                s.Target(top);
                s.input.evidence.axisEvidence.vertical.scanComplete=true;
                s.input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
                s.input.evidence.top.trusted=s.input.evidence.bottom.trusted=true;
                s.input.retention.activePicture=s.input.evidence;
                for(auto* edge : {&s.input.retention.expandingTop,
                                  &s.input.retention.expandingBottom})
                {
                    edge->blackFraction=.04;
                    edge->continuity=0;
                    edge->lumaP90=36;
                    edge->texture=7.1;
                    edge->distributedSupportDepthRowsByQuartile.fill(0x3f);
                }
                const auto proof=ConfirmOutwardPictureTransition({},s.input.trustedGeometry,
                    s.input.outwardCandidate,s.input.retention,7,s.identity.acceptedSequence);
                Assert::IsFalse(proof.broadOpposingPicture);
                Assert::IsTrue(proof.nativeDistributedPicture);
            };
            for(int i=0;i<8;++i)
            {
                const uint64_t current=s.identity.acceptedSequence;
                distributed(s.input.outwardCandidate.top-4);
                s.Step();
                Assert::IsTrue(HasCurrentMovingPictureTransition(s.motion,7,current));
                Assert::IsTrue(s.motion.active);
                Assert::IsTrue(s.motion.quietSamples<7);
            }
            // The next ordinary broad sample must compare with the latest
            // native-distributed edge, not the pre-darkness edge.
            s.input.retention.expandingTop=broadTop;
            s.input.retention.expandingBottom=broadBottom;
            s.Target(s.input.outwardCandidate.top-4);
            s.Step();
            Assert::IsTrue(s.motion.active);
            Assert::AreEqual(s.input.outwardCandidate.top,s.motion.last.top);

            // An unknown current strip interrupts quiet proof. Only seven new
            // distinct, stationary native-distributed sources can settle it.
            distributed(s.input.outwardCandidate.top);
            for(int i=0;i<3;++i) { s.Step(); Assert::IsTrue(s.motion.active); }
            s.input.retention.expandingTop.distributedSupportDepthRowsByQuartile.fill(0);
            s.Step();
            Assert::IsTrue(s.motion.active);
            Assert::AreEqual(0u,s.motion.quietSamples);
            constexpr int required=7; // ceil(24 fps * 250 ms) + 1
            for(int i=0;i<required;++i)
            {
                distributed(s.input.outwardCandidate.top);
                s.Step();
                if(i+1<required) Assert::IsTrue(s.motion.active);
            }
            Assert::IsFalse(s.motion.active);
            Assert::IsTrue(s.motion.awaitingPublication);
            ActivePictureTransitionDecision published;
            published.publish=published.stable=true;
            CompleteMovingPictureTransition(s.motion,s.input,published);
            Assert::IsFalse(s.motion.awaitingPublication);
        }
        TEST_METHOD(DistributedMotionCannotStartJumpGapsOrUnknownPixelProof)
        {
            MotionSequence s;
            s.Ramp(12,4);
            Assert::IsTrue(s.motion.active);
            const auto prior=s.motion;
            s.Target(prior.last.top-4);
            s.input.sourceSequence=s.identity.acceptedSequence;
            s.input.evidence.axisEvidence.vertical.scanComplete=true;
            s.input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
            s.input.evidence.top.trusted=s.input.evidence.bottom.trusted=true;
            s.input.retention.activePicture=s.input.evidence;
            for(auto* edge : {&s.input.retention.expandingTop,
                              &s.input.retention.expandingBottom})
            {
                edge->blackFraction=.04;
                edge->continuity=0;
                edge->lumaP90=36;
                edge->texture=7.1;
                edge->distributedSupportDepthRowsByQuartile.fill(0x3f);
            }
            Assert::IsFalse(ObserveMovingPictureTransition({},s.identity,s.input).active);
            auto unknown=s.input;
            unknown.retention.expandingTop.distributedSupportDepthRowsByQuartile.fill(0);
            const auto held=ObserveMovingPictureTransition(prior,s.identity,unknown);
            Assert::IsTrue(held.active);
            Assert::AreEqual(prior.last.top,held.last.top);
            auto gap=s.identity;
            gap.acceptedSequence+=2;
            gap.sourceFrameNumber+=2;
            gap.captureTimestamp+=834166;
            auto gapInput=s.input;
            gapInput.sourceSequence=gap.acceptedSequence;
            const auto afterGap=ObserveMovingPictureTransition(prior,gap,gapInput);
            Assert::IsTrue(afterGap.active);
            Assert::AreEqual(prior.last.top,afterGap.last.top);
            auto jumped=s.input;
            jumped.evidence.trustedBounds.top-=24;
            jumped.evidence.trustedBounds.bottom+=24;
            jumped.evidence.trustedBounds.aspectRatio=double(jumped.evidence.trustedBounds.rasterWidth)/
                (jumped.evidence.trustedBounds.bottom-jumped.evidence.trustedBounds.top);
            jumped.evidence.proposedBounds=jumped.outwardCandidate=jumped.evidence.trustedBounds;
            jumped.retention.expansionCandidate=jumped.evidence.trustedBounds;
            jumped.retention.activePicture=jumped.evidence;
            const auto abrupt=ObserveMovingPictureTransition(prior,s.identity,jumped);
            Assert::IsFalse(abrupt.active);
            Assert::IsTrue(abrupt.awaitingPublication);
        }
        TEST_METHOD(NearBlackSceneNoticeKeepsOneAdmittedScopeThroughMovingPicture)
        {
            MotionSequence s;
            const auto scope=s.input.trustedGeometry;
            s.Ramp(12,4);
            Assert::IsTrue(s.motion.active);

            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.latestObservationSupportsCrop=true;
            crop.geometry=scope;
            crop.rasterWidth=scope.rasterWidth;
            crop.rasterHeight=scope.rasterHeight;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
            crop.framePresentationEpoch=17;
            crop.frameSourceSequence=s.motion.identity.acceptedSequence;
            auto admitted=AdmitCropPresentation({},crop,Evaluate(crop),17);
            Assert::IsFalse(admitted.blocked);
            auto admission=admitted.state;
            AspectLimitFillInput fill;
            fill.trustedContentAuthorityAccepted=true;
            fill.cropNarrowerContentToFillScreen=fill.cropWiderContentToFillScreen=true;
            fill.narrowerLimitConfigured=fill.widerLimitConfigured=true;
            fill.narrowerAspectLimit=2.32;
            fill.widerAspectLimit=2.42;
            fill.screenAspect=2.35;
            fill.contentReferenceAvailable=true;
            fill.contentReferenceBounds=scope;
            fill.sourceBounds=scope;
            const auto originalFill=EvaluateAspectLimitFill(fill);
            Assert::IsTrue(originalFill.applied);

            // Produce the actual SceneDetector event type seen mid-ramp:
            // near-black entry with a measured difference, no hard-cut cause.
            SceneDetector detector;
            const uint64_t boundarySequence=s.identity.acceptedSequence;
            const auto level=[&](uint16_t luma,uint64_t sequence) {
                const std::vector<uint16_t> pixels(64*36,uint16_t(luma<<6));
                const SceneDetectorInput scene={pixels.data(),64,36,64*sizeof(uint16_t),
                    sequence,int64_t(sequence*417083),7,417083,true};
                return detector.Analyze(scene);
            };
            level(120,boundarySequence-3);
            level(108,boundarySequence-2);
            level(100,boundarySequence-1);
            const auto boundary=level(96,boundarySequence);
            Assert::IsTrue(boundary.safeBoundary);
            Assert::IsTrue(boundary.nearBlackEntry);
            Assert::IsFalse(boundary.hardCutCandidate);
            Assert::IsFalse(boundary.hardCutConfirmed);

            // Renderer clears scene candidates first, then exact current native
            // proof may restore only this already active moving episode.
            const auto beforeBoundary=s.motion;
            s.Target(beforeBoundary.last.top-4);
            s.input.sourceSequence=boundarySequence;
            s.input.evidence.axisEvidence.vertical.scanComplete=true;
            s.input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
            s.input.evidence.axisEvidence.vertical.barCandidate=true;
            s.motion={};
            s.motion=RetainMovingPictureAtDarknessBoundary(beforeBoundary,boundary,
                s.identity,s.input);
            Assert::IsTrue(s.motion.active);
            s.motion=ObserveMovingPictureTransition(s.motion,s.identity,s.input);
            Assert::IsTrue(HasCurrentMovingPictureTransition(s.motion,7,boundarySequence));
            ++s.identity.acceptedSequence;
            ++s.identity.sourceFrameNumber;
            s.identity.captureTimestamp+=417083;

            const auto presentHeldScope=[&](uint64_t sequence) {
                crop.frameSourceSequence=sequence;
                crop.currentVisibleBoundsAvailable=true;
                crop.currentVisibleBounds=scope;
                crop.currentVisibleBounds.bottom=scope.bottom+150;
                crop.currentVisibleBase=scope;
                crop.currentVisibleSourceGeneration=7;
                crop.currentVisibleSourceSequence=sequence;
                crop.movingPictureTransition=HasCurrentMovingPictureTransition(s.motion,7,sequence);
                crop.movingPictureHold.base=scope;
                crop.movingPictureHold.sourceGeneration=7;
                crop.movingPictureHold.sourceSequence=sequence;
                crop.movingPictureHold.presentationEpoch=17;
                Assert::IsTrue(crop.movingPictureTransition);
                // UpdateScopeSubtitleShift uses this exact-source predicate to
                // clear old FIT evidence and skip scanning moving picture edges.
                VerticalBarPresentationState subtitleFit;
                subtitleFit.action=VerticalBarPresentationAction::FIT;
                if (crop.movingPictureTransition) subtitleFit={};
                Assert::IsTrue(subtitleFit.action==VerticalBarPresentationAction::NONE);
                Assert::IsTrue(Evaluate(crop).owner==DecisionOwner::MOVING_PICTURE_HOLD);
                auto shown=AdmitCropPresentation(admission,crop,Evaluate(crop),17);
                Assert::IsFalse(shown.blocked);
                Assert::IsTrue(shown.presentation.applyCrop);
                Assert::AreEqual(scope.top,shown.presentation.sourceBounds.top);
                Assert::AreEqual(scope.bottom,shown.presentation.sourceBounds.bottom);
                fill.sourceBounds=shown.presentation.sourceBounds;
                const auto visible=EvaluateAspectLimitFill(fill);
                Assert::IsTrue(visible.applied);
                Assert::AreEqual(originalFill.sourceBounds.left,visible.sourceBounds.left);
                Assert::AreEqual(originalFill.sourceBounds.top,visible.sourceBounds.top);
                Assert::AreEqual(originalFill.sourceBounds.right,visible.sourceBounds.right);
                Assert::AreEqual(originalFill.sourceBounds.bottom,visible.sourceBounds.bottom);
                admission=shown.state;
            };
            presentHeldScope(boundarySequence);
            for(int i=0;i<8;++i)
            {
                const uint64_t current=s.identity.acceptedSequence;
                s.Target(s.motion.last.top-4);
                s.Step();
                presentHeldScope(current);
            }

            // A genuinely trusted full-raster frame terminates motion now;
            // it must never borrow the near-black exception or stale FIT state.
            const uint64_t endpointSequence=s.identity.acceptedSequence;
            s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            s.Step();
            Assert::IsFalse(s.motion.active);
            Assert::IsFalse(HasCurrentMovingPictureTransition(s.motion,7,endpointSequence));
            crop.frameSourceSequence=endpointSequence;
            crop.currentVisibleBoundsAvailable=false;
            crop.movingPictureTransition=false;
            crop.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            crop.latestObservationClassification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            crop.fullRasterPresentationAuthoritative=true;
            const auto full=AdmitCropPresentation(admission,crop,Evaluate(crop),17);
            Assert::IsFalse(full.blocked);
            Assert::IsFalse(full.presentation.applyCrop);
            // Once the motion owner is gone, an independently established FIT
            // is eligible for normal subtitle arbitration on a later source.
            s.input.presentation.action=VerticalBarPresentationAction::FIT;
            Assert::IsTrue(PictureMotionTrackingInput(s.input,true).presentation.action==
                VerticalBarPresentationAction::FIT);
        }
        TEST_METHOD(AdvancedPictureHoldsAdmittedScopeAndFillUntilMovingEndpointPublishes)
        {
            MotionSequence s;
            const auto established=s.input.trustedGeometry;
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.latestObservationSupportsCrop=true;
            crop.geometry=established;
            crop.rasterWidth=established.rasterWidth;
            crop.rasterHeight=established.rasterHeight;
            crop.geometrySourceGeneration=crop.frameSourceGeneration=7;
            crop.framePresentationEpoch=17;
            crop.frameSourceSequence=99;
            auto admitted=AdmitCropPresentation({},crop,Evaluate(crop),17);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.applyCrop);
            Assert::AreEqual(established.top,admitted.presentation.sourceBounds.top);
            auto admission=admitted.state;
            AspectLimitFillInput fill;
            fill.trustedContentAuthorityAccepted=true;
            fill.cropNarrowerContentToFillScreen=fill.cropWiderContentToFillScreen=true;
            fill.narrowerLimitConfigured=fill.widerLimitConfigured=true;
            fill.narrowerAspectLimit=2.32;
            fill.widerAspectLimit=2.42;
            fill.screenAspect=2.35;
            fill.contentReferenceAvailable=true;
            fill.contentReferenceBounds=established;
            fill.sourceBounds=established;
            const auto originalFill=EvaluateAspectLimitFill(fill);
            Assert::IsTrue(originalFill.applied);
            uint32_t holdFrames=0;
            uint32_t translatedBeforeHold=0;
            for(int i=0;i<18;++i)
            {
                s.Target(264-i*4);
                const uint64_t current=s.identity.acceptedSequence;
                s.input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
                s.input.translationDriftActive=true;
                s.input.previousOutward.verticalPresentationSeen=true;
                s.Step(false);
                crop.frameSourceSequence=current;
                crop.currentVisibleBoundsAvailable=true;
                crop.currentVisibleBounds=established;
                crop.currentVisibleBounds.bottom=established.bottom+150;
                crop.currentVisibleBase=established;
                crop.currentVisibleSourceGeneration=7;
                crop.currentVisibleSourceSequence=current;
                crop.verticalTranslationActive=true;
                crop.verticalTranslationPixels=20;
                crop.verticalTranslationBase=established;
                crop.verticalTranslationSourceGeneration=7;
                crop.verticalTranslationConfirmationPending=true;
                crop.movingPictureTransition=HasCurrentMovingPictureTransition(s.motion,7,current);
                crop.movingPictureHold.base=established;
                crop.movingPictureHold.sourceGeneration=7;
                crop.movingPictureHold.sourceSequence=current;
                crop.movingPictureHold.presentationEpoch=17;
                const bool routed=RouteAdvancedMovingPictureHold(
                    crop,SubtitlePreviewMode::GeneratedGrayBackground);
                if(crop.movingPictureTransition)
                {
                    Assert::IsTrue(routed);
                    ++holdFrames;
                }
                auto decision=Evaluate(crop);
                if(crop.movingPictureTransition)
                    Assert::IsTrue(decision.owner==DecisionOwner::MOVING_PICTURE_HOLD);
                else if(decision.owner==DecisionOwner::VERTICAL_TRANSLATION)
                    ++translatedBeforeHold;
                admitted=AdmitCropPresentation(admission,crop,decision,17);
                Assert::IsFalse(admitted.blocked);
                Assert::IsTrue(admitted.presentation.applyCrop);
                admission=admitted.state;
                auto physical=SubtitlePresentationBounds(admitted.presentation,
                    SubtitlePreviewMode::GeneratedGrayBackground,(i%2)==0,false);
                Assert::AreEqual(established.top,physical.top);
                Assert::AreEqual(established.bottom,physical.bottom);
                fill.sourceBounds=physical;
                const auto shown=EvaluateAspectLimitFill(fill);
                Assert::IsTrue(shown.applied);
                Assert::AreEqual(originalFill.sourceBounds.left,shown.sourceBounds.left);
                Assert::AreEqual(originalFill.sourceBounds.right,shown.sourceBounds.right);
                Assert::AreEqual(originalFill.sourceBounds.top,shown.sourceBounds.top);
                Assert::AreEqual(originalFill.sourceBounds.bottom,shown.sourceBounds.bottom);
            }
            Assert::IsTrue(holdFrames>0);
            Assert::IsTrue(translatedBeforeHold>0);
            for(int i=0;i<8 && s.motion.active;++i)
            {
                const uint64_t current=s.identity.acceptedSequence;
                s.Step(false);
                crop.frameSourceSequence=current;
                crop.currentVisibleSourceSequence=current;
                crop.movingPictureHold.sourceSequence=current;
                crop.movingPictureTransition=HasCurrentMovingPictureTransition(s.motion,7,current);
                RouteAdvancedMovingPictureHold(crop,SubtitlePreviewMode::GeneratedGrayBackground);
                const auto held=AdmitCropPresentation(admission,crop,Evaluate(crop),17);
                Assert::IsFalse(held.blocked);
                Assert::AreEqual(established.top,held.presentation.sourceBounds.top);
                admission=held.state;
            }
            Assert::IsFalse(s.motion.active);
            Assert::IsTrue(s.motion.awaitingPublication);
            ActivePictureTransitionDecision published;
            published.publish=published.stable=true;
            CompleteMovingPictureTransition(s.motion,s.input,published);
            Assert::IsFalse(s.motion.awaitingPublication);
            const auto endpoint=s.input.outwardCandidate;
            crop.geometry=endpoint;
            crop.frameSourceSequence=s.input.sourceSequence;
            crop.movingPictureTransition=false;
            crop.verticalTranslationActive=false;
            crop.verticalTranslationConfirmationPending=false;
            crop.latestObservationSupportsCrop=true;
            crop.currentVisibleBase=endpoint;
            crop.currentVisibleBounds=endpoint;
            crop.currentVisibleBounds.bottom=endpoint.bottom+80;
            crop.currentVisibleSourceSequence=crop.frameSourceSequence;
            const auto blockedEndpoint=AdmitCropPresentation(admission,crop,Evaluate(crop),17);
            Assert::IsTrue(blockedEndpoint.blocked);
            // Only a new current-source witness within the endpoint permits the final cut.
            crop.currentVisibleBounds=endpoint;
            fill.contentReferenceBounds=endpoint;
            admitted=AdmitCropPresentation(admission,crop,Evaluate(crop),17);
            Assert::IsFalse(admitted.blocked);
            Assert::IsTrue(admitted.presentation.applyCrop);
            Assert::AreEqual(endpoint.top,admitted.presentation.sourceBounds.top);
            Assert::IsTrue(established.top != admitted.presentation.sourceBounds.top);
        }
        TEST_METHOD(AdvancedPictureTrackingPreservesNativeEvidenceAndIdentityVetoes)
        {
            for(int fault=0;fault<7;++fault)
            {
                MotionSequence s;
                s.input.presentation.action=VerticalBarPresentationAction::TRANSLATE;
                s.input.translationDriftActive=true;
                s.input.previousOutward.verticalPresentationSeen=true;
                for(int i=0;i<12;++i)
                {
                    s.Target(264-i*4);
                    switch(fault)
                    {
                    case 0: s.input.retention.expansionStripsAvailable=false; break;
                    case 1: s.input.retention.analysisValid=false; break;
                    case 2: s.input.evidence.available=false; break;
                    case 3: s.input.retention.globalNearBlack=true; break;
                    case 4: s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                    case 5: s.input.compatiblePresentation=false; break;
                    case 6: s.input.trustedGeneration=99; break;
                    }
                    const auto native=s.input.evidence.trustedBounds;
                    const auto base=s.input.trustedGeometry;
                    s.Step(false);
                    Assert::IsFalse(s.motion.active);
                    Assert::AreEqual(native.top,s.input.evidence.trustedBounds.top);
                    Assert::AreEqual(base.top,s.input.trustedGeometry.top);
                }
            }
        }
        TEST_METHOD(ReversalAndBriefPauseKeepOnePresentationHold)
        {
            MotionSequence s; s.Ramp(20,4); Assert::IsTrue(s.motion.active);
            for (int i=0;i<3;++i) { s.Step(); Assert::IsTrue(s.motion.active); }
            const int top=s.input.outwardCandidate.top;
            for (int i=1;i<12;++i) { s.Target(top+i*4); s.Step(); Assert::IsTrue(s.motion.active); }
        }
    };
}

namespace Tests
{
    TEST_CLASS(MovingPictureNativePositionTests)
    {
        static void Native(MotionSequence& s)
        {
            s.input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            s.input.evidence.axisEvidence.vertical.scanComplete=true;
            s.input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
            s.input.evidence.top.trusted=s.input.evidence.bottom.trusted=true;
            s.input.retention.activePicture=s.input.evidence;
        }
        static void UnknownStrips(MotionSequence& s)
        {
            Native(s);
            for(auto* edge : {&s.input.retention.expandingTop,&s.input.retention.expandingBottom}) {
                edge->blackFraction=.4; edge->continuity=1; edge->lumaP90=60;
                edge->distributedSupportDepthRowsByQuartile.fill(0);
            }
            const auto proof=ConfirmOutwardPictureTransition({},s.input.trustedGeometry,
                s.input.outwardCandidate,s.input.retention,7,s.identity.acceptedSequence);
            Assert::IsFalse(proof.broadOpposingPicture || proof.nativeDistributedPicture,
                L"This interval must lack both pixel certificates; native distributed coverage alone missed the real regression.");
        }
        static void AssertHeldOutput(const MotionSequence& s)
        {
            Assert::IsTrue(s.motion.active);
            Assert::IsFalse(s.motion.awaitingPublication);
            auto admissionInput=s.input;
            auto transition=EvaluateTransitionAdmission(admissionInput);
            ConstrainMovingPictureTransition(s.motion,transition);
            Assert::IsTrue(transition.deferOutward && transition.observation.transitionDeferred);
            Input crop;
            crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
            crop.geometry=s.input.trustedGeometry;
            crop.rasterWidth=crop.geometry.rasterWidth; crop.rasterHeight=crop.geometry.rasterHeight;
            crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
            crop.frameSourceGeneration=crop.geometrySourceGeneration=7;
            crop.frameSourceSequence=s.input.sourceSequence;
            crop.framePresentationEpoch=13;
            crop.movingPictureTransition=HasCurrentMovingPictureTransition(s.motion,7,s.input.sourceSequence);
            crop.movingPictureHold={false,s.motion.base,7,s.input.sourceSequence,13};
            crop.currentVisibleBoundsAvailable=true;
            crop.currentVisibleBounds=s.input.outwardCandidate;
            crop.currentVisibleBase=crop.geometry;
            crop.currentVisibleSourceGeneration=7; crop.currentVisibleSourceSequence=s.input.sourceSequence;
            CropPresentationAdmissionState prior;
            prior.available=true; prior.sourceGeneration=7; prior.presentationEpoch=13;
            prior.trustedCrop=crop.geometry; prior.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            const auto shown=AdmitCropPresentation(prior,crop,Evaluate(crop),13);
            Assert::IsFalse(shown.blocked);
            Assert::IsTrue(shown.presentation.applyCrop && shown.presentation.owner==DecisionOwner::MOVING_PICTURE_HOLD);
            Assert::AreEqual(crop.geometry.top,shown.presentation.sourceBounds.top);
            Assert::AreEqual(crop.geometry.bottom,shown.presentation.sourceBounds.bottom);
            AspectLimitFillInput fill;
            fill.sourceBounds=shown.presentation.sourceBounds;
            fill.trustedContentAuthorityAccepted=true;
            fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;
            fill.widerAspectLimit=2.42; fill.screenAspect=2.35;
            const auto filled=EvaluateAspectLimitFill(fill);
            Assert::IsTrue(filled.applied);
            Assert::AreEqual(crop.geometry.top,filled.sourceBounds.top);
            Assert::AreEqual(crop.geometry.bottom,filled.sourceBounds.bottom);
        }
    public:
        TEST_METHOD(LongUncertifiedNativeRampAndProofResumeKeepOriginalScopeOutput)
        {
            for(int step : {2,4}) {
                MotionSequence s; Native(s); s.Ramp(12,4); AssertHeldOutput(s);
                const auto certified=s.motion.last;
                const auto broadTop=s.input.retention.expandingTop, broadBottom=s.input.retention.expandingBottom;
                for(int i=0;i<16;++i) {
                    s.Target(s.input.outwardCandidate.top-step); UnknownStrips(s); s.Step();
                    AssertHeldOutput(s);
                    Assert::AreEqual(0u,s.motion.quietSamples);
                    Assert::AreEqual(certified.top,s.motion.last.top,
                        L"Uncertified position tracking must not overwrite certified geometry.");
                }
                s.input.retention.expandingTop=broadTop; s.input.retention.expandingBottom=broadBottom;
                s.Target(s.input.outwardCandidate.top-step); Native(s); s.Step();
                AssertHeldOutput(s);
                Assert::AreEqual(s.input.outwardCandidate.top,s.motion.last.top);
            }
        }
        TEST_METHOD(UncertifiedNativePositionsNeverAcquireOrSettleMotion)
        {
            MotionSequence startup;
            for(int i=0;i<16;++i) {
                startup.Target(264-i*4); UnknownStrips(startup); startup.Step();
                Assert::IsFalse(startup.motion.active || startup.motion.awaitingPublication);
            }
            MotionSequence active; Native(active); active.Ramp(12,4);
            const auto top=active.input.retention.expandingTop, bottom=active.input.retention.expandingBottom;
            for(int i=0;i<16;++i) {
                active.Target(active.input.outwardCandidate.top-2); UnknownStrips(active); active.Step();
                AssertHeldOutput(active);
            }
            for(int i=0;i<30;++i) { UnknownStrips(active); active.Step(); AssertHeldOutput(active); Assert::AreEqual(0u,active.motion.quietSamples); }
            active.input.retention.expandingTop=top; active.input.retention.expandingBottom=bottom;
            for(int i=0;i<7;++i) {
                Native(active); active.Step();
                if(i<6) Assert::IsTrue(active.motion.active,L"Only fresh certified quiet samples may settle the episode.");
            }
            Assert::IsFalse(active.motion.active);
            Assert::IsTrue(active.motion.awaitingPublication);
        }
        TEST_METHOD(ActualAbruptJumpInsideUncertifiedIntervalStillExitsMotion)
        {
            MotionSequence s; Native(s); s.Ramp(12,4);
            for(int i=0;i<4;++i) { s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s); s.Step(); AssertHeldOutput(s); }
            s.Target(s.input.outwardCandidate.top-40); UnknownStrips(s); s.Step();
            Assert::IsFalse(s.motion.active);
            Assert::IsTrue(s.motion.awaitingPublication);
            auto decision=EvaluateTransitionAdmission(s.input); ConstrainMovingPictureTransition(s.motion,decision);
            // Waiting for actual publication is allowed; the motion-specific
            // veto must not mistake this genuine abrupt edge change for a ramp.
            Assert::IsFalse(s.motion.active);
        }
        TEST_METHOD(RepeatsGapsAndChangedContextsCannotBorrowPositionEvidence)
        {
            MotionSequence s; Native(s); s.Ramp(12,4);
            for(int i=0;i<14;++i) { s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s); s.Step(); AssertHeldOutput(s); }
            const auto previous=s.motion;
            auto replay=s.input; replay.sourceSequence=previous.identity.acceptedSequence;
            replay.evidence.trustedBounds.top-=40; replay.outwardCandidate=replay.evidence.trustedBounds;
            const auto repeated=ObserveMovingPictureTransition(previous,previous.identity,replay);
            Assert::IsTrue(repeated.active);
            Assert::AreEqual(previous.identity.acceptedSequence,repeated.identity.acceptedSequence);
            Assert::AreEqual(previous.last.top,repeated.last.top);
            ++s.identity.acceptedSequence; ++s.identity.sourceFrameNumber; s.identity.captureTimestamp+=417083;
            s.Target(s.input.outwardCandidate.top-4); UnknownStrips(s); s.Step();
            AssertHeldOutput(s); Assert::AreEqual(0u,s.motion.quietSamples);
            for(int context=0;context<5;++context) {
                auto id=s.identity; auto input=s.input; input.sourceSequence=id.acceptedSequence;
                if(context==0) ++id.transportGeneration;
                if(context==1) ++id.sourceFormatGeneration;
                if(context==2) ++id.analysisDomainKey;
                if(context==3) ++id.viewportGeneration;
                if(context==4) ++id.rendererGeneration;
                const auto reset=ObserveMovingPictureTransition(s.motion,id,input);
                Assert::IsFalse(reset.active || reset.awaitingPublication);
            }
        }
        TEST_METHOD(MissingNativeScanOrSourceGapInvalidatesPositionWithoutInventingAbruptEvidence)
        {
            for(int fault=0;fault<3;++fault) {
                MotionSequence s; s.Ramp(12,4);
                for(int i=0;i<14;++i) {
                    s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s); s.Step(); AssertHeldOutput(s);
                }
                s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s);
                if(fault==0) s.input.evidence.axisEvidence.vertical.scanComplete=false;
                if(fault==1) s.input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
                if(fault==2) {
                    ++s.identity.acceptedSequence; ++s.identity.sourceFrameNumber;
                    s.identity.captureTimestamp+=417083;
                }
                s.Step(); AssertHeldOutput(s);
                Assert::AreEqual(uint64_t(0),s.motion.observedNativeSourceSequence);
                // A large delta over the missing measurement is not proof of a
                // one-source jump. The next valid sample only re-establishes position.
                s.Target(s.input.outwardCandidate.top-40); UnknownStrips(s); s.Step();
                AssertHeldOutput(s);
                Assert::AreEqual(s.motion.identity.acceptedSequence,s.motion.observedNativeSourceSequence);
                Assert::AreEqual(0u,s.motion.quietSamples);
                // Once adjacent native measurements resume, a real abrupt jump
                // is detected using that fresh pair and exits the episode.
                s.Target(s.input.outwardCandidate.top-40); UnknownStrips(s); s.Step();
                Assert::IsFalse(s.motion.active);
                Assert::IsTrue(s.motion.awaitingPublication);
            }
        }
        TEST_METHOD(RealDarknessBoundaryAfterUncertifiedRampRequiresFreshNativePredecessor)
        {
            MotionSequence s; s.Ramp(12,4);
            const auto broadTop=s.input.retention.expandingTop, broadBottom=s.input.retention.expandingBottom;
            const auto certified=s.motion.last;
            for(int i=0;i<16;++i) {
                s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s); s.Step(); AssertHeldOutput(s);
            }
            Assert::AreEqual(certified.top,s.motion.last.top);
            Assert::IsTrue(certified.top-s.motion.observedNative.top>21);
            const auto prior=s.motion;
            const uint64_t seq=s.identity.acceptedSequence;
            SceneDetector detector;
            const auto level=[&](uint16_t luma,uint64_t sequence) {
                const std::vector<uint16_t> pixels(64*36,uint16_t(luma<<6));
                const SceneDetectorInput scene={pixels.data(),64,36,64*sizeof(uint16_t),
                    sequence,int64_t(sequence*417083),7,417083,true};
                return detector.Analyze(scene);
            };
            level(120,seq-3); level(108,seq-2); level(100,seq-1);
            const auto boundary=level(96,seq);
            Assert::IsTrue(boundary.safeBoundary && boundary.nearBlackEntry && boundary.differenceEvaluated);
            Assert::IsFalse(boundary.hardCutCandidate || boundary.hardCutConfirmed);
            s.Target(prior.observedNative.top-2); Native(s);
            s.input.retention.expandingTop=broadTop; s.input.retention.expandingBottom=broadBottom;
            s.input.sourceSequence=seq;
            s.motion=RetainMovingPictureAtDarknessBoundary(prior,boundary,s.identity,s.input);
            Assert::IsTrue(s.motion.active);
            s.Step(); AssertHeldOutput(s);
            Assert::AreEqual(s.input.outwardCandidate.top,s.motion.last.top);
            for(int fault=0;fault<4;++fault) {
                auto bad=prior; auto input=s.input;
                if(fault==0) bad.observedNativeSourceSequence=0;
                if(fault==1) --bad.observedNativeSourceSequence;
                if(fault==2) bad.observedNative={};
                if(fault==3) {
                    input.evidence.trustedBounds.top-=40; input.evidence.trustedBounds.bottom+=40;
                    input.evidence.proposedBounds=input.outwardCandidate=input.evidence.trustedBounds;
                    input.retention.expansionCandidate=input.outwardCandidate;
                    input.retention.activePicture=input.evidence;
                }
                const auto rejected=RetainMovingPictureAtDarknessBoundary(bad,boundary,s.motion.identity,input);
                Assert::IsFalse(rejected.active,L"A missing predecessor or real jump cannot borrow the old gradual episode.");
            }
        }
        // Logged bounds and scene summary, with explicitly synthetic adjacent
        // identities/native proof. This diagnoses gates; it does not claim to
        // reproduce the unrecorded proof inputs of live frame 4485.
        static SceneDetectorResult DiagnosticBoundaryFixture(MotionSequence& s,
            int baseTop, int baseBottom, bool asymmetricStep=false)
        {
            s.input.trustedGeometry.top=baseTop; s.input.trustedGeometry.bottom=baseBottom;
            s.input.presentationBeforeObservation=s.input.retention.expansionBase=s.input.trustedGeometry;
            s.Target(124);
            s.input.evidence.trustedBounds.bottom=2038;
            s.input.evidence.proposedBounds=s.input.outwardCandidate=
                s.input.retention.expansionCandidate=s.input.evidence.trustedBounds;
            s.input.evidence.proposedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
            s.identity.acceptedSequence=4485; s.identity.sourceFrameNumber=7001;
            s.identity.captureTimestamp=7001*417083ULL;
            s.input.sourceSequence=s.identity.acceptedSequence;
            s.motion.active=true; s.motion.base=s.input.trustedGeometry;
            s.motion.identity=s.identity; --s.motion.identity.acceptedSequence;
            --s.motion.identity.sourceFrameNumber; s.motion.identity.captureTimestamp-=417083;
            s.motion.observedNative=s.input.evidence.trustedBounds;
            s.motion.observedNative.top=126;
            s.motion.observedNative.bottom=asymmetricStep?2038:2036;
            s.motion.observedNativeSourceSequence=s.motion.identity.acceptedSequence;
            s.motion.last=s.motion.observedNative;
            s.motion.directionalChanges=12; s.motion.quietSamples=3;
            SceneDetectorResult boundary;
            boundary.safeBoundary=boundary.nearBlackEntry=boundary.differenceEvaluated=true;
            boundary.sampleCount=576; boundary.generation=s.identity.transportGeneration;
            boundary.sourceSequence=s.identity.acceptedSequence;
            return boundary;
        }
        TEST_METHOD(DarknessBoundaryDiagnosticsAcceptBothLoggedBasesWithoutChangingDecision)
        {
            for(bool secondBase : {false,true}) for(bool asymmetric : {false,true}) {
                MotionSequence s;
                const auto boundary=DiagnosticBoundaryFixture(s,secondBase?274:270,secondBase?1886:1888,asymmetric);
                GradualPictureBoundaryRetentionDiagnostics diagnostic;
                const auto plain=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input);
                const auto traced=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input,&diagnostic);
                Assert::IsTrue(plain.active && traced.active);
                Assert::AreEqual("retained",diagnostic.reason);
                Assert::IsTrue(diagnostic.stepsEvaluated && diagnostic.pixelProofEvaluated && diagnostic.broadOpposingPicture);
                Assert::AreEqual(2,diagnostic.topStep);
                Assert::AreEqual(asymmetric?0:2,diagnostic.bottomStep);
                Assert::AreEqual(4,diagnostic.scanStep);
                Assert::AreEqual(4484ULL,diagnostic.previousSequence);
                Assert::AreEqual(4484ULL,diagnostic.observedNativeSequence);
                Assert::AreEqual(4485ULL,diagnostic.currentSequence);
                Assert::AreEqual(126,diagnostic.previousNative.top);
                Assert::AreEqual(2038,diagnostic.currentNative.bottom);
                Assert::AreEqual(plain.identity.acceptedSequence,traced.identity.acceptedSequence);
                Assert::AreEqual(plain.quietSamples,traced.quietSamples);
                Assert::AreEqual(plain.directionalChanges,traced.directionalChanges);
                Assert::AreEqual(plain.quietAnchor.top,traced.quietAnchor.top);
                Assert::AreEqual(s.motion.base.top,traced.base.top);
            }
        }
        TEST_METHOD(DarknessBoundaryDiagnosticsIdentifyRejectedGuardWithoutRelaxingIt)
        {
            for(int fault=0;fault<28;++fault) {
                MotionSequence s; auto boundary=DiagnosticBoundaryFixture(s,274,1886);
                const char* expected="";
                switch(fault) {
                case 0: s.motion.active=false; expected="inactive-episode"; break;
                case 1: boundary.hardCutCandidate=true; expected="boundary-kind"; break;
                case 2: ++boundary.sourceSequence; expected="boundary-identity"; break;
                case 3: ++s.identity.viewportGeneration; expected="source-context"; break;
                case 4: --s.motion.identity.acceptedSequence; expected="nonadjacent-source"; break;
                case 5: s.input.compatiblePresentation=false; expected="presentation-unavailable"; break;
                case 6: ++s.input.sourceSequence; expected="measurement-identity"; break;
                case 7: s.input.retention.globalNearBlack=true; expected="measurement-unavailable-or-dark"; break;
                case 8: s.input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
                    expected="native-authority"; break;
                case 9: s.motion.base.top=0; expected="invalid-base"; break;
                case 10: s.motion.observedNative={}; expected="invalid-native-predecessor"; break;
                case 11: s.motion.observedNativeSourceSequence=0; expected="native-predecessor-sequence"; break;
                case 12: --s.input.evidence.trustedBounds.top; expected="invalid-native-geometry"; break;
                case 13: s.input.trustedGeometry.top-=2; expected="logical-base-mismatch"; break;
                case 14: s.input.presentationBeforeObservation.top-=2; expected="presentation-base-mismatch"; break;
                case 15: s.input.outwardCandidate.top-=2; expected="outward-candidate-mismatch"; break;
                case 16: s.input.evidence.proposedBounds.bottom+=2; expected="raw-proposal-mismatch"; break;
                case 17: s.input.evidence.axisEvidence.vertical.scanComplete=false; expected="native-axis-authority"; break;
                case 18: s.motion.observedNative.rasterHeight+=2; expected="native-geometry"; break;
                case 19: s.motion.observedNative.top=122; expected="edge-step"; break; // 2px reversal is still rejected.
                case 20: s.motion.observedNative.top=130; expected="edge-step"; break;
                case 21: s.motion.observedNative=s.input.evidence.trustedBounds; expected="edge-step"; break;
                case 22: s.input.retention.expansionStripsAvailable=false; expected="strip-identity"; break;
                case 23: s.input.retention.expansionBase.top-=2; expected="strip-identity"; break;
                case 24: UnknownStrips(s); expected="pixel-proof"; break;
                case 25: s.identity.captureTimestamp=s.motion.identity.captureTimestamp; expected="nonadjacent-source"; break;
                case 26: s.motion.observedNativeSourceSequence--; expected="native-predecessor-sequence"; break;
                case 27: s.input.retention.expansionCandidate.bottom+=2; expected="strip-identity"; break;
                }
                GradualPictureBoundaryRetentionDiagnostics diagnostic;
                diagnostic.reason="stale-diagnostic"; diagnostic.pixelProofEvaluated=true;
                const auto plain=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input);
                const auto traced=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input,&diagnostic);
                Assert::IsFalse(plain.active || plain.awaitingPublication);
                Assert::IsFalse(traced.active || traced.awaitingPublication);
                Assert::AreEqual(expected,diagnostic.reason);
                Assert::AreEqual(fault==24,diagnostic.pixelProofEvaluated);
                if(fault==24) {
                    Assert::IsFalse(diagnostic.broadOpposingPicture || diagnostic.nativeDistributedPicture);
                    Assert::AreEqual("vertical-not-broad",diagnostic.pixelReason);
                }
            }
        }
        static SceneDetectorResult LoggedDarknessBoundaryFixture(MotionSequence& s)
        {
            auto boundary=DiagnosticBoundaryFixture(s,274,1886);
            s.identity={2,743,1224,963343347730ULL,16737397427277638757ULL,0,2,3440028774926749422ULL};
            s.motion.identity=s.identity; s.motion.identity.acceptedSequence=742;
            s.motion.identity.sourceFrameNumber=1223; s.motion.identity.captureTimestamp=963343306060ULL;
            s.motion.observedNativeSourceSequence=742;
            s.input.sourceSequence=743; s.input.sourceGeneration=s.input.trustedGeneration=2;
            s.input.presentationEvidenceGeneration=2;
            boundary.eventId=4; boundary.generation=2; boundary.sourceSequence=743;
            boundary.immediateAverageLumaDifference=7; boundary.changedSampleCount=26;
            boundary.histogramDistance=18;
            auto& top=s.input.retention.expandingTop;
            top.barPixels=150; top.blackFraction=.347222; top.lumaP90=45; top.lumaFloor=0;
            top.lumaDispersion=26; top.texture=6.206; top.continuity=0; top.neutralChromaFraction=1;
            top.innerBoundaryContrast=.809; top.confidence=.309223; top.trusted=false;
            top.distributedSupportDepthRowsByQuartile={32,63,63,63};
            auto& bottom=s.input.retention.expandingBottom;
            bottom.barPixels=152; bottom.blackFraction=.222222; bottom.lumaP90=376; bottom.lumaFloor=0;
            bottom.lumaDispersion=356; bottom.texture=45.078; bottom.continuity=0; bottom.neutralChromaFraction=.993056;
            bottom.innerBoundaryContrast=-6.115; bottom.confidence=.265278; bottom.trusted=false;
            bottom.distributedSupportDepthRowsByQuartile={63,63,63,63};
            s.input.retention.activePicture=s.input.evidence;
            return boundary;
        }
        TEST_METHOD(LoggedMandalorianDarknessBoundaryKeepsAlreadyActiveScopeWithoutNewAuthority)
        {
            MotionSequence s; const auto boundary=LoggedDarknessBoundaryFixture(s);
            const auto proof=ConfirmOutwardPictureTransition({},s.input.trustedGeometry,s.input.outwardCandidate,
                s.input.retention,s.input.sourceGeneration,s.input.sourceSequence);
            Assert::IsFalse(proof.broadOpposingPicture || proof.nativeDistributedPicture);
            Assert::AreEqual("vertical-not-broad",proof.diagnosticReason);
            const auto prior=s.motion;
            GradualPictureBoundaryRetentionDiagnostics diagnostic;
            s.motion=RetainMovingPictureAtDarknessBoundary(prior,boundary,s.identity,s.input,&diagnostic);
            Assert::IsTrue(s.motion.active,
                L"Logged frame743 has exact adjacent native motion and one sparse depth quartile; keep only its established scope episode.");
            Assert::IsFalse(s.motion.awaitingPublication);
            Assert::AreEqual("retained-native-continuation",diagnostic.reason);
            Assert::AreEqual("vertical-not-broad",diagnostic.pixelReason);
            Assert::IsTrue(diagnostic.continuationDistributedPicture);
            Assert::IsFalse(diagnostic.broadOpposingPicture || diagnostic.nativeDistributedPicture);
            Assert::AreEqual(prior.base.top,s.motion.base.top);
            Assert::AreEqual(prior.base.bottom,s.motion.base.bottom);
            Assert::AreEqual(prior.last.top,s.motion.last.top);
            Assert::AreEqual(0u,s.motion.quietSamples);
            Assert::AreEqual(0u,s.motion.directionalChanges);
            // Current weak strip evidence still grants no new endpoint/geometry.
            s.motion=ObserveMovingPictureTransition(s.motion,s.identity,s.input);
            const auto transition=EvaluateTransitionAdmission(s.input);
            Assert::IsFalse(transition.outward.authoritative);
            Assert::IsTrue(s.motion.active);
        }
        TEST_METHOD(LoggedBoundaryRetainsOneScopePoseUntilCurrentFullRasterAuthority)
        {
            MotionSequence s; const auto boundary=LoggedDarknessBoundaryFixture(s);
            s.motion=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input);
            Assert::IsTrue(s.motion.active,L"The logged darkness boundary must not drop the established motion episode.");
            const auto base=s.input.trustedGeometry;
            CropPresentationAdmissionState admitted;
            admitted.available=true; admitted.sourceGeneration=2; admitted.presentationEpoch=0;
            admitted.trustedCrop=base; admitted.authorityOrigin=ActivePictureAuthorityOrigin::NATIVE;
            int changes=0, lastTop=base.top, lastBottom=base.bottom;
            const auto present=[&](bool full) {
                Input crop;
                crop.automaticCropEnabled=crop.sharedGeometryAvailable=true;
                crop.geometry=base; crop.rasterWidth=3840; crop.rasterHeight=2160;
                crop.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.latestObservationClassification=full?ActivePictureClassification::FULL_RASTER_TRUSTED:
                    ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.frameSourceGeneration=crop.geometrySourceGeneration=2;
                crop.frameSourceSequence=s.input.sourceSequence; crop.framePresentationEpoch=0;
                crop.movingPictureTransition=HasCurrentMovingPictureTransition(s.motion,2,s.input.sourceSequence);
                crop.movingPictureHold={false,base,2,s.input.sourceSequence,0};
                crop.fullRasterPresentationAuthoritative=full;
                crop.currentVisibleBoundsAvailable=true; crop.currentVisibleBounds=s.input.outwardCandidate;
                crop.currentVisibleBase=base; crop.currentVisibleSourceGeneration=2;
                crop.currentVisibleSourceSequence=s.input.sourceSequence;
                const bool routed=RouteAdvancedMovingPictureHold(crop,SubtitlePreviewMode::GeneratedGrayBackground);
                Assert::AreEqual(!full,routed);
                const auto result=AdmitCropPresentation(admitted,crop,Evaluate(crop),0);
                Assert::IsFalse(result.blocked);
                admitted=result.state;
                const auto shown=SubtitlePresentationBounds(result.presentation,
                    SubtitlePreviewMode::GeneratedGrayBackground,false,false);
                if(shown.top!=lastTop || shown.bottom!=lastBottom)++changes;
                lastTop=shown.top;lastBottom=shown.bottom;
                Assert::AreEqual(full?0:base.top,shown.top);
                Assert::AreEqual(full?2160:base.bottom,shown.bottom);
            };
            s.Step(); present(false);
            for(int top=122;top>=2;top-=2) {
                s.Target(top); s.input.retention.activePicture=s.input.evidence;
                s.Step(); Assert::IsTrue(s.motion.active); present(false);
            }
            Assert::AreEqual(0,changes,L"A dark strip cannot cause intermediate display sizes during the remaining ramp.");
            s.Target(0); s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            s.input.retention.activePicture=s.input.evidence;
            s.Step(); Assert::IsFalse(s.motion.active || s.motion.awaitingPublication);
            present(true);
            Assert::AreEqual(1,changes,L"Current full-raster authority ends the hold in one display change.");
        }
        TEST_METHOD(NativeBoundaryContinuationRejectsSparseCaptionStaleProofAndCleanCuts)
        {
            for(int fault=0;fault<30;++fault) {
                MotionSequence s; auto boundary=LoggedDarknessBoundaryFixture(s);
                auto& top=s.input.retention.expandingTop;
                auto& bottom=s.input.retention.expandingBottom;
                auto& native=s.input.retention.activePicture;
                switch(fault) {
                case 0: top.distributedSupportDepthRowsByQuartile.fill(0); break;
                case 1: top.distributedSupportDepthRowsByQuartile.fill(32); break; // One full-width caption row.
                case 2: top.distributedSupportDepthRowsByQuartile={0,63,63,63}; break; // Missing quarter.
                case 3: top.distributedSupportDepthRowsByQuartile={32,3,63,63}; break;
                case 4: bottom.distributedSupportDepthRowsByQuartile={32,63,63,63}; break;
                case 5: top.blackFraction=.500001; break;
                case 6: bottom.continuity=.850001; break;
                case 7: top.blackFraction=std::numeric_limits<double>::quiet_NaN(); break;
                case 8: top.continuity=-.01; break;
                case 9: top.distributedSupportDepthRowsByQuartile[0]=128; break;
                case 10: native.available=false; break;
                case 11: native.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN; break;
                case 12: native.trustedBounds.top-=2; break;
                case 13: native.proposedBounds.bottom+=2; break;
                case 14: native.trustedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE; break;
                case 15: native.axisEvidence.vertical.scanComplete=false; break;
                case 16: native.top.trusted=false; break;
                case 17: s.input.evidence.top.trusted=false; break;
                case 18: --s.motion.identity.acceptedSequence; break;
                case 19: boundary.hardCutConfirmed=true; break;
                case 20: s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED; break;
                case 21: s.input.retention.globalNearBlack=true; break;
                case 22: --top.barPixels; break;
                case 23: top.blackFraction=bottom.blackFraction=1; break;
                case 24: top.distributedSupportDepthRowsByQuartile={1,0,0,0}; break; // Isolated ink.
                case 25: native.classification=ActivePictureClassification::PROVISIONAL; break;
                case 26: s.motion.active=false; break;
                case 27: s.motion.observedNative.top=130; break;
                case 28: s.motion.observedNative.top=122; break;
                case 29: boundary.differenceEvaluated=false; break;
                }
                GradualPictureBoundaryRetentionDiagnostics diagnostic;
                const auto held=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input,&diagnostic);
                Assert::IsFalse(held.active || held.awaitingPublication);
                Assert::IsFalse(diagnostic.continuationDistributedPicture);
            }
        }
        TEST_METHOD(CleanNativeChangeAtDarknessBoundaryUsesOrdinaryAdmissionWithoutRampDwell)
        {
            for(bool hardCut : {false,true}) {
                MotionSequence s; auto boundary=LoggedDarknessBoundaryFixture(s);
                boundary.hardCutConfirmed=hardCut;
                if(!hardCut) {
                    s.Target(68); // Clear immediate geometry jump, not a gradual step.
                    s.input.retention.activePicture=s.input.evidence;
                }
                const auto retained=RetainMovingPictureAtDarknessBoundary(s.motion,boundary,s.identity,s.input);
                Assert::IsFalse(retained.active || retained.awaitingPublication);
                s.motion=ObserveMovingPictureTransition(retained,s.identity,s.input);
                Assert::IsFalse(s.motion.active || s.motion.awaitingPublication);
                auto admission=EvaluateTransitionAdmission(s.input);
                const auto ordinary=admission;
                ConstrainMovingPictureTransition(s.motion,admission);
                Assert::AreEqual(ordinary.deferOutward,admission.deferOutward);
                Assert::AreEqual(ordinary.deferPresentation,admission.deferPresentation);
                Assert::AreEqual(ordinary.observation.transitionDeferred,admission.observation.transitionDeferred);
                Assert::AreEqual(0u,s.motion.quietSamples);
                // Once the unchanged native model publishes the clean geometry,
                // the presentation policy applies it directly, with no new dwell.
                Input crop;
                crop.automaticCropEnabled=crop.sharedGeometryAvailable=crop.latestObservationSupportsCrop=true;
                crop.geometry=s.input.evidence.trustedBounds;
                crop.rasterWidth=3840;crop.rasterHeight=2160;
                crop.classification=crop.latestObservationClassification=ActivePictureClassification::BAR_CROP_TRUSTED;
                crop.frameSourceGeneration=crop.geometrySourceGeneration=2;
                crop.frameSourceSequence=743;crop.frameLocalPresentationRetentionEvaluated=crop.frameLocalPresentationRetentionSafe=true;
                const auto shown=Evaluate(crop);
                Assert::IsTrue(shown.applyCrop);
                Assert::AreEqual(crop.geometry.top,shown.sourceBounds.top);
                Assert::AreEqual(crop.geometry.bottom,shown.sourceBounds.bottom);
            }
        }
        TEST_METHOD(ProgressiveOnsetBlocksQueuedIntermediatePublicationAcrossTwoHistoryLoops)
        {
            for(bool futureReady : {false,true})
            {
            ActivePictureTransitionModel model;
            uint64_t sequence=2000;
            const auto bounds=[](int top) { return ActivePictureBounds{0,top,3840,2160-top,3840,2160,
                3840.0/(2160-2*top),top?ActivePictureBounds::BarAxes::TOP_BOTTOM:ActivePictureBounds::BarAxes::NONE}; };
            // This is a reconstructed onset with logged geometry/proof gaps, not
            // captured pixels. Keep one real model/history alive across both loops.
            for(int top : {248,0,274}) for(int i=0;i<24;++i)
                model.Observe({bounds(top),sequence++,true,top?ActivePictureClassification::BAR_CROP_TRUSTED:
                    ActivePictureClassification::FULL_RASTER_TRUSTED,24});
            unsigned blockedReadyWindows=0;
            for(int loop=0;loop<2;++loop)
            {
                for(int i=0;i<8;++i) model.Observe({bounds(0),sequence++,true,ActivePictureClassification::FULL_RASTER_TRUSTED,24});
                model.ResetCandidateEvidence();
                auto logical=bounds(274);
                for(int i=0;i<24;++i) { const auto d=model.Observe({logical,sequence++,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
                    if(d.publish)logical=d.bounds; }
                MovingPictureTransitionState motion;
                OutwardPictureConfirmationState outward;
                const int tops[]={256,255,254,252,250,248,247,244,242,240,238,236,234,232,230,228};
                unsigned endpointPublications=0;
                for(int i=0;i<36;++i,++sequence)
                {
                    const bool missing=i==1||i==6;
                    MotionSequence sample;
                    sample.input.trustedGeometry=sample.input.presentationBeforeObservation=logical;
                    sample.input.retention.expansionBase=logical;
                    const int top=i<16?tops[i]:228;
                    sample.Target(top);
                    sample.input.evidence.classification=missing?ActivePictureClassification::PROVISIONAL:ActivePictureClassification::BAR_CROP_TRUSTED;
                    sample.input.retention.expansionStripsAvailable=!missing;
                    sample.input.retention.activePicture=sample.input.evidence;
                    sample.input.retention.expandingTop.barPixels=sample.input.retention.expandingBottom.barPixels=logical.top-top;
                    sample.input.sourceSequence=sequence;
                    sample.identity.acceptedSequence=sample.identity.sourceFrameNumber=sequence;
                    sample.identity.captureTimestamp=sequence*417083;
                    sample.input.previousOutward=outward;
                    auto admission=EvaluateTransitionAdmission(sample.input);
                    motion=ObserveMovingPictureTransition(motion,sample.identity,sample.input);
                    ConstrainMovingPictureTransition(motion,admission);
                    BufferedPictureExpansionSample window[3];
                    for(int n=0;n<3;++n) {
                        auto& w=window[n];w.identity=sample.identity;w.identity.acceptedSequence+=n;w.identity.sourceFrameNumber+=n;w.identity.captureTimestamp+=n*417083;
                        w.observation=MakeActivePictureObservation(sample.input.evidence,sequence+n,24);w.observation.bounds=bounds(i<16?top-2*n:top);
                        w.retention=sample.input.retention;w.retention.expansionCandidate=w.observation.bounds;
                        w.retention.activePicture=sample.input.evidence;
                        w.retention.activePicture.trustedBounds=w.retention.activePicture.proposedBounds=w.observation.bounds;
                        w.retention.expandingTop.barPixels=w.retention.expandingBottom.barPixels=logical.top-w.observation.bounds.top;
                        w.nearBlackEvaluated=true;
                    }
                    const auto proof=BuildBufferedPictureExpansion(window,futureReady?3:1,logical,2,futureReady?2:0,11,12);
                    const bool proofReady=ValidateBufferedPictureExpansion(proof,sample.identity,sample.input,
                        model.WouldAdmitGeometryChange(MakeActivePictureObservation(sample.input.evidence,sequence,24)));
                    const bool onset=HasCurrentMovingPictureOnset(motion,7,sequence);
                    const bool queuedOnset=!motion.active&&!motion.awaitingPublication&&
                        ConstrainBufferedMovingPictureOnset(proof,sample.identity,sample.input,admission);
                    if((onset||queuedOnset)&&proofReady)++blockedReadyWindows;
                    const bool ready=!motion.active&&!onset&&!queuedOnset&&proofReady;
                    if(ready) {
                        admission.deferOutward=admission.deferPresentation=false;
                        admission.observation=MakeActivePictureObservation(sample.input.evidence,sequence,24);
                    }
                    const bool adopted=ready&&model.AdoptPublishedDecision(proof.decision.transition,
                        sample.input.evidence.classification,admission.observation.transitionDeferred,nullptr,
                        &sample.input.evidence.axisEvidence,true);
                    const auto decision=adopted?proof.decision.transition:model.Observe(admission.observation);
                    outward=admission.outward.state;
                    CompleteMovingPictureTransition(motion,sample.input,decision);
                    if(decision.publish) { logical=decision.bounds;if(i>=16)++endpointPublications; }
                    if(i<16)Assert::AreEqual(274,logical.top,L"A coherent moving preview must not win the race before motion acquisition.");
                    if(missing)Assert::IsFalse(onset||queuedOnset,L"Unknown pixels cannot renew current onset interest.");
                }
                Assert::IsFalse(motion.active||motion.awaitingPublication);
                Assert::AreEqual(228,logical.top);
                Assert::AreEqual(1u,endpointPublications,L"A stable endpoint must publish once, through ordinary authority.");
            }
            if(futureReady)Assert::IsTrue(blockedReadyWindows>=2,L"The test must exercise actual valid queued proofs in both history loops.");
            else Assert::AreEqual(0u,blockedReadyWindows);
            }
        }
        TEST_METHOD(QueuedOnsetRequiresCompleteProgressiveNativeWindowAndExactCurrentSource)
        {
            for(int fault=0;fault<32;++fault)
            {
                MotionSequence s;s.Target(244);s.input.sourceSequence=s.identity.acceptedSequence;
                s.input.retention.activePicture=s.input.evidence;
                BufferedPictureExpansionSample window[3];
                for(int n=0;n<3;++n) {
                    auto future=s;future.Target(244-2*n);
                    auto&w=window[n];w.identity=s.identity;w.identity.acceptedSequence+=n;w.identity.sourceFrameNumber+=n;w.identity.captureTimestamp+=n*417083;
                    w.observation=MakeActivePictureObservation(future.input.evidence,s.identity.acceptedSequence+n,24);
                    w.retention=future.input.retention;w.retention.activePicture=future.input.evidence;w.nearBlackEvaluated=true;
                }
                if(fault>=29)for(int n=0;n<3;++n) {
                    auto& w=window[n];
                    if(fault==29)w.observation.bounds.bottom=window[0].observation.bounds.bottom;
                    if(fault==30)w.observation.bounds.top=window[0].observation.bounds.top;
                    if(fault==31) { w.observation.bounds.top=244-(n?2:0);w.observation.bounds.bottom=1916+(n==2?2:0); }
                    w.observation.bounds.aspectRatio=3840.0/(w.observation.bounds.bottom-w.observation.bounds.top);
                    w.retention.expansionCandidate=w.observation.bounds;
                    w.retention.activePicture.trustedBounds=w.retention.activePicture.proposedBounds=w.observation.bounds;
                    w.retention.expandingTop.barPixels=s.input.trustedGeometry.top-w.observation.bounds.top;
                    w.retention.expandingBottom.barPixels=w.observation.bounds.bottom-s.input.trustedGeometry.bottom;
                }
                size_t count=3;
                switch(fault) {
                case 1:++window[1].identity.acceptedSequence;break;
                case 2:++window[1].identity.sourceFrameNumber;break;
                case 3:window[1].identity.captureTimestamp=window[0].identity.captureTimestamp;break;
                case 4:++window[1].identity.analysisDomainKey;break;
                case 5:window[1].observation.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;break;
                case 6:window[1].retention.activePicture.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;break;
                case 7:++window[1].retention.activePicture.trustedBounds.top;break;
                case 8:++window[1].retention.activePicture.proposedBounds.top;break;
                case 9:window[1].retention.activePicture.available=false;break;
                case 10:window[1].observation.classification=ActivePictureClassification::PROVISIONAL;break;
                case 11:window[1].retention.activePicture.axisEvidence.vertical.scanComplete=false;break;
                case 12:window[1].retention.activePicture.top.trusted=false;break;
                case 13:count=2;break;
                case 14:window[1].retention.expansionStripsAvailable=false;break;
                case 15:window[1].retention.globalNearBlack=true;break;
                }
                const auto proof=BuildBufferedPictureExpansion(window,count,s.input.trustedGeometry,2,2,11,12);
                auto identity=s.identity;
                switch(fault) {
                case 16:++identity.transportGeneration;break;
                case 17:++identity.acceptedSequence;break;
                case 18:++identity.sourceFrameNumber;break;
                case 19:++identity.captureTimestamp;break;
                case 20:++identity.analysisDomainKey;break;
                case 21:++s.input.sourceGeneration;break;
                case 22:s.input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;break;
                case 23:++s.input.evidence.trustedBounds.top;break;
                case 24:++s.input.trustedGeometry.top;break;
                case 25:++s.input.retention.activePicture.trustedBounds.top;break;
                case 26:s.input.retention.activePicture.axisEvidence.vertical.scanComplete=false;break;
                case 27:s.input.retention.globalNearBlack=true;break;
                case 28:s.input.retention.expansionStripsAvailable=false;break;
                }
                auto admission=EvaluateTransitionAdmission(s.input);const auto original=admission;
                const bool constrained=ConstrainBufferedMovingPictureOnset(proof,identity,s.input,admission);
                Assert::AreEqual(fault==0||fault==31,constrained);
                if(constrained)Assert::IsTrue(admission.observation.transitionDeferred);
                else {
                    Assert::AreEqual(original.observation.transitionDeferred,admission.observation.transitionDeferred);
                    Assert::AreEqual(original.deferOutward,admission.deferOutward);
                }
            }
        }
        TEST_METHOD(OnsetIgnoresOneEdgeJitterButAllowsAlternatingOpposingQuantization)
        {
            for(int height : {1080,2160})
            {
                const auto set=[](MotionSequence& s,int top,int bottom) {
                    s.Target(top);auto b=s.input.evidence.trustedBounds;b.bottom=bottom;
                    b.aspectRatio=double(b.rasterWidth)/(b.bottom-b.top);
                    s.input.evidence.trustedBounds=s.input.evidence.proposedBounds=s.input.outwardCandidate=b;
                    s.input.retention.expansionCandidate=b;
                };
                for(int jitter=0;jitter<3;++jitter) {
                    MotionSequence s(height);const int top=height/32/2*2,bottom=height-top;
                    for(int n=0;n<6;++n) {
                        set(s,top+(jitter!=0&&(n&1)?2:0),bottom-(jitter!=1&&(n&1)?2:0));s.Step();
                        Assert::IsFalse(s.motion.onsetPending||s.motion.active,
                            L"One-edge or inward-return scan jitter must not become gradual opposing-picture onset.");
                    }
                }
                MotionSequence s(height);const int top=s.input.trustedGeometry.top-16,bottom=height-top;
                set(s,top,bottom);s.Step();
                set(s,top-2,bottom);s.Step();Assert::IsFalse(s.motion.onsetPending);
                set(s,top-2,bottom+2);s.Step();Assert::IsTrue(s.motion.onsetPending);
                Assert::IsFalse(s.motion.active||s.motion.awaitingPublication);
            }
        }
        TEST_METHOD(StationaryQueuedNativeChangeKeepsImmediateOrdinaryAdoption)
        {
            for(int top : {244,68})
            {
                MotionSequence s;s.Target(top);s.input.sourceSequence=s.identity.acceptedSequence;
                s.input.retention.activePicture=s.input.evidence;
                BufferedPictureExpansionSample window[3];
                for(int n=0;n<3;++n) {
                    auto&w=window[n];w.identity=s.identity;w.identity.acceptedSequence+=n;w.identity.sourceFrameNumber+=n;w.identity.captureTimestamp+=n*417083;
                    w.observation=MakeActivePictureObservation(s.input.evidence,s.identity.acceptedSequence+n,24);
                    w.retention=s.input.retention;w.nearBlackEvaluated=true;
                }
                const auto proof=BuildBufferedPictureExpansion(window,3,s.input.trustedGeometry,2,2,11,12);
                Assert::IsTrue(proof.valid);Assert::IsFalse(proof.progressiveNativeEdges);
                auto admission=EvaluateTransitionAdmission(s.input);
                Assert::IsFalse(ConstrainBufferedMovingPictureOnset(proof,s.identity,s.input,admission));
                ActivePictureTransitionModel model;
                for(uint64_t sequence=1;sequence<40;++sequence)model.Observe({s.input.trustedGeometry,sequence,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
                Assert::IsTrue(ValidateBufferedPictureExpansion(proof,s.identity,s.input,
                    model.WouldAdmitGeometryChange(MakeActivePictureObservation(s.input.evidence,s.input.sourceSequence,24))));
                // The ordinary renderer clears its satisfied temporal deferral.
                admission.observation=MakeActivePictureObservation(s.input.evidence,s.input.sourceSequence,24);
                Assert::IsTrue(model.AdoptPublishedDecision(proof.decision.transition,s.input.evidence.classification,
                    admission.observation.transitionDeferred,nullptr,&s.input.evidence.axisEvidence,true));
            }
        }
        TEST_METHOD(CurrentOnsetExpiresWithoutRenewalOnStaticUnknownAbruptAndChangedContext)
        {
            for(int fault=0;fault<12;++fault)
            {
                MotionSequence s; s.Target(258);s.Step();s.Target(256);s.Step();s.Target(254);s.Step();
                Assert::IsTrue(HasCurrentMovingPictureOnset(s.motion,7,s.identity.acceptedSequence-1));
                Assert::IsFalse(s.motion.active||s.motion.awaitingPublication);
                Assert::IsFalse(HasCurrentMovingPictureOnset(s.motion,8,s.identity.acceptedSequence-1));
                Assert::IsFalse(HasCurrentMovingPictureOnset(s.motion,7,s.identity.acceptedSequence));
                s.Target(252);
                switch(fault) {
                case 0:s.Target(254);break; // Static: ordinary native confirmation resumes immediately.
                case 1:s.Target(68);break; // Clean abrupt change, no gradual waiting.
                case 2:s.Target(256);break;
                case 3:s.input.evidence.classification=ActivePictureClassification::PROVISIONAL;break;
                case 4:s.input.retention.expansionStripsAvailable=false;break;
                case 5:++s.identity.acceptedSequence;++s.identity.sourceFrameNumber;break;
                case 6:++s.identity.viewportGeneration;break;
                case 7:s.input.presentation.action=VerticalBarPresentationAction::FIT;break;
                case 8:s.input.retention.globalNearBlack=true;break;
                case 9:s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;break;
                case 10:s.input.evidence.authorityOrigin=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;break;
                case 11:s.motion={};break; // An explicit scene/cut reset does not preserve the interest.
                }
                s.Step();
                Assert::IsFalse(HasCurrentMovingPictureOnset(s.motion,7,s.identity.acceptedSequence-1));
                auto ordinary=EvaluateTransitionAdmission(s.input);const auto before=ordinary;
                ConstrainMovingPictureTransition(s.motion,ordinary);
                Assert::AreEqual(before.observation.transitionDeferred,ordinary.observation.transitionDeferred);
            }
        }
        TEST_METHOD(OnsetDoesNotDelayCleanStaticOrAbruptNativeModelPublication)
        {
            for(int target : {254,68,0})
            {
                MotionSequence s;s.Target(258);s.Step();s.Target(256);s.Step();s.Target(254);s.Step();
                Assert::IsTrue(s.motion.onsetPending);
                ActivePictureTransitionModel ordinaryModel;
                for(uint64_t n=1;n<40;++n)ordinaryModel.Observe({s.input.trustedGeometry,n,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
                auto onsetModel=ordinaryModel;
                OutwardPictureConfirmationState outward;
                unsigned ordinaryPublications=0;
                for(int n=0;n<20;++n)
                {
                    s.Target(target);
                    if(target==0) { s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
                        s.input.evidence.axisEvidence.vertical.state=ActivePictureAxisState::FULL_EXTENT_SUPPORTED; }
                    s.input.previousOutward=outward;s.Step();
                    Assert::IsFalse(s.motion.onsetPending||s.motion.active);
                    auto ordinary=EvaluateTransitionAdmission(s.input);auto guarded=ordinary;
                    ConstrainMovingPictureTransition(s.motion,guarded);
                    const auto expected=ordinaryModel.Observe(ordinary.observation);
                    const auto actual=onsetModel.Observe(guarded.observation);
                    outward=ordinary.outward.state;
                    Assert::AreEqual(expected.publish,actual.publish,L"Static and clean abrupt native changes retain ordinary publication timing.");
                    Assert::AreEqual(expected.bounds.top,actual.bounds.top);
                    Assert::AreEqual(expected.bounds.bottom,actual.bounds.bottom);
                    ordinaryPublications+=expected.publish?1:0;
                }
                // The small static shift may remain deliberately inside the native
                // deadband. The large crop jump and full raster must publish.
                if(target!=254)Assert::IsTrue(ordinaryPublications>0);
            }
        }
        TEST_METHOD(CleanOutwardReturnJitterKeepsOrdinaryPublicationTimingWithAndWithoutHistory)
        {
            for(int height : {1080,2160}) for(bool known : {false,true})
            {
                int first[2]={-1,-1};
                for(int guarded=0;guarded<2;++guarded)
                {
                    MotionSequence s(height);const auto initial=s.input.trustedGeometry;
                    const int target=height*68/2160;
                    const auto bounds=[&](int top) { auto b=initial;b.top=top;b.bottom=height-top;
                        b.aspectRatio=double(b.rasterWidth)/(b.bottom-b.top);
                        b.trustedBarAxes=top?ActivePictureBounds::BarAxes::TOP_BOTTOM:ActivePictureBounds::BarAxes::NONE;return b; };
                    ActivePictureTransitionModel model;uint64_t sequence=1;
                    if(known)for(int top : {target,0})for(int n=0;n<24;++n)
                        model.Observe({bounds(top),sequence++,true,top?ActivePictureClassification::BAR_CROP_TRUSTED:
                            ActivePictureClassification::FULL_RASTER_TRUSTED,24});
                    for(int n=0;n<24;++n)model.Observe({initial,sequence++,true,ActivePictureClassification::BAR_CROP_TRUSTED,24});
                    auto logical=initial;OutwardPictureConfirmationState outward;
                    for(int n=0;n<12;++n,++sequence)
                    {
                        s.input.trustedGeometry=s.input.presentationBeforeObservation=logical;
                        s.input.retention.expansionBase=logical;s.Target(target-(n&1?2:0));
                        s.input.retention.activePicture=s.input.evidence;
                        s.input.retention.expandingTop.barPixels=logical.top-s.input.outwardCandidate.top;
                        s.input.retention.expandingBottom.barPixels=s.input.outwardCandidate.bottom-logical.bottom;
                        s.input.previousOutward=outward;
                        s.identity.acceptedSequence=s.identity.sourceFrameNumber=sequence;s.identity.captureTimestamp=sequence*417083;
                        s.Step();auto admission=EvaluateTransitionAdmission(s.input);
                        if(guarded)ConstrainMovingPictureTransition(s.motion,admission);
                        const auto result=model.Observe(admission.observation);outward=admission.outward.state;
                        if(result.publish){logical=result.bounds;if(first[guarded]<0)first[guarded]=n;}
                    }
                }
                Assert::IsTrue(first[0]>=0,L"The ordinary clean change must actually publish.");
                Assert::AreEqual(first[0],first[1],L"Outward-return scan jitter must not starve ordinary clean-change publication.");
            }
        }
        TEST_METHOD(SecondProgressivePairStillPrecedesRememberedNativePublicationWithoutFuture)
        {
            MotionSequence s;ActivePictureTransitionModel model;uint64_t sequence=1;
            const auto initial=s.input.trustedGeometry;
            const auto bounds=[&](int top) {auto b=initial;b.top=top;b.bottom=2160-top;
                b.aspectRatio=3840.0/(b.bottom-b.top);b.trustedBarAxes=top?ActivePictureBounds::BarAxes::TOP_BOTTOM:
                    ActivePictureBounds::BarAxes::NONE;return b;};
            for(int top : {244,0,initial.top})for(int n=0;n<24;++n)
                model.Observe({bounds(top),sequence++,true,top?ActivePictureClassification::BAR_CROP_TRUSTED:
                    ActivePictureClassification::FULL_RASTER_TRUSTED,24});
            OutwardPictureConfirmationState outward;
            for(int n=0;n<12;++n,++sequence)
            {
                s.Target(244-2*n);s.input.retention.activePicture=s.input.evidence;
                s.input.retention.expandingTop.barPixels=s.input.retention.expandingBottom.barPixels=initial.top-s.input.outwardCandidate.top;
                s.input.previousOutward=outward;s.identity.acceptedSequence=s.identity.sourceFrameNumber=sequence;
                s.identity.captureTimestamp=sequence*417083;s.Step();
                auto admission=EvaluateTransitionAdmission(s.input);ConstrainMovingPictureTransition(s.motion,admission);
                const auto result=model.Observe(admission.observation);outward=admission.outward.state;
                Assert::IsFalse(result.publish,L"Ordinary outward confirmation must leave time for the second progressive pair before remembered publication.");
                if(n==1)Assert::IsFalse(s.motion.onsetPending);
                if(n==2)Assert::IsTrue(s.motion.onsetPending);
            }
        }
        TEST_METHOD(PreactiveNativeAbruptJumpResetsEarlierVotesAndSeedsLaterMotion)
        {
            for(int height : {1080,2160}) for(int axis=0;axis<3;++axis) for(bool abrupt : {false,true})
            {
                MotionSequence s(height);const int start=s.input.trustedGeometry.top-16;
                s.Target(start);s.Step();s.Target(start-2);s.Step();s.Target(start-4);s.Step();
                Assert::IsTrue(s.motion.onsetPending);
                const int limit=std::max(4,height/100),delta=(limit/2)*2+(abrupt?2:0);
                const auto set=[&](int top,int bottom) {
                    s.Target(top);auto b=s.input.outwardCandidate;b.bottom=bottom;
                    b.aspectRatio=double(b.rasterWidth)/(b.bottom-b.top);
                    s.input.evidence.trustedBounds=s.input.evidence.proposedBounds=s.input.outwardCandidate=b;
                    s.input.retention.expansionCandidate=b;s.input.retention.activePicture=s.input.evidence;
                    s.input.retention.expandingTop.barPixels=s.input.trustedGeometry.top-top;
                    s.input.retention.expandingBottom.barPixels=bottom-s.input.trustedGeometry.bottom;
                };
                const int top=start-4-(axis==1?2:delta),bottom=height-(start-4)+(axis==0?2:delta);
                set(top,bottom);s.Step();
                if(!abrupt)
                {
                    Assert::IsTrue(s.motion.change!=MovingPictureTransitionChange::ABRUPT_NATIVE,
                        L"The existing native jump threshold remains inclusive; supported larger scan steps are unchanged.");
                    continue;
                }
                Assert::IsTrue(s.motion.change==MovingPictureTransitionChange::ABRUPT_NATIVE);
                Assert::IsFalse(s.motion.active||s.motion.onsetPending||s.motion.awaitingPublication);
                Assert::AreEqual(0u,s.motion.directionalChanges);
                Assert::AreEqual(top,s.motion.anchor.top);Assert::AreEqual(bottom,s.motion.quietAnchor.bottom);
                Assert::AreEqual(s.identity.acceptedSequence-1,s.motion.observedNativeSourceSequence);
                Assert::AreEqual(top,s.motion.observedNative.top);
                set(top-2,bottom+2);s.Step();Assert::IsFalse(s.motion.onsetPending||s.motion.active);
                set(top-4,bottom+4);s.Step();Assert::IsTrue(s.motion.onsetPending);
                Assert::IsFalse(s.motion.active||s.motion.awaitingPublication);
            }
        }
        TEST_METHOD(FullRasterAuthorityAndNearBlackStillRetireUncertifiedHold)
        {
            for(bool full : {false,true}) {
                MotionSequence s; Native(s); s.Ramp(12,4);
                for(int i=0;i<8;++i) { s.Target(s.input.outwardCandidate.top-2); UnknownStrips(s); s.Step(); }
                if(full) s.input.evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
                else s.input.retention.globalNearBlack=true;
                s.Step(); Assert::IsFalse(s.motion.active || s.motion.awaitingPublication);
            }
        }
    };
}
