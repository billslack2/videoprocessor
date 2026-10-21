// Exercise source-rate rendering against either bundled libplacebo API.
// Build with that runtime's include directory and import library.
#include <vprenderer/TemporalRenderer.h>
#include <libplacebo/d3d11.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    std::atomic<unsigned> errors{0};
    pl_log log=nullptr; pl_d3d11 device=nullptr; pl_renderer renderer=nullptr;
    pl_tex source=nullptr,target=nullptr; pl_frame input{},output{};
    TemporalRenderer temporal;
    static void Log(void* priv, pl_log_level level, const char* message) {
        if (level<=PL_LOG_ERR) {
            ++static_cast<Fixture*>(priv)->errors;
            std::cerr << message << '\n';
        }
    }
    Fixture() {
        pl_log_params logging{}; logging.log_cb=Log; logging.log_priv=this;
        logging.log_level=PL_LOG_ERR; log=pl_log_create(PL_API_VER,&logging);
        pl_d3d11_params d{}; d.allow_software=true;
        device=pl_d3d11_create(log,&d); Require(device!=nullptr,"D3D11 unavailable");
        renderer=pl_renderer_create(log,device->gpu);
        pl_tex_params p{}; p.w=p.h=64; p.format=pl_find_named_fmt(device->gpu,"rgba8");
        p.sampleable=p.renderable=p.blit_src=p.blit_dst=p.host_readable=p.host_writable=true;
        source=pl_tex_create(device->gpu,&p); target=pl_tex_create(device->gpu,&p);
        Require(source&&target,"Texture allocation failed");
        input.num_planes=1; input.planes[0].texture=source; input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb; input.repr.levels=PL_COLOR_LEVELS_FULL;
        input.repr.alpha=PL_ALPHA_NONE; input.color=pl_color_space_srgb; input.crop={0,0,64,64};
        output=input; output.planes[0].texture=target; output.crop={0,8,64,56};
    }
    ~Fixture() {
        temporal.Clear();
        if(device) {
            pl_tex_destroy(device->gpu,&source); pl_tex_destroy(device->gpu,&target);
            pl_renderer_destroy(&renderer); pl_d3d11_destroy(&device);
        }
        pl_log_destroy(&log);
    }
    void Run(const char* filter, bool composition) {
        MotionConfig::Settings settings; settings.smoothMotion="always";
        settings.deinterlace="off"; settings.mixer=filter;
        const float gray[]={.5f,.5f,.5f,1}, poison[]={1,0,1,1};
        for(unsigned i=1;i<=3;++i) {
            temporal.Begin(settings,24,75,false,0,1,i,double(i-1)/24,false,composition);
            pl_tex_clear(device->gpu,source,gray);
            const bool captured=composition
                ? temporal.CaptureComposition(device->gpu,input,nullptr,i,{})
                : temporal.Capture(device->gpu,input,i);
            Require(captured,"Capture failed");
        }
        auto params=pl_render_fast_params;
        // Actual operator path letterboxes a picture into a larger output.
        // A non-full crop forces the mixer to clear the intermediate border.
        for(int i=0;i<3;++i) {
            temporal.outputPts=1./24+i/750.;
            pl_tex_clear(device->gpu,target,poison);
            Require(temporal.Render(renderer,input,output,params),"Render failed");
            std::vector<unsigned char> pixels(64*64*4);
            pl_tex_transfer_params transfer{}; transfer.tex=target; transfer.ptr=pixels.data();
            Require(pl_tex_download(device->gpu,&transfer),"Readback failed");
            Require(errors.load()==0,"GPU validation errors during letterboxed source-rate blending");
            Require(pixels[(32*64+32)*4]>100,"Picture missing");
            for(int c=0;c<3;++c) Require(pixels[(4*64+32)*4+c]<=1,"Letterbox border not cleared");
        }
        const auto stats=temporal.ProcessingSnapshot();
        Require(stats.sourceReuses>0&&stats.outputRenders==3,"Source-rate cache not reused");
    }
};
int main() {
    try {
        for(bool composition:{false,true}) for(const char* filter:{"oversample","linear","mitchell_clamp"}) {
            Fixture fixture; fixture.Run(filter,composition);
            std::cout << "PASS API " << PL_API_VER << ' ' << filter << " composition=" << composition << '\n';
        }
        return 0;
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
