#include "pch.h"
#include "CppUnitTest.h"
#include <DisplayRasterInfo.h>
#include <OsdTelemetryText.h>
#include "../VideoProcessor-GUI/StatsOverlayWindow.h"
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
            Assert::IsTrue(size.cx<=StatsOverlayLayout::Width-StatsOverlayLayout::Padding*2,
                L"Raster annotation clipped by the native panel");
        }
    }
    TEST_METHOD(NativeBitmapHeightTracksRasterContinuationRows)
    {
        StatsOverlayWindow overlay;
        StatsData stats; stats.isAlphaRenderer=true; stats.resolution=L"1920 x 1080";
        stats.outputMode=L"Target SDR Rec.709 | Req A/A/A/A -> Flip/F/sRGB/709";
        const auto height=[&]() {
            overlay.UpdateStats(stats); std::vector<uint8_t> pixels; int width=0,rows=0,stride=0;
            Assert::IsTrue(overlay.RenderBgra(pixels,width,rows,stride));
            Assert::AreEqual(StatsOverlayLayout::Width,width); Assert::AreEqual(width*4,stride);
            Assert::AreEqual(size_t(stride)*rows,pixels.size()); return rows;
        };
        const int baseline=height();
        stats.outputMode+=L" | WIRE: 3840x2160";
        Assert::AreEqual(baseline,height());
        stats.outputMode+=L" | DRAWN: 3840x2160";
        Assert::AreEqual(baseline+StatsOverlayLayout::LineHeight,height());
        stats.outputMode+=L" | DESKTOP: 1920x1080";
        Assert::AreEqual(baseline+2*StatsOverlayLayout::LineHeight,height());
    }
    TEST_METHOD(NativeBitmapDoesNotAllocateRowsForUnavailableOrMalformedRaster)
    {
        StatsOverlayWindow overlay;
        StatsData stats; stats.isAlphaRenderer=true; stats.resolution=L"1920 x 1080";
        stats.outputMode=L"Target SDR Rec.709 | Req A/A/A/A -> Flip/F/sRGB/709";
        std::vector<uint8_t> pixels; int width=0,baseline=0,stride=0;
        overlay.UpdateStats(stats); Assert::IsTrue(overlay.RenderBgra(pixels,width,baseline,stride));
        stats.outputMode+=L" | WIRE: unavailable | DRAWN: 3840x2160 | DESKTOP: 1920x1080";
        int height=0; overlay.UpdateStats(stats);
        Assert::IsTrue(overlay.RenderBgra(pixels,width,height,stride));
        Assert::AreEqual(baseline,height);
    }
    TEST_METHOD(HostFallbackDoesNotResurrectRasterAfterAuthoritativeInvalidation)
    {
        Cache cache; cache.RecordMode(Mode()); cache.RecordDrawn(1344,756);
        const CString previous(Row(cache).c_str());
        Assert::IsTrue(previous.Find(L"1920x1080") >= 0);
        cache.Invalidate();
        CString queried;
        // The supported renderer getter still runs during graph transitions,
        // and its handled/empty snapshot overrides same-generation host text.
        OsdTelemetryText::ApplyFallback(queried,true,true,&previous);
        Assert::IsTrue(queried.IsEmpty());
        cache.RecordMode(Mode(3840,2160,3840,2160));
        queried = Row(cache).c_str();
        OsdTelemetryText::ApplyFallback(queried,true,true,&previous);
        Assert::IsTrue(queried.Find(L"3840x2160") >= 0);
        Assert::IsTrue(queried.Find(L"drawn") < 0);
    }
    TEST_METHOD(HostFallbackPreservesUnsupportedSameGenerationOnlyAndHonorsLutClear)
    {
        const CString previous(L"Active: old-calibration.cube (65^3)");
        CString queried;
        OsdTelemetryText::ApplyFallback(queried,false,true,&previous);
        Assert::AreEqual(previous.GetString(),queried.GetString());
        queried.Empty();
        OsdTelemetryText::ApplyFallback(queried,false,false,&previous);
        Assert::IsTrue(queried.IsEmpty());
        OsdTelemetryText::ApplyFallback(queried,true,true,&previous);
        Assert::IsTrue(queried.IsEmpty());
        queried = L"Rejected: invalid LUT";
        OsdTelemetryText::ApplyFallback(queried,true,true,&previous);
        Assert::AreEqual(L"Rejected: invalid LUT",queried.GetString());
    }
    TEST_METHOD(FirstHostSnapshotHasNoPreviousTelemetryToDereference)
    {
        for (const bool handled : {false,true})
            for (const bool sameGeneration : {false,true})
            {
                CString queried;
                OsdTelemetryText::ApplyFallback(queried,handled,sameGeneration,nullptr);
                Assert::IsTrue(queried.IsEmpty());
                queried = L"Current snapshot";
                OsdTelemetryText::ApplyFallback(queried,handled,sameGeneration,nullptr);
                Assert::AreEqual(L"Current snapshot",queried.GetString());
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
