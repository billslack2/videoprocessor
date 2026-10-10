#pragma once
#include <ActivePictureEvidence.h>
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <cmath>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

// Separate, current-source presentation evidence. Neither type is native AR
// authority; callers must not feed these bounds into observations or history.
struct SubtitleAssistedSourceNomination {
    bool nominated=false;
    ActivePictureBounds candidate;
    int rawTop=0,rawBottom=0,blackThreshold=0;
    const char* reason="not-evaluated";
};
struct SubtitleAssistedSourceProof {
    bool verified=false;
    ActivePictureBounds candidate;
    SubtitleBoxRect protectedSubtitleBounds;
    size_t nativePixels=0,ownedBarPixels=0;
    const char* reason="not-evaluated";
};
namespace SubtitleAssistedSourceDetail {
inline bool Neutral(const AnalysisLumaSample& p) {
    return std::abs(int(p.chromaU)-512)<=32 && std::abs(int(p.chromaV)-512)<=32;
}
// Each boundary is corroborated independently at the same columns, in at
// least three separated horizontal zones. A clean opposite edge never supplies
// the location or contrast of the contaminated edge.
inline bool Boundary(const AnalysisLumaSource& source,int at,bool top,int guard,int black,const SubtitleBoxResult* exclude=nullptr) {
    int zones=0,left=0,right=0;
    for(int zone=0;zone<8;++zone) {
        int supported=0;
        for(int col=0;col<16;++col) {
            const int x=(source.width*(2*(zone*16+col)+1))/256;
            AnalysisLumaSample outside,inside,deep;
            const int oy=top?at-guard-1:at+guard;
            const int iy=top?at+guard:at-guard-1;
            const int dy=top?at+2*guard:at-2*guard-1;
            bool glyphColumn=false;
            if(exclude)for(int line=0;line<exclude->lineCount && line<3;++line) {
                const auto& r=exclude->lineBounds[line];
                const int lo=(std::min)({oy,iy,dy})-guard-2,hi=(std::max)({oy,iy,dy})+guard+2;
                if(r.Valid() && r.top<=hi && r.bottom>lo && x>=r.left-guard-2 && x<r.right+guard+2)
                    glyphColumn=true;
            }
            if(glyphColumn)continue;
            if(!source.Sample(x,oy,outside)||!source.Sample(x,iy,inside)||!source.Sample(x,dy,deep))return false;
            if(outside.luma<=black && Neutral(outside) &&
                int(inside.luma)>=int(outside.luma)+8 && int(deep.luma)>=int(outside.luma)+8)++supported;
        }
        if(supported>=4) {++zones;if(zone<4)++left;else ++right;}
    }
    return zones>=3 && left>0 && right>0;
}
}
inline SubtitleAssistedSourceNomination NominateSubtitleAssistedSourceBounds(
    const AnalysisLumaSource& source,const ActivePictureEvidence& raw) {
    SubtitleAssistedSourceNomination out;
    auto reject=[&](const char* why){out.reason=why;return out;};
    if(!source.IsValid() || !source.generation || source.width<320 || source.height<180 || source.width>4096 || source.height>2160 ||
        !raw.available || raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED ||
        raw.proposedBounds.rasterWidth!=source.width || raw.proposedBounds.rasterHeight!=source.height)
        return reject("source-or-raw-unavailable");
    const auto global=EvaluateActivePictureGlobalNearBlack(source);
    if(!global.evaluated || global.nearBlack)return reject("global-near-black");
    // Cold assistance deliberately supports one contaminated edge only.
    if(raw.top.trusted==raw.bottom.trusted)return reject("requires-one-independent-clean-edge");
    const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    if(step<1 || step>4)return reject("unsupported-sampling-step");
    out.rawTop=raw.top.barPixels;out.rawBottom=source.height-raw.bottom.barPixels;
    if(out.rawTop<=3*step || out.rawBottom>=source.height-3*step ||
        out.rawTop>source.height/3 || out.rawBottom<2*source.height/3 || out.rawTop>=out.rawBottom)
        return reject("candidate-depth-out-of-range");
    if(!std::isfinite(raw.top.lumaFloor) || !std::isfinite(raw.bottom.lumaFloor) || raw.top.lumaFloor<0 || raw.bottom.lumaFloor<0 || raw.top.lumaFloor>80 || raw.bottom.lumaFloor>80 ||
        std::abs(raw.top.lumaFloor-raw.bottom.lumaFloor)>8 ||
        raw.top.neutralChromaFraction<0.98 || raw.bottom.neutralChromaFraction<0.98)
        return reject("incompatible-bar-palette");
    out.blackThreshold=static_cast<int>((std::max)(raw.top.lumaFloor,raw.bottom.lumaFloor))+24;
    if(!SubtitleAssistedSourceDetail::Boundary(source,out.rawTop,true,step,out.blackThreshold) ||
        !SubtitleAssistedSourceDetail::Boundary(source,out.rawBottom,false,step,out.blackThreshold))
        return reject("independent-boundary-support-missing");
    out.candidate={0,out.rawTop-step,source.width,out.rawBottom+step,source.width,source.height,
        double(source.width)/(out.rawBottom-out.rawTop+2*step),ActivePictureBounds::BarAxes::NONE};
    out.nominated=true;out.reason="independent-boundary-nomination";return out;
}

// Invoke only with the fresh detector result/ink produced for nomination's
// exact planes and frame. The temporal owner must bind source frame identity,
// epochs and policy. This function independently reattests all owned bar seeds
// in the current planar source; cleanup/capture/generated panels are unused.
inline SubtitleAssistedSourceProof VerifySubtitleAssistedSourceEvidence(
    const AnalysisLumaSource& source,const SubtitleAssistedSourceNomination& nomination,
    const SubtitleBoxResult& text,const SubtitleInkSnapshot& ink,const SubtitleBoxRect& captureBounds) {
    SubtitleAssistedSourceProof out;
    auto reject=[&](const char* why){out.reason=why;return out;};
    const auto& b=nomination.candidate;
    const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    if(!source.IsValid() || !source.generation || (source.format!=AnalysisLumaFormat::P010 && source.format!=AnalysisLumaFormat::P210) ||
        !nomination.nominated || source.width<320 || source.height<180 || source.width>4096 || source.height>2160 ||
        step<1 || step>4 || b.rasterWidth!=source.width || b.rasterHeight!=source.height ||
        b.left!=0 || b.right!=source.width || b.top!=nomination.rawTop-step ||
        b.bottom!=nomination.rawBottom+step || b.top<=0 || b.bottom>=source.height ||
        b.top>=b.bottom || nomination.blackThreshold<24 || nomination.blackThreshold>104)
        return reject("invalid-source-or-nomination");
    const auto global=EvaluateActivePictureGlobalNearBlack(source);
    if(!global.evaluated || global.nearBlack)return reject("global-near-black");
    if(!SubtitleAssistedSourceDetail::Boundary(source,nomination.rawTop,true,step,nomination.blackThreshold,&text) ||
        !SubtitleAssistedSourceDetail::Boundary(source,nomination.rawBottom,false,step,nomination.blackThreshold,&text))
        return reject("current-boundary-conflict");
    if(!captureBounds.Valid() || captureBounds.left<0 || captureBounds.top<0 || captureBounds.right>source.width || captureBounds.bottom>source.height || !text.detected || text.held || text.workLimit || text.lineCount<1 || text.lineCount>3 ||
        text.diagnosticComponentCount<4 || !text.bounds.Valid() || text.bounds.left<0 || text.bounds.top<0 ||
        text.bounds.right>source.width || text.bounds.bottom>source.height ||
        text.bounds.right-text.bounds.left>source.width*4/5 || text.bounds.bottom-text.bounds.top>source.height/4 ||
        !ink.rawEvidenceComplete || !ink.paletteValid || ink.inkFloor<=nomination.blackThreshold+48 ||
        std::abs(ink.inkU-512)>32 || std::abs(ink.inkV-512)>32 ||
        ink.step!=step || ink.width!=(source.width+step-1)/step || ink.height!=(source.height+step-1)/step)
        return reject("fresh-morphology-required");
    const size_t words=(size_t(ink.width)*ink.height+63)/64;
    if(ink.sourceRows.size()!=size_t(ink.height) || ink.rawInk.size()!=words || ink.ownedInk.size()!=words)
        return reject("invalid-ink-grid");
    const size_t bandPixels=size_t(source.width)*(b.top+source.height-b.bottom);
    if(bandPixels>4*1024*1024)return reject("native-band-budget");
    std::vector<uint8_t> mask(bandPixels,0);
    const int radius=step+1,moat=2*step;
    size_t masked=0,seeds[2]={};int previous=-1;
    auto bandOffset=[&](int y){return size_t(y<b.top?y:b.top+y-b.bottom)*source.width;};
    for(int row=0;row<ink.height;++row) {
        const int y=ink.sourceRows[row];
        if(y<0 || y>=source.height || y<=previous)return reject("invalid-ink-rows");
        previous=y;
        if(y>=b.top && y<b.bottom)continue;
        for(int col=0;col<ink.width;++col) {
            if(!ink.Get(col,row,true))continue;
            const int x=col*step;bool lineOwned=false;
            for(int line=0;line<text.lineCount;++line) {
                const auto& r=text.lineBounds[line];
                lineOwned|=r.Valid() && x>=r.left && x<r.right && y>=r.top && y<r.bottom;
            }
            AnalysisLumaSample p;
            if(!ink.Get(col,row) || !lineOwned || x<text.bounds.left || x>=text.bounds.right ||
                y<text.bounds.top || y>=text.bounds.bottom || !source.Sample(x,y,p) ||
                p.luma<ink.inkFloor || !SubtitleAssistedSourceDetail::Neutral(p))
                return reject("owned-seed-not-current-glyph");
            const bool top=y<b.top;
            if(top?y>=b.top-moat:y<b.bottom+moat)return reject("glyph-touches-picture-moat");
            ++seeds[top?0:1];
            for(int yy=(std::max)(0,y-radius);yy<(std::min)(source.height,y+radius+1);++yy) {
                if(top?yy>=b.top-moat:yy<b.bottom+moat)continue;
                const size_t base=bandOffset(yy);
                for(int xx=(std::max)(0,x-radius);xx<(std::min)(source.width,x+radius+1);++xx)
                    if(!mask[base+xx])mask[base+xx]=1,++masked;
            }
        }
    }
    if((seeds[0]>0)==(seeds[1]>0) || seeds[0]+seeds[1]<8 || masked>bandPixels/10)
        return reject("bar-glyph-coverage-out-of-range");
    auto code=[](const uint8_t* p){return int((unsigned(p[0])|(unsigned(p[1])<<8))>>6);};
#if defined(_M_X64) || defined(__x86_64__)
    const auto black=_mm_set1_epi16(static_cast<short>(nomination.blackThreshold));
    const auto low=_mm_set1_epi16(480),high=_mm_set1_epi16(544);
#endif
    for(int y=0;y<source.height;++y) {
        if(y>=b.top && y<b.bottom)continue;
        const auto* luma=source.data+size_t(y)*source.rowBytes;
        const auto* uv=source.data+size_t(source.height)*source.rowBytes+
            size_t(source.format==AnalysisLumaFormat::P210?y:y/2)*source.chromaRowBytes;
        const auto* allowed=mask.data()+bandOffset(y);
        auto pixelSafe=[&](int x) {
            const int u=code(uv+size_t(x/2)*4),v=code(uv+size_t(x/2)*4+2),l=code(luma+size_t(x)*2);
            if(std::abs(u-512)>32 || std::abs(v-512)>32)return false;
            if(l<=nomination.blackThreshold)return true;
            if(!allowed[x] || x<captureBounds.left || x>=captureBounds.right || y<captureBounds.top || y>=captureBounds.bottom)return false;
            ++out.ownedBarPixels;return true;
        };
        int x=0;
#if defined(_M_X64) || defined(__x86_64__)
        for(;x+8<=source.width;x+=8) {
            const auto ys=_mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(luma+size_t(x)*2)),6);
            const auto cs=_mm_srli_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(uv+size_t(x)*2)),6);
            out.nativePixels+=8;
            if(_mm_movemask_epi8(_mm_or_si128(_mm_cmpgt_epi16(low,cs),_mm_cmpgt_epi16(cs,high))))
                return reject("native-chromatic-content");
            if(_mm_movemask_epi8(_mm_cmpgt_epi16(ys,black)))
                for(int i=0;i<8;++i)if(!pixelSafe(x+i))return reject("native-unowned-content");
        }
#endif
        for(;x<source.width;++x) {++out.nativePixels;if(!pixelSafe(x))return reject("native-unowned-content");}
    }
    if(out.ownedBarPixels<8)return reject("insufficient-native-bar-ink");
    out.verified=true;out.candidate=b;out.protectedSubtitleBounds=captureBounds;
    out.reason="current-glyph-masked-source-proof";return out;
}




