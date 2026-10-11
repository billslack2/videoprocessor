// Diagnostic probe: post-tone BT.2020 channel bounds before integer clipping.
// Compile with MSYS2 mingw64 gcc against the VP master fork (see docs/libplacebo-beta.md).
#include <libplacebo/d3d11.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/sampling.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/gamut_mapping.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s\n", #x); return 1; } } while(0)
#define W 128
#define H 24
static float input[W*H*4], output[W*H*4];
int main(void) {
    pl_d3d11 d3d = pl_d3d11_create(NULL, NULL); CHECK(d3d);
    pl_gpu gpu=d3d->gpu;
    pl_fmt fmt=pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_SAMPLEABLE);
    CHECK(fmt);
    double avg=0;
    for (int y=0;y<H;y++) {
        float h=6.f*y/H, f=h-floorf(h), rgb[3]={0};
        switch ((int)h) {
        case 0:rgb[0]=1;rgb[1]=f;break;
        case 1:rgb[0]=1-f;rgb[1]=1;break;
        case 2:rgb[1]=1;rgb[2]=f;break;
        case 3:rgb[1]=1-f;rgb[2]=1;break;
        case 4:rgb[0]=f;rgb[2]=1;break;
        default:rgb[0]=1;rgb[2]=1-f;break;
        }
        for(int x=0;x<W;x++) {
            float nits=4000*powf((float)x/(W-1),8);
            avg+=nits;
            for(int c=0;c<3;c++) input[(y*W+x)*4+c]=rgb[c]*nits/PL_COLOR_SDR_WHITE;
            input[(y*W+x)*4+3]=1;
        }
    }
    avg/=W*H;
    pl_tex src=pl_tex_create(gpu, pl_tex_params(.w=W,.h=H,.format=fmt,.sampleable=true,.initial_data=input)); CHECK(src);
    pl_tex dst=pl_tex_create(gpu, pl_tex_params(.w=W,.h=H,.format=fmt,.renderable=true,.host_readable=true)); CHECK(dst);
    pl_dispatch dp=pl_dispatch_create(NULL,gpu); CHECK(dp);
    struct pl_color_space source=pl_color_space_hdr10;
    source.transfer=PL_COLOR_TRC_LINEAR; source.hdr.min_luma=0;
    source.hdr.max_luma=4000; source.hdr.scene_avg=avg;
    source.hdr.scene_max[0]=source.hdr.scene_max[1]=source.hdr.scene_max[2]=4000;
    source.hdr.prim=*pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020);
    printf("source_peak_nits=4000 scene_avg_maxRGB_nits=%.3f\n", avg);
    puts("target_nits,dynamic_target,film_strength,tone_mapping,gamut_mapping,min_channel_nits,max_channel_nits,outside_pixels,total_pixels");
    const struct pl_gamut_map_function *maps[]={&pl_gamut_map_perceptual,&pl_gamut_map_softclip,&pl_gamut_map_relative,&pl_gamut_map_desaturate};
    const struct pl_tone_map_function *tones[]={&pl_tone_map_spline,&pl_tone_map_bt2390,&pl_tone_map_st2094_40,&pl_tone_map_reinhard};
    const float strengths[]={0,.3f,1,2};
    const float targets[]={100,203,500};
    for(int t=0;t<3;t++) for(int dynamic=0;dynamic<2;dynamic++) for(int f=0;f<4;f++) for(int g=0;g<4;g++) for(int curve=0;curve<4;curve++) {
        pl_shader_obj state=NULL;
        struct pl_color_space target=source; target.hdr=(struct pl_hdr_metadata){.max_luma=targets[t],.prim=source.hdr.prim};
        struct pl_color_map_params map=pl_color_map_default_params;
        map.dynamic_target=dynamic;map.film_strength=strengths[f];map.gamut_mapping=maps[g];map.tone_mapping_function=tones[curve];
        pl_shader sh=pl_dispatch_begin(dp);
        CHECK(pl_shader_sample_nearest(sh,pl_sample_src(.tex=src)));
        pl_shader_color_map(sh,&map,source,target,&state,false);
        CHECK(pl_dispatch_finish(dp,pl_dispatch_params(.shader=&sh,.target=dst)));
        CHECK(pl_tex_download(gpu,pl_tex_transfer_params(.tex=dst,.ptr=output)));
        float low=1e10f,high=-1e10f;int outside=0;
        for(int i=0;i<W*H;i++) {
            int bad=0;
            for(int c=0;c<3;c++) {float n=output[i*4+c]*PL_COLOR_SDR_WHITE; CHECK(isfinite(n)); low=fminf(low,n);high=fmaxf(high,n);bad|=n<-.02f||n>targets[t]+.02f;}
            outside+=bad;
        }
        printf("%.0f,%d,%.1f,%s,%s,%.6f,%.6f,%d,%d\n",targets[t],dynamic,strengths[f],tones[curve]->name,maps[g]->name,low,high,outside,W*H);
        pl_shader_obj_destroy(&state);
    }
    pl_dispatch_destroy(&dp);pl_tex_destroy(gpu,&src);pl_tex_destroy(gpu,&dst);pl_d3d11_destroy(&d3d);
    return 0;
}
