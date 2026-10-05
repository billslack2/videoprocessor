#include "pch.h"
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <cmath>

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
}
int SubtitleBoxDetector::SamplingStep(int width, int height)
{
    return std::max({1, (width + 959) / 960, (height + 539) / 540});
}

void SubtitleBoxDetector::Reset()
{
    m_inkSnapshot.reset();
    m_result = {}; m_currentBarAnchor = {}; m_signature = {}; m_currentAnchorSignature = {};
    m_currentLineBounds = {}; m_currentLineSignatures = {}; m_currentPanelTopEdges = {};
    m_generation = m_viewport = m_sequence = 0;
    m_width = m_height = m_top = m_bottom = m_misses = 0;
    m_hasSequence = m_workLimit = false;
}

bool SubtitleBoxDetector::Detect(const AnalysisLumaSource& source, int pictureTop,
    int pictureBottom, SubtitleBoxRect& box, int& lineCount,
    std::array<uint64_t,16>& signature)
{
    // Inspect every source frame. Spatial sampling bounds cost without an idle
    // frame cadence that could postpone subtitle onset. Full line search remains
    // active throughout a cue, so an initially missed companion can be recovered.
    m_currentBarAnchor = {};
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
    const int depth = std::max(24, h / 7);
    const int topEnd = pictureTop > 0 ? std::min(h,top+depth) : 0;
    const int bottomStart = pictureBottom < source.height ? std::max(0,bottom-depth) : h;
    const size_t area = static_cast<size_t>(w)*h;
    m_luma.resize(area); m_chroma.resize(area); m_mask.assign(area,0);
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
            if(planar)
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
            if(sy<pictureTop || sy>=pictureBottom) { ++barHistogram[luma]; ++barSamples; }
        }
    }
    if(!barSamples) return false;
    uint32_t cumulative=0; int bar=0;
    for(;bar<1023;++bar) { cumulative+=barHistogram[bar]; if(cumulative>=barSamples/2) break; }
    // Learn the text level from the bar; the same level applies on both sides
    // of the boundary. No SDR-white/PQ-white split at the picture edge.
    uint32_t brightCount=0;
    for(int v=std::min(1023,bar+80);v<1024;++v) brightCount+=barHistogram[v];
    if(!brightCount) return false;
    cumulative=0; int ink=std::min(1023,bar+80);
    const uint32_t inkQuantile=(brightCount*3+3)/4;
    for(;ink<1023;++ink) { cumulative+=barHistogram[ink]; if(cumulative>=inkQuantile) break; }
    const int threshold=bar+std::max(64,(ink-bar)*70/100);
    // Learn the bar text's color rather than assuming all captions are white.
    // A similarly bright orange/blue picture edge is not evidence for that text.
    std::array<uint32_t,1024> histU{},histV{};uint32_t colors=0;
    for(int y=0;y<h;++y) if(y<top||y>=bottom) for(int x=0;x<w;++x)
        if(m_luma[y*w+x]>=threshold) { const auto uv=m_chroma[y*w+x];++histU[uv&1023];++histV[uv>>16];++colors; }
    auto median=[colors](const std::array<uint32_t,1024>& histogram){
        uint32_t count=0;for(int i=0;i<1024;++i) if((count+=histogram[i])>colors/2) return i;return 512; };
    const int inkU=median(histU), inkV=median(histV);
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
    for(size_t i=0;i<area;++i) if(m_mask[i]) evidence.rawInk[i/64]|=uint64_t{1}<<(i%64);
    // Components preserve short words and punctuation until line grouping.
    // A hard component count caps noisy-frame work and yields no diagnostic.
    constexpr size_t MaxComponents=768;
    const int minimumHeight=std::max(3,h/240), maximumHeight=std::max(16,h/12);
    // Word gaps can vary substantially between subtitle renderers and scripts.
    // Use a wider, scale-relative baseline corridor when joining components;
    // line width, baseline agreement, learned glyph height, and the independent
    // bar-anchor requirement still constrain which groups can form a cue.
    constexpr int SameLineGapGlyphHeights = 4;
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
        const bool punctuation=component.ink>=1 && ch<=2 && cw<=2;
        // A connected Arabic word can be several glyph-heights wide. Keep
        // scale tied to its vertical strokes, while allowing the line-level
        // checks below to reject broad scene regions. The old fixed
        // maximumHeight*3 ceiling cut valid joined words before they could be
        // grouped with the rest of their RTL line.
        const int maximumComponentWidth = (std::max)(maximumHeight*3,ch*8);
        if(punctuation || (component.ink>=3 && ch<=maximumHeight &&
            cw<=maximumComponentWidth && cw<w*9/10 &&
            (ch>=minimumHeight || (cw<=minimumHeight*2 && ch>=2))))
        {
            m_components.push_back(component);
            m_components.back().label=static_cast<uint16_t>(m_components.size());
            for(int pixel : m_flood) {
                m_mask[pixel] = 3;
                m_labels[pixel]=m_components.back().label;
            }
        }
        if(m_components.size()>=MaxComponents) { m_workLimit=true; return false; }
    }
    // Reconnect a detached dot to the stroke immediately above it (for example
    // '?' or '!'). Its baseline belongs to the complete glyph; using only the
    // upper component would discard it as an unrelated high picture stroke.
    const int dotLimit=std::max(2,h/180);
    std::array<bool,MaxComponents+1> compoundGlyph{};
    for(size_t dot=0;dot<m_components.size();) {
        const auto dotComponent=m_components[dot];
        if(Width(dotComponent.box)>dotLimit || Height(dotComponent.box)>dotLimit) {++dot;continue;}
        size_t head=m_components.size();int bestGap=maximumHeight;
        for(size_t i=0;i<m_components.size();++i) {
            const auto& c=m_components[i];
            const int overlap=std::min(c.box.right,dotComponent.box.right)-std::max(c.box.left,dotComponent.box.left);
            const int gap=dotComponent.box.top-c.box.bottom;
            if(i!=dot && !compoundGlyph[c.label] &&
                Height(c.box)>=std::max(minimumHeight,Height(dotComponent.box)*3) &&
                Width(c.box)<=Height(c.box)*2 && overlap>0 && gap>=0 &&
                gap<=std::max(2,Height(c.box)/2) && gap<bestGap &&
                Height(Union(c.box,dotComponent.box))<=maximumHeight) {head=i;bestGap=gap;}
        }
        if(head==m_components.size()) {++dot;continue;}
        auto& c=m_components[head];c.box=Union(c.box,dotComponent.box);c.ink+=dotComponent.ink;
        compoundGlyph[c.label]=true;
        c.topBarInk+=dotComponent.topBarInk;c.bottomBarInk+=dotComponent.bottomBarInk;
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
        if(c.topBarInk || c.bottomBarInk || (c.peakLuma>=coreThreshold &&
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
    // Learn character scale from components that actually enter a bar. Tiny
    // picture highlights must not seed a line and chain into real lettering.
    // Keep them for a bounded punctuation pass after substantial glyphs group.
    // Estimate scale from a plausible centered bar line, rather than every bar
    // component: a small corner title must not set a short subtitle's size.
    // Group across the full width so long captions retain their outer letters.
    struct ScaleLine { Line line; std::vector<int> heights; };
    std::vector<const Line*> barComponents;
    for(const auto& c:m_components)
        if((c.topBarInk || c.bottomBarInk) && Height(c.box)>=minimumHeight &&
            c.ink>=6 && Width(c.box)<=Height(c.box)*8)
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
                line.ink>=12)) && Width(line.box)<w*9/10 &&
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
            const int bandTop=y<topEnd?0:bottomStart;
            const int bandBottom=y<topEnd?topEnd-1:h-1;
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
            const int maximumComponentWidth=(std::max)(maximumHeight*3,ch*8);
            if(component.hasTextCore && component.ink>=3 && ch>=minimumHeight &&
                ch<=maximumHeight && cw<=maximumComponentWidth && cw<w*9/10)
            {
                component.label=static_cast<uint16_t>(m_components.size()+1);
                for(int pixel:m_flood) m_mask[pixel]=3;
                for(int pixel:m_flood) {
                    const size_t bit=static_cast<size_t>(pixel);
                    evidence.rawInk[bit/64]|=uint64_t{1}<<(bit%64);
                    m_labels[bit]=component.label;
                }
                m_components.push_back(component);
                if(m_components.size()>=MaxComponents) {m_workLimit=true;return false;}
            }
            else for(int pixel:m_flood) m_mask[pixel]=0;
        }
    }
    auto substantial=[&](const Line& c) {
        return c.hasTextCore && (!glyphHeight ? (c.ink>=3 && Height(c.box)>=minimumHeight) :
            (Height(c.box)>=(glyphHeight+1)/2 &&
            Height(c.box)<=glyphHeight*3/2+1 &&
            Width(c.box)<=glyphHeight*8));
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
        const bool horizontalDash=Width(c.box)>narrowMarkWidth &&
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
        const int minimumAspectTenths=barStrokeSupport[index]?8:13;
        return (l.components>=2 || oneConnectedWord) && lh>=minimumHeight &&
            lw*10>=lh*minimumAspectTenths &&
            lw<w*9/10 && l.ink>=12 && l.ink*100<lw*lh*82;
    };
    // The diagnostic targets centered movie subtitles. A title or player label
    // near either corner must not win the anchor search merely by having more
    // ink. Keep this independent of line width so short centered cues qualify.
    auto centered=[&](const Line& l){
        return std::abs(l.box.left+l.box.right-w)*5<=w*2;
    };
    size_t anchor=m_lines.size(); int score=0;
    for(size_t i=0;i<m_lines.size();++i)
    {
        const auto& l=m_lines[i];
        if(!plausible(l,i) || !centered(l)) continue;
        // Eligibility belongs to this line's accepted components, never all
        // bright pixels in its envelope (which includes discarded picture/noise).
        const bool inTopBar = (barStrokeSupport[i]&1)!=0;
        const bool inBottomBar = (barStrokeSupport[i]&2)!=0;
        if(inTopBar != inBottomBar && l.ink>score) { anchor=i; score=l.ink; }
    }
    if(anchor==m_lines.size()) return false;
    const Line& a=m_lines[anchor];
    m_currentBarAnchor={a.box.left*step,a.box.top*step,
        std::min(source.width,a.box.right*step),std::min(source.height,a.box.bottom*step)};
    box=a.box; lineCount=1;
    std::array<SubtitleBoxRect,3> lineBoxes{};
    std::array<size_t,3> lineIndices{};
    lineBoxes[0]=a.box;
    lineIndices[0]=anchor;
    const int anchorHeight=Height(a.box);
    // Companions are compared to the original bar anchor, never admitted by a
    // chain of unrelated picture/UI text. Search on both sides: the strongest
    // bar anchor may be the upper line crossing the lower picture boundary.
    for(size_t i=0;i<m_lines.size();++i)
    {
        const Line& l=m_lines[i];
        if(i==anchor || !plausible(l,i) || !centered(l)) continue;
        const int lh=Height(l.box);
        const int overlap=std::min(a.box.right,l.box.right)-std::max(a.box.left,l.box.left);
        const int gap=std::max(a.box.top,l.box.top)-std::min(a.box.bottom,l.box.bottom);
        if(gap < -std::min(lh,anchorHeight)/3 || gap>anchorHeight*3 ||
            lh*2<anchorHeight || lh>anchorHeight*2 ||
            std::abs(a.box.left+a.box.right-l.box.left-l.box.right)>std::max(lh,anchorHeight)*4 ||
            overlap<std::min(Width(a.box),Width(l.box))/3) continue;
        if(++lineCount>3) return false;
        lineBoxes[lineCount-1]=l.box;
        lineIndices[lineCount-1]=i;
        box=Union(box,l.box);
    }
    // A small tolerant visual signature is used only for cue identity. All
    // line extents are still inspected on every frame, even after geometry locks.
    auto belongs=[&](int pixel,int selectedLine) {
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
        m_currentLineBounds[i]={line.left*step,line.top*step,
            std::min(source.width,line.right*step),std::min(source.height,line.bottom*step)};
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
    SubtitleBoxRect box; int lines=0; std::array<uint64_t,16> signature{};
    if(!Detect(source,top,bottom,box,lines,signature))
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
        { m_result.detected=false; m_result.held=true; m_result.revised=false; return m_result; }
        m_result={}; m_result.workLimit=m_workLimit; return m_result;
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
    m_result.anchor=m_currentBarAnchor; m_result.signature=signature;
    m_result.anchorSignature=m_currentAnchorSignature;
    m_result.lineBounds=m_currentLineBounds; m_result.lineSignatures=m_currentLineSignatures;
    m_result.panelTopEdges=m_currentPanelTopEdges;
    m_misses=0; return m_result;
}
