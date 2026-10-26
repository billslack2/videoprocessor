#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/TemporalRenderer.h>
#include <vprenderer/DisplayFrameCache.h>
#include <vprenderer/DisplayVsyncClock.h>
#include <vprenderer/InputAnalysisCadence.h>
#include <vprenderer/SourceUploadBatch.h>
#include <vprenderer/PresentationAnalysisBudget.h>
#include <vprenderer/AnalysisPerformanceWindow.h>
#include <vprenderer/OptionalAnalysisWorker.h>
#include <vprenderer/AlphaPresentationTelemetry.h>
#include <future>
#include <vprenderer/NlsPipelinePreparation.h>
#include <vprenderer/SubtitlePipelinePreparation.h>
#include <vprenderer/TemporalPresentationLedger.h>
#include <SubtitleCutPasteShader.h>
#include <vprenderer/LibplaceboRenderParameters.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/utils/upload.h>
#include <array>
#include <fstream>
#include <cstdint>
#include "RendererProfileConfig.h"
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace {
struct GpuFixture {
    SubtitlePreparationRenderer preparation;
    pl_d3d11 device=nullptr;pl_renderer renderer=nullptr;pl_tex source=nullptr,target=nullptr,clean=nullptr,layer=nullptr,luma=nullptr,chroma=nullptr;
    pl_frame input{},output{},cleanInput{},p010Input{};TemporalRenderer temporal;
    GpuFixture(){
        pl_d3d11_params d{};d.allow_software=true;device=pl_d3d11_create(nullptr,&d);
        Assert::IsTrue(device!=nullptr,L"D3D11 fixture unavailable");renderer=pl_renderer_create(nullptr,device->gpu);
        pl_tex_params params{};params.w=64;params.h=64;
        params.format=pl_find_named_fmt(device->gpu,"rgba8");
        Assert::IsTrue(params.format!=nullptr,L"RGBA8 missing");
        params.sampleable=true;params.renderable=true;params.blit_src=true;params.blit_dst=true;
        params.host_writable=true;params.host_readable=true;
        source=pl_tex_create(device->gpu,&params);target=pl_tex_create(device->gpu,&params);
        const pl_fmt half=pl_find_named_fmt(device->gpu,"rgba16f");
        Assert::IsTrue(half!=nullptr,L"RGBA16F missing");params.format=half;
        clean=pl_tex_create(device->gpu,&params);layer=pl_tex_create(device->gpu,&params);
        Assert::IsTrue(clean&&layer,L"Composition texture allocation failed");
        Assert::IsTrue(source&&target,L"Texture allocation failed");
        input.num_planes=1;input.planes[0].texture=source;input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb;input.color=pl_color_space_srgb;input.crop={0,0,64,64};
        output=input;output.planes[0].texture=target;
        cleanInput=input;cleanInput.planes[0].texture=clean;cleanInput.repr.sys=PL_COLOR_SYSTEM_RGB;
        cleanInput.repr.levels=PL_COLOR_LEVELS_FULL;cleanInput.repr.alpha=PL_ALPHA_NONE;
    }
    ~GpuFixture(){temporal.Clear();preparation.Reset();if(device){pl_tex_destroy(device->gpu,&source);pl_tex_destroy(device->gpu,&target);pl_tex_destroy(device->gpu,&clean);pl_tex_destroy(device->gpu,&layer);pl_tex_destroy(device->gpu,&luma);pl_tex_destroy(device->gpu,&chroma);pl_renderer_destroy(&renderer);pl_d3d11_destroy(&device);}}
    void Upload(int gray,bool fields=false){
        std::vector<unsigned char> pixels(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){const size_t o=(y*64+x)*4;
            pixels[o]=fields?(y%2?0:255):static_cast<unsigned char>(gray);
            pixels[o+1]=fields?0:static_cast<unsigned char>(gray);
            pixels[o+2]=fields?(y%2?255:0):static_cast<unsigned char>(gray);
        }
        pl_tex_transfer_params upload{};upload.tex=source;upload.ptr=pixels.data();
        Assert::IsTrue(pl_tex_upload(device->gpu,&upload));
    }
    void CreateP010AlternatingChroma(bool impulse=false){
        std::vector<uint16_t> y(64*64,uint16_t(500<<6));
        std::vector<uint16_t> uv(32*32*2);
        for(int row=0;row<32;++row)for(int col=0;col<32;++col){
            const size_t o=(row*32+col)*2;
            const int u=impulse?(row==16?900:512):(row%2?300:700);
            const int v=impulse?(row==16?100:512):(row%2?700:300);
            uv[o]=uint16_t(u<<6);uv[o+1]=uint16_t(v<<6);
        }
        pl_tex_params p{};p.w=p.h=64;p.sampleable=true;p.blit_src=true;p.blit_dst=true;
        p.format=pl_find_fmt(device->gpu,PL_FMT_UNORM,1,16,16,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_BLITTABLE));
        Assert::IsTrue(p.format!=nullptr,L"P010 luma format unavailable");
        p.initial_data=y.data();luma=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(luma!=nullptr,L"P010 luma texture unavailable");
        p.w=p.h=32;
        p.format=pl_find_fmt(device->gpu,PL_FMT_UNORM,2,16,16,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_BLITTABLE));
        Assert::IsTrue(p.format!=nullptr,L"P010 chroma format unavailable");
        p.initial_data=uv.data();chroma=pl_tex_create(device->gpu,&p);
        Assert::IsTrue(chroma!=nullptr,L"P010 chroma texture unavailable");
        p010Input.num_planes=2;
        p010Input.planes[0].texture=luma;p010Input.planes[0].components=1;
        p010Input.planes[0].component_mapping[0]=0;
        p010Input.planes[1].texture=chroma;p010Input.planes[1].components=2;
        p010Input.planes[1].component_mapping[0]=1;
        p010Input.planes[1].component_mapping[1]=2;
        p010Input.repr.sys=PL_COLOR_SYSTEM_BT_2020_NC;
        p010Input.repr.levels=PL_COLOR_LEVELS_LIMITED;
        p010Input.repr.alpha=PL_ALPHA_NONE;
        p010Input.repr.bits.sample_depth=16;p010Input.repr.bits.color_depth=10;
        p010Input.repr.bits.bit_shift=6;
        p010Input.color=pl_color_space_srgb;
        p010Input.color.transfer=PL_COLOR_TRC_PQ;
        p010Input.color.primaries=PL_COLOR_PRIM_BT_2020;
        p010Input.color.hdr.min_luma=0.005f;p010Input.color.hdr.max_luma=1000.0f;
        p010Input.crop={0,0,64,64};
    }
    void UploadFloatPixels(pl_tex texture,std::vector<float>& values){
        const pl_fmt format=texture->params.format;
        Assert::IsTrue(format->num_components==4,L"Expected a four-channel texture");
        pl_tex_transfer_params upload{};upload.tex=texture;
        if(format->texel_size==16 && format->host_bits[0]==32){
            upload.ptr=values.data();
            Assert::IsTrue(pl_tex_upload(device->gpu,&upload),L"Float32 host upload failed");
        }else if(format->texel_size==8 && format->host_bits[0]==16){
            std::vector<uint16_t> half(values.size());
            for(size_t i=0;i<values.size();++i)half[i]=values[i]>=1.0f?uint16_t(0x3c00):uint16_t(0);
            upload.ptr=half.data();
            Assert::IsTrue(pl_tex_upload(device->gpu,&upload),L"Float16 host upload failed");
        }else Assert::Fail(L"Unsupported RGBA16F host transfer layout");
    }
    void UploadComposition(bool white,int glyphColor){
        std::vector<float> background(64*64*4);
        std::vector<float> subtitle(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){
            const size_t o=(y*64+x)*4;
            background[o]=background[o+1]=background[o+2]=white?1.0f:0.0f;
            background[o+3]=1.0f;
            if(x>=24&&x<40&&y>=24&&y<40&&glyphColor){
                subtitle[o+(glyphColor==1?0:2)]=1.0f;
                subtitle[o+3]=1.0f;
            }
        }
        UploadFloatPixels(clean,background);
        UploadFloatPixels(layer,subtitle);
    }
    void UploadFieldComposition(){
        std::vector<float> background(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){
            const size_t o=(y*64+x)*4;
            background[o+(y%2?2:0)]=1.0f;
            background[o+3]=1.0f;
        }
        UploadFloatPixels(clean,background);
    }
    std::vector<unsigned char> ReadPixels(){
        std::vector<unsigned char> pixels(64*64*4);
        pl_tex_transfer_params read{};read.tex=target;read.ptr=pixels.data();
        Assert::IsTrue(pl_tex_download(device->gpu,&read));
        return pixels;
    }
    std::array<int,3> Read(int x,int y){
        std::vector<unsigned char> pixels(64*64*4);pl_tex_transfer_params read{};read.tex=target;read.ptr=pixels.data();
        Assert::IsTrue(pl_tex_download(device->gpu,&read));const size_t o=(y*64+x)*4;
        return {pixels[o],pixels[o+1],pixels[o+2]};
    }
    static std::wstring PixelText(const std::array<int,3>& p){
        return L"R="+std::to_wstring(p[0])+L" G="+std::to_wstring(p[1])+L" B="+std::to_wstring(p[2]);
    }
    std::array<int,3> Draw(){
        auto params=pl_render_default_params;params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
        Assert::IsTrue(temporal.Render(renderer,input,output,params),L"Motion render failed");
        std::vector<unsigned char> pixels(64*64*4);pl_tex_transfer_params read{};read.tex=target;read.ptr=pixels.data();
        Assert::IsTrue(pl_tex_download(device->gpu,&read));const size_t o=(32*64+32)*4;
        return {pixels[o],pixels[o+1],pixels[o+2]};
    }
};
}
TEST_CLASS(MotionRendererTests){public:
    TEST_METHOD(SubtitlePreparationReusesCacheAcrossCueGeometryAndAllCompositionPhases) {
        for (int scenario : {0, 1, 2}) {
            GpuFixture g;
            struct Cache { pl_cache value=pl_cache_create(nullptr); ~Cache(){pl_cache_destroy(&value);} } cache;
            pl_gpu_set_cache(g.device->gpu,cache.value);
            g.Upload(100);
            if (scenario==2) g.CreateP010AlternatingChroma();
            auto source=scenario==2?g.p010Input:g.input;
            auto params=pl_render_high_quality_params;
            pl_deinterlace_params deinterlace{};deinterlace.algo=PL_DEINTERLACE_YADIF;
            if(scenario==2) {
                source.field=source.first_field=PL_FIELD_TOP;
                params.deinterlace_params=&deinterlace;
            } else params.deinterlace_params=nullptr;
            const bool temporal=scenario!=0;
            SubtitleGeneratedGrayStyle style;
            auto request=std::make_shared<SubtitlePipelinePreparationRequest>(source,params,temporal,style,3,&g.output);
            CComPtr<IDXGIDevice> dxgi;CComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
            Assert::IsTrue(SUCCEEDED(g.device->device->QueryInterface(IID_PPV_ARGS(&dxgi))));
            Assert::IsTrue(SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&description)));
            request->adapter=description.AdapterLuid;request->featureLevel=g.device->device->GetFeatureLevel();
            NlsPipelinePreparation worker;
            Assert::IsTrue(worker.Submit("subtitles",[request,&cache]{return request->Compile(cache.value);}));
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(300);
            while(worker.Get("subtitles")==NlsPipelinePreparation::Status::Pending && std::chrono::steady_clock::now()<deadline) {
                // The live GPU remains available while the independent worker
                // builds subtitle programs. Draw real changing video throughout.
                g.Upload(100);
                Assert::IsTrue(pl_render_image(g.renderer,&g.input,&g.output,&pl_render_fast_params));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Assert::IsTrue(worker.Get("subtitles")==NlsPipelinePreparation::Status::Ready);
            worker.Stop();
            const auto before=pl_cache_signature(cache.value);
            const auto* hook=CreateSubtitleCutPasteHook(g.device->gpu);
            Assert::IsNotNull(hook);
            const auto processing=CaptureSubtitleSourceProcessingOptions(params);
            pl_frame neighbor=source;source.prev=source.next=scenario==2?&neighbor:nullptr;
            for(int fill:{0,1,2,3})for(int shift:{0,3,7})for(int background:{3,5})for(float radius:{0.0f,3.0f,8.0f}) {
                SubtitleCutPasteGeometry geometry{};geometry.valid=true;geometry.glyphScale=1.0f;
                geometry.pictureTop=16;geometry.pictureBottom=48;
                geometry.source={8+shift,54,55,61};geometry.destination={8+shift,36,55,43};
                geometry.content=geometry.destination;geometry.pictureCapture=geometry.destination;
                geometry.generatedCleanup=geometry.source;geometry.glyphLines[0]=geometry.source;
                style.blurPixels=radius;
                for(auto phase:{SubtitleCompositionPhase::All,SubtitleCompositionPhase::CleanupOnly,SubtitleCompositionPhase::LayerOnly}) {
                    auto input=source;
                    const bool layer=phase==SubtitleCompositionPhase::LayerOnly;
                    if(layer){input.field=input.first_field=PL_FIELD_NONE;input.prev=input.next=nullptr;}
                    Assert::IsTrue(BindSubtitleCutPasteHook(hook,geometry,background,input.color.transfer,style,fill,phase));
                    pl_frame composed{};
                    Assert::IsTrue(RenderSubtitleFullRaster(g.preparation,g.device->gpu,input,hook,
                        layer?&g.layer:&g.clean,composed,layer||temporal,!layer&&background==5,layer,
                        !layer&&scenario==2?&deinterlace:nullptr,temporal?&processing:nullptr));
                }
            }
            // Exercise the newly activated RGB-to-output path as well as the hooks.
            auto normalized=g.cleanInput; normalized.color=source.color;
            auto destination=g.output;
            auto finalParams=params;
            finalParams.deinterlace_params=nullptr; finalParams.frame_mixer=nullptr;
            if(temporal) {
                destination.planes[0].texture=g.layer;
                destination.repr=pl_color_repr_rgb; destination.repr.levels=PL_COLOR_LEVELS_FULL;
                destination.repr.alpha=PL_ALPHA_NONE;
                finalParams.deband_params=nullptr; finalParams.dither_params=nullptr;
                finalParams.error_diffusion=nullptr; finalParams.blend_params=nullptr;
            }
            Assert::IsTrue(pl_render_image(g.renderer,&normalized,&destination,&finalParams));
            pl_gpu_finish(g.device->gpu);
            const auto after=pl_cache_signature(cache.value);
            pl_mpv_user_shader_destroy(&hook);
            pl_gpu_set_cache(g.device->gpu,nullptr);
            Assert::IsTrue(before==after,L"Live subtitle geometry/phase created an unprepared shader");
        }
    }
    TEST_METHOD(NlsPreparationUsesActualCompositionContract) {
        GpuFixture g;
        MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        auto params=pl_render_default_params;
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::AreEqual(0,g.temporal.NlsPreparationInput(g.input,params).num_planes,
            L"First composition frame must defer NLS preparation");
        for(uint64_t i=0;i<4;++i) {
            g.UploadComposition(false,0);
            g.temporal.Begin(settings,24,60,false,0,1,i+1,double(i)/24,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,i+1));
        }
        g.temporal.outputPts=1.0/24;
        g.Draw();
        auto raw=g.input;
        raw.icc=reinterpret_cast<pl_icc_object>(uintptr_t(1));
        raw.film_grain.type=PL_FILM_GRAIN_H274;
        raw.acquire=[](pl_gpu,pl_frame*){return true;};
        const auto actual=g.temporal.NlsPreparationInput(raw,params);
        Assert::AreEqual(1,actual.num_planes);
        Assert::IsTrue(!actual.icc&&!actual.acquire&&!actual.lut);
        Assert::IsTrue(actual.film_grain.type==PL_FILM_GRAIN_NONE);
        Assert::IsTrue(actual.color.transfer==g.cleanInput.color.transfer);
        Assert::IsTrue(actual.planes[0].texture->params.w==g.cleanInput.planes[0].texture->params.w);
        Assert::IsTrue(!params.deband_params&&!params.frame_mixer&&!params.deinterlace_params);
        NlsPipelinePreparation::Frame snapshot;
        snapshot.Copy(actual); // Must not reject the consumed raw metadata.
    }
    TEST_METHOD(NlsPreparationTenSecondGateKeepsLiveFramesRendering) {
        GpuFixture g;
        NlsPipelinePreparation worker;
        struct Gate { std::promise<void> release; ~Gate(){try{release.set_value();}catch(...){}} } gate;
        auto released = gate.release.get_future().share();
        auto entered = std::make_shared<std::promise<void>>();
        auto started = entered->get_future();
        g.Upload(90); g.Draw();
        Assert::IsTrue(worker.Submit("nls", [entered,released] { entered->set_value(); released.wait(); return true; }));
        Assert::IsTrue(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        int rendered = 0;
        while (std::chrono::steady_clock::now() < end) {
            g.Upload((rendered & 1) ? 80 : 120);
            const auto pixel = g.Draw();
            Assert::IsTrue(pixel[0] > 40, L"Fallback output went black during preparation");
            Assert::IsTrue(worker.Get("nls") == NlsPipelinePreparation::Status::Pending);
            ++rendered;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        Assert::IsTrue(rendered > 30, L"Presentation did not progress during the compiler gate");
        gate.release.set_value();
        worker.Stop();
    }
    TEST_METHOD(NlsPreparationOnlyNewestRequestCanActivate) {
        NlsPipelinePreparation worker;
        struct Gate { std::promise<void> release; ~Gate(){try{release.set_value();}catch(...){}} } gate;
        auto released = gate.release.get_future().share();
        auto entered = std::make_shared<std::promise<void>>();
        auto started = entered->get_future();
        auto executed = std::make_shared<std::atomic<int>>(0);
        Assert::IsTrue(worker.Submit("old", [entered,released,executed] { entered->set_value(); released.wait(); ++*executed; return true; }));
        Assert::IsTrue(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        for (int i = 0; i < 100; ++i)
            Assert::IsTrue(worker.Submit(std::to_string(i), [executed] { ++*executed; return true; }));
        gate.release.set_value();
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (worker.Get("99") != NlsPipelinePreparation::Status::Ready && std::chrono::steady_clock::now() < end)
            std::this_thread::yield();
        Assert::IsTrue(worker.Get("99") == NlsPipelinePreparation::Status::Ready);
        Assert::IsTrue(worker.Get("old") == NlsPipelinePreparation::Status::Missing);
        Assert::AreEqual(2, executed->load(), L"More than one active and newest pending job executed");
        worker.Invalidate();
        Assert::IsTrue(worker.Get("99") == NlsPipelinePreparation::Status::Missing);
    }
    TEST_METHOD(NlsPreparationFailureRemainsFailedAndCanRetry) {
        NlsPipelinePreparation worker;
        Assert::IsTrue(worker.Submit("bad", []()->bool {throw std::runtime_error("compile");}));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(worker.Get("bad") == NlsPipelinePreparation::Status::Pending && std::chrono::steady_clock::now() < end)
            std::this_thread::yield();
        Assert::IsTrue(worker.Get("bad") == NlsPipelinePreparation::Status::Failed);
        worker.Invalidate();
        Assert::IsTrue(worker.Submit("bad", [] {return true;}));
        while(worker.Get("bad") == NlsPipelinePreparation::Status::Pending && std::chrono::steady_clock::now() < end)
            std::this_thread::yield();
        Assert::IsTrue(worker.Get("bad") == NlsPipelinePreparation::Status::Ready);
    }
    TEST_METHOD(NlsPreparationCacheReusedAcrossIsolatedDevices) {
        for (const char* name : {"NLS.glsl", "NLSPlus.glsl"}) {
            std::string path = __FILE__;
            for (int i = 0; i < 3; ++i) path.resize(path.find_last_of("\\/"));
            std::ifstream file(path + "\\shaders\\" + name);
            std::string shader((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            Assert::IsFalse(shader.empty());
            for (const auto& item : std::vector<std::pair<std::string,std::string>>{
                {"strength","1.0"},{"curve","2.0"},{"geometry","1"},{"center_protection","0.35"},
                {"axis_balance","0.5"},{"max_center_zoom","1.08"},
                {"horizontal_center_protection","0.35"},{"vertical_center_protection","0.25"}}) {
                const auto token = "{{" + item.first + "}}";
                for (auto pos = shader.find(token); pos != std::string::npos; pos = shader.find(token))
                    shader.replace(pos, token.size(), item.second);
            }
            for (bool hdr : {false, true}) for (bool processed : {false,true}) {
                GpuFixture g;
                struct Cache {pl_cache value=pl_cache_create(nullptr);~Cache(){pl_cache_destroy(&value);}} cache;
                pl_gpu_set_cache(g.device->gpu, cache.value);
                auto params = pl_render_high_quality_params;
                params.dynamic_constants = true;
                params.peak_detect_params = nullptr;
                if (hdr) {
                    g.input.color.transfer=PL_COLOR_TRC_PQ;g.input.color.primaries=PL_COLOR_PRIM_BT_2020;g.input.color.hdr.max_luma=1000;
                    params.frame_mixer=&pl_filter_bicubic; // Wider than the original two-frame prewarm.
                    Assert::IsTrue(pl_frame_mix_radius(&params)>0.75f);
                }
                g.Upload(100);
                Assert::IsTrue(pl_render_image(g.renderer,&g.input,&g.output,&params),L"Ordinary-fit baseline failed");
                auto request = std::make_shared<NlsPipelinePreparation::Request>(g.input,g.output,params,shader);
                CComPtr<IDXGIDevice> dxgi; CComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC description{};
                Assert::IsTrue(SUCCEEDED(g.device->device->QueryInterface(IID_PPV_ARGS(&dxgi))));
                Assert::IsTrue(SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&description)));
                request->adapter=description.AdapterLuid;request->featureLevel=g.device->device->GetFeatureLevel();
                request->stretch=1.25f; request->processedSource=processed;
                NlsPipelinePreparation worker;
                Assert::IsTrue(worker.Submit("gpu", [request,&cache]{return request->Compile(cache.value);}));
                const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while(worker.Get("gpu")==NlsPipelinePreparation::Status::Pending && std::chrono::steady_clock::now()<end)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                Assert::IsTrue(worker.Get("gpu")==NlsPipelinePreparation::Status::Ready,L"Isolated NLS compilation failed");
                worker.Stop();
                const auto before=pl_cache_signature(cache.value);
                const pl_hook* hook=pl_mpv_user_shader_parse(g.device->gpu,shader.data(),shader.size());
                Assert::IsNotNull(hook);
                for(int i=0;i<hook->num_parameters;++i)
                    if(std::string(hook->parameters[i].name)=="stretch_ratio")hook->parameters[i].data->f=1.25f;
                params.hooks=&hook;params.num_hooks=1;
                if(processed) {
                    g.output.planes[0].texture=g.clean;
                    g.output.repr=pl_color_repr_rgb; g.output.repr.levels=PL_COLOR_LEVELS_FULL;
                    g.output.repr.alpha=PL_ALPHA_NONE;
                    params.dither_params=nullptr; params.error_diffusion=nullptr;
                    params.blend_params=nullptr; params.frame_mixer=nullptr;
                }
                g.Upload(100);
                Assert::IsTrue(pl_render_image(g.renderer,&g.input,&g.output,&params));
                if (hdr && !processed) {
                    const pl_frame* frames[8]{};uint64_t signatures[8]{};float timestamps[8]{};
                    for (int count : {1,2,4,8}) {
                        for (int i=0;i<count;++i) {frames[i]=&g.input;signatures[i]=uint64_t(i+1);timestamps[i]=float(i-count/2)+0.25f;}
                        pl_frame_mix mix{};mix.num_frames=count;mix.frames=frames;mix.signatures=signatures;
                        mix.timestamps=timestamps;mix.vsync_duration=0.5f;
                        Assert::IsTrue(pl_render_image_mix(g.renderer,&mix,&g.output,&params));
                    }
                }
                pl_gpu_finish(g.device->gpu);
                const auto after=pl_cache_signature(cache.value);
                pl_mpv_user_shader_destroy(&hook);
                pl_gpu_set_cache(g.device->gpu,nullptr);
                Assert::IsTrue(before==after,L"First live NLS draw generated a missing cache entry");
                Assert::IsTrue(g.Read(32,32)[0]>0,L"Prepared NLS draw returned black");
            }
        }
    }

    TEST_METHOD(DisplayRefreshCacheSurvivesSourceAndBackbufferReuseWithExactPixels) {
        GpuFixture g;
        g.Upload(90);
        auto params=pl_render_default_params;
        Assert::IsTrue(pl_render_image(g.renderer,&g.input,&g.output,&params));
        const auto expected=g.ReadPixels();
        DisplayFrameCache cache;
        Assert::IsTrue(cache.Capture(g.device->gpu,g.target));
        g.Upload(220);
        Assert::IsTrue(pl_render_image(g.renderer,&g.input,&g.output,&params));
        Assert::IsTrue(expected!=g.ReadPixels());
        for(int repeat=0;repeat<5;++repeat) {
            Assert::IsTrue(cache.CopyTo(g.target));
            Assert::IsTrue(expected==g.ReadPixels(),L"Repeated output pixels changed");
        }
        Assert::IsFalse(cache.CopyTo(g.clean),L"An incompatible output format must invalidate the repeat");
        cache.Clear();
        Assert::IsFalse(cache.CopyTo(g.target));
    }
    TEST_METHOD(EveryRefreshDefersToActualMixingIncludingAutoTransitions) {
        MotionConfig::Settings settings;settings.smoothMotion="auto";
        TemporalRenderer temporal;
        const double displays[]={120,60,120,60,0};
        for(size_t i=0;i<5;++i) {
            const bool predicted=TemporalRenderer::WillMix(settings,24,displays[i],false,0);
            temporal.Begin(settings,24,displays[i],false,0,1,i+1,i/24.0,false);
            Assert::AreEqual(predicted,temporal.Mixing());
            Assert::AreEqual(!temporal.Mixing(),PresentationConfig::UseRepeatCadence(true,predicted));
            Assert::IsFalse(PresentationConfig::UseRepeatCadence(false,predicted));
        }
        settings.smoothMotion="off";
        Assert::IsFalse(TemporalRenderer::WillMix(settings,24,60,false,0));
        settings.smoothMotion="always";
        Assert::IsTrue(TemporalRenderer::WillMix(settings,24,120,false,0));
        settings.smoothMotion="auto";
        // Deinterlacing uses a 50 Hz field clock; 100 Hz needs no mixing.
        temporal.Begin(settings,25,100,true,1,2,1,0,false);
        Assert::IsTrue(temporal.Active());Assert::IsFalse(temporal.Mixing());
        Assert::IsTrue(PresentationConfig::UseRepeatCadence(true,temporal.Mixing()));
        Assert::AreEqual(temporal.Mixing(),TemporalRenderer::WillMix(settings,25,100,true,1));
    }
    TEST_METHOD(EngagedSmoothMotionOutputIsUnchangedByEveryRefreshPreference) {
        MotionConfig::Settings settings;settings.smoothMotion="auto";
        TemporalRenderer baseline,candidate;
        DisplaySourceClock sourceClock;
        sourceClock.Accept(100,1,24,1,false);
        sourceClock.Accept(100,2,24,1,false); // old synthetic fallback must retire
        for(uint64_t sequence=1;sequence<20;++sequence) {
            const double pts=sequence/24.0;
            const bool mixing=TemporalRenderer::WillMix(settings,24,60,false,0);
            Assert::IsFalse(PresentationConfig::UseRepeatCadence(true,mixing));
            const double selected=sourceClock.Select(
                PresentationConfig::UseRepeatCadence(true,mixing),pts,sequence,24,1,false);
            Assert::AreEqual(pts,selected,1e-12);
            Assert::IsFalse(sourceClock.Synthetic());
            baseline.Begin(settings,24,60,false,0,1,sequence,pts,false);
            candidate.Begin(settings,24,60,false,0,1,sequence,selected,false);
            Assert::IsTrue(candidate.Mixing());
            Assert::AreEqual(baseline.outputs.size(),candidate.outputs.size());
            for(size_t i=0;i<baseline.outputs.size();++i)
                Assert::AreEqual(baseline.outputs[i],candidate.outputs[i],1e-12);
        }
        // Auto bypass can resume repeats with a fresh, anchored source domain.
        const bool bypassed=TemporalRenderer::WillMix(settings,24,120,false,0);
        Assert::IsFalse(bypassed);
        Assert::AreEqual(20.0,sourceClock.Select(
            PresentationConfig::UseRepeatCadence(true,bypassed),20.0,100,24,1,false),1e-12);
        Assert::IsFalse(sourceClock.Synthetic());
    }
    TEST_METHOD(DisplayRefreshUsesNominalDeinterlacingWithoutEnablingBlending) {
        MotionConfig::Settings settings;
        TemporalRenderer temporal;
        temporal.Begin(settings,20.0,360.0,true,1,1,1,0.0,false);
        Assert::IsTrue(temporal.Active());
        Assert::IsFalse(temporal.Mixing());
        Assert::AreEqual(40.0,temporal.ScheduledOutputRateHz(),1e-9);
        Assert::AreEqual(size_t(2),temporal.outputs.size());
        // Presentation repeats final pixels between these field outputs rather
        // than generating 18 expensive temporal outputs and hitting its cap.
    }
    TEST_METHOD(AnalysisUsesInputSequenceAndAlternatesOnlyAboveThirty) {
        for(double hz:{23.976,24.0,25.0,29.97,30.0,50.0,59.94,60.0,120.0}) {
            unsigned analyses=0;
            for(uint64_t sequence=1;sequence<=120;++sequence) {
                const bool due=InputFrameAnalysisDue(hz,sequence);
                Assert::IsTrue(due==(hz<=30.0 || (sequence&1)!=0));
                for(int output=0;output<4;++output)
                    Assert::IsTrue(due==InputFrameAnalysisDue(hz,sequence));
                if(due)++analyses;
            }
            Assert::AreEqual(hz<=30.0?120u:60u,analyses);
        }
    }
    TEST_METHOD(ProcessedSourceIsReusedAcrossOutputsAndInvalidatesForGeometryAndEpoch) {
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.deinterlace="off";g.Upload(128);
        g.temporal.Begin(settings,24,75,false,0,1,1,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));
        g.temporal.outputPts=0;g.Draw();
        const auto first=g.temporal.ProcessingSnapshot();
        Assert::AreEqual(uint64_t(1),first.sourceRenders);
        g.temporal.outputPts=.005;g.Draw();g.temporal.outputPts=.010;g.Draw();
        const auto repeated=g.temporal.ProcessingSnapshot();
        Assert::AreEqual(first.sourceRenders,repeated.sourceRenders);
        Assert::AreEqual(first.outputRenders+2,repeated.outputRenders);
        auto parameters=pl_render_default_params;
        g.input.crop={4,4,60,60};
        Assert::IsTrue(g.temporal.Render(g.renderer,g.input,g.output,parameters));
        Assert::AreEqual(first.sourceRenders+1,g.temporal.ProcessingSnapshot().sourceRenders);
        Assert::IsTrue(g.temporal.Render(g.renderer,g.input,g.output,parameters,1));
        Assert::AreEqual(first.sourceRenders+2,g.temporal.ProcessingSnapshot().sourceRenders);
    }
    TEST_METHOD(OutputOverlayStaysLiveWhileSourceProcessingIsCached) {
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";settings.deinterlace="off";
        g.Upload(128);g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,75,false,0,1,1,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));g.temporal.outputPts=0;
        pl_overlay_part part{};part.src={0,0,64,64};part.dst={0,0,64,64};
        pl_overlay overlay{};overlay.tex=g.layer;overlay.mode=PL_OVERLAY_NORMAL;
        overlay.coords=PL_OVERLAY_COORDS_DST_FRAME;overlay.repr=pl_color_repr_rgb;
        overlay.repr.alpha=PL_ALPHA_INDEPENDENT;overlay.color=pl_color_space_srgb;
        overlay.parts=&part;overlay.num_parts=1;g.output.overlays=&overlay;g.output.num_overlays=1;
        const auto red=g.Draw();g.UploadComposition(false,2);const auto blue=g.Draw();
        Assert::IsTrue(red[0]>200&&red[2]<20);
        Assert::IsTrue(blue[2]>200&&blue[0]<20);
        Assert::AreEqual(uint64_t(1),g.temporal.ProcessingSnapshot().sourceRenders);
    }
    TEST_METHOD(HdrToneMappedSourceIsReusedAtOutputRate) {
        GpuFixture g;g.CreateP010AlternatingChroma();
        MotionConfig::Settings settings;settings.smoothMotion="always";settings.deinterlace="off";
        g.temporal.Begin(settings,24,75,false,0,1,1,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.p010Input,1));
        auto params=pl_render_default_params;
        auto tone=pl_color_map_default_params;params.color_map_params=&tone;
        g.temporal.outputPts=0;
        Assert::IsTrue(g.temporal.Render(g.renderer,g.p010Input,g.output,params));
        const auto first=g.Read(32,32);
        for(int i=1;i<4;++i) {
            g.temporal.outputPts=i*.005;
            Assert::IsTrue(g.temporal.Render(g.renderer,g.p010Input,g.output,params));
            const auto repeated=g.Read(32,32);
            for(int c=0;c<3;++c)Assert::IsTrue(std::abs(first[c]-repeated[c])<=2);
        }
        Assert::AreEqual(uint64_t(1),g.temporal.ProcessingSnapshot().sourceRenders);
        Assert::AreEqual(uint64_t(4),g.temporal.ProcessingSnapshot().outputRenders);
        g.output.color.transfer=PL_COLOR_TRC_GAMMA22;
        Assert::IsTrue(g.temporal.Render(g.renderer,g.p010Input,g.output,params));
        Assert::AreEqual(uint64_t(2),g.temporal.ProcessingSnapshot().sourceRenders);
    }
    TEST_METHOD(ExplicitCadenceRepeatReusesSubmittedPixelsAndLeavesNextTickUnchanged) {
        for (bool fields : {false,true}) {
            GpuFixture g;
            MotionConfig::Settings settings;settings.smoothMotion="always";
            settings.deinterlace=fields?"force":"off";settings.fieldOrder="top";
            settings.algorithm="bob";
            TemporalRenderer control;
            for(uint64_t i=1;i<=6;++i) {
                const double pts=double(i-1)/24;
                g.temporal.Begin(settings,24,60,fields,1,1,i,pts,false);
                control.Begin(settings,24,60,fields,1,1,i,pts,false);
                g.Upload(int(i*25));
                Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,i));
            }
            g.temporal.outputPts=g.temporal.outputs.back();
            const auto before=g.Draw();g.temporal.MarkSubmitted();
            const auto pts=g.temporal.outputPts;
            const auto selected=g.temporal.SelectedPacketSequence();
            const auto depth=g.temporal.HistoryDepth();
            g.temporal.Begin(settings,24,60,fields,1,1,6,5./24,false,false,true);
            Assert::AreEqual(size_t(1),g.temporal.outputs.size());
            Assert::AreEqual(pts,g.temporal.outputPts,1e-10);
            Assert::AreEqual(selected,g.temporal.SelectedPacketSequence());
            Assert::AreEqual(depth,g.temporal.HistoryDepth());
            const auto repeated=g.Draw();
            for(int c=0;c<3;++c)Assert::AreEqual(before[c],repeated[c]);
            g.temporal.Begin(settings,24,60,fields,1,1,7,6./24,false);
            control.Begin(settings,24,60,fields,1,1,7,6./24,false);
            Assert::AreEqual(control.outputs.size(),g.temporal.outputs.size());
            for(size_t i=0;i<control.outputs.size();++i)
                Assert::AreEqual(control.outputs[i],g.temporal.outputs[i],1e-10);
            g.temporal.Begin(settings,24,60,fields,1,2,7,6./24,false,false,true);
            Assert::AreEqual(size_t(0),g.temporal.HistoryDepth(),L"A repeat must not cross a source generation.");
        }
    }
    TEST_METHOD(ExplicitRepeatKeepsTheSelectedCueInsteadOfTheNewestSourceCue) {
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.deinterlace="off";settings.mixer="linear";
        for(uint64_t i=1;i<=6;++i) {
            g.temporal.Begin(settings,24,60,false,0,1,i,double(i-1)/24,false,true);
            g.UploadComposition(false,i==6?2:1);
            Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,i,{24,24,40,40}));
        }
        g.temporal.outputPts=g.temporal.outputs.back();
        const auto first=g.Draw();g.temporal.MarkSubmitted();
        const uint64_t cue=g.temporal.LastSubtitleSequence();
        Assert::IsTrue(cue>0 && cue<6);
        Assert::IsTrue(first[0]>230 && first[2]<20);
        g.temporal.Begin(settings,24,60,false,0,1,6,5./24,false,true,true);
        const auto repeat=g.Draw();
        Assert::AreEqual(cue,g.temporal.LastSubtitleSequence());
        for(int c=0;c<3;++c)Assert::AreEqual(first[c],repeat[c]);
    }
    TEST_METHOD(UnsubmittedWarmupRepeatCannotCreateOutputOrEraseHistory) {
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false);
        g.Upload(80);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,false,true);
        Assert::IsTrue(g.temporal.skipOutput);Assert::IsTrue(g.temporal.outputs.empty());
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        // An unmarked duplicate retains the original discontinuity behavior.
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
    }
    TEST_METHOD(OptionalAnalysisUsesObservedPhaseWithoutMovingMediaClock) {
        PresentationAnalysisBudget budget;
        budget.Observe(7,1000000,1001000,1000000,60,true,1);
        auto early=budget.Begin(7,1001000,1000000,false,false);
        Assert::IsTrue(early.phaseKnown);Assert::AreEqual(2.0,early.allowanceMs,1e-6);
        auto late=budget.Begin(7,1015000,1000000,false,false);
        Assert::IsTrue(late.phaseKnown);Assert::AreEqual(int64_t(0),late.deadlineQpc);
        Assert::AreEqual(int64_t(1003000),early.deadlineQpc,L"A work ticket cannot roll its deadline forward.");
        Assert::AreEqual(int64_t(0),budget.Begin(7,1001000,1000000,true,false).deadlineQpc);
        Assert::AreEqual(int64_t(0),budget.Begin(7,1001000,1000000,false,true).deadlineQpc);
        Assert::IsFalse(budget.Begin(8,1001000,1000000,false,false).phaseKnown);
        Assert::IsFalse(budget.Begin(7,1300000,1000000,false,false).phaseKnown);
        budget.Observe(7,1016667,1017000,1000000,60,false,1);
        Assert::IsFalse(budget.Begin(7,1018000,1000000,false,false).phaseKnown);
        Assert::AreEqual(int64_t(0),budget.Begin(7,1018000,0,false,false).deadlineQpc);
        budget.Observe(7,1033334,1034000,1000000,60,true,14);
        Assert::AreEqual(int64_t(0),budget.Begin(7,1034000,1000000,false,false).deadlineQpc,
            L"Live analysis and upload preparation must be reserved alongside rendering, leaving no optional work on a busy output.");
    }
    TEST_METHOD(OptionalAnalysisTargetsOnlyReachableDisplayBoundary) {
        constexpr int64_t qpc=1000000;
        PresentationAnalysisBudget slowDisplay;
        // 23.976 Hz: a 12 ms mandatory stage cannot reach a boundary 0.7 ms away.
        slowDisplay.Observe(9,qpc,qpc+41000,qpc,23.976,true,12);
        const auto nearBoundary=slowDisplay.Begin(9,qpc+41000,qpc,false,false);
        Assert::IsTrue(nearBoundary.phaseKnown);
        Assert::IsTrue(nearBoundary.skippedBoundary);
        Assert::IsTrue(nearBoundary.nextBoundaryLeadMs<1.0);
        Assert::AreEqual(12.0,nearBoundary.mandatoryMs,1e-9);
        Assert::AreEqual(15.0,nearBoundary.reserveMs,1e-9);
        Assert::IsTrue(nearBoundary.targetLeadMs>40.0);
        Assert::AreEqual(2.0,nearBoundary.allowanceMs,1e-9);
        Assert::AreEqual(qpc+43000,nearBoundary.deadlineQpc);
        // The issued ticket is immutable even if a later observation changes phase.
        slowDisplay.Observe(9,qpc+41708,qpc+41800,qpc,23.976,true,1);
        Assert::AreEqual(qpc+43000,nearBoundary.deadlineQpc);

        PresentationAnalysisBudget fastDisplay;
        fastDisplay.Observe(10,qpc,qpc+1000,qpc,60,true,1);
        const auto early=fastDisplay.Begin(10,qpc+1000,qpc,false,false);
        Assert::IsTrue(early.phaseKnown);
        Assert::IsFalse(early.skippedBoundary);
        Assert::AreEqual(2.0,early.allowanceMs,1e-9);
        Assert::AreEqual(qpc+3000,early.deadlineQpc);
        const auto late=fastDisplay.Begin(10,qpc+15000,qpc,false,false);
        Assert::IsTrue(late.phaseKnown);
        Assert::IsFalse(late.skippedBoundary,
            L"A safety reserve alone cannot move the target past vblank");
        Assert::AreEqual(int64_t(0),late.deadlineQpc);
        Assert::AreEqual(0.0,late.allowanceMs,1e-9);
        fastDisplay.Observe(10,qpc+33334,qpc+34000,qpc,60,true,14);
        const auto busy=fastDisplay.Begin(10,qpc+34000,qpc,false,false);
        Assert::IsFalse(busy.skippedBoundary);
        Assert::AreEqual(int64_t(0),busy.deadlineQpc,
            L"Reserve blocking optional work must not roll an otherwise reachable frame");
        fastDisplay.Observe(10,qpc+50000,qpc+50500,qpc,60,true,20);
        const auto overloaded=fastDisplay.Begin(10,qpc+50500,qpc,false,false);
        Assert::AreEqual(int64_t(0),overloaded.deadlineQpc);
        Assert::IsFalse(overloaded.skippedBoundary,
            L"Mandatory work longer than a display period cannot fund optional proof");
        Assert::AreEqual(int64_t(0),fastDisplay.Begin(10,qpc+50500,qpc,true,false).deadlineQpc);
        Assert::AreEqual(int64_t(0),fastDisplay.Begin(10,qpc+50500,qpc,false,true).deadlineQpc);
        const auto stale=fastDisplay.Begin(10,qpc+300000,qpc,false,false);
        Assert::IsFalse(stale.phaseKnown);
        Assert::AreEqual(1.0,stale.allowanceMs,1e-9);
        const auto otherGeneration=fastDisplay.Begin(11,qpc+50500,qpc,false,false);
        Assert::IsFalse(otherGeneration.phaseKnown);
        Assert::AreEqual(1.0,otherGeneration.allowanceMs,1e-9);
    }
    TEST_METHOD(OptionalWorkerOwnsPixelsAndNeverWaitsForAnActiveScan) {
        OptionalAnalysisWorker<int,1> worker;
        auto first=std::make_shared<OptionalAnalysisWorker<int,1>::Slot>();
        auto second=std::make_shared<OptionalAnalysisWorker<int,1>::Slot>();
        auto third=std::make_shared<OptionalAnalysisWorker<int,1>::Slot>();
        std::promise<void> entered,release;
        auto gate=release.get_future().share();
        struct Release { std::promise<void>& p; OptionalAnalysisWorker<int,1>& worker; bool done=false;
            void Open(){if(!done){p.set_value();done=true;}}~Release(){Open();worker.Stop();} } guard{release,worker};
        auto pixels=std::make_shared<int>(73);std::weak_ptr<int> lifetime=pixels;
        Assert::IsTrue(worker.Submit(first,1,10,10,[pixels,gate,&entered]{entered.set_value();gate.wait();return *pixels;}));
        Assert::IsTrue(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready);
        pixels.reset();Assert::IsFalse(lifetime.expired());
        Assert::IsTrue(worker.Submit(second,1,11,10,[]{return 11;}));
        Assert::IsFalse(worker.Submit(third,1,12,10,[]{return 12;}));
        Assert::IsFalse(worker.Submit(first,1,10,10,[]{return 99;}));
        Assert::IsTrue(worker.Submit(third,2,20,20,[]{return 20;}));
        Assert::IsFalse(second->pending.load(),L"A new generation must cancel queued old measurements.");
        Assert::IsTrue(OptionalAnalysisWorker<int,1>::Ready(second)==nullptr);
        Assert::IsTrue(OptionalAnalysisWorker<int,1>::Ready(first)==nullptr);
        const auto busy=worker.Snapshot();
        Assert::AreEqual(uint64_t(3),busy.submitted);Assert::AreEqual(uint64_t(0),busy.completed);
        Assert::AreEqual(uint64_t(1),busy.cancelled);Assert::AreEqual(uint64_t(1),busy.full);
        Assert::AreEqual(uint64_t(1),busy.duplicate);Assert::AreEqual(uint64_t(1),busy.queued);
        Assert::AreEqual(uint64_t(1),busy.queueHighWater);
        guard.Open();worker.Stop();
        const auto finished=worker.Snapshot();
        Assert::AreEqual(finished.submitted,finished.completed+finished.cancelled);
        Assert::AreEqual(uint64_t(0),finished.queued);Assert::AreEqual(uint64_t(0),finished.activeAgeUs);
        Assert::AreEqual(finished.completed,finished.cpuSamples);
        Assert::IsTrue(finished.wallUs>=finished.wallPeakUs);
        const auto ready=OptionalAnalysisWorker<int,1>::Ready(first);
        Assert::IsTrue(ready!=nullptr);Assert::AreEqual(73,*ready);
        Assert::IsTrue(lifetime.expired());Assert::IsFalse(first->pending.load());
        Assert::IsFalse(second->pending.load());Assert::IsFalse(third->pending.load());
    }
    TEST_METHOD(AnalysisPerformanceSeparatesSourceBudgetFromDisplayInterval) {
        AnalysisPerformanceWindow p;
        p.Submission(20,18,5,24,60);
        Assert::AreEqual(uint64_t(0),p.sourceOverruns);
        Assert::AreEqual(uint64_t(1),p.displayOverruns);
        p.Submission(43,40,0,24,60);
        Assert::AreEqual(uint64_t(1),p.sourceOverruns);
        Assert::AreEqual(uint64_t(2),p.displayOverruns);
        Assert::AreEqual(31.5,p.active.Average(),1e-9);
        Assert::AreEqual(43.,p.active.peak,1e-9);
        Assert::AreEqual(5.,p.swap.peak,1e-9);
        p.Submission(100,100,0,0,0);
        Assert::AreEqual(uint64_t(1),p.sourceOverruns,L"Unknown rates cannot imply a missed deadline.");
    }
    TEST_METHOD(AnalysisPerformanceRecordsSkippedAndOverBudgetPreviewIndependently) {
        AnalysisPerformanceWindow p;
        p.Preview(false,false,false,0,0);
        p.Preview(true,false,false,1.3,.3);
        p.Preview(true,true,true,.2,0);
        Assert::AreEqual(uint64_t(2),p.attempts);Assert::AreEqual(uint64_t(1),p.skipped);
        Assert::AreEqual(uint64_t(1),p.completed);Assert::AreEqual(uint64_t(1),p.phaseKnown);
        Assert::AreEqual(uint64_t(1),p.deadlineOverruns);
        Assert::AreEqual(.3,p.deadlineOverrunPeakMs,1e-9);
        Assert::AreEqual(.75,p.preview.Average(),1e-9);
        p={};Assert::AreEqual(uint64_t(0),p.attempts);Assert::AreEqual(0.,p.preview.peak);
    }
    TEST_METHOD(OptionalWorkerFailureIsVisibleWithoutInventingEvidence) {
        OptionalAnalysisWorker<int> worker;auto slot=std::make_shared<OptionalAnalysisWorker<int>::Slot>();
        std::promise<void> started;auto entered=started.get_future();
        Assert::IsTrue(worker.Submit(slot,1,1,1,[&]() -> int {started.set_value();throw std::runtime_error("measurement unavailable");}));
        Assert::IsTrue(entered.wait_for(std::chrono::seconds(5))==std::future_status::ready);
        worker.Stop();const auto metrics=worker.Snapshot();
        Assert::AreEqual(uint64_t(1),metrics.completed);Assert::AreEqual(uint64_t(1),metrics.failed);
        Assert::IsFalse(slot->pending.load());Assert::IsTrue(worker.Ready(slot)==nullptr);
    }
    TEST_METHOD(OptionalLookaheadUsesReadyHeadWithoutBridgingMissingFrames) {
        OptionalAnalysisPrefix prefix;
        Assert::IsTrue(prefix.Admit(0,true));Assert::IsTrue(prefix.Admit(1,true));
        Assert::IsFalse(prefix.Admit(2,false));Assert::IsFalse(prefix.Admit(3,true));
        Assert::AreEqual(size_t(2),prefix.Size(),L"One future frame is ready; a later tail cannot extend the proof.");
        OptionalAnalysisPrefix noHead;
        Assert::IsFalse(noHead.Admit(0,false));Assert::IsFalse(noHead.Admit(1,true));
        Assert::AreEqual(size_t(0),noHead.Size());
        OptionalAnalysisPrefix gap;
        Assert::IsTrue(gap.Admit(0,true));Assert::IsFalse(gap.Admit(2,true));
        Assert::AreEqual(size_t(1),gap.Size());
    }
    TEST_METHOD(PrimedArMeasurementsKeepExactReadyPrefixAndSourceOwnership) {
        struct Measurement { uint64_t domain=0; int pixel=0; };
        using Worker=OptionalAnalysisWorker<Measurement,1>;
        Worker worker;
        auto head=std::make_shared<Worker::Slot>();
        auto future=std::make_shared<Worker::Slot>();
        auto beyond=std::make_shared<Worker::Slot>();
        std::promise<void> entered,release,secondCompleted;
        auto releaseGate=release.get_future().share();
        auto secondDone=secondCompleted.get_future();
        struct Finish { std::promise<void>& release; Worker& worker; bool opened=false;
            void Open(){if(!opened){release.set_value();opened=true;}}
            ~Finish(){Open();worker.Stop();}
        } finish{release,worker};
        auto ownedPixels=std::make_shared<int>(73);
        std::weak_ptr<int> lifetime=ownedPixels;
        Assert::IsTrue(worker.Submit(head,2,40,40,[ownedPixels,releaseGate,&entered] {
            entered.set_value();releaseGate.wait();return Measurement{7,*ownedPixels};
        }));
        Assert::IsTrue(entered.get_future().wait_for(std::chrono::seconds(5))==
            std::future_status::ready);
        Assert::IsTrue(worker.Submit(future,2,41,40,[&secondCompleted] {
            secondCompleted.set_value();return Measurement{7,74};
        }));
        Assert::IsFalse(worker.Submit(head,2,40,40,[]{return Measurement{7,99};}),
            L"Priming the same source twice must not enqueue another scan");
        Assert::IsFalse(worker.Submit(beyond,2,42,40,[]{return Measurement{7,75};}),
            L"Bounded future work must not evict the current source");
        ownedPixels.reset();
        Assert::IsFalse(lifetime.expired(),L"Worker owns the exact queued source pixels");
        OptionalAnalysisPrefix pendingHead;
        Assert::IsFalse(pendingHead.Admit(0,Worker::Ready(head)!=nullptr));
        Assert::IsFalse(pendingHead.Admit(1,true),L"A ready future cannot bridge a pending head");
        Assert::AreEqual(size_t(0),pendingHead.Size());
        finish.Open();
        Assert::IsTrue(secondDone.wait_for(std::chrono::seconds(5))==std::future_status::ready);
        worker.Stop();
        const auto headResult=Worker::Ready(head),futureResult=Worker::Ready(future);
        Assert::IsTrue(headResult!=nullptr && futureResult!=nullptr);
        Assert::AreEqual(uint64_t(7),headResult->domain);
        Assert::AreEqual(73,headResult->pixel);
        Assert::AreEqual(74,futureResult->pixel);
        OptionalAnalysisPrefix readyPrefix;
        Assert::IsTrue(readyPrefix.Admit(0,headResult->domain==7));
        Assert::IsTrue(readyPrefix.Admit(1,futureResult->domain==7));
        Assert::AreEqual(size_t(2),readyPrefix.Size());
        OptionalAnalysisPrefix wrongDomain;
        Assert::IsFalse(wrongDomain.Admit(0,headResult->domain==8));
        Assert::IsFalse(wrongDomain.Admit(1,futureResult->domain==8));
        Assert::AreEqual(size_t(0),wrongDomain.Size());
        PresentationAnalysisBudget budget;
        const auto skipped=budget.Begin(2,1000000,1000000,true,false);
        Assert::AreEqual(int64_t(0),skipped.deadlineQpc);
        OptionalAnalysisPrefix expiredBudget;
        if(skipped.deadlineQpc>0) expiredBudget.Admit(0,headResult!=nullptr);
        Assert::AreEqual(size_t(0),expiredBudget.Size(),
            L"A skipped optional budget cannot publish a ready lookahead certificate");
        worker.Stop();
        Assert::IsTrue(lifetime.expired());
        Assert::AreEqual(73,headResult->pixel,
            L"Completed immutable measurement survives worker and source release");
        const auto metrics=worker.Snapshot();
        Assert::AreEqual(uint64_t(2),metrics.submitted);
        Assert::AreEqual(uint64_t(1),metrics.duplicate);
        Assert::AreEqual(uint64_t(1),metrics.full);
    }
    TEST_METHOD(ArWorkerReadyResultFromOldDomainCannotCertifyCurrentSource) {
        struct Measurement { uint64_t domain=0; int value=0; };
        using Worker=OptionalAnalysisWorker<Measurement>;
        Worker worker;
        auto slot=std::make_shared<Worker::Slot>();
        const auto awaitDomain=[&](uint64_t domain) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(std::chrono::steady_clock::now()<deadline) {
                const auto ready=Worker::Ready(slot);
                if(ready && ready->domain==domain && !slot->pending.load()) return ready;
                std::this_thread::yield();
            }
            return std::shared_ptr<const Measurement>{};
        };
        Assert::IsTrue(worker.Submit(slot,2,40,40,[]{return Measurement{7,73};}));
        const auto old=awaitDomain(7);
        Assert::IsTrue(old!=nullptr);
        OptionalAnalysisPrefix wrongDomain;
        Assert::IsFalse(wrongDomain.Admit(0,old->domain==8));
        Assert::AreEqual(size_t(0),wrongDomain.Size());
        Assert::IsTrue(worker.Submit(slot,2,40,40,[]{return Measurement{8,74};}),
            L"The current interpretation must be measured despite an old ready result");
        const auto current=awaitDomain(8);
        worker.Stop();
        Assert::IsTrue(current!=nullptr);
        Assert::AreEqual(74,current->value);
        Assert::AreEqual(73,old->value,L"Replacing ready metadata must not mutate old source evidence");
        const auto metrics=worker.Snapshot();
        Assert::AreEqual(uint64_t(2),metrics.submitted);
        Assert::AreEqual(uint64_t(2),metrics.completed);
    }
    TEST_METHOD(OutputTelemetryKeepsSelectedPacketDistinctFromInputDebtKey) {
        AlphaPresentationTelemetry telemetry;
        AlphaPresentationRecord record;
        record.generation=1;record.sourceSequence=10;record.selectedSourceSequence=8;
        record.selectedSourcePts=.25;record.outputPts=.27;record.subtitleSourceSequence=8;
        record.presentId=42;record.temporalActive=true;
        telemetry.RecordSubmission(record);
        const auto& saved=telemetry.RecentRecords().back();
        Assert::AreEqual(uint64_t(10),saved.sourceSequence);
        Assert::AreEqual(uint64_t(8),saved.selectedSourceSequence);
        Assert::AreEqual(uint32_t(42),saved.presentId);
        Assert::AreEqual(.27,saved.outputPts,1e-9);
        Assert::AreEqual(uint64_t(8),saved.subtitleSourceSequence);
    }

    TEST_METHOD(MotionConfigLoadsAllSupportedOptionsAndRejectsUnknownMixer){
        char directory[MAX_PATH]{},path[MAX_PATH]{};GetTempPathA(MAX_PATH,directory);GetTempFileNameA(directory,"vpm",0,path);
        struct Cleanup{const char* path;~Cleanup(){DeleteFileA(path);}}cleanup{path};
        {std::ofstream file(path);file<<"[vprenderer]\n[vprenderer.motion]\nsmooth_motion: always\nframe_mixer: cubic\ndeinterlace: force\ndeinterlace_algorithm: yadif\nfield_order: bottom\ndeinterlace_cadence: frame\ndeinterlace_skip_spatial: true\n";}
        ConfigFile config;Assert::IsTrue(config.Load(path));RendererProfileConfig::Model model;std::string error;
        Assert::IsTrue(RendererProfileConfig::Read(config,model,error));
        MotionConfig::Settings settings;MotionConfig::Read(config,"vprenderer.motion",settings);
        Assert::IsTrue(settings.smoothMotion=="always"&&settings.mixer=="cubic"&&settings.deinterlace=="force"&&settings.algorithm=="yadif"&&settings.fieldOrder=="bottom"&&settings.cadence=="frame"&&settings.skipSpatial);
        std::string expected;Assert::IsFalse(RendererProfileConfig::ValidateProfileSetting("motion","frame_mixer","rife",expected));
    }

    TEST_METHOD(MatchedCadenceBypassesAndMismatchSchedulesEveryVsync){
        MotionConfig::Settings settings;settings.deinterlace="off";settings.smoothMotion="auto";
        TemporalRenderer r;r.Begin(settings,24,120,false,0,1,1,0,false);
        Assert::IsFalse(r.Active());Assert::AreEqual(size_t(1),r.outputs.size());
        size_t count=0;for(uint64_t i=0;i<24;++i){r.Begin(settings,24,60,false,0,2,i+1,double(i)/24,false);count+=r.outputs.size();}
        Assert::AreEqual(size_t(60),count);Assert::IsTrue(r.Mixing());
    }
    TEST_METHOD(FieldOrderChangeResetsHistoryAndLowerCadenceRetainsIt){
        GpuFixture g;MotionConfig::Settings settings;settings.cadence="frame";
        for(uint64_t i=0;i<3;++i){g.Upload(0,true);g.temporal.Begin(settings,30,60,true,1,1,i+1,double(i)/30,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,i+1));}
        Assert::AreEqual(size_t(3),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,30,60,true,2,1,4,3./30,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,4));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
    }
    TEST_METHOD(CompositionFixtureDirectHalfRenderBaseline){
        GpuFixture g;auto params=pl_render_default_params;
        params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
        g.UploadComposition(true,1);
        Assert::IsTrue(pl_render_image(g.renderer,&g.cleanInput,&g.output,&params));
        const auto first=g.Read(4,4);
        Assert::IsTrue(first[0]>230&&first[1]>230&&first[2]>230,
            (L"RGBA16F clean upload did not render white: "+GpuFixture::PixelText(first)).c_str());
        g.UploadComposition(false,2);
        Assert::IsTrue(pl_render_image(g.renderer,&g.cleanInput,&g.output,&params));
        const auto second=g.Read(4,4);
        Assert::IsTrue(second[0]<20&&second[1]<20&&second[2]<20,
            (L"RGBA16F clean upload did not render black: "+GpuFixture::PixelText(second)).c_str());
    }
    TEST_METHOD(SubtitleCompositionModeKeepsSmoothMotionCadenceAcrossCues){
        MotionConfig::Settings settings;settings.smoothMotion="always";
        TemporalRenderer r;
        size_t count=0;
        for(uint64_t i=1;i<=24;++i){
            const double pts=double(i)/24;
            r.Begin(settings,24,60,false,0,1,i,pts,false,true);
            Assert::IsTrue(r.Active());Assert::IsTrue(r.Mixing());
            count+=r.outputs.size();
        }
        Assert::AreEqual(size_t(60),count);
    }
    TEST_METHOD(CompositionModeTransitionResetsHistoryButKeepsMotionActive){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        g.Upload(50);g.temporal.Begin(settings,24,60,false,0,1,1,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,1,2,1./24,false,true);
        Assert::IsTrue(g.temporal.Active());Assert::IsTrue(g.temporal.Mixing());
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        g.UploadComposition(false,0);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,2));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,1,3,2./24,false,false);
        Assert::IsTrue(g.temporal.Mixing());Assert::IsTrue(g.temporal.outputs.size()>1);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
    }
    TEST_METHOD(CompositionModeRetainsFieldCadenceAndParity){
        MotionConfig::Settings settings;TemporalRenderer r;
        r.Begin(settings,30,60,true,1,1,1,0,false,true);
        Assert::IsTrue(r.Active());Assert::AreEqual(size_t(2),r.outputs.size());
        r.Begin(settings,30,60,true,1,1,2,1./30,false,false);
        Assert::IsTrue(r.Active());Assert::AreEqual(size_t(2),r.outputs.size());
    }
    TEST_METHOD(CompositionBlendsTheCompleteProcessedSourceIncludingSubtitle){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.mixer="linear";settings.deinterlace="off";
        const pl_rect2d glyph{24,24,40,40};
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1,glyph));
        g.temporal.outputPts=-1./24;
        Assert::IsFalse(g.temporal.CompositionOutputReady(),L"Future packet became presentable before its PTS");
        Assert::IsFalse(g.temporal.PreflightCompositionOutput(),L"Warmup output was not held");
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.UploadComposition(true,0);
        g.temporal.Begin(settings,24,60,false,0,1,2,1./24,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,2));
        g.temporal.outputPts=.5/24;
        const auto held=g.Draw();
        const auto mixedBackground=g.Read(4,4);
        Assert::AreEqual(uint64_t(1),g.temporal.LastSubtitleSequence());
        Assert::IsTrue(mixedBackground[0]>20&&mixedBackground[0]<240,(L"Clean background did not mix: "+GpuFixture::PixelText(mixedBackground)+L" glyph "+GpuFixture::PixelText(held)).c_str());
        Assert::IsTrue(held[0]>held[1]+20&&held[1]>20,L"Subtitle did not blend with the complete processed source");
        g.temporal.outputPts=1./24;
        const auto cleared=g.Draw();
        const auto clearBackground=g.Read(4,4);
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
        for(int c=0;c<3;++c)Assert::IsTrue(std::abs(cleared[c]-clearBackground[c])<=5,L"Empty cue retained an earlier glyph");
        g.UploadComposition(false,2);
        g.temporal.Begin(settings,24,60,false,0,1,3,2./24,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,3,glyph));
        g.temporal.outputPts=1.5/24;
        g.Draw();
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
        g.temporal.outputPts=2./24;
        const auto blue=g.Draw();
        Assert::AreEqual(uint64_t(3),g.temporal.LastSubtitleSequence());
        Assert::IsTrue(blue[2]>230&&blue[0]<20&&blue[1]<20,L"Opaque blue glyph did not replace the old cue");
    }
    TEST_METHOD(CompositionSnapshotsSurviveTextureReuseAndDuplicateCapture){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.mixer="linear";settings.deinterlace="off";
        const pl_rect2d glyph{24,24,40,40};
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1,glyph));
        g.UploadComposition(true,2);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1,glyph));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.outputPts=0;
        const auto pixel=g.Draw();
        Assert::AreEqual(uint64_t(1),g.temporal.LastSubtitleSequence());
        Assert::IsTrue(pixel[0]>pixel[2]+30,(L"Duplicate capture replaced the immutable first packet: "+GpuFixture::PixelText(pixel)).c_str());
        for(uint64_t i=2;i<=20;++i){
            g.temporal.Begin(settings,24,60,false,0,1,i,double(i-1)/24,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,i));
            Assert::IsTrue(g.temporal.HistoryDepth()<=8,L"Composition history exceeded its memory bound");
        }
        g.temporal.Clear();
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
    }
    TEST_METHOD(CompositionCaptureFailureDoesNotRetainBorrowedPixels){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        pl_frame invalid=g.cleanInput;invalid.planes[0].texture=nullptr;
        Assert::IsFalse(g.temporal.CaptureComposition(g.device->gpu,invalid,g.layer,1));
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,2,2,1./24,true,true);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,2));
        g.temporal.outputPts=1./24;g.Draw();
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
    }
    TEST_METHOD(CompositionRejectsNonNormalizedCleanFrameAndClearsHistory){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1,{24,24,40,40}));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,1,2,1./24,false,true);
        pl_frame invalid=g.cleanInput;invalid.crop={1,0,64,64};
        Assert::IsFalse(g.temporal.CaptureComposition(g.device->gpu,invalid,nullptr,2));
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
    }
    TEST_METHOD(CompositionModeRendersEveryMixerWithOpaqueGlyphs){
        for(const char* filter:{"oversample","hermite","linear","mitchell_clamp","cubic"}){
            GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
            settings.mixer=filter;settings.deinterlace="off";
            for(uint64_t i=0;i<7;++i){
                g.UploadComposition(i%2!=0,i==3?1:0);
                g.temporal.Begin(settings,24,60,false,0,1,i+1,double(i)/24,false,true);
                Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,
                    i==3?g.layer:nullptr,i+1,{24,24,40,40}));
            }
            g.temporal.outputPts=3./24;
            const auto glyph=g.Draw();
            Assert::AreEqual(uint64_t(4),g.temporal.LastSubtitleSequence());
            Assert::IsTrue(glyph[0]>230&&glyph[1]<20&&glyph[2]<20,(std::wstring(L"Mixer altered the opaque glyph: ")+GpuFixture::PixelText(glyph)).c_str());
        }
    }
    TEST_METHOD(CompositionCapturesSourceFramesWithNoScheduledOutput){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.mixer="linear";settings.deinterlace="off";
        bool sawEmpty=false;
        for(uint64_t i=1;i<=20;++i){
            g.UploadComposition(i%2!=0,0);
            g.temporal.Begin(settings,60,24,false,0,1,i,double(i-1)/60,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,i));
            if(g.temporal.outputs.empty()){
                sawEmpty=true;
                Assert::IsTrue(g.temporal.HasCurrentFrame(i),L"Zero-output source was not retained");
                Assert::IsTrue(g.temporal.skipOutput);
            }
        }
        Assert::IsTrue(sawEmpty,L"Fixture did not exercise a zero-output source frame");
    }
    TEST_METHOD(CompositionSnapshotDoesNotBorrowUploadTextures){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.mixer="linear";settings.deinterlace="off";
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,g.layer,1,{24,24,40,40}));
        pl_tex_destroy(g.device->gpu,&g.clean);pl_tex_destroy(g.device->gpu,&g.layer);
        g.cleanInput.planes[0].texture=nullptr;
        g.temporal.outputPts=0;
        const auto glyph=g.Draw();
        Assert::IsTrue(glyph[0]>230&&glyph[1]<20&&glyph[2]<20,(L"History borrowed an upload texture: "+GpuFixture::PixelText(glyph)).c_str());
    }
    TEST_METHOD(CompositionAppliesCropAndRotationOnce){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.mixer="linear";settings.deinterlace="off";
        std::vector<float> picture(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){
            const size_t o=(y*64+x)*4;
            picture[o]=x<32?1.0f:0.0f;
            picture[o+1]=y<32?1.0f:0.0f;
            picture[o+2]=x+y<64?1.0f:0.0f;picture[o+3]=1.0f;
        }
        g.UploadFloatPixels(g.clean,picture);
        g.input.crop={8,4,56,60};g.input.rotation=PL_ROTATION_90;
        pl_frame direct=g.cleanInput;direct.crop=g.input.crop;direct.rotation=g.input.rotation;
        auto params=pl_render_default_params;
        params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
        Assert::IsTrue(pl_render_image(g.renderer,&direct,&g.output,&params));
        const std::array<std::array<int,2>,5> points{{{8,8},{55,8},{32,32},{8,55},{55,55}}};
        std::array<std::array<int,3>,5> expected{};
        for(size_t i=0;i<points.size();++i)expected[i]=g.Read(points[i][0],points[i][1]);
        g.temporal.Begin(settings,24,60,false,0,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,1));
        g.temporal.outputPts=0;g.Draw();
        for(size_t i=0;i<points.size();++i){
            const auto pixel=g.Read(points[i][0],points[i][1]);
            for(int c=0;c<3;++c)Assert::IsTrue(std::abs(pixel[c]-expected[i][c])<=12,
                (L"Composition crop/rotation differs from direct render at "+
                    std::to_wstring(points[i][0])+L","+std::to_wstring(points[i][1])+L": "+
                    GpuFixture::PixelText(pixel)+L" expected "+GpuFixture::PixelText(expected[i])).c_str());
        }
    }
    TEST_METHOD(NativeCompositionKeepsFirstDuplicateCallbackAndDiscreteLayer){
        GpuFixture g;MotionConfig::Settings settings;settings.cadence="field";
        settings.deinterlace="force";g.Upload(0,true);g.UploadComposition(false,1);
        g.temporal.Begin(settings,30,60,true,1,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,1,
            [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                bounds={24,24,40,40};layer=g.layer;
                return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                    &g.clean,clean,true,false,false,&algorithm);
            },true));
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,1,
            [](const pl_frame&,const pl_deinterlace_params&,pl_frame&,pl_tex&,pl_rect2d&){return false;},false));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.outputPts=0;
        const auto red=g.Draw();
        Assert::AreEqual(uint64_t(1),g.temporal.LastSubtitleSequence());
        Assert::IsTrue(red[0]>230&&red[1]<20&&red[2]<20,
            (L"Native duplicate replaced first cue: "+GpuFixture::PixelText(red)).c_str());
        std::vector<float> blue(64*64*4,0.0f);
        for(int y=24;y<40;++y)for(int x=24;x<40;++x){
            const size_t o=(y*64+x)*4;blue[o+2]=1.0f;blue[o+3]=1.0f;
        }
        g.UploadFloatPixels(g.layer,blue);
        const auto repeated=g.Draw();
        Assert::IsTrue(repeated[0]>230&&repeated[1]<20&&repeated[2]<20,
            L"Prepared native layer borrowed reusable callback texture");
        g.temporal.Begin(settings,30,60,true,1,1,2,1./30,false,true);
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,2,
            [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                layer=nullptr;bounds={};
                return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                    &g.clean,clean,true,false,false,&algorithm);
            }));
        g.temporal.outputPts=1./30;g.Draw();
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
    }
    TEST_METHOD(NativeCompositionRetainsRawTextureAfterUploadRelease){
        GpuFixture g;MotionConfig::Settings settings;settings.cadence="field";
        settings.deinterlace="force";g.Upload(160);
        g.temporal.Begin(settings,30,60,true,1,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,1,
            [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                layer=nullptr;bounds={};
                return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                    &g.clean,clean,true,false,false,&algorithm);
            }));
        pl_tex_destroy(g.device->gpu,&g.source);
        g.input.planes[0].texture=g.clean;
        g.temporal.outputPts=0;
        const auto pixel=g.Draw();
        for(int c=0;c<3;++c)Assert::IsTrue(std::abs(pixel[c]-160)<=18,
            (L"Native history borrowed released raw texture: "+GpuFixture::PixelText(pixel)).c_str());
    }
    TEST_METHOD(NativeCompositionOversampleMixesAdjacentCleanFields){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        settings.cadence="frame";settings.mixer="oversample";settings.deinterlace="force";
        for(uint64_t i=0;i<5;++i){
            g.Upload(i%2?255:0);
            g.temporal.Begin(settings,30,60,true,1,1,i+1,double(i)/30,false,true);
            Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,i+1,
                [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                    pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                    layer=nullptr;bounds={};
                    return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                        &g.clean,clean,true,false,false,&algorithm);
                }));
        }
        g.temporal.outputPts=.75/30;
        const auto mixed=g.Draw();
        std::vector<float> white(64*64*4,1.0f);
        g.UploadFloatPixels(g.layer,white);
        pl_frame blackFrame=g.input,whiteFrame=g.cleanInput;
        blackFrame.repr.sys=PL_COLOR_SYSTEM_RGB;
        blackFrame.repr.levels=PL_COLOR_LEVELS_FULL;blackFrame.repr.alpha=PL_ALPHA_NONE;
        whiteFrame.planes[0].texture=g.layer;
        const pl_frame* pointers[]={&blackFrame,&whiteFrame,&blackFrame};
        const uint64_t signatures[]={1,2,3};
        const float timestamps[]={-.75f,.25f,1.25f};
        pl_frame_mix direct{};direct.num_frames=3;direct.frames=pointers;
        direct.signatures=signatures;direct.timestamps=timestamps;direct.vsync_duration=.5f;
        auto params=pl_render_fast_params;params.frame_mixer=g.temporal.MixerParameters();
        pl_frame linearTarget=g.output;linearTarget.color.transfer=PL_COLOR_TRC_LINEAR;
        Assert::IsTrue(pl_render_image_mix(g.renderer,&direct,&linearTarget,&params));
        const auto expected=g.Read(32,32);
        Assert::IsTrue(expected[0]>40&&expected[0]<215,
            (L"Direct linear oversample reference did not blend: "+GpuFixture::PixelText(expected)).c_str());
        auto decodeSrgb=[](int code){
            const double value=double(code)/255.0;
            return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);
        };
        for(int c=0;c<3;++c)Assert::IsTrue(
            std::abs(decodeSrgb(mixed[c])-double(expected[c])/255.0)<=.03,
            (L"Native oversample differs from direct linear reference: "+GpuFixture::PixelText(mixed)+
                L" expected linear "+GpuFixture::PixelText(expected)).c_str());
        Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
    }
    TEST_METHOD(NativeCompositionPreparationFailureClearsHistory){
        GpuFixture g;MotionConfig::Settings settings;settings.cadence="field";
        settings.deinterlace="force";
        g.Upload(100);
        g.temporal.Begin(settings,30,60,true,1,1,1,0,false,true);
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,1,
            [](const pl_frame&,const pl_deinterlace_params&,pl_frame&,pl_tex&,pl_rect2d&){return false;}));
        g.temporal.outputPts=0;
        auto params=pl_render_default_params;
        params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
        Assert::IsFalse(g.temporal.PreflightCompositionOutput());
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::IsFalse(g.temporal.Render(g.renderer,g.input,g.output,params),
            L"Failed native preparation silently fell back to raw input");
        g.temporal.Begin(settings,30,60,true,1,1,2,1./30,false,true);
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,2,
            [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                layer=nullptr;bounds={};
                return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                    &g.clean,clean,true,false,false,&algorithm);
            }));
        g.temporal.outputPts=1./30;
        Assert::IsTrue(g.temporal.PreflightCompositionOutput());
        g.Draw();
    }
    TEST_METHOD(CompositionDeinterlaceProducesCorrectTopAndBottomFields){
        for(const char* algorithm:{"bob","yadif","bwdif"})for(int order:{1,2}){
            GpuFixture g;MotionConfig::Settings settings;settings.cadence="field";
            settings.algorithm=algorithm;settings.deinterlace="force";
            for(uint64_t i=0;i<5;++i){
                g.Upload(0,true);
                g.temporal.Begin(settings,30,60,true,order,1,i+1,double(i)/30,false,true);
                Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,i+1,
                    [&g](const pl_frame& field,const pl_deinterlace_params& algorithm,
                        pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                        layer=nullptr;bounds={};
                        return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                            &g.clean,clean,true,false,false,&algorithm);
                    }));
            }
            pl_frame direct=g.input;
            direct.first_field=order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM;
            direct.prev=&g.input;direct.next=&g.input;
            auto params=pl_render_default_params;
            params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
            auto directDeinterlace=g.temporal.DeinterlaceParameters();
            params.deinterlace_params=&directDeinterlace;
            direct.field=direct.first_field;
            Assert::IsTrue(pl_render_image(g.renderer,&direct,&g.output,&params));
            const auto directFirst=g.Read(32,32);const auto directFirstOdd=g.Read(32,33);
            direct.field=pl_field_other(direct.first_field);
            Assert::IsTrue(pl_render_image(g.renderer,&direct,&g.output,&params));
            const auto directSecond=g.Read(32,32);const auto directSecondOdd=g.Read(32,33);
            g.temporal.outputPts=3./30;
            const auto first=g.Draw();const auto firstOdd=g.Read(32,33);const auto firstErrors=pl_renderer_get_errors(g.renderer).errors;
            Assert::IsTrue(g.temporal.lastSubmittedField==(order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM));
            g.temporal.outputPts=3.5/30;
            const auto second=g.Draw();const auto secondOdd=g.Read(32,33);const auto secondErrors=pl_renderer_get_errors(g.renderer).errors;
            Assert::IsTrue(g.temporal.lastSubmittedField==(order==1?PL_FIELD_BOTTOM:PL_FIELD_TOP));
            const bool firstRed=first[0]>first[2]+30;
            const bool secondRed=second[0]>second[2]+30;
            const std::wstring context=std::wstring(algorithm,algorithm+strlen(algorithm))+L" order "+std::to_wstring(order)+L" first "+GpuFixture::PixelText(first)+L" second "+GpuFixture::PixelText(second)+L" firstOdd "+GpuFixture::PixelText(firstOdd)+L" secondOdd "+GpuFixture::PixelText(secondOdd)+L" direct "+GpuFixture::PixelText(directFirst)+L"/"+GpuFixture::PixelText(directSecond)+L" directOdd "+GpuFixture::PixelText(directFirstOdd)+L"/"+GpuFixture::PixelText(directSecondOdd)+L" errors "+std::to_wstring(static_cast<int>(firstErrors))+L"/"+std::to_wstring(static_cast<int>(secondErrors));
            for(int c=0;c<3;++c){
                Assert::IsTrue(std::abs(first[c]-directFirst[c])<=8&&
                    std::abs(second[c]-directSecond[c])<=8&&
                    std::abs(firstOdd[c]-directFirstOdd[c])<=8&&
                    std::abs(secondOdd[c]-directSecondOdd[c])<=8,
                    (L"Native field differs from direct deinterlace: "+context).c_str());
            }
            if(std::string(algorithm)!="bwdif"){
                Assert::IsTrue(firstRed != secondRed,(L"Alternating fields did not deinterlace: "+context).c_str());
                Assert::IsTrue(firstRed==(order==1),(L"First deinterlaced field has the wrong color: "+context).c_str());
            }
        }
    }
    TEST_METHOD(NativeCompositionSnapshotsDolbyMetadataBeforeCallback){
        GpuFixture g;MotionConfig::Settings settings;settings.cadence="field";
        settings.deinterlace="force";g.Upload(80);
        pl_dovi_metadata metadata{};metadata.nonlinear_offset[0]=.125f;
        g.input.repr.dovi=&metadata;
        g.temporal.Begin(settings,30,60,true,1,1,1,0,false,true);
        bool checked=false;
        Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.input,1,
            [&g,&metadata,&checked](const pl_frame& field,const pl_deinterlace_params& algorithm,
                pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                Assert::IsTrue(field.repr.dovi!=nullptr&&field.repr.dovi!=&metadata,
                    L"Native callback retained caller Dolby metadata pointer");
                Assert::IsTrue(std::abs(field.repr.dovi->nonlinear_offset[0]-.125f)<1e-6f,
                    L"Native callback saw mutated Dolby metadata");
                checked=true;layer=nullptr;bounds={};
                return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                    &g.clean,clean,true,false,false,&algorithm);
            }));
        metadata.nonlinear_offset[0]=.75f;
        g.temporal.outputPts=0;g.Draw();
        Assert::IsTrue(checked,L"Native callback was never invoked");
    }
    TEST_METHOD(NativeP010ChromaUpscalerChangesNoCuePixelsLikeDirectRender){
        std::array<std::vector<unsigned char>,2> native{},direct{};
        const pl_filter_config* filters[]={&pl_filter_bilinear,&pl_filter_bicubic};
        for(int mode=0;mode<2;++mode){
            GpuFixture g;g.CreateP010AlternatingChroma(true);
            MotionConfig::Settings settings;settings.cadence="field";
            settings.algorithm="bob";settings.deinterlace="force";
            auto sourceParams=pl_render_default_params;
            sourceParams.plane_upscaler=filters[mode];
            sourceParams.dither_params=nullptr;sourceParams.deband_params=nullptr;
            sourceParams.peak_detect_params=nullptr;
            const auto sourceOptions=CaptureSubtitleSourceProcessingOptions(sourceParams);
            for(uint64_t i=0;i<3;++i){
                g.temporal.Begin(settings,30,60,true,1,1,i+1,double(i)/30,false,true);
                Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.p010Input,i+1,
                    [&g,sourceOptions](const pl_frame& field,const pl_deinterlace_params& deinterlace,
                        pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                        layer=nullptr;bounds={};
                        return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                            &g.clean,clean,true,false,false,&deinterlace,&sourceOptions);
                    }));
            }
            g.temporal.outputPts=1./30;g.Draw();native[mode]=g.ReadPixels();
            pl_frame raw=g.p010Input;raw.field=PL_FIELD_TOP;raw.first_field=PL_FIELD_TOP;
            raw.prev=&g.p010Input;raw.next=&g.p010Input;
            auto deinterlace=g.temporal.DeinterlaceParameters();
            sourceParams.deinterlace_params=&deinterlace;
            Assert::IsTrue(pl_render_image(g.renderer,&raw,&g.output,&sourceParams),
                L"Direct P010 chroma-filter render failed");
            direct[mode]=g.ReadPixels();
        }
        int directDelta=0,nativeDelta=0,deltaError=0;
        for(int y=8;y<56;++y)for(int x=8;x<56;++x)for(int c=0;c<3;++c){
            const size_t o=(y*64+x)*4+c;
            const int reference=int(direct[0][o])-int(direct[1][o]);
            const int actual=int(native[0][o])-int(native[1][o]);
            directDelta=(std::max)(directDelta,std::abs(reference));
            nativeDelta=(std::max)(nativeDelta,std::abs(actual));
            if(std::abs(reference)>=4)
                deltaError=(std::max)(deltaError,std::abs(actual-reference));
        }
        Assert::IsTrue(directDelta>=4,
            (L"P010 impulse did not distinguish direct chroma filters: max="+
                std::to_wstring(directDelta)).c_str());
        Assert::IsTrue(nativeDelta>=4,
            (L"Native no-cue P010 lost configured chroma filter: max="+
                std::to_wstring(nativeDelta)).c_str());
        Assert::IsTrue(deltaError<=10,
            (L"Native chroma-filter pixel effect differs from direct render: max="+
                std::to_wstring(deltaError)+L" direct="+std::to_wstring(directDelta)+
                L" native="+std::to_wstring(nativeDelta)).c_str());
    }
    TEST_METHOD(NativeP010ChromaFieldsMatchDirectLibplacebo){
        for(const char* algorithm:{"bob","yadif","bwdif"})for(int order:{1,2}){
            GpuFixture g;g.CreateP010AlternatingChroma();
            MotionConfig::Settings settings;settings.cadence="field";
            settings.algorithm=algorithm;settings.deinterlace="force";
            for(uint64_t i=0;i<5;++i){
                g.temporal.Begin(settings,30,60,true,order,1,i+1,double(i)/30,false,true);
                Assert::IsTrue(g.temporal.CaptureNativeComposition(g.device->gpu,g.p010Input,i+1,
                    [&g](const pl_frame& field,const pl_deinterlace_params& deinterlace,
                        pl_frame& clean,pl_tex& layer,pl_rect2d& bounds){
                        layer=nullptr;bounds={};
                        return RenderSubtitleFullRaster(g.preparation,g.device->gpu,field,nullptr,
                            &g.clean,clean,true,false,false,&deinterlace);
                    }));
            }
            auto renderDirect=[&](pl_field parity){
                pl_frame raw=g.p010Input;raw.first_field=order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM;
                raw.field=parity;raw.prev=&g.p010Input;raw.next=&g.p010Input;
                const auto deinterlace=g.temporal.DeinterlaceParameters();
                pl_frame normalized{};
                Assert::IsTrue(RenderSubtitleFullRaster(g.preparation,g.device->gpu,raw,nullptr,
                    &g.clean,normalized,true,false,false,&deinterlace),L"Direct P010 normalization failed");
                auto params=pl_render_default_params;
                params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
                Assert::IsTrue(pl_render_image(g.renderer,&normalized,&g.output,&params),L"Direct P010 output failed");
                return g.Read(32,32);
            };
            auto renderRaw=[&](pl_field parity){
                pl_frame raw=g.p010Input;raw.first_field=order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM;
                raw.field=parity;raw.prev=&g.p010Input;raw.next=&g.p010Input;
                auto deinterlace=g.temporal.DeinterlaceParameters();
                auto params=pl_render_default_params;
                params.dither_params=nullptr;params.deband_params=nullptr;params.peak_detect_params=nullptr;
                params.deinterlace_params=&deinterlace;
                Assert::IsTrue(pl_render_image(g.renderer,&raw,&g.output,&params),
                    L"Independent raw P010 deinterlace failed");
                return g.Read(32,32);
            };
            const auto firstField=order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM;
            const auto directFirst=renderDirect(firstField);
            const auto directSecond=renderDirect(pl_field_other(firstField));
            const auto rawFirst=renderRaw(firstField);
            const auto rawSecond=renderRaw(pl_field_other(firstField));
            g.temporal.outputPts=3./30;
            const auto nativeFirst=g.Draw();
            g.temporal.outputPts=3.5/30;
            const auto nativeSecond=g.Draw();
            const std::wstring label=std::wstring(algorithm,algorithm+strlen(algorithm))+
                L" order "+std::to_wstring(order)+L" direct "+GpuFixture::PixelText(directFirst)+
                L"/"+GpuFixture::PixelText(directSecond)+L" raw "+
                GpuFixture::PixelText(rawFirst)+L"/"+GpuFixture::PixelText(rawSecond)+
                L" native "+GpuFixture::PixelText(nativeFirst)+L"/"+GpuFixture::PixelText(nativeSecond);
            for(int c=0;c<3;++c){
                Assert::IsTrue(std::abs(nativeFirst[c]-directFirst[c])<=4&&
                    std::abs(nativeSecond[c]-directSecond[c])<=4,
                    (L"Native P010 chroma field differs from normalized reference: "+label).c_str());
                Assert::IsTrue(std::abs(nativeFirst[c]-rawFirst[c])<=12&&
                    std::abs(nativeSecond[c]-rawSecond[c])<=12,
                    (L"Native P010 chroma field differs from independent raw deinterlace: "+label).c_str());
            }
            Assert::AreEqual(uint64_t(0),g.temporal.LastSubtitleSequence());
        }
    }
    TEST_METHOD(DefaultOffDoesNotAllocateTemporalHistory){
        MotionConfig::Settings settings;TemporalRenderer r;r.Begin(settings,24,60,false,0,1,1,0,false);
        Assert::IsFalse(r.Active());Assert::AreEqual(0.0,r.DelayMs());
    }
    TEST_METHOD(UnknownFieldOrderDoesNotDeinterlaceProgressiveOrGuessInterlace){
        MotionConfig::Settings settings;TemporalRenderer r;r.Begin(settings,30,60,true,0,1,1,0,false);
        Assert::IsFalse(r.Active());Assert::IsTrue(r.status.find("field order unavailable")!=std::string::npos);
        r.Begin(settings,30,60,false,1,1,2,1./30,false);Assert::IsFalse(r.Active());
    }
    TEST_METHOD(LinearMixerRetainsSourceAfterUploadTextureIsReleased){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        for(uint64_t i=0;i<6;++i){g.Upload(static_cast<int>(i)*30);g.temporal.Begin(settings,24,60,false,0,1,i+1,double(i)/24,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,i+1));}
        Assert::AreEqual(size_t(6),g.temporal.HistoryDepth());
        g.temporal.outputPts=3.5/24.;pl_tex_destroy(g.device->gpu,&g.source);g.input.planes[0].texture=nullptr;
        g.Draw();
    }
    TEST_METHOD(AllMixerFiltersRenderOnGpu){
        for(const char* filter:{"oversample","hermite","linear","mitchell_clamp","cubic"}){
            GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";settings.mixer=filter;settings.deinterlace="off";
            for(uint64_t i=0;i<7;++i){g.Upload(static_cast<int>(i)*25);g.temporal.Begin(settings,24,60,false,0,1,i+1,double(i)/24,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,i+1));}
            const pl_filter_config* requested=nullptr;
            for(const pl_filter_preset* preset=pl_frame_mixers;preset->name;++preset)if(settings.mixer==preset->name)requested=preset->filter;
            Assert::IsTrue(g.temporal.MixerParameters()==requested);g.temporal.outputPts=3.5/24.;g.Draw();
        }
    }
    TEST_METHOD(AlgorithmsReceiveBothFieldsInTopAndBottomOrder){
        for(const char* algorithm:{"bob","yadif","bwdif"})for(int order:{1,2}){
            GpuFixture g;MotionConfig::Settings settings;settings.algorithm=algorithm;
            for(uint64_t i=0;i<5;++i){g.Upload(0,true);g.temporal.Begin(settings,30,60,true,order,1,i+1,double(i)/30,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,i+1));}
            Assert::IsTrue(g.temporal.DeinterlaceParameters().algo==(settings.algorithm=="bob"?PL_DEINTERLACE_BOB:settings.algorithm=="yadif"?PL_DEINTERLACE_YADIF:PL_DEINTERLACE_BWDIF));
            g.temporal.outputPts=3./30;g.Draw();
            Assert::IsTrue(g.temporal.lastSubmittedField==(order==1?PL_FIELD_TOP:PL_FIELD_BOTTOM),L"First submitted field parity incorrect");
            g.temporal.outputPts=3.5/30;g.Draw();
            Assert::IsTrue(g.temporal.lastSubmittedField==(order==1?PL_FIELD_BOTTOM:PL_FIELD_TOP),L"Second submitted field parity incorrect");
        }
    }
    TEST_METHOD(SubtitleStyleEpochChangeClearsSameSourceBeforeRecapture){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        g.UploadComposition(false,1);
        g.temporal.Begin(settings,24,60,false,0,101,7,0,false,true);
        Assert::IsTrue(g.temporal.CaptureComposition(
            g.device->gpu,g.cleanInput,g.layer,7,{24,24,40,40}));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.UploadComposition(false,2);
        // The host folds subtitle style into the capture epoch before Begin.
        // A paused packet can keep its source sequence while style changes.
        g.temporal.Begin(settings,24,60,false,0,102,7,0,false,true);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::IsTrue(g.temporal.CaptureComposition(
            g.device->gpu,g.cleanInput,g.layer,7,{24,24,40,40}));
        g.temporal.outputPts=0;
        const auto glyph=g.Draw();
        Assert::IsTrue(glyph[2]>230&&glyph[0]<20&&glyph[1]<20,
            (std::wstring(L"Previous subtitle style survived capture epoch: ")+
                GpuFixture::PixelText(glyph)).c_str());
    }    TEST_METHOD(ClassicZeroOutputPacketReplaysItsOwnCropAtLaterGpuOutput){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        std::vector<unsigned char> picture(64*64*4,255);
        for(int y=0;y<64;++y)for(int x=0;x<32;++x){
            const size_t o=(y*64+x)*4;
            picture[o]=picture[o+1]=picture[o+2]=0;
        }
        pl_tex_transfer_params upload{};upload.tex=g.source;upload.ptr=picture.data();
        Assert::IsTrue(pl_tex_upload(g.device->gpu,&upload));
        TemporalPresentationLedger<int> crops;
        std::array<bool,40> skipped{};
        int evaluations=0;
        bool replayed=false;
        for(uint64_t sequence=1;sequence<=32&&!replayed;++sequence){
            g.temporal.Begin(settings,60,24,false,0,1,sequence,
                double(sequence-1)/60,false,false);
            Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,sequence));
            skipped[sequence]=g.temporal.skipOutput;
            Assert::IsTrue(crops.Ensure({1,sequence,1,1},[&](int& half){
                ++evaluations;half=skipped[sequence]?0:1;return true;
            }));
            Assert::AreEqual(int(sequence),evaluations);
            if(skipped[sequence]){
                Assert::IsTrue(g.temporal.HasCurrentFrame(sequence));
                continue;
            }
            const uint64_t selected=g.temporal.SelectedPacketSequence();
            if(!selected||selected==sequence||!skipped[selected])continue;
            const int* selectedHalf=crops.Find({1,selected,1,1});
            Assert::IsNotNull(selectedHalf);
            Assert::AreEqual(0,*selectedHalf);
            g.input.crop={0,0,32,64};
            const auto pixel=g.Draw();
            Assert::IsTrue(pixel[0]<20&&pixel[1]<20&&pixel[2]<20,
                (std::wstring(L"Classic skipped source lost its selected crop: ")+
                    GpuFixture::PixelText(pixel)).c_str());
            replayed=true;
        }
        Assert::IsTrue(replayed,
            L"Classic 60-to-24 scheduler did not replay a skipped source packet");
    }    TEST_METHOD(ZeroOutputPacketReplaysItsOwnCropAtLaterGpuOutput){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        std::vector<float> picture(64*64*4,1.0f);
        for(int y=0;y<64;++y)for(int x=0;x<32;++x){
            const size_t o=(y*64+x)*4;
            picture[o]=picture[o+1]=picture[o+2]=0.0f;
        }
        g.UploadFloatPixels(g.clean,picture);
        TemporalPresentationLedger<int> crops;
        std::array<bool,40> sourceHadNoOutput{};
        int sourceEvaluations=0;
        bool replayedSkippedSource=false;
        for(uint64_t sequence=1;sequence<=32&&!replayedSkippedSource;++sequence){
            const double pts=double(sequence-1)/60.0;
            g.temporal.Begin(settings,60,24,false,0,1,sequence,pts,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(
                g.device->gpu,g.cleanInput,nullptr,sequence));
            const bool noOutput=g.temporal.skipOutput;
            sourceHadNoOutput[sequence]=noOutput;
            const TemporalPresentationKey key{1,sequence,1,1};
            Assert::IsTrue(crops.Ensure(key,[&](int& cropHalf){
                ++sourceEvaluations;
                cropHalf=noOutput?0:1;
                return true;
            }));
            Assert::AreEqual(int(sequence),sourceEvaluations);
            if(noOutput){
                Assert::IsTrue(g.temporal.HasCurrentFrame(sequence));
                continue;
            }
            const uint64_t selected=g.temporal.SelectedPacketSequence();
            if(!selected||selected==sequence||!sourceHadNoOutput[selected])continue;
            const int* selectedHalf=crops.Find({1,selected,1,1});
            Assert::IsNotNull(selectedHalf);
            Assert::AreEqual(0,*selectedHalf);
            g.input.crop=*selectedHalf==0?pl_rect2df{0,0,32,64}:pl_rect2df{32,0,64,64};
            const auto selectedPixel=g.Draw();
            Assert::IsTrue(selectedPixel[0]<20&&selectedPixel[1]<20&&selectedPixel[2]<20,
                (std::wstring(L"Delayed zero-output source lost its crop: ")+
                    GpuFixture::PixelText(selectedPixel)).c_str());
            const int evaluationsBeforeRepeat=sourceEvaluations;
            Assert::IsTrue(crops.Ensure(key,[&](int&){++sourceEvaluations;return true;}));
            Assert::AreEqual(evaluationsBeforeRepeat,sourceEvaluations);
            const auto repeatedPixel=g.Draw();
            Assert::AreEqual(selectedPixel[0],repeatedPixel[0]);
            replayedSkippedSource=true;
        }
        Assert::IsTrue(replayedSkippedSource,
            L"60-to-24 scheduler never selected a previously captured zero-output packet");
    }    TEST_METHOD(IncompatibleInterlaceNeighborsFallBackToBob)
    {
        auto renderField=[](const char* algorithm,bool invalidateNeighbors){
            GpuFixture g;MotionConfig::Settings settings;
            settings.deinterlace="force";settings.algorithm=algorithm;
            settings.cadence="frame";settings.smoothMotion="off";
            TemporalRenderer::PresentationGeometry geometry{};
            geometry.familyBase={0,0,64,64};geometry.sourceCrop={0,0,64,64};
            geometry.viewportEpoch=4;geometry.owner=1;geometry.valid=true;
            for(uint64_t sequence=1;sequence<=3;++sequence){
                g.Upload(0,true);
                g.temporal.Begin(settings,30,30,true,1,1,sequence,
                    double(sequence-1)/30,false,false);
                Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,sequence));
                auto packet=geometry;
                if(invalidateNeighbors && sequence!=2)
                    packet.familyBase={0,4,64,60};
                Assert::IsTrue(g.temporal.AnnotatePresentation(sequence,packet));
            }
            g.temporal.outputPts=1./30;
            Assert::AreEqual(uint64_t(2),g.temporal.SelectedPacketSequence());
            g.Draw();
            return g.ReadPixels();
        };
        const auto expectedBob=renderField("bob",false);
        const auto missingNeighbors=renderField("bwdif",true);
        Assert::AreEqual(expectedBob.size(),missingNeighbors.size());
        int maxError=0;
        for(size_t i=0;i<expectedBob.size();++i)
            maxError=(std::max)(maxError,
                std::abs(int(expectedBob[i])-int(missingNeighbors[i])));
        Assert::IsTrue(maxError<=2,
            (std::wstring(L"Incompatible BWDIF neighbors did not fall back to Bob; max error=")+
                std::to_wstring(maxError)).c_str());
    }    TEST_METHOD(EmptyPixelSafeBandsPermitMixAcrossDifferentSourceCrops){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        TemporalRenderer::PresentationGeometry geometry{};
        geometry.familyBase={0,0,64,64};geometry.sourceCrop={0,0,64,64};
        geometry.viewportEpoch=4;geometry.owner=1;
        geometry.valid=true;geometry.visibleSafe=true;
        for(uint64_t sequence=1;sequence<=3;++sequence){
            g.Upload(sequence==2?255:0);
            g.temporal.Begin(settings,24,60,false,0,1,sequence,
                double(sequence-1)/24,false,false);
            Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,sequence));
            auto candidate=geometry;
            if(sequence==3)candidate.sourceCrop={0,2,64,62};
            Assert::IsTrue(g.temporal.AnnotatePresentation(sequence,candidate));
        }
        g.temporal.outputPts=1.5/24;
        Assert::AreEqual(uint64_t(2),g.temporal.SelectedPacketSequence());
        const auto unknownBands=g.Draw();
        Assert::IsTrue(unknownBands[0]>220,
            (std::wstring(L"Unknown empty bands incorrectly mixed: ")+
                GpuFixture::PixelText(unknownBands)).c_str());

        auto safe=geometry;safe.sourceCrop={0,2,64,62};safe.emptyBandsSafe=true;
        Assert::IsTrue(g.temporal.AnnotatePresentation(3,safe));
        const auto pixelSafe=g.Draw();
        Assert::IsTrue(pixelSafe[0]<unknownBands[0]-30,
            (std::wstring(L"Proved empty bands did not permit mixing: ")+
                GpuFixture::PixelText(pixelSafe)).c_str());

        safe.viewportEpoch=5;
        Assert::IsTrue(g.temporal.AnnotatePresentation(3,safe));
        const auto staleEpoch=g.Draw();
        Assert::IsTrue(staleEpoch[0]>220,
            (std::wstring(L"Stale viewport neighbor mixed: ")+
                GpuFixture::PixelText(staleEpoch)).c_str());
    }    TEST_METHOD(IncompatibleGeometryNeighborDoesNotBlendIntoSelectedPicture){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        TemporalRenderer::PresentationGeometry geometry{};
        geometry.familyBase={0,0,64,64};geometry.sourceCrop={0,0,64,64};
        geometry.verifiedVisible={0,0,64,64};geometry.viewportEpoch=4;
        geometry.owner=1;geometry.valid=true;geometry.visibleSafe=true;geometry.cleaned=true;
        for(uint64_t sequence=1;sequence<=3;++sequence){
            g.UploadComposition(sequence==2,0);
            g.temporal.Begin(settings,24,60,false,0,1,sequence,
                double(sequence-1)/24,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(
                g.device->gpu,g.cleanInput,nullptr,sequence));
            auto packetGeometry=geometry;
            if(sequence==3)packetGeometry.familyBase={0,4,64,60};
            Assert::IsTrue(g.temporal.AnnotatePresentation(sequence,packetGeometry));
        }
        g.temporal.outputPts=1.5/24;
        Assert::AreEqual(uint64_t(2),g.temporal.SelectedPacketSequence());
        const auto isolated=g.Draw();
        Assert::IsTrue(isolated[0]>220,
            (std::wstring(L"Incompatible neighbor bled into selected picture: ")+
                GpuFixture::PixelText(isolated)).c_str());
        Assert::IsTrue(g.temporal.AnnotatePresentation(3,geometry));
        const auto compatible=g.Draw();
        Assert::IsTrue(compatible[0]<isolated[0]-30,
            (std::wstring(L"Compatible neighbor did not mix: ")+
                GpuFixture::PixelText(compatible)).c_str());
    }    TEST_METHOD(SourceSequenceGapClearsSelectedPacketBeforeNewCapture){
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        g.Upload(0);g.temporal.Begin(settings,24,60,false,0,1,100,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,100));
        g.Upload(128);g.temporal.Begin(settings,24,60,false,0,1,101,1./24,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,101));
        Assert::AreEqual(size_t(2),g.temporal.HistoryDepth());
        g.temporal.outputPts=1./24;
        Assert::AreEqual(uint64_t(101),g.temporal.SelectedPacketSequence());

        // A dropped capture packet resets temporal history before the next
        // output can select any crop or cue from the prior source generation.
        g.temporal.Begin(settings,24,60,false,0,1,116,2./24,false);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        Assert::AreEqual(uint64_t(0),g.temporal.SelectedPacketSequence());
        g.Upload(255);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,116));
        g.temporal.outputPts=2./24;
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        Assert::AreEqual(uint64_t(116),g.temporal.SelectedPacketSequence());
    }    TEST_METHOD(DiscontinuityClearsHistoryInsteadOfMixingOldSource){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        g.Upload(0);g.temporal.Begin(settings,24,60,false,0,1,1,0,false);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));
        g.Upload(255);g.temporal.Begin(settings,24,60,false,0,2,2,1./24,true);Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,2));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());g.temporal.outputPts=1./24;g.Draw();
    }
    TEST_METHOD(ChromaDefaultAndExplicitScalerReachRenderParameters){
        LibplaceboRenderParameters::Settings settings;LibplaceboRenderParameters::Projection projection;std::string error;
        Assert::IsTrue(LibplaceboRenderParameters::Build(settings,false,projection,error));
        Assert::IsTrue(projection.renderParams.plane_upscaler==&pl_filter_bicubic);
        settings.chromaUpscaler="bilinear";Assert::IsTrue(LibplaceboRenderParameters::Build(settings,false,projection,error));
        Assert::IsTrue(projection.renderParams.plane_upscaler==&pl_filter_bilinear);
    }
};

namespace VideoProcessorTest {
TEST_CLASS(SourceUploadBatchTests) {
public:
    TEST_METHOD(RepeatReusesOnlyPlanesAndPreservesPerOutputMetadata) {
        GpuFixture g;g.Upload(97);
        ActivePictureFrameIdentity id{};id.transportGeneration=2;id.acceptedSequence=10;id.analysisDomainKey=7;
        SourceUploadBatch batch;batch.Store(id,g.device->gpu,64,64,g.input);
        pl_frame out=g.input;out.crop={1,2,60,61};out.color=pl_color_space_hdr10;out.num_planes=0;
        const pl_tex resources[]={g.source,nullptr};
        Assert::IsTrue(batch.Restore(true,id,g.device->gpu,64,64,resources,1,out));
        Assert::AreEqual(1,out.num_planes);Assert::IsTrue(out.planes[0].texture==g.source);
        Assert::AreEqual(2.0f,out.crop.y0);Assert::IsTrue(out.color.transfer==pl_color_space_hdr10.transfer);
        Assert::IsFalse(batch.Restore(false,id,g.device->gpu,64,64,resources,1,out));
    }
    TEST_METHOD(ChangedSourceDomainGeometryDeviceOrResourceCannotReuse) {
        GpuFixture g;ActivePictureFrameIdentity id{};id.transportGeneration=2;id.acceptedSequence=10;id.analysisDomainKey=7;
        SourceUploadBatch batch;batch.Store(id,g.device->gpu,64,64,g.input);
        auto out=g.input;const pl_tex resources[]={g.source,nullptr};
        auto changed=id;changed.acceptedSequence++;
        Assert::IsFalse(batch.Restore(true,changed,g.device->gpu,64,64,resources,1,out));
        changed=id;changed.analysisDomainKey++;
        Assert::IsFalse(batch.Restore(true,changed,g.device->gpu,64,64,resources,1,out));
        changed=id;changed.viewportGeneration++;
        Assert::IsFalse(batch.Restore(true,changed,g.device->gpu,64,64,resources,1,out));
        Assert::IsFalse(batch.Restore(true,id,nullptr,64,64,resources,1,out));
        Assert::IsFalse(batch.Restore(true,id,g.device->gpu,32,64,resources,1,out));
        Assert::IsFalse(batch.Restore(true,id,g.device->gpu,64,64,resources,2,out));
        const pl_tex replaced[]={g.target,nullptr};
        Assert::IsFalse(batch.Restore(true,id,g.device->gpu,64,64,replaced,1,out));
    }
    TEST_METHOD(FailedUploadAndNewBatchCannotReuseEarlierSource) {
        GpuFixture g;ActivePictureFrameIdentity id{};const pl_tex resources[]={g.source,nullptr};auto out=g.input;
        SourceUploadBatch batch;
        Assert::IsFalse(batch.Restore(true,id,g.device->gpu,64,64,resources,1,out));
        batch.Store(id,g.device->gpu,64,64,g.input);pl_frame incomplete{};incomplete.num_planes=2;
        incomplete.planes[0]=g.input.planes[0];batch.Store(id,g.device->gpu,64,64,incomplete);
        Assert::IsFalse(batch.Restore(true,id,g.device->gpu,64,64,resources,1,out));
    }
    TEST_METHOD(ReuseRendersSameRgbPixelsAcrossOutputs) {
        GpuFixture g;g.Upload(97);ActivePictureFrameIdentity id{};
        SourceUploadBatch batch;batch.Store(id,g.device->gpu,64,64,g.input);
        const pl_tex resources[]={g.source,nullptr};
        for(int i=0;i<3;++i) {
            auto out=g.input;out.num_planes=0;
            Assert::IsTrue(batch.Restore(true,id,g.device->gpu,64,64,resources,1,out));
            Assert::IsTrue(pl_render_image(g.renderer,&out,&g.output,&pl_render_default_params));
            std::vector<unsigned char> pixels(64*64*4);pl_tex_transfer_params read{};read.tex=g.target;read.ptr=pixels.data();
            Assert::IsTrue(pl_tex_download(g.device->gpu,&read));
            Assert::IsTrue(pixels[0]>=95&&pixels[0]<=99);
        }
    }
    TEST_METHOD(TwoPlaneReusePreservesChromaLayout) {
        GpuFixture g;g.CreateP010AlternatingChroma();ActivePictureFrameIdentity id{};
        SourceUploadBatch batch;batch.Store(id,g.device->gpu,64,64,g.p010Input);
        const pl_tex resources[]={g.luma,g.chroma};pl_frame out{};
        Assert::IsTrue(batch.Restore(true,id,g.device->gpu,64,64,resources,2,out));
        Assert::AreEqual(2,out.num_planes);Assert::IsTrue(out.planes[1].texture==g.chroma);
        Assert::AreEqual(2,out.planes[1].components);
        Assert::AreEqual(2,out.planes[1].component_mapping[1]);
    }
};
}
namespace VideoProcessorTest {
TEST_CLASS(TemporalCleanPacketGpuTests) { public:
    TEST_METHOD(UncleanSameCropNeighborCannotLeakIntoCleanSelectedPicture) {
        GpuFixture g;MotionConfig::Settings settings;
        settings.smoothMotion="always";settings.mixer="linear";settings.deinterlace="off";
        TemporalRenderer::PresentationGeometry geometry{};
        geometry.familyBase={0,0,64,64};geometry.sourceCrop={0,0,64,64};
        geometry.verifiedVisible={0,0,64,64};geometry.viewportEpoch=4;
        geometry.owner=1;geometry.valid=true;geometry.visibleSafe=true;geometry.cleaned=true;
        for(uint64_t sequence=1;sequence<=3;++sequence) {
            g.UploadComposition(sequence==2,0);
            g.temporal.Begin(settings,24,60,false,0,1,sequence,double(sequence-1)/24,false,true);
            Assert::IsTrue(g.temporal.CaptureComposition(g.device->gpu,g.cleanInput,nullptr,sequence));
            auto packet=geometry;if(sequence==3)packet.cleaned=false;
            Assert::IsTrue(g.temporal.AnnotatePresentation(sequence,packet));
        }
        g.temporal.outputPts=1.5/24;
        const auto isolated=g.Draw();
        Assert::IsTrue(isolated[0]>220,L"Unclean caption packet mixed into repaired current picture.");
        Assert::IsTrue(g.temporal.AnnotatePresentation(3,geometry));
        const auto mixed=g.Draw();
        Assert::IsTrue(mixed[0]<isolated[0]-30,L"Compatible cleaned picture should retain smooth motion.");
    }
};
}
