#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/FrameHdrStatus.h>
#include <vprenderer/DolbyVisionLuminancePolicy.h>
#include <vprenderer/TemporalRenderer.h>
#include <HdrOsdFormatting.h>
#include <libplacebo/d3d11.h>
#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace {
pl_frame HdrFrame()
{
    pl_frame frame{}; frame.color=pl_color_space_hdr10;
    frame.color.hdr={}; frame.color.hdr.min_luma=.001f;frame.color.hdr.max_luma=1000.0f;
    return frame;
}
struct HdrGpuFixture
{
    pl_d3d11 device=nullptr;pl_renderer renderer=nullptr;pl_tex inputTexture=nullptr,outputTexture=nullptr;
    pl_frame input{},output{};TemporalRenderer temporal;
    HdrGpuFixture()
    {
        pl_d3d11_params d{};d.allow_software=true;device=pl_d3d11_create(nullptr,&d);
        Assert::IsTrue(device!=nullptr,L"D3D11 HDR fixture unavailable");
        renderer=pl_renderer_create(nullptr,device->gpu);
        pl_tex_params p{};p.w=p.h=32;p.format=pl_find_named_fmt(device->gpu,"rgba8");
        p.sampleable=p.renderable=p.blit_src=p.blit_dst=true;
        Assert::IsTrue(p.format!=nullptr);
        inputTexture=pl_tex_create(device->gpu,&p);outputTexture=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(inputTexture&&outputTexture&&renderer);
        input=HdrFrame();input.num_planes=1;input.planes[0].texture=inputTexture;input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb;input.repr.levels=PL_COLOR_LEVELS_FULL;input.repr.alpha=PL_ALPHA_NONE;
        input.crop={0,0,32,32};output=input;output.planes[0].texture=outputTexture;output.color=pl_color_space_srgb;
    }
    ~HdrGpuFixture()
    {
        temporal.Clear();if(device){pl_tex_destroy(device->gpu,&inputTexture);pl_tex_destroy(device->gpu,&outputTexture);
            pl_renderer_destroy(&renderer);pl_d3d11_destroy(&device);}
    }
    void Capture(uint64_t sequence,float gray,float maxCll)
    {
        MotionConfig::Settings settings;settings.smoothMotion="always";settings.deinterlace="off";settings.mixer="oversample";
        temporal.Begin(settings,24,60,false,0,1,sequence,double(sequence-1)/24.0,false);
        const float rgb[3]={gray,gray,gray};pl_tex_clear(device->gpu,inputTexture,rgb);
        input.color.hdr.max_cll=maxCll;
        Assert::IsTrue(temporal.Capture(device->gpu,input,sequence));
        Assert::IsTrue(temporal.AnnotateHdrSource(sequence,input));
    }
    RendererHdrStatus Draw(double pts,uint64_t epoch=7)
    {
        temporal.outputPts=pts;
        auto params=pl_render_default_params;auto peak=pl_peak_detect_default_params;
        peak.allow_delayed=false;peak.smoothing_period=0.0f;params.peak_detect_params=&peak;
        params.dither_params=nullptr;params.deband_params=nullptr;
        Assert::IsTrue(temporal.Render(renderer,input,output,params,epoch),L"HDR temporal render failed");
        RendererHdrStatus status;Assert::IsTrue(temporal.HdrStatusSnapshot(status));return status;
    }
};
}
namespace Tests {
TEST_CLASS(HdrStatusTests) {
public:
    TEST_METHOD(UnknownStaticMetadataDoesNotPrintAsZero)
    {
        auto frame=HdrFrame();const auto status=FrameHdrStatus::Source(frame,12);
        const CString text=HdrOsdFormatting::Static(status);
        Assert::IsTrue(text.Find(_T("CLL --- FALL ---"))>=0);
        Assert::IsTrue(text.Find(_T("M 0.001-1000"))>=0);
        Assert::IsTrue(HdrOsdFormatting::Hdr10(0,0,0,0).Find(_T("M ---"))>=0);
    }
    TEST_METHOD(ZeroMasteringMinimumAndBlackMeasurementsRemainRealZero)
    {
        auto frame=HdrFrame();frame.color.hdr.min_luma=0;
        const auto source=FrameHdrStatus::Source(frame,1);pl_hdr_metadata black{};
        const auto status=FrameHdrStatus::WithDetection(source,true,&black);
        Assert::IsTrue(status.detectedValid);Assert::AreEqual(0.0,status.detectedPeakNits);
        Assert::IsTrue(HdrOsdFormatting::Static(status).Find(_T("M 0-1000"))>=0);
        Assert::IsTrue(HdrOsdFormatting::Measured(status)==_T("peak 0"));
    }
    TEST_METHOD(MixedDetectionLabelsOnlyMeasuredOutputAndKeepsAuthoredIdentity)
    {
        RendererHdrStatus status;status.hdr=true;status.peakDetectionEnabled=true;
        status.detectedValid=true;status.detectedFromMix=true;status.detectedPeakNits=400;
        status.detectedLinearAvgNits=120;status.sceneValid=true;status.scenePeakNits=800;
        status.sceneAvgNits=200;status.sourceSequence=17;status.sourceGeneration=3;status.processingEpoch=9;
        Assert::IsTrue(HdrOsdFormatting::Measured(status)==_T("mixed peak 400 / avg 120 | frame 800 / 200"));
        Assert::AreEqual(uint64_t(17),status.sourceSequence);
        Assert::AreEqual(uint64_t(3),status.sourceGeneration);
        Assert::AreEqual(uint64_t(9),status.processingEpoch);
        status.detectedFromMix=false;
        Assert::IsTrue(HdrOsdFormatting::Measured(status)==_T("peak 400 / avg 120 | frame 800 / 200"));
        status.detectedFromMix=true;status.detectedValid=false;
        Assert::IsTrue(HdrOsdFormatting::Measured(status)==_T("--- | frame 800 / 200"));
        status.peakDetectionEnabled=false;
        Assert::IsTrue(HdrOsdFormatting::Measured(status)==_T("off | frame 800 / 200"));
    }
    TEST_METHOD(RefreshingDetectionClearsPriorMixedOutputProvenance)
    {
        RendererHdrStatus mixed;mixed.hdr=true;mixed.detectedFromMix=true;
        mixed.detectedValid=true;mixed.detectedPeakNits=400;mixed.sourceSequence=17;
        for(bool enabled:{false,true}) {
            const auto refreshed=FrameHdrStatus::WithDetection(mixed,enabled,nullptr);
            Assert::IsFalse(refreshed.detectedFromMix||refreshed.detectedValid);
            Assert::AreEqual(0.0,refreshed.detectedPeakNits);
            Assert::AreEqual(uint64_t(17),refreshed.sourceSequence);
        }
    }
    TEST_METHOD(NonfiniteAndNegativeMetadataNeverLeaksIntoOsd)
    {
        const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
        for(double value:{nan,inf,-inf,-1.0})
        {
            Assert::IsTrue(HdrOsdFormatting::Nits(value)==_T("---"));
            Assert::IsTrue(HdrOsdFormatting::Hdr10(value,value,value,value)==_T("CLL --- FALL --- | M ---"));
        }
        auto frame=HdrFrame();frame.color.hdr.max_cll=float(nan);frame.color.hdr.max_fall=float(inf);
        frame.color.hdr.max_luma=float(inf);frame.color.hdr.max_pq_y=float(nan);
        const auto status=FrameHdrStatus::Source(frame,1);
        Assert::AreEqual(0.0,status.maxCll);Assert::AreEqual(0.0,status.maxFall);
        Assert::AreEqual(0.0,status.masteringMax);Assert::IsFalse(status.sceneValid);
    }
    TEST_METHOD(InvalidDetectorResultIsPendingAndDoesNotReusePreviousNumbers)
    {
        auto source=FrameHdrStatus::Source(HdrFrame(),1);pl_hdr_metadata detected{};
        detected.max_pq_y=.7f;detected.avg_pq_y=.3f;
        source=FrameHdrStatus::WithDetection(source,true,&detected);Assert::IsTrue(source.detectedValid);
        detected.max_pq_y=std::numeric_limits<float>::infinity();
        auto invalid=FrameHdrStatus::WithDetection(source,true,&detected);
        Assert::IsFalse(invalid.detectedValid);Assert::AreEqual(0.0,invalid.detectedPeakNits);
        Assert::IsTrue(HdrOsdFormatting::Measured(invalid)==_T("---"));
        detected.max_pq_y=.3f;detected.avg_pq_y=1.01f;
        Assert::IsFalse(FrameHdrStatus::WithDetection(source,true,&detected).detectedValid);
    }
    TEST_METHOD(PercentileAndHistogramPeakDoNotInvalidateIndependentAverage)
    {
        const auto source=FrameHdrStatus::Source(HdrFrame(),1);pl_hdr_metadata detected{};
        // Actual beta constant-field result: peak and average use independent
        // reductions; the percentile-clipped peak is not a bound on the mean.
        detected.max_pq_y=0.250465899706f;detected.avg_pq_y=0.250930845737f;
        const auto status=FrameHdrStatus::WithDetection(source,true,&detected);
        Assert::IsTrue(status.detectedValid);Assert::IsTrue(status.detectedAvgNits>status.detectedPeakNits);
        Assert::AreEqual(double(pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,detected.avg_pq_y)),status.detectedAvgNits,1e-6);
        detected.avg_pq_y=1.001f;Assert::IsFalse(FrameHdrStatus::WithDetection(source,true,&detected).detectedValid);
        detected.avg_pq_y=-.001f;Assert::IsFalse(FrameHdrStatus::WithDetection(source,true,&detected).detectedValid);
    }
    TEST_METHOD(SceneAndDolbyMinimumBelongToOneSourcePacket)
    {
        auto frame=HdrFrame();pl_dovi_metadata dovi{};frame.repr.dovi=&dovi;
        frame.color.hdr.max_pq_y=.7f;frame.color.hdr.avg_pq_y=.4f;
        DolbyVisionLuminanceStatus dv;dv.sceneAvailable=true;dv.sceneMin=.00001f;
        const auto status=FrameHdrStatus::Source(frame,31,dv);
        Assert::IsTrue(status.sceneValid&&status.dolbyVision);Assert::AreEqual(uint64_t(31),status.sourceSequence);
        Assert::AreEqual(double(dv.sceneMin),status.sceneMinNits);
        Assert::AreEqual(double(pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,.4f)),status.sceneAvgNits,1e-6);
        Assert::IsTrue(HdrOsdFormatting::Measured(status).Find(_T("L1"))>=0);
        Assert::IsTrue(HdrOsdFormatting::Measured(status).Find(_T("nits"))<0, L"Units are supplied by the shared OSD row label");
    }
    TEST_METHOD(ExplicitDolbyBlackL1RemainsPresentThroughProductionStatusAndFormatting)
    {
        auto frame=HdrFrame();pl_dovi_metadata dovi{};frame.repr.dovi=&dovi;
        DolbyVisionTunnelFrameMetadata metadata;metadata.valid=true;metadata.sourceMaxPq=4095;
        metadata.hasSceneMetadata=true; // Authored min/max/average are all black.
        const auto luminance=DolbyVisionLuminancePolicy::Apply(metadata,frame.color.hdr);
        Assert::IsTrue(luminance.sceneAvailable);
        const auto black=FrameHdrStatus::Source(frame,42,luminance);
        Assert::IsTrue(black.sceneValid&&black.dolbyVision);Assert::AreEqual(uint64_t(42),black.sourceSequence);
        Assert::AreEqual(0.0,black.sceneMinNits);Assert::AreEqual(0.0,black.scenePeakNits);Assert::AreEqual(0.0,black.sceneAvgNits);
        Assert::IsTrue(HdrOsdFormatting::Measured(black)==_T("off | L1 0 / 0"));
        const auto waiting=FrameHdrStatus::WithDetection(black,true,nullptr);
        Assert::IsTrue(HdrOsdFormatting::Measured(waiting)==_T("--- | L1 0 / 0"));
        metadata.hasSceneMetadata=false;
        const auto missing=DolbyVisionLuminancePolicy::Apply(metadata,frame.color.hdr);
        Assert::IsFalse(missing.sceneAvailable);Assert::IsFalse(FrameHdrStatus::Source(frame,43,missing).sceneValid);
        frame.repr.dovi=nullptr;Assert::IsFalse(FrameHdrStatus::Source(frame,44,luminance).sceneValid);
    }
    TEST_METHOD(DolbyTransitionToBlackPublishesChangedL1WithoutInventingMissingScene)
    {
        auto frame=HdrFrame();pl_dovi_metadata dovi{};frame.repr.dovi=&dovi;
        DolbyVisionTunnelFrameMetadata metadata;metadata.valid=true;metadata.sourceMaxPq=4095;metadata.hasSceneMetadata=true;
        metadata.sceneMinPq=16;metadata.sceneMaxPq=2800;metadata.sceneAvgPq=1600;
        auto luminance=DolbyVisionLuminancePolicy::Apply(metadata,frame.color.hdr);
        const auto bright=FrameHdrStatus::Source(frame,1,luminance);
        FrameHdrStatus::Publication publication;const auto token=publication.Begin({1,2,3,4,5});
        Assert::IsTrue(publication.CommitPresented(token,bright,true,true));
        metadata.sceneMinPq=metadata.sceneMaxPq=metadata.sceneAvgPq=0;
        luminance=DolbyVisionLuminancePolicy::Apply(metadata,frame.color.hdr);
        const auto black=FrameHdrStatus::Source(frame,2,luminance);
        Assert::IsTrue(publication.CommitPresented(token,black,true,true));RendererHdrStatus observed;
        Assert::IsTrue(publication.Snapshot(observed));Assert::IsTrue(observed.sceneValid);
        Assert::IsTrue(observed.dolbyVision&&(observed.sceneMinNits!=bright.sceneMinNits||
            observed.scenePeakNits!=bright.scenePeakNits||observed.sceneAvgNits!=bright.sceneAvgNits),
            L"The existing L1-change log predicate must see an authored transition to zero");
        Assert::AreEqual(uint64_t(2),observed.sourceSequence);
        Assert::IsTrue(HdrOsdFormatting::Measured(observed)==_T("off | L1 0 / 0"));
        frame.color.hdr.max_pq_y=std::numeric_limits<float>::quiet_NaN();
        Assert::IsFalse(FrameHdrStatus::Source(frame,3,luminance).sceneValid);
        frame.color.hdr.max_pq_y=-.01f;Assert::IsFalse(FrameHdrStatus::Source(frame,4,luminance).sceneValid);
        frame.color.hdr.max_pq_y=1.01f;Assert::IsFalse(FrameHdrStatus::Source(frame,5,luminance).sceneValid);
        frame.color.hdr.max_pq_y=.2f;frame.color.hdr.avg_pq_y=.3f;
        Assert::IsFalse(FrameHdrStatus::Source(frame,6,luminance).sceneValid);
    }
    TEST_METHOD(SdrCannotExposeHdrOrOldDetectionValues)
    {
        auto frame=HdrFrame();frame.color.transfer=PL_COLOR_TRC_SRGB;
        frame.color.hdr.max_cll=1000;pl_hdr_metadata detected{};detected.max_pq_y=.7f;detected.avg_pq_y=.4f;
        const auto status=FrameHdrStatus::WithDetection(FrameHdrStatus::Source(frame,2),true,&detected);
        Assert::IsFalse(status.hdr||status.peakDetectionEnabled||status.detectedValid);
        Assert::AreEqual(0.0,status.maxCll);Assert::IsTrue(HdrOsdFormatting::Static(status).IsEmpty());
        Assert::IsTrue(HdrOsdFormatting::Measured(status).IsEmpty());
    }
    TEST_METHOD(PublicationChangesImmediatelyWithoutQuarterSecondGate)
    {
        FrameHdrStatus::Publication publication;FrameHdrStatus::Context context{1,2,3,4,5};
        auto hdr=FrameHdrStatus::Source(HdrFrame(),1);const auto token=publication.Begin(context);
        Assert::IsTrue(publication.Commit(token,hdr));RendererHdrStatus observed;
        hdr.sourceSequence=2;hdr.detectedValid=true;hdr.detectedPeakNits=10;
        Assert::IsTrue(publication.Commit(token,hdr));Assert::IsTrue(publication.Snapshot(observed));
        Assert::AreEqual(uint64_t(2),observed.sourceSequence);
        auto sdr=hdr;sdr.hdr=false;sdr.detectedValid=false;context.transfer=9;
        const auto sdrToken=publication.Begin(context);Assert::IsFalse(publication.Snapshot(observed));
        Assert::IsTrue(publication.Commit(sdrToken,sdr));Assert::IsTrue(publication.Snapshot(observed));
        Assert::IsFalse(observed.hdr);Assert::IsFalse(publication.Commit(token,hdr));
    }
    TEST_METHOD(SourceAndProfileResetsRejectInFlightOrOldContext)
    {
        FrameHdrStatus::Publication publication;FrameHdrStatus::Context context{1,2,3,4,5};
        const auto hdr=FrameHdrStatus::Source(HdrFrame(),1);const auto old=publication.Begin(context);
        Assert::IsTrue(publication.Commit(old,hdr));publication.ResetForGeneration(2);
        RendererHdrStatus observed;Assert::IsFalse(publication.Snapshot(observed));
        Assert::IsFalse(publication.Commit(old,hdr));Assert::AreEqual(uint64_t(0),publication.Begin(context));
        context.sourceGeneration=2;const auto current=publication.Begin(context);
        Assert::IsTrue(publication.Commit(current,hdr));publication.ResetForViewport(7);
        Assert::IsFalse(publication.Commit(current,hdr));Assert::AreEqual(uint64_t(0),publication.Begin(context));
        context.viewportEpoch=7;Assert::IsTrue(publication.Commit(publication.Begin(context),hdr));
        Assert::IsTrue(publication.Snapshot(observed));Assert::AreEqual(uint64_t(2),observed.sourceGeneration);
    }
    TEST_METHOD(ProcessingAndDiscontinuityInvalidateButPausedPictureRemains)
    {
        FrameHdrStatus::Publication publication;FrameHdrStatus::Context context{1,2,3,4,5};
        const auto hdr=FrameHdrStatus::Source(HdrFrame(),1);const auto token=publication.Begin(context);
        Assert::IsTrue(publication.Commit(token,hdr));RendererHdrStatus observed;
        for(int i=0;i<20;++i){Assert::AreEqual(token,publication.Begin(context));Assert::IsTrue(publication.Snapshot(observed));}
        context.processingEpoch=4;const auto updated=publication.Begin(context);
        Assert::IsFalse(publication.Snapshot(observed));Assert::IsFalse(publication.Commit(token,hdr));
        Assert::IsTrue(publication.Commit(updated,hdr));publication.Begin(context,true);
        Assert::IsFalse(publication.Snapshot(observed));
    }
    TEST_METHOD(PendingRequestBeforeBeginCannotRevalidateOldContext)
    {
        FrameHdrStatus::Publication publication;FrameHdrStatus::Context context{1,2,3,4,5};
        const auto hdr=FrameHdrStatus::Source(HdrFrame(),1);const auto old=publication.Begin(context);
        Assert::IsTrue(publication.Commit(old,hdr));publication.Invalidate();
        Assert::AreEqual(uint64_t(0),publication.Begin(context,false,true));
        RendererHdrStatus observed;Assert::IsFalse(publication.Snapshot(observed));
        Assert::IsFalse(publication.Commit(old,hdr));
        Assert::IsTrue(publication.Commit(publication.Begin(context),hdr));
        publication.Suspend();Assert::AreEqual(uint64_t(0),publication.Begin(context));
        publication.ResetForGeneration(2);context.sourceGeneration=2;
        Assert::IsTrue(publication.Commit(publication.Begin(context),hdr));
    }
    TEST_METHOD(FailedRenderPreservesRetainedPictureButWithdrawsSubmittedBlack)
    {
        FrameHdrStatus::Publication publication;FrameHdrStatus::Context context{1,2,3,4,5};
        const auto hdr=FrameHdrStatus::Source(HdrFrame(),1);const auto token=publication.Begin(context);
        Assert::IsTrue(publication.CommitPresented(token,hdr,true,true));RendererHdrStatus observed;
        Assert::IsFalse(publication.CommitPresented(token,{},false,false));
        Assert::IsTrue(publication.Snapshot(observed));Assert::AreEqual(uint64_t(1),observed.sourceSequence);
        Assert::IsFalse(publication.CommitPresented(token,{},false,true));Assert::IsFalse(publication.Snapshot(observed));
        Assert::IsFalse(publication.Commit(token,hdr));
    }
    TEST_METHOD(OptionalStatusLayoutRejectsOldSizeAndUnknownVersion)
    {
        RendererHdrStatus status;Assert::IsTrue(status.Supported());status.size-=8;Assert::IsFalse(status.Supported());
        status={};status.version=2;Assert::IsFalse(status.Supported());
    }
    TEST_METHOD(DetectorConstantFieldPublishedValuesMatchNativeResult)
    {
        HdrGpuFixture gpu;gpu.Capture(1,.25f,400);const auto published=gpu.Draw(0);
        pl_hdr_metadata detected{};Assert::IsTrue(pl_renderer_get_hdr_metadata(gpu.temporal.ProcessingRenderer(gpu.renderer),&detected));
        CString diagnostic;diagnostic.Format(_T("Raw constant-field detector max=%.12f avg=%.12f delta=%.12g"),
            detected.max_pq_y,detected.avg_pq_y,double(detected.avg_pq_y)-detected.max_pq_y);
        Logger::WriteMessage(diagnostic.GetString());
        Assert::IsTrue(published.detectedValid);
        Assert::AreEqual(double(pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,detected.max_pq_y)),published.detectedPeakNits,1e-6);
        Assert::AreEqual(double(pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,detected.avg_pq_y)),published.detectedAvgNits,1e-6);
    }
    TEST_METHOD(TemporalCueKeepsOwnDetectorWhenFutureSourceWasRenderedLater)
    {
        HdrGpuFixture gpu;gpu.Capture(1,.25f,400);gpu.Capture(2,.9f,1800);gpu.Capture(3,.6f,900);
        const auto first=gpu.Draw(0);Assert::AreEqual(uint64_t(1),first.sourceSequence);
        Assert::AreEqual(400.0,first.maxCll);
        pl_hdr_metadata future{};
        Assert::IsTrue(pl_renderer_get_hdr_metadata(gpu.temporal.ProcessingRenderer(gpu.renderer),&future));
        const auto diagnostic=L"Temporal detector max="+std::to_wstring(future.max_pq_y)+L" avg="+std::to_wstring(future.avg_pq_y);
        Assert::IsTrue(first.detectedValid,diagnostic.c_str());
        const double futurePeak=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,future.max_pq_y);
        Assert::IsTrue(std::abs(first.detectedPeakNits-futurePeak)>1.0,L"Fixture must process a different future neighbour last");
        const auto before=gpu.temporal.ProcessingSnapshot();const auto repeated=gpu.Draw(0);
        Assert::AreEqual(before.sourceRenders,gpu.temporal.ProcessingSnapshot().sourceRenders);
        Assert::IsTrue(gpu.temporal.ProcessingSnapshot().sourceReuses>before.sourceReuses);
        Assert::AreEqual(first.detectedPeakNits,repeated.detectedPeakNits,1e-6);
        Assert::AreEqual(first.sourceSequence,repeated.sourceSequence);
        const auto second=gpu.Draw(1.0/24.0);Assert::AreEqual(uint64_t(2),second.sourceSequence);
        Assert::AreEqual(1800.0,second.maxCll);Assert::IsTrue(second.detectedPeakNits>first.detectedPeakNits);
    }
    TEST_METHOD(BufferedNormalizedDolbyPicturesKeepEachPacketsSceneMinimum)
    {
        HdrGpuFixture gpu;pl_dovi_metadata dovi{};DolbyVisionLuminanceStatus dv;dv.sceneAvailable=true;
        gpu.Capture(1,.25f,400);auto annotation=gpu.input;annotation.repr.dovi=&dovi;
        annotation.color.hdr.max_pq_y=.7f;annotation.color.hdr.avg_pq_y=.3f;dv.sceneMin=.00001f;
        Assert::IsTrue(gpu.temporal.AnnotateHdrSource(1,annotation,dv));
        gpu.Capture(2,.9f,1800);annotation=gpu.input;annotation.repr.dovi=&dovi;
        annotation.color.hdr.max_pq_y=.9f;annotation.color.hdr.avg_pq_y=.5f;dv.sceneMin=.05f;
        Assert::IsTrue(gpu.temporal.AnnotateHdrSource(2,annotation,dv));
        const auto first=gpu.Draw(0);Assert::IsTrue(first.dolbyVision&&first.sceneValid);
        Assert::AreEqual(double(.00001f),first.sceneMinNits);Assert::AreEqual(uint64_t(1),first.sourceSequence);
        const auto second=gpu.Draw(1.0/24.0);Assert::AreEqual(double(.05f),second.sceneMinNits);
        Assert::AreEqual(uint64_t(2),second.sourceSequence);
    }
    TEST_METHOD(TemporalClearWithdrawsSnapshotAndProfileEpochRecomputesIt)
    {
        HdrGpuFixture gpu;gpu.Capture(1,.3f,400);gpu.Capture(2,.6f,800);
        gpu.Draw(0,1);const auto before=gpu.temporal.ProcessingSnapshot();const auto changed=gpu.Draw(0,2);
        Assert::AreEqual(uint64_t(2),changed.processingEpoch);
        Assert::IsTrue(gpu.temporal.ProcessingSnapshot().sourceRenders>before.sourceRenders);
        gpu.temporal.Clear();RendererHdrStatus status;Assert::IsFalse(gpu.temporal.HdrStatusSnapshot(status));
    }
};
}
