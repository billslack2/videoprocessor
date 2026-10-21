#include "pch.h"
#include "CppUnitTest.h"
#include <DisplayRasterInfo.h>
#include <atomic>
#include <thread>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DisplayRasterInfo;
namespace Tests
{
TEST_CLASS(DisplayRasterInfoTests)
{
    static DisplayOutputModeState Mode(uint32_t sourceWidth=1920,uint32_t sourceHeight=1080,
        uint32_t targetWidth=1920,uint32_t targetHeight=1080)
    { return {sourceWidth,sourceHeight,targetWidth,targetHeight,{60000,1001},DisplayOutputScan::Progressive,true}; }
    static std::wstring Row(const Cache& cache,const std::wstring& input=L"1920 x 1080")
    {
        const auto bytes=FormatDetails(cache.Read());
        return FormatResolution(input,std::wstring(bytes.begin(),bytes.end()));
    }
public:
    TEST_METHOD(UnknownModePreservesLegacyRowEvenWithDrawnSize)
    { Cache cache; cache.RecordDrawn(1920,1080); Assert::AreEqual(L"1920 x 1080",Row(cache).c_str()); }
    TEST_METHOD(NativeOutputAddsOnlyRasterWithoutRefresh)
    {
        Cache cache; cache.RecordMode(Mode()); cache.RecordDrawn(1920,1080);
        Assert::AreEqual(L"1920 x 1080 -> 1920x1080",Row(cache).c_str());
        Assert::IsTrue(FormatDetails(cache.Read()).find("Hz")==std::string::npos);
    }
    TEST_METHOD(GpuScalingAndDrawingTagsRepresentDistinctStages)
    {
        Cache cache; cache.RecordMode(Mode(1920,1080,3840,2160)); cache.RecordDrawn(3840,2160);
        Assert::AreEqual(L"1920 x 1080 -> 3840x2160  [drawn 3840x2160]  [GPU-scaled from 1920x1080]",Row(cache).c_str());
    }
    TEST_METHOD(WindowedDrawingSizeIsShownWithoutGpuScalingTag)
    {
        Cache cache; cache.RecordMode(Mode(3840,2160,3840,2160)); cache.RecordDrawn(1344,756);
        Assert::AreEqual(L"1920 x 1080 -> 3840x2160  [drawn 1344x756]",Row(cache).c_str());
    }
    TEST_METHOD(InterlacedTargetIsMarkedAndEmptyInputHasPlaceholder)
    {
        Cache cache; auto mode=Mode(); mode.scan=DisplayOutputScan::InterlacedLower; cache.RecordMode(mode);
        Assert::AreEqual(L"--- -> 1920x1080i",Row(cache,L"").c_str());
    }
    TEST_METHOD(InvalidModesAndSizesFailClosed)
    {
        Cache cache; cache.RecordMode(Mode()); cache.RecordDrawn(-1,1080);
        Assert::AreEqual(L"1920 x 1080 -> 1920x1080",Row(cache).c_str());
        auto unknown=Mode(); unknown.targetKnown=false; cache.RecordMode(unknown);
        Assert::AreEqual(L"1920 x 1080",Row(cache).c_str());
    }
    TEST_METHOD(DisplayChangeAndFailedReadBackDiscardOldMode)
    {
        Cache cache; cache.RecordMode(Mode()); cache.RecordDrawn(1920,1080); cache.Invalidate();
        Assert::AreEqual(L"1920 x 1080",Row(cache).c_str());
        cache.RecordMode(Mode());
        Assert::IsFalse(cache.PublishObservation(cache.Generation(),false,Mode()));
        Assert::AreEqual(L"1920 x 1080",Row(cache).c_str());
    }
    TEST_METHOD(StaleReadCannotResurrectModeAfterNewDisplayNotification)
    {
        Cache cache; const auto old=cache.Generation(); cache.Invalidate();
        Assert::IsFalse(cache.PublishObservation(old,true,Mode()));
        Assert::IsFalse(cache.Read().hasMode);
        Assert::IsTrue(cache.PublishObservation(cache.Generation(),true,Mode(3840,2160,3840,2160)));
        Assert::AreEqual(L"1920 x 1080 -> 3840x2160",Row(cache).c_str());
    }
    TEST_METHOD(RestorationAndRecreationReplaceDrawingDimensions)
    {
        Cache cache; cache.RecordMode(Mode()); cache.RecordDrawn(1920,1080);
        cache.RecordMode(Mode(3840,2160,3840,2160));
        Assert::AreEqual(uint32_t(0),cache.Read().drawnWidth);
        cache.RecordDrawn(1344,756);
        Assert::AreEqual(L"1920 x 1080 -> 3840x2160  [drawn 1344x756]",Row(cache).c_str());
        cache.ClearDrawn(); cache.RecordDrawn(3840,2160);
        Assert::AreEqual(L"1920 x 1080 -> 3840x2160",Row(cache).c_str());
    }
    TEST_METHOD(DetailParserIgnoresMalformedOrUnknownRasterFields)
    {
        for(const auto* bad : {L"0x1080",L"1920x0",L"1920x",L"x1080",L"1920x1080 @ 60Hz",L"4294967296x1080"})
            Assert::AreEqual(L"capture",FormatResolution(L"capture",std::wstring(L"Target | WIRE: ")+bad).c_str());
        Assert::AreEqual(L"capture",FormatResolution(L"capture",L"Target | DESKTOP: 1920x1080 | DRAWN: 3840x2160").c_str());
        Assert::AreEqual(L"capture -> 3840x2160",FormatResolution(L"capture",L"Target | WIRE: 3840x2160 | STATUS: Active | DRAWN: junk").c_str());
    }
    TEST_METHOD(RasterAnnotationsFitNativePanelWithMatchingContinuationCount)
    {
        const auto lines=ResolutionLines(L"4096 x 2160",L"Target | WIRE: 3840x2160 | DESKTOP: 1920x1080 | DRAWN: 3840x2160");
        Assert::AreEqual(size_t(3),lines.size());
        Assert::AreEqual(size_t(1),ResolutionLines(L"1920 x 1080",L"Target").size());
        struct Graphics {
            HDC dc=CreateCompatibleDC(nullptr); HFONT font=CreateFontW(20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
            HGDIOBJ old=nullptr;
            ~Graphics(){if(old) SelectObject(dc,old); if(font) DeleteObject(font); if(dc) DeleteDC(dc);}
        } graphics;
        Assert::IsTrue(graphics.dc && graphics.font);
        graphics.old=SelectObject(graphics.dc,graphics.font);
        for(size_t i=0;i<lines.size();++i) {
            const auto text=(i==0 ? std::wstring(L"Resolution:       ") : std::wstring())+lines[i];
            SIZE size{};
            Assert::IsTrue(GetTextExtentPoint32W(graphics.dc,text.c_str(),static_cast<int>(text.size()),&size)!=FALSE);
            Assert::IsTrue(size.cx<=520,L"Raster annotation clipped by the native 540px panel");
        }
    }
    TEST_METHOD(ConcurrentDrawnUpdatesAreCoherentPairs)
    {
        Cache cache; cache.RecordMode(Mode()); std::atomic_bool bad{false};
        std::thread writer([&] { for(int i=0;i<3000;++i) cache.RecordDrawn(i%2 ? 1920 : 3840,i%2 ? 1080 : 2160); });
        for(int i=0;i<3000;++i) { const auto v=cache.Read();
            if(v.drawnWidth && !((v.drawnWidth==1920 && v.drawnHeight==1080) || (v.drawnWidth==3840 && v.drawnHeight==2160))) bad=true; }
        writer.join(); Assert::IsFalse(bad.load());
    }
};
}
