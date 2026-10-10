#pragma once
#include "SubtitleAssistedSourceEvidence.h"
#include <cstring>

// Optional reacquisition stays presentation-only. All geometry is current-source
// evidence, never an observation for native aspect-ratio learning.
namespace SubtitleReacquisitionDetail {
inline bool Supported(const AnalysisLumaSource& s) {
    return s.IsValid() && s.generation && s.width>=320 && s.height>=180 && s.width<=4096 && s.height<=2160 &&
        SubtitleBoxDetector::SamplingStep(s.width,s.height)<=4 &&
        (s.format==AnalysisLumaFormat::P010 || s.format==AnalysisLumaFormat::P210 ||
         (s.format==AnalysisLumaFormat::NativeYuv422 &&
          (s.encoding==VideoFrameEncoding::V210 || s.encoding==VideoFrameEncoding::UYVY || s.encoding==VideoFrameEncoding::HDYC)));
}
// The caller validates layout once. Native reads have exactly Sample's code
// conversion, without repeating descriptor validation for millions of pixels.
struct Reader {
    const AnalysisLumaSource& s;
    static uint32_t Word(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return v;}
    static uint16_t Code(const uint8_t* p){return uint16_t((unsigned(p[0])|(unsigned(p[1])<<8))>>6);}
    AnalysisLumaSample At(int x,int y) const {
        const auto* row=s.data+size_t(y)*s.rowBytes;
        if(s.format==AnalysisLumaFormat::P010 || s.format==AnalysisLumaFormat::P210) {
            const auto* uv=s.data+size_t(s.height)*s.rowBytes+size_t(s.format==AnalysisLumaFormat::P210?y:y/2)*s.chromaRowBytes+size_t(x/2)*4;
            return {Code(row+size_t(x)*2),Code(uv),Code(uv+2)};
        }
        if(s.encoding!=VideoFrameEncoding::V210) {
            const auto* p=row+size_t(x/2)*4;
            return {uint16_t(unsigned(p[(x&1)?3:1])<<2),uint16_t(unsigned(p[0])<<2),uint16_t(unsigned(p[2])<<2)};
        }
        const auto* p=row+size_t(x/6)*16;
        const uint32_t a=Word(p),b=Word(p+4),c=Word(p+8),d=Word(p+12);
        switch(x%6) {
        case 0:return {uint16_t((a>>10)&1023),uint16_t(a&1023),uint16_t((a>>20)&1023)};
        case 1:return {uint16_t(b&1023),uint16_t(a&1023),uint16_t((a>>20)&1023)};
        case 2:return {uint16_t((b>>20)&1023),uint16_t((b>>10)&1023),uint16_t(c&1023)};
        case 3:return {uint16_t((c>>10)&1023),uint16_t((b>>10)&1023),uint16_t(c&1023)};
        case 4:return {uint16_t(d&1023),uint16_t((c>>20)&1023),uint16_t((d>>10)&1023)};
        default:return {uint16_t((d>>20)&1023),uint16_t((c>>20)&1023),uint16_t((d>>10)&1023)};
        }
    }
};
inline bool Geometry(const AnalysisLumaSource& s,const ActivePictureBounds& b) {
    const int step=SubtitleBoxDetector::SamplingStep(s.width,s.height);
    return b.rasterWidth==s.width && b.rasterHeight==s.height && b.left==0 && b.right==s.width &&
        b.top>3*step && b.bottom<s.height-3*step && b.top<=s.height/3 && b.bottom>=2*s.height/3 && b.top<b.bottom;
}
inline int BlackThreshold(const AnalysisLumaSource& s) {
    std::array<int,64> top{},bottom{};int neutral[2]={};Reader reader{s};
    for(int n=0;n<64;++n) {
        const int x=((2*n+1)*s.width)/128;
        const auto a=reader.At(x,0),b=reader.At(x,s.height-1);
        top[n]=a.luma;bottom[n]=b.luma;
        neutral[0]+=SubtitleAssistedSourceDetail::Neutral(a);neutral[1]+=SubtitleAssistedSourceDetail::Neutral(b);
    }
    std::sort(top.begin(),top.end());std::sort(bottom.begin(),bottom.end());
    if(top[6]>80 || bottom[6]>80 || std::abs(top[6]-bottom[6])>8 || neutral[0]<63 || neutral[1]<63)return -1;
    const int low=(std::max)(top[6],bottom[6]);
    return (low<32?0:(std::max)(48,low))+24;
}
struct ConnectedInk {
    SubtitleBoxRect roi;
    std::vector<uint8_t> pixels;
    size_t connected=0,disconnected=0;
    bool valid=false;
    const char* reason="connected-ink-unavailable";
    bool Contains(int x,int y) const {
        return valid && x>=roi.left && x<roi.right && y>=roi.top && y<roi.bottom &&
            pixels[size_t(y-roi.top)*(roi.right-roi.left)+x-roi.left]==4;
    }
};
inline ConnectedInk BuildConnectedInk(const AnalysisLumaSource& s,const ActivePictureBounds& b,
    const SubtitleBoxResult& text,const SubtitleInkSnapshot& ink,const SubtitleBoxRect& capture,int black) {
    ConnectedInk out;auto fail=[&](const char* r){out.reason=r;return out;};
    const int step=SubtitleBoxDetector::SamplingStep(s.width,s.height),radius=step+1,moat=2*step;
    if(!Supported(s) || !Geometry(s,b) || !capture.Valid() || capture.left<0 || capture.top<0 || capture.right>s.width || capture.bottom>s.height ||
        !text.detected || text.held || text.workLimit || text.lineCount<1 || text.lineCount>3 || text.diagnosticComponentCount<4 ||
        !text.bounds.Valid() || text.bounds.left<0 || text.bounds.top<0 || text.bounds.right>s.width || text.bounds.bottom>s.height ||
        text.bounds.right-text.bounds.left>s.width*4/5 || text.bounds.bottom-text.bounds.top>s.height/4 ||
        !ink.rawEvidenceComplete || !ink.paletteValid || ink.inkFloor<=black+48 || std::abs(ink.inkU-512)>32 || std::abs(ink.inkV-512)>32 ||
        ink.step!=step || ink.width!=(s.width+step-1)/step || ink.height!=(s.height+step-1)/step)
        return fail("connected-current-morphology-required");
    const size_t words=(size_t(ink.width)*ink.height+63)/64;
    if(ink.sourceRows.size()!=size_t(ink.height) || ink.rawInk.size()!=words || ink.ownedInk.size()!=words)return fail("connected-invalid-grid");
    struct Seed{int x,y;};std::vector<Seed> seeds;seeds.reserve(2048);
    int previous=-1,minX=s.width,minY=s.height,maxX=0,maxY=0;unsigned edges=0;
    Reader reader{s};
    for(int gy=0;gy<ink.height;++gy) {
        const int y=ink.sourceRows[gy];if(y<0 || y>=s.height || y<=previous)return fail("connected-invalid-rows");previous=y;
        if(y>=b.top && y<b.bottom)continue;
        for(int gx=0;gx<ink.width;++gx) {
            if(!ink.Get(gx,gy,true))continue;const int x=gx*step;bool ownedLine=false;
            for(int line=0;line<text.lineCount;++line) {const auto& r=text.lineBounds[line];ownedLine|=r.Valid() && x>=r.left && x<r.right && y>=r.top && y<r.bottom;}
            const auto p=reader.At(x,y);
            if(!ink.Get(gx,gy) || !ownedLine || x<text.bounds.left || x>=text.bounds.right || y<text.bounds.top || y>=text.bounds.bottom ||
                x<capture.left || x>=capture.right || y<capture.top || y>=capture.bottom || p.luma<ink.inkFloor || !SubtitleAssistedSourceDetail::Neutral(p))
                return fail("connected-seed-not-current");
            const bool top=y<b.top;
            if(top?y>=b.top-moat:y<b.bottom+moat)return fail("connected-glyph-touches-moat");
            edges|=top?1:2;seeds.push_back({x,y});if(seeds.size()>32768)return fail("connected-seed-budget");
            minX=(std::min)(minX,x);maxX=(std::max)(maxX,x);minY=(std::min)(minY,y);maxY=(std::max)(maxY,y);
        }
    }
    if(seeds.size()<8 || edges==3)return fail("connected-bar-glyph-coverage");
    out.roi={(std::max)({0,capture.left,minX-radius}),(std::max)({0,capture.top,minY-radius}),
        (std::min)({s.width,capture.right,maxX+radius+1}),(std::min)({s.height,capture.bottom,maxY+radius+1})};
    if(edges==1)out.roi.bottom=(std::min)(out.roi.bottom,b.top-moat);else out.roi.top=(std::max)(out.roi.top,b.bottom+moat);
    if(!out.roi.Valid())return fail("connected-empty-region");
    const int w=out.roi.right-out.roi.left,h=out.roi.bottom-out.roi.top;
    if(size_t(w)*h>512*1024)return fail("connected-region-budget");
    out.pixels.assign(size_t(w)*h,0);
    auto index=[&](int x,int y){return size_t(y-out.roi.top)*w+x-out.roi.left;};
    for(const auto& seed:seeds)for(int y=(std::max)(out.roi.top,seed.y-radius);y<(std::min)(out.roi.bottom,seed.y+radius+1);++y)
        for(int x=(std::max)(out.roi.left,seed.x-radius);x<(std::min)(out.roi.right,seed.x+radius+1);++x)out.pixels[index(x,y)]=1;
    size_t allowed=0;
    for(int y=out.roi.top;y<out.roi.bottom;++y)for(int x=out.roi.left;x<out.roi.right;++x) {
        auto& value=out.pixels[index(x,y)];if(!value)continue;
        const auto p=reader.At(x,y);value=p.luma>black && SubtitleAssistedSourceDetail::Neutral(p)?1:0;allowed+=value;
    }
    if(allowed>65536 || allowed>size_t(s.width)*(b.top+s.height-b.bottom)/10)return fail("connected-pixel-budget");
    std::vector<size_t> queue;queue.reserve(allowed);
    for(const auto& seed:seeds){const auto i=index(seed.x,seed.y);if(out.pixels[i]==1){out.pixels[i]=4;queue.push_back(i);}}
    for(size_t head=0;head<queue.size();++head) {
        const int y=int(queue[head]/w),x=int(queue[head]%w);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(!dx && !dy)continue;const int xx=x+dx,yy=y+dy;
            if(xx<0 || xx>=w || yy<0 || yy>=h)continue;const size_t i=size_t(yy)*w+xx;
            if(out.pixels[i]==1){out.pixels[i]=4;queue.push_back(i);}
        }
    }
    out.connected=queue.size();out.disconnected=allowed-out.connected;out.valid=true;out.reason="current-connected-native-ink";return out;
}
struct Audit {bool valid=false;int firstTop=-1,lastBottom=-1;size_t pixels=0;};
inline Audit AuditBands(const AnalysisLumaSource& s,const ActivePictureBounds& b,const ConnectedInk& ink,int black) {
    Audit out;const size_t count=size_t(s.width)*(b.top+s.height-b.bottom);if(!ink.valid || count>4*1024*1024)return out;
    Reader reader{s};
    for(int y=0;y<s.height;++y) {
        if(y>=b.top && y<b.bottom)continue;
        for(int x=0;x<s.width;++x) {
            const auto p=reader.At(x,y);++out.pixels;const bool neutral=SubtitleAssistedSourceDetail::Neutral(p);
            if(neutral && (p.luma<=black || ink.Contains(x,y)))continue;
            if(y<b.top){if(out.firstTop<0)out.firstTop=y;}else out.lastBottom=y;
        }
    }
    out.valid=true;return out;
}
}

inline SubtitleAssistedSourceNomination ReacquisitionNominationFor(const AnalysisLumaSource& source,const ActivePictureBounds& candidate) {
    SubtitleAssistedSourceNomination out;auto fail=[&](const char* r){out.reason=r;return out;};
    if(!SubtitleReacquisitionDetail::Supported(source) || !SubtitleReacquisitionDetail::Geometry(source,candidate))return fail("reacquisition-source-or-geometry");
    const auto global=EvaluateActivePictureGlobalNearBlack(source);if(!global.evaluated || global.nearBlack)return fail("global-near-black");
    const int black=SubtitleReacquisitionDetail::BlackThreshold(source);if(black<0)return fail("incompatible-bar-palette");
    const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    out.nominated=true;out.candidate=candidate;out.candidate.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
    out.candidate.aspectRatio=double(source.width)/(candidate.bottom-candidate.top);
    out.rawTop=candidate.top+step;out.rawBottom=candidate.bottom-step;out.blackThreshold=black;out.reason="current-reacquisition-nomination";return out;
}
inline SubtitleAssistedSourceProof VerifySubtitleAssistedReacquisitionEvidence(const AnalysisLumaSource& source,
    const SubtitleAssistedSourceNomination& nomination,const SubtitleBoxResult& text,const SubtitleInkSnapshot& ink,const SubtitleBoxRect& capture) {
    SubtitleAssistedSourceProof result;
    const auto current=ReacquisitionNominationFor(source,nomination.candidate);
    if(!nomination.nominated || !current.nominated || nomination.rawTop!=current.rawTop || nomination.rawBottom!=current.rawBottom || nomination.blackThreshold!=current.blackThreshold)
        {result.reason="reacquisition-current-nomination-mismatch";return result;}
    if(source.format!=AnalysisLumaFormat::P010 && source.format!=AnalysisLumaFormat::P210){result.reason="reacquisition-verification-requires-planar";return result;}
    const auto connected=SubtitleReacquisitionDetail::BuildConnectedInk(source,current.candidate,text,ink,capture,current.blackThreshold);
    if(!connected.valid){result.reason=connected.reason;return result;}
    if(connected.disconnected){result.reason="native-disconnected-ink";return result;}
    return VerifySubtitleAssistedSourceEvidence(source,current,text,ink,capture);
}
struct SubtitleAssistedReacquisitionMeasurement {
    bool available=false;
    SubtitleAssistedSourceNomination nomination;
    SubtitleBoxResult text;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
    size_t searchPixels=0;
    const char* reason="not-evaluated";
};
inline SubtitleAssistedReacquisitionMeasurement MeasureSubtitleAssistedReacquisition(const AnalysisLumaSource& source,
    uint64_t sequence,uint64_t viewportGeneration,int nearBarDistance=0,int optimizationMode=0) {
    SubtitleAssistedReacquisitionMeasurement out;auto fail=[&](const char* r){out.reason=r;return out;};
    if(!sequence || !SubtitleReacquisitionDetail::Supported(source))return fail("reacquisition-source-unavailable");
    const auto raw=ExtractActivePictureEvidence(source);
    if(!raw.available || raw.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)return fail("reacquisition-no-rough-bars");
    if(!std::isfinite(raw.top.lumaFloor) || !std::isfinite(raw.bottom.lumaFloor) || raw.top.lumaFloor<0 || raw.bottom.lumaFloor<0 ||
        raw.top.lumaFloor>80 || raw.bottom.lumaFloor>80 || std::abs(raw.top.lumaFloor-raw.bottom.lumaFloor)>8 ||
        raw.top.neutralChromaFraction<0.98 || raw.bottom.neutralChromaFraction<0.98)return fail("incompatible-rough-bar-palette");
    const int step=SubtitleBoxDetector::SamplingStep(source.width,source.height);
    const ActivePictureBounds initial{0,raw.top.barPixels-step,source.width,source.height-raw.bottom.barPixels+step,source.width,source.height};
    const auto first=ReacquisitionNominationFor(source,initial);if(!first.nominated)return fail(first.reason);
    SubtitleBoxDetector scanner;scanner.SetNearBarDistance(nearBarDistance);scanner.SetOptimizationMode(optimizationMode);
    const auto text=scanner.Analyze(source,initial.top,initial.bottom,sequence,viewportGeneration);const auto ink=scanner.InkSnapshot();
    if(!ink) {
        if(text.workLimit)return fail("reacquisition-initial-detector-work-limit");
        if(text.diagnosticReason && std::strcmp(text.diagnosticReason,"no-bright-ink")==0)
            return fail("reacquisition-initial-no-bright-ink");
        return fail("reacquisition-current-ink-unavailable");
    }
    // Search owns no composition footprint. The renderer later intersects this
    // evidence with exact producer-derived capture lines before any crop.
    const auto connected=SubtitleReacquisitionDetail::BuildConnectedInk(source,initial,text,*ink,text.bounds,first.blackThreshold);
    if(!connected.valid)return fail(connected.reason);
    const auto audit=SubtitleReacquisitionDetail::AuditBands(source,initial,connected,first.blackThreshold);
    out.searchPixels=audit.pixels;if(!audit.valid)return fail("reacquisition-search-budget");
    auto adjusted=initial;
    if(audit.firstTop>=0)adjusted.top=(std::max)(0,audit.firstTop-1)&~1;
    if(audit.lastBottom>=0)adjusted.bottom=(std::min)(source.height,(audit.lastBottom+3)&~1);
    if(initial.top-adjusted.top>source.height/12 || adjusted.bottom-initial.bottom>source.height/12)return fail("outward-adjustment-outside-bound");
    out.nomination=ReacquisitionNominationFor(source,adjusted);if(!out.nomination.nominated)return fail(out.nomination.reason);
    scanner.Reset();out.text=scanner.Analyze(source,adjusted.top,adjusted.bottom,sequence,viewportGeneration);out.ink=scanner.InkSnapshot();
    if(!out.text.detected || out.text.held || out.text.workLimit || !out.ink || !out.ink->rawEvidenceComplete)return fail("reacquisition-final-morphology-unavailable");
    if(!SubtitleAssistedSourceDetail::Boundary(source,out.nomination.rawTop,true,step,first.blackThreshold,&out.text) ||
        !SubtitleAssistedSourceDetail::Boundary(source,out.nomination.rawBottom,false,step,first.blackThreshold,&out.text))return fail("current-boundary-conflict");
    out.available=true;out.reason="outward-only-current-source-reacquisition";return out;
}
