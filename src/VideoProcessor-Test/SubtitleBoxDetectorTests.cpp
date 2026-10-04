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
    static void JoinedArabicWord(std::vector<uint16_t>& f,int left,int top,
        int width,int height,int white=510) {
        // Script-neutral raster proxy: a joined horizontal body with repeated
        // vertical strokes and detached dots/diacritics. The body is one
        // connected component and is deliberately much wider than 2x height.
        const int joinY=top+height*2/3;
        Fill(f,left,joinY,left+width,joinY+3,white);
        for(int x=left+7;x<left+width-5;x+=20) {
            Fill(f,x,top+2,x+3,top+height-3,white);
            Fill(f,x+8,top+4,x+11,top+height-1,white);
        }
        Fill(f,left+2,top+height/2,left+5,top+height-2,white);
        Fill(f,left+width-5,top+3,left+width-2,top+height/2,white);
        for(int x=left+12;x<left+width-5;x+=31)
            Fill(f,x,top,x+2,top+2,white);
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
    TEST_METHOD(CutPasteFlagLoadsFromRendererRoot) {
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_test: true\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model; std::string error;
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        Assert::IsTrue(loaded);bool enabled=false;
        Assert::IsTrue(RendererConfigView(config).TryGetDisplayBool("subtitle_cut_paste_test",enabled));
        Assert::IsTrue(enabled);
    }
    TEST_METHOD(CutPasteBackgroundModesLoadFromRendererRootAndValidate) {
        for(const char* value:{"rectangle","transparent","blend","black","dark_gray"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_background: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            RendererProfileConfig::Model model;std::string error,mode;
            Assert::IsTrue(loaded && RendererProfileConfig::Read(config,model,error));
            Assert::IsTrue(RendererConfigView(config).TryGetDisplayString("subtitle_cut_paste_background",mode));
            Assert::AreEqual(std::string(value),mode);
        }
        char directory[MAX_PATH]{},path[MAX_PATH]{};
        Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
        Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
        { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_background: neon\n"; }
        ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
        RendererProfileConfig::Model model;std::string error;
        Assert::IsTrue(loaded);
        Assert::IsFalse(RendererProfileConfig::Read(config,model,error),L"unsupported backing must be rejected");
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
    TEST_METHOD(CutPasteFlagStartupValidationAcceptsFalseAndRejectsInvalid) {
        for (const char* value : {"false", "not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer]\nsubtitle_cut_paste_test: "<<value<<"\n"; }
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
        auto lowResolution=Frame();Text(lowResolution,309,320,2);
        const int scale=6,w=W*scale,h=H*scale;
        std::vector<uint16_t> big(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) big[static_cast<size_t>(y)*w+x]=lowResolution[(y/scale)*W+x/scale];
        auto s=Source(big);s.width=w;s.height=h;s.rowBytes=s.chromaRowBytes=w*2;
        SubtitleBoxDetector d;auto r=d.Analyze(s,45*scale,315*scale,1,1);
        Assert::IsTrue(r.detected);Assert::IsTrue(r.bounds.left<=309*scale);
        Assert::IsTrue(r.bounds.right>=330*scale);Assert::IsTrue(r.bounds.bottom>=334*scale);
    }
    TEST_METHOD(NarrowCenteredHiRasterIsNotRejectedByWordAspectRatio) {
        // Rasterized H and dotted i, from a 31x29 antialiased font glyph mask
        // thresholded at half intensity. Its width/height is close to one.
        const uint32_t rows[]={0x00000000,0x1e000000,0x3f06000c,0x3f0f001e,
            0x3e0e001e,0x1c0e001e,0x000e001e,0x000e001e,0x000e001e,0x000e001e,
            0x1e0e001e,0x1e0e001e,0x1e0f001e,0x1e0ffffe,0x1e0ffffe,0x1e0ffffe,
            0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,
            0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0e001e,0x1e0f001e,0x0c06000c,0};
        auto f=Frame();Fill(f,301,308,338,341,64);
        for(int y=0;y<29;++y) for(int x=0;x<31;++x)
            if(rows[y]&(uint32_t{1}<<x)) Fill(f,304+x,310+y,305+x,311+y,510);
        SubtitleBoxDetector detector;auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.left<=305 && r.bounds.top<=311);
        Assert::IsTrue(r.bounds.right>=334 && r.bounds.bottom>=338);
    }
    TEST_METHOD(CenteredJoinedArabicWordCrossingBarIsDetectedAsAFullLine) {
        auto f=Frame();JoinedArabicWord(f,250,306,140,20);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected,L"a wide joined word with actual lower-bar ink is a valid anchor");
        Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.anchor.Valid());
        Assert::IsTrue(r.anchor.top<315 && r.anchor.bottom>315);
        Assert::IsTrue(r.lineBounds[0].left<=250 && r.lineBounds[0].right>=390,
            L"component width limits must not truncate the connected word");
    }
    TEST_METHOD(WiderPictureSideArabicLineUnionsWithNarrowerBarAnchor) {
        auto f=Frame();
        // Two separate connected words make a long RTL line; each individual
        // component remains within the learned per-word height envelope.
        JoinedArabicWord(f,222,277,90,20);
        JoinedArabicWord(f,328,277,90,20);
        JoinedArabicWord(f,265,305,110,20);
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.anchor.Valid() && r.anchor.top<315 && r.anchor.bottom>315);
        Assert::IsTrue(r.bounds.left<=222 && r.bounds.right>=418,
            L"the bar-crossing line is a minimum span; a wider companion line must expand the union");
    }
    TEST_METHOD(PictureOnlyJoinedTextCannotUseOffCenterBarTitleAsAnchor) {
        auto f=Frame();
        JoinedArabicWord(f,250,277,140,20); // centered, but wholly in the picture
        JoinedArabicWord(f,18,306,120,20);  // genuine bar crossing, but corner title
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected,
            L"picture-side text and an unrelated off-center bar title cannot create a subtitle cue");
        Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(LongChangedPictureLineRetainsDistinctDensityWithSameBarCompanion) {
        const int w=960,h=540;
        for(int count : {22,40,65}) {
            auto frame=[&](bool changed){
                std::vector<uint16_t> pixels(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    pixels[y*w+x]=(y>=68 && y<472?200:64)<<6;
                auto word=[&](int y,int letters,bool hShape){
                    const int left=(w-(letters-1)*13-8)/2;
                    for(int n=0;n<letters;++n) for(int yy=-1;yy<=14;++yy) for(int xx=-1;xx<=8;++xx) {
                        const bool stroke=xx>=0 && xx<8 && yy>=0 && yy<14 &&
                            (xx<2 || xx>=6 || (hShape?(yy>=6 && yy<8):(yy<2 || yy>=12)));
                        pixels[(y+yy)*w+left+n*13+xx]=(stroke?510:64)<<6;
                    }
                };
                word(480,20,false);word(442,count,changed);return pixels;
            };
            auto oldPixels=frame(false),newPixels=frame(true);
            auto source=[&](const std::vector<uint16_t>& pixels){
                auto s=Source(pixels);s.width=w;s.height=h;s.rowBytes=s.chromaRowBytes=w*2;return s;
            };
            SubtitleBoxDetector beforeDetector,afterDetector;
            const auto before=beforeDetector.Analyze(source(oldPixels),68,472,1,1);
            const auto after=afterDetector.Analyze(source(newPixels),68,472,2,1);
            Assert::IsTrue(before.detected && after.detected);
            Assert::AreEqual(2,before.lineCount);Assert::AreEqual(2,after.lineCount);
            Assert::AreEqual(before.bounds.left,after.bounds.left);
            Assert::AreEqual(before.bounds.right,after.bounds.right);
            Assert::IsTrue(before.lineSignatures[0]==after.lineSignatures[0]);
            int difference=0,total=0;
            for(size_t i=0;i<before.lineSignatures[1].size();++i) {
                const int a=before.lineSignatures[1][i],b=after.lineSignatures[1][i];
                difference+=std::abs(a-b);total+=std::max(a,b);
            }
            Assert::IsTrue(difference*100>total*25);
        }
    }
    TEST_METHOD(NarrowPictureShapeCannotJoinValidBarSubtitle) {
        auto f=Frame();Text(f,200,320,20);
        for(int x : {310,323}) {
            Fill(f,x-1,284,x+9,310,64);
            Fill(f,x,285,x+8,309,510);Fill(f,x+2,287,x+6,307,64);
        }
        SubtitleBoxDetector detector;const auto r=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.top>=315 && r.bounds.bottom>=334);
    }
    TEST_METHOD(IsolatedCornerTitlesDoNotQualifyInEitherBar) {
        for(int left : {20,520}) for(int top : {20,325}) {
            auto f=Frame();Text(f,left,top,8);SubtitleBoxDetector d;
            auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
        }
    }
    TEST_METHOD(DiagnosticFlagNamedDisplayProfilePreservesBooleansAndRejectsInvalid) {
        for(const char* value : {"true","false","not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer.profile_1]\nsubtitle_bbox_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            const bool valid=std::string(value)!="not-a-bool";
            Assert::AreEqual(valid,RendererProfileConfig::Read(config,model,error));
            if(valid) {
                const auto profile=model.profiles.find("display.profile_1");
                Assert::IsTrue(profile!=model.profiles.end());
                const auto setting=profile->second.settings.find("subtitle_bbox_test");
                Assert::IsTrue(setting!=profile->second.settings.end());
                Assert::AreEqual(std::string(value),setting->second);
            }
        }
    }
    TEST_METHOD(CutPasteFlagNamedDisplayProfilePreservesBooleansAndRejectsInvalid) {
        for(const char* value : {"true","false","not-a-bool"}) {
            char directory[MAX_PATH]{},path[MAX_PATH]{};
            Assert::IsTrue(GetTempPathA(MAX_PATH,directory)!=0);
            Assert::IsTrue(GetTempFileNameA(directory,"vpb",0,path)!=0);
            { std::ofstream file(path);file<<"[vprenderer.profile_1]\nsubtitle_cut_paste_test: "<<value<<"\n"; }
            ConfigFile config;const bool loaded=config.Load(path);DeleteFileA(path);
            Assert::IsTrue(loaded);
            RendererProfileConfig::Model model;std::string error;
            const bool valid=std::string(value)!="not-a-bool";
            Assert::AreEqual(valid,RendererProfileConfig::Read(config,model,error));
            if(valid) {
                const auto profile=model.profiles.find("display.profile_1");
                Assert::IsTrue(profile!=model.profiles.end());
                const auto setting=profile->second.settings.find("subtitle_cut_paste_test");
                Assert::IsTrue(setting!=profile->second.settings.end());
                Assert::AreEqual(std::string(value),setting->second);
            }
        }
    }
    TEST_METHOD(CornerTitleCannotSuppressShortCenteredSubtitle) {
        for(int left : {20,455}) {
            auto f=Frame();Text(f,left,20,12);Text(f,309,325,2);
            SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
            Assert::IsTrue(r.bounds.left<=309 && r.bounds.right>=330);
            Assert::IsTrue(r.bounds.top>=315 && r.bounds.bottom>=339);
        }
    }
    TEST_METHOD(UpperBoundaryAnchorIncludesLowerBarCompanionOnFirstFrame) {
        auto f=Frame();Text(f,150,307,26);Text(f,240,333,12);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
        Assert::IsTrue(r.bounds.top<=307 && r.bounds.bottom>=347);
        Assert::AreEqual(uint32_t{1},r.observations);
    }
    TEST_METHOD(TwoBarLinesAreCompleteRegardlessOfStrongerAnchor) {
        for(bool upperIsWider : {false,true}) {
            auto f=Frame();
            Text(f,upperIsWider?150:240,317,upperIsWider?26:12);
            Text(f,upperIsWider?240:150,341,upperIsWider?12:26);
            SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
            Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
            Assert::IsTrue(r.bounds.top<=317 && r.bounds.bottom>=355);
        }
    }
    TEST_METHOD(TopBoundaryAnchorIncludesUpperBarCompanionOnFirstFrame) {
        auto f=Frame();Text(f,240,12,12);Text(f,150,38,26);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.left<=150 && r.bounds.right>=483);
        Assert::IsTrue(r.bounds.top<=12 && r.bounds.bottom>=52);
    }
    TEST_METHOD(CompanionCannotChainAnotherPictureLineIntoTheBox) {
        auto f=Frame();Text(f,200,320,20);Text(f,240,283,12);Text(f,240,246,12);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(2,r.lineCount);
        Assert::IsTrue(r.bounds.top<=283 && r.bounds.top>260);
        Assert::IsTrue(r.bounds.bottom>=334);
    }
    TEST_METHOD(FarSideLabelCannotJoinWideCenteredSubtitle) {
        auto f=Frame();Text(f,100,320,34);Text(f,70,342,8);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(1,r.lineCount);
        Assert::IsTrue(r.bounds.left<=100 && r.bounds.left>70);
        Assert::IsTrue(r.bounds.bottom>=334 && r.bounds.bottom<342);
    }
    TEST_METHOD(QueuedObservationKeepsBarAnchorEvidenceSeparateFromCompanion) {
        auto f=Frame();Text(f,240,320,12);SubtitleBoxDetector firstDetector;
        const auto first=firstDetector.Analyze(Source(f),45,315,1,1);
        Text(f,150,295,25);SubtitleBoxDetector nextDetector;
        const auto next=nextDetector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(first.detected && next.detected);
        Assert::AreEqual(first.anchor.left,next.anchor.left);
        Assert::AreEqual(first.anchor.top,next.anchor.top);
        Assert::AreEqual(first.anchor.right,next.anchor.right);
        Assert::AreEqual(first.anchor.bottom,next.anchor.bottom);
        Assert::IsTrue(first.anchorSignature==next.anchorSignature);
        Assert::IsTrue(first.signature!=next.signature);
        Assert::IsTrue(next.bounds.top<next.anchor.top);
    }
    TEST_METHOD(QueuedLineEvidenceSurvivesStrongerSecondLineChangingTheAnchor) {
        auto f=Frame();Text(f,240,307,12);SubtitleBoxDetector firstDetector;
        const auto first=firstDetector.Analyze(Source(f),45,315,1,1);
        Text(f,150,333,26);SubtitleBoxDetector nextDetector;
        const auto next=nextDetector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(first.detected && next.detected);Assert::AreEqual(2,next.lineCount);
        Assert::AreNotEqual(first.anchor.top,next.anchor.top);
        Assert::AreEqual(first.lineBounds[0].top,next.lineBounds[1].top);
        Assert::AreEqual(first.lineBounds[0].left,next.lineBounds[1].left);
        Assert::IsTrue(first.lineSignatures[0]==next.lineSignatures[1]);
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
    TEST_METHOD(DetectorMissStillPublishesMatchingInkPlanes) {
        auto f=Frame();Fill(f,320,320,321,321,510);
        SubtitleBoxDetector detector;
        Assert::IsFalse(detector.Analyze(Source(f),45,315,1,1).detected);
        const auto ink=detector.InkSnapshot();
        Assert::IsTrue(ink!=nullptr);
        Assert::IsTrue(!ink->rawInk.empty());
        Assert::IsTrue(ink->rawInk.size()==ink->ownedInk.size(),
            L"a grouping miss must leave a complete ownership plane for presentation");
    }
    TEST_METHOD(PictureLineCannotBorrowRejectedBarPixelsInsideItsEnvelope) {
        auto f=Frame();Text(f,200,300,20);
        // One tiny accepted fragment has only two pixels in the bar. Isolated
        // one-pixel noise must not supply the remaining bar proof for the line.
        Fill(f,210,313,212,316,510);
        for(int x : {221,234,247,260,273,286,299,312}) Fill(f,x,315,x+1,316,510);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(PaddingIntoBarDoesNotMakePictureTextEligible) {
        auto f=Frame();Text(f,200,300,20);
        // Bright isolated bar pixels elsewhere allow threshold estimation but
        // do not belong to this picture-only cue (whose padding crosses y=315).
        for(int x=10;x<50;x+=3) Fill(f,x,340,x+1,341,510);
        SubtitleBoxDetector d;auto r=d.Analyze(Source(f),45,315,1,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(PartialSampledTopBarRowQualifiesOnFirstFrame) {
        const int w=1920,h=1080,top=137,bottom=945;
        std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom ? 200:64)<<6;
        // Only the first source row of each letter is inside the real top bar.
        // Sampling at y=136 must count it, despite the unaligned boundary y=137.
        for(int n=0;n<20;++n) for(int y=0;y<42;++y) for(int x=0;x<24;++x)
            f[static_cast<size_t>(136+y)*w+600+n*39+x]=
                (x<6 || x>=18 || y<6 || y>=36 ? 510:64)<<6;
        auto source=Source(f);source.width=w;source.height=h;
        source.rowBytes=source.chromaRowBytes=w*2;
        SubtitleBoxDetector d;auto r=d.Analyze(source,top,bottom,1,1);
        Assert::IsTrue(r.detected);Assert::AreEqual(uint32_t{1},r.observations);
        Assert::IsTrue(r.bounds.top<=136 && r.bounds.bottom>=178);
    }
    TEST_METHOD(FarSideTitleCannotPoisonShortCenteredSubtitleGlyphScale) {
        for(bool right:{false,true}) for(int height:{10,24}) {
            auto f=Frame();const int width=height*3/4;
            const int left=(640-(width*2+4))/2;
            for(int n=0;n<2;++n) {
                const int x=left+n*(width+4);
                Fill(f,x,320,x+width,320+height,510);
                Fill(f,x+2,322,x+width-2,318+height,64);
            }
            SubtitleBoxDetector detector;
            const auto withoutTitle=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(withoutTitle.detected);
            const int titleHeight=height==10?24:10,titleWidth=titleHeight/2;
            const int titleLeft=right?630-(titleWidth+3)*10:10;
            for(int n=0;n<10;++n) {
                const int x=titleLeft+n*(titleWidth+3);
                Fill(f,x,10,x+titleWidth,10+titleHeight,510);
                Fill(f,x+1,11,x+titleWidth-1,9+titleHeight,64);
            }
            detector.Reset();
            const auto result=detector.Analyze(Source(f),45,315,2,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
            Assert::AreEqual(withoutTitle.bounds.left,result.bounds.left);
            Assert::AreEqual(withoutTitle.bounds.top,result.bounds.top);
            Assert::AreEqual(withoutTitle.bounds.right,result.bounds.right);
            Assert::AreEqual(withoutTitle.bounds.bottom,result.bounds.bottom);
        }
    }
    TEST_METHOD(CenteredScaleGroupPreservesOuterLettersOfWideCaption) {
        auto f=Frame();Text(f,50,320,40);
        for(int n=0;n<10;++n) {
            const int x=10+n*8;
            Fill(f,x,15,x+5,25,510);Fill(f,x+1,16,x+4,24,64);
        }
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.left<=50 && result.bounds.right>=565);
    }
    TEST_METHOD(OneTinyDescenderBarSampleSurvivesEveryUhdSamplingPhase) {
        const int w=3840,h=2160;
        for(bool atTop:{false,true}) for(int phase=0;phase<4;++phase)
            for(bool entersBar:{false,true}) {
                const int top=276+phase,bottom=h-top;
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                const int glyphTop=atTop?top+4:bottom-40;
                for(int n=0;n<20;++n) for(int y=0;y<36;++y) for(int x=0;x<32;++x)
                    f[static_cast<size_t>(glyphTop+y)*w+1450+n*48+x]=
                        (x<8 || x>=24 || y<8 || y>=28?510:64)<<6;
                const int tailLeft=atTop?1450:1478;
                const int tailTop=atTop?top-(entersBar?1:0):glyphTop+36;
                const int tailBottom=atTop?glyphTop:bottom+(entersBar?1:0);
                for(int y=tailTop;y<tailBottom;++y) for(int x=tailLeft;x<tailLeft+4;++x)
                    f[static_cast<size_t>(y)*w+x]=510<<6;
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                Assert::AreEqual(entersBar,detector.Analyze(source,top,bottom,1,1).detected);
            }
    }
    TEST_METHOD(NarrowTwoGlyphCueUsesProvenSingleDescenderBarSample) {
        const int w=3840,h=2160;
        for(bool atTop:{false,true}) for(int phase=0;phase<4;++phase) {
            const int top=276+phase,bottom=h-top;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
            const int glyphTop=atTop?top+4:bottom-40;
            for(int n=0;n<2;++n) for(int y=0;y<36;++y) for(int x=0;x<16;++x)
                f[static_cast<size_t>(glyphTop+y)*w+1900+n*20+x]=
                    (x<4 || x>=12 || y<8 || y>=28?510:64)<<6;
            const int tailLeft=atTop?1900:1912;
            for(int y=atTop?top-1:glyphTop+36;y<(atTop?glyphTop:bottom+1);++y)
                for(int x=tailLeft;x<tailLeft+4;++x) f[static_cast<size_t>(y)*w+x]=510<<6;
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(source,top,bottom,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        }
    }
    TEST_METHOD(IsolatedTinyBarNoiseCannotAuthorizePictureOnlyLine) {
        const int w=3840,h=2160;
        for(int phase=0;phase<4;++phase) {
            const int top=276+phase,bottom=h-top;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
            for(int n=0;n<20;++n) for(int y=0;y<36;++y) for(int x=0;x<32;++x)
                f[static_cast<size_t>(bottom-52+y)*w+1450+n*48+x]=
                    (x<8 || x>=24 || y<8 || y>=28?510:64)<<6;
            for(int x=1490;x<1494;++x) f[static_cast<size_t>(bottom)*w+x]=510<<6;
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            Assert::IsFalse(detector.Analyze(source,top,bottom,1,1).detected);
        }
    }
    TEST_METHOD(DetachedQuestionMarkHeadAndDotJoinTheSameTextLine) {
        auto f=Frame();Text(f,200,320,12);
        Fill(f,352,320,370,322,510);Fill(f,352,322,354,325,510);
        Fill(f,368,322,370,326,510);Fill(f,356,325,370,327,510);
        Fill(f,356,327,358,329,510);Fill(f,356,332,358,334,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.lineBounds[0].right>=370 && result.lineBounds[0].bottom>=334);
    }
    TEST_METHOD(QuestionMarkCannotBorrowAChainOfLowerNoiseDots) {
        auto f=Frame();Text(f,200,320,12);
        Fill(f,352,320,370,322,510);Fill(f,352,322,354,325,510);
        Fill(f,368,322,370,326,510);Fill(f,356,325,370,327,510);
        Fill(f,356,327,358,329,510);Fill(f,356,332,358,334,510);
        SubtitleBoxDetector detector;
        const auto clean=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(clean.detected);
        Fill(f,356,338,358,340,510);Fill(f,356,344,358,346,510);
        detector.Reset();
        const auto result=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::AreEqual(clean.bounds.right,result.bounds.right);
        Assert::AreEqual(clean.bounds.bottom,result.bounds.bottom);
    }
    TEST_METHOD(CompanionCentersFollowTheCueRatherThanTheRasterCenterAlone) {
        auto f=Frame();Text(f,186,320,21);Text(f,240,300,2);
        SubtitleBoxDetector detector;
        const auto offCenter=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(offCenter.detected);Assert::AreEqual(1,offCenter.lineCount);
        Assert::IsTrue(offCenter.bounds.top>=316);
        f=Frame();Text(f,186,320,21);Text(f,164,294,28);
        detector.Reset();
        const auto legitimate=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(legitimate.detected);Assert::AreEqual(2,legitimate.lineCount);
        Assert::IsTrue(legitimate.bounds.top<=294 && legitimate.bounds.right>=523);
    }
    TEST_METHOD(ThinPictureGlyphNativeCoresSurviveEveryUhdPhaseAndLowPq) {
        const int w=3840,h=2160,top=276,bottom=1884;
        for(int white:{180,700}) for(int phase=0;phase<4;++phase) {
            const int fringe=white==180?160:600;
            std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?(white==180?90:200):64)<<6;
            for(int line=0;line<2;++line) for(int n=0;n<20;++n)
                for(int y=0;y<48;++y) for(int x=0;x<32;++x) {
                    const bool stroke=x<8 || x>=24 || y<8 || y>=40;
                    const int value=!stroke?64:line==0 || x%4==1?white:fringe;
                    const int glyphTop=line==0?1900:1818;
                    f[static_cast<size_t>(glyphTop+y)*w+1450+phase+n*48+x]=value<<6;
                }
            auto source=Source(f);source.width=w;source.height=h;
            source.rowBytes=source.chromaRowBytes=w*2;
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(source,top,bottom,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
            Assert::IsTrue(result.bounds.top<=1818 && result.bounds.bottom>=1948);
        }
    }
    TEST_METHOD(FlatGrayPicturePrefixCannotExtendFullBrightnessCaption) {
        const int w=3840,h=2160,top=276,bottom=1884;
        std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
        auto text=[&](int left,int glyphTop,int count,int white) {
            for(int n=0;n<count;++n) for(int y=0;y<48;++y) for(int x=0;x<32;++x)
                f[static_cast<size_t>(glyphTop+y)*w+left+n*48+x]=
                    (x<8 || x>=24 || y<8 || y>=40?white:64)<<6;
        };
        text(1450,1900,20,700);text(1660,1818,12,700);
        text(1200,1818,9,600); // Same baseline and color, but no native white core.
        auto source=Source(f);source.width=w;source.height=h;
        source.rowBytes=source.chromaRowBytes=w*2;
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(source,top,bottom,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.top<=1818 && result.bounds.left>=1440);
    }
    TEST_METHOD(EqualBrightnessPicturePrefixNeedsTheLearnedSubtitleCoreColor) {
        for(bool coloredSubtitle:{false,true}) {
            auto f=Frame();Text(f,230,320,14,983);Text(f,240,295,12,983);
            auto color=[&](int left,int top,int right,int bottom,int u,int v) {
                for(int y=top/2;y<(bottom+1)/2;++y) for(int x=left/2;x<(right+1)/2;++x) {
                    f[W*H+y*W+x*2]=static_cast<uint16_t>(u<<6);
                    f[W*H+y*W+x*2+1]=static_cast<uint16_t>(v<<6);
                }
            };
            if(coloredSubtitle) {
                color(230,320,407,334,475,441);
                color(240,295,391,309,475,441);
            }
            Text(f,205,295,2,983);
            // Equal luma and within the loose antialias mask tolerance. This
            // color is either unrelated scenery or the actual learned text.
            color(205,295,226,309,475,441);
            SubtitleBoxDetector detector;
            const auto result=detector.Analyze(Source(f),45,315,1,1);
            Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
            if(coloredSubtitle) Assert::IsTrue(result.bounds.left<=205);
            else Assert::IsTrue(result.bounds.left>=226);
        }
    }
    TEST_METHOD(NativeTerminalDotRecoversQuestionHeadAcrossUhdSamplingPhases) {
        const int w=3840,h=2160,top=276,bottom=1884;
        for(int phaseX=0;phaseX<4;++phaseX) for(int phaseY=0;phaseY<4;++phaseY)
            for(bool nativeDot:{false,true}) {
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                auto fill=[&](int left,int ytop,int right,int ybottom,int code) {
                    for(int y=ytop;y<ybottom;++y) for(int x=left;x<right;++x)
                        f[static_cast<size_t>(y)*w+x]=code<<6;
                };
                const int glyphTop=1900+phaseY;
                for(int n=0;n<12;++n) {
                    const int x=1600+phaseX+n*48;
                    fill(x,glyphTop,x+32,glyphTop+48,700);
                    fill(x+8,glyphTop+8,x+24,glyphTop+40,64);
                }
                const int headLeft=2172+phaseX;
                fill(headLeft,glyphTop,headLeft+32,glyphTop+8,700);
                fill(headLeft+24,glyphTop+8,headLeft+32,glyphTop+20,700);
                fill(headLeft+8,glyphTop+16,headLeft+32,glyphTop+24,700);
                fill(headLeft+8,glyphTop+24,headLeft+12,glyphTop+28,700);
                if(nativeDot) fill(headLeft+9,glyphTop+46,headLeft+10,glyphTop+47,700);
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                const auto result=detector.Analyze(source,top,bottom,1,1);
                Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
                if(nativeDot) Assert::IsTrue(result.lineBounds[0].right>=headLeft+32);
                else Assert::IsTrue(result.lineBounds[0].right<headLeft);
            }
    }
    TEST_METHOD(TinyTrailingPeriodJoinsConfirmedLettersButCannotSeedText) {
        auto f=Frame();Text(f,200,308,12);
        Fill(f,355,320,356,322,510); // Two analysis pixels; not a letter component.
        SubtitleBoxDetector detector;
        auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.right>=356);
        f=Frame();
        for(int x=200;x<400;x+=8) Fill(f,x,320,x+1,322,510);
        Assert::IsFalse(detector.Analyze(Source(f),45,315,2,1).detected);
    }
    TEST_METHOD(UnassignedBrightShapeInsideLineDoesNotChangeItsSignature) {
        auto f=Frame();Text(f,180,320,5);Text(f,254,320,5);
        SubtitleBoxDetector detector;const auto first=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(first.detected);
        // This accepted small component lies in a word space, but its shape
        // belongs to neither the core letters nor the bounded punctuation set.
        Fill(f,242,324,252,327,510);
        const auto second=detector.Analyze(Source(f),45,315,2,1);
        Assert::IsTrue(second.detected);
        Assert::IsTrue(first.lineSignatures==second.lineSignatures);
    }
    TEST_METHOD(SmallerXHeightPictureLineCanJoinCapitalHeightBarLine) {
        auto f=Frame();Text(f,220,320,15);
        for(int n=0;n<25;++n) {
            const int x=200+n*8;
            Fill(f,x,295,x+5,302,510);Fill(f,x+1,296,x+4,301,64);
        }
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.top<=295);
    }
    TEST_METHOD(TrailingSpecksCannotChainBeyondOriginalLetterExtent) {
        auto f=Frame();Text(f,200,308,12);
        for(int x=355;x<500;x+=4) Fill(f,x,320,x+1,322,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.right>=356);
        Assert::IsTrue(result.bounds.right<=363);
    }
    TEST_METHOD(PictureHighlightCannotChainIntoBarLettersAtAnotherBaseline) {
        auto f=Frame();Text(f,220,308,18);
        // Thin bright picture shape near the letters, but on a higher baseline.
        for(int x=168;x<204;++x) Fill(f,x,300,x+1,302,510);
        Fill(f,168,292,170,302,510);
        SubtitleBoxDetector detector;const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.left>=216);
    }
    TEST_METHOD(DisconnectedLetterStrokeGroupsRejoinTheSameTextRow) {
        auto f=Frame();Text(f,176,320,15);
        Text(f,200,295,5);
        Fill(f,266,295,269,302,510); // Upper stroke starts its own short group.
        Text(f,270,295,6); // Following letters establish the same baseline.
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.bounds.left<=200 && result.bounds.right>=343);
    }
    TEST_METHOD(WordSpaceLargerThanLetterHeightStillFormsOneLine) {
        auto f=Frame();Text(f,220,320,5);Text(f,300,320,5);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.bounds.left<=220 && result.bounds.right>=360);
    }
    TEST_METHOD(WideWordSpaceKeepsBothSubtitleWordsInOneLine) {
        // A renderer may leave nearly four glyph heights between words. The
        // line must retain both extents while the genuine bar strokes remain
        // the acquisition anchor.
        auto f=Frame();Text(f,180,320,5);Text(f,295,320,5);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(1,result.lineCount);
        Assert::IsTrue(result.anchor.Valid());
        Assert::IsTrue(result.bounds.left<=180 && result.bounds.right>=355,
            L"a broad but baseline-aligned word gap must not truncate the subtitle line");
    }
    TEST_METHOD(BrightPictureSubtitleCompanionKeepsFullWidthFromBarAnchor) {
        auto f=Frame();
        // Simulate a bright stairway/background. The dark outline remains a
        // local glyph cue, while the lower row supplies real black-bar ink.
        Fill(f,0,45,W,315,470);
        Text(f,195,288,20);Text(f,240,320,12);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::AreEqual(2,result.lineCount);
        Assert::IsTrue(result.anchor.Valid() && result.anchor.bottom>315,
            L"the independently detected lower subtitle row must remain the bar anchor");
        Assert::IsTrue(result.bounds.left<=195,
            (L"bright-picture box left="+std::to_wstring(result.bounds.left)).c_str());
        Assert::IsTrue(result.bounds.right>=447,L"bright-picture companion right extent was lost");
        Assert::IsTrue(result.bounds.top<=288,L"bright-picture companion top extent was lost");
        Assert::IsTrue(result.bounds.bottom>=334,L"bar anchor right extent was lost");
    }
    TEST_METHOD(WideLowPictureHighlightCannotExtendRealLetters) {
        auto f=Frame();Text(f,240,308,18);
        Fill(f,211,314,232,316,510);Fill(f,211,308,213,316,510);
        SubtitleBoxDetector detector;
        const auto result=detector.Analyze(Source(f),45,315,1,1);
        Assert::IsTrue(result.detected);Assert::IsTrue(result.bounds.left>=236);
    }
    TEST_METHOD(OneSourceRowOfBarInkSurvivesEveryUhdSamplingPhase) {
        const int w=3840,h=2160;
        for(bool atTop : {false,true}) for(int phase=0;phase<4;++phase)
            for(bool entersBar : {false,true}) {
                const int top=276+phase,bottom=h-top;
                std::vector<uint16_t> f(static_cast<size_t>(w)*h*3/2,512<<6);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                    f[static_cast<size_t>(y)*w+x]=(y>=top && y<bottom?200:64)<<6;
                const int glyphTop=atTop ? top-(entersBar?1:0) : bottom-48+(entersBar?1:0);
                for(int n=0;n<20;++n) for(int y=0;y<48;++y) for(int x=0;x<32;++x)
                    f[static_cast<size_t>(glyphTop+y)*w+1450+n*48+x]=
                        (x<8 || x>=24 || y<8 || y>=40?510:64)<<6;
                auto source=Source(f);source.width=w;source.height=h;
                source.rowBytes=source.chromaRowBytes=w*2;
                SubtitleBoxDetector detector;
                const auto result=detector.Analyze(source,top,bottom,1,1);
                Assert::AreEqual(entersBar,result.detected);
                if(entersBar) {
                    Assert::IsTrue(result.bounds.top<=glyphTop);
                    Assert::IsTrue(result.bounds.bottom>=glyphTop+48);
                }
            }
    }
    TEST_METHOD(LosingBarLineImmediatelyClearsBoxDespiteRemainingPictureLine) {
        auto f=Frame();Text(f,240,320,12);Text(f,150,295,25);
        SubtitleBoxDetector d;Assert::IsTrue(d.Analyze(Source(f),45,315,1,1).detected);
        Fill(f,0,315,W,H,64);
        auto r=d.Analyze(Source(f),45,315,2,1);
        Assert::IsFalse(r.detected);Assert::IsFalse(r.held);Assert::IsFalse(r.bounds.Valid());
    }
    TEST_METHOD(TopBarAndBoundaryWorkAtLowPqLuma) {
        for(int y: {20,38,310,325}) {
            auto f=Frame();Text(f,200,y,20,410);SubtitleBoxDetector d;
            auto r=d.Analyze(Source(f),45,315,1,1);Assert::IsTrue(r.detected);
            Assert::IsTrue(r.bounds.top<=y && r.bounds.bottom>=y+14);
        }
    }
    TEST_METHOD(HoldRequiresCurrentBarAnchorAndRemainsBounded) {
        auto f=Frame();Text(f,200,320,20);SubtitleBoxDetector d;
        d.Analyze(Source(f),45,315,1,1);
        // Too many companion proposals cannot authorize new geometry. A short
        // diagnostic hold is allowed only while the original bar anchor exists.
        Text(f,200,300,20);Text(f,200,284,20);Text(f,200,268,20);
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
