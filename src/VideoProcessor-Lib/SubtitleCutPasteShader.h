#pragma once
#include <SubtitleCutPaste.h>
#include <SubtitleGeneratedGrayStyle.h>
#include <libplacebo/shaders/custom.h>
#include <cstring>
#include <string>
#include <libplacebo/renderer.h>
#include <libplacebo/colorspace.h>

struct SubtitleGaussianRegion {
    float left=0,top=0,width=1,height=1,step=1;
    bool active=false;
};
inline SubtitleGaussianRegion ComputeSubtitleGaussianRegion(const SubtitleCutPasteGeometry& g,
    int mode,float radius) {
    SubtitleGaussianRegion r;
    if(!g.valid || mode!=5 || !std::isfinite(radius) || radius<=0)return r;
    radius=(std::min)(radius,30.0f);r.active=true;r.step=radius>=4?2.0f:1.0f;
    float left=float(g.destination.left)-6,right=float(g.destination.right)+6;
    float top=float(g.extendToBar && g.fromTopBar ? (std::min)(g.destination.top,g.pictureTop) : g.destination.top);
    float bottom=float(g.extendToBar && !g.fromTopBar ? (std::max)(g.destination.bottom,g.pictureBottom) : g.destination.bottom);
    if(g.generatedCleanup.Valid()) {
        left=(std::min)(left,float(g.generatedCleanup.left));right=(std::max)(right,float(g.generatedCleanup.right));
        top=(std::min)(top,float(g.generatedCleanup.top));bottom=(std::max)(bottom,float(g.generatedCleanup.bottom));
    }
    const float halo=std::ceil(radius)+2;
    r.left=std::floor((left-halo)/r.step)*r.step;r.top=std::floor((top-halo)/r.step)*r.step;
    r.width=std::ceil((right+halo-r.left)/r.step);r.height=std::ceil((bottom+halo-r.top)/r.step);
    return r;
}

// MAIN is libplacebo's combined RGB stage, before scaling and output color
// management. Relocation reads the complete raster before restoring the viewport crop.
// The caller owns this parsed hook and destroys it with pl_mpv_user_shader_destroy.
inline const pl_hook* CreateSubtitleCutPasteHook(pl_gpu gpu)
{
    static const char parameters[]=R"GLSL(
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
//!PARAM panel_flags
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 7.0
3.0
//!PARAM glyph_scale
//!TYPE DYNAMIC float
//!MINIMUM 0.25
//!MAXIMUM 1.0
1.0
//!PARAM glyph_translate_x
//!TYPE DYNAMIC float
//!MINIMUM -65536.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_translate_y
//!TYPE DYNAMIC float
//!MINIMUM -65536.0
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
//!PARAM generated_clean_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM generated_clean_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM generated_clean_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM generated_clean_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM generated_color_r
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.001214108
//!PARAM generated_color_g
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.001214108
//!PARAM generated_color_b
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.001214108
//!PARAM generated_opacity
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.85
//!PARAM generated_blur_px
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 30.0
3.0
//!PARAM generated_max_luminance
//!TYPE DYNAMIC float
//!MINIMUM 0.01
//!MAXIMUM 1.0
0.16
//!PARAM generated_border_r
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0
//!PARAM generated_border_g
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0
//!PARAM generated_border_b
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0
//!PARAM generated_border_opacity
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.65
//!PARAM generated_border_width
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 8.0
0.0
//!PARAM picture_capture_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM picture_capture_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM picture_capture_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM picture_capture_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_0_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_0_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_0_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_0_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_1_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_1_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_1_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_1_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_2_left
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_2_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_2_right
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM glyph_2_bottom
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM blur_active
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.0
//!PARAM blur_roi_left
//!TYPE DYNAMIC float
//!MINIMUM -65536.0
//!MAXIMUM 65536.0
0.0
//!PARAM blur_roi_top
//!TYPE DYNAMIC float
//!MINIMUM -65536.0
//!MAXIMUM 65536.0
0.0
//!PARAM blur_roi_w
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 65536.0
1.0
//!PARAM blur_roi_h
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 65536.0
1.0
//!PARAM blur_step
//!TYPE DYNAMIC float
//!MINIMUM 1.0
//!MAXIMUM 2.0
1.0
)GLSL";
    static const char common[]=R"GLSL(
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
// Continue nearby picture detail into the old subtitle card. The reflected
// upper patch can invert strongly directional scenery, but preserves much more
// texture than a full-width left/right color ramp. Feather its outer 24 pixels
// to current-frame side samples so the old card edge does not show.
bool subtitleInGeneratedCleanup(vec2 p) {
    return p.x>=generated_clean_left && p.x<generated_clean_right &&
        p.y>=generated_clean_top && p.y<generated_clean_bottom;
}
vec3 subtitleGeneratedRawFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom) return vec3(0.0);
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    float sampleY=fromTop
        ? clamp(2.0*generated_clean_bottom-p.y, min(picture_bottom-0.5,generated_clean_bottom+0.5),picture_bottom-0.5)
        : clamp(2.0*generated_clean_top-p.y-1.0,
            picture_top+0.5,max(picture_top+0.5,generated_clean_top-0.5));
    vec3 reflected=subtitleLinear(HOOKED_tex(vec2(clamp(p.x,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb);
    float left=max(0.5,generated_clean_left-1.5);
    float right=min(HOOKED_size.x-0.5,generated_clean_right+1.5);
    vec3 a=subtitleLinear(HOOKED_tex(vec2(left,p.y)*HOOKED_pt).rgb);
    vec3 b=subtitleLinear(HOOKED_tex(vec2(right,p.y)*HOOKED_pt).rgb);
    vec3 side=mix(a,b,clamp((p.x-generated_clean_left)/max(generated_clean_right-generated_clean_left,1.0),0.0,1.0));
    float edge=min(p.x-generated_clean_left,generated_clean_right-p.x);
    return mix(side,reflected,smoothstep(0.0,24.0,edge)*(fromTop ? step(generated_clean_bottom+1.0,picture_bottom) : step(picture_top+1.0,generated_clean_top)));
}
// Reconstruct first, then filter the combined real/generated scene. Testing
// the footprint as well as the center prevents bilinear samples just outside
// a card edge from pulling its black backing or glyphs into the overlay.
vec3 subtitleCleanBackdropSample(vec2 p) {
    p=clamp(p,vec2(0.5,picture_top+0.5),vec2(HOOKED_size.x-0.5,picture_bottom-0.5));
    bool touchesCard=generated_clean_right>generated_clean_left && generated_clean_bottom>generated_clean_top &&
        p.x>generated_clean_left-0.5 && p.x<generated_clean_right+0.5 &&
        p.y>generated_clean_top-0.5 && p.y<generated_clean_bottom+0.5;
    if(touchesCard)return subtitleGeneratedRawFill(p);
    return subtitleLinear(HOOKED_tex(p*HOOKED_pt).rgb);
}
vec3 subtitleCappedBacking(vec3 backing) {
    float luminance=dot(backing,vec3(0.2126,0.7152,0.0722));
    // Preserve local scene contrast through a smooth highlight shoulder rather
    // than mapping every bright source pixel to one identical luminance.
    float knee=generated_max_luminance*0.65;
    float shoulder=generated_max_luminance-knee;
    float excess=max(luminance-knee,0.0);
    float compressed=knee+shoulder*excess/(excess+shoulder);
    float mapped=luminance<=knee ? luminance : min(luminance,compressed);
    return backing*(mapped/max(luminance,0.000001));
}
// The independently measured source card is the removal footprint. Pixels may be
// gray, translucent, antialiased, or as dark as their surroundings; brightness
// cannot decide whether part of an already accepted subtitle card survives.
float subtitleGeneratedCleanupMask(vec2 p, vec3 actual, vec3 fill) {
    return subtitleInGeneratedCleanup(p) ? 1.0 : 0.0;
}
// Display-only extension; glyph coordinates and measured cleanup stay unchanged.
float subtitlePanelSideExtension() {
    return (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5)) ? 6.0 : 0.0;
}
float subtitlePanelTop() {
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5 && (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5));
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    return attached && fromTop ? min(destination_top,picture_top) : destination_top;
}
float subtitlePanelBottom() {
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5 && (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5));
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    return attached && !fromTop ? max(destination_bottom,picture_bottom) : destination_bottom;
}
float subtitleRoundedPanelDistance(vec2 p) {
    float radius=mod(panel_flags,2.0)>0.5 ? min(28.0,min(destination_right-destination_left,
        destination_bottom-destination_top)*0.18) : 0.0;
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5;
    // Square the bar-facing corners; round only the picture-facing corners.
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    vec2 lo=vec2(destination_left-subtitlePanelSideExtension(),subtitlePanelTop()-(attached && fromTop ? 2.0*radius : 0.0));
    vec2 hi=vec2(destination_right+subtitlePanelSideExtension(),subtitlePanelBottom()+(attached && !fromTop ? 2.0*radius : 0.0));
    vec2 halfSize=(hi-lo)*0.5;
    vec2 center=(lo+hi)*0.5;
    vec2 q=abs(p-center)-(halfSize-vec2(radius));
    return length(max(q,vec2(0.0)))+min(max(q.x,q.y),0.0)-radius;
}
float subtitleRoundedPanelMask(vec2 p) {
    return 1.0-smoothstep(-0.75,0.75,subtitleRoundedPanelDistance(p));
}
bool subtitleOwnedLine(vec2 p) {
    bool hasLines=glyph_0_right>glyph_0_left || glyph_1_right>glyph_1_left || glyph_2_right>glyph_2_left;
    return !hasLines ||
        (p.x>=glyph_0_left && p.x<glyph_0_right && p.y>=glyph_0_top && p.y<glyph_0_bottom) ||
        (p.x>=glyph_1_left && p.x<glyph_1_right && p.y>=glyph_1_top && p.y<glyph_1_bottom) ||
        (p.x>=glyph_2_left && p.x<glyph_2_right && p.y>=glyph_2_top && p.y<glyph_2_bottom);
}
// AR and subtitle measurement can disagree by up to three rows. Neither
// padding nor cleanup grants glyph authority there. Preserve only a bright
// stroke connected to either side of that tiny ownership gap; a detached
// picture stripe must still be rejected. Runs only for disputed picture pixels.
bool subtitleConnectedBoundaryStroke(vec2 p) {
    if(p.x<picture_capture_left || p.x>=picture_capture_right ||
       picture_capture_right<=picture_capture_left)return false;
    float a=0.0,b=0.0;
    if(picture_bottom>picture_capture_bottom && picture_bottom-picture_capture_bottom<=3.0 &&
       p.y>=picture_capture_bottom && p.y<picture_bottom) {
        a=picture_capture_bottom-0.5;b=picture_bottom+0.5;
    } else if(picture_capture_top>picture_top && picture_capture_top-picture_top<=3.0 &&
       p.y>=picture_top && p.y<picture_capture_top) {
        a=picture_top-0.5;b=picture_capture_top+0.5;
    } else return false;
    float above=0.0,below=0.0;
    for(int dx=-1;dx<=1;++dx) {
        vec2 pa=vec2(p.x+float(dx),a),pb=vec2(p.x+float(dx),b);
        if(subtitleOwnedLine(pa))above=max(above,subtitleGlyphAlpha(HOOKED_tex(pa*HOOKED_pt).rgb));
        if(subtitleOwnedLine(pb))below=max(below,subtitleGlyphAlpha(HOOKED_tex(pb*HOOKED_pt).rgb));
    }
    return above>0.01 || below>0.01;
}
// Key native samples BEFORE filtering. Averaging white glyphs with black and
// then thresholding would erase punctuation and thin strokes at quarter size.
// At most five native cells overlap each axis of the 1..4-pixel footprint.
vec4 subtitleReducedGlyph(vec2 center) {
    float footprint=1.0/glyph_scale,halfFootprint=footprint*0.5;
    vec2 lo=center-vec2(halfFootprint),hi=center+vec2(halfFootprint);
    if(hi.x<=content_left || lo.x>=content_right || hi.y<=content_top || lo.y>=content_bottom)
        return vec4(0.0);
    bool hasLines=glyph_0_right>glyph_0_left || glyph_1_right>glyph_1_left || glyph_2_right>glyph_2_left;
    if(hasLines &&
        !(hi.x>glyph_0_left && lo.x<glyph_0_right && hi.y>glyph_0_top && lo.y<glyph_0_bottom) &&
        !(hi.x>glyph_1_left && lo.x<glyph_1_right && hi.y>glyph_1_top && lo.y<glyph_1_bottom) &&
        !(hi.x>glyph_2_left && lo.x<glyph_2_right && hi.y>glyph_2_top && lo.y<glyph_2_bottom))return vec4(0.0);
    vec2 first=floor(lo);
    vec3 premultiplied=vec3(0.0);float coverage=0.0;
    for(int row=0;row<5;++row)for(int column=0;column<5;++column) {
        vec2 cell=first+vec2(float(column),float(row));
        vec2 overlap=max(vec2(0.0),min(hi,cell+vec2(1.0))-max(lo,cell));
        float weight=overlap.x*overlap.y;
        if(weight<=0.0)continue;
        vec2 p=cell+vec2(0.5);
        if(p.x<0.0 || p.y<0.0 || p.x>=HOOKED_size.x || p.y>=HOOKED_size.y ||
            p.x<content_left || p.x>=content_right || p.y<content_top || p.y>=content_bottom ||
            !subtitleOwnedLine(p))continue;
        bool inPicture=p.y>=picture_top && p.y<picture_bottom;
        bool onCard=p.x>=picture_capture_left && p.x<picture_capture_right &&
            p.y>=picture_capture_top && p.y<picture_capture_bottom;
        vec3 rgb=HOOKED_tex(p*HOOKED_pt).rgb;
        float alpha=subtitleGlyphAlpha(rgb);
        if(inPicture && !onCard && !(alpha>0.01 && subtitleConnectedBoundaryStroke(p)))continue;
        premultiplied+=subtitleLinear(rgb)*(alpha*weight);
        coverage+=alpha*weight;
    }
    return vec4(premultiplied/max(coverage,0.000001),coverage/(footprint*footprint));
}
)GLSL";
    static const char composition[]=R"GLSL(
vec4 hook() {
    vec2 p=HOOKED_pos*HOOKED_size;
    if(enabled>0.5) {
        bool inPanel=p.x>=destination_left-subtitlePanelSideExtension() && p.x<destination_right+subtitlePanelSideExtension() &&
            p.y>=subtitlePanelTop() && p.y<subtitlePanelBottom();
        bool inCleanup=subtitleInGeneratedCleanup(p);
        // One evaluation and one compiled call site, shared by composition
        // and source cleanup. The default also clears original bar glyphs.
        vec3 generatedBackdrop=vec3(0.0);
        if(background_mode>4.5 && (inPanel || inCleanup))
            generatedBackdrop=subtitleGeneratedFill(p);
        if(inPanel) {
            vec2 sourcePos=HOOKED_pos+vec2(source_left-destination_left,
                source_top-destination_top)*HOOKED_pt;
            if(glyph_scale<0.9999 || panel_flags>=4.0)
                sourcePos=((p-vec2(glyph_translate_x,glyph_translate_y))/glyph_scale)*HOOKED_pt;
            bool filterGlyph=glyph_scale<0.9999 || (panel_flags>=4.0 &&
                (abs(glyph_translate_x-floor(glyph_translate_x+0.5))>0.0001 ||
                 abs(glyph_translate_y-floor(glyph_translate_y+0.5))>0.0001));
            vec4 source=vec4(0.0);
            if(!filterGlyph || background_mode<0.5)source=HOOKED_tex(sourcePos);
            if(background_mode<0.5) return source;
            vec3 original=subtitleLinear(HOOKED_tex(HOOKED_pos).rgb);
            if(background_mode>4.5 && subtitleInGeneratedCleanup(p)) {
                original=generatedBackdrop;
            }
            // Source removal precedes rounded destination composition. Its
            // antialiased corners must not reveal an old glyph underneath.
            if(background_mode>2.5 && background_mode<3.5 && subtitleInGeneratedCleanup(p))
                original=vec3(0.0);
            vec3 backing=original;
            // Use the same smooth current-frame cleanup in overlap and vacated
            // source pixels, avoiding a black seam through the moved line.
            if(p.x>=content_left && p.x<content_right &&
               p.y>=content_top && p.y<content_bottom &&
               background_mode<=4.5)
                backing=subtitleSourceFill(p);
            // Cleanup pixels already have the filtered reconstructed backdrop.
            // Elsewhere filter real picture too, using the same clean sampler
            // across the boundary. The final panel mask confines the blur.
            if(background_mode>4.5)
                backing=generatedBackdrop;
            if(background_mode>1.5 && background_mode<2.5)
                backing*=0.50;
            else if(background_mode>2.5 && background_mode<3.5)
                backing=vec3(generated_color_r,generated_color_g,generated_color_b);
            else if(background_mode>4.5)
                backing=subtitleCappedBacking(mix(backing,
                    vec3(generated_color_r,generated_color_g,generated_color_b),generated_opacity));
            else if(background_mode>3.5)
                backing=vec3(0.08);
            vec2 sourcePixel=sourcePos*HOOKED_size;
            // The glyph envelope includes sampling fringes, which can contain
            // bright scenery. Picture pixels need measured card ownership;
            // letterbox glyphs remain eligible without a visible card edge.
            float alpha=0.0;vec3 glyphColor=vec3(0.0);
            if(filterGlyph) {
                vec4 filtered=subtitleReducedGlyph(sourcePixel);
                glyphColor=filtered.rgb;alpha=filtered.a;
            } else {
                bool inPicture=sourcePixel.y>=picture_top && sourcePixel.y<picture_bottom;
                bool onSourceCard=sourcePixel.x>=picture_capture_left && sourcePixel.x<picture_capture_right &&
                    sourcePixel.y>=picture_capture_top && sourcePixel.y<picture_capture_bottom;
                alpha=(p.y>=destination_top && p.y<destination_bottom && sourcePixel.x>=content_left && sourcePixel.x<content_right &&
                    sourcePixel.y>=content_top && sourcePixel.y<content_bottom &&
                    (!inPicture || onSourceCard || (subtitleGlyphAlpha(source.rgb)>0.01 && subtitleConnectedBoundaryStroke(sourcePixel))) && subtitleOwnedLine(sourcePixel)) ? subtitleGlyphAlpha(source.rgb) : 0.0;
                glyphColor=subtitleLinear(source.rgb);
            }
            // Generated blend/solid panels follow a rounded rectangle. The
            // antialiased edge reveals the live destination pixels beneath it.
            float panelMask=background_mode>1.5 ? subtitleRoundedPanelMask(p) : 1.0;
            vec3 canvas=mix(original,backing,panelMask);
            if((background_mode>4.5 || (background_mode>2.5 && background_mode<3.5)) && generated_border_width>0.0) {
                float inner=1.0-smoothstep(-0.75,0.75,
                    subtitleRoundedPanelDistance(p)+generated_border_width);
                canvas=mix(canvas,vec3(generated_border_r,generated_border_g,generated_border_b),
                    max(panelMask-inner,0.0)*generated_border_opacity);
            }
            return vec4(subtitleSignal(mix(canvas,glyphColor,alpha)),1.0);
        }
        // The original card can extend beyond display padding and beyond the
        // copied glyph rectangle. Remove it before testing the sampling extent.
        if(background_mode>4.5 && subtitleInGeneratedCleanup(p)) {
            return vec4(subtitleSignal(generatedBackdrop),1.0);
        }
        if(background_mode>2.5 && background_mode<3.5 && subtitleInGeneratedCleanup(p))
            return vec4(0.0,0.0,0.0,1.0);
        if(p.x>=source_left && p.x<source_right && p.y>=source_top && p.y<source_bottom) {
            // Measured-card modes already handled every authorized picture
            // cleanup above. Glyph-envelope fringe must keep its live picture.
            if(p.y>=picture_top && p.y<picture_bottom &&
                (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5)))
                return HOOKED_tex(HOOKED_pos);
            if(background_mode>0.5 && !(p.x>=content_left && p.x<content_right &&
                p.y>=content_top && p.y<content_bottom)) return HOOKED_tex(HOOKED_pos);
            if(background_mode>0.5 && (background_mode<2.5 || background_mode>4.5))
                return vec4(subtitleSignal(background_mode>4.5 ?
                    generatedBackdrop : subtitleSourceFill(p)),1.0);
            return vec4(0.0,0.0,0.0,1.0);
        }
    }
    return HOOKED_tex(HOOKED_pos);
}
)GLSL";
    // Saved textures cover only the subtitle ROI plus its Gaussian halo.
    // Skipped entirely for zero blur, disabled relocation and other modes.
    const std::string prepare=R"GLSL(
//!HOOK MAIN
//!BIND HOOKED
//!SAVE VP_SUB_CLEAN
//!WIDTH blur_roi_w
//!HEIGHT blur_roi_h
//!WHEN blur_active
//!COMPONENTS 3
//!DESC Subtitle clean background ROI
)GLSL"+std::string(common)+R"GLSL(
vec4 hook() {
    vec2 p=vec2(blur_roi_left,blur_roi_top)+HOOKED_pos*vec2(blur_roi_w,blur_roi_h)*blur_step;
    if(blur_step<1.5)return vec4(subtitleCleanBackdropSample(p),1.0);
    // Exact 2x2 area reduction, not four widely spaced blur taps.
    vec3 sum=subtitleCleanBackdropSample(p+vec2(-0.5,-0.5))+
        subtitleCleanBackdropSample(p+vec2(0.5,-0.5))+
        subtitleCleanBackdropSample(p+vec2(-0.5,0.5))+
        subtitleCleanBackdropSample(p+vec2(0.5,0.5));
    return vec4(sum*0.25,1.0);
}
)GLSL";
    const auto gaussian=[](const char* input,const char* output,const char* axis) {
        std::string pass="\n//!HOOK MAIN\n//!BIND HOOKED\n//!BIND "+std::string(input)+
            "\n//!SAVE "+output+"\n//!WIDTH blur_roi_w\n//!HEIGHT blur_roi_h\n//!WHEN blur_active\n//!COMPONENTS 3\n//!DESC Subtitle Gaussian "+axis+"\n";
        pass+="vec4 hook() {\n vec2 p="+std::string(input)+"_pos; vec2 axis=vec2("+axis+");\n";
        pass+=" float radius=generated_blur_px/blur_step; int extent=int(ceil(radius)); float sigma=max(radius/3.0,0.333333);\n";
        pass+=" vec3 sum="+std::string(input)+"_tex(p).rgb; float total=1.0;\n";
        pass+=" for(int i=1;i<=extent;i+=2) { float x=float(i); float w1=exp(-0.5*x*x/(sigma*sigma)); float w2=i+1<=extent?exp(-0.5*(x+1.0)*(x+1.0)/(sigma*sigma)):0.0; float w=w1+w2; float offset=x+w2/w;\n";
        pass+=" vec2 delta=axis*offset*"+std::string(input)+"_pt; sum+=("+input+"_tex(p-delta).rgb+"+input+"_tex(p+delta).rgb)*w; total+=2.0*w; } return vec4(sum/total,1.0); }\n";
        return pass;
    };
    const std::string blurred=R"GLSL(
//!HOOK MAIN
//!BIND HOOKED
//!BIND VP_SUB_GAUSS
//!WHEN blur_active
//!DESC Subtitle composition with smooth Gaussian background
)GLSL"+std::string(common)+R"GLSL(
vec3 subtitleGeneratedFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom)return vec3(0.0);
    return VP_SUB_GAUSS_tex((p-vec2(blur_roi_left,blur_roi_top))/(vec2(blur_roi_w,blur_roi_h)*blur_step)).rgb;
}
)GLSL"+composition;
    const std::string unblurred=R"GLSL(
//!HOOK MAIN
//!BIND HOOKED
//!WHEN blur_active !
//!DESC Subtitle composition without background blur
)GLSL"+std::string(common)+R"GLSL(
vec3 subtitleGeneratedFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom)return vec3(0.0);
    return subtitleCleanBackdropSample(p);
}
)GLSL"+composition;
    const std::string shader=std::string(parameters)+prepare+
        gaussian("VP_SUB_CLEAN","VP_SUB_H","1.0,0.0")+
        gaussian("VP_SUB_H","VP_SUB_GAUSS","0.0,1.0")+blurred+unblurred;
    return pl_mpv_user_shader_parse(gpu,shader.data(),shader.size());
}

inline bool BindSubtitleCutPasteHook(const pl_hook* hook,
    const SubtitleCutPasteGeometry& geometry, int backgroundMode,
    pl_color_transfer transfer,
    const SubtitleGeneratedGrayStyle& generated)
{
    if (!hook || backgroundMode<0 || backgroundMode>5) return false;
    const char* names[]={"enabled","source_left","source_top","source_right","source_bottom",
        "destination_left","destination_top","destination_right","destination_bottom","background_mode",
        "glyph_scale","glyph_translate_x","glyph_translate_y","panel_flags",
        "transfer_mode","picture_top","picture_bottom","transfer_gamma",
        "content_left","content_top","content_right","content_bottom",
        "generated_clean_left","generated_clean_top","generated_clean_right","generated_clean_bottom",
        "generated_color_r","generated_color_g","generated_color_b","generated_opacity",
        "generated_max_luminance","generated_blur_px","blur_active","blur_roi_left","blur_roi_top","blur_roi_w","blur_roi_h","blur_step",
        "generated_border_r","generated_border_g","generated_border_b",
        "generated_border_opacity","generated_border_width",
        "picture_capture_left","picture_capture_top","picture_capture_right","picture_capture_bottom","glyph_0_left","glyph_0_top","glyph_0_right","glyph_0_bottom","glyph_1_left","glyph_1_top","glyph_1_right","glyph_1_bottom","glyph_2_left","glyph_2_top","glyph_2_right","glyph_2_bottom"};
    const float transferMode=transfer==PL_COLOR_TRC_PQ?2.0f:transfer==PL_COLOR_TRC_HLG?3.0f:
        transfer==PL_COLOR_TRC_SRGB?1.0f:0.0f;
    const float gamma=transfer==PL_COLOR_TRC_LINEAR?1.0f:
        transfer==PL_COLOR_TRC_GAMMA18?1.8f:transfer==PL_COLOR_TRC_GAMMA20?2.0f:
        transfer==PL_COLOR_TRC_GAMMA22?2.2f:transfer==PL_COLOR_TRC_GAMMA26?2.6f:
        transfer==PL_COLOR_TRC_GAMMA28?2.8f:2.4f;
    const auto& s=geometry.source;const auto& d=geometry.destination;
    const auto roi=ComputeSubtitleGaussianRegion(geometry,backgroundMode,generated.blurPixels);
    const float values[]={geometry.valid?1.0f:0.0f,float(s.left),float(s.top),float(s.right),float(s.bottom),
        float(d.left),float(d.top),float(d.right),float(d.bottom),float(backgroundMode),
        geometry.glyphScale,geometry.glyphTranslateX,geometry.glyphTranslateY,
        float((geometry.roundedCorners?1:0)+(geometry.extendToBar?2:0)+(geometry.centeredGlyphMapping?4:0)),transferMode,
        float(geometry.pictureTop),float(geometry.pictureBottom),gamma,
        float(geometry.content.left),float(geometry.content.top),float(geometry.content.right),float(geometry.content.bottom),
        float(geometry.generatedCleanup.left),float(geometry.generatedCleanup.top),
        float(geometry.generatedCleanup.right),float(geometry.generatedCleanup.bottom),
        generated.color[0],generated.color[1],generated.color[2],generated.opacity,
        generated.maxLuminance,generated.blurPixels,roi.active?1.0f:0.0f,roi.left,roi.top,roi.width,roi.height,roi.step,
        generated.borderColor[0],generated.borderColor[1],generated.borderColor[2],
        generated.borderOpacity,generated.borderWidth,
        float(geometry.pictureCapture.left),float(geometry.pictureCapture.top),
        float(geometry.pictureCapture.right),float(geometry.pictureCapture.bottom),float(geometry.glyphLines[0].left),float(geometry.glyphLines[0].top),float(geometry.glyphLines[0].right),float(geometry.glyphLines[0].bottom),float(geometry.glyphLines[1].left),float(geometry.glyphLines[1].top),float(geometry.glyphLines[1].right),float(geometry.glyphLines[1].bottom),float(geometry.glyphLines[2].left),float(geometry.glyphLines[2].top),float(geometry.glyphLines[2].right),float(geometry.glyphLines[2].bottom)};
    constexpr unsigned parameterCount=sizeof(names)/sizeof(names[0]);
    static_assert(parameterCount<64,"parameter presence mask overflow");
    static_assert(sizeof(values)/sizeof(values[0])==parameterCount,"subtitle parameter mismatch");
    uint64_t found=0;
    for(int i=0;i<hook->num_parameters;++i) {
        const auto& parameter=hook->parameters[i];
        if(!parameter.name || !parameter.data || parameter.type!=PL_VAR_FLOAT ||
            parameter.mode!=PL_HOOK_PAR_DYNAMIC) continue;
        for(unsigned n=0;n<parameterCount;++n) if(std::strcmp(parameter.name,names[n])==0) {
            parameter.data->f=values[n];found|=uint64_t{1}<<n;break;
        }
    }
    return found==((uint64_t{1}<<parameterCount)-1);
}

// Preserve the historical exact-black default for callers without explicit style.
inline bool BindSubtitleCutPasteHook(const pl_hook* hook,
    const SubtitleCutPasteGeometry& geometry, int backgroundMode=0,
    pl_color_transfer transfer=PL_COLOR_TRC_SRGB)
{
    SubtitleGeneratedGrayStyle style;
    if(backgroundMode==3) style.color={0.0f,0.0f,0.0f};
    return BindSubtitleCutPasteHook(hook,geometry,backgroundMode,transfer,style);
}

// Compose before the main renderer crops away letterbox pixels. Reuse a
// full-resolution floating-point surface and retain the input transfer function;
// output tone mapping, scaling, NLS and viewport placement occur only afterward.
inline bool ComposeSubtitleBeforeCrop(pl_renderer renderer,pl_gpu gpu,
    const pl_frame& source,const pl_hook* hook,pl_tex* surface,pl_frame& composed)
{
    if(!hook || source.num_planes<1 || !source.planes[0].texture)return false;
    const auto texture=source.planes[0].texture;
    pl_tex_params storage{};
    storage.w=texture->params.w;storage.h=texture->params.h;
    storage.format=pl_find_fmt(gpu,PL_FMT_FLOAT,4,16,0,
        static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE));
    storage.sampleable=true;storage.renderable=true;
    if(!storage.format || !pl_tex_recreate(gpu,surface,&storage))return false;
    auto input=source;input.crop={0,0,float(storage.w),float(storage.h)};
    input.rotation=PL_ROTATION_0;input.overlays=nullptr;input.num_overlays=0;
    pl_frame target{};target.num_planes=1;
    target.planes[0].texture=*surface;target.planes[0].components=4;
    for(int i=0;i<4;++i)target.planes[0].component_mapping[i]=i;
    target.repr.sys=PL_COLOR_SYSTEM_RGB;target.repr.levels=PL_COLOR_LEVELS_FULL;
    target.repr.alpha=PL_ALPHA_NONE;target.color=source.color;
    auto params=pl_render_fast_params;
    params.dither_params=nullptr;params.peak_detect_params=nullptr;
    params.hooks=&hook;params.num_hooks=1;
    if(!pl_render_image(renderer,&input,&target,&params))return false;
    const auto errors=pl_renderer_get_errors(renderer);
    for(int i=0;i<errors.num_disabled_hooks;++i)
        if(errors.disabled_hooks[i]==hook->signature)return false;
    composed=target;composed.crop=source.crop;composed.rotation=source.rotation;
    composed.overlays=source.overlays;composed.num_overlays=source.num_overlays;
    return true;
}
