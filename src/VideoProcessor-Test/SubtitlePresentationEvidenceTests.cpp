#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/SubtitlePresentationEvidence.h>
#include <vector>
#include <chrono>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest {
namespace {
struct SubtitleEvidenceFixture {
    int Width, Height;
    bool p210;
    size_t pitch;
    std::vector<uint8_t> pixels;
    SubtitleBoxObservation observation;
    SubtitleBoxResult cue;
    AlphaSourceCrop::SubtitlePresentationEvidenceInput input;

    void Code(size_t offset,int code) {
        const auto value=static_cast<uint16_t>(code<<6);
        pixels[offset]=static_cast<uint8_t>(value);pixels[offset+1]=static_cast<uint8_t>(value>>8);
    }
    void Rectangle(int left,int top,int right,int bottom,int y,int u=512,int v=512) {
        for(int row=top;row<bottom;++row)for(int x=left;x<right;++x)Code(size_t(row)*pitch+x*2,y);
        for(int row=p210?top:top/2;row<(p210?bottom:(bottom+1)/2);++row)
            for(int x=left&~1;x<right;x+=2) {
                const size_t at=pitch*Height+size_t(row)*pitch+x*2;Code(at,u);Code(at+2,v);
            }
    }
    AnalysisLumaSource Source() const {
        return {pixels.data(),pixels.size(),Width,Height,pitch,pitch,
            p210?AnalysisLumaFormat::P210:AnalysisLumaFormat::P010,
            p210?VideoFrameEncoding::V210:VideoFrameEncoding::UNKNOWN,ColorSpace::REC_709,11};
    }
    explicit SubtitleEvidenceFixture(bool top=false,bool crossing=false,bool fullChroma=false,int scale=1,int cueRight=400):
        Width(640*scale),Height(360*scale),p210(fullChroma),pitch(size_t(Width)*2+32),
        pixels(pitch*(Height+(fullChroma?Height:Height/2)),0) {
        using namespace AlphaSourceCrop;
        Rectangle(0,0,Width,Height,64);Rectangle(0,48*scale,Width,312*scale,300);
        input.identity={11,101,501,12345,13,17,19};
        input.policyGeneration=23;input.continuityGeneration=29;
        input.logical={0,48*scale,Width,312*scale,Width,Height,double(Width)/(264*scale),ActivePictureBounds::BarAxes::TOP_BOTTOM};
        input.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
        input.priorAdmission.available=true;input.priorAdmission.trustedCrop=input.logical;
        input.priorAdmission.sourceGeneration=11;input.priorAdmission.presentationEpoch=17;
        input.admitted.applyCrop=input.admitted.outwardExpanded=true;
        input.admitted.owner=DecisionOwner::OUTWARD_FIT;input.admitted.sourceBounds=input.logical;
        input.admitted.sourceBounds.bottom=348*scale;
        input.compositionSucceeded=true;input.backgroundMode=3;
        observation.identity=input.identity;observation.policyGeneration=23;observation.continuityGeneration=29;
        observation.width=Width;observation.height=Height;observation.pictureTop=48*scale;observation.pictureBottom=312*scale;
        observation.analyzed=observation.barAuthority=true;
        observation.sharedPicture.required=true;observation.sharedPicture.identity=input.identity;
        observation.sharedPicture.bounds=input.logical;
        cue.detected=true;cue.cue=3;cue.observations=3;cue.lineCount=1;cue.capturePanelMeasured=true;
        cue.bounds={200*scale,(crossing?304:326)*scale,cueRight*scale,340*scale};
        auto mirror=[this](SubtitleBoxRect r){return SubtitleBoxRect{r.left,Height-r.bottom,r.right,Height-r.top};};
        if(top) {cue.bounds=mirror(cue.bounds);input.admitted.sourceBounds=input.logical;input.admitted.sourceBounds.top=12*scale;}
        cue.lineBounds[0]=cue.anchor=cue.bounds;
        if(crossing) {
            cue.sourcePanel=cue.capturePanel={190*scale,(top?48:298)*scale,410*scale,(top?62:312)*scale};
            Rectangle(cue.sourcePanel.left,cue.sourcePanel.top,cue.sourcePanel.right,cue.sourcePanel.bottom,64);
        }
        Rectangle(cue.bounds.left,cue.bounds.top,cue.bounds.right,cue.bounds.bottom,700);
        observation.text=cue;
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->rawEvidenceComplete=ink->paletteValid=true;ink->step=SubtitleBoxDetector::SamplingStep(Width,Height);
        ink->width=(Width+ink->step-1)/ink->step;
        for(int y=0;y<Height;y+=ink->step)ink->sourceRows.push_back(y);
        ink->height=static_cast<int>(ink->sourceRows.size());
        const size_t words=(size_t(ink->width)*ink->height+63)/64;
        ink->rawInk.resize(words);ink->ownedInk.resize(words);ink->blackBacking.resize(words);
        ink->inkFloor=200;ink->blackLimit=80;
        for(int row=0;row<ink->height;++row)for(int x=0;x<ink->width;++x) {
            const int sx=x*ink->step,sy=ink->sourceRows[row];const size_t bit=size_t(row)*ink->width+x;
            const uint64_t mask=uint64_t(1)<<(bit%64);
            if(sx>=cue.bounds.left && sx<cue.bounds.right && sy>=cue.bounds.top && sy<cue.bounds.bottom)
                ink->rawInk[bit/64]|=mask,ink->ownedInk[bit/64]|=mask;
            else if(sy<48*scale || sy>=312*scale)ink->blackBacking[bit/64]|=mask;
        }
        observation.ink=ink;
        SubtitleBoxPreview preview;preview.available=true;preview.current=observation;preview.text=cue;
        preview.policyGeneration=input.policyGeneration;preview.continuityGeneration=input.continuityGeneration;
        SubtitleCutPastePresentation presentation;
        const SubtitleBoxPadding padding{12*scale,8*scale,8*scale};
        input.move=presentation.Consume(cue,preview,padding,15*scale);
        input.move=FitSubtitleToVisiblePicture(input.move,{0,48*scale,Width,312*scale},15*scale);
        input.move=ApplySubtitleTextReduction(input.move,0,padding,true,true);
        input.observation=&observation;input.acceptedCue=&cue;
    }
    auto Evaluate() const { return AlphaSourceCrop::EvaluateSubtitlePresentationEvidence(input,Source()); }
};
void AssertBounds(const ActivePictureBounds& expected,const ActivePictureBounds& actual) {
    Assert::AreEqual(expected.left,actual.left);Assert::AreEqual(expected.top,actual.top);
    Assert::AreEqual(expected.right,actual.right);Assert::AreEqual(expected.bottom,actual.bottom);
}
}
TEST_CLASS(SubtitlePresentationEvidenceTests) {
public:
    TEST_METHOD(CurrentRelocatedSubtitleRetainsExactLogicalBoundsAcrossEdgesAndFormats) {
        for(bool top:{false,true})for(bool crossing:{false,true})for(bool p210:{false,true})for(int mode:{3,5}) {
            SubtitleEvidenceFixture f(top,crossing,p210);f.input.backgroundMode=mode;
            const auto before=f.pixels;
            const auto raw=EvaluateActivePicturePresentationRetention(f.Source(),f.input.logical);
            Assert::IsFalse(raw.excludedBandsPixelSafe);
            const auto result=f.Evaluate();Assert::IsTrue(result.accepted);
            AssertBounds(f.input.logical,result.bounds);
            Assert::IsTrue(result.retention.excludedBandsPixelSafe);
            Assert::IsTrue(before==f.pixels,L"presentation certificate must never edit source pixels");
            const auto after=EvaluateActivePicturePresentationRetention(f.Source(),f.input.logical);
            Assert::AreEqual(raw.activePicture.reason,after.activePicture.reason);
            AssertBounds(raw.activePicture.proposedBounds,after.activePicture.proposedBounds);
            Assert::AreEqual(raw.outwardVisibleBoundsAvailable,after.outwardVisibleBoundsAvailable);
            AssertBounds(raw.outwardVisibleBounds,after.outwardVisibleBounds);
        }
    }
    TEST_METHOD(FourKCertificateReportsNativeAuditCostAndRejectsBrightExpansionEarly) {
        for(bool p210:{false,true}) {
            SubtitleEvidenceFixture f(false,false,p210,6);
            const int repeats=8;
            size_t samples=0;
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<repeats;++i) {
                const auto result=f.Evaluate();
                Assert::IsTrue(result.accepted,L"4K production geometry must be certifiable");
                samples=result.samples;
            }
            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/repeats;
            std::ostringstream log;
            log<<"Subtitle presentation 3840x2160 "<<(p210?"P210":"P010")<<": "<<elapsed<<" ms/frame, samples="<<samples;
            Logger::WriteMessage(log.str().c_str());
            Assert::IsTrue(samples>=size_t(3840)*500,L"exercise native bar audit, not an early no-op");
            f.Rectangle(0,1872,3840,1956,300); // broad newly exposed picture before the existing caption
            const auto conflictStart=std::chrono::steady_clock::now();
            const auto conflict=f.Evaluate();
            Assert::IsFalse(conflict.accepted);
            Assert::IsTrue(conflict.samples<samples,L"visible broad expansion should reject before full native audit");
            std::ostringstream conflictLog;
            conflictLog<<"Subtitle presentation 4K bright expansion: "
                <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-conflictStart).count()
                <<" ms, samples="<<conflict.samples<<", reason="<<conflict.reason;
            Logger::WriteMessage(conflictLog.str().c_str());
        }
    }
    TEST_METHOD(CompositionFailureAndUnsupportedStylesCannotCertifyErasure) {
        for(int mode:{0,1,2,4}) {SubtitleEvidenceFixture f;f.input.backgroundMode=mode;Assert::IsFalse(f.Evaluate().accepted);}
        SubtitleEvidenceFixture f;f.input.compositionSucceeded=false;
        Assert::IsFalse(f.Evaluate().accepted);
        const auto raw=EvaluateActivePicturePresentationRetention(f.Source(),f.input.logical);
        Assert::IsFalse(raw.excludedBandsPixelSafe,L"visible original caption remains protected");
    }
    TEST_METHOD(EveryCurrentFrameIdentityAndPolicyDimensionMustMatch) {
        for(int changed=0;changed<9;++changed) {
            SubtitleEvidenceFixture f;
            if(changed==0)++f.observation.identity.transportGeneration;
            if(changed==1)++f.observation.identity.acceptedSequence;
            if(changed==2)++f.observation.identity.sourceFrameNumber;
            if(changed==3)++f.observation.identity.captureTimestamp;
            if(changed==4)++f.observation.identity.sourceFormatGeneration;
            if(changed==5)++f.observation.identity.viewportGeneration;
            if(changed==6)++f.observation.identity.rendererGeneration;
            if(changed==7)++f.observation.policyGeneration;
            if(changed==8)++f.observation.continuityGeneration;
            Assert::IsFalse(f.Evaluate().accepted);
        }
        SubtitleEvidenceFixture f;auto source=f.Source();++source.generation;
        Assert::IsFalse(AlphaSourceCrop::EvaluateSubtitlePresentationEvidence(f.input,source).accepted);
    }
    TEST_METHOD(HeldPartialAndWorkLimitedMeasurementsCannotReplaceFreshOwnership) {
        for(int changed=0;changed<10;++changed) {
            SubtitleEvidenceFixture f;
            if(changed==0)f.observation.text.held=true;
            if(changed==1)f.cue.held=true;
            if(changed==2)f.observation.text.workLimit=true;
            if(changed==3)f.cue.workLimit=true;
            if(changed==4)f.observation.analysisRefresh=true;
            if(changed==5)f.observation.pendingRefresh=true;
            if(changed==6)f.observation.analyzed=false;
            if(changed==7)f.observation.text.detected=false;
            if(changed==8)f.cue.detected=false;
            if(changed==9)f.observation.ink.reset();
            Assert::IsFalse(f.Evaluate().accepted);
        }
    }
    TEST_METHOD(CannotCreateAdmissionOrOverrideRecoveryAndTransitionGuards) {
        for(int changed=0;changed<14;++changed) {
            SubtitleEvidenceFixture f;
            if(changed==0)f.input.priorAdmission.available=false;
            if(changed==1)++f.input.priorAdmission.sourceGeneration;
            if(changed==2)++f.input.priorAdmission.presentationEpoch;
            if(changed==3)--f.input.priorAdmission.trustedCrop.bottom;
            if(changed==4)f.input.admitted.applyCrop=false;
            if(changed==5)f.input.admitted.outwardExpanded=false;
            if(changed==6)f.input.admitted.owner=AlphaSourceCrop::DecisionOwner::FULL_RASTER;
            if(changed==7)f.input.admitted.verticallyTranslated=true;
            if(changed==8)f.input.sourceDiscontinuity=true;
            if(changed==9)f.input.transitionBlocked=true;
            if(changed==10)f.input.recoveryBlocked=true;
            if(changed==11)f.input.conflictingOwnership=true;
            if(changed==12)f.input.classification=ActivePictureClassification::FULL_RASTER_TRUSTED;
            if(changed==13)f.observation.discontinuity=true;
            Assert::IsFalse(f.Evaluate().accepted);
        }
    }
    TEST_METHOD(SharedPictureAuthorityAndCleanupCoordinatesCannotBeBorrowed) {
        for(int changed=0;changed<9;++changed) {
            SubtitleEvidenceFixture f;
            if(changed==0)f.observation.sharedPicture.required=false;
            if(changed==1)++f.observation.sharedPicture.identity.transportGeneration;
            if(changed==2)--f.observation.sharedPicture.bounds.bottom;
            if(changed==3)--f.input.move.pictureBottom;
            if(changed==4)f.input.move.valid=false;
            if(changed==5)--f.input.move.content.left;
            if(changed==6)--f.cue.bounds.left;
            if(changed==7)--f.input.move.glyphLines[0].left;
            if(changed==8)f.observation.barAuthority=false;
            Assert::IsFalse(f.Evaluate().accepted);
        }
    }
    TEST_METHOD(RealPictureExpansionAndIndependentBarContentAlwaysWin) {
        for(bool top:{false,true})for(bool p210:{false,true})for(int content=0;content<4;++content) {
            SubtitleEvidenceFixture f(top,false,p210);
            if(content==0)f.Rectangle(0,312,640,316,300); // broad true format expansion
            if(content==1)f.Rectangle(30,315,31,316,300); // native off-grid single pixel
            if(content==2)f.Rectangle(80,12,160,28,700); // distinct overlay on opposite edge
            if(content==3)f.Rectangle(30,320,34,324,64,700,512); // colored near-black scene content
            Assert::IsFalse(f.Evaluate().accepted);
        }
    }
    TEST_METHOD(NativeAuditFindsEveryVectorLumaAndChromaLane) {
        for(bool p210:{false,true}) {
            for(int lane=0;lane<8;++lane) {
                SubtitleEvidenceFixture f(false,false,p210);
                f.Rectangle(32+lane,319,33+lane,320,300);
                const auto result=f.Evaluate();Assert::IsFalse(result.accepted);
                Assert::AreEqual(std::string("native-content-outside-erasure"),std::string(result.reason));
            }
            for(int pair=0;pair<4;++pair)for(int component:{0,1})for(int value:{479,545}) {
                SubtitleEvidenceFixture f(false,false,p210);
                const int y=p210?319:319/2;
                f.Code(f.pitch*f.Height+size_t(y)*f.pitch+(32/2+pair)*4+component*2,value);
                const auto result=f.Evaluate();Assert::IsFalse(result.accepted);
                Assert::AreEqual(std::string("native-content-outside-erasure"),std::string(result.reason));
            }
        }
    }
    TEST_METHOD(NativeAuditChecksErasureEdgesOddStartsAndScalarTails) {
        for(bool p210:{false,true})for(int cueRight:{399,400}) {
            SubtitleEvidenceFixture base(false,false,p210,1,cueRight);
            Assert::IsTrue(base.Evaluate().accepted);
            for(int x:{base.input.move.content.left-1,base.input.move.content.right,
                base.input.move.content.right+1,base.input.move.content.right+7,638,639}) {
                SubtitleEvidenceFixture f(false,false,p210,1,cueRight);
                f.Rectangle(x,330,x+1,331,300);
                const auto result=f.Evaluate();Assert::IsFalse(result.accepted);
                Assert::AreEqual(std::string("native-content-outside-erasure"),std::string(result.reason));
            }
        }
    }
    TEST_METHOD(PlanarAuditSupportsUnalignedSourceAndPitchedRows) {
        for(bool p210:{false,true})for(size_t offset:{size_t(1),size_t(3)}) {
            SubtitleEvidenceFixture f(false,false,p210);
            std::vector<uint8_t> bytes(f.pixels.size()+offset);
            std::copy(f.pixels.begin(),f.pixels.end(),bytes.begin()+offset);
            auto source=f.Source();source.data=bytes.data()+offset;
            const auto safe=AlphaSourceCrop::EvaluateSubtitlePresentationEvidence(f.input,source);
            Assert::IsTrue(safe.accepted);
            const size_t at=offset+size_t(319)*f.pitch+39*2;
            bytes[at]=static_cast<uint8_t>(300<<6);bytes[at+1]=static_cast<uint8_t>((300<<6)>>8);
            const auto conflict=AlphaSourceCrop::EvaluateSubtitlePresentationEvidence(f.input,source);
            Assert::IsFalse(conflict.accepted);
            Assert::AreEqual(std::string("native-content-outside-erasure"),std::string(conflict.reason));
        }
    }
    TEST_METHOD(MovedSubtitleDestinationMustRemainFullyInsideRetainedPicture) {
        for(bool top:{false,true}) {
            SubtitleEvidenceFixture f(top);const int height=f.input.move.destination.bottom-f.input.move.destination.top;
            if(top) {f.input.move.destination.top=47;f.input.move.destination.bottom=47+height;}
            else {f.input.move.destination.bottom=313;f.input.move.destination.top=313-height;}
            Assert::IsFalse(f.Evaluate().accepted);
        }
    }
    TEST_METHOD(AlternatingFreshAndHeldFramesCannotLeaveRetainedAuthorityBehind) {
        SubtitleEvidenceFixture f;
        const auto admitted=f.input.admitted;const auto prior=f.input.priorAdmission;
        for(int frame=0;frame<12;++frame) {
            ++f.input.identity.acceptedSequence;++f.input.identity.sourceFrameNumber;++f.input.identity.captureTimestamp;
            f.observation.identity=f.input.identity;
            f.observation.text.held=f.cue.held=(frame%2)==1;
            const auto result=f.Evaluate();
            Assert::AreEqual(frame%2==0,result.accepted);
            AssertBounds(admitted.sourceBounds,f.input.admitted.sourceBounds);
            Assert::AreEqual(admitted.applyCrop,f.input.admitted.applyCrop);
            Assert::AreEqual(admitted.outwardExpanded,f.input.admitted.outwardExpanded);
            AssertBounds(prior.trustedCrop,f.input.priorAdmission.trustedCrop);
            if(!result.accepted)Assert::AreEqual(0,result.bounds.rasterWidth,L"failed certificate grants no geometry");
        }
    }
    TEST_METHOD(MissingOrContradictoryOwnedInkFailsClosed) {
        for(int changed=0;changed<4;++changed) {
            SubtitleEvidenceFixture f;auto ink=std::make_shared<SubtitleInkSnapshot>(*f.observation.ink);
            if(changed==0)ink->rawEvidenceComplete=false;
            if(changed==1)std::fill(ink->ownedInk.begin(),ink->ownedInk.end(),0);
            if(changed==2)std::fill(ink->rawInk.begin(),ink->rawInk.end(),0);
            if(changed==3)ink->sourceRows.clear();
            f.observation.ink=ink;Assert::IsFalse(f.Evaluate().accepted);
        }
    }
};
}