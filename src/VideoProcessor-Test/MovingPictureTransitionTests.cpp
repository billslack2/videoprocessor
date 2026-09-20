#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/BufferedPictureExpansion.h>
#include <algorithm>
#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
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
            void Step()
            {
                input.sourceSequence=identity.acceptedSequence;
                motion=ObserveMovingPictureTransition(motion,identity,input);
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
        TEST_METHOD(ReversalAndBriefPauseKeepOnePresentationHold)
        {
            MotionSequence s; s.Ramp(20,4); Assert::IsTrue(s.motion.active);
            for (int i=0;i<3;++i) { s.Step(); Assert::IsTrue(s.motion.active); }
            const int top=s.input.outwardCandidate.top;
            for (int i=1;i<12;++i) { s.Target(top+i*4); s.Step(); Assert::IsTrue(s.motion.active); }
        }
    };
}
