#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleBoxDetector.h>
#include <vector>
#include <RendererConfigView.h>
#include <RendererProfileConfig.h>
#include <fstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(SubtitleBoxDetectorTests) {
    static constexpr int W=640,H=360;
    static void Fill(std::vector<uint16_t>& f,int l,int t,int r,int b,int value) {
        for(int y=t;y<b;++y) for(int x=l;x<r;++x) f[y*W+x]=static_cast<uint16_t>(value<<6);
    }
    static std::vector<uint16_t> Frame() {
        std::vector<uint16_t> f(W*H*3/2,static_cast<uint16_t>(512<<6));
        Fill(f,0,0,W,H,64); Fill(f,0,45,W,315,200); return f;
    }
    static void Text(std::vector<uint16_t>& f,int l,int t,int count,int white=510) {
        // Outlined hollow letter forms, with disconnected inter-word spacing.
        for(int n=0;n<count;++n) {
            int x=l+n*13;
            Fill(f,x-1,t-1,x+9,t+15,64);
            Fill(f,x,t,x+8,t+14,white); Fill(f,x+2,t+2,x+6,t+12,64);
        }
    }
    static AnalysisLumaSource Source(const std::vector<uint16_t>& f,uint64_t gen=1) {
        AnalysisLumaSource s; s.data=reinterpret_cast<const uint8_t*>(f.data());
        s.dataBytes=f.size()*2; s.width=W;s.height=H;s.rowBytes=W*2;s.chromaRowBytes=W*2;
        s.format=AnalysisLumaFormat::P010;s.generation=gen;return s;
    }
public:

    TEST_METHOD(DiagnosticFlagLoadsFromRendererRoot) {
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_bbox_test: true\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model; std::string error;
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(loaded);bool enabled=false;
        Assert::IsTrue(RendererConfigView(config).TryGetDisplayBool("subtitle_bbox_test",enabled));
        Assert::IsTrue(enabled);
    }
    TEST_METHOD(DiagnosticFlagStartupValidationAcceptsFalseAndRejectsInvalid) {
        for (const char* value : {"false", "not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_bbox_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            Assert::AreEqual(std::string(value)=="false",RendererProfileConfig::Read(config,model,error));
        }
    }
    TEST_METHOD(NativeRgbAndP210HaveEquivalentBoxes) {
        auto f=Frame();Text(f,200,310,20);SubtitleBoxDetector p010;
        auto expected=p010.Analyze(Source(f),45,315,0,1);Assert::IsTrue(expected.detected);
        std::vector<uint8_t> rgb(W*H*4,255);
        for(int i=0;i<W*H;++i) for(int c=0;c<3;++c) rgb[i*4+c]=static_cast<uint8_t>((f[i]>>6)/4);
        AnalysisLumaSource s;s.data=rgb.data();s.dataBytes=rgb.size();s.width=W;s.height=H;
        s.rowBytes=W*4;s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;
        s.colorspace=ColorSpace::REC_709;s.generation=1;
        SubtitleBoxDetector native;auto actual=native.Analyze(s,45,315,0,1);
        Assert::IsTrue(actual.detected);Assert::AreEqual(expected.bounds.top,actual.bounds.top);
        Assert::AreEqual(expected.bounds.right,actual.bounds.right);
        f.resize(W*H*2,512<<6);s=Source(f);s.format=AnalysisLumaFormat::P210;
        SubtitleBoxDetector p210;actual=p210.Analyze(s,45,315,0,1);
        Assert::IsTrue(actual.detected);Assert::AreEqual(expected.bounds.left,actual.bounds.left);
    }
    TEST_METHOD(ChangedUpperLineIsInspectedWhileBottomLineIsUnchanged) {
        auto f=Frame();Text(f,220,320,14);Text(f,210,295,16);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);Assert::AreEqual(2,first.lineCount);
        f=Frame();Text(f,220,320,14);Text(f,140,295,26);
        auto changed=d.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(changed.detected);Assert::IsTrue(changed.bounds.left<=140);
        Assert::AreNotEqual(first.cue,changed.cue);
    }
    TEST_METHOD(UhdShortTwoGlyphCueAppearsOnFirstFrame) {
        auto lowResolution=Frame();Text(lowResolution,200,320,2);
        const int scale=6,w=W*scale,h=H*scale;
        std::vector<uint16_t> big(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) big[static_cast<size_t>(y)*w+x]=lowResolution[(y/scale)*W+x/scale];
        auto s=Source(big);s.width=w;s.height=h;s.rowBytes=s.chromaRowBytes=w*2;
        SubtitleBoxDetector d;auto r=d.Analyze(s,45*scale,315*scale,1,1);
        Assert::IsTrue(r.detected);Assert::IsTrue(r.bounds.left<=200*scale);
        Assert::IsTrue(r.bounds.right>=221*scale);Assert::IsTrue(r.bounds.bottom>=334*scale);
    }
    TEST_METHOD(FirstFrameIncludesBarLineAndWiderPictureCompanion) {
        auto f=Frame();Text(f,240,320,12);Text(f,150,295,25);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=470 && r.bounds.top<=295 && r.bounds.bottom>=334);
        Assert::AreEqual(uint32_t{1},r.observations);
    }
    TEST_METHOD(StableCueHasNoBoxDriftThroughNoiseAndDuration) {
        auto f=Frame();Text(f,200,310,20);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(first.detected);
        for(int frame=2;frame<120;++frame) {
            auto n=f;for(int y=45;y<290;++y) for(int x=0;x<W;++x) n[y*W+x]=static_cast<uint16_t>((150+(x+frame)%60)<<6);
            auto r=d.Analyze(Source(n),45,315,frame,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(first.cue,r.cue);
            Assert::AreEqual(first.bounds.left,r.bounds.left);Assert::AreEqual(first.bounds.top,r.bounds.top);
            Assert::AreEqual(first.bounds.right,r.bounds.right);Assert::AreEqual(first.bounds.bottom,r.bounds.bottom);
        }
    }
    TEST_METHOD(LatePictureLineIsNotPermanentlyFrozenOut) {
        auto f=Frame();Text(f,220,320,14);SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);Assert::IsTrue(first.detected);
        Text(f,150,295,25);auto next=d.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(next.detected);Assert::IsTrue(next.revised);
        Assert::AreEqual(2,next.lineCount);Assert::IsTrue(next.bounds.top<=295);
    }
    TEST_METHOD(PictureOnlyAndBlankFramesDoNotAcquire) {
        auto f=Frame();Text(f,200,280,20);SubtitleBoxDetector d;
        Assert::IsFalse(d.Analyze(Source(f),45,315,1,1).detected);
        f=Frame();Assert::IsFalse(d.Analyze(Source(f),45,315,2,1).bounds.Valid());
    }
    TEST_METHOD(TopBarAndBoundaryWorkAtLowPqLuma) {
        for(int y: {20,38,310,325}) {
            auto f=Frame();Text(f,200,y,20,410);SubtitleBoxDetector d;
            auto r=d.Analyze(Source(f),45,315,1,1);Assert::IsTrue(r.detected);
            Assert::IsTrue(r.bounds.top<=y && r.bounds.bottom>=y+14);
        }
    }
    TEST_METHOD(HeldMissesAreExplicitAndReleaseIsBounded) {
        auto f=Frame();Text(f,200,320,20);SubtitleBoxDetector d;
        d.Analyze(Source(f),45,315,1,1);f=Frame();
        auto r=d.Analyze(Source(f),45,315,2,1);Assert::IsFalse(r.detected);Assert::IsTrue(r.held);
        d.Analyze(Source(f),45,315,3,1);
        Assert::IsFalse(d.Analyze(Source(f),45,315,4,1).bounds.Valid());
    }
    TEST_METHOD(GenerationOrPictureChangeCannotHoldOldBox) {
        auto f=Frame();Text(f,200,320,20);SubtitleBoxDetector d;
        d.Analyze(Source(f),45,315,1,1);f=Frame();
        Assert::IsFalse(d.Analyze(Source(f,2),45,315,2,1).bounds.Valid());
        Text(f,200,320,20);d.Analyze(Source(f,2),45,315,3,1);
        Assert::IsFalse(d.Analyze(Source(f,2),0,H,4,1).bounds.Valid());
    }
    TEST_METHOD(InputPixelsRemainUntouchedAndRepeatedFrameDoesNotConfirm) {
        auto f=Frame();Text(f,200,320,20);const auto original=f;SubtitleBoxDetector d;
        auto first=d.Analyze(Source(f),45,315,1,1);
        auto repeat=d.Analyze(Source(f),45,315,1,1);
        Assert::AreEqual(first.observations,repeat.observations);Assert::IsTrue(f==original);
    }
}; }
