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
    const bool selectedPicture=g.displayPictureRight>g.displayPictureLeft &&
        g.displayPictureBottom>g.displayPictureTop;
    const int displayTop=selectedPicture?g.displayPictureTop:g.pictureTop;
    const int displayBottom=selectedPicture?g.displayPictureBottom:g.pictureBottom;
    float left=float(g.destination.left)-6,right=float(g.destination.right)+6;
    float top=float(g.extendToBar && g.fromTopBar ? (std::min)(g.destination.top,displayTop) : g.destination.top);
    float bottom=float(g.extendToBar && !g.fromTopBar ? (std::max)(g.destination.bottom,displayBottom) : g.destination.bottom);
    if(g.generatedCleanup.Valid()) {
        left=(std::min)(left,float(g.generatedCleanup.left));right=(std::max)(right,float(g.generatedCleanup.right));
        top=(std::min)(top,float(g.generatedCleanup.top));bottom=(std::max)(bottom,float(g.generatedCleanup.bottom));
    }
    const float halo=std::ceil(radius)+2;
    r.left=std::floor((left-halo)/r.step)*r.step;r.top=std::floor((top-halo)/r.step)*r.step;
    r.width=std::ceil((right+halo-r.left)/r.step);r.height=std::ceil((bottom+halo-r.top)/r.step);
    return r;
}

inline pl_rect2d ComputeSubtitleLayerBounds(const SubtitleCutPasteGeometry& g,
    int width,int height)
{
    pl_rect2d bounds{};
    if(!g.valid || !g.destination.Valid() || width<=0 || height<=0)return bounds;
    const bool selectedPicture=g.displayPictureRight>g.displayPictureLeft &&
        g.displayPictureBottom>g.displayPictureTop;
    const int displayTop=selectedPicture?g.displayPictureTop:g.pictureTop;
    const int displayBottom=selectedPicture?g.displayPictureBottom:g.pictureBottom;
    // The shader extends six pixels horizontally and antialiases the panel.
    // Include two pixels at each vertical edge, including its selected bar edge.
    bounds.x0=(std::max)(0,g.destination.left-8);
    bounds.x1=(std::min)(width,g.destination.right+8);
    bounds.y0=(std::max)(0,(g.extendToBar && g.fromTopBar?
        (std::min)(g.destination.top,displayTop):g.destination.top)-2);
    bounds.y1=(std::min)(height,(g.extendToBar && !g.fromTopBar?
        (std::max)(g.destination.bottom,displayBottom):g.destination.bottom)+2);
    return bounds;
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
//!PARAM composition_phase
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 2.0
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
//!PARAM display_picture_top
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 65536.0
0.0
//!PARAM display_picture_bottom
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
//!PARAM cleanup_fill_mode
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 3.0
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
//!PARAM text_color_r
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0
//!PARAM text_color_g
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0
//!PARAM text_color_b
//!TYPE DYNAMIC float
//!MINIMUM 0.0
//!MAXIMUM 1.0
1.0
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
    static const char commonPrefix[]=R"GLSL(
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
// Replace only the old subtitle footprint inside the picture. Stretch samples
// an upright strip outside all measured card and glyph bounds; mirror remains
// the fallback. Preserve the black bar and feather the footprint's edges.
float subtitleSafeDonorEdge(bool fromTop) {
    float edge=fromTop ? generated_clean_bottom : generated_clean_top;
    edge=fromTop ? max(edge,source_bottom) : min(edge,source_top);
    edge=fromTop ? max(edge,content_bottom) : min(edge,content_top);

    if(glyph_0_right>glyph_0_left && glyph_0_bottom>glyph_0_top)
        edge=fromTop ? max(edge,glyph_0_bottom) : min(edge,glyph_0_top);
    if(glyph_1_right>glyph_1_left && glyph_1_bottom>glyph_1_top)
        edge=fromTop ? max(edge,glyph_1_bottom) : min(edge,glyph_1_top);
    if(glyph_2_right>glyph_2_left && glyph_2_bottom>glyph_2_top)
        edge=fromTop ? max(edge,glyph_2_bottom) : min(edge,glyph_2_top);
    return clamp(edge+(fromTop ? 2.0 : -2.0),picture_top,picture_bottom);
}
float subtitleSafeDonorLeft() {
    float edge=generated_clean_left;
    if(source_right>source_left && source_bottom>source_top) edge=min(edge,source_left);
    if(content_right>content_left && content_bottom>content_top) edge=min(edge,content_left);

    if(glyph_0_right>glyph_0_left && glyph_0_bottom>glyph_0_top) edge=min(edge,glyph_0_left);
    if(glyph_1_right>glyph_1_left && glyph_1_bottom>glyph_1_top) edge=min(edge,glyph_1_left);
    if(glyph_2_right>glyph_2_left && glyph_2_bottom>glyph_2_top) edge=min(edge,glyph_2_left);
    return edge;
}
float subtitleSafeDonorRight() {
    float edge=generated_clean_right;
    if(source_right>source_left && source_bottom>source_top) edge=max(edge,source_right);
    if(content_right>content_left && content_bottom>content_top) edge=max(edge,content_right);

    if(glyph_0_right>glyph_0_left && glyph_0_bottom>glyph_0_top) edge=max(edge,glyph_0_right);
    if(glyph_1_right>glyph_1_left && glyph_1_bottom>glyph_1_top) edge=max(edge,glyph_1_right);
    if(glyph_2_right>glyph_2_left && glyph_2_bottom>glyph_2_top) edge=max(edge,glyph_2_right);
    return edge;
}
)GLSL";
    static const char commonMiddle[]=R"GLSL(
bool subtitleStretchAvailable() {
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    float height=picture_bottom-picture_top;
    float safeEdge=subtitleSafeDonorEdge(fromTop);
    float clean=fromTop ? picture_bottom-safeEdge : safeEdge-picture_top;
    float edgeGap=fromTop ? generated_clean_top-picture_top : picture_bottom-generated_clean_bottom;
    float donorGap=fromTop ? safeEdge-generated_clean_bottom : generated_clean_top-safeEdge;
    float fillHeight=min(picture_bottom,generated_clean_bottom)-
        max(picture_top,generated_clean_top);
    return cleanup_fill_mode>0.5 && cleanup_fill_mode<1.5 && height>1.0 && fillHeight>2.0 &&
        clean>=0.5*height && edgeGap<=0.1*height &&
        donorGap<=max(64.0,2.0*fillHeight) &&
        generated_clean_right>generated_clean_left &&
        generated_clean_bottom>generated_clean_top;
}
vec3 subtitleStretchedPicture(vec2 p) {
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    float fillTop=max(picture_top,generated_clean_top);
    float fillBottom=min(picture_bottom,generated_clean_bottom);
    float fillHeight=max(fillBottom-fillTop,1.0);
    float safeEdge=subtitleSafeDonorEdge(fromTop);
    float clean=fromTop ? picture_bottom-safeEdge : safeEdge-picture_top;
    float donorHeight=min(clean,min(fillHeight,max(8.0,0.65*fillHeight)));
    float fraction=clamp((p.y-fillTop)/fillHeight,0.0,1.0);
    float edgeY=fromTop ? safeEdge+0.5 : safeEdge-0.5;
    float startY=fromTop ? edgeY : edgeY-donorHeight;
    float sampleY=clamp(startY+fraction*donorHeight,picture_top+0.5,picture_bottom-0.5);
    float x=clamp(p.x,0.5,HOOKED_size.x-0.5);
    vec3 stretched=subtitleLinear(HOOKED_tex(vec2(x,sampleY)*HOOKED_pt).rgb);
    vec3 edge=subtitleLinear(HOOKED_tex(vec2(x,edgeY)*HOOKED_pt).rgb);
    float fromEdge=fromTop ? fillBottom-p.y : p.y-fillTop;
    return mix(edge,stretched,smoothstep(0.0,min(20.0,0.25*fillHeight),fromEdge));
}
bool subtitleInGeneratedCleanup(vec2 p) {
    return p.x>=generated_clean_left && p.x<generated_clean_right &&
        p.y>=generated_clean_top && p.y<generated_clean_bottom;
}
// Bounded spatial reconstruction: at most two samples per usable edge.
// Both taps remain beyond ALL removal/glyph bounds. Blend in linear light;
// the outer edge gets most weight so it joins its neighbouring real picture.
// No frame history, search iterations, or geometry-specific shader source.
vec3 subtitleSurroundingPicture(vec2 p) {
    vec4 edges=vec4(subtitleSafeDonorLeft()-1.5,subtitleSafeDonorRight()+1.5,
        subtitleSafeDonorEdge(false)-0.5,subtitleSafeDonorEdge(true)+0.5);
    vec3 sum=vec3(0.0);float weights=0.0;
    // Uniform loop bound (always eight in this mode) keeps texture sampling
    // and transfer decoding at ONE compiled call site, not eight expanded ones.
    int count=4*int(clamp(cleanup_fill_mode,0.0,2.0));
    for(int tap=0;tap<count;tap++) {
        int side=tap/2;bool mirrored=(tap-2*side)!=0;
        vec2 edge=p,dir=vec2(0.0);float d=0.0,available=-1.0;
        if(side==0) {
            edge.x=edges.x;dir.x=-1.0;d=p.x-generated_clean_left;available=edges.x-0.5;
        } else if(side==1) {
            edge.x=edges.y;dir.x=1.0;d=generated_clean_right-p.x;available=HOOKED_size.x-0.5-edges.y;
        } else if(side==2) {
            edge.y=edges.z;dir.y=-1.0;d=p.y-generated_clean_top;available=edges.z-picture_top-0.5;
        } else {
            edge.y=edges.w;dir.y=1.0;d=generated_clean_bottom-p.y;available=picture_bottom-0.5-edges.w;
        }
        if(available<0.0)continue;
        edge=clamp(edge,vec2(0.5,picture_top+0.5),vec2(HOOKED_size.x-0.5,picture_bottom-0.5));
        d=max(0.0,d);float w=1.0/((d+2.0)*(d+2.0));
        w*=mirrored?0.35:0.65;
        vec2 donor=edge+dir*min(available,(mirrored?1.0:0.35)*d);
        sum+=w*subtitleLinear(HOOKED_tex(donor*HOOKED_pt).rgb);weights+=w;
    }
    return weights>0.0 ? sum/weights : vec3(0.0);
}
// Fixed, current-frame reconstruction. Donor positions and normalized weights
// depend only on geometry, never on image gradients or color comparisons. This
// keeps small frame changes from moving the sampling pattern and causing shimmer.
vec3 subtitleStableSurroundingPicture(vec2 p) {
    vec4 edges=vec4(subtitleSafeDonorLeft()-1.5,subtitleSafeDonorRight()+1.5,
        subtitleSafeDonorEdge(false)-0.5,subtitleSafeDonorEdge(true)+0.5);
    vec2 lo=vec2(0.5,picture_top+0.5),hi=vec2(HOOKED_size.x-0.5,picture_bottom-0.5);
    float seam=clamp(0.20*min(generated_clean_right-generated_clean_left,
        generated_clean_bottom-generated_clean_top),6.0,20.0);
    vec3 sum=vec3(0.0);float weights=0.0;
    int count=2*int(clamp(cleanup_fill_mode,0.0,2.0));
    for(int side=0;side<count;side++) {
        vec2 edge=p,dir=vec2(0.0);float d=0.0,available=-1.0;
        if(side==0) {
            edge.x=edges.x;dir.x=-1.0;d=p.x-generated_clean_left;available=edges.x-0.5;
        } else if(side==1) {
            edge.x=edges.y;dir.x=1.0;d=generated_clean_right-p.x;available=HOOKED_size.x-0.5-edges.y;
        } else if(side==2) {
            edge.y=edges.z;dir.y=-1.0;d=p.y-generated_clean_top;available=edges.z-picture_top-0.5;
        } else {
            edge.y=edges.w;dir.y=1.0;d=generated_clean_bottom-p.y;available=picture_bottom-0.5-edges.w;
        }
        if(available<0.0)continue;
        edge=clamp(edge,lo,hi);d=max(0.0,d);
        vec2 tangent=vec2(-dir.y,dir.x);
        float radius=min(2.0,0.25*d);
        vec3 boundary=vec3(0.0),filled=vec3(0.0);
        // One transfer-decoding site, five fixed taps per available edge.
        // Positive, normalized weights make this a non-amplifying linear
        // operator in scene light while the accepted geometry is unchanged.
        int taps=2*int(clamp(cleanup_fill_mode,0.0,2.0))+1;
        for(int tap=0;tap<taps;tap++) {
            vec2 donor=edge;
            if(tap==1)donor-=tangent*radius;
            else if(tap==2)donor+=tangent*radius;
            else if(tap>=3)donor+=dir*min(available,(tap==3?0.35:1.0)*d);
            vec3 color=subtitleLinear(HOOKED_tex(clamp(donor,lo,hi)*HOOKED_pt).rgb);
            if(tap<3)boundary+=(tap==0?0.5:0.25)*color;
            else filled+=(tap==3?0.65:0.35)*color;
        }
        // A slightly wider transition joins the clean boundary to the fixed
        // interior fill. Never blend the original subtitle/card pixels back in.
        filled=mix(boundary,filled,smoothstep(0.0,seam,d));
        float w=1.0/((d+0.5)*(d+0.5));
        sum+=w*filled;weights+=w;
    }
    return weights>0.0 ? sum/weights : vec3(0.0);
}
vec3 subtitleGeneratedRawFill(vec2 p) {
    if(p.y<picture_top || p.y>=picture_bottom) return vec3(0.0);
    if(cleanup_fill_mode>2.5) return subtitleStableSurroundingPicture(p);
    if(cleanup_fill_mode>1.5) return subtitleSurroundingPicture(p);
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    float safeEdge=subtitleSafeDonorEdge(fromTop);
    bool stretched=subtitleStretchAvailable();
    bool mirrorAvailable=fromTop ? safeEdge<picture_bottom-1.0 : safeEdge>picture_top+1.0;
    vec3 filled=vec3(0.0);
    if(stretched) filled=subtitleStretchedPicture(p);
    else if(mirrorAvailable) {
        float sampleY=fromTop
            ? clamp(2.0*safeEdge-p.y,safeEdge+0.5,picture_bottom-0.5)
            : clamp(2.0*safeEdge-p.y-1.0,picture_top+0.5,safeEdge-0.5);
        filled=subtitleLinear(HOOKED_tex(vec2(clamp(p.x,0.5,HOOKED_size.x-0.5),sampleY)*HOOKED_pt).rgb);
    }
    float donorLeft=subtitleSafeDonorLeft()-1.5;
    float donorRight=subtitleSafeDonorRight()+1.5;
    bool hasLeft=donorLeft>=0.5;
    bool hasRight=donorRight<=HOOKED_size.x-0.5;
    if(!hasLeft && !hasRight) return filled;
    vec3 a=filled,b=filled;
    if(hasLeft) a=subtitleLinear(HOOKED_tex(vec2(donorLeft,p.y)*HOOKED_pt).rgb);
    if(hasRight) b=subtitleLinear(HOOKED_tex(vec2(donorRight,p.y)*HOOKED_pt).rgb);
    if(!hasLeft) a=b;
    if(!hasRight) b=a;
    vec3 side=mix(a,b,clamp((p.x-generated_clean_left)/max(generated_clean_right-generated_clean_left,1.0),0.0,1.0));
    if(!stretched && !mirrorAvailable)return side;
    float edge=min(p.x-generated_clean_left,generated_clean_right-p.x);
    return mix(side,filled,smoothstep(0.0,24.0,edge)*(fromTop ? step(generated_clean_bottom+1.0,picture_bottom) : step(picture_top+1.0,generated_clean_top)));
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
)GLSL";
    static const char commonSuffix[]=R"GLSL(
float subtitlePanelSideExtension() {
    return (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5)) ? 6.0 : 0.0;
}
float subtitlePanelTop() {
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5 && (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5));
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    return attached && fromTop ? min(destination_top,display_picture_top) : destination_top;
}
float subtitlePanelBottom() {
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5 && (background_mode>4.5 || (background_mode>2.5 && background_mode<3.5));
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    return attached && !fromTop ? max(destination_bottom,display_picture_bottom) : destination_bottom;
}
float subtitleRoundedPanelDistance(vec2 p) {
    float radius=mod(panel_flags,2.0)>0.5 ? min(28.0,min(destination_right-destination_left,
        destination_bottom-destination_top)*0.18) : 0.0;
    bool attached=mod(floor(panel_flags/2.0),2.0)>0.5;
    // Square bar-facing corners and any picture-facing edge clipped by the
    // selected viewport. Keep inPanel bounded; this only makes the visible edge
    // opaque instead of exposing cleanup through a clipped rounded/AA corner.
    bool fromTop=source_top+source_bottom<picture_top+picture_bottom;
    bool clippedTop=!fromTop && destination_top<=display_picture_top;
    bool clippedBottom=fromTop && destination_bottom>=display_picture_bottom;
    float squareExtension=max(2.0*radius,2.0);
    vec2 lo=vec2(destination_left-subtitlePanelSideExtension(),subtitlePanelTop()-
        ((attached && fromTop) || clippedTop ? squareExtension : 0.0));
    vec2 hi=vec2(destination_right+subtitlePanelSideExtension(),subtitlePanelBottom()+
        ((attached && !fromTop) || clippedBottom ? squareExtension : 0.0));
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
    const std::string common=std::string(commonPrefix)+commonMiddle+commonSuffix;
    static const char composition[]=R"GLSL(
vec4 hook() {
    vec2 p=HOOKED_pos*HOOKED_size;
    if(enabled>0.5) {
        bool cleanupOnly=composition_phase>0.5 && composition_phase<1.5;
        bool layerOnly=composition_phase>1.5;
        // Reconstruction's neutral settings are the UI's transparent style.
        // Keep deliberately blurred or brightness-limited backdrops separate.
        bool transparentBackground=(background_mode>0.5 && background_mode<1.5) ||
            (background_mode>4.5 && generated_opacity<=0.0 && generated_blur_px<=0.0 &&
             generated_max_luminance>=1.0 &&
             (generated_border_width<=0.0 || generated_border_opacity<=0.0));
        bool inPanel=p.x>=destination_left-subtitlePanelSideExtension() && p.x<destination_right+subtitlePanelSideExtension() &&
            p.y>=subtitlePanelTop() && p.y<subtitlePanelBottom();
        bool inCleanup=subtitleInGeneratedCleanup(p);
        // One evaluation and one compiled call site, shared by composition
        // and source cleanup. The default also clears original bar glyphs.
        vec3 generatedBackdrop=vec3(0.0);
        // Black uses the unblurred pass. Its cleanup coordinate is inside the
        // mask, so the common clean sampler reaches the same raw fill. Share
        // that call site instead of compiling another complete spatial kernel.
        if((background_mode>4.5 && (inPanel || inCleanup)) ||
            (background_mode>2.5 && background_mode<3.5 && inCleanup && !layerOnly))
            generatedBackdrop=subtitleGeneratedFill(p);
        if(layerOnly && !inPanel)return vec4(0.0);
        if(inPanel && !cleanupOnly) {
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
            if(inCleanup && (background_mode>4.5 ||
                (background_mode>2.5 && background_mode<3.5 && !layerOnly)))
                original=generatedBackdrop;
            // Cleanup precedes rounded destination composition, so its
            // antialiased corners reveal only the repaired current picture.
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
            // Tint only the extracted glyph, after ownership/alpha and area filtering.
            // Identity white preserves source values; linear multiplication keeps
            // dimming relative to the original SDR/HDR text brightness.
            glyphColor*=vec3(text_color_r,text_color_g,text_color_b);
            // Generated blend/solid panels follow a rounded rectangle. The
            // antialiased edge reveals the live destination pixels beneath it.
            float panelMask=background_mode>1.5 ? subtitleRoundedPanelMask(p) : 1.0;
            vec3 canvas=mix(original,backing,panelMask);
            float borderAlpha=0.0;
            vec3 borderColor=vec3(generated_border_r,generated_border_g,generated_border_b);
            if((background_mode>4.5 || (background_mode>2.5 && background_mode<3.5)) && generated_border_width>0.0) {
                float inner=1.0-smoothstep(-0.75,0.75,
                    subtitleRoundedPanelDistance(p)+generated_border_width);
                borderAlpha=max(panelMask-inner,0.0)*generated_border_opacity;
                canvas=mix(canvas,borderColor,borderAlpha);
            }
            if(transparentBackground) {
                // Transparent output carries glyphs only. Copying the live
                // picture as an opaque tile lets overlay color management and
                // temporal blending expose a rectangular one-pixel seam.
                if(layerOnly)return vec4(alpha>0.0?subtitleSignal(glyphColor):vec3(0.0),alpha);
                // Avoid a transfer-function round trip on untouched picture.
                // Overlap with the old glyph footprint still needs its fill.
                if(alpha<=0.0 && !inCleanup && !(p.x>=content_left && p.x<content_right &&
                    p.y>=content_top && p.y<content_bottom))return HOOKED_tex(HOOKED_pos);
                if(background_mode>4.5)canvas=original;
            }
            if(layerOnly) {
                // Straight source-transfer RGB plus independent alpha. Compose
                // over the temporally processed picture in linear light.
                float opacity=1.0-(1.0-panelMask)*(1.0-borderAlpha)*(1.0-alpha);
                vec3 premultiplied=((backing*panelMask)*(1.0-borderAlpha)+
                    borderColor*borderAlpha)*(1.0-alpha)+glyphColor*alpha;
                return vec4(subtitleSignal(premultiplied/max(opacity,0.000001)),opacity);
            }
            return vec4(subtitleSignal(mix(canvas,glyphColor,alpha)),1.0);
        }
        if(layerOnly)return vec4(0.0);
        // The original card can extend beyond display padding and beyond the
        // copied glyph rectangle. Remove it before testing the sampling extent.
        if(background_mode>4.5 && subtitleInGeneratedCleanup(p)) {
            return vec4(subtitleSignal(generatedBackdrop),1.0);
        }
        if(background_mode>2.5 && background_mode<3.5 && inCleanup)
            return vec4(subtitleSignal(generatedBackdrop),1.0);
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
    return composition_phase>1.5 ? vec4(0.0) : HOOKED_tex(HOOKED_pos);
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
    vec3 sum=vec3(0.0);
    int count=2*int(clamp(blur_step,1.0,2.0)); // four when this branch runs
    for(int tap=0;tap<count;tap++) {
        vec2 delta=vec2(float(tap-2*(tap/2)),float(tap/2))-vec2(0.5);
        sum+=subtitleCleanBackdropSample(p+delta);
    }
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
//!COMPONENTS 4
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
//!COMPONENTS 4
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

// Source-plane options are snapshotted per packet. In particular, debanding
// must run on native YUV planes before normalization, not on the mixed RGB
// picture during the final viewport pass. Preset filter pointers are libplacebo
// static data; deband values are copied because renderer settings can change.
struct SubtitleSourceProcessingOptions {
    const pl_filter_config* planeUpscaler=nullptr;
    const pl_filter_config* planeDownscaler=nullptr;
    float antiringingStrength=0.0f;
    bool debandEnabled=false;
    pl_deband_params debandParams{};
};

inline SubtitleSourceProcessingOptions CaptureSubtitleSourceProcessingOptions(
    const pl_render_params& settings)
{
    SubtitleSourceProcessingOptions options;
    options.planeUpscaler=settings.plane_upscaler;
    options.planeDownscaler=settings.plane_downscaler;
    options.antiringingStrength=settings.antiringing_strength;
    options.debandEnabled=settings.deband_params!=nullptr;
    if(options.debandEnabled)options.debandParams=*settings.deband_params;
    return options;
}

enum class SubtitleCompositionPhase : int {
    All = 0,
    CleanupOnly = 1,
    LayerOnly = 2,
};

inline bool BindSubtitleCutPasteHook(const pl_hook* hook,
    const SubtitleCutPasteGeometry& geometry, int backgroundMode,
    pl_color_transfer transfer,
    const SubtitleGeneratedGrayStyle& generated,int spatialFill=0,
    SubtitleCompositionPhase phase=SubtitleCompositionPhase::All)
{
    if (!hook || backgroundMode<0 || backgroundMode>5) return false;
    const char* names[]={"enabled","composition_phase","source_left","source_top","source_right","source_bottom",
        "destination_left","destination_top","destination_right","destination_bottom","background_mode",
        "glyph_scale","glyph_translate_x","glyph_translate_y","panel_flags",
        "transfer_mode","picture_top","picture_bottom","display_picture_top","display_picture_bottom","transfer_gamma",
        "content_left","content_top","content_right","content_bottom",
        "cleanup_fill_mode","generated_clean_left","generated_clean_top","generated_clean_right","generated_clean_bottom",
        "text_color_r","text_color_g","text_color_b",
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
    const bool selectedPicture=geometry.displayPictureRight>geometry.displayPictureLeft &&
        geometry.displayPictureBottom>geometry.displayPictureTop;
    const int displayTop=selectedPicture?geometry.displayPictureTop:geometry.pictureTop;
    const int displayBottom=selectedPicture?geometry.displayPictureBottom:geometry.pictureBottom;
    const auto roi=ComputeSubtitleGaussianRegion(geometry,backgroundMode,generated.blurPixels);
    const float values[]={geometry.valid?1.0f:0.0f,float(static_cast<int>(phase)),float(s.left),float(s.top),float(s.right),float(s.bottom),
        float(d.left),float(d.top),float(d.right),float(d.bottom),float(backgroundMode),
        geometry.glyphScale,geometry.glyphTranslateX,geometry.glyphTranslateY,
        float((geometry.roundedCorners?1:0)+(geometry.extendToBar?2:0)+(geometry.centeredGlyphMapping?4:0)),transferMode,
        float(geometry.pictureTop),float(geometry.pictureBottom),float(displayTop),float(displayBottom),gamma,
        float(geometry.content.left),float(geometry.content.top),float(geometry.content.right),float(geometry.content.bottom),
        float((std::max)(0,(std::min)(3,spatialFill))),float(geometry.generatedCleanup.left),float(geometry.generatedCleanup.top),
        float(geometry.generatedCleanup.right),float(geometry.generatedCleanup.bottom),
        generated.textColor[0],generated.textColor[1],generated.textColor[2],
        generated.color[0],generated.color[1],generated.color[2],generated.opacity,
        generated.maxLuminance,generated.blurPixels,roi.active?1.0f:0.0f,roi.left,roi.top,roi.width,roi.height,roi.step,
        generated.borderColor[0],generated.borderColor[1],generated.borderColor[2],
        generated.borderOpacity,generated.borderWidth,
        float(geometry.pictureCapture.left),float(geometry.pictureCapture.top),
        float(geometry.pictureCapture.right),float(geometry.pictureCapture.bottom),float(geometry.glyphLines[0].left),float(geometry.glyphLines[0].top),float(geometry.glyphLines[0].right),float(geometry.glyphLines[0].bottom),float(geometry.glyphLines[1].left),float(geometry.glyphLines[1].top),float(geometry.glyphLines[1].right),float(geometry.glyphLines[1].bottom),float(geometry.glyphLines[2].left),float(geometry.glyphLines[2].top),float(geometry.glyphLines[2].right),float(geometry.glyphLines[2].bottom)};
    constexpr unsigned parameterCount=sizeof(names)/sizeof(names[0]);
    static_assert(sizeof(values)/sizeof(values[0])==parameterCount,"subtitle parameter mismatch");
    bool found[parameterCount]{};
    for(int i=0;i<hook->num_parameters;++i) {
        const auto& parameter=hook->parameters[i];
        if(!parameter.name || !parameter.data || parameter.type!=PL_VAR_FLOAT ||
            parameter.mode!=PL_HOOK_PAR_DYNAMIC) continue;
        for(unsigned n=0;n<parameterCount;++n) if(std::strcmp(parameter.name,names[n])==0) {
            parameter.data->f=values[n];found[n]=true;break;
        }
    }
    for(bool present:found)if(!present)return false;
    return true;
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

// Preparation disables peak detection. Sharing the output renderer would reset
// its HDR history on each new source packet but not on repeated display frames.
class SubtitlePreparationRenderer {
    pl_renderer renderer=nullptr;
    pl_gpu owner=nullptr;
public:
    SubtitlePreparationRenderer()=default;
    SubtitlePreparationRenderer(const SubtitlePreparationRenderer&)=delete;
    SubtitlePreparationRenderer& operator=(const SubtitlePreparationRenderer&)=delete;
    ~SubtitlePreparationRenderer() { Reset(); }
    void Reset() { pl_renderer_destroy(&renderer);owner=nullptr; }
    pl_renderer Get(pl_gpu gpu) {
        if(owner!=gpu)Reset();
        if(!renderer && gpu) { renderer=pl_renderer_create(nullptr,gpu);owner=gpu; }
        return renderer;
    }
};

// Compose before the main renderer crops away letterbox pixels. Reuse a
// full-resolution floating-point surface and retain the input transfer function;
// output tone mapping, scaling, NLS and viewport placement occur only afterward.
inline bool RenderSubtitleFullRaster(SubtitlePreparationRenderer& preparation,pl_gpu gpu,
    const pl_frame& source,const pl_hook* hook,pl_tex* surface,pl_frame& composed,
    bool blitSource,bool blitDestination,bool independentAlpha=false,
    const pl_deinterlace_params* deinterlaceParams=nullptr,
    const SubtitleSourceProcessingOptions* sourceProcessing=nullptr)
{
    const auto renderer=preparation.Get(gpu);
    if(!renderer || source.num_planes<1 || !source.planes[0].texture)return false;
    const auto texture=source.planes[0].texture;
    pl_tex_params storage{};
    storage.w=texture->params.w;storage.h=texture->params.h;
    storage.format=pl_find_fmt(gpu,PL_FMT_FLOAT,4,16,0,
        static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE|
            ((blitSource||blitDestination)?PL_FMT_CAP_BLITTABLE:0)));
    if(!storage.format && blitDestination) {
        blitDestination=false;
        storage.format=pl_find_fmt(gpu,PL_FMT_FLOAT,4,16,0,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE));
    }
    storage.sampleable=true;storage.renderable=true;
    storage.blit_src=blitSource;storage.blit_dst=blitDestination;
    if(!storage.format || !pl_tex_recreate(gpu,surface,&storage))return false;
    auto input=source;input.crop={0,0,float(storage.w),float(storage.h)};
    input.rotation=PL_ROTATION_0;input.overlays=nullptr;input.num_overlays=0;
    // MPV hooks inherit the input alpha mode. The layer hook replaces alpha
    // completely, but needs independent-alpha metadata through libplacebo.
    if(independentAlpha)input.repr.alpha=PL_ALPHA_INDEPENDENT;
    pl_frame target{};target.num_planes=1;
    target.planes[0].texture=*surface;target.planes[0].components=4;
    for(int i=0;i<4;++i)target.planes[0].component_mapping[i]=i;
    target.repr.sys=PL_COLOR_SYSTEM_RGB;target.repr.levels=PL_COLOR_LEVELS_FULL;
    target.repr.alpha=independentAlpha?PL_ALPHA_INDEPENDENT:PL_ALPHA_NONE;target.color=source.color;
    auto params=pl_render_fast_params;
    params.dither_params=nullptr;params.peak_detect_params=nullptr;
    // Native-plane deinterlacing runs before the MAIN subtitle hook. Keep the
    // caller's field, first_field, prev and next on the copied source frame.
    params.deinterlace_params=deinterlaceParams;
    if(sourceProcessing) {
        params.plane_upscaler=sourceProcessing->planeUpscaler;
        params.plane_downscaler=sourceProcessing->planeDownscaler;
        params.antiringing_strength=sourceProcessing->antiringingStrength;
        params.deband_params=sourceProcessing->debandEnabled?
            &sourceProcessing->debandParams:nullptr;
    }
    if(independentAlpha) {
        // The default opaque clear forces alpha to one even for an RGBA hook.
        // Every pixel is written by this full-raster pass.
        params.background=PL_CLEAR_SKIP;
        params.background_transparency=1.0f;
    }
    params.hooks=hook?&hook:nullptr;params.num_hooks=hook?1:0;
    if(!pl_render_image(renderer,&input,&target,&params))return false;
    if(hook) {
        const auto errors=pl_renderer_get_errors(renderer);
        for(int i=0;i<errors.num_disabled_hooks;++i)
            if(errors.disabled_hooks[i]==hook->signature)return false;
    }
    composed=target;composed.crop=source.crop;composed.rotation=source.rotation;
    composed.overlays=source.overlays;composed.num_overlays=source.num_overlays;
    return true;
}

inline bool ComposeSubtitleBeforeCrop(SubtitlePreparationRenderer& preparation,pl_gpu gpu,
    const pl_frame& source,const pl_hook* hook,pl_tex* surface,pl_frame& composed,
    bool temporalBlit=false,
    const SubtitleSourceProcessingOptions* sourceProcessing=nullptr)
{
    return hook && RenderSubtitleFullRaster(preparation,gpu,source,hook,surface,
        composed,false,temporalBlit,false,nullptr,sourceProcessing);
}

// The layer carries straight RGB in source transfer and independent alpha.
inline bool ComposeSubtitleLayerBeforeCrop(SubtitlePreparationRenderer& preparation,pl_gpu gpu,
    const pl_frame& source,const pl_hook* hook,pl_tex* surface,pl_frame& layer,
    const pl_deinterlace_params* deinterlaceParams=nullptr,
    const SubtitleSourceProcessingOptions* sourceProcessing=nullptr)
{
    return hook && RenderSubtitleFullRaster(preparation,gpu,source,hook,surface,
        layer,true,false,true,deinterlaceParams,sourceProcessing);
}

inline bool CaptureSubtitleTemporalBackground(SubtitlePreparationRenderer& preparation,pl_gpu gpu,
    const pl_frame& source,pl_tex* surface,
    const SubtitleSourceProcessingOptions* sourceProcessing=nullptr)
{
    pl_frame captured{};
    return RenderSubtitleFullRaster(preparation,gpu,source,nullptr,surface,
        captured,true,false,false,nullptr,sourceProcessing);
}
