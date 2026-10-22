#include "pch.h"
#include "CppUnitTest.h"
#include <microsoft_directshow/video_renderers/MadVROsdSizing.h>
#include <microsoft_directshow/video_renderers/MadVROsdRefresh.h>
#include <vprenderer/NativeStatsOverlayPlacement.h>
#include <vprenderer/AnamorphicPresentation.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
TEST_CLASS(MadVROsdSizingTests)
{
    static MadVROsdSizing::Mapping Fullscreen(double width = 3840, double height = 2160)
    {
        MadVROsdSizing::Mapping mapping;
        mapping.monitorWidth = mapping.physicalHostWidth = mapping.logicalHostWidth =
            mapping.logicalChildWidth = mapping.osdWidth = width;
        mapping.monitorHeight = mapping.physicalHostHeight = mapping.logicalHostHeight =
            mapping.logicalChildHeight = mapping.osdHeight = height;
        mapping.fullscreen = true;
        mapping.unmaskedOutputWidth = width; mapping.unmaskedOutputHeight = height;
        return mapping;
    }
    static NativeStatsOverlayPlacement::StatsSizing Size(const MadVROsdSizing::Mapping& mapping,
        bool enabled, bool configured, double aspect)
    {
        const auto screen = MadVROsdSizing::Resolve(mapping, configured, aspect);
        return NativeStatsOverlayPlacement::ResolveStatsSizing(enabled && screen.valid,
            screen.fittedHeight, screen.outputHeight);
    }
public:
    TEST_METHOD(FullscreenOpticalZoomPreservesProjectedStatsSizeAndInset)
    {
        for (const double height : {1080.0, 2160.0})
        {
            const auto mapping = Fullscreen(height * 16.0 / 9.0, height);
            const auto standard = Size(mapping, true, true, 16.0 / 9.0);
            const auto scope = Size(mapping, true, true, 2.35);
            const double zoom = 2.35 / (16.0 / 9.0);
            Assert::AreEqual(double(standard.scale), scope.scale * zoom, 0.00001);
            Assert::AreEqual(double(standard.insetPixels), scope.insetPixels * zoom, 0.00001);
        }
    }
    TEST_METHOD(OutputResolutionChangesRasterUnitsButKeepsRelativePhysicalSize)
    {
        const auto hd = Size(Fullscreen(1920,1080), true, true, 2.35);
        const auto uhd = Size(Fullscreen(), true, true, 2.35);
        Assert::AreEqual(double(hd.scale * 2.0f), double(uhd.scale), 0.00001);
        Assert::AreEqual(double(hd.insetPixels * 2.0f), double(uhd.insetPixels), 0.00001);
    }
    TEST_METHOD(WindowedNlsCanvasIsNotFittedTwice)
    {
        auto mapping = Fullscreen();
        // A scope NLS child has already been reduced by SetWindowPosition.
        // The monitor remains16:9, and the OSD's full rect is this child canvas.
        mapping.fullscreen = false;
        mapping.physicalHostWidth = mapping.logicalHostWidth = 1920;
        mapping.physicalHostHeight = mapping.logicalHostHeight = 1080;
        mapping.logicalChildWidth = mapping.osdWidth = 1920;
        mapping.logicalChildHeight = mapping.osdHeight = 817;
        const auto windowed = Size(mapping, true, true, 2.35);
        const auto host = Size(Fullscreen(1920,1080), true, true, 2.35);
        Assert::AreEqual(double(host.scale), double(windowed.scale), 0.00001);
        Assert::AreEqual(double(host.insetPixels), double(windowed.insetPixels), 0.00001);
    }
    TEST_METHOD(WindowReferenceTracksHostCanvasBeforeMovieCrop)
    {
        const auto reference = Size(Fullscreen(), true, true, 2.35);
        for (const double width : {960.0, 1440.0, 2560.0})
        {
            auto mapping = Fullscreen(); mapping.fullscreen = false;
            mapping.physicalHostWidth = mapping.logicalHostWidth = mapping.logicalChildWidth = mapping.osdWidth = width;
            mapping.physicalHostHeight = mapping.logicalHostHeight = mapping.logicalChildHeight = mapping.osdHeight = width * 9.0 / 16.0;
            const auto actual = Size(mapping, true, true, 2.35);
            Assert::AreEqual(double(reference.scale) * width / 3840.0, double(actual.scale), 0.00001);
        }
    }
    TEST_METHOD(DpiVirtualizedOsdCoordinatesCompensateExactlyOnce)
    {
        const auto reference = Size(Fullscreen(), true, true, 2.35);
        for (const double dpiScale : {1.0,1.25,1.5,2.0})
        {
            auto mapping = Fullscreen(); mapping.fullscreen = false;
            mapping.logicalHostWidth /= dpiScale; mapping.logicalHostHeight /= dpiScale;
            mapping.logicalChildWidth /= dpiScale; mapping.logicalChildHeight /= dpiScale;
            mapping.osdWidth /= dpiScale; mapping.osdHeight /= dpiScale;
            const auto actual = Size(mapping, true, true, 2.35);
            Assert::AreEqual(double(reference.scale), double(actual.scale) * dpiScale, 0.00001);
            Assert::AreEqual(double(reference.insetPixels), double(actual.insetPixels) * dpiScale, 0.00001);
        }
    }
    TEST_METHOD(DpiAwareOsdWithLogicalChildWindowKeepsPhysicalPixelSize)
    {
        auto mapping = Fullscreen(); mapping.fullscreen = false;
        mapping.logicalHostWidth /= 1.5; mapping.logicalHostHeight /= 1.5;
        mapping.logicalChildWidth /= 1.5; mapping.logicalChildHeight /= 1.5;
        const auto actual = Size(mapping, true, true, 2.35);
        const auto reference = Size(Fullscreen(), true, true, 2.35);
        Assert::AreEqual(double(reference.scale), double(actual.scale), 0.00001);
    }
    TEST_METHOD(WindowedMadvrMatchesVpHostReferenceAcrossDpiAndNlsChildCanvases)
    {
        for (const double dpiScale : {1.0,1.25,1.5,2.0})
            for (const bool nls : {false,true})
            {
                auto mapping = Fullscreen(); mapping.fullscreen = false;
                mapping.physicalHostWidth = 1920; mapping.physicalHostHeight = 1080;
                mapping.logicalHostWidth = 1920 / dpiScale; mapping.logicalHostHeight = 1080 / dpiScale;
                mapping.logicalChildWidth = mapping.osdWidth = mapping.logicalHostWidth;
                mapping.logicalChildHeight = mapping.osdHeight = nls
                    ? mapping.logicalHostWidth / 2.35 : mapping.logicalHostHeight;
                const auto madvr = Size(mapping,true,true,2.35);
                const AlphaSourceCrop::PresentationRect available{0,0,
                    mapping.logicalHostWidth,mapping.logicalHostHeight};
                const auto vpScreen = AnamorphicPresentation::FitScreen(true,2.35,1.0,available,
                    AlphaSourceCrop::VerticalPictureAlignment::CENTER,0);
                Assert::IsTrue(vpScreen.valid);
                const auto vp = NativeStatsOverlayPlacement::ResolveStatsSizing(true,
                    static_cast<float>(vpScreen.rect.bottom-vpScreen.rect.top),
                    static_cast<float>(mapping.logicalHostHeight));
                Assert::AreEqual(double(vp.scale),double(madvr.scale),0.00001);
                Assert::AreEqual(double(vp.insetPixels),double(madvr.insetPixels),0.00001);
            }
    }
    TEST_METHOD(NonzeroMadvrOutputOriginOnlyChangesBitmapPosition)
    {
        const auto sizing = Size(Fullscreen(), true, true, 2.35);
        const NativeStatsOverlayPlacement::Rect output{120,-50,3960,2110};
        const NativeStatsOverlayPlacement::Rect picture{120,214,3960,1848};
        const auto offset = NativeStatsOverlayPlacement::Place(picture,output,578,948,
            sizing.insetPixels,sizing.scale);
        const auto zero = NativeStatsOverlayPlacement::Place({0,264,3840,1898},{0,0,3840,2160},
            578,948,sizing.insetPixels,sizing.scale);
        Assert::AreEqual(double(zero.panel.left), double(offset.panel.left-output.left), 0.0001);
        Assert::AreEqual(double(zero.panel.top), double(offset.panel.top-output.top), 0.0001);
        Assert::AreEqual(double(zero.scale), double(offset.scale), 0.00001);
    }
    TEST_METHOD(ActivePictureAndSoftwareCropOnlyAnchorAndContainNominalPanel)
    {
        const auto sizing = Size(Fullscreen(), true, true, 2.35);
        const NativeStatsOverlayPlacement::Rect output{0,0,3840,2160};
        const auto full = NativeStatsOverlayPlacement::Place(output,output,578,948,
            sizing.insetPixels,sizing.scale);
        const auto scope = NativeStatsOverlayPlacement::Place({0,263,3840,1897},output,578,948,
            sizing.insetPixels,sizing.scale);
        Assert::AreEqual(double(full.scale),double(scope.scale),0.00001);
        const NativeStatsOverlayPlacement::Rect tiny{200,300,800,700};
        const auto contained = NativeStatsOverlayPlacement::Place(tiny,output,578,948,
            sizing.insetPixels,sizing.scale);
        Assert::IsTrue(contained.scale < sizing.scale);
        Assert::IsTrue(contained.panel.left >= tiny.left && contained.panel.top >= tiny.top &&
            contained.panel.right <= tiny.right && contained.panel.bottom <= tiny.bottom);
    }
    TEST_METHOD(InvalidOrNonuniformCoordinateMappingRetainsLegacySizing)
    {
        auto mapping = Fullscreen(); mapping.unmaskedOutputHeight *= 0.75;
        Assert::IsFalse(MadVROsdSizing::Resolve(mapping,true,2.35).valid);
        auto actual = Size(mapping,true,true,2.35);
        Assert::AreEqual(1.0f,actual.scale); Assert::AreEqual(40.0f,actual.insetPixels);
        mapping = Fullscreen(); mapping.fullscreen = false; mapping.osdHeight *= 0.75;
        Assert::IsFalse(MadVROsdSizing::Resolve(mapping,true,2.35).valid);
        mapping = Fullscreen(); mapping.logicalHostHeight = 0;
        Assert::IsFalse(MadVROsdSizing::Resolve(mapping,true,2.35).valid);
        mapping = Fullscreen(); mapping.physicalHostWidth = std::numeric_limits<double>::infinity();
        Assert::IsFalse(MadVROsdSizing::Resolve(mapping,true,2.35).valid);
        Assert::IsFalse(MadVROsdSizing::Resolve(Fullscreen(),true,std::numeric_limits<double>::quiet_NaN()).valid);
    }
    TEST_METHOD(FullscreenUniformAndVerticalMaskingNeverBecomeAnotherScaleFactor)
    {
        const auto reference = Size(Fullscreen(),true,true,2.35);
        for (const double widthFactor : {0.8,1.0})
        {
            auto mapping = Fullscreen(); mapping.osdWidth *= widthFactor; mapping.osdHeight *= 0.8;
            const auto masked = Size(mapping,true,true,2.35);
            Assert::AreEqual(double(reference.scale),double(masked.scale),0.00001);
            Assert::AreEqual(double(reference.insetPixels),double(masked.insetPixels),0.00001);
        }
        auto missing = Fullscreen(); missing.unmaskedOutputWidth = missing.unmaskedOutputHeight = 0;
        Assert::IsFalse(MadVROsdSizing::Resolve(missing,true,2.35).valid);
    }
    TEST_METHOD(PausedRefreshUsesSupportedRedrawCommandAndHandlesMissingInterface)
    {
        struct Command : IMadVRCommand
        {
            bool supported = true; int redraws = 0;
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override
            {
                if (!output) return E_POINTER; *output = nullptr;
                if (iid == IID_IUnknown || (supported && iid == __uuidof(IMadVRCommand)))
                { *output = static_cast<IMadVRCommand*>(this); AddRef(); return S_OK; }
                return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
            ULONG STDMETHODCALLTYPE Release() override { return 1; }
            HRESULT STDMETHODCALLTYPE SendCommand(LPCSTR command) override
            { if (strcmp(command,"redraw") != 0) return E_INVALIDARG; ++redraws; return S_OK; }
            HRESULT STDMETHODCALLTYPE SendCommandBool(LPCSTR,bool) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandInt(LPCSTR,int) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandSize(LPCSTR,SIZE) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandRect(LPCSTR,RECT) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandUlonglong(LPCSTR,ULONGLONG) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandDouble(LPCSTR,double) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandString(LPCSTR,LPWSTR) override {return E_NOTIMPL;}
            HRESULT STDMETHODCALLTYPE SendCommandBin(LPCSTR,LPVOID,int) override {return E_NOTIMPL;}
        } command;
        Assert::AreEqual(S_OK,MadVROsdRefresh::RedrawPaused(&command));
        Assert::AreEqual(1,command.redraws);
        command.supported = false;
        Assert::AreEqual(E_NOINTERFACE,MadVROsdRefresh::RedrawPaused(&command));
        Assert::AreEqual(1,command.redraws);
        Assert::AreEqual(E_POINTER,MadVROsdRefresh::RedrawPaused(nullptr));
    }
    TEST_METHOD(DisabledSettingPreservesLegacySizeAndInsetAcrossProfiles)
    {
        for (const double aspect : {16.0/9.0,2.35,2.40})
        {
            const auto actual = Size(Fullscreen(),false,true,aspect);
            Assert::AreEqual(1.0f,actual.scale); Assert::AreEqual(40.0f,actual.insetPixels);
        }
    }
    TEST_METHOD(MissingScreenAspectUsesMonitorWhileNarrowerScreensKeepFullHeight)
    {
        const auto unknown = MadVROsdSizing::Resolve(Fullscreen(),false,0.0);
        const auto narrow = MadVROsdSizing::Resolve(Fullscreen(),true,4.0/3.0);
        Assert::IsTrue(unknown.valid && narrow.valid);
        Assert::AreEqual(2160.0f,unknown.fittedHeight);
        Assert::AreEqual(2160.0f,narrow.fittedHeight);
    }
};
}
