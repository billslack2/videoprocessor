#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/OutputOsdTelemetryCache.h>
#include <future>
#include <thread>
#include <atomic>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
TEST_CLASS(OutputOsdTelemetryCacheTests)
{
    static DisplayOutputModeState Mode(uint32_t sourceW=1920,uint32_t sourceH=1080,
        uint32_t targetW=3840,uint32_t targetH=2160)
    { return {sourceW,sourceH,targetW,targetH,{60000,1001},DisplayOutputScan::Progressive,true}; }
    // A different thread owns the real mutex: try_lock on an already-owned
    // nonrecursive mutex in the same thread would be undefined behaviour.
    struct BusyRenderer
    {
        std::promise<void> release, ready;
        std::thread worker;
        explicit BusyRenderer(std::mutex& mutex) : worker([&mutex,this] {
            std::lock_guard<std::mutex> lock(mutex);
            ready.set_value(); release.get_future().wait();
        }) { ready.get_future().wait(); }
        ~BusyRenderer() { release.set_value(); worker.join(); }
    };
    static std::string Read(OutputOsdTelemetryCache& cache,std::mutex& mutex,
        const DisplayRasterInfo::Cache& raster)
    {
        return cache.ReadOutput(mutex, []() -> std::string {
            Assert::Fail(L"Formatter must not run while renderMutex is busy"); return {};
        }, [&] { return raster.Read(); });
    }
public:
    TEST_METHOD(ColdCacheIsAuthoritativeEmptyUnderRenderContention)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        raster.RecordMode(Mode()); BusyRenderer busy(render);
        Assert::IsTrue(Read(cache,render,raster).empty());
        Assert::IsTrue(cache.ReadLut().empty());
    }
    TEST_METHOD(GetterRefreshAndBusyFallbackShareProductionAssembly)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        raster.RecordMode(Mode()); raster.RecordDrawn(1344,756);
        const auto expected="Target SDR Rec.709 | WIRE: 3840x2160 | DESKTOP: 1920x1080 | DRAWN: 1344x756";
        const auto current=cache.ReadOutput(render, [] { return std::string("Target SDR Rec.709"); },
            [&] { return raster.Read(); });
        Assert::AreEqual(expected,current.c_str());
        BusyRenderer busy(render);
        Assert::AreEqual(expected,Read(cache,render,raster).c_str());
    }
    TEST_METHOD(DisplayInvalidationCannotResurrectCachedWireDesktopOrDrawn)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        raster.RecordMode(Mode()); raster.RecordDrawn(1344,756);
        cache.PublishOutput(cache.OutputGeneration(),"Target"); BusyRenderer busy(render);
        Assert::IsTrue(Read(cache,render,raster).find("DRAWN")!=std::string::npos);
        raster.Invalidate();
        Assert::AreEqual("Target",Read(cache,render,raster).c_str());
    }
    TEST_METHOD(ModeChangesAndSwapchainClearsAreVisibleEvenWithoutAnotherFrame)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        cache.PublishOutput(cache.OutputGeneration(),"Target"); raster.RecordMode(Mode());
        raster.RecordDrawn(1344,756); BusyRenderer busy(render);
        raster.ClearDrawn();
        Assert::AreEqual("Target | WIRE: 3840x2160 | DESKTOP: 1920x1080",Read(cache,render,raster).c_str());
        raster.RecordMode(Mode(1920,1080,1920,1080));
        Assert::AreEqual("Target | WIRE: 1920x1080",Read(cache,render,raster).c_str());
        raster.RecordDrawn(1280,720);
        Assert::AreEqual("Target | WIRE: 1920x1080 | DRAWN: 1280x720",Read(cache,render,raster).c_str());
    }
    TEST_METHOD(FailedModeReadBackOmitsUnknownRasterInsteadOfReportingPreviousMode)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        cache.PublishOutput(cache.OutputGeneration(),"Target"); raster.RecordMode(Mode());
        Assert::IsFalse(raster.PublishObservation(raster.Generation(),false,Mode()));
        raster.RecordDrawn(1344,756); BusyRenderer busy(render);
        Assert::AreEqual("Target",Read(cache,render,raster).c_str());
    }
    TEST_METHOD(QueuedModeChangeClearsOutputUntilCurrentStateIsAvailable)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        cache.PublishOutput(cache.OutputGeneration(),"Old target"); raster.RecordMode(Mode());
        cache.InvalidateOutput();
        Assert::IsTrue(cache.ReadOutput(render, [] { return std::string{}; },
            [&] { return raster.Read(); }).empty());
        BusyRenderer busy(render);
        Assert::IsTrue(Read(cache,render,raster).empty());
        cache.PublishOutput(cache.OutputGeneration(),"Blocked: swapchain unavailable");
        Assert::IsTrue(Read(cache,render,raster).find("Blocked: swapchain unavailable")==0);
    }
    TEST_METHOD(ConcurrentInvalidationRejectsAlreadyStartedFormatter)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        const auto result=cache.ReadOutput(render, [&] {
            // Models a display notification arriving while formatting old state.
            cache.InvalidateOutput();
            return std::string("Old target");
        }, [&] { return raster.Read(); });
        Assert::IsTrue(result.empty());
        Assert::IsFalse(cache.HasOutput());
    }
    TEST_METHOD(OldPublicationCannotAdoptNewModeGeneration)
    {
        OutputOsdTelemetryCache cache;
        const auto generation=cache.OutputGeneration(); cache.InvalidateOutput();
        Assert::IsFalse(cache.PublishOutput(generation,"old mode"));
        Assert::IsTrue(cache.PublishOutput(cache.OutputGeneration(),"new mode"));
    }
    TEST_METHOD(PausedOutputPersistsUntilExplicitInvalidation)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        cache.PublishOutput(cache.OutputGeneration(),"Target"); raster.RecordMode(Mode());
        cache.PublishLut("Active: calibrated.cube (65^3)"); BusyRenderer busy(render);
        for(int i=0;i<500;++i) {
            Assert::AreEqual("Target | WIRE: 3840x2160 | DESKTOP: 1920x1080",Read(cache,render,raster).c_str());
            Assert::AreEqual("Active: calibrated.cube (65^3)",cache.ReadLut().c_str());
        }
    }
    TEST_METHOD(LutReconfigureRejectReloadAndDisablePublishImmediatelyWhileBusy)
    {
        OutputOsdTelemetryCache cache; std::mutex render; BusyRenderer busy(render);
        cache.PublishLut("Active: old.cube (65^3)"); cache.InvalidateLut();
        Assert::IsTrue(cache.ReadLut().empty());
        for(const auto* status : {"Loaded", "Active: new.cube (65^3)",
            "Active (reload rejected: invalid Cube)", "Rejected: render error", "Disabled"}) {
            cache.PublishLut(status); Assert::AreEqual(status,cache.ReadLut().c_str());
        }
    }
    TEST_METHOD(RetirementPermanentlyWithdrawsOutputAndLutIncludingLatePublications)
    {
        OutputOsdTelemetryCache cache; DisplayRasterInfo::Cache raster; std::mutex render;
        cache.PublishOutput(cache.OutputGeneration(),"Target"); cache.PublishLut("Active");
        raster.RecordMode(Mode()); cache.Retire();
        Assert::IsFalse(cache.PublishOutput(cache.OutputGeneration(),"Late target"));
        cache.PublishLut("Late active LUT"); Assert::IsTrue(cache.ReadLut().empty());
        Assert::IsTrue(cache.ReadOutput(render, [] { return std::string("Retired state"); },
            [&] { return raster.Read(); }).empty());
    }
    TEST_METHOD(ConcurrentLutReadersObserveCompleteValues)
    {
        OutputOsdTelemetryCache cache; std::atomic_bool bad{false};
        const std::string first(300,'a'),second(500,'b'); cache.PublishLut(first);
        std::thread writer([&] { for(int i=0;i<3000;++i) cache.PublishLut(i%2 ? first : second); });
        for(int i=0;i<3000;++i) { const auto value=cache.ReadLut(); if(value!=first && value!=second) bad=true; }
        writer.join(); Assert::IsFalse(bad.load());
    }
};
}
