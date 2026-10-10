#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleMeasurementWorkerTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxPreview Ready(uint64_t sequence) {
        SubtitleBoxPreview preview;preview.available=true;
        preview.policyGeneration=11;preview.continuityGeneration=21;
        auto& current=preview.current;
        current.identity=Identity(sequence);current.analyzed=current.barAuthority=true;
        current.width=160;current.height=90;current.pictureTop=12;current.pictureBottom=78;
        current.policyGeneration=11;current.continuityGeneration=21;
        current.sharedPicture.required=true;current.sharedPicture.identity=current.identity;
        current.sharedPicture.bounds={0,12,160,78,160,90};
        auto& text=current.text;text.detected=true;text.lineCount=3;text.cue=1;
        text.bounds={50,50,110,80};text.capturePanelMeasured=true;
        text.capturePanel=text.sourcePanel={40,47,120,82};
        for(int line=0;line<3;++line) {
            text.lineBounds[line]={50,50+12*line,110,56+12*line};
            text.lineCapturePanels[line]=text.capturePanel;text.linePanels[line]=text.sourcePanel;
        }
        preview.text=text;return preview;
    }
    static std::vector<uint16_t> Pixels() {
        std::vector<uint16_t> pixels(160*90*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
        for(int line=0;line<3;++line)for(int y=50+12*line;y<56+12*line;++y)
            for(int x=50;x<110;++x)if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        return pixels;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=160;source.height=90;
        source.rowBytes=source.chromaRowBytes=320;source.format=AnalysisLumaFormat::P010;
        source.generation=2;return source;
    }
    static SubtitleBoxPreview Pending(uint64_t sequence) {
        auto preview=Ready(sequence);preview.available=false;preview.text={};
        preview.current.analyzed=false;preview.current.text={};return preview;
    }
public:
    TEST_METHOD(AcceptedCompleteExtractionSeedsCurrentPixelPendingFallback) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsTrue(pending.current.pendingRefresh);
        Assert::AreEqual(3,pending.current.text.lineCount);
        for(int line=0;line<3;++line)
            Assert::AreEqual(ready.text.lineBounds[line].top,pending.current.text.lineBounds[line].top);
    }
    TEST_METHOD(PartialDetectionPruningAndNarrowCaptureCannotSeedSmallerFallback) {
        for(int scenario=0;scenario<4;++scenario) {
            auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto initial=Ready(1);
            Assert::IsTrue(guard.RememberAccepted(initial,initial.text,Source(pixels),40,250));
            auto partial=Ready(2);const auto accepted=partial.text;
            if(scenario==0)partial.current.text.lineCount=2;
            if(scenario==1)partial.text.lineCount=2;
            if(scenario==2)partial.current.text.capturePanel.top=60;
            if(scenario==3)partial.text.capturePanel.top=60;
            Assert::IsFalse(guard.RememberAccepted(partial,accepted,Source(pixels),40,250));
            auto pending=Pending(3);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(pending.available);
        }
    }
    TEST_METHOD(PendingSeedNeverPromotesUnacceptedDetectorRows) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        auto accepted=ready.text;accepted.lineCount=2;
        accepted.lineBounds[2]={};accepted.lineCapturePanels[2]={};accepted.linePanels[2]={};
        Assert::IsTrue(guard.RememberAccepted(ready,accepted,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual(2,pending.current.text.lineCount);
        Assert::IsFalse(pending.current.text.lineBounds[2].Valid());
    }
    TEST_METHOD(AcceptedSeedStillRejectsChangedCurrentPixels) {
        auto pixels=Pixels();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsFalse(pending.available);
    }
};
}
