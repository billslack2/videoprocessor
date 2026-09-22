#include "pch.h"
#include "CppUnitTest.h"
#include <ActivePictureEvidence.h>
#include <vprenderer/BufferedPictureExpansion.h>
#include <array>
#include <sstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace AlphaSourceCrop;

namespace Tests
{
    namespace
    {
        ActivePictureBounds ShadowScope()
        {
            return {0,276,3840,1884,3840,2160,3840.0/1608.0,ActivePictureBounds::BarAxes::TOP_BOTTOM};
        }
        ActivePictureFrameIdentity ShadowIdentity(uint64_t sequence)
        {
            return {7,sequence,sequence,sequence*417083,11,13,17};
        }
        struct ShadowPixels
        {
            std::vector<uint16_t> pixels;
            ActivePictureEvidence evidence;
            ActivePicturePresentationRetentionEvidence retention;
            ShadowPixels() : pixels(3840*2160*3/2,uint16_t(512<<6))
            {
                for (int y=0;y<2160;++y)
                    if (y<68 || y>=2092)
                        std::fill(pixels.begin()+size_t(y)*3840,pixels.begin()+size_t(y+1)*3840,uint16_t(64<<6));
                AnalysisLumaSource source;
                source.data=reinterpret_cast<const uint8_t*>(pixels.data()); source.dataBytes=pixels.size()*sizeof(uint16_t);
                source.width=3840; source.height=2160; source.rowBytes=source.chromaRowBytes=3840*sizeof(uint16_t);
                source.format=AnalysisLumaFormat::P010; source.generation=7;
                evidence=ExtractActivePictureEvidence(source);
                retention=EvaluateActivePicturePresentationRetention(source,ShadowScope());
            }
            std::array<BufferedPictureExpansionSample,3> Samples() const
            {
                std::array<BufferedPictureExpansionSample,3> result;
                for (size_t i=0;i<result.size();++i)
                {
                    result[i].identity=ShadowIdentity(100+i);
                    result[i].observation=MakeActivePictureObservation(evidence,100+i,24);
                    result[i].retention=retention;
                    result[i].nearBlackEvaluated=true;
                }
                return result;
            }
        };
        void WriteBounds(std::ostringstream& out,const ActivePictureBounds& b)
        {
            out<<b.left<<','<<b.top<<','<<b.right<<','<<b.bottom<<','<<b.rasterWidth<<','<<b.rasterHeight
                <<','<<b.aspectRatio<<','<<static_cast<int>(b.trustedBarAxes)<<';';
        }
        std::string InputFingerprint(const std::array<BufferedPictureExpansionSample,3>& samples)
        {
            std::ostringstream out;
            for (const auto& s:samples)
            {
                const auto& id=s.identity;
                out<<id.transportGeneration<<','<<id.acceptedSequence<<','<<id.sourceFrameNumber<<','<<id.captureTimestamp
                    <<','<<id.sourceFormatGeneration<<','<<id.viewportGeneration<<','<<id.rendererGeneration<<';';
                WriteBounds(out,s.observation.bounds);
                out<<s.observation.frameNumber<<','<<s.observation.available<<','<<static_cast<int>(s.observation.classification)
                    <<','<<s.observation.framesPerSecond<<','<<s.observation.transitionDeferred<<','<<s.nearBlackEvaluated<<';';
                const auto& r=s.retention;
                out<<r.analysisValid<<','<<r.presentationValid<<','<<r.globalNearBlack<<','<<r.globalLumaP90
                    <<','<<r.expansionStripsAvailable<<','<<r.excludedBandsPixelSafe<<','<<r.reason<<';';
                WriteBounds(out,r.expansionBase); WriteBounds(out,r.expansionCandidate);
                for (const auto* edge : {&r.expandingLeft,&r.expandingTop,&r.expandingRight,&r.expandingBottom})
                    out<<edge->barPixels<<','<<edge->blackFraction<<','<<edge->lumaP90<<','<<edge->texture
                        <<','<<edge->continuity<<','<<edge->trusted<<';';
            }
            return out.str();
        }
        void CheckParity(const std::array<BufferedPictureExpansionSample,3>& samples,
            const char* reason,int failedSample,bool passes=false)
        {
            const auto before=InputFingerprint(samples);
            BufferedPictureExpansionDiagnostic attached;
            const auto with=BuildBufferedPictureExpansion(samples.data(),samples.size(),ShadowScope(),5,4,19,23,&attached);
            const auto ordinary=BuildBufferedPictureExpansion(samples.data(),samples.size(),ShadowScope(),5,4,19,23);
            const auto inspected=InspectBufferedPictureExpansion(samples.data(),samples.size(),ShadowScope(),5,4,19,23);
            Assert::AreEqual(passes,inspected.passes);
            Assert::AreEqual(passes,with.valid);
            Assert::AreEqual(ordinary.valid,with.valid);
            Assert::AreEqual(reason,inspected.reason);
            Assert::AreEqual(reason,attached.reason);
            Assert::AreEqual(failedSample,inspected.failedSample);
            Assert::AreEqual(failedSample,attached.failedSample);
            Assert::AreEqual(before,InputFingerprint(samples),L"Diagnostic inspection must not mutate samples or confirmation inputs.");
            Assert::AreEqual(ordinary.decision.transition.publish,with.decision.transition.publish);
            Assert::AreEqual(ordinary.decision.effectiveIdentity.acceptedSequence,with.decision.effectiveIdentity.acceptedSequence);
            Assert::AreEqual(ordinary.decision.observationIdentity.acceptedSequence,with.decision.observationIdentity.acceptedSequence);
            Assert::AreEqual(ordinary.decision.transition.bounds.top,with.decision.transition.bounds.top);
            Assert::AreEqual(ordinary.decision.transition.bounds.bottom,with.decision.transition.bounds.bottom);
        }
    }

    TEST_CLASS(FitLookaheadDiagnosticTests)
    {
    public:
        TEST_METHOD(ShadowInspectionMatchesExistingPixelProofWithoutMutatingInputs)
        {
            const ShadowPixels pixels;
            CheckParity(pixels.Samples(),"proof-ready",-1,true);
        }

        TEST_METHOD(ShadowInspectionExplainsBudgetAndFirstIdentityFailures)
        {
            const ShadowPixels pixels;
            const auto samples=pixels.Samples();
            for (int fault=0;fault<6;++fault)
            {
                const auto result=InspectBufferedPictureExpansion(fault==0 ? nullptr : samples.data(),
                    fault==1 ? 2 : 3,ShadowScope(),fault==2 ? 1 : 5,fault==3 ? 1 : 4,
                    fault==4 ? 0 : 19,fault==5 ? 0 : 23);
                Assert::IsFalse(result.passes);
                Assert::AreEqual("budget",result.reason);
                Assert::AreEqual(-1,result.failedSample);
            }
            for (int fault=0;fault<4;++fault)
            {
                auto invalid=samples;
                if (fault==0) invalid[0].identity.transportGeneration=0;
                if (fault==1) invalid[0].identity.sourceFormatGeneration=0;
                if (fault==2) invalid[0].identity.acceptedSequence=0;
                if (fault==3) invalid[0].identity.acceptedSequence=UINT64_MAX;
                CheckParity(invalid,"first-identity",-1);
            }
        }

        TEST_METHOD(ShadowInspectionReportsExactFailingSampleAndExistingVeto)
        {
            const ShadowPixels pixels;
            for (int fault=0;fault<12;++fault)
            {
                auto samples=pixels.Samples();
                auto& changed=samples[1];
                const char* reason="sample-evidence";
                switch (fault)
                {
                case 0: ++changed.identity.viewportGeneration; reason="sample-identity"; break;
                case 1: ++changed.identity.acceptedSequence; reason="sample-identity"; break;
                case 2: ++changed.observation.frameNumber; reason="sample-identity"; break;
                case 3: changed.nearBlackEvaluated=false; break;
                case 4: changed.observation.available=false; break;
                case 5: changed.observation.transitionDeferred=true; break;
                case 6: changed.retention.globalNearBlack=true; break;
                case 7: changed.retention.analysisValid=false; break;
                case 8: changed.retention.expansionCandidate.top+=4; break;
                case 9: changed.identity.sourceFrameNumber=samples[0].identity.sourceFrameNumber; reason="source-continuity"; break;
                case 10: changed.identity.captureTimestamp=samples[0].identity.captureTimestamp; reason="timestamp-continuity"; break;
                case 11:
                    changed.observation.bounds.top=100;
                    changed.retention.expansionCandidate=changed.observation.bounds;
                    reason="geometry-continuity"; break;
                }
                CheckParity(samples,reason,1);
            }
        }

        TEST_METHOD(RecordedWeakUpperStripAndProvisionalSampleCannotPassShadowProof)
        {
            const ShadowPixels pixels;
            auto weak=pixels.Samples();
            // Recorded sequence3661: adequate bottom content, but upper strip
            // falls below both existing broad-picture brightness/texture gates.
            auto& top=weak[0].retention.expandingTop;
            top.barPixels=208; top.blackFraction=0.7083; top.lumaP90=106;
            top.texture=10.6; top.continuity=0.1667;
            CheckParity(weak,"sample-evidence",0);
            auto provisional=pixels.Samples();
            provisional[2].observation.classification=ActivePictureClassification::PROVISIONAL;
            CheckParity(provisional,"sample-evidence",2);
        }

        TEST_METHOD(SuccessfulShadowProofStillCannotPublishThroughCurrentFitOwner)
        {
            const ShadowPixels pixels;
            const auto samples=pixels.Samples();
            const auto inspected=InspectBufferedPictureExpansion(samples.data(),3,ShadowScope(),5,4,19,23);
            Assert::IsTrue(inspected.passes);
            const auto proof=BuildBufferedPictureExpansion(samples.data(),3,ShadowScope(),5,4,19,23);
            TransitionAdmissionInput current;
            current.evidence=pixels.evidence; current.retention=pixels.retention;
            current.outwardCandidate=pixels.evidence.trustedBounds;
            current.trustedGeometry=current.presentationBeforeObservation=ShadowScope();
            current.trustedGeometryAvailable=current.compatiblePresentation=true;
            current.trustedGeneration=current.sourceGeneration=current.presentationEvidenceGeneration=7;
            current.sourceSequence=100; current.framesPerSecond=24;
            Assert::IsTrue(ValidateBufferedPictureExpansion(proof,samples[0].identity,current,true),
                L"The fixture must otherwise satisfy the existing consumer before testing the FIT veto.");
            current.presentation.action=VerticalBarPresentationAction::FIT;
            const auto oldTop=current.trustedGeometry.top;
            Assert::IsFalse(ValidateBufferedPictureExpansion(proof,samples[0].identity,current,true));
            Assert::IsFalse(ValidateBufferedTranslatedPictureExpansion(proof,samples[0].identity,current,true,ShadowScope()));
            Assert::IsTrue(current.presentation.action==VerticalBarPresentationAction::FIT);
            Assert::AreEqual(oldTop,current.trustedGeometry.top);
        }

        TEST_METHOD(ShadowTraceCountsUniqueWindowsAndKeepsFirstPassingSequence)
        {
            BufferedExpansionShadowTrace trace;
            Assert::IsFalse(ObserveBufferedExpansionShadow(trace,ShadowIdentity(100),ShadowScope(),false));
            Assert::IsTrue(trace.active);
            Assert::AreEqual(uint64_t{100},trace.firstSequence);
            Assert::AreEqual(uint64_t{1},trace.windows);
            Assert::AreEqual(uint64_t{0},trace.passes);
            Assert::IsFalse(ObserveBufferedExpansionShadow(trace,ShadowIdentity(100),ShadowScope(),true));
            Assert::AreEqual(uint64_t{1},trace.windows);
            Assert::AreEqual(uint64_t{0},trace.firstPassSequence);
            Assert::IsTrue(ObserveBufferedExpansionShadow(trace,ShadowIdentity(101),ShadowScope(),true));
            Assert::AreEqual(uint64_t{101},trace.firstPassSequence);
            Assert::AreEqual(uint64_t{2},trace.windows);
            Assert::AreEqual(uint64_t{1},trace.passes);
            Assert::IsFalse(ObserveBufferedExpansionShadow(trace,ShadowIdentity(101),ShadowScope(),true));
            Assert::AreEqual(uint64_t{1},trace.passes);
            Assert::IsFalse(ObserveBufferedExpansionShadow(trace,ShadowIdentity(102),ShadowScope(),false));
            Assert::IsFalse(ObserveBufferedExpansionShadow(trace,ShadowIdentity(103),ShadowScope(),true));
            Assert::AreEqual(uint64_t{4},trace.windows);
            Assert::AreEqual(uint64_t{2},trace.passes);
            Assert::AreEqual(uint64_t{101},trace.firstPassSequence);
        }

        TEST_METHOD(ShadowTraceResetsOnContextBaseOrSequenceRollback)
        {
            for (int fault=0;fault<8;++fault)
            {
                BufferedExpansionShadowTrace trace;
                Assert::IsTrue(ObserveBufferedExpansionShadow(trace,ShadowIdentity(100),ShadowScope(),true,23,19));
                auto identity=ShadowIdentity(101);
                uint64_t policy=23;
                uint64_t continuity=19;
                auto base=ShadowScope();
                switch (fault)
                {
                case 0: ++identity.transportGeneration; break;
                case 1: ++identity.sourceFormatGeneration; break;
                case 2: ++identity.viewportGeneration; break;
                case 3: ++identity.rendererGeneration; break;
                case 4: base.top+=4; break;
                case 5: identity=ShadowIdentity(99); break;
                case 6: policy=24; break;
                case 7: continuity=20; break;
                }
                Assert::IsTrue(ObserveBufferedExpansionShadow(trace,identity,base,true,policy,continuity));
                Assert::AreEqual(identity.acceptedSequence,trace.firstSequence);
                Assert::AreEqual(identity.acceptedSequence,trace.firstPassSequence);
                Assert::AreEqual(uint64_t{1},trace.windows);
                Assert::AreEqual(uint64_t{1},trace.passes);
            }
        }
    };
}
