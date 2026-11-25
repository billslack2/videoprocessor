#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/FrameHdrStatus.h>
#include <vprenderer/CompactHdrPeak.h>
#include <vprenderer/DolbyVisionLuminancePolicy.h>
#include <vprenderer/TemporalRenderer.h>
#include <HdrOsdFormatting.h>
#include <AnalysisFrame.h>
#include <libplacebo/d3d11.h>
#include <limits>
#include <vector>

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
    void Capture(uint64_t sequence,float gray,float maxCll,
        const CompactHdrPeak::Decision& peak={}, bool complete=false)
    {
        MotionConfig::Settings settings;settings.smoothMotion="always";settings.deinterlace="off";settings.mixer="oversample";
        temporal.Begin(settings,24,60,false,0,1,sequence,double(sequence-1)/24.0,false,complete);
        const float rgb[3]={gray,gray,gray};pl_tex_clear(device->gpu,inputTexture,rgb);
        input.color.hdr.max_cll=maxCll;
        if (complete)
            Assert::IsTrue(temporal.CaptureCompleteComposition(device->gpu,input,sequence,false));
        else
            Assert::IsTrue(temporal.Capture(device->gpu,input,sequence));
        Assert::IsTrue(temporal.AnnotateHdrSource(sequence,input,{},peak));
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
    TEST_METHOD(CompactHdrPeakMeasuresPqGrayAndExcludesEncodedBlack)
    {
        constexpr int width=64, height=32, rowBytes=width*4+16;
        std::vector<uint8_t> pixels(rowBytes*height);
        for (int y=8; y<height-8; ++y)
            for (int x=0; x<width; ++x)
                for (int channel=0; channel<3; ++channel)
                    pixels[y*rowBytes+x*4+channel]=128;

        AnalysisLumaSource source;
        source.data=pixels.data();
        source.dataBytes=pixels.size();
        source.width=width;
        source.height=height;
        source.rowBytes=rowBytes;
        source.format=AnalysisLumaFormat::NativeRgb;
        source.encoding=VideoFrameEncoding::BGRA_8BIT;
        source.range=AnalysisLumaRange::Full;

        const auto measured=CompactHdrPeak::Measure(source);
        Assert::AreEqual(static_cast<unsigned>(width*(height-16)),measured.samples);
        Assert::AreEqual(128.0f/255.0f,measured.peakPq,0.0001f);
        Assert::AreEqual(measured.peakPq,measured.averagePq,0.0001f);
        Assert::AreEqual(pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,128.0f/255.0f),
            measured.peakRgbNits,0.001f);
        source.range=AnalysisLumaRange::Limited;
        Assert::AreEqual(0u,CompactHdrPeak::Measure(source).samples);
    }

    TEST_METHOD(CompactHdrPeakDecodesLimitedPlanarAndPackedYuv)
    {
        constexpr int width=32, height=16;
        for (bool verticalSubsampling : {false,true}) {
            const int lumaCount=width*height;
            std::vector<uint16_t> yuv(lumaCount+width*(verticalSubsampling?height/2:height),512u<<6);
            for (int y=0; y<height; ++y)
                std::fill_n(yuv.begin()+y*width,width,(y<4?64u:502u)<<6);

            AnalysisLumaSource source;
            source.data=reinterpret_cast<const uint8_t*>(yuv.data());
            source.dataBytes=yuv.size()*sizeof(uint16_t);
            source.width=width;
            source.height=height;
            source.rowBytes=source.chromaRowBytes=width*sizeof(uint16_t);
            source.format=verticalSubsampling?AnalysisLumaFormat::P010:AnalysisLumaFormat::P210;
            source.encoding=verticalSubsampling?VideoFrameEncoding::P010:VideoFrameEncoding::P210;
            source.range=AnalysisLumaRange::Limited;
            source.colorspace=ColorSpace::BT_2020;

            const auto neutral=CompactHdrPeak::Measure(source);
            Assert::AreEqual(static_cast<unsigned>(width*(height-4)),neutral.samples);
            Assert::AreEqual(0.5f,neutral.averagePq,0.001f);
            for (size_t i=lumaCount; i<yuv.size(); i+=2) yuv[i]=736u<<6;
            const auto blue=CompactHdrPeak::Measure(source);
            Assert::IsTrue(blue.peakRgbNits>3.0f*neutral.peakRgbNits);
            Assert::IsTrue(blue.peakPq>neutral.peakPq);
            source.colorspace=ColorSpace::REC_709;
            Assert::AreEqual(0u,CompactHdrPeak::Measure(source).samples);
        }

        const uint8_t packed[]={128,126,128,126,128,16,128,16};
        AnalysisLumaSource source;
        source.data=packed;
        source.dataBytes=sizeof(packed);
        source.width=2;
        source.height=2;
        source.rowBytes=4;
        source.format=AnalysisLumaFormat::NativeYuv422;
        source.encoding=VideoFrameEncoding::UYVY;
        source.range=AnalysisLumaRange::Limited;
        source.colorspace=ColorSpace::BT_2020;
        const auto neutral=CompactHdrPeak::Measure(source);
        Assert::AreEqual(2u,neutral.samples);
        Assert::AreEqual(0.5023f,neutral.averagePq,0.001f);
    }

    TEST_METHOD(CompactHdrPeakAcceptsEveryNativeRgbLayoutAndItsRenderConversion)
    {
        using Encoding=VideoFrameEncoding;
        struct Layout { Encoding encoding; uint16_t black, gray, white; };
        const Layout layouts[]={
            {Encoding::ARGB_8BIT,0,128,255}, {Encoding::BGRA_8BIT,0,128,255},
            {Encoding::R210,64,512,960}, {Encoding::R10b,64,502,940},
            {Encoding::R10l,64,502,940}, {Encoding::R12B,0,2048,4095},
            {Encoding::R12L,0,2048,4095},
        };
        for (const auto& layout : layouts) {
            constexpr int width=8, height=2, stride=48;
            auto bytes=std::make_shared<std::vector<uint8_t>>(stride*height,0xfe);
            const auto packRow=[&](uint8_t* row, uint16_t red, uint16_t green, uint16_t blue) {
                if (layout.white==4095) {
                    uint8_t logical[36];
                    for (int pair=0; pair<4; ++pair) {
                        auto* p=logical+9*pair;
                        p[0]=uint8_t(red); p[1]=uint8_t((red>>8)|(green<<4));
                        p[2]=uint8_t(green>>4); p[3]=uint8_t(blue);
                        p[4]=uint8_t((blue>>8)|(red<<4)); p[5]=uint8_t(red>>4);
                        p[6]=uint8_t(green); p[7]=uint8_t((green>>8)|(blue<<4));
                        p[8]=uint8_t(blue>>4);
                    }
                    for (int i=0; i<36; ++i)
                        row[i]=logical[layout.encoding==Encoding::R12B ? i^3 : i];
                } else {
                    uint32_t word=0;
                    switch (layout.encoding) {
                    case Encoding::ARGB_8BIT: word=0xffu|red<<8|green<<16|blue<<24; break;
                    case Encoding::BGRA_8BIT: word=0xff000000u|red<<16|green<<8|blue; break;
                    case Encoding::R210: word=uint32_t(red)<<20|uint32_t(green)<<10|blue; break;
                    default: word=uint32_t(red)<<22|uint32_t(green)<<12|uint32_t(blue)<<2; break;
                    }
                    for (int i=0; i<width*4; ++i) {
                        int shift=i%4;
                        if (layout.encoding==Encoding::R210 || layout.encoding==Encoding::R10b)
                            shift=3-shift;
                        row[i]=uint8_t(word>>(8*shift));
                    }
                }
            };
            packRow(bytes->data(),layout.black,layout.black,layout.black);
            packRow(bytes->data()+stride,layout.gray,layout.gray,layout.gray);
            AnalysisFrameInput input{};
            input.nativeOwner=bytes;
            input.nativeData=bytes->data();
            input.nativeBytes=bytes->size();
            input.nativeRowBytes=stride;
            input.width=width;
            input.height=height;
            input.encoding=layout.encoding;
            input.colorspace=ColorSpace::BT_2020;
            const auto prepared=PrepareAnalysisFrame(input);
            auto image=HdrFrame();
            image.repr.sys=PL_COLOR_SYSTEM_RGB;
            image.repr.levels=layout.black ? PL_COLOR_LEVELS_LIMITED : PL_COLOR_LEVELS_FULL;
            Assert::IsTrue(CompactHdrPeak::Supports(prepared.source,image));
            image.repr.sys=PL_COLOR_SYSTEM_BT_2020_NC;
            Assert::IsTrue(CompactHdrPeak::Supports(prepared.source,image));
            image.repr.sys=PL_COLOR_SYSTEM_DOLBYVISION;
            Assert::IsFalse(CompactHdrPeak::Supports(prepared.source,image));

            const auto gray=CompactHdrPeak::Measure(prepared.source);
            const float expected=layout.white==4095 ? 512.0f/1023.0f :
                float(layout.gray-layout.black)/(layout.white-layout.black);
            Assert::AreEqual(unsigned(width),gray.samples);
            Assert::AreEqual(expected,gray.peakPq,0.0001f);
            packRow(bytes->data()+stride,layout.black,layout.white,layout.black);
            const auto green=CompactHdrPeak::Measure(prepared.source);
            Assert::AreEqual(unsigned(width),green.samples);
            Assert::AreEqual(10000.0f,green.peakRgbNits,0.02f);
            Assert::AreEqual(pl_hdr_rescale(PL_HDR_NITS,PL_HDR_PQ,6780.0f),green.peakPq,0.0001f);
        }
    }

    TEST_METHOD(CompactHdrPeakUsesHlgLuminanceAndSkipsAnHdrDestination)
    {
        auto image=HdrFrame();
        image.color=pl_color_space_bt2020_hlg;
        image.repr.sys=PL_COLOR_SYSTEM_BT_2020_NC;
        image.repr.levels=PL_COLOR_LEVELS_LIMITED;
        auto hdrTarget=pl_color_space_hdr10;
        hdrTarget.hdr.max_luma=1500.0f;
        auto color=image.color;
        Assert::IsFalse(CompactHdrPeak::InferHlgMeasurementColor(color,hdrTarget));
        auto sdrTarget=pl_color_space_srgb;
        sdrTarget.hdr.min_luma=0.05f;
        sdrTarget.hdr.max_luma=100.0f;
        color=image.color;
        Assert::IsTrue(CompactHdrPeak::InferHlgMeasurementColor(color,sdrTarget));
        Assert::AreEqual(sdrTarget.hdr.min_luma,color.hdr.min_luma,0.00001f);

        // Neutral 75% HLG and encoded black, in a padded native v210 row.
        const uint32_t white=64u+876u*3u/4u, black=64u, neutral=512u;
        const uint32_t packed[]={
            neutral|(white<<10)|(neutral<<20), white|(neutral<<10)|(white<<20),
            neutral|(white<<10)|(neutral<<20), white|(neutral<<10)|(white<<20),
            0u, neutral|(black<<10)|(neutral<<20), black|(neutral<<10)|(black<<20),
            neutral|(black<<10)|(neutral<<20), black|(neutral<<10)|(black<<20), 0u,
        };
        auto owner=std::make_shared<std::vector<uint32_t>>(std::begin(packed),std::end(packed));
        AnalysisFrameInput input{};
        input.nativeOwner=owner;
        input.nativeData=reinterpret_cast<const uint8_t*>(owner->data());
        input.nativeBytes=owner->size()*sizeof(uint32_t);
        input.nativeRowBytes=5*sizeof(uint32_t);
        input.width=6;
        input.height=2;
        input.encoding=VideoFrameEncoding::V210;
        input.colorspace=ColorSpace::BT_2020;
        const auto prepared=PrepareAnalysisFrame(input);
        Assert::IsTrue(CompactHdrPeak::Supports(prepared.source,image));
        const auto hlg=CompactHdrPeak::Measure(prepared.source,color);
        float reference[]={0.75f,0.75f,0.75f};
        pl_color_linearize(&color,reference);
        Assert::AreEqual(6u,hlg.samples);
        Assert::AreEqual(reference[0]*PL_COLOR_SDR_WHITE,hlg.peakRgbNits,0.001f);
        Assert::AreEqual(pl_hdr_rescale(PL_HDR_NITS,PL_HDR_PQ,hlg.peakRgbNits),hlg.averagePq,0.0001f);

        const CompactHdrPeak::Decision decision{true,hlg,color.hdr.min_luma,color.hdr.max_luma};
        Assert::IsTrue(CompactHdrPeak::ReplacesDetector(decision,image.color,sdrTarget));
        Assert::IsFalse(CompactHdrPeak::ReplacesDetector(decision,image.color,hdrTarget));
        sdrTarget.hdr.min_luma=0.1f;
        Assert::IsFalse(CompactHdrPeak::ReplacesDetector(decision,image.color,sdrTarget));

        image.color=pl_color_space_hdr10;
        Assert::IsTrue(CompactHdrPeak::Supports(prepared.source,image));
        Assert::AreEqual(0.75f,CompactHdrPeak::Measure(prepared.source).peakPq,0.001f);
        image.color.transfer=PL_COLOR_TRC_BT_1886;
        Assert::IsFalse(CompactHdrPeak::Supports(prepared.source,image));
    }

    TEST_METHOD(CompactHdrPeakStaysWithTheSelectedMotionPacket)
    {
        HdrGpuFixture gpu;
        const CompactHdrPeak::Decision compact{true,{0.6f,0.3f,245.0f,10.0f,500}};
        gpu.Capture(1,.25f,400,compact);
        gpu.Capture(2,.9f,1800);
        // A later measurement cannot change the second source once queued.
        Assert::IsTrue(gpu.temporal.AnnotateHdrSource(2,gpu.input,{},compact));
        const auto first=gpu.Draw(0);
        Assert::AreEqual(uint64_t(1),first.sourceSequence);
        Assert::AreEqual(400.0,first.maxCll);
        Assert::IsFalse(first.peakDetectionEnabled || first.sceneValid);
        const auto second=gpu.Draw(1.0/24.0);
        Assert::AreEqual(uint64_t(2),second.sourceSequence);
        Assert::IsTrue(second.peakDetectionEnabled);
    }

    TEST_METHOD(CompactHdrPeakCanSkipTheDetectorForCompleteMotionPackets)
    {
        HdrGpuFixture gpu;
        const CompactHdrPeak::Decision compact{true,{0.6f,0.3f,245.0f,10.0f,500}};
        gpu.Capture(1,.25f,400,compact,true);
        gpu.Capture(2,.9f,1800,compact,true);
        const auto selected=gpu.Draw(0);
        Assert::IsTrue(gpu.temporal.CompleteCompositionSelected());
        Assert::AreEqual(uint64_t(1),selected.sourceSequence);
        Assert::IsFalse(selected.peakDetectionEnabled);
    }

    TEST_METHOD(CompactHdrPeakPreservesAuthoredAndStaticMetadata)
    {
        const CompactHdrPeak::Measurement measured{0.6f,0.3f,245.0f,10.0f,500};
        auto frame=HdrFrame();
        auto& hdr=frame.color.hdr;
        hdr.max_pq_y=0.7f;
        Assert::IsFalse(CompactHdrPeak::HasSourceLuminance(hdr));
        Assert::IsFalse(CompactHdrPeak::Apply(measured,hdr));
        hdr.avg_pq_y=0.4f;
        Assert::IsTrue(CompactHdrPeak::HasSourceLuminance(hdr));
        Assert::IsFalse(CompactHdrPeak::Apply(measured,hdr));
        hdr.max_pq_y=hdr.avg_pq_y=0.0f;
        Assert::IsTrue(CompactHdrPeak::Apply(measured,hdr));
        Assert::AreEqual(1000.0f,hdr.max_luma);
        Assert::AreEqual(0.6f,hdr.max_pq_y);
        Assert::AreEqual(0.3f,hdr.avg_pq_y);
        Assert::AreEqual(245.0f,hdr.scene_max[2]);
    }

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
