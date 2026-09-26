#include "pch.h"
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include "CppUnitTest.h"
#include <array>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace VideoProcessorTest
{
    TEST_CLASS(FullRasterLookaheadTests)
    {
        using Axes = ActivePictureBounds::BarAxes;
        using Sample = AlphaSourceCrop::BufferedPictureExpansionSample;
        static ActivePictureBounds Full() { return {0,0,3840,2160,3840,2160,16.0/9.0,Axes::NONE}; }
        static ActivePictureBounds Bars(bool sides=false) {
            return sides ? ActivePictureBounds{276,0,3564,2160,3840,2160,3288.0/2160,Axes::LEFT_RIGHT}
                : ActivePictureBounds{0,208,3840,1952,3840,2160,3840.0/1744,Axes::TOP_BOTTOM};
        }
        static ActivePictureObservation Observe(uint64_t n,const ActivePictureBounds& b) {
            ActivePictureObservation o; o.available=true; o.frameNumber=n; o.framesPerSecond=23.976; o.bounds=b;
            o.classification=b.trustedBarAxes==Axes::NONE ? ActivePictureClassification::FULL_RASTER_TRUSTED : ActivePictureClassification::BAR_CROP_TRUSTED;
            return o;
        }
        static ActivePictureTransitionModel Established(const ActivePictureBounds& b) {
            ActivePictureTransitionModel model;
            for (uint64_t i=1;i<=4;++i) model.Observe(Observe(i,b));
            return model;
        }
        static std::array<Sample,2> Samples(bool sides=false) {
            std::array<Sample,2> samples;
            for (uint64_t i=0;i<2;++i) {
                samples[i].identity={7,100+i,500+i,100000+i*1000,11,13,17};
                samples[i].observation=Observe(100+i,Bars(sides));
                samples[i].nearBlackEvaluated=true;
            }
            return samples;
        }
        static ActivePictureFrameDecision Build(const std::array<Sample,2>& samples,
            const ActivePictureBounds& base=Full(),uint8_t configured=5,uint8_t available=4) {
            return AlphaSourceCrop::BuildBufferedInwardDecision(samples.data(),samples.size(),
                Established(base),base,configured,available,19,23);
        }
        static ActivePictureFrameIdentity Identity(uint64_t n) { return {7,n,400+n,n*1000,11,13,17}; }
    public:
        TEST_METHOD(BufferedFullRasterReturnAppliesToFirstEligibleFrameOnBothAxes) {
            for(bool sides:{false,true}) for(double fps:{23.976,24.0,25.0,30.0,50.0,60.0}) {
                auto samples=Samples(sides);
                for(auto& s:samples) s.observation.framesPerSecond=fps;
                auto proof=Build(samples);
                Assert::IsTrue(proof.transition.publish);
                Assert::AreEqual<uint64_t>(100,proof.effectiveIdentity.acceptedSequence);
                Assert::AreEqual<uint64_t>(101,proof.observationIdentity.acceptedSequence);
                Assert::AreEqual<unsigned>(2,proof.proofFrameCount);
                auto live=Established(Full());
                Assert::IsTrue(live.AdoptPublishedDecision(proof.transition,ActivePictureClassification::BAR_CROP_TRUSTED));
            }
        }
        TEST_METHOD(OrdinaryTimelineFullRasterReturnRequiresCompletePendingEvidence) {
            for(bool sides:{false,true}) {
                ActivePictureDecisionTimeline timeline; ActivePictureFrameDecision decision;
                Assert::IsTrue(timeline.SubmitScheduledObservation(Identity(1),Observe(1,Full()),5,4,decision));
                for(uint64_t n=2;n<=3;++n) {
                    const auto id=Identity(n); auto o=Observe(n,Bars(sides));
                    Assert::IsTrue(timeline.TrackAcceptedFrame(id));
                    Assert::IsTrue(timeline.TrackLookaheadEvidence(id,o,true,false));
                    Assert::AreEqual(n==3,timeline.SubmitScheduledObservation(id,o,5,4,decision));
                }
                Assert::AreEqual<uint64_t>(2,decision.effectiveIdentity.acceptedSequence);
                Assert::AreEqual<int>(int(ActivePictureInwardProofValidation::ACCEPTED),int(decision.inwardProof));
                Assert::IsFalse(decision.late);
            }
        }
        TEST_METHOD(OrdinaryFullRasterProofCannotSkipContradictoryOrMissingIntermediateFrames) {
            for(int fault=0;fault<6;++fault) {
                ActivePictureDecisionTimeline timeline; ActivePictureFrameDecision d;
                timeline.SubmitScheduledObservation(Identity(1),Observe(1,Full()),5,4,d);
                for(uint64_t n=2;n<=4;++n) {
                    auto id=Identity(n); timeline.TrackAcceptedFrame(id); auto o=Observe(n,Bars());
                    if(n==3) {
                        if(fault==0) continue;
                        if(fault==1) o=Observe(n,Full());
                        if(fault==2) o.transitionDeferred=true;
                        if(fault==3) o.bounds.bottom-=4;
                        if(fault==5) o.classification=ActivePictureClassification::PROVISIONAL;
                    }
                    timeline.TrackLookaheadEvidence(id,o,true,n==3&&fault==4);
                    if(n!=3) timeline.SubmitScheduledObservation(id,o,5,4,d);
                }
                Assert::IsTrue(d.transition.publish); // Confirmation itself is valid.
                Assert::AreEqual<uint64_t>(4,d.effectiveIdentity.acceptedSequence); // Earlier association is not.
                Assert::IsTrue(d.late);
            }
        }
        TEST_METHOD(BufferedFullRasterProofKeepsAllEvidenceAndIdentityVetoes) {
            for(bool sides:{false,true}) for(int index:{0,1}) for(int fault=0;fault<17;++fault) {
                auto samples=Samples(sides); auto& s=samples[index];
                switch(fault) {
                case 0:s.observation.available=false;break;
                case 1:s.observation.classification=ActivePictureClassification::PROVISIONAL;break;
                case 2:s.observation=Observe(s.identity.acceptedSequence,Full());break;
                case 3:s.observation.transitionDeferred=true;break;
                case 4:s.nearBlackEvaluated=false;break;
                case 5:s.retention.globalNearBlack=true;break;
                case 6:s.observation.bounds.trustedBarAxes=Axes::NONE;break;
                case 7:++s.identity.acceptedSequence;break;
                case 8:++s.identity.transportGeneration;break;
                case 9:++s.identity.sourceFormatGeneration;break;
                case 10:++s.identity.viewportGeneration;break;
                case 11:++s.identity.rendererGeneration;break;
                case 12:++s.observation.frameNumber;break;
                case 13:s.identity.sourceFrameNumber+=2;break;
                case 14:s.identity.captureTimestamp=samples[1-index].identity.captureTimestamp;break;
                case 15:if(sides) s.observation.bounds.left+=4;else s.observation.bounds.top+=4;break;
                case 16:if(sides) s.observation.axisEvidence.horizontal.barCandidate=true;else s.observation.axisEvidence.vertical.barCandidate=true;break;
                }
                Assert::IsFalse(Build(samples).transition.publish);
            }
        }
        TEST_METHOD(FullRasterProofRequiresTwoRealFramesAndAvailableBudget) {
            auto samples=Samples(); const auto live=Established(Full());
            Assert::IsFalse(Build(samples,Full(),0,4).transition.publish);
            Assert::IsFalse(Build(samples,Full(),5,0).transition.publish);
            Assert::IsTrue(Build(samples,Full(),1,1).transition.publish);
            Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples.data(),1,live,Full(),5,4,19,23).transition.publish);
            Assert::IsFalse(AlphaSourceCrop::BuildBufferedInwardDecision(samples.data(),2,ActivePictureTransitionModel{},Full(),5,4,19,23).transition.publish);
        }
        TEST_METHOD(FullBaseExceptionDoesNotPermitUntrustedPartialOrCrossAxisBounds) {
            for(int fault=0;fault<5;++fault) {
                auto base=Full();
                if(fault==0) base.right-=4;
                if(fault==1) base.top=4;
                if(fault==2) base.rasterWidth=0;
                if(fault==3) base.trustedBarAxes=Axes::LEFT_RIGHT;
                if(fault==4) base.rasterHeight=2164;
                ActivePictureTransitionDecision geometry;geometry.stableBounds=base;geometry.bounds=Bars();
                Assert::IsFalse(IsExactInwardActivePictureAssociationGeometry(geometry,ActivePictureClassification::BAR_CROP_TRUSTED));
            }
            auto samples=Samples();
            for(auto& s:samples){s.observation.bounds.left=16;s.observation.bounds.right-=16;}
            Assert::IsFalse(Build(samples).transition.publish); // Windowboxed composition is not single-axis.
        }
        TEST_METHOD(FullRasterCanonicalizationKeepsTinyMeasurementNoiseOutOfAuthority) {
            ActivePictureEvidence evidence; evidence.available=true;
            evidence.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            evidence.trustedBounds=Full();evidence.proposedBounds=Full();evidence.proposedBounds.right-=4;
            auto live=Established(Bars());
            auto first=AlphaSourceCrop::MakeBufferedPictureSample(Identity(10),evidence,24,false,false).observation;
            Assert::AreEqual(3840,first.bounds.right);
            Assert::IsFalse(live.Observe(first).publish);
            auto confirmed=live.Observe(AlphaSourceCrop::MakeBufferedPictureSample(Identity(11),evidence,24,false,false).observation);
            Assert::IsTrue(confirmed.publish);
            Assert::AreEqual(3840,confirmed.bounds.right);
            evidence.classification=ActivePictureClassification::PROVISIONAL;
            Assert::AreEqual(3836,AlphaSourceCrop::MakeBufferedPictureSample(Identity(12),evidence,24,false,false).observation.bounds.right);
        }
        TEST_METHOD(BufferedLiveReferenceCannotCrossSourceFormatOrViewportReset) {
            auto id=Identity(10); id.rendererGeneration=id.transportGeneration;
            Assert::IsTrue(AlphaSourceCrop::BufferedPictureReferenceMatches(id,7,11,13));
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,8,11,13));
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,7,12,13));
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,7,11,14));
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,0,11,13));
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,7,0,13));
            ++id.rendererGeneration;
            Assert::IsFalse(AlphaSourceCrop::BufferedPictureReferenceMatches(id,7,11,13));
        }
        TEST_METHOD(AmbiguousCutFrameOrSecondCutCannotSupplyConfirmation) {
            auto model=Established(Full());
            model.Observe(Observe(98,Bars()));
            model.ResetCandidateEvidence();
            auto ambiguous=Observe(99,Bars());ambiguous.available=false;
            Assert::IsFalse(model.Observe(ambiguous).publish);
            Assert::IsFalse(model.Observe(Observe(100,Bars())).publish);
            model.ResetCandidateEvidence(); // A second cut invalidates the new partial proof.
            Assert::IsFalse(model.Observe(Observe(101,Bars())).publish);
            Assert::IsTrue(model.Observe(Observe(102,Bars())).publish);
        }
        TEST_METHOD(CutFrameStartsFreshProofAndCountsOnce) {
            auto model=Established(Bars());
            model.Observe(Observe(10,Full()));model.Observe(Observe(11,Full()));
            Assert::IsFalse(model.Observe(Observe(99,Bars())).publish); // Pre-cut evidence must not count.
            model.ResetCandidateEvidence();
            const auto cut=model.Observe(Observe(100,Bars()));
            Assert::IsFalse(cut.publish);
            Assert::AreEqual<uint64_t>(100,cut.firstContradictoryFrame);
            Assert::IsFalse(model.Observe(Observe(100,Bars())).publish); // Duplicate capture does not count twice.
            const auto next=model.Observe(Observe(101,Bars()));
            Assert::IsTrue(next.publish);
            Assert::AreEqual<uint64_t>(100,next.firstContradictoryFrame);
        }
        TEST_METHOD(FullRasterBufferedProofRetainsGeometryDeadbandAndDoesNotMutateLiveHistory) {
            auto samples=Samples(); auto tiny=Full();tiny.top=4;tiny.bottom-=4;tiny.trustedBarAxes=Axes::TOP_BOTTOM;
            tiny.aspectRatio=3840.0/(tiny.bottom-tiny.top);
            for(auto& s:samples)s.observation.bounds=tiny;
            Assert::IsFalse(Build(samples).transition.publish);
            auto live=Established(Full());samples=Samples();
            auto proof=AlphaSourceCrop::BuildBufferedInwardDecision(samples.data(),2,live,Full(),5,4,19,23);
            Assert::IsTrue(proof.transition.publish);
            Assert::IsFalse(live.Observe(samples[0].observation).publish);
            Assert::IsTrue(live.Observe(samples[1].observation).publish);
        }
    };
}
