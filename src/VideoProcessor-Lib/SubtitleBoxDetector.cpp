#include "pch.h"
#include <SubtitleBoxDetector.h>
#include <SubtitleOpaqueCardRefinement.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    int Width(const SubtitleBoxRect& r) { return r.right - r.left; }
    int Height(const SubtitleBoxRect& r) { return r.bottom - r.top; }
    SubtitleBoxRect Union(const SubtitleBoxRect& a, const SubtitleBoxRect& b)
    {
        return { std::min(a.left,b.left), std::min(a.top,b.top),
            std::max(a.right,b.right), std::max(a.bottom,b.bottom) };
    }
    int Bits(uint64_t x) { int n=0; while(x) { x &= x-1; ++n; } return n; }
    constexpr int CardBoundaryUncertainty=3;
    bool HasBoundaryGlyphCore(const AnalysisLumaSource& source,int x,int y,
        const SubtitleBoxRect& interior,int threshold,int inkU,int inkV,
        size_t* sampleCount=nullptr,size_t sampleLimit=0)
    {
        // A thin antialiased stroke can have its bright core on either side of
        // the uncertain row (a letter's lower curve is a common example).
        // The row itself never proves its own content; a scene stripe cannot
        // qualify merely because it is bright or nearly neutral.
        for(int distance=1;distance<=CardBoundaryUncertainty;++distance)
            for(int sign:{-1,1}) {
                const int sy=y+sign*distance;
                if(sy<0 || sy>=source.height)continue;
                for(int dx=-1;dx<=1;++dx) {
                    const int sx=x+dx;
                    if(sx<interior.left || sx>=interior.right)continue;
                    if(sampleCount) {
                        if(*sampleCount>=sampleLimit)return false;
                        ++*sampleCount;
                    }
                    AnalysisLumaSample core;
                    if(source.Sample(sx,sy,core) && core.luma>=threshold &&
                        std::abs(int(core.chromaU)-inkU)<=32 &&
                        std::abs(int(core.chromaV)-inkV)<=32)return true;
                }
            }
        return false;
    }
    SubtitleBoxRect ProveCardBoundaryContinuation(const AnalysisLumaSource& source,
        SubtitleBoxRect interior,int pictureTop,int pictureBottom,int darkLimit,
        int threshold,int inkU,int inkV,int glyphHeight)
    {
        // The subtitle edge can differ by a few rows from independently
        // measured picture geometry. Prove opaque continuation there instead
        // of classifying the whole uncertain row as black-bar glyph space.
        // A finite card followed by a bright/dim picture stripe fails this.
        auto opaqueBoundaryRow=[&](int sourceY) {
            if(sourceY<0 || sourceY>=source.height)return false;
            unsigned tested=0,black=0,weak=0;int run=0;
            auto supportedRun=[&](int end) {
                if(run<=(std::max)(4,glyphHeight))return true;
                // Joined Arabic strokes and long dashes may exceed a glyph's
                // height. Preserve them only when the same horizontal footprint
                // is corroborated by nearby core ink, rather than by this row.
                unsigned supported=0;
                for(int sx=end-run;sx<end;++sx)
                    supported+=HasBoundaryGlyphCore(source,sx,sourceY,interior,threshold,inkU,inkV);
                return supported*100>=unsigned(run)*85;
            };
            for(int x=interior.left+1;x<interior.right-1;++x) {
                AnalysisLumaSample value;if(!source.Sample(x,sourceY,value))return false;
                ++tested;
                if(value.luma<=darkLimit) {
                    if(!supportedRun(x))return false;
                    ++black;run=0;continue;
                }
                // Count all connected nonblack pixels, including antialiasing:
                // a dim scene seam must not evade the long-stripe rejection.
                ++run;
                bool glyph=value.luma>=threshold &&
                    std::abs(int(value.chromaU)-inkU)<=80 && std::abs(int(value.chromaV)-inkV)<=80;
                if(!glyph && std::abs(int(value.chromaU)-inkU)<=32 &&
                    std::abs(int(value.chromaV)-inkV)<=32)
                    glyph=HasBoundaryGlyphCore(source,x,sourceY,interior,threshold,inkU,inkV);
                if(!glyph)++weak;
            }
            return supportedRun(interior.right-1) && tested>=8 &&
                black*100>=tested*40 && weak*100<=tested*8;
        };
        if(interior.bottom==pictureBottom)
            for(int y=pictureBottom;y<(std::min)(source.height,pictureBottom+CardBoundaryUncertainty);++y) {
                if(!opaqueBoundaryRow(y))break;interior.bottom=y+1;
            }
        if(interior.top==pictureTop)
            for(int y=pictureTop-1;y>=(std::max)(0,pictureTop-CardBoundaryUncertainty);--y) {
                if(!opaqueBoundaryRow(y))break;interior.top=y;
            }
        return interior;
    }


}
int SubtitleBoxDetector::SamplingStep(int width, int height)
{
    return std::max({1, (width + 959) / 960, (height + 539) / 540});
}

void SubtitleBoxDetector::Reset()
{
    m_sampledRows.clear();m_sharedSampleCount=0;
    m_inkSnapshot.reset();
    m_result = {}; m_currentBarAnchor = {}; m_currentSourcePanel = {}; m_currentLinePanels = {}; m_currentCapturePanel = {}; m_currentLineCapturePanels = {}; m_signature = {}; m_currentAnchorSignature = {};
    m_currentLineBounds = {}; m_currentLineSignatures = {}; m_currentPanelTopEdges = {};
    m_generation = m_viewport = m_sequence = 0;
    m_width = m_height = m_top = m_bottom = m_misses = 0;
    m_hasSequence = m_workLimit = false;
}

bool SubtitleBoxDetector::Detect(const AnalysisLumaSource& source, int pictureTop,
    int pictureBottom, SubtitleBoxRect& box, int& lineCount,
    std::array<uint64_t,16>& signature, int nearBarDistance)
{
    // Inspect every source frame. Spatial sampling bounds cost without an idle
    // frame cadence that could postpone subtitle onset. Full line search remains
    // active throughout a cue, so an initially missed companion can be recovered.
    m_diagnosticReason="no-backed-anchor";m_diagnosticComponentCount=0;
    m_diagnosticNearBarGap=-1;m_diagnosticNearBarGapInferred=false;
    m_nearBarEligibilityMeasured=false;m_nearBarEligible=false;
    m_currentBarAnchor = {};
    m_currentSourcePanel = {}; m_currentLinePanels = {}; m_currentCapturePanel = {}; m_currentLineCapturePanels = {};
    m_inkSnapshot.reset();
    m_currentAnchorSignature = {};
    m_currentLineBounds = {}; m_currentLineSignatures = {}; m_currentPanelTopEdges = {};
    const int step = SamplingStep(source.width, source.height);
    const int w = (source.width + step - 1) / step;
    const int h = (source.height + step - 1) / step;
    // Always inspect the two source rows touching the picture boundary. A
    // subtitle may put only one row of real ink in a bar; the regular stride
    // must not skip that evidence solely because of its sampling phase.
    const int top = (pictureTop + step - 1) / step;
    const int bottom = pictureBottom < source.height
        ? std::min(h-1,(pictureBottom + step - 1) / step) : h;
    // Keep the default picture-side search close to the bar. Scanning a full
    // quarter of a 4K frame admits large amounts of unrelated bright texture
    // before the subtitle row is reached and can exhaust the component cap.
    // Larger text is admitted by scale learned from actual bar-crossing glyphs
    // below, rather than by widening the whole-frame search for every cue.
    const int depth = std::max(24, h / 7)+(nearBarDistance+step-1)/step;
    int topEnd = pictureTop > 0 ? std::min(h,top+depth) : 0;
    int bottomStart = pictureBottom < source.height ? std::max(0,bottom-depth) : h;
    const int normalTopEnd=topEnd,normalBottomStart=bottomStart;
    const size_t area = static_cast<size_t>(w)*h;
    m_luma.resize(area); m_chroma.resize(area); m_mask.assign(area,0);
    const bool share=m_optimizationMode==2;
    if(share){m_rawLuma.resize(area);m_rawChroma.resize(area);}
    m_labels.assign(area,0);
    m_components.clear(); m_lines.clear(); m_workLimit = false;
    std::array<uint32_t,1024> barHistogram{};
    uint32_t barSamples=0;
    const bool planar = source.format == AnalysisLumaFormat::P010 ||
        source.format == AnalysisLumaFormat::P210;
    for(int y=0;y<h;++y)
    {
        if(y>=topEnd && y<bottomStart) continue;
        for(int x=0;x<w;++x)
        {
            const int sx=std::min(source.width-1,x*step);
            const int sy = pictureTop > 0 && y == top-1 ? pictureTop-1 :
                (pictureBottom < source.height && y == bottom ? pictureBottom :
                    std::min(source.height-1,y*step));
            uint16_t luma=0, u=512, v=512;
            if(share && m_sampledRows[y]) {
                const size_t i=static_cast<size_t>(y)*w+x;
                luma=m_rawLuma[i];u=m_rawChroma[i]&1023;v=m_rawChroma[i]>>16;
                ++m_sharedSampleCount;
            }
            else if(planar)
            {
                const auto* row=reinterpret_cast<const uint16_t*>(source.data+static_cast<size_t>(sy)*source.rowBytes);
                luma=row[sx]>>6;
                const int cy=source.format==AnalysisLumaFormat::P210?sy:sy/2;
                const auto* uv=reinterpret_cast<const uint16_t*>(source.data+
                    source.rowBytes*source.height+static_cast<size_t>(cy)*source.chromaRowBytes);
                u=uv[(sx/2)*2]>>6;v=uv[(sx/2)*2+1]>>6;
            }
            else
            {
                AnalysisLumaSample sample;
                if(!source.Sample(sx,sy,sample)) return false;
                luma=sample.luma;u=sample.chromaU;v=sample.chromaV;
            }
            m_luma[static_cast<size_t>(y)*w+x]=luma;
            m_chroma[static_cast<size_t>(y)*w+x]=static_cast<uint32_t>(u)|(static_cast<uint32_t>(v)<<16);
            if(share && !m_sampledRows[y]) {
                const size_t i=static_cast<size_t>(y)*w+x;
                m_rawLuma[i]=luma;m_rawChroma[i]=static_cast<uint32_t>(u)|(static_cast<uint32_t>(v)<<16);
            }
            if(sy<pictureTop || sy>=pictureBottom) { ++barHistogram[luma]; ++barSamples; }
        }
    }
    if(share)for(int y=0;y<h;++y)if(y<topEnd || y>=bottomStart)m_sampledRows[y]=1;
    if(!barSamples) return false;
    uint32_t cumulative=0; int bar=0;
    for(;bar<1023;++bar) { cumulative+=barHistogram[bar]; if(cumulative>=barSamples/2) break; }
    // Learn the text level from the bar; the same level applies on both sides
    // of the boundary. No SDR-white/PQ-white split at the picture edge.
    uint32_t brightCount=0;
    for(int v=std::min(1023,bar+80);v<1024;++v) brightCount+=barHistogram[v];
    auto inkHistogram=barHistogram;
    auto locallyDarkInk=[&](int x,int y) {
        if(x<2 || x>=w-2 || y<2 || y>=h-2 ||
            (y>=topEnd-2 && y<bottomStart+2))return false;
        const int pixel=y*w+x;
        if(m_luma[pixel]<bar+80)return false;
        int black=0;
        for(int offset:{-2,2,-2*w,2*w})black+=m_luma[pixel+offset]<=bar+24;
        return black>=2;
    };
    // Nominate a locally black-surrounded glyph row before learning its palette.
    // A few unrelated bright bar pixels, or another picture row outside the
    // eligibility corridor, must not set this caption's brightness or color.
    struct CalibrationBand { SubtitleBoxRect box;unsigned ink=0; };
    CalibrationBand calibration;
    std::vector<CalibrationBand> pendingBands;
    int calibrationGap=std::numeric_limits<int>::max();
    auto considerCalibration=[&](const CalibrationBand& pending) {
        if(!pending.ink)return;
        const auto& r=pending.box;const int height=Height(r),width=Width(r);
        const bool crosses=r.top*step<pictureTop || r.bottom*step>pictureBottom;
        const bool nearBoundary=nearBarDistance>0 &&
            ((pictureTop>0 && r.top*step<=pictureTop+nearBarDistance+height*step) ||
             (pictureBottom<source.height && r.bottom*step>=pictureBottom-nearBarDistance-height*step));
        const int gap=crosses?0:(std::min)(
            pictureTop>0?(std::max)(0,r.top*step-pictureTop):source.height,
            pictureBottom<source.height?(std::max)(0,pictureBottom-r.bottom*step):source.height);
        auto opaqueStrip=[&](int y) {
            if(y<0 || y>=h || (y>=topEnd && y<bottomStart))return false;
            unsigned black=0;
            for(int x=r.left;x<r.right;++x)black+=m_luma[y*w+x]<=bar+24;
            return width>=3 && black*100>=unsigned(width)*85;
        };
        if(height>=std::max(3,h/240) && height<=std::max(16,h/8) &&
            width*10>=height*8 && width<w*49/50 && pending.ink>=12 &&
            std::abs(r.left+r.right-w)*5<=w*2 && (crosses || nearBoundary) &&
            (opaqueStrip(r.top-2) || opaqueStrip(r.bottom+1)) &&
            (gap<calibrationGap || (gap==calibrationGap && pending.ink>calibration.ink))) {
            calibration=pending;calibrationGap=gap;
        }
    };
    // Keep separated same-row regions independent. Otherwise unrelated bright
    // scenery can erase a small caption's local evidence and force a scene-wide
    // fallback. Every palette must come from bar ink or a supported local row.
    const int calibrationWordGap=std::max(8,h/12);
    for(int y=0;y<h;++y) {
        for(size_t i=0;i<pendingBands.size();) {
            if(y-pendingBands[i].box.bottom>1) {
                considerCalibration(pendingBands[i]);
                pendingBands.erase(pendingBands.begin()+i);
            } else ++i;
        }
        CalibrationBand row;
        auto addRow=[&]() {
            if(!row.ink)return;
            size_t best=pendingBands.size();
            for(size_t i=0;i<pendingBands.size();++i) {
                const auto& band=pendingBands[i].box;
                if(row.box.left>band.right || row.box.right<band.left)continue;
                if(best==pendingBands.size())best=i;
                else {
                    pendingBands[best].box=Union(pendingBands[best].box,band);
                    pendingBands[best].ink+=pendingBands[i].ink;
                    pendingBands.erase(pendingBands.begin()+i--);
                }
            }
            if(best==pendingBands.size())pendingBands.push_back(row);
            else {
                pendingBands[best].box=Union(pendingBands[best].box,row.box);
                pendingBands[best].ink+=row.ink;
            }
            row={};
        };
        if(y<topEnd || y>=bottomStart)for(int x=2;x<w-2;++x)if(locallyDarkInk(x,y)) {
            if(row.ink && x-row.box.right>calibrationWordGap)addRow();
            const SubtitleBoxRect pixel{x,y,x+1,y+1};
            row.box=row.ink?Union(row.box,pixel):pixel;++row.ink;
        }
        addRow();
        if(pendingBands.size()>128) {m_workLimit=true;m_diagnosticReason="calibration-budget";return false;}
    }
    for(const auto& band:pendingBands)considerCalibration(band);
    const bool localPalette=calibration.ink!=0;
    auto calibrationPixel=[&](int x,int y) {
        return x>=calibration.box.left && x<calibration.box.right &&
            y>=calibration.box.top && y<calibration.box.bottom && locallyDarkInk(x,y);
    };
    if(localPalette) {
        inkHistogram={};brightCount=0;
        for(int y=calibration.box.top;y<calibration.box.bottom;++y)
            for(int x=calibration.box.left;x<calibration.box.right;++x)
                if(calibrationPixel(x,y)){++inkHistogram[m_luma[y*w+x]];++brightCount;}
    }
    // Do not learn "text" from scattered scene edges when no supported row
    // exists: that circular evidence admitted jacket folds beside an empty bar.
    if(!brightCount){m_diagnosticReason="no-bright-ink";return false;}
    cumulative=0;int ink=std::min(1023,bar+80);
    const uint32_t inkQuantile=(brightCount*3+3)/4;
    for(;ink<1023;++ink){cumulative+=inkHistogram[ink];if(cumulative>=inkQuantile)break;}
    const int threshold=bar+std::max(64,(ink-bar)*70/100);
    std::array<uint32_t,1024> histU{},histV{};uint32_t colors=0;
    for(int y=0;y<h;++y)if(y<top || y>=bottom || localPalette)
        for(int x=0;x<w;++x)
            if(m_luma[y*w+x]>=threshold && (!localPalette || calibrationPixel(x,y))) {
                const auto uv=m_chroma[y*w+x];++histU[uv&1023];++histV[uv>>16];++colors;
            }
    auto median=[colors](const std::array<uint32_t,1024>& histogram){
        uint32_t count=0;for(int i=0;i<1024;++i)if((count+=histogram[i])>colors/2)return i;return 512;};
    const int inkU=median(histU),inkV=median(histV);
    const int radius=std::max(3,h/180);
    for(int y=0;y<h;++y)
    {
        if(y>=topEnd && y<bottomStart) continue;
        for(int x=1;x<w-1;++x)
        {
            const int i=y*w+x, v=m_luma[i];
            if(v<threshold) continue;
            const auto uv=m_chroma[i];
            if(std::abs(static_cast<int>(uv&1023)-inkU)>80 ||
                std::abs(static_cast<int>(uv>>16)-inkV)>80) continue;
            if(y<top || y>=bottom) { m_mask[i]=1; continue; }
            // Picture-side strokes need local contrast; broad bright scenery
            // must not become one giant subtitle component.
            const int lo=std::max(0,x-radius), hi=std::min(w-1,x+radius);
            const int above=std::max(y<topEnd?0:bottomStart,y-radius);
            const int below=std::min(y<topEnd?topEnd-1:h-1,y+radius);
            int dark=v;
            // Inspect the entire short ray, not only its endpoint: on bright
            // scenery the endpoint can be beyond a subtitle's dark outline.
            for(int xx=lo;xx<=hi;++xx) dark=std::min(dark,static_cast<int>(m_luma[y*w+xx]));
            for(int yy=above;yy<=below;++yy) dark=std::min(dark,static_cast<int>(m_luma[yy*w+x]));
            if(v-dark>=std::max(48,(ink-bar)/5)) m_mask[i]=1;
        }
    }
    m_inkSnapshot=std::make_shared<SubtitleInkSnapshot>();
    auto& evidence=*m_inkSnapshot;
    evidence.width=w;evidence.height=h;evidence.step=step;
    evidence.sourceRows.resize(h);
    for(int y=0;y<h;++y) evidence.sourceRows[y]=pictureTop>0 && y==top-1?pictureTop-1:
        (pictureBottom<source.height && y==bottom?pictureBottom:std::min(source.height-1,y*step));
    evidence.rawInk.assign((area+63)/64,0);
    // A failed grouping still publishes raw ink for cue revalidation. Keep the
    // ownership plane allocated for every published snapshot, including misses.
    evidence.ownedInk.assign(evidence.rawInk.size(),0);
    evidence.blackBacking.assign(evidence.rawInk.size(),0);
    const int trackingBlackLimit=bar+std::max(12,(ink-bar)/48);
    evidence.paletteValid=true; evidence.inkFloor=threshold;
    evidence.inkU=inkU; evidence.inkV=inkV; evidence.blackLimit=trackingBlackLimit;
    for(int y=0;y<h;++y) {
        if(y>=topEnd && y<bottomStart)continue; // Unread raster is not black evidence.
        for(int x=0;x<w;++x) {
            const size_t pixel=size_t(y)*w+x;
            if(m_luma[pixel]<=trackingBlackLimit)
                evidence.blackBacking[pixel/64]|=uint64_t{1}<<(pixel%64);
        }
    }
    for(size_t i=0;i<area;++i) if(m_mask[i]) evidence.rawInk[i/64]|=uint64_t{1}<<(i%64);
    evidence.rawEvidenceComplete=true;
    // Components preserve short words and punctuation until line grouping.
    // A hard component count caps noisy-frame work and yields no diagnostic.
    constexpr size_t MaxComponents=768;
    // Labels identify sampled pixels for the whole pass, not vector slots.
    // Reconnecting a detached dot erases its component but leaves surviving
    // labels intact. Later card/weak searches must never reuse those labels.
    // Count allocated identities so the fixed ownership tables stay bounded.
    uint16_t nextComponentLabel=0;
    const int minimumHeight=std::max(3,h/240), maximumHeight=std::max(16,h/12);
    const int maximumBarGlyphHeight=std::max(maximumHeight,h/8);
    // Word gaps can vary substantially between subtitle renderers and scripts.
    // Use a wider, scale-relative baseline corridor when joining components;
    // line width, baseline agreement, learned glyph height, and the independent
    // bar-anchor requirement still constrain which groups can form a cue.
    constexpr int SameLineGapGlyphHeights = 4;
    // Arabic joining can produce a single connected word much wider than a
    // Latin glyph. Bound that fast path to the detected stroke height and the
    // existing centered/bar-anchor rules instead of clipping it at 8x.
    constexpr int JoinedWordMaxGlyphHeights = 14;
    const int componentCardLimit=bar+(std::max)(24,(ink-bar)/12);
    auto hasAnyOpaqueSurround=[&](const Line& component) {
        const auto& r=component.box;
        if(r.top<top || r.bottom>bottom)return true;
        auto black=[&](int x,int y) {
            if(x<0 || x>=w || y<top || y>=bottom)return false;
            AnalysisLumaSample p;
            return source.Sample((std::min)(source.width-1,x*step),evidence.sourceRows[y],p) &&
                p.luma<=componentCardLimit;
        };
        for(int x=r.left;x<r.right;++x)
            if(black(x,r.top-2) || black(x,r.bottom+1))return true;
        for(int y=r.top;y<r.bottom;++y)
            if(black(r.left-2,y) || black(r.right+1,y))return true;
        return false;
    };
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        if(y>=topEnd && y<bottomStart) break;
        const int origin=y*w+x;
        if(m_mask[origin]!=1) continue;
        Line component{{x,y,x+1,y+1},0,1};
        m_flood.clear(); m_flood.push_back(origin); m_mask[origin]=2;
        for(size_t q=0;q<m_flood.size();++q)
        {
            const int i=m_flood[q], cy=i/w, cx=i-cy*w;
            ++component.ink;
            if(m_luma[i]>component.peakLuma) {
                component.peakLuma=m_luma[i];component.peakPixel=i;
            }
            component.topBarInk += cy < top;
            component.bottomBarInk += cy >= bottom;
            component.box=Union(component.box,{cx,cy,cx+1,cy+1});
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            {
                const int nx=cx+dx, ny=cy+dy;
                if(nx<0||nx>=w||ny<0||ny>=h) continue;
                const int n=ny*w+nx;
                if(m_mask[n]==1) { m_mask[n]=2; m_flood.push_back(n); }
            }
        }
        const int cw=Width(component.box), ch=Height(component.box);
        component.actualBarInk=component.topBarInk>0 || component.bottomBarInk>0;
        // Broad nomination only: the measured opaque panel edge makes the
        // final distance decision. Glyphs may be inset within that panel.
        if(nearBarDistance>0) {
            const int reach=nearBarDistance+ch*step;
            if(pictureTop>0 && component.box.top*step<=pictureTop+reach)component.topBarInk++;
            if(pictureBottom<source.height && component.box.bottom*step>=pictureBottom-reach)component.bottomBarInk++;
        }
        const int componentHeightLimit=(component.topBarInk || component.bottomBarInk)
            ? maximumBarGlyphHeight : maximumHeight;
        const bool punctuation=component.ink>=1 && ch<=2 && cw<=2;
        // Preserve thin horizontal symbols until the learned text scale can
        // decide attachment. A UHD dash can be only one sampled row high but
        // wider than the old short-component gate; it cannot seed a text line.
        const bool thinMark=component.ink>=3 && ch<=minimumHeight &&
            cw>=ch*2 && cw<=componentHeightLimit*2;
        // A connected Arabic word can be several glyph-heights wide. Keep
        // scale tied to its vertical strokes, while allowing the line-level
        // checks below to reject broad scene regions. The old fixed
        // maximumHeight*3 ceiling cut valid joined words before they could be
        // grouped with the rest of their RTL line.
        const int maximumComponentWidth = (std::max)(componentHeightLimit*3,ch*JoinedWordMaxGlyphHeights);
        if((punctuation || thinMark || (component.ink>=3 && ch<=componentHeightLimit &&
            cw<=maximumComponentWidth && cw<w*49/50 &&
            (ch>=minimumHeight || (cw<=minimumHeight*2 && ch>=2)))) && hasAnyOpaqueSurround(component))
        {
            m_components.push_back(component);
            m_components.back().label=++nextComponentLabel;
            for(int pixel : m_flood) {
                m_mask[pixel] = 3;
                m_labels[pixel]=m_components.back().label;
            }
        }
        if(nextComponentLabel>=MaxComponents) { m_workLimit=true;m_diagnosticReason="component-budget";m_diagnosticComponentCount=nextComponentLabel;return false; }
    }
    // Reconnect a detached dot to the stroke immediately above it (for example
    // '?' or '!'). Its baseline belongs to the complete glyph; using only the
    // upper component would discard it as an unrelated high picture stroke.
    const int dotLimit=std::max(2,h/180);
    std::array<bool,MaxComponents+1> compoundGlyph{};
    for(size_t dot=0;dot<m_components.size();) {
        const auto dotComponent=m_components[dot];
        if(Width(dotComponent.box)>dotLimit || Height(dotComponent.box)>dotLimit) {++dot;continue;}
        size_t head=m_components.size();int bestGap=maximumBarGlyphHeight;
        for(size_t i=0;i<m_components.size();++i) {
            const auto& c=m_components[i];
            const int overlap=std::min(c.box.right,dotComponent.box.right)-std::max(c.box.left,dotComponent.box.left);
            const int gap=dotComponent.box.top-c.box.bottom;
            if(i!=dot && !compoundGlyph[c.label] &&
                Height(c.box)>=std::max(minimumHeight,Height(dotComponent.box)*3) &&
                Width(c.box)<=Height(c.box)*2 && overlap>0 && gap>=0 &&
                gap<=std::max(2,Height(c.box)/2) && gap<bestGap &&
                Height(Union(c.box,dotComponent.box))<=maximumBarGlyphHeight) {head=i;bestGap=gap;}
        }
        if(head==m_components.size()) {++dot;continue;}
        auto& c=m_components[head];c.box=Union(c.box,dotComponent.box);c.ink+=dotComponent.ink;
        compoundGlyph[c.label]=true;
        c.topBarInk+=dotComponent.topBarInk;c.bottomBarInk+=dotComponent.bottomBarInk;
        c.actualBarInk=c.actualBarInk || dotComponent.actualBarInk;
        if(dotComponent.peakLuma>c.peakLuma) {
            c.peakLuma=dotComponent.peakLuma;c.peakPixel=dotComponent.peakPixel;
        }
        for(int y=dotComponent.box.top;y<dotComponent.box.bottom;++y) for(int x=dotComponent.box.left;x<dotComponent.box.right;++x)
            if(m_labels[y*w+x]==dotComponent.label) m_labels[y*w+x]=c.label;
        m_components.erase(m_components.begin()+dot);
    }
    // The low mask threshold retains antialias fringes. A picture-side letter
    // still needs a core near the level learned from bar text. Before rejecting
    // a weak coarse sample, inspect its small native neighborhood: a thin white
    // stroke may lie between stride positions. This is bounded local sampling,
    // not a second full-resolution image analysis.
    const int coreThreshold=ink;
    auto coreColor=[&](int u,int v) {
        return std::abs(u-inkU)<=32 && std::abs(v-inkV)<=32;
    };
    for(auto& c:m_components) {
        const auto peakColor=m_chroma[c.peakPixel];
        // Near-bar counters include search nominations, not measured bar ink.
        // They must not exempt picture texture from the bright-core proof.
        if(c.actualBarInk || (c.peakLuma>=coreThreshold &&
            coreColor(peakColor&1023,peakColor>>16))) continue;
        c.hasTextCore=false;
        const int gy=c.peakPixel/w,gx=c.peakPixel%w;
        const int sx=gx*step;
        const int sy=pictureTop>0 && gy==top-1 ? pictureTop-1 :
            (pictureBottom<source.height && gy==bottom ? pictureBottom : gy*step);
        for(int y=std::max(0,sy-step+1);y<=std::min(source.height-1,sy+step-1) && !c.hasTextCore;++y)
            for(int x=std::max(0,sx-step+1);x<=std::min(source.width-1,sx+step-1);++x) {
                AnalysisLumaSample value;
                if(source.Sample(x,y,value) && value.luma>=coreThreshold &&
                    coreColor(value.chromaU,value.chromaV)) {
                    c.hasTextCore=true;break;
                }
            }
    }
    // Picture-only nominations need a bounded bright stroke, not a one-sided
    // brightness edge against the letterbox bar. Both opposing samples must
    // remain inside picture content; physical bar ink retains its own proof.
    for(auto& c:m_components) {
        if(c.actualBarInk || !c.hasTextCore || Height(c.box)<minimumHeight)continue;
        const int reach=(std::max)(radius,((std::min)(Width(c.box),Height(c.box))+1)/2+1);
        bool boundedCore=false;
        auto darkRay=[&](int x,int y,int dx,int dy) {
            for(int distance=1;distance<=reach;++distance) {
                const int xx=x+dx*distance,yy=y+dy*distance;
                if(xx<0 || xx>=w || yy<0 || yy>=h || (yy>=topEnd && yy<bottomStart) ||
                    evidence.sourceRows[yy]<pictureTop || evidence.sourceRows[yy]>=pictureBottom)break;
                if(m_luma[yy*w+xx]<=componentCardLimit)return true;
            }
            return false;
        };
        for(int y=c.box.top;y<c.box.bottom && !boundedCore;++y)
            for(int x=c.box.left;x<c.box.right;++x) {
                const int pixel=y*w+x;
                if(m_labels[pixel]!=c.label ||
                    m_luma[pixel]<(c.peakLuma>=coreThreshold?coreThreshold:threshold))continue;
                const auto color=m_chroma[pixel];
                if(!coreColor(color&1023,color>>16))continue;
                if((darkRay(x,y,-1,0) && darkRay(x,y,1,0)) ||
                    (darkRay(x,y,0,-1) && darkRay(x,y,0,1))) {boundedCore=true;break;}
            }
        if(!boundedCore)c.hasTextCore=false;
    }
    // Learn character scale from components that actually enter a bar. Tiny
    // picture highlights must not seed a line and chain into real lettering.
    // Keep them for a bounded punctuation pass after substantial glyphs group.
    // Estimate scale from a plausible centered bar line, rather than every bar
    // component: a small corner title must not set a short subtitle's size.
    // Group across the full width so long captions retain their outer letters.
    struct ScaleLine { Line line; std::vector<int> heights; };
    std::vector<const Line*> barComponents;
    for(const auto& c:m_components)
        // Proximity only nominates a card. It is not physical bar ink and
        // must not teach a global font scale from unrelated top/bottom scenery.
        if(c.actualBarInk && Height(c.box)>=minimumHeight &&
            c.ink>=6 && Width(c.box)<=Height(c.box)*JoinedWordMaxGlyphHeights)
            barComponents.push_back(&c);
    std::sort(barComponents.begin(),barComponents.end(),[](const Line* a,const Line* b) {
        return a->box.left<b->box.left;
    });
    std::vector<ScaleLine> scaleLines;
    for(const auto* component:barComponents) {
        const auto& c=*component;
        size_t best=scaleLines.size();int bestGap=w;
        for(size_t i=0;i<scaleLines.size();++i) {
            const auto& line=scaleLines[i].line;
            const int overlap=std::min(line.box.bottom,c.box.bottom)-std::max(line.box.top,c.box.top);
            const int gap=std::max(0,c.box.left-line.box.right);
            const int height=std::max(Height(line.box),Height(c.box));
            if(overlap>=std::max(1,std::min(Height(line.box),Height(c.box))/3) &&
                std::abs(line.box.bottom-c.box.bottom)<=height/3+1 &&
                Height(Union(line.box,c.box))<=height*3/2+2 &&
                gap<=height*SameLineGapGlyphHeights && gap<bestGap)
                {best=i;bestGap=gap;}
        }
        if(best==scaleLines.size()) scaleLines.push_back({c,{Height(c.box)}});
        else {
            auto& group=scaleLines[best];
            group.line.box=Union(group.line.box,c.box);group.line.ink+=c.ink;
            group.line.topBarInk+=c.topBarInk;
            group.line.bottomBarInk+=c.bottomBarInk;
            ++group.line.components;group.heights.push_back(Height(c.box));
        }
    }
    std::vector<int> barHeights;int scaleInk=0;
    SubtitleBoxRect scaleAnchorBox{};bool scaleAnchorTop=false,scaleAnchorBottom=false;
    for(const auto& group:scaleLines) {
        const auto& line=group.line;
        if((line.components>=2 ||
            (line.components==1 && Height(line.box)>=(std::max)(minimumHeight*2,6) &&
                Width(line.box)>=Height(line.box)*2 &&
                line.ink>=12)) && Width(line.box)<w*49/50 &&
            std::abs(line.box.left+line.box.right-w)*5<=w*2 && line.ink>scaleInk) {
            barHeights=group.heights;scaleInk=line.ink;scaleAnchorBox=line.box;
            scaleAnchorTop=line.topBarInk!=0;scaleAnchorBottom=line.bottomBarInk!=0;
        }
    }
    int glyphHeight=0;
    if(!barHeights.empty()) {
        const auto index=barHeights.size()*3/4;
        std::nth_element(barHeights.begin(),barHeights.begin()+index,barHeights.end());
        glyphHeight=barHeights[index];
    }
    // A subtitle card can put its second line well above the bar, especially
    // with large fonts. Extend the search only when dark backing connected to
    // the already validated bar anchor is visible in the anchor's corridor.
    // Do not trace the maximal dark run on the row: dark scene detail can join
    // the black bar all the way to the frame edges and hide a real card.
    std::vector<int> panelLeft(h,-1),panelRight(h,-1);
    int panelRows=0;
    if(glyphHeight>0 && scaleAnchorBox.Valid() && scaleAnchorTop!=scaleAnchorBottom)
    {
        const int anchorWidth=Width(scaleAnchorBox);
        const int minPanelWidth=(std::max)(glyphHeight*4,
            (std::min)(w/2,anchorWidth)*3/4);
        const int panelDarkLimit=bar+(std::max)(24,(ink-bar)/10);
        const int maximumPanelDepth=(std::min)(h/3,
            (std::max)(depth,glyphHeight*6));
        const int centerX=(scaleAnchorBox.left+scaleAnchorBox.right)/2;
        // The bar-crossing line supplies a minimum expected panel width. A
        // wider preceding line may enlarge it substantially, so permit up to
        // four times that width (or most of the raster for exceptionally wide
        // captions) while preventing a connected scene shadow from consuming
        // the entire row.
        const int maximumPanelWidth=(std::min)(std::max(1,w-2),
            (std::max)(anchorWidth*4,glyphHeight*24));
        const int maximumPanelHalfWidth=maximumPanelWidth/2;
        std::vector<uint8_t> panelPixels(w);
        std::vector<uint8_t> panelInkPixels(w);
        std::vector<uint8_t> panelDarkPixels(w);
        int traceMinRow=h,traceMaxRow=-1,weakPanelRows=0;
        int activePanelLeft=-1,activePanelRight=-1;
        auto panelExtent=[&](int row,int& left,int& right) {
            const int sy=(std::min)(source.height-1,row*step);
            for(int x=0;x<w;++x) {
                AnalysisLumaSample sample;
                const int sx=(std::min)(source.width-1,x*step);
                if(!source.Sample(sx,sy,sample)) {
                    panelPixels[x]=0;panelInkPixels[x]=0;panelDarkPixels[x]=0;continue;
                }
                const bool dark=sample.luma<=panelDarkLimit;
                const bool subtitleInk=sample.luma>=coreThreshold &&
                    std::abs(static_cast<int>(sample.chromaU)-inkU)<=80 &&
                    std::abs(static_cast<int>(sample.chromaV)-inkV)<=80;
                panelPixels[x]=dark || subtitleInk;
                panelInkPixels[x]=subtitleInk;
                panelDarkPixels[x]=dark;
            }
            if(panelRows>0) {
                left=activePanelLeft;right=activePanelRight;
            } else {
                int seedX=centerX;
                if(seedX<0 || seedX>=w || !panelPixels[seedX]) {
                    const int search=(std::max)(2,glyphHeight/2);
                    int nearestDistance=search+1;
                    for(int x=(std::max)(0,centerX-search);
                        x<=(std::min)(w-1,centerX+search);++x)
                        if(panelPixels[x] && std::abs(x-centerX)<nearestDistance) {
                            seedX=x;nearestDistance=std::abs(x-centerX);
                        }
                    if(nearestDistance>search) return false;
                }
                left=right=seedX;
                const int allowedLeft=(std::max)(0,centerX-maximumPanelHalfWidth);
                const int allowedRight=(std::min)(w-1,centerX+maximumPanelHalfWidth);
                while(left>allowedLeft && panelPixels[left-1]) --left;
                while(right<allowedRight && panelPixels[right+1]) ++right;
                activePanelLeft=left;activePanelRight=right;
            }
            const int width=right-left+1;
            if(width<minPanelWidth) return false;
            int nonInkSamples=0,darkNonInkSamples=0;
            for(int x=left;x<=right;++x) if(!panelInkPixels[x]) {
                ++nonInkSamples;
                darkNonInkSamples+=panelDarkPixels[x]!=0;
            }
            // Matching bright glyphs can occlude much of the black panel on a
            // text row. Let a short, bounded run bridge those rows only after
            // a dark row has anchored the trace. Count backing only among
            // non-glyph samples; a white letter crossing the center cannot
            // split the row, and an edge-connected dark scene cannot widen the
            // fixed corridor to the frame edges.
            const bool darkRow=nonInkSamples>=width/3 &&
                darkNonInkSamples*2>=nonInkSamples;
            const bool glyphCoveredRow=panelRows>0 && nonInkSamples>0 &&
                darkNonInkSamples*6>=nonInkSamples && weakPanelRows<glyphHeight*2;
            if(!darkRow && !glyphCoveredRow) return false;
            if(darkRow) {
                weakPanelRows=0;
            }
            else {
                ++weakPanelRows;
            }
            return true;
        };
        if(scaleAnchorBottom) {
            int boundaryMisses=0;
            for(int y=bottom-1,distance=0;y>=0 && distance<maximumPanelDepth;--y,++distance) {
                int left=0,right=0;
                if(!panelExtent(y,left,right)) {
                    if(!panelRows && boundaryMisses<2) { ++boundaryMisses;continue; }
                    break;
                }
                boundaryMisses=0;
                panelLeft[y]=left;panelRight[y]=right;++panelRows;
                traceMinRow=(std::min)(traceMinRow,y);
                traceMaxRow=(std::max)(traceMaxRow,y);
            }
            if(panelRows<std::max(3,glyphHeight/3)) panelRows=0;
            if(panelRows) {
                bottomStart=(std::min)(bottomStart,traceMinRow);
            }
        } else {
            int boundaryMisses=0;
            for(int y=top,distance=0;y<h && distance<maximumPanelDepth;++y,++distance) {
                int left=0,right=0;
                if(!panelExtent(y,left,right)) {
                    if(!panelRows && boundaryMisses<2) { ++boundaryMisses;continue; }
                    break;
                }
                boundaryMisses=0;
                panelLeft[y]=left;panelRight[y]=right;++panelRows;
                traceMinRow=(std::min)(traceMinRow,y);
                traceMaxRow=(std::max)(traceMaxRow,y);
            }
            if(panelRows<std::max(3,glyphHeight/3)) panelRows=0;
            if(panelRows) topEnd=(std::max)(topEnd,traceMaxRow+1);
        }
        if(!panelRows && traceMaxRow>=traceMinRow)
            for(int y=traceMinRow;y<=traceMaxRow;++y)
                panelLeft[y]=panelRight[y]=-1;
        if(panelRows) {
            const int learnedHeightLimit=(std::max)(maximumHeight,glyphHeight*3/2+1);
            for(int y=0;y<h;++y) {
                if(panelLeft[y]<0) continue;
                const int sy=(std::min)(source.height-1,y*step);
                for(int x=panelLeft[y];x<=panelRight[y];++x) {
                    const int i=y*w+x;
                    if(m_mask[i]) continue;
                    const int sx=(std::min)(source.width-1,x*step);
                    AnalysisLumaSample sample;
                    if(!source.Sample(sx,sy,sample)) continue;
                    m_luma[i]=sample.luma;
                    if(sy==evidence.sourceRows[y] && sample.luma<=trackingBlackLimit)
                        evidence.blackBacking[size_t(i)/64]|=uint64_t{1}<<(size_t(i)%64);
                    m_chroma[i]=static_cast<uint32_t>(sample.chromaU)|
                        (static_cast<uint32_t>(sample.chromaV)<<16);
                    if(sample.luma<threshold ||
                        std::abs(static_cast<int>(sample.chromaU)-inkU)>80 ||
                        std::abs(static_cast<int>(sample.chromaV)-inkV)>80) continue;
                    m_mask[i]=1;
                }
            }
            // Flood only the newly admitted panel rows. Their color and scale
            // already match the bar anchor, so unrelated scene texture cannot
            // consume the earlier global component budget.
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                if(y>=topEnd && y<bottomStart) break;
                const int origin=y*w+x;
                if(m_mask[origin]!=1) continue;
                Line component{{x,y,x+1,y+1},0,1};
                m_flood.clear();m_flood.push_back(origin);m_mask[origin]=2;
                for(size_t q=0;q<m_flood.size();++q) {
                    const int pixel=m_flood[q],cy=pixel/w,cx=pixel-cy*w;
                    ++component.ink;
                    if(m_luma[pixel]>component.peakLuma) {
                        component.peakLuma=m_luma[pixel];component.peakPixel=pixel;
                    }
                    component.box=Union(component.box,{cx,cy,cx+1,cy+1});
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                        const int nx=cx+dx,ny=cy+dy;
                        if(nx<0||nx>=w||ny<0||ny>=h) continue;
                        const int next=ny*w+nx;
                        if(m_mask[next]==1) {m_mask[next]=2;m_flood.push_back(next);}
                    }
                }
                const int cw=Width(component.box),ch=Height(component.box);
                const auto uv=m_chroma[component.peakPixel];
                component.hasTextCore=component.peakLuma>=coreThreshold &&
                    coreColor(uv&1023,uv>>16);
                const int maximumComponentWidth=(std::max)(learnedHeightLimit*3,
                    ch*JoinedWordMaxGlyphHeights);
                const bool punctuation=component.ink>=1 && ch<=2 && cw<=2;
                const bool thinMark=component.ink>=3 && ch<=minimumHeight &&
                    cw>=ch*2 && cw<=learnedHeightLimit*2;
                if(punctuation || thinMark || (component.ink>=3 && component.hasTextCore &&
                    ch>=minimumHeight && ch<=learnedHeightLimit &&
                    cw<=maximumComponentWidth && cw<w*49/50)) {
                    component.label=++nextComponentLabel;
                    for(int pixel:m_flood) {
                        m_mask[pixel]=3;
                        const size_t bit=static_cast<size_t>(pixel);
                        m_labels[bit]=component.label;
                        evidence.rawInk[bit/64]|=uint64_t{1}<<(bit%64);
                    }
                    m_components.push_back(component);
                    if(nextComponentLabel>=MaxComponents) {m_workLimit=true;m_diagnosticReason="component-budget";m_diagnosticComponentCount=nextComponentLabel;return false;}
                } else for(int pixel:m_flood) m_mask[pixel]=2;
            }
        }
    }
    // Bright scene pixels can erase the outline contrast of white subtitle
    // strokes during the strong global pass. Once a centered, real bar line
    // establishes scale and color, make a second bounded pass only on the
    // picture side immediately adjacent to that bar edge. This recovers weak
    // same-cue strokes without relaxing the mask across the rest of the frame.
    if(glyphHeight>0 && scaleAnchorBox.Valid() && scaleAnchorTop!=scaleAnchorBottom)
    {
        const int corridor=std::min((std::max)(24,glyphHeight*3*step),
            (std::max)(24,depth*step));
        auto inPictureCorridor=[&](int sourceY) {
            if(scaleAnchorBottom)
                return sourceY>=pictureBottom-corridor && sourceY<pictureBottom;
            return sourceY>=pictureTop && sourceY<pictureTop+corridor;
        };
        const int weakContrast=std::max(24,(ink-bar)/12);
        for(int y=0;y<h;++y)
        {
            const int sy=evidence.sourceRows[y];
            if(!inPictureCorridor(sy)) continue;
            const int bandTop=y<normalTopEnd?0:normalBottomStart;
            const int bandBottom=y<normalTopEnd?normalTopEnd-1:h-1;
            for(int x=1;x<w-1;++x)
            {
                const int i=y*w+x,v=m_luma[i];
                if(m_mask[i] || v<threshold) continue;
                const auto uv=m_chroma[i];
                if(std::abs(static_cast<int>(uv&1023)-inkU)>80 ||
                    std::abs(static_cast<int>(uv>>16)-inkV)>80) continue;
                int dark=v;
                for(int xx=std::max(0,x-radius);xx<=std::min(w-1,x+radius);++xx)
                    dark=std::min(dark,static_cast<int>(m_luma[y*w+xx]));
                for(int yy=std::max(bandTop,y-radius);yy<=std::min(bandBottom,y+radius);++yy)
                    dark=std::min(dark,static_cast<int>(m_luma[yy*w+x]));
                if(v-dark>=weakContrast) m_mask[i]=1;
            }
        }
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
        {
            const int origin=y*w+x;
            if(m_mask[origin]!=1) continue;
            Line component{{x,y,x+1,y+1},0,1};
            m_flood.clear();m_flood.push_back(origin);m_mask[origin]=2;
            for(size_t q=0;q<m_flood.size();++q)
            {
                const int i=m_flood[q],cy=i/w,cx=i-cy*w;
                ++component.ink;
                if(m_luma[i]>component.peakLuma) {
                    component.peakLuma=m_luma[i];component.peakPixel=i;
                }
                component.box=Union(component.box,{cx,cy,cx+1,cy+1});
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                {
                    const int nx=cx+dx,ny=cy+dy;
                    if(nx<0||nx>=w||ny<0||ny>=h) continue;
                    const int n=ny*w+nx;
                    if(m_mask[n]==1) {m_mask[n]=2;m_flood.push_back(n);}
                }
            }
            const int cw=Width(component.box),ch=Height(component.box);
            const auto uv=m_chroma[component.peakPixel];
            component.hasTextCore=component.peakLuma>=coreThreshold &&
                coreColor(uv&1023,uv>>16);
            const int learnedHeightLimit=glyphHeight
                ? (std::max)(maximumHeight,glyphHeight*3/2+1) : maximumHeight;
            const int maximumComponentWidth=(std::max)(learnedHeightLimit*3,ch*JoinedWordMaxGlyphHeights);
            // Tiny same-color dots cannot seed a line, but must survive until
            // the anchored punctuation pass can assign them to actual lettering.
            const bool tinyMark=component.ink>=1 && ch<=2 && cw<=2;
            if(component.hasTextCore && (tinyMark || (component.ink>=3 && ch>=minimumHeight &&
                ch<=learnedHeightLimit && cw<=maximumComponentWidth && cw<w*49/50)))
            {
                component.label=++nextComponentLabel;
                for(int pixel:m_flood) m_mask[pixel]=3;
                for(int pixel:m_flood) {
                    const size_t bit=static_cast<size_t>(pixel);
                    evidence.rawInk[bit/64]|=uint64_t{1}<<(bit%64);
                    m_labels[bit]=component.label;
                }
                m_components.push_back(component);
                if(nextComponentLabel>=MaxComponents) {m_workLimit=true;m_diagnosticReason="component-budget";m_diagnosticComponentCount=nextComponentLabel;return false;}
            }
            else for(int pixel:m_flood) m_mask[pixel]=0;
        }
    }
    // Establish local card support before grouping or signatures. A small
    // bright scene stroke beside a real card must not borrow the black area
    // under the rest of a long subtitle row. Sample outside its envelope so
    // glyph holes and dark outlines cannot masquerade as a backing panel.
    auto locallyBacked=[&](const Line& c) {
        const auto& r=c.box;
        if(r.bottom<=top || r.top>=bottom) return true;
        unsigned samples[4]{},black[4]{};
        auto sample=[&](int side,int x,int y) {
            if(x<0 || x>=w || y<top || y>=bottom) return;
            AnalysisLumaSample pixel;
            if(!source.Sample((std::min)(source.width-1,x*step),evidence.sourceRows[y],pixel)) return;
            ++samples[side];black[side]+=pixel.luma<=componentCardLimit;
        };
        for(int x=r.left;x<r.right;++x) {
            sample(0,x,r.top-2);sample(1,x,r.bottom+1);
        }
        for(int y=(std::max)(top,r.top);y<(std::min)(bottom,r.bottom);++y) {
            sample(2,r.left-2,y);sample(3,r.right+1,y);
        }
        unsigned total=0,dark=0;int supported=0;
        for(int side=0;side<4;++side) {
            total+=samples[side];dark+=black[side];
            if(samples[side] && black[side]*100>=samples[side]*65) ++supported;
        }
        // Rounded card corners can expose one side. Neighboring glyphs may
        // interrupt another; two distributed opaque sides establish support.
        // Near-bar nomination increments topBarInk/bottomBarInk even when
        // every stroke is in the picture. Only physical bar pixels justify
        // the reduced one-strip proof used by genuinely crossing lettering.
        const bool crossing=c.actualBarInk;
        const bool horizontalCardStrip=(samples[0]>=2 && black[0]*100>=samples[0]*85) ||
            (samples[1]>=2 && black[1]*100>=samples[1]*85);
        // A crossing glyph can expose just one or two picture sample rows.
        // Its vertical strips may hit adjacent glyphs and its other horizontal
        // strip is in the bar. The available broad opaque strip is sufficient
        // locally; the complete line must still pass distributed card proof.
        // A narrow glyph beside another letter has long vertical strips but
        // short horizontal strips. Do not let the occluded neighboring side
        // outweigh an independently black horizontal strip and outer side.
        return (crossing && horizontalCardStrip) ||
            (supported>=2 && total>=3 &&
                (dark*100>=total*60 || horizontalCardStrip));
    };
    for(auto component=m_components.begin();component!=m_components.end();) {
        if(locallyBacked(*component)) {++component;continue;}
        for(int y=component->box.top;y<component->box.bottom;++y)
            for(int x=component->box.left;x<component->box.right;++x) {
                const size_t pixel=static_cast<size_t>(y)*w+x;
                if(m_labels[pixel]!=component->label) continue;
                m_labels[pixel]=0;m_mask[pixel]=2;
            }
        component=m_components.erase(component);
    }
    m_diagnosticComponentCount=nextComponentLabel;
    auto substantial=[&](const Line& c) {
        return c.hasTextCore && (!glyphHeight ? (c.ink>=3 && Height(c.box)>=minimumHeight) :
            (Height(c.box)>=(glyphHeight+1)/2 &&
            Height(c.box)<=glyphHeight*3/2+1 &&
            Width(c.box)<=glyphHeight*JoinedWordMaxGlyphHeights));
    };
    std::sort(m_components.begin(),m_components.end(),[](const Line&a,const Line&b){
        return a.box.left<b.box.left; });
    std::array<uint16_t,MaxComponents+1> componentOwners{};
    for(const Line& c:m_components)
    {
        if(!substantial(c)) continue;
        size_t best=m_lines.size(); int bestGap=w;
        for(size_t i=0;i<m_lines.size();++i)
        {
            const Line& l=m_lines[i];
            const int overlap=std::min(l.box.bottom,c.box.bottom)-std::max(l.box.top,c.box.top);
            const int gap=std::max(0,c.box.left-l.box.right);
            const int height=std::max(Height(l.box),Height(c.box));
            if(overlap>=std::max(1,std::min(Height(l.box),Height(c.box))/3) &&
                (!glyphHeight || std::abs(l.box.bottom-c.box.bottom)<=std::max(1,height/3)) &&
                Height(Union(l.box,c.box))<=height*3/2+2 &&
                gap<=(glyphHeight?glyphHeight*SameLineGapGlyphHeights:
                    height*SameLineGapGlyphHeights) && gap<bestGap)
                { best=i; bestGap=gap; }
        }
        if(best==m_lines.size()) m_lines.push_back(c);
        else {
            auto& line = m_lines[best];
            line.box=Union(line.box,c.box); line.ink+=c.ink; ++line.components;
            line.topBarInk+=c.topBarInk; line.bottomBarInk+=c.bottomBarInk;
        }
        componentOwners[c.label]=static_cast<uint16_t>(best+1);
    }
    // Sampling can disconnect the first stroke of a letter from its baseline.
    // That stroke may seed another group before later letters reveal that both
    // groups occupy the same text row. Reconcile completed groups, preserving
    // component ownership, before punctuation and temporal signatures.
    for(size_t i=0;i<m_lines.size();++i) for(size_t j=i+1;j<m_lines.size();) {
        const auto& a=m_lines[i];const auto& b=m_lines[j];
        const int height=std::max(Height(a.box),Height(b.box));
        const int overlap=std::min(a.box.bottom,b.box.bottom)-std::max(a.box.top,b.box.top);
        const int gap=std::max({0,a.box.left-b.box.right,b.box.left-a.box.right});
        if(overlap>=std::max(1,std::min(Height(a.box),Height(b.box))/2) &&
            std::abs(a.box.bottom-b.box.bottom)<=std::max(1,height/3) &&
            Height(Union(a.box,b.box))<=height*3/2+2 &&
            gap<=(glyphHeight?glyphHeight*SameLineGapGlyphHeights:
                height*SameLineGapGlyphHeights)) {
            auto& merged=m_lines[i];merged.box=Union(merged.box,b.box);
            merged.ink+=b.ink;merged.components+=b.components;
            merged.topBarInk+=b.topBarInk;merged.bottomBarInk+=b.bottomBarInk;
            for(auto& owner:componentOwners) {
                if(owner==j+1) owner=static_cast<uint16_t>(i+1);
                else if(owner>j+1) --owner;
            }
            m_lines.erase(m_lines.begin()+j);
            j=i+1;
        } else ++j;
    }
    std::vector<SubtitleBoxRect> coreExtents;
    coreExtents.reserve(m_lines.size());
    for(const auto& l:m_lines) coreExtents.push_back(l.box);
    if(glyphHeight) for(const auto& c:m_components) {
        const auto owner=componentOwners[c.label];
        const int maximumDetachedWidth=glyphHeight*2;
        const bool detachedHead=substantial(c) && owner && m_lines[owner-1].components==1 &&
            Height(c.box)<=glyphHeight*3/4 && Width(c.box)<=maximumDetachedWidth;
        const int narrowMarkWidth=glyphHeight/2+1;
        // A leading hyphen/em dash is a real subtitle glyph even when its
        // horizontal stroke is a detached, dense component. Accept it only at
        // a line's leading/trailing edge; sparse marks sampled unevenly retain
        // the existing bounded attachment behavior.
        const int componentBoxArea=Width(c.box)*Height(c.box);
        // Dash identity comes from its flat shape, not from exceeding half
        // a capital's height. Short dialogue dashes otherwise miss their own
        // baseline and can be mistaken for a separate-row unknown symbol.
        const bool horizontalDash=Width(c.box)>=(std::max)(2,glyphHeight/5) &&
            Width(c.box)<=maximumDetachedWidth &&
            Height(c.box)<=glyphHeight/3+1 &&
            Width(c.box)>=Height(c.box)*2;
        const bool sparseWideMark=horizontalDash && componentBoxArea>0 &&
            c.ink*100<=componentBoxArea*85;
        if(!detachedHead && (substantial(c) || Height(c.box)>glyphHeight/2+1 ||
            (Width(c.box)>narrowMarkWidth && !horizontalDash))) continue;
        size_t best=m_lines.size();int bestGap=glyphHeight*2;
        for(size_t i=0;i<m_lines.size();++i) {
            const auto& l=m_lines[i];
            const auto& core=coreExtents[i];
            if(l.components<1) continue;
            // Every attachment must be close to the original letters. Attached
            // dots cannot authorize a growing chain of unrelated bright specks.
            const int gap=std::max({0,core.left-c.box.right,c.box.left-core.right});
            const int centerOffset=std::abs((c.box.top+c.box.bottom)-(core.top+core.bottom));
            const bool terminalDash=horizontalDash && centerOffset<=glyphHeight &&
                (c.box.right<=core.left || c.box.left>=core.right);
            if(horizontalDash && !terminalDash && !sparseWideMark) continue;
            const int maximumGap=terminalDash?glyphHeight*2:glyphHeight/2;
            if(gap>maximumGap || gap>=bestGap || c.box.top<core.top-glyphHeight/4 ||
                c.box.bottom>core.bottom+glyphHeight/4) continue;
            if(detachedHead) {
                if(std::abs(c.box.top-core.top)>glyphHeight/4+1 || c.box.bottom>=core.bottom) continue;
                // A tiny terminal dot can disappear from the coarse mask while
                // remaining in the actual pixels. Check only the narrow region
                // below this isolated head and near the established baseline.
                bool nativeDot=false;
                const int startY=std::max(c.box.bottom*step,(core.bottom-3)*step);
                const int endY=std::min(source.height,(core.bottom+1)*step);
                for(int y=startY;y<endY && !nativeDot;++y)
                    for(int x=c.box.left*step;x<std::min(source.width,c.box.right*step);++x) {
                        AnalysisLumaSample value;
                        if(source.Sample(x,y,value) && value.luma>=coreThreshold &&
                            coreColor(value.chromaU,value.chromaV)) {
                            nativeDot=true;break;
                        }
                    }
                if(!nativeDot) continue;
            }
            best=i;bestGap=gap;
        }
        if(best<m_lines.size()) {
            auto& l=m_lines[best];l.box=Union(l.box,c.box);l.ink+=c.ink;++l.components;
            l.topBarInk+=c.topBarInk;l.bottomBarInk+=c.bottomBarInk;
            componentOwners[c.label]=static_cast<uint16_t>(best+1);
        }
    }
    // A finite adjacent mark cluster (for example terminal dots) belongs to
    // its original letter row even when its last component is farther than
    // half a glyph from that row. Inspect the complete cluster before granting
    // any new ownership: an ongoing speck chain cannot extend a caption.
    if(glyphHeight) for(size_t lineIndex=0;lineIndex<m_lines.size();++lineIndex) {
        const auto core=coreExtents[lineIndex];
        if(Width(core)<glyphHeight*3) continue;
        const int maxMarkHeight=(std::max)(2,glyphHeight*2/3+1);
        const int maxGap=(std::max)(2,glyphHeight*3/4+1);
        const int maxMark=(std::max)(2,glyphHeight/3+1);
        for(bool leading:{false,true}) {
            std::vector<const Line*> cluster;
            int frontier=leading?core.left:core.right;
            bool bounded=true;
            for(size_t offset=0;offset<m_components.size();++offset) {
                const auto& c=m_components[leading?m_components.size()-1-offset:offset];
                const auto owner=componentOwners[c.label];
                const bool isolatedMarkOwner=owner && owner!=lineIndex+1 &&
                    m_lines[owner-1].components<=2 && Width(coreExtents[owner-1])<=maxMark*3 &&
                    Height(coreExtents[owner-1])<=maxMarkHeight;
                if((owner && owner!=lineIndex+1 && !isolatedMarkOwner) || !c.hasTextCore ||
                    Width(c.box)>maxMark || Height(c.box)>maxMarkHeight ||
                    c.box.top<core.top-glyphHeight/4 || c.box.bottom>core.bottom+glyphHeight/4 ||
                    (leading?c.box.right>core.left:c.box.left<core.right)) continue;
                const int gap=leading?frontier-c.box.right:c.box.left-frontier;
                if(gap>maxGap) break;
                if(gap<0) continue;
                // Periods and closing quotes share a text row but occupy
                // different vertical positions. The original row bounds above
                // constrain both; one mark's baseline must not veto the next.
                cluster.push_back(&c);
                frontier=leading?c.box.left:c.box.right;
                if(cluster.size()>4 || (leading?core.left-frontier:frontier-core.right)>glyphHeight*3) {
                    bounded=false;break;
                }
            }
            if(!bounded || cluster.size()<2) continue;
            // Individual components passed local backing; also require opaque
            // whitespace across their joint extent so gaps cannot borrow scene.
            SubtitleBoxRect marks=cluster.front()->box;
            for(const auto* c:cluster) marks=Union(marks,c->box);
            unsigned tested=0,dark=0;
            for(int y:{marks.top-2,marks.bottom+1}) for(int x=marks.left;x<marks.right;++x) {
                if(x<0 || x>=w || y<0 || y>=h) continue;
                AnalysisLumaSample pixel;
                if(!source.Sample((std::min)(source.width-1,x*step),evidence.sourceRows[y],pixel))continue;
                ++tested;dark+=pixel.luma<=componentCardLimit;
            }
            if(!tested || dark*100<tested*85) continue;
            for(const auto* c:cluster) {
                const auto oldOwner=componentOwners[c->label];
                if(oldOwner==lineIndex+1) continue;
                if(oldOwner) {
                    auto& old=m_lines[oldOwner-1];
                    old.ink-=c->ink;
                    if(--old.components==0) old.box={};
                }
                auto& line=m_lines[lineIndex];
                line.box=Union(line.box,c->box);line.ink+=c->ink;++line.components;
                line.topBarInk+=c->topBarInk;line.bottomBarInk+=c->bottomBarInk;
                componentOwners[c->label]=static_cast<uint16_t>(lineIndex+1);
            }
        }
    }
    // Even one sampled bar pixel can belong to a real descender. Require it to
    // belong to a substantial stroke in this line, rather than demanding many
    // bar pixels or borrowing isolated noise/punctuation from the envelope.
    std::vector<unsigned> barStrokeSupport(m_lines.size());
    for(const auto& c:m_components) {
        const auto owner=componentOwners[c.label];
        if(!owner || c.ink<6 || Height(c.box)<minimumHeight ||
            Height(c.box)*2<Height(m_lines[owner-1].box)) continue;
        if(c.topBarInk) barStrokeSupport[owner-1]|=1;
        if(c.bottomBarInk) barStrokeSupport[owner-1]|=2;
    }
    // A text line needs several strokes, a plausible baseline, and whitespace.
    auto plausible=[&](const Line& l,size_t index){
        const int lw=Width(l.box),lh=Height(l.box);
        // Direct bar ink supports narrow words such as "Hi". Picture-only
        // companions retain the wider-text requirement so narrow scenery
        // fragments cannot attach themselves to an otherwise valid bar cue.
        const bool oneConnectedWord = l.components==1 &&
            lh>=(std::max)(minimumHeight*2,6) && lw>=lh*2 &&
            l.ink>=(std::max)(12,lh*2);
        // One compact connected glyph can be a complete caption in many
        // scripts. Only direct bar support grants this nomination; retain
        // size, whitespace/density and black-surround component checks.
        const bool compactBarGlyph=l.components==1 && barStrokeSupport[index] &&
            lh>=(std::max)(minimumHeight*2,6) && lw*10>=lh*8 &&
            l.ink>=(std::max)(12,lh*2);
        const int minimumAspectTenths=barStrokeSupport[index]?8:13;
        return (l.components>=2 || oneConnectedWord || compactBarGlyph) && lh>=minimumHeight &&
            lw*10>=lh*minimumAspectTenths &&
            lw<w*49/50 && l.ink>=12 && l.ink*100<lw*lh*82;
    };
    // The diagnostic targets centered movie subtitles. A title or player label
    // near either corner must not win the anchor search merely by having more
    // ink. Keep this independent of line width so short centered cues qualify.
    auto centered=[&](const Line& l){
        return std::abs(l.box.left+l.box.right-w)*5<=w*2;
    };
    // Character recognition is deliberately not part of this detector. After
    // a credible centered text row exists, let a compact, matching-color mark
    // in a letterbox bar join that row even when it is a music note or another
    // unknown glyph that does not resemble a letter. Such a mark can extend a
    // cue into the bar, but cannot establish a cue by itself.
    const std::vector<Line> coreLines=m_lines;
    std::vector<bool> captionCore(m_lines.size(),false);
    for(size_t i=0;i<coreLines.size();++i)
        captionCore[i]=plausible(coreLines[i],i) && centered(coreLines[i]);
    for(const auto& markComponent:m_components)
    {
        if((markComponent.topBarInk==0 && markComponent.bottomBarInk==0) ||
            markComponent.ink<6)
            continue;
        const unsigned owner=componentOwners[markComponent.label];
        if(owner && owner<=coreLines.size() && coreLines[owner-1].components>4)
            continue;
        // A tiny off-center row made only of this mark is not a text baseline.
        // It may still be a real glyph beside a much wider centered caption.
        // A centered symbol-only cue keeps its own row and bar eligibility.
        bool compactOffCenterOwner=false;
        if(owner && owner<=coreLines.size())
        {
            const auto& own=coreLines[owner-1];
            const int ownScale=(std::max)({glyphHeight,Height(own.box),minimumHeight*2});
            compactOffCenterOwner=own.components<=4 && Width(own.box)<=ownScale*3 &&
                std::abs(own.box.left+own.box.right-w)>w/5;
        }
        size_t best=m_lines.size();int bestVertical=std::numeric_limits<int>::max();
        int bestDistance=std::numeric_limits<int>::max();int bestScale=0;
        for(size_t lineIndex=0;lineIndex<coreLines.size();++lineIndex)
        {
            if((compactOffCenterOwner && owner && lineIndex==owner-1) ||
                !captionCore[lineIndex] ||
                m_lines[lineIndex].components==0) continue;
            const auto& core=coreLines[lineIndex].box;
            const auto& mark=markComponent.box;
            const int scale=(std::max)({glyphHeight,Height(core),minimumHeight*2});
            const int markWidth=Width(mark),markHeight=Height(mark);
            if(markWidth>scale*3 || markHeight>scale*2) continue;
            const int horizontalGap=(std::max)({0,core.left-mark.right,mark.left-core.right});
            const int verticalGap=(std::max)({0,core.top-mark.bottom,mark.top-core.bottom});
            if(horizontalGap>(std::max)(8,scale*3) || verticalGap>scale*2) continue;
            // Select the nearest text baseline before deciding whether the
            // mark is a separate-row symbol. Otherwise a lower-row fragment
            // can stretch the upper caption across both rows.
            const int centerX2=core.left+core.right;
            const int markCenterX2=mark.left+mark.right;
            const int centerY2=core.top+core.bottom;
            const int markCenterY2=mark.top+mark.bottom;
            const int distance=horizontalGap*2+
                std::abs(centerX2-markCenterX2)/2+
                std::abs(centerY2-markCenterY2);
            if(verticalGap<bestVertical ||
                (verticalGap==bestVertical && distance<bestDistance))
            {
                best=lineIndex;bestVertical=verticalGap;
                bestDistance=distance;bestScale=scale;
            }
        }
        if(best==m_lines.size() || (owner && best==owner-1) ||
            bestVertical<=(std::max)(1,bestScale/4)) continue;
        auto& caption=m_lines[best];
        caption.box=Union(caption.box,markComponent.box);
        caption.ink+=markComponent.ink;
        ++caption.components;
        caption.topBarInk+=markComponent.topBarInk;
        caption.bottomBarInk+=markComponent.bottomBarInk;
        if(markComponent.topBarInk) barStrokeSupport[best]|=1;
        if(markComponent.bottomBarInk) barStrokeSupport[best]|=2;
        componentOwners[markComponent.label]=static_cast<uint16_t>(best+1);
    }
    // A picture-side companion must sit on the opaque subtitle card. Inspect
    // whitespace distributed across the line, excluding whole component boxes:
    // a letter's dark hole or outline is not evidence of a black background.
    const int cardDarkLimit=bar+(std::max)(24,(ink-bar)/12);
    auto darkSample=[&](int x,int y) {
        AnalysisLumaSample sample;
        return x>=0 && x<w && y>=0 && y<h &&
            source.Sample((std::min)(source.width-1,x*step),
                evidence.sourceRows[y],sample) && sample.luma<=cardDarkLimit;
    };
    auto onOpaqueCard=[&](const Line& candidate,size_t index) {
        const auto& r=candidate.box;
        const int first=(std::max)(top,r.top),last=(std::min)(bottom,r.bottom);
        if(first>=last) return true;
        unsigned tested[3]{},dark[3]{};
        const int candidateWidth=Width(r);
        std::vector<uint8_t> glyphEnvelope(static_cast<size_t>(candidateWidth)*(last-first),0);
        for(const auto& c:m_components) {
            if(componentOwners[c.label]!=index+1) continue;
            for(int y=(std::max)(first,c.box.top-1);y<(std::min)(last,c.box.bottom+1);++y)
                for(int x=(std::max)(r.left,c.box.left-1);x<(std::min)(r.right,c.box.right+1);++x)
                    glyphEnvelope[static_cast<size_t>(y-first)*candidateWidth+x-r.left]=1;
        }
        for(int y=first;y<last;++y) for(int x=r.left;x<r.right;++x) {
            if(glyphEnvelope[static_cast<size_t>(y-first)*candidateWidth+x-r.left]) continue;
            const int tile=(std::min)(2,(x-r.left)*3/(std::max)(1,Width(r)));
            ++tested[tile];dark[tile]+=darkSample(x,y);
        }
        // Joined scripts may form one component across an entire word. Their
        // external whitespace still proves backing without inspecting holes.
        for(int y : {r.top-2,r.bottom+1}) {
            if(y<top || y>=bottom) continue;
            for(int x=r.left;x<r.right;++x) {
                const int tile=(std::min)(2,(x-r.left)*3/(std::max)(1,Width(r)));
                ++tested[tile];dark[tile]+=darkSample(x,y);
            }
        }
        // Component envelopes can surround real scenery (a window or lens
        // opening). The exterior strips below are necessary but insufficient:
        // unowned pixels INSIDE a component must also be black backing. Keep
        // one sampled pixel around actual strokes for antialiasing. This does
        // not use glyph holes as a substitute for the exterior card proof.
        unsigned interiorTested=0,interiorBlack=0;
        for(const auto& c:m_components) {
            if(componentOwners[c.label]!=index+1) continue;
            for(int y=(std::max)(first,c.box.top);y<(std::min)(last,c.box.bottom);++y)
                for(int x=c.box.left;x<c.box.right;++x) {
                    bool stroke=false;
                    for(int dy=-1;dy<=1 && !stroke;++dy)
                        for(int dx=-1;dx<=1;++dx) {
                            const int xx=x+dx,yy=y+dy;
                            if(xx>=0 && xx<w && yy>=0 && yy<h && m_labels[yy*w+xx]==c.label) {
                                stroke=true;break;
                            }
                        }
                    if(stroke) continue;
                    ++interiorTested;
                    interiorBlack+=m_luma[y*w+x]<=cardDarkLimit;
                }
        }
        if(interiorTested>=9 && interiorBlack*100<interiorTested*85) return false;
        unsigned total=0,black=0;int supported=0;
        for(int tile=0;tile<3;++tile) {
            total+=tested[tile];black+=dark[tile];
            if(tested[tile]>=3 && dark[tile]*100>=tested[tile]*80) ++supported;
        }
        return total>=9 && black*100>=total*80 && supported>=2;
    };

    // Relate every companion to the original bar anchor, never to another
    // candidate. This prevents scenery from forming a chain above the cue.
    size_t anchor=m_lines.size(); int score=0;
    for(size_t i=0;i<m_lines.size();++i)
    {
        const auto& l=m_lines[i];
        if(!plausible(l,i) || !centered(l)) continue;
        // Eligibility belongs to this line's accepted components, never all
        // bright pixels in its envelope (which includes discarded picture/noise).
        const bool inTopBar = (barStrokeSupport[i]&1)!=0;
        const bool inBottomBar = (barStrokeSupport[i]&2)!=0;
        const bool trulyInBar=l.box.top*step<pictureTop || l.box.bottom*step>pictureBottom;
        if(!trulyInBar && !onOpaqueCard(l,i)) continue;
        if(inTopBar != inBottomBar && l.ink>score) { anchor=i; score=l.ink; }
    }
    if(anchor==m_lines.size()) return false;
    const Line& a=m_lines[anchor];

    if(!onOpaqueCard(a,anchor)) return false;
    m_currentBarAnchor={a.box.left*step,a.box.top*step,
        std::min(source.width,a.box.right*step),std::min(source.height,a.box.bottom*step)};
    box=a.box; lineCount=1;
    std::array<SubtitleBoxRect,3> lineBoxes{};
    std::array<size_t,3> lineIndices{};
    lineBoxes[0]=a.box;
    lineIndices[0]=anchor;
    const int anchorHeight=Height(a.box);
    for(size_t i=0;i<m_lines.size();++i)
    {
        const Line& l=m_lines[i];
        if(i==anchor || !plausible(l,i)) continue;
        if(!onOpaqueCard(l,i)) continue;
        const int lh=Height(l.box);
        const int overlap=std::min(a.box.right,l.box.right)-std::max(a.box.left,l.box.left);
        const int gap=std::max(a.box.top,l.box.top)-std::min(a.box.bottom,l.box.bottom);
        // An RTL second line commonly ends at the same right edge as a longer
        // centered bar-crossing line. Keep the anchor centered, but let a
        // shorter companion prove its relationship through either shared edge
        // plus substantial horizontal overlap.
        const int alignmentTolerance=std::max(8,std::max(lh,anchorHeight)*2);
        const bool centerAligned=std::abs(a.box.left+a.box.right-
            l.box.left-l.box.right)<=std::max(lh,anchorHeight)*4;
        const bool sharesEdge=
            std::abs(a.box.left-l.box.left)<=alignmentTolerance ||
            std::abs(a.box.right-l.box.right)<=alignmentTolerance;
        if(gap < -std::min(lh,anchorHeight)/3 || gap>anchorHeight*3 ||
            lh*2<anchorHeight || lh>anchorHeight*2 ||
            (!centerAligned && !sharesEdge) ||
            overlap<std::min(Width(a.box),Width(l.box))/3) continue;
        if(++lineCount>3) return false;
        lineBoxes[lineCount-1]=l.box;
        lineIndices[lineCount-1]=i;
        box=Union(box,l.box);
    }
    const int edgeDarkLimit=bar+(std::max)(8,(ink-bar)/64);
    auto cardEdgePixel=[&](int x,int y) {
        AnalysisLumaSample sample;
        return source.Sample((std::min)(source.width-1,x*step),evidence.sourceRows[y],sample) &&
            sample.luma<=edgeDarkLimit;
    };
    auto recordBackingDistance=[&](int unpaddedTop,int unpaddedBottom,bool topInferred,bool bottomInferred) {
        auto record=[&](int gap,bool inferred) {
            gap=(std::max)(0,gap);
            if(m_diagnosticNearBarGap<0 || gap<m_diagnosticNearBarGap ||
                (gap==m_diagnosticNearBarGap && !inferred)) {
                m_diagnosticNearBarGap=gap;m_diagnosticNearBarGapInferred=inferred;
            }
        };
        if(pictureTop>0)record(unpaddedTop-pictureTop,topInferred);
        if(pictureBottom<source.height)record(pictureBottom-unpaddedBottom,bottomInferred);
    };
    std::array<SubtitleBoxRect,3> cardInteriors{};
    int incompleteCardSeed=-1;
    SubtitleBoxRect completedCard;
    // Measure the original card separately from glyph bounds. Start with each
    // accepted picture row, seek its nearby dark margins, then cover down to
    // the bar. Rounded corners may taper at the first/last scanline, so retain
    // the widest corroborated run. No display padding participates here.
    for(int selected=0;selected<lineCount;++selected) {
        const auto& r=lineBoxes[selected];
        if(r.bottom<=top || r.top>=bottom) continue;
        const int scale=(std::max)(3,Height(r));
        const int first=(std::max)(top,r.top),last=(std::min)(bottom,r.bottom);
        int cardTop=first,cardBottom=last,left=r.left,right=r.right;
        auto darkRow=[&](int y) {
            unsigned count=0,black=0;
            for(int x=r.left;x<r.right;++x) {
                const auto label=m_labels[y*w+x];
                if(label && componentOwners[label]==lineIndices[selected]+1) continue;
                ++count;black+=darkSample(x,y);
            }
            return count>=unsigned((std::max)(3,Width(r)/5)) && black*100>=count*85;
        };
        // Top/side search is deliberately local to actual accepted lettering.
        for(int y=first-1;y>=(std::max)(top,first-scale);--y) {
            if(!darkRow(y)) break;cardTop=y;
        }
        for(int y=last;y<(std::min)(bottom,last+scale);++y) {
            if(!darkRow(y)) break;cardBottom=y+1;
        }
        const bool topInferred=cardTop==(std::max)(top,first-scale) && cardTop>top;
        const bool bottomInferred=cardBottom==(std::min)(bottom,last+scale) && cardBottom<bottom;
        int unpaddedTop=cardTop==top?pictureTop:evidence.sourceRows[cardTop];
        int unpaddedBottom=(std::min)(pictureBottom,cardBottom*step);
        auto nativeBlackRow=[&](int y) {
            unsigned count=0,black=0;
            for(int x=r.left;x<r.right;++x) {
                AnalysisLumaSample value;
                if(!source.Sample((std::min)(source.width-1,x*step),y,value))return false;
                ++count;black+=value.luma<=cardDarkLimit;
            }
            return count>=3 && black*100>=count*85;
        };
        // Resolve only the final sampling interval of an actually visible edge.
        // Cleanup guards are not distance evidence. If a boundary disappears
        // into black scenery, retain only the already-proved black support up
        // to one glyph height; do not invent a finite measured card edge.
        if(!topInferred && cardTop>top)
            while(unpaddedTop>evidence.sourceRows[cardTop-1]+1 && nativeBlackRow(unpaddedTop-1))--unpaddedTop;
        if(!bottomInferred && cardBottom<bottom)
            while(unpaddedBottom>evidence.sourceRows[cardBottom-1]+1 && !nativeBlackRow(unpaddedBottom-1))--unpaddedBottom;
        // When only one edge disappears into scenery, the actually measured
        // opposite whitespace inset bounds the missing margin. A scene shadow
        // cannot borrow a whole glyph-height beyond a visibly tight card.
        // With both edges hidden, retain the one-height verified-support cap.
        if(bottomInferred && !topInferred && cardTop>top) {
            const int inset=(std::max)(0,evidence.sourceRows[first]-unpaddedTop);
            unpaddedBottom=(std::min)(unpaddedBottom,last*step+inset);
        }
        if(topInferred && !bottomInferred && cardBottom<bottom) {
            const int inset=(std::max)(0,unpaddedBottom-last*step);
            unpaddedTop=(std::max)(unpaddedTop,evidence.sourceRows[first]-inset);
        }
        recordBackingDistance(unpaddedTop,unpaddedBottom,topInferred,bottomInferred);
        if(!(a.box.top*step<pictureTop || a.box.bottom*step>pictureBottom)) {
            if(topInferred)cardTop=first;
            if(bottomInferred)cardBottom=last;
        }
        std::vector<int> leftEdges,rightEdges;
        for(int y=cardTop;y<cardBottom;++y) {
            if(!darkRow(y)) continue;
            int l=r.left,rr=r.right;
            const int leftLimit=(std::max)(0,r.left-scale*4);
            const int rightLimit=(std::min)(w,r.right+scale*4);
            while(l>leftLimit && cardEdgePixel(l-1,y)) --l;
            while(rr<rightLimit && cardEdgePixel(rr,y)) ++rr;
            // Hitting a search limit is an occluded edge, not a measurement.
            // Shadows/robes can connect to one side of the card indefinitely.
            if(l>leftLimit && l<r.left) leftEdges.push_back(l);
            if(rr<rightLimit && rr>r.right) rightEdges.push_back(rr);
        }
        // A rectangle contributes the same straight side on multiple rows.
        // Curved corners and a moving dark robe do not share that coordinate.
        auto straightEdge=[&](std::vector<int>& edges,bool leftSide,int& value) {
            if(edges.size()<2) return false;
            std::sort(edges.begin(),edges.end());
            size_t bestCount=0;int best=0;
            for(size_t begin=0;begin<edges.size();) {
                size_t end=begin+1;
                while(end<edges.size() && edges[end]==edges[begin]) ++end;
                const size_t count=end-begin;
                if(count>bestCount || (count==bestCount &&
                    (leftSide?edges[begin]>best:edges[begin]<best))) {
                    bestCount=count;best=edges[begin];
                }
                begin=end;
            }
            if(bestCount<2) return false;
            // Glyph strokes and rounded corners can produce an inner repeated
            // stop. Prefer a wider straight side when multiple rows corroborate
            // it with at least half the modal support. Bound that correction to
            // half a glyph height so adjoining dark scenery cannot grow a card.
            const int modal=best;
            for(size_t begin=0;begin<edges.size();) {
                size_t end=begin+1;
                while(end<edges.size() && edges[end]==edges[begin])++end;
                if(end-begin>=2 && (end-begin)*2>=bestCount &&
                    std::abs(edges[begin]-modal)<=(std::max)(1,scale/2) &&
                    (leftSide?edges[begin]<best:edges[begin]>best))best=edges[begin];
                begin=end;
            }
            value=best;return true;
        };
        const bool measuredLeft=straightEdge(leftEdges,true,left);
        const bool measuredRight=straightEdge(rightEdges,false,right);
        // A row truncated by the initial search band is not a complete glyph
        // measurement, even when both side edges were found. Let the bounded
        // opaque-card proof recover the remaining rows above/below that band.
        const int boundaryReach=(std::max)(scale,glyphHeight)+2;
        const bool touchesSearchBoundary =
            (r.top <= normalBottomStart + boundaryReach && r.bottom > normalBottomStart && r.top >= top) ||
            (r.bottom >= normalTopEnd - boundaryReach && r.top < normalTopEnd && r.bottom <= bottom);
        // A rejected parenthesis/mark can masquerade as a measured side when
        // the local black-run search stops at its stroke. Nearby matching ink
        // outside that interior nominates full card proof; it never authorizes
        // expansion by itself, and black scenery is not an edge to follow.
        auto hasExcludedSideInk=[&]() {
            const int reach=(std::max)(15,(std::min)(60,scale*step/2));
            const int firstY=(std::max)(pictureTop,r.top*step);
            const int lastY=(std::min)(pictureBottom,r.bottom*step);
            for(int side=0;side<2;++side) {
                const int firstX=side ? r.right*step : (std::max)(0,r.left*step-reach);
                const int lastX=side ? (std::min)(source.width,r.right*step+reach) : r.left*step;
                for(int x=firstX;x<lastX;++x) {
                    if(x>left*step && x<right*step-1)continue;
                    for(int y=firstY;y<lastY;++y) {
                        AnalysisLumaSample value;
                        if(source.Sample(x,y,value) && value.luma>=coreThreshold &&
                            coreColor(value.chromaU,value.chromaV))return true;
                    }
                }
            }
            return false;
        };
        if(incompleteCardSeed<0 &&
            (!measuredLeft || !measuredRight || touchesSearchBoundary || hasExcludedSideInk()))
            incompleteCardSeed=selected;
        // A rectangular caption uses comparable side insets. An occluded side
        // borrows only the measured opposite inset, never a full search radius.
        if(!measuredLeft && measuredRight) left=(std::max)(0,r.left-(right-r.right));
        if(!measuredRight && measuredLeft) right=(std::min)(w,r.right+(r.left-left));
        // The card is attached to its bar; retain the intervening black area.
        if(a.box.bottom*step>pictureBottom) cardBottom=bottom;
        if(a.box.top*step<pictureTop) cardTop=top;
        // Cover antialiased card edges beyond the last coarse black sample.
        // This small fixed cleanup guard is unrelated to display padding.
        const int fringe=(std::max)(1,step);
        SubtitleBoxRect measured{(std::max)(0,left*step-fringe-(measuredLeft?2:0)),
            (std::max)(pictureTop,evidence.sourceRows[cardTop]-fringe-2),
            (std::min)(source.width,right*step+fringe+(measuredRight?2:0)),
            (std::min)(pictureBottom,cardBottom*step+fringe+2)};
        // Cleanup includes an outside fringe; only the measured interior may
        // authorize small otherwise-unrecognized marks in picture content.
        cardInteriors[selected]={left*step,evidence.sourceRows[cardTop],
            right*step,(std::min)(pictureBottom,cardBottom*step)};
        cardInteriors[selected]=ProveCardBoundaryContinuation(source,cardInteriors[selected],
            pictureTop,pictureBottom,componentCardLimit,threshold,inkU,inkV,scale*step);
        m_currentLineCapturePanels[selected]=cardInteriors[selected];
        if(cardInteriors[selected].Valid())m_currentCapturePanel=m_currentCapturePanel.Valid()
            ? Union(m_currentCapturePanel,cardInteriors[selected]):cardInteriors[selected];
        m_currentLinePanels[selected]=measured;
        if(measured.Valid()) m_currentSourcePanel=m_currentSourcePanel.Valid()
            ? Union(m_currentSourcePanel,measured):measured;
    }
    // If the old glyph-relative side search could not find a real boundary,
    // measure the finite card from its opaque whitespace. A partial word must
    // not set the maximum width or hide another row on that same proved card.
    // Healthy already-measured cards keep their existing fast path.
    if(incompleteCardSeed>=0) {
        // The bar anchor can be a short third line. Its nearby whitespace
        // cannot establish the width of the full card when scenery joins a
        // side. Use the strongest accepted width already proved in picture.
        for(int i=0;i<lineCount;++i)
            if(lineBoxes[i].top<bottom && lineBoxes[i].bottom>top &&
                Width(lineBoxes[i])>Width(lineBoxes[incompleteCardSeed]))incompleteCardSeed=i;
        const auto seed=lineBoxes[incompleteCardSeed];
        const SubtitleBoxRect nativeSeed{seed.left*step,seed.top*step,seed.right*step,seed.bottom*step};
        SubtitleOpaqueCardRefinement refined;
        if(RefineSubtitleOpaqueCard(source,nativeSeed,(std::max)(Height(seed),glyphHeight)*step,pictureTop,pictureBottom,
            edgeDarkLimit,threshold,inkU,inkV,refined,coreThreshold)) {
            bool containsCurrent=true;
            std::array<SubtitleBoxRect,3> recovered{};
            int recoveredCount=lineCount;
            for(int i=0;i<lineCount;++i) {
                const auto& old=lineBoxes[i];
                recovered[i]={old.left*step,old.top*step,old.right*step,old.bottom*step};
                const int first=(std::max)(pictureTop,recovered[i].top);
                const int last=(std::min)(pictureBottom,recovered[i].bottom);
                if(first<last && (recovered[i].left<refined.interior.left ||
                    recovered[i].right>refined.interior.right || first<refined.interior.top || last>refined.interior.bottom))
                    containsCurrent=false;
            }
            for(int row=0;row<refined.glyphRowCount && containsCurrent;++row) {
                const auto& added=refined.glyphRows[row];int match=-1;
                for(int i=0;i<recoveredCount;++i) {
                    const auto& old=recovered[i];
                    const int overlap=(std::min)(old.bottom,added.bottom)-(std::max)(old.top,added.top);
                    if(overlap*2>=(std::min)(Height(old),Height(added))) {match=i;break;}
                }
                if(match>=0)recovered[match]=Union(recovered[match],added);
                else if(recoveredCount<3)recovered[recoveredCount++]=added;
                else containsCurrent=false;
            }
            if(containsCurrent) {
                m_diagnosticNearBarGap=-1;m_diagnosticNearBarGapInferred=false;
                recordBackingDistance(refined.measuredBounds.top,refined.measuredBounds.bottom,false,false);
                completedCard=refined.interior;
                for(int i=lineCount;i<recoveredCount;++i)lineIndices[i]=MaxComponents+i;
                lineCount=recoveredCount;
                m_currentSourcePanel={};m_currentCapturePanel={};
                m_currentLinePanels={};m_currentLineCapturePanels={};cardInteriors={};
                const int fringe=step+2;
                const auto& measured=refined.measuredBounds;
                const SubtitleBoxRect cleanup{(std::max)(0,measured.left-fringe),
                    (std::max)(pictureTop,measured.top-fringe),
                    (std::min)(source.width,measured.right+fringe),
                    (std::min)(pictureBottom,measured.bottom+fringe)};
                for(int i=0;i<lineCount;++i) {
                    const auto& r=recovered[i];
                    lineBoxes[i]={r.left/step,r.top/step,(r.right+step-1)/step,(r.bottom+step-1)/step};
                    box=Union(box,lineBoxes[i]);
                    if(r.top>=pictureBottom || r.bottom<=pictureTop)continue;
                    cardInteriors[i]=ProveCardBoundaryContinuation(source,completedCard,pictureTop,pictureBottom,
                        componentCardLimit,threshold,inkU,inkV,Height(lineBoxes[i])*step);
                    m_currentLineCapturePanels[i]=cardInteriors[i];m_currentLinePanels[i]=cleanup;
                    m_currentSourcePanel=cleanup;
                    m_currentCapturePanel=m_currentCapturePanel.Valid()
                        ? Union(m_currentCapturePanel,cardInteriors[i]):cardInteriors[i];
                }
                // Restore sampled ownership from the original pixels. Earlier
                // letter-shape/local-halo rejection is no longer authoritative
                // once a complete opaque rectangle has independently proved it.
                const int firstY=(std::max)(0,completedCard.top/step);
                const int lastY=(std::min)(h,(completedCard.bottom+step-1)/step);
                for(int y=firstY;y<lastY;++y)for(int x=(completedCard.left+step-1)/step;
                    x<(std::min)(w,(completedCard.right+step-1)/step);++x) {
                    const int sy=evidence.sourceRows[y];
                    if(sy<completedCard.top || sy>=completedCard.bottom)continue;
                    AnalysisLumaSample value;if(!source.Sample(x*step,sy,value))continue;
                    const size_t pixel=size_t(y)*w+x;
                    m_luma[pixel]=value.luma;
                    m_chroma[pixel]=uint32_t(value.chromaU)|(uint32_t(value.chromaV)<<16);
                    if(value.luma<=trackingBlackLimit)evidence.blackBacking[pixel/64]|=uint64_t{1}<<(pixel%64);
                    if(value.luma>=threshold && std::abs(int(value.chromaU)-inkU)<=80 &&
                        std::abs(int(value.chromaV)-inkV)<=80) {
                        evidence.rawInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                        evidence.ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
                    }
                }
            }
        }
    }
    // Attach nearby raster marks only after a real caption and its opaque
    // backing are established. Reuse existing components; no extra image pass,
    // OCR, frame delay, or unbounded flood fill. Four 15-source-pixel hops max.
    const int hop=(std::max)(1,15/step), cap=(std::max)(1,60/step);
    for(int selected=0;selected<lineCount;++selected) {
        auto& line=lineBoxes[selected];const auto seed=line;
        const auto owner=lineIndices[selected]+1;
        const auto& panel=m_currentLinePanels[selected];
        // Picture-card completion below uses the original native raster. This
        // older component attachment remains only for cues wholly in a bar.
        if(cardInteriors[selected].Valid())continue;
        auto safePixel=[&](int x,int y) {
            if(x<0 || x>=w || y<0 || y>=h)return false;
            const int sy=evidence.sourceRows[y],sx=x*step;
            return sy<pictureTop || sy>=pictureBottom ||
                (panel.Valid() && sx>=panel.left && sx<panel.right &&
                 sy>=panel.top && sy<panel.bottom);
        };
        for(int pass=0;pass<4;++pass) {
            const auto previous=line;bool changed=false;
            for(const auto& c:m_components) {
                if(componentOwners[c.label] || !c.hasTextCore)continue;
                const auto& q=c.box;
                const auto& interior=cardInteriors[selected];
                const bool insideCard=interior.Valid() &&
                    (q.left-1)*step>=interior.left && (q.right+1)*step<=interior.right &&
                    evidence.sourceRows[(std::max)(0,q.top-1)]>=interior.top &&
                    evidence.sourceRows[(std::min)(h-1,q.bottom)]<interior.bottom;
                if((q.left<previous.right && q.right>previous.left) ||
                   (!insideCard && Height(q)<(std::max)(3,Height(seed)/3)) ||
                   q.left<seed.left-cap || q.right>seed.right+cap ||
                   q.top<seed.top || q.bottom>seed.bottom ||
                   Width(q)>Height(seed)*2 || Height(q)>Height(seed)*3/2)continue;
                const int dx=(std::max)({0,previous.left-q.right,q.left-previous.right});
                const int dy=(std::max)({0,previous.top-q.bottom,q.top-previous.bottom});
                if(dx>hop || dy>hop || (dx>0 && dy>0))continue;
                // A component cannot cross from the measured card into scene
                // detail. A dark halo rejects highlights even on black scenery.
                bool safe=true;unsigned dark=0,count=0;
                for(int y=q.top-1;y<=q.bottom && safe;++y)
                    for(int x=q.left-1;x<=q.right;++x) {
                        if(!safePixel(x,y)){safe=false;break;}
                        if(x>=q.left && x<q.right && y>=q.top && y<q.bottom)continue;
                        ++count;dark+=darkSample(x,y);
                    }
                if(!safe || !count || dark*100<count*90)continue;
                componentOwners[c.label]=static_cast<uint16_t>(owner);
                line=Union(line,q);changed=true;
            }
            if(!changed)break;
        }
        box=Union(box,line);
    }
    // Detection nominates a backed cue; its coarse component heuristics do
    // not own individual letters. Complete only the narrow native fringes of
    // accepted rows inside their measured opaque interior. This recovers a
    // question-mark loop or detached diacritic even if grouping rejected it.
    // Identity signatures below deliberately retain the existing sampled ink.
    std::array<SubtitleBoxRect,3> nativeCapture{};
    size_t nativeSamples=0;
    constexpr size_t NativeSampleLimit=65536;
    for(int selected=0;selected<lineCount;++selected) {
        const auto coarse=lineBoxes[selected];
        const SubtitleBoxRect seed{coarse.left*step,coarse.top*step,
            (std::min)(source.width,coarse.right*step),(std::min)(source.height,coarse.bottom*step)};
        nativeCapture[selected]=seed;
        auto interior=cardInteriors[selected];
        // The physical bar is already verified backing. Complete adjacent
        // native glyph pixels there too; no visible rectangular card edge is
        // required when black backing merges into the bar. The same finite
        // 15-pixel hops / 60-pixel reach and component checks still apply.
        if(!interior.Valid()) {
            if(seed.bottom<=pictureTop)interior={0,0,source.width,pictureTop};
            else if(seed.top>=pictureBottom)interior={0,pictureBottom,source.width,source.height};
        }
        if(!interior.Valid() || nativeSamples>=NativeSampleLimit)continue;
        constexpr int NativeHop=15,NativeReach=60;
        const SubtitleBoxRect roi{(std::max)(interior.left+1,seed.left-NativeReach),
            (std::max)(0,seed.top-NativeReach),(std::min)(interior.right-1,seed.right+NativeReach),
            (std::min)(source.height,seed.bottom+NativeReach)};
        if(!roi.Valid())continue;
        const int rw=Width(roi),rh=Height(roi),seedHeight=Height(seed);
        const size_t cells=size_t(rw)*rh;
        m_nativeCardSamples.resize(cells);m_nativeCardState.assign(cells,0);
        auto inside=[&](int x,int y) {
            if(x<roi.left || x>=roi.right || y<roi.top || y>=roi.bottom)return false;
            if(y<pictureTop || y>=pictureBottom)return true;
            return x>interior.left && x<interior.right-1 &&
                (interior.top<=pictureTop || y>interior.top) &&
                (interior.bottom>=pictureBottom || y<interior.bottom-1);
        };
        auto read=[&](int x,int y)->int {
            if(!inside(x,y))return -1;
            const int index=(y-roi.top)*rw+x-roi.left;
            auto& state=m_nativeCardState[index];
            if(!state) {
                if(nativeSamples>=NativeSampleLimit)return -1;
                ++nativeSamples;
                auto& value=m_nativeCardSamples[index];
                const bool sampled=source.Sample(x,y,value);
                bool bright=sampled && value.luma>=threshold &&
                    std::abs(int(value.chromaU)-inkU)<=80 && std::abs(int(value.chromaV)-inkV)<=80;
                const bool boundaryRow=(y>=pictureBottom && y<pictureBottom+CardBoundaryUncertainty) ||
                    (y>=pictureTop-CardBoundaryUncertainty && y<pictureTop);
                if(!bright && sampled && boundaryRow && y>=interior.top && y<interior.bottom &&
                    value.luma>componentCardLimit && std::abs(int(value.chromaU)-inkU)<=32 &&
                    std::abs(int(value.chromaV)-inkV)<=32)
                    bright=HasBoundaryGlyphCore(source,x,y,interior,threshold,inkU,inkV,
                        &nativeSamples,NativeSampleLimit);
                state=bright?2:1;
            }
            return index;
        };
        auto inspect=[&](int x,int y) {
            const int origin=read(x,y);
            if(origin<0 || m_nativeCardState[origin]!=2)return;
            SubtitleBoxRect component{x,y,x+1,y+1};
            bool attached=false,hasCore=false,boundary=false,complete=true;
            m_flood.clear();m_flood.push_back(origin);m_nativeCardState[origin]=3;
            for(size_t next=0;next<m_flood.size();++next) {
                const int index=m_flood[next],cy=roi.top+index/rw,cx=roi.left+index%rw;
                component=Union(component,{cx,cy,cx+1,cy+1});
                attached=attached || (cx>=seed.left && cx<seed.right && cy>=seed.top && cy<seed.bottom);
                const auto& value=m_nativeCardSamples[index];
                hasCore=hasCore || (value.luma>=coreThreshold && coreColor(value.chromaU,value.chromaV));
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if(!dx && !dy)continue;
                    if(!inside(cx+dx,cy+dy)) {boundary=true;continue;}
                    const int adjacent=read(cx+dx,cy+dy);
                    if(adjacent<0) {complete=false;continue;}
                    if(m_nativeCardState[adjacent]!=2)continue;
                    m_nativeCardState[adjacent]=3;m_flood.push_back(adjacent);
                }
            }
            if(!complete || boundary || !hasCore)return;
            // A nearby accepted caption row already owns its own glyphs.
            // Native fringe recovery must not merge two legitimate lines just
            // because their baseline gap is shorter than the search strip.
            const int center=component.top+component.bottom;
            const int ownDistance=std::abs(center-seed.top-seed.bottom);
            for(int other=0;other<lineCount;++other)if(other!=selected) {
                const auto& row=lineBoxes[other];
                const int overlap=(std::min)(component.bottom,row.bottom*step)-
                    (std::max)(component.top,row.top*step);
                if(overlap>0 && std::abs(center-(row.top+row.bottom)*step)<ownDistance)return;
            }
            // Standalone marks need surrounding black and a bounded footprint.
            // A long boundary/reflection stripe cannot masquerade as a glyph.
            if(!attached) {
                if(Width(component)>seedHeight*2 || Height(component)>seedHeight*3/2)return;
                unsigned count=0,black=0;
                // One-pixel rings often contain the antialiased fringe of a
                // curved parenthesis. Test black backing just beyond that
                // fringe, without relaxing the required black fraction.
                constexpr int halo=2;
                for(int yy=component.top-halo;yy<component.bottom+halo;++yy)
                    for(int xx=component.left-halo;xx<component.right+halo;++xx) {
                        if(xx>component.left-halo && xx<component.right+halo-1 &&
                            yy>component.top-halo && yy<component.bottom+halo-1)continue;
                        const int index=read(xx,yy);if(index<0)return;
                        ++count;black+=m_nativeCardSamples[index].luma<=componentCardLimit;
                    }
                if(!count || black*100<count*85)return;
            }
            nativeCapture[selected]=Union(nativeCapture[selected],component);
        };
        for(int pass=0;pass<4 && nativeSamples<NativeSampleLimit;++pass) {
            const auto previous=nativeCapture[selected];
            const int left=(std::max)(roi.left,previous.left-NativeHop);
            const int right=(std::min)(roi.right,previous.right+NativeHop);
            const int topRow=(std::max)(roi.top,previous.top-NativeHop);
            const int bottomRow=(std::min)(roi.bottom,previous.bottom+NativeHop);
            // Terminal punctuation first, before the longer horizontal strips.
            for(int yy=topRow;yy<bottomRow;++yy) {
                for(int xx=left;xx<(std::min)(right,previous.left);++xx)inspect(xx,yy);
                for(int xx=(std::max)(left,previous.right);xx<right;++xx)inspect(xx,yy);
            }
            for(int yy=topRow;yy<(std::min)(bottomRow,previous.top);++yy)
                for(int xx=(std::max)(left,previous.left);xx<(std::min)(right,previous.right);++xx)inspect(xx,yy);
            for(int yy=(std::max)(topRow,previous.bottom);yy<bottomRow;++yy)
                for(int xx=(std::max)(left,previous.left);xx<(std::min)(right,previous.right);++xx)inspect(xx,yy);
            const auto& now=nativeCapture[selected];
            if(now.left==previous.left && now.top==previous.top && now.right==previous.right && now.bottom==previous.bottom)break;
        }
    }
    const bool actuallyCrosses=box.top*step<pictureTop || box.bottom*step>pictureBottom;
    m_nearBarEligibilityMeasured=true;
    m_nearBarEligible=actuallyCrosses || (nearBarDistance>0 && m_diagnosticNearBarGap>=0 &&
        m_diagnosticNearBarGap<=nearBarDistance);
    if(actuallyCrosses){m_diagnosticNearBarGap=0;m_diagnosticNearBarGapInferred=false;}
    if(!m_nearBarEligible){m_diagnosticReason="outside-nearbar";return false;}
    // A small tolerant visual signature is used only for cue identity. All
    // line extents are still inspected on every frame, even after geometry locks.
    auto belongs=[&](int pixel,int selectedLine) {
        const int y=pixel/w,x=pixel%w,sy=evidence.sourceRows[y];
        if(completedCard.Valid() && x*step>=completedCard.left && x*step<completedCard.right &&
            sy>=completedCard.top && sy<completedCard.bottom && evidence.Get(x,y,true)) {
            if(selectedLine<0)return true;
            const auto& row=lineBoxes[selectedLine];
            return x>=row.left && x<row.right && y>=row.top && y<row.bottom;
        }
        const auto owner=componentOwners[m_labels[pixel]];
        if(!owner) return false;
        if(selectedLine>=0) return owner==lineIndices[selectedLine]+1;
        for(int i=0;i<lineCount;++i) if(owner==lineIndices[i]+1) return true;
        return false;
    };
    for(int y=box.top;y<box.bottom;++y) for(int x=box.left;x<box.right;++x) {
        const int pixel=y*w+x;
        if(belongs(pixel,-1)) evidence.ownedInk[pixel/64]|=uint64_t{1}<<(pixel%64);
    }
    auto makeSignature=[&](const SubtitleBoxRect& extent,std::array<uint64_t,16>& value,int selectedLine){
        value={};
        for(int y=extent.top;y<extent.bottom;++y) for(int x=extent.left;x<extent.right;++x)
            if(belongs(y*w+x,selectedLine))
            {
                const int sy=std::min(15,(y-extent.top)*16/Height(extent));
                const int sx=std::min(63,(x-extent.left)*64/Width(extent));
                value[sy]|=uint64_t{1}<<sx;
            }
    };
    makeSignature(box,signature,-1);
    makeSignature(a.box,m_currentAnchorSignature,0);
    auto makeLineSignature=[&](const SubtitleBoxRect& extent,SubtitleLineSignature& value,int selectedLine){
        std::array<uint16_t,64*16> inkCounts{},sampleCounts{};
        for(int y=extent.top;y<extent.bottom;++y) for(int x=extent.left;x<extent.right;++x)
        {
            const int sy=std::min(15,(y-extent.top)*16/Height(extent));
            const int sx=std::min(63,(x-extent.left)*64/Width(extent));
            const int cell=sy*64+sx;
            ++sampleCounts[cell];
            inkCounts[cell]+=belongs(y*w+x,selectedLine);
        }
        for(size_t cell=0;cell<value.size();++cell)
            value[cell]=sampleCounts[cell] ? static_cast<uint8_t>(
                (inkCounts[cell]*255+sampleCounts[cell]/2)/sampleCounts[cell]) : 0;
    };
    for(int i=0;i<lineCount;++i)
    {
        const auto& line=lineBoxes[i];
        makeLineSignature(line,m_currentLineSignatures[i],i);
        m_currentLineBounds[i]=nativeCapture[i];
        // A translucent backing can supply a broad, stable top edge. Reuse
        // the same reduced-resolution luma grid already sampled for glyphs;
        // this never searches new source pixels or grants subtitle eligibility.
        const int lineHeight=Height(line);
        const bool nearTop=line.top<topEnd;
        const int bandStart=nearTop?1:bottomStart+1;
        const int bandEnd=nearTop?topEnd-2:h-2;
        const int lowY=std::max(bandStart,line.top-2*lineHeight);
        // Stay clear of the glyph's own outline and the picture/bar boundary;
        // neither is evidence for a separate translucent backing.
        const int highY=std::min(bandEnd,line.top-std::max(3,lineHeight/2));
        unsigned bestSupport=0;
        for(int y=highY;y>=lowY;--y) {
            if(evidence.sourceRows[y-1]<pictureTop ||
                evidence.sourceRows[y+1]>=pictureBottom)
                continue;
            unsigned tested=0,supported=0,positiveDrop=0;
            for(int x=line.left;x<line.right;++x) {
                const int drop=int(m_luma[(y-1)*w+x])-int(m_luma[(y+1)*w+x]);
                ++tested;
                if(drop>=48) {++supported;positiveDrop+=unsigned(drop);}
            }
            if(tested<8 || supported*10000<tested*4500 ||
                positiveDrop<supported*60)continue;
            const unsigned strength=supported*10000/tested;
            if(strength>bestSupport) {
                m_currentPanelTopEdges[i]={evidence.sourceRows[y],
                    m_currentLineBounds[i].left,m_currentLineBounds[i].right,strength};
                bestSupport=strength;
            }
        }
    }
    // Include unsampled antialias fringes and tiny terminal punctuation. This
    // conservative envelope is not glyph evidence and cannot grant bar entry.
    const int padding=std::max({4,source.height/270,step*2+2});
    box={std::max(0,box.left*step-padding),std::max(0,box.top*step-padding),
        std::min(source.width,box.right*step+padding),std::min(source.height,box.bottom*step+padding)};
    return true;
}

SubtitleBoxResult SubtitleBoxDetector::Analyze(const AnalysisLumaSource& source,
    int top,int bottom,uint64_t sequence,uint64_t viewportGeneration)
{
    if(!source.IsValid() || source.width>8192 || source.height>4320 || top<0 ||
        bottom<=top || bottom>source.height || (top==0 && bottom==source.height))
        { Reset(); return {}; }
    if(source.generation!=m_generation || viewportGeneration!=m_viewport ||
        source.width!=m_width || source.height!=m_height || top!=m_top || bottom!=m_bottom ||
        sequence<m_sequence)
    {
        Reset(); m_generation=source.generation; m_viewport=viewportGeneration;
        m_width=source.width; m_height=source.height; m_top=top; m_bottom=bottom;
    }
    if(m_hasSequence && sequence==m_sequence) return m_result;
    m_sequence=sequence; m_hasSequence=true;
    m_sharedSampleCount=0;
    if(m_optimizationMode==2)m_sampledRows.assign((source.height+SamplingStep(source.width,source.height)-1)/SamplingStep(source.width,source.height),0);
    SubtitleBoxRect box; int lines=0; std::array<uint64_t,16> signature{};
    // Preserve the established overlap detector. Only widen nomination when
    // that detector finds no valid cue; near-picture noise cannot displace it.
    bool detected=Detect(source,top,bottom,box,lines,signature,0);
    if(!detected && m_nearBarDistance>0)
        detected=Detect(source,top,bottom,box,lines,signature,m_nearBarDistance);
    auto diagnostics=[&]() {
        m_result.diagnosticReason=detected?"detected":m_diagnosticReason;
        m_result.diagnosticComponentCount=m_diagnosticComponentCount;
        m_result.diagnosticNearBarGap=m_diagnosticNearBarGap;
        m_result.diagnosticNearBarGapInferred=m_diagnosticNearBarGapInferred;
        m_result.nearBarEligibilityMeasured=m_nearBarEligibilityMeasured;
        m_result.nearBarEligible=m_nearBarEligible;
    };
    if(!detected)
    {
        // Outline-only grace, explicitly reported as held. Never use this
        // diagnostic state as authorization to erase or move source pixels.
        // Loss of bar evidence releases immediately, even if picture-side text
        // remains. Grace is allowed only with a fresh accepted anchor inside the
        // old cue, never for unrelated new bar content elsewhere in the raster.
        const auto& prior=m_result.bounds;
        const bool anchorStillPresent=m_currentBarAnchor.Valid() && prior.Valid() &&
            m_currentBarAnchor.left>=prior.left && m_currentBarAnchor.top>=prior.top &&
            m_currentBarAnchor.right<=prior.right && m_currentBarAnchor.bottom<=prior.bottom;
        if(!m_workLimit && anchorStillPresent && ++m_misses<=2)
        { m_result.detected=false; m_result.held=true; m_result.revised=false;diagnostics();return m_result; }
        m_result={};m_result.workLimit=m_workLimit;diagnostics();return m_result;
    }
    int difference=0, total=0;
    for(size_t i=0;i<signature.size();++i)
    { difference+=Bits(signature[i]^m_signature[i]); total+=Bits(signature[i]|m_signature[i]); }
    const int tolerance=std::max(4,source.height/180);
    const auto& old=m_result.bounds;
    const int padding=std::max({4,source.height/270,SamplingStep(source.width,source.height)*2+2});
    const bool same=m_result.cue && lines==m_result.lineCount &&
        box.left+padding>=old.left && box.top+padding>=old.top &&
        box.right-padding<=old.right && box.bottom-padding<=old.bottom &&
        std::abs(box.left-old.left)<=tolerance && std::abs(box.right-old.right)<=tolerance &&
        std::abs(box.top-old.top)<=tolerance && std::abs(box.bottom-old.bottom)<=tolerance &&
        difference*100<=std::max(1,total)*30;
    const bool revised=m_result.cue && !same &&
        std::min(box.right,old.right)>std::max(box.left,old.left) &&
        std::min(box.bottom,old.bottom)>std::max(box.top,old.top);
    if(same) { ++m_result.observations; }
    else
    {
        m_result.bounds=box; m_result.cue=++m_nextCue; m_result.observations=1;
        m_result.lineCount=lines; m_signature=signature;
    }
    m_result.detected=true; m_result.held=false; m_result.revised=revised; m_result.workLimit=false;
    m_result.sourcePanel=m_currentSourcePanel;
    m_result.linePanels=m_currentLinePanels;
    m_result.capturePanel=m_currentCapturePanel;
    m_result.lineCapturePanels=m_currentLineCapturePanels;
    m_result.capturePanelMeasured=true;
    m_result.anchor=m_currentBarAnchor; m_result.signature=signature;
    m_result.anchorSignature=m_currentAnchorSignature;
    m_result.lineBounds=m_currentLineBounds; m_result.lineSignatures=m_currentLineSignatures;
    m_result.panelTopEdges=m_currentPanelTopEdges;
    m_misses=0;diagnostics();return m_result;
}
