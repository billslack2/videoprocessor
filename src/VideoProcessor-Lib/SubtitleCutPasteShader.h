#pragma once
#include <SubtitleCutPaste.h>
#include <libplacebo/shaders/custom.h>
#include <cstring>
#include <libplacebo/colorspace.h>

// MAIN is libplacebo's combined RGB stage, before scaling and output color
// management. These experimental modes submit the complete source raster.
// The caller owns this parsed hook and destroys it with pl_mpv_user_shader_destroy.
inline const pl_hook* CreateSubtitleCutPasteHook(pl_gpu gpu)
{
    static const char shader[]=R"GLSL(
//!PARAM enabled
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0
//!PARAM source_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM source_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM source_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM source_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM destination_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM destination_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM destination_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM destination_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM background_mode
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 5.0
0.0
//!PARAM transfer_mode
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 3.0
1.0
//!PARAM transfer_gamma
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 2.8
2.4
//!PARAM picture_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM picture_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM content_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM content_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM content_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM content_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!HOOK MAIN
//!BIND HOOKED
//!DESC VideoProcessor subtitle move preview with selectable backing
// SDR linear values use reference white=1; PQ uses 203 nits=1.
// HLG uses scene-linear values with nominal reference white at signal 0.75.
float subtitleDecode(float v) {
    v=max(v,0.0);
    if(transfer_mode<0.5) return pow(v,transfer_gamma);
    if(transfer_mode<1.5) return v<=0.04045 ? v/12.92 : pow((v+0.055)/1.055,2.4);
    if(transfer_mode<2.5) {
        float q=pow(v,1.0/78.84375);
        return pow(max(q-0.8359375,0.0)/max(18.8515625-18.6875*q,0.000001),1.0/0.1593017578125)*(10000.0/203.0);
    }
    if(transfer_mode<3.5) return (v<=0.5 ? v*v/3.0 : (exp((v-0.55991073)/0.17883277)+0.28466892)/12.0)/0.26496256;
    return v;
}
float subtitleEncode(float v) {
    v=max(v,0.0);
    if(transfer_mode<0.5) return pow(v,1.0/transfer_gamma);
    if(transfer_mode<1.5) return v<=0.0031308 ? v*12.92 : 1.055*pow(v,1.0/2.4)-0.055;
    if(transfer_mode<2.5) {
        float q=pow(v*(203.0/10000.0),0.1593017578125);
        return pow((0.8359375+18.8515625*q)/(1.0+18.6875*q),78.84375);
    }
    if(transfer_mode<3.5) {
        v*=0.26496256;
        return v<=1.0/12.0 ? sqrt(3.0*v) : 0.17883277*log(12.0*v-0.28466892)+0.55991073;
    }
    return v;
}
vec3 subtitleLinear(vec3 v) {return vec3(subtitleDecode(v.r),subtitleDecode(v.g),subtitleDecode(v.b));}
vec3 subtitleSignal(vec3 v) {return vec3(subtitleEncode(v.r),subtitleEncode(v.g),subtitleEncode(v.b));}
// Approximate current-frame background from clean strips beside the padded
// source. This smooth fill cannot reconstruct hidden detail. Never reuse it.
vec3 subtitleSourceFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom) return vec3(0.0);
    float left=max(0.5,source_left-1.5);
    float right=min(HOOKED_size.x-0.5,source_right+1.5);
    // If padding reaches both edges, no clean side sample exists.
    if(source_left<1.0 && source_right>HOOKED_size.x-1.0) return vec3(0.0);
    vec3 a=subtitleLinear(HOOKED_tex(vec2(left,p.y)*HOOKED_pt).rgb);
    vec3 b=subtitleLinear(HOOKED_tex(vec2(right,p.y)*HOOKED_pt).rgb);
    if(source_left<1.0) a=b;
    if(source_right>HOOKED_size.x-1.0) b=a;
    return mix(a,b,clamp((p.x-source_left)/max(source_right-source_left,1.0),0.0,1.0));
}
float subtitleGlyphAlpha(vec3 rgb) {
    float lo=min(rgb.r,min(rgb.g,rgb.b));
    float hi=max(rgb.r,max(rgb.g,rgb.b));
    // The MAIN hook can carry PQ-encoded values: ordinary subtitle white is
    // around 0.58, so a threshold near 0.8 erases real glyph coverage. Keep
    // the key deliberately limited to bright, near-neutral pixels; this is a
    // preview matte, not a semantic subtitle mask.
    float nearNeutral=1.0-smoothstep(0.10,0.28,hi-lo);
    return smoothstep(0.15,0.50,lo)*nearNeutral;
}
// Continue the clean picture immediately above the detected text into the
// vacated source region. Reflection keeps the first row continuous. A side
// feather meets the known pixels at both edges of the missing region. All
// samples come from this frame and the geometry is fixed for a held cue.
vec3 subtitleGeneratedFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom) return vec3(0.0);
    float sampleY=clamp(2.0*content_top-p.y-1.0,
        picture_top+0.5,max(picture_top+0.5,content_top-0.5));
    vec3 reflected=vec3(0.0);
    reflected+=subtitleLinear(HOOKED_tex(vec2(clamp(p.x-2.0,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb)*0.12;
    reflected+=subtitleLinear(HOOKED_tex(vec2(clamp(p.x-1.0,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb)*0.22;
    reflected+=subtitleLinear(HOOKED_tex(vec2(clamp(p.x,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb)*0.32;
    reflected+=subtitleLinear(HOOKED_tex(vec2(clamp(p.x+1.0,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb)*0.22;
    reflected+=subtitleLinear(HOOKED_tex(vec2(clamp(p.x+2.0,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb)*0.12;
    float left=max(0.5,content_left-1.5);
    float right=min(HOOKED_size.x-0.5,content_right+1.5);
    vec3 a=subtitleLinear(HOOKED_tex(vec2(left,p.y)*HOOKED_pt).rgb);
    vec3 b=subtitleLinear(HOOKED_tex(vec2(right,p.y)*HOOKED_pt).rgb);
    vec3 side=mix(a,b,clamp((p.x-content_left)/max(content_right-content_left,1.0),0.0,1.0));
    float edge=min(p.x-content_left,content_right-p.x);
    return mix(side,reflected,smoothstep(0.0,24.0,edge)*step(picture_top+1.0,content_top));
}
float subtitleRoundedPanelMask(vec2 p) {
    vec2 halfSize=vec2(destination_right-destination_left,
        destination_bottom-destination_top)*0.5;
    float radius=min(28.0,min(halfSize.x,halfSize.y)*0.18);
    vec2 center=vec2(destination_left+destination_right,
        destination_top+destination_bottom)*0.5;
    vec2 q=abs(p-center)-(halfSize-vec2(radius));
    float distance=length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-radius;
    return 1.0-smoothstep(-0.75,0.75,distance);
}
vec4 hook() {
    vec2 p=HOOKED_pos*HOOKED_size;
    if(enabled>0.5) {
        if(p.x>=destination_left && p.x<destination_right &&
           p.y>=destination_top && p.y<destination_bottom) {
            vec2 sourcePos=HOOKED_pos+vec2(source_left-destination_left,
                source_top-destination_top)*HOOKED_pt;
            vec4 source=HOOKED_tex(sourcePos);
            if(background_mode<0.5) return source;
            vec3 original=subtitleLinear(HOOKED_tex(HOOKED_pos).rgb);
            vec3 backing=original;
            // Use the same smooth current-frame cleanup in overlap and vacated
            // source pixels, avoiding a black seam through the moved line.
            if(p.x>=content_left && p.x<content_right &&
               p.y>=content_top && p.y<content_bottom)
                backing=background_mode>4.5 ? subtitleGeneratedFill(p) : subtitleSourceFill(p);
            if(background_mode>1.5 && background_mode<2.5)
                backing*=0.50;
            else if(background_mode>2.5 && background_mode<3.5)
                backing=vec3(0.0);
            else if(background_mode>3.5)
                backing=background_mode>4.5 ? mix(backing,vec3(0.08),0.45) : vec3(0.08);
            vec2 sourcePixel=sourcePos*HOOKED_size;
            float alpha=(sourcePixel.x>=content_left && sourcePixel.x<content_right &&
                sourcePixel.y>=content_top && sourcePixel.y<content_bottom) ? subtitleGlyphAlpha(source.rgb) : 0.0;
            // Generated blend/solid panels follow a rounded rectangle. The
            // antialiased edge reveals the live destination pixels beneath it.
            float panelMask=background_mode>1.5 ? subtitleRoundedPanelMask(p) : 1.0;
            vec3 canvas=mix(original,backing,panelMask);
            return vec4(subtitleSignal(mix(canvas,subtitleLinear(source.rgb),alpha)),1.0);
        }
        if(p.x>=source_left && p.x<source_right && p.y>=source_top && p.y<source_bottom) {
            if(background_mode>0.5 && !(p.x>=content_left && p.x<content_right &&
                p.y>=content_top && p.y<content_bottom)) return HOOKED_tex(HOOKED_pos);
            if(background_mode>0.5 && (background_mode<2.5 || background_mode>4.5))
                return vec4(subtitleSignal(background_mode>4.5 ?
                    subtitleGeneratedFill(p) : subtitleSourceFill(p)),1.0);
            return vec4(0.0,0.0,0.0,1.0);
        }
    }
    return HOOKED_tex(HOOKED_pos);
}
)GLSL";
    return pl_mpv_user_shader_parse(gpu,shader,sizeof(shader)-1);
}

inline bool BindSubtitleCutPasteHook(const pl_hook* hook,
    const SubtitleCutPasteGeometry& geometry, int backgroundMode=0, pl_color_transfer transfer=PL_COLOR_TRC_SRGB)
{
    if (!hook || backgroundMode<0 || backgroundMode>5) return false;
    const char* names[]={"enabled","source_left","source_top","source_right","source_bottom",
        "destination_left","destination_top","destination_right","destination_bottom","background_mode",
        "transfer_mode","picture_top","picture_bottom","transfer_gamma",
        "content_left","content_top","content_right","content_bottom"};
    const float transferMode=transfer==PL_COLOR_TRC_PQ?2.0f:transfer==PL_COLOR_TRC_HLG?3.0f:
        transfer==PL_COLOR_TRC_SRGB?1.0f:0.0f;
    const float gamma=transfer==PL_COLOR_TRC_LINEAR?1.0f:
        transfer==PL_COLOR_TRC_GAMMA18?1.8f:transfer==PL_COLOR_TRC_GAMMA20?2.0f:
        transfer==PL_COLOR_TRC_GAMMA22?2.2f:transfer==PL_COLOR_TRC_GAMMA26?2.6f:
        transfer==PL_COLOR_TRC_GAMMA28?2.8f:2.4f;
    const auto& s=geometry.source;const auto& d=geometry.destination;
    const float values[]={geometry.valid?1.0f:0.0f,float(s.left),float(s.top),float(s.right),float(s.bottom),
        float(d.left),float(d.top),float(d.right),float(d.bottom),float(backgroundMode),transferMode,
        float(geometry.pictureTop),float(geometry.pictureBottom),gamma,
        float(geometry.content.left),float(geometry.content.top),float(geometry.content.right),float(geometry.content.bottom)};
    unsigned found=0;
    for(int i=0;i<hook->num_parameters;++i) {
        const auto& parameter=hook->parameters[i];
        if(!parameter.name || !parameter.data || parameter.type!=PL_VAR_FLOAT ||
            parameter.mode!=PL_HOOK_PAR_DYNAMIC) continue;
        for(unsigned n=0;n<18;++n) if(std::strcmp(parameter.name,names[n])==0) {
            parameter.data->f=values[n];found|=1u<<n;break;
        }
    }
    return found==262143;
}
