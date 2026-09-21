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
void SubtitleBoxDetector::Reset()
{
    m_result = {}; m_signature = {}; m_generation = m_viewport = m_sequence = 0;
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
    const int step = std::max(1, (source.width + 959) / 960);
    const int w = (source.width + step - 1) / step;
    const int h = (source.height + step - 1) / step;
    const int top = pictureTop / step, bottom = (pictureBottom + step - 1) / step;
    const int depth = std::max(24, h / 7);
    const int topEnd = pictureTop > 0 ? std::min(h,top+depth) : 0;
    const int bottomStart = pictureBottom < source.height ? std::max(0,bottom-depth) : h;
    const size_t area = static_cast<size_t>(w)*h;
    m_luma.resize(area); m_chroma.resize(area); m_mask.assign(area,0);
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
            const int sx=std::min(source.width-1,x*step), sy=std::min(source.height-1,y*step);
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
    if(brightCount<8) return false;
    cumulative=0; int ink=std::min(1023,bar+80);
    for(;ink<1023;++ink) { cumulative+=barHistogram[ink]; if(cumulative>=brightCount*3/4) break; }
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
    // Components preserve short words and punctuation until line grouping.
    // A hard component count caps noisy-frame work and yields no diagnostic.
    constexpr size_t MaxComponents=768;
    const int minimumHeight=std::max(3,h/240), maximumHeight=std::max(16,h/12);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        const int origin=y*w+x;
        if(m_mask[origin]!=1) continue;
        Line component{{x,y,x+1,y+1},0,1};
        m_flood.clear(); m_flood.push_back(origin); m_mask[origin]=2;
        for(size_t q=0;q<m_flood.size();++q)
        {
            const int i=m_flood[q], cy=i/w, cx=i-cy*w;
            ++component.ink;
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
        if(component.ink>=3 && ch<=maximumHeight && cw<=maximumHeight*3 &&
            (ch>=minimumHeight || (cw<=minimumHeight*2 && ch>=2)))
            m_components.push_back(component);
        if(m_components.size()>=MaxComponents) { m_workLimit=true; return false; }
    }
    std::sort(m_components.begin(),m_components.end(),[](const Line&a,const Line&b){
        return a.box.left<b.box.left; });
    for(const Line& c:m_components)
    {
        size_t best=m_lines.size(); int bestGap=w;
        for(size_t i=0;i<m_lines.size();++i)
        {
            const Line& l=m_lines[i];
            const int overlap=std::min(l.box.bottom,c.box.bottom)-std::max(l.box.top,c.box.top);
            const int gap=std::max(0,c.box.left-l.box.right);
            const int height=std::max(Height(l.box),Height(c.box));
            if(overlap>=std::max(1,std::min(Height(l.box),Height(c.box))/3) &&
                Height(Union(l.box,c.box))<=height*3/2+2 && gap<=height*2 && gap<bestGap)
                { best=i; bestGap=gap; }
        }
        if(best==m_lines.size()) m_lines.push_back(c);
        else { m_lines[best].box=Union(m_lines[best].box,c.box); m_lines[best].ink+=c.ink; ++m_lines[best].components; }
    }
    // A text line needs several strokes, a plausible baseline, and whitespace.
    auto plausible=[&](const Line& l){
        const int lw=Width(l.box),lh=Height(l.box);
        return l.components>=2 && lh>=minimumHeight && lw*10>=lh*13 &&
            lw<w*9/10 && l.ink>=12 && l.ink*100<lw*lh*82;
    };
    size_t anchor=m_lines.size(); int score=0;
    for(size_t i=0;i<m_lines.size();++i)
    {
        const auto& l=m_lines[i];
        if(!plausible(l)) continue;
        int barInk=0;
        for(int y=l.box.top;y<l.box.bottom;++y)
            if(y<top||y>=bottom)
                for(int x=l.box.left;x<l.box.right;++x) barInk+=m_mask[y*w+x]!=0;
        if(barInk>=6 && l.ink>score) { anchor=i; score=l.ink; }
    }
    if(anchor==m_lines.size()) return false;
    const Line& a=m_lines[anchor];
    const bool topCue=a.box.top<top;
    box=a.box; lineCount=1;
    const int anchorHeight=Height(a.box);
    // Companions are compared to the original bar anchor, never admitted by a
    // chain of unrelated picture/UI text. Gather the entire cue on its first frame.
    for(size_t i=0;i<m_lines.size();++i)
    {
        const Line& l=m_lines[i];
        if(i==anchor || !plausible(l)) continue;
        const int lh=Height(l.box);
        const int overlap=std::min(a.box.right,l.box.right)-std::max(a.box.left,l.box.left);
        const int gap=topCue?l.box.top-a.box.bottom:a.box.top-l.box.bottom;
        if(gap < -std::min(lh,anchorHeight)/3 || gap>anchorHeight*3 ||
            lh*2<anchorHeight || lh>anchorHeight*2 ||
            overlap<std::min(Width(a.box),Width(l.box))/3) continue;
        if(++lineCount>3) return false;
        box=Union(box,l.box);
    }
    // A small tolerant visual signature is used only for cue identity. All
    // line extents are still inspected on every frame, even after geometry locks.
    signature={};
    for(int y=box.top;y<box.bottom;++y) for(int x=box.left;x<box.right;++x)
        if(m_mask[y*w+x])
        {
            const int sy=std::min(15,(y-box.top)*16/Height(box));
            const int sx=std::min(63,(x-box.left)*64/Width(box));
            signature[sy]|=uint64_t{1}<<sx;
        }
    const int padding=std::max(4,source.height/270);
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
        if(!m_workLimit && m_result.bounds.Valid() && ++m_misses<=2)
        { m_result.detected=false; m_result.held=true; m_result.revised=false; return m_result; }
        m_result={}; m_result.workLimit=m_workLimit; return m_result;
    }
    int difference=0, total=0;
    for(size_t i=0;i<signature.size();++i)
    { difference+=Bits(signature[i]^m_signature[i]); total+=Bits(signature[i]|m_signature[i]); }
    const int tolerance=std::max(4,source.height/180);
    const auto& old=m_result.bounds;
    const int padding=std::max(4,source.height/270);
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
    m_misses=0; return m_result;
}
