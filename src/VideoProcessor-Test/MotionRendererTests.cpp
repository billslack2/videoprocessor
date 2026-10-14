#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/TemporalRenderer.h>
#include <vprenderer/LibplaceboRenderParameters.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/utils/upload.h>
#include <array>
#include <fstream>
#include "RendererProfileConfig.h"
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace {
struct GpuFixture {
    pl_d3d11 device=nullptr;pl_renderer renderer=nullptr;pl_tex source=nullptr,target=nullptr;
    pl_frame input{},output{};TemporalRenderer temporal;
    GpuFixture(){
        pl_d3d11_params d{};d.allow_software=true;device=pl_d3d11_create(nullptr,&d);
        Assert::IsTrue(device!=nullptr,L"D3D11 fixture unavailable");renderer=pl_renderer_create(nullptr,device->gpu);
        pl_tex_params params{};params.w=64;params.h=64;
        params.format=pl_find_named_fmt(device->gpu,"rgba8");
        Assert::IsTrue(params.format!=nullptr,L"RGBA8 missing");
        params.sampleable=true;params.renderable=true;params.blit_src=true;params.blit_dst=true;
        params.host_writable=true;params.host_readable=true;
        source=pl_tex_create(device->gpu,&params);target=pl_tex_create(device->gpu,&params);
        Assert::IsTrue(source&&target,L"Texture allocation failed");
        input.num_planes=1;input.planes[0].texture=source;input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb;input.color=pl_color_space_srgb;input.crop={0,0,64,64};
        output=input;output.planes[0].texture=target;
    }
    ~GpuFixture(){temporal.Clear();if(device){pl_tex_destroy(device->gpu,&source);pl_tex_destroy(device->gpu,&target);pl_renderer_destroy(&renderer);pl_d3d11_destroy(&device);}}
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
    TEST_METHOD(SubtitleCompositionPolicySchedulesOneCurrentOutputAcrossCues){
        MotionConfig::Settings settings;settings.smoothMotion="always";
        TemporalRenderer r;
        for(uint64_t i=1;i<=24;++i){
            const double pts=double(i)/24;
            r.Begin(settings,24,60,false,0,1,i,pts,false,true);
            Assert::IsFalse(r.Active());Assert::IsFalse(r.Mixing());
            Assert::AreEqual(size_t(1),r.outputs.size());
            Assert::AreEqual(pts,r.outputPts);Assert::AreEqual(pts,r.outputs.front());
            Assert::AreEqual(0.0,r.DelayMs());Assert::AreEqual(size_t(0),r.HistoryDepth());
        }
    }
    TEST_METHOD(SubtitleCompositionTransitionDropsHistoryAndRestoresMixing){
        GpuFixture g;MotionConfig::Settings settings;settings.smoothMotion="always";
        g.Upload(50);g.temporal.Begin(settings,24,60,false,0,1,1,0,false);
        Assert::IsTrue(g.temporal.Capture(g.device->gpu,g.input,1));
        Assert::AreEqual(size_t(1),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,1,2,1./24,false,true);
        Assert::IsFalse(g.temporal.Active());Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
        g.temporal.Begin(settings,24,60,false,0,1,3,2./24,false,false);
        Assert::IsTrue(g.temporal.Mixing());Assert::IsTrue(g.temporal.outputs.size()>1);
        Assert::AreEqual(size_t(0),g.temporal.HistoryDepth());
    }
    TEST_METHOD(SubtitleCompositionPolicyAlsoPreventsFieldCadenceSwitching){
        MotionConfig::Settings settings;TemporalRenderer r;
        r.Begin(settings,30,60,true,1,1,1,0,false,true);
        Assert::IsFalse(r.Active());Assert::AreEqual(size_t(1),r.outputs.size());
        r.Begin(settings,30,60,true,1,1,2,1./30,false,false);
        Assert::IsTrue(r.Active());Assert::AreEqual(size_t(2),r.outputs.size());
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
        for(const char* filter:{"oversample","hermite","linear","cubic"}){
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
    TEST_METHOD(DiscontinuityClearsHistoryInsteadOfMixingOldSource){
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
