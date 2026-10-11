// Real D3D11 original/custom/original regression for the VP master fork.
// Saves both outputs and requires bit-exact restoration with reused shader state.
#include <libplacebo/d3d11.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/sampling.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/gamut_mapping.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x); return 1; } } while(0)
#define W 128
#define H 73
static float input[W*H*4], output[W*H*4], original[W*H*4], custom[W*H*4];
int main(void) {
    clock_t started=clock();
    fprintf(stderr,"libplacebo %s\n",pl_version());
    pl_d3d11 d3d=pl_d3d11_create(NULL,NULL); CHECK(d3d);
    pl_gpu gpu=d3d->gpu;
    pl_fmt fmt=pl_find_fmt(gpu,PL_FMT_FLOAT,4,32,32,PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_SAMPLEABLE); CHECK(fmt);
    pl_dispatch dp=pl_dispatch_create(NULL,gpu); CHECK(dp);
    pl_shader_obj state=NULL;
    pl_tex dst=pl_tex_create(gpu,pl_tex_params(.w=W,.h=H,.format=fmt,.renderable=true,.host_readable=true)); CHECK(dst);
    FILE *bin=fopen("frames.f32","wb"), *csv=fopen("frames.csv","w"), *mat=fopen("matrices.csv","w"); CHECK(bin&&csv&&mat);
    FILE *raw=fopen("frames-original.f32","wb");CHECK(raw);
    fprintf(csv,"frame,scenario,measured,target_nits,dynamic_target,film_strength,tone,scene_avg,max_y_nits\n");
    const enum pl_color_primaries prims[]={PL_COLOR_PRIM_BT_2020,PL_COLOR_PRIM_BT_709};
    for(int p=0;p<2;p++) {
        pl_matrix3x3 a=pl_ipt_rgb2lms(pl_raw_primaries_get(prims[p]));
        pl_matrix3x3 b=pl_get_rgb2xyz_matrix(pl_raw_primaries_get(prims[p]));
        fprintf(mat,"%d",p); for(int i=0;i<3;i++)for(int j=0;j<3;j++)fprintf(mat,",%.10g",a.m[i][j]);
        for(int i=0;i<3;i++)fprintf(mat,",%.10g",b.m[1][i]); fprintf(mat,"\n");
    }
    for(int i=0;i<3;i++){for(int j=0;j<3;j++)fprintf(mat,"%s%.10g",j?",":"",pl_ipt_lms2ipt.m[i][j]);fprintf(mat,"\n");} fclose(mat);
    const struct pl_tone_map_function *tones[]={&pl_tone_map_spline,&pl_tone_map_bt2390,&pl_tone_map_st2094_40,&pl_tone_map_reinhard};
    float strengths[]={0,.3f,1,2}, targets[]={100,203,500}; int frame=0;
    for(int scenario=0;scenario<3;scenario++) {
        const struct pl_raw_primaries *source_prim=pl_raw_primaries_get(scenario==1?PL_COLOR_PRIM_DISPLAY_P3:PL_COLOR_PRIM_BT_2020);
        pl_matrix3x3 convert=pl_get_color_mapping_matrix(source_prim,pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020),PL_INTENT_RELATIVE_COLORIMETRIC);
        double avg=0; float maxima[3]={0};
        for(int y=0;y<H;y++) {
            float h=6.f*(y%24)/24,f=h-floorf(h),rgb[3]={0};
            switch((int)h){case 0:rgb[0]=1;rgb[1]=f;break;case 1:rgb[0]=1-f;rgb[1]=1;break;case 2:rgb[1]=1;rgb[2]=f;break;case 3:rgb[1]=1-f;rgb[2]=1;break;case 4:rgb[0]=f;rgb[2]=1;break;default:rgb[0]=1;rgb[2]=1-f;break;}
            float sat=y==72?0:(y/24==0?1:(y/24==1?.5f:.05f));
            for(int c=0;c<3;c++)rgb[c]=1+sat*(rgb[c]-1);
            float coded[3]; for(int i=0;i<3;i++){coded[i]=0;for(int j=0;j<3;j++)coded[i]+=convert.m[i][j]*rgb[j];}
            for(int x=0;x<W;x++) {
                float nits=4000*powf((float)x/(W-1),8), m=0;
                for(int c=0;c<3;c++){float v=coded[c]*nits; input[(y*W+x)*4+c]=v/PL_COLOR_SDR_WHITE; m=fmaxf(m,v);maxima[c]=fmaxf(maxima[c],v);}
                input[(y*W+x)*4+3]=1;avg+=m;
            }
        } avg/=W*H;
        pl_tex src=pl_tex_create(gpu,pl_tex_params(.w=W,.h=H,.format=fmt,.sampleable=true,.initial_data=input)); CHECK(src);
        struct pl_color_space source=pl_color_space_hdr10;
        source.transfer=PL_COLOR_TRC_LINEAR; source.hdr.min_luma=0;source.hdr.max_luma=4000;
        source.hdr.prim=*source_prim;
        for(int measured=0;measured<(scenario==0?2:1);measured++)
        for(int t=0;t<(measured?1:3);t++) for(int dynamic=0;dynamic<2;dynamic++) for(int curve=0;curve<4;curve++) for(int f=0;f<4;f++) {
            pl_reset_detected_peak(state); struct pl_color_space s=source;
            s.hdr.scene_avg=measured?0:avg; for(int c=0;c<3;c++)s.hdr.scene_max[c]=measured?0:maxima[c];
            float max_y=0;
            if(measured) {
                pl_shader peak=pl_dispatch_begin(dp); CHECK(pl_shader_sample_nearest(peak,pl_sample_src(.tex=src)));
                struct pl_peak_detect_params p=pl_peak_detect_default_params; p.allow_delayed=false;
                CHECK(pl_shader_detect_peak(peak,s,&state,&p));
                CHECK(pl_dispatch_compute(dp,pl_dispatch_compute_params(.shader=&peak,.width=W,.height=H)));
                struct pl_hdr_metadata detected={0};CHECK(pl_get_detected_hdr_metadata(state,&detected));
                max_y=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,detected.max_pq_y);
            }
            struct pl_color_space target=s;target.primaries=scenario==2?PL_COLOR_PRIM_BT_709:PL_COLOR_PRIM_BT_2020;
            target.hdr=(struct pl_hdr_metadata){.max_luma=targets[t],.prim=*pl_raw_primaries_get(target.primaries)};
            struct pl_color_map_params map=pl_color_map_default_params;
            map.dynamic_target=dynamic;map.film_strength=strengths[f];map.tone_mapping_function=tones[curve];map.mastering_clip=measured?1:0;
            for(int pass=0;pass<3;pass++) {
            map.vp_gamut_compression=pass==1;
            pl_shader sh=pl_dispatch_begin(dp);CHECK(pl_shader_sample_nearest(sh,pl_sample_src(.tex=src)));
            pl_shader_color_map(sh,&map,s,target,&state,false);
            CHECK(pl_dispatch_finish(dp,pl_dispatch_params(.shader=&sh,.target=dst)));
            CHECK(pl_tex_download(gpu,pl_tex_transfer_params(.tex=dst,.ptr=output)));
            if(pass==0)memcpy(original,output,sizeof(output));
            if(pass==1)memcpy(custom,output,sizeof(output));
            if(pass==2)CHECK(memcmp(original,output,sizeof(output))==0);
            }
            CHECK(fwrite(custom,sizeof(float),W*H*4,bin)==W*H*4);
            CHECK(fwrite(original,sizeof(float),W*H*4,raw)==W*H*4);
            fprintf(csv,"%d,%d,%d,%.0f,%d,%.1f,%s,%.6f,%.6f\n",frame++,scenario,measured,targets[t],dynamic,strengths[f],tones[curve]->name,avg,max_y);

        }
        pl_tex_destroy(gpu,&src);fprintf(stderr,"scenario %d complete; %d frames\n",scenario,frame);
    }
    pl_shader_obj_destroy(&state);fclose(raw);fclose(bin);fclose(csv);pl_tex_destroy(gpu,&dst);pl_dispatch_destroy(&dp);pl_d3d11_destroy(&d3d);
    fprintf(stderr,"complete: %d frames of %dx%d float RGBA\n",frame,W,H);fprintf(stderr,"elapsed CPU clock %.3f seconds\n",(double)(clock()-started)/CLOCKS_PER_SEC);return 0;
}
