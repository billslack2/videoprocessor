#pragma once
#include <SubtitleBoxLookahead.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

struct SubtitleBoxPadding
{
    int sides = 60, top = 40, bottom = 21;
    SubtitleBoxPadding() = default;
    SubtitleBoxPadding(int uniform) : sides(uniform), top(uniform), bottom(uniform) {}
    SubtitleBoxPadding(int horizontal, int above, int below) : sides(horizontal), top(above), bottom(below) {}
    bool Valid() const { return sides >= 0 && top >= 0 && bottom >= 0; }
    bool operator==(const SubtitleBoxPadding& other) const {
        return sides == other.sides && top == other.top && bottom == other.bottom;
    }
};

// All rectangles and distances use half-open, unscaled source-raster pixels.
// The caller supplies an accepted current cue and fresh picture authority.
inline SubtitleBoxRect ExpandSubtitleBox(const SubtitleBoxRect& bounds,
    int width, int height, SubtitleBoxPadding padding = {})
{
    if (!bounds.Valid() || width <= 0 || height <= 0 || !padding.Valid() ||
        bounds.right <= 0 || bounds.bottom <= 0 || bounds.left >= width || bounds.top >= height)
        return {};
    auto clip = [](int64_t value, int limit) {
        return static_cast<int>((std::max)(int64_t(0), (std::min)(int64_t(limit), value)));
    };
    return {clip(int64_t(bounds.left) - padding.sides, width),
        clip(int64_t(bounds.top) - padding.top, height),
        clip(int64_t(bounds.right) + padding.sides, width),
        clip(int64_t(bounds.bottom) + padding.bottom, height)};
}

// Configured padding is retained where its margin is clear. Nearby sampled
// glyph ink limits expansion when the requested margin would cross that ink.
inline SubtitleBoxPadding LimitSubtitlePaddingAtGlyphInk(const SubtitleBoxRect& bounds,
    int width, int height, SubtitleBoxPadding requested, const SubtitleInkSnapshot* ink)
{
    if (!bounds.Valid() || width <= 0 || height <= 0 || !requested.Valid() ||
        !ink || ink->step <= 0 || ink->width <= 0 || ink->height <= 0 ||
        ink->sourceRows.size() != static_cast<size_t>(ink->height)) return requested;

    const int x0=(std::max)(0,bounds.left/ink->step);
    const int x1=(std::min)(ink->width,(bounds.right+ink->step-1)/ink->step);
    const int y0=(std::max)(0,bounds.top/ink->step);
    const int y1=(std::min)(ink->height,(bounds.bottom+ink->step-1)/ink->step);
    if(x0>=x1 || y0>=y1) return requested;
    const int rowSupport=(std::max)(2,(std::min)(8,(x1-x0)/120));
    const int columnSupport=(std::max)(2,(std::min)(8,(y1-y0)/40));

    int nearestAbove=-1,nearestBelow=height+1;
    auto clipToRaster=[](int64_t value,int limit) {
        return static_cast<int>((std::max)(int64_t(0),(std::min)(int64_t(limit),value)));
    };
    const int topLimit=clipToRaster(int64_t(bounds.top)-requested.top,height);
    const int bottomLimit=clipToRaster(int64_t(bounds.bottom)+requested.bottom,height);
    for(int y=0;y<ink->height;++y) {
        const int sourceY=ink->sourceRows[static_cast<size_t>(y)];
        if(sourceY<topLimit || sourceY>=bottomLimit ||
            (sourceY>=bounds.top && sourceY<bounds.bottom)) continue;
        int support=0;
        for(int x=x0;x<x1;++x) support+=ink->Get(x,y);
        if(support<rowSupport) continue;
        if(sourceY<bounds.top) nearestAbove=(std::max)(nearestAbove,sourceY);
        else nearestBelow=(std::min)(nearestBelow,sourceY);
    }

    SubtitleBoxPadding safe=requested;
    if(nearestAbove>=0)
        safe.top=(std::min)(safe.top,(std::max)(0,bounds.top-nearestAbove-ink->step));
    if(nearestBelow<=height)
        safe.bottom=(std::min)(safe.bottom,(std::max)(0,nearestBelow-bounds.bottom));

    int nearestLeft=-1,nearestRight=width+1;
    const int leftLimit=clipToRaster(int64_t(bounds.left)-requested.sides,width);
    const int rightLimit=clipToRaster(int64_t(bounds.right)+requested.sides,width);
    for(int x=0;x<ink->width;++x) {
        const int sourceX=x*ink->step;
        if(sourceX<leftLimit || sourceX>=rightLimit ||
            (sourceX>=bounds.left && sourceX<bounds.right)) continue;
        int support=0;
        for(int y=y0;y<y1;++y) support+=ink->Get(x,y);
        if(support<columnSupport) continue;
        if(sourceX<bounds.left) nearestLeft=(std::max)(nearestLeft,sourceX);
        else nearestRight=(std::min)(nearestRight,sourceX);
    }
    if(nearestLeft>=0)
        safe.sides=(std::min)(safe.sides,(std::max)(0,bounds.left-nearestLeft-ink->step));
    if(nearestRight<=width)
        safe.sides=(std::min)(safe.sides,(std::max)(0,nearestRight-bounds.right));
    return safe;
}

// The accepted per-line measurements own extraction. The overall cue bounds
// may be frozen by tracking, so they must not clip current verified glyphs or
// their antialias fringe. Picture capture still requires measured card ownership;
// this guard cannot grant subtitle/bar eligibility or change display padding.
inline std::array<SubtitleBoxRect,3> SubtitleGlyphCaptureLines(
    const SubtitleBoxResult& text,int width,int height)
{
    std::array<SubtitleBoxRect,3> lines{};
    const int leading=SubtitleBoxDetector::SamplingStep(width,height)-1;
    constexpr int fringe=2; // Native antialias pixels; never configured display padding.
    for(int i=0;i<text.lineCount && i<3;++i) {
        const auto& line=text.lineBounds[i];
        if(!line.Valid())continue;
        lines[i]={(std::max)(0,line.left-leading-fringe),
            (std::max)(0,line.top-leading),
            (std::min)(width,line.right+fringe),
            (std::min)(height,line.bottom)};
    }
    return lines;
}
inline SubtitleBoxRect SubtitleGlyphCaptureBounds(const SubtitleBoxResult& text,int width,int height)
{
    SubtitleBoxRect glyphs{};
    for(const auto& line:SubtitleGlyphCaptureLines(text,width,height)) {
        if(!line.Valid())continue;
        if(!glyphs.Valid())glyphs=line;
        else glyphs={(std::min)(glyphs.left,line.left),(std::min)(glyphs.top,line.top),
            (std::max)(glyphs.right,line.right),(std::max)(glyphs.bottom,line.bottom)};
    }
    return glyphs.Valid()?glyphs:ExpandSubtitleBox(text.bounds,width,height,0);
}

// Percentages apply to one typical glyph row, never the multiline block.
// Fixed mode preserves the historical source-pixel settings and fallback.
struct SubtitlePaddingPolicy
{
    bool glyphRelative=false;
    int sidesPercent=75,topPercent=35,bottomPercent=25;
    int minimumHeadroomPixels=0;
    bool operator==(const SubtitlePaddingPolicy& b) const {
        return glyphRelative==b.glyphRelative && sidesPercent==b.sidesPercent &&
            topPercent==b.topPercent && bottomPercent==b.bottomPercent &&
            minimumHeadroomPixels==b.minimumHeadroomPixels;
    }
};
inline int SubtitleTypicalLineHeight(const SubtitleBoxResult& text)
{
    std::array<int,3> heights{};int count=0;
    for(int i=0;i<text.lineCount && i<3;++i) {
        const auto& line=text.lineBounds[i];
        if(line.Valid()) heights[count++]=line.bottom-line.top;
    }
    if(count==0) return 0;
    std::sort(heights.begin(),heights.begin()+count);
    // Detached punctuation may be grouped into a short row. Keep it in the
    // capture envelope, but do not let it define the font's padding scale.
    const int floor=(heights[count-1]+1)/2;
    int first=0;while(first<count-1 && heights[first]<floor) ++first;
    return heights[first+(count-first)/2];
}
inline SubtitleBoxPadding SubtitleRelativePadding(int height,const SubtitlePaddingPolicy& policy)
{
    auto scale=[height](int percent) {
        return int((std::min)(int64_t(500),(std::max)(int64_t(0),
            (int64_t(height)*(std::max)(0,(std::min)(200,percent))+99)/100)));
    };
    return {scale(policy.sidesPercent),scale(policy.topPercent),scale(policy.bottomPercent)};
}

// Establish a guarded margin from the acquisition window, then freeze it for
// the unchanged cue. Moving scenery must neither shrink nor restore that margin.
class SubtitlePaddingGuard
{
public:
    void Reset()
    {
        m_reference={};m_safe={};m_requested={};m_cue=0;m_policy=0;m_continuity=0;
        m_valid=false;m_glyphHeight=0;m_displayBounds={};m_scalePolicy={};
    }
    int GlyphHeight() const { return m_glyphHeight; }
    SubtitleBoxRect DisplayBounds() const { return m_displayBounds; }
    SubtitleBoxPadding Consume(const SubtitleBoxResult& text,const SubtitleBoxPreview& preview,
        SubtitleBoxPadding requested,SubtitlePaddingPolicy scalePolicy={})
    {
        if(!preview.available || (!text.detected && !text.held) || !text.bounds.Valid()) {
            Reset();return requested;
        }
        const bool same=m_valid && m_cue==text.cue && m_requested==requested && m_scalePolicy==scalePolicy &&
            m_policy==preview.policyGeneration && m_continuity==preview.continuityGeneration &&
            !preview.current.discontinuity && SameCueContext(m_reference,preview.current);
        if(!same) {
            m_requested=requested;m_scalePolicy=scalePolicy;m_cue=text.cue;
            m_policy=preview.policyGeneration;m_continuity=preview.continuityGeneration;
            m_valid=true;m_glyphHeight=0;
            m_displayBounds=scalePolicy.glyphRelative
                ? SubtitleGlyphCaptureBounds(text,preview.current.width,preview.current.height) : text.bounds;
            if(scalePolicy.glyphRelative) {
                std::array<int,6> heights{};int count=0;
                auto measure=[&](const SubtitleBoxResult& measured) {
                    const int h=SubtitleTypicalLineHeight(measured);
                    if(h>0 && count<int(heights.size()))heights[count++]=h;
                };
                measure(text);
                // No extra latency or scan: use the already queued, consecutive
                // observations of this cue. An unrelated future cue never votes.
                for(unsigned i=0;i<preview.followingCount && i<preview.following.size();++i) {
                    const auto& next=preview.following[i];
                    if(!next.analyzed || next.discontinuity ||
                        next.identity.acceptedSequence!=preview.current.identity.acceptedSequence+i+1 ||
                        !SubtitleBoxLookahead::SameCue(preview.current,next))break;
                    measure(next.text);
                }
                if(count>0) {
                    std::sort(heights.begin(),heights.begin()+count);
                    m_glyphHeight=heights[count/2];
                    requested=SubtitleRelativePadding(m_glyphHeight,scalePolicy);
                }
            }
            m_safe=requested;
            const auto include=[&](const SubtitleBoxObservation& observation) {
                if(!observation.text.detected || !observation.ink) return;
                const auto measured=LimitSubtitlePaddingAtGlyphInk(m_displayBounds,
                    observation.width,observation.height,requested,observation.ink.get());
                m_safe.sides=(std::min)(m_safe.sides,measured.sides);
                m_safe.top=(std::min)(m_safe.top,measured.top);
                m_safe.bottom=(std::min)(m_safe.bottom,measured.bottom);
            };
            include(preview.current);
            // Only consecutive queued observations of this cue participate.
            // Future captions, scene/context changes and seeks cannot set its margin.
            for(unsigned i=0;i<preview.followingCount && i<preview.following.size();++i) {
                const auto& next=preview.following[i];
                if(!next.analyzed || next.discontinuity ||
                    next.identity.acceptedSequence!=preview.current.identity.acceptedSequence+i+1 ||
                    !SubtitleBoxLookahead::SameCue(preview.current,next)) break;
                include(next);
            }
        }
        if(same && scalePolicy.glyphRelative && text.revised) {
            // Only an expansion already confirmed by cue tracking may resize
            // an acquired display box. Raw measurement jitter cannot breathe.
            auto current=SubtitleGlyphCaptureBounds(text,preview.current.width,preview.current.height);
            // The tracker may confirm detached extension ink before line grouping
            // incorporates it. Its revised accepted bounds are also display proof.
            if(current.Valid()) current={(std::min)(current.left,text.bounds.left),
                (std::min)(current.top,text.bounds.top),(std::max)(current.right,text.bounds.right),
                (std::max)(current.bottom,text.bounds.bottom)};
            if(current.Valid()) m_displayBounds={
                (std::min)(m_displayBounds.left,current.left),(std::min)(m_displayBounds.top,current.top),
                (std::max)(m_displayBounds.right,current.right),(std::max)(m_displayBounds.bottom,current.bottom)};
        }
        // A display-only floor above the frozen glyph envelope. Nearby ink can
        // limit detection/capture padding, but must not remove requested visual
        // headroom. Keyed extraction retains its independent per-line masks.
        m_safe.top=(std::max)(m_safe.top,(std::max)(0,(std::min)(200,scalePolicy.minimumHeadroomPixels)));
        m_reference=preview.current;
        return m_safe;
    }
private:
    static bool SameCueContext(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b)
    {
        const auto& x=a.identity;const auto& y=b.identity;
        return x.transportGeneration==y.transportGeneration &&
            x.sourceFormatGeneration==y.sourceFormatGeneration &&
            x.viewportGeneration==y.viewportGeneration && x.rendererGeneration==y.rendererGeneration &&
            a.width==b.width && a.height==b.height &&
            (a.pictureTop>0)==(b.pictureTop>0) &&
            (a.pictureBottom<a.height)==(b.pictureBottom<b.height) &&
            std::abs(a.pictureTop-b.pictureTop)<(std::max)(1,a.height/180) &&
            std::abs(a.pictureBottom-b.pictureBottom)<(std::max)(1,a.height/180);
    }
    SubtitleBoxObservation m_reference;
    SubtitleBoxPadding m_safe,m_requested;
    uint64_t m_cue=0,m_policy=0,m_continuity=0;
    bool m_valid=false;
    int m_glyphHeight=0;
    SubtitleBoxRect m_displayBounds;
    SubtitlePaddingPolicy m_scalePolicy;
};

struct SubtitleCutPasteGeometry
{
    SubtitleBoxRect source, destination, content, generatedCleanup, pictureCapture;
    std::array<SubtitleBoxRect,3> glyphLines{};
    // Display-only mapping, applied after stable placement. Source ownership and
    // source cleanup stay in the original raster. Default keeps the exact old path.
    float glyphScale=1.0f,glyphTranslateX=0.0f,glyphTranslateY=0.0f;
    SubtitleBoxRect placementLimits, layoutContent, layoutCoverage;
    bool centeredGlyphMapping=false, roundedCorners=true, extendToBar=true, pictureIntrusion=false;
    int pictureTop = 0, pictureBottom = 0;
    bool valid = false;
    bool fromTopBar = false;
};

inline SubtitleCutPasteGeometry ComputeSubtitleCutPaste(const SubtitleBoxRect& bounds,
    int width, int height, int pictureTop, int pictureBottom, SubtitleBoxPadding padding = {}, int gap = 0,
    const SubtitleBoxRect* sourcePanel = nullptr, int nearBarDistance=0,
    const SubtitleBoxRect* capturePanel = nullptr, bool verifiedNearBarEligible = false)
{
    SubtitleCutPasteGeometry result;
    result.pictureTop=pictureTop;result.pictureBottom=pictureBottom;
    result.placementLimits={0,pictureTop,width,pictureBottom};
    if (width <= 0 || height <= 0 || pictureTop < 0 || pictureBottom > height ||
        pictureBottom <= pictureTop || !padding.Valid() || gap < 0 || !bounds.Valid() ||
        (nearBarDistance==0 && bounds.top >= pictureTop && bounds.bottom <= pictureBottom))
        return result;
    result.content = ExpandSubtitleBox(bounds, width, height, 0);
    result.layoutContent=result.content;
    result.source = ExpandSubtitleBox(bounds, width, height, padding);
    if (!result.source.Valid()) return result;
    // Display width must cover the measured opaque card, even when its blank
    // side margins exceed the configured glyph padding. This is not ink authority.
    if(sourcePanel && sourcePanel->Valid()) {
        result.source.left=(std::min)(result.source.left,(std::max)(0,sourcePanel->left));
        result.source.right=(std::max)(result.source.right,(std::min)(width,sourcePanel->right));
    }
    // Glyph keying may sample picture pixels only on the measured opaque card.
    // The conservative glyph envelope and display padding are not capture authority.
    const auto* capture=capturePanel ? capturePanel : sourcePanel;
    result.pictureCapture=capture ? SubtitleBoxRect{} : SubtitleBoxRect{0,pictureTop,width,pictureBottom};
    if(capture && capture->Valid()) {
        // Native card proof can include the few boundary rows where the
        // subtitle and AR edge estimates differ. Preserve that proof; clipping
        // it to the old estimate would cut real crossing glyphs after docking.
        result.pictureCapture={(std::max)(0,capture->left),
            (std::max)(capturePanel?0:pictureTop,capture->top),
            (std::min)(width,capture->right),
            (std::min)(capturePanel?height:pictureBottom,capture->bottom)};
        if(!result.pictureCapture.Valid()) result.pictureCapture={};
    }
    // The detected source card and the display margin are independent. Legacy
    // geometry-only callers retain their padded cleanup; live presentation
    // always supplies the measured panel, even when it is empty (bar-only cue).
    // A glyph envelope contains sampling fringe, not proof that those picture
    // pixels belonged to the original opaque card. Only measured coverage can
    // authorize live picture restoration; an absent panel authorizes none.
    const auto cleanup = sourcePanel ? *sourcePanel : result.source;
    result.generatedCleanup = {(std::max)(0,cleanup.left),
        (std::max)(cleanup.top,pictureTop),
        (std::min)(width,cleanup.right),
        (std::min)(cleanup.bottom,pictureBottom)};
    // Once an opaque near-bar card is accepted, cover the short picture gap
    // to the physical bar. This is backing/cleanup only, never glyph authority.
    if(result.generatedCleanup.Valid() && nearBarDistance>0) {
        if(pictureBottom<height && result.generatedCleanup.bottom>=pictureBottom-nearBarDistance)
            result.generatedCleanup.bottom=pictureBottom;
        if(pictureTop>0 && result.generatedCleanup.top<=pictureTop+nearBarDistance)
            result.generatedCleanup.top=pictureTop;
    }
    if (!result.generatedCleanup.Valid()) result.generatedCleanup = {};
    result.layoutCoverage=result.generatedCleanup;
    result.pictureIntrusion=result.generatedCleanup.Valid() ||
        (bounds.top<pictureBottom && bounds.bottom>pictureTop);
    // Classify with detected content, never padding. A top cue moves down;
    // a bottom cue moves up. Do not combine opposite-bar cues into one move.
    // Detector admission may use bounded black support when the backing edge
    // blends into black scenery. Use that authority only to choose placement;
    // never enlarge pictureCapture or generatedCleanup to make it qualify.
    const int topGap=pictureTop>0?bounds.top-pictureTop:(std::numeric_limits<int>::max)();
    const int bottomGap=pictureBottom<height?pictureBottom-bounds.bottom:(std::numeric_limits<int>::max)();
    const bool fromTop=pictureTop>0 && (bounds.top<pictureTop || (nearBarDistance>0 &&
        (verifiedNearBarEligible?topGap<bottomGap:sourcePanel && sourcePanel->Valid() && sourcePanel->top<=pictureTop+nearBarDistance)));
    const bool fromBottom=pictureBottom<height && (bounds.bottom>pictureBottom || (nearBarDistance>0 &&
        (verifiedNearBarEligible?bottomGap<topGap:sourcePanel && sourcePanel->Valid() && sourcePanel->bottom>=pictureBottom-nearBarDistance)));
    result.fromTopBar=fromTop;
    if(fromTop==fromBottom) return result;
    // Near-bar text can already be entirely inside the picture. Extend its
    // display-only bar-facing margin so docking never pulls those glyphs back
    // toward the bar. Actual crossing captions still move into the picture.
    if(nearBarDistance>0 && bounds.top>=pictureTop && bounds.bottom<=pictureBottom) {
        if(fromTop) result.source.top=(std::min)(result.source.top,pictureTop);
        else result.source.bottom=(std::max)(result.source.bottom,pictureBottom);
    }
    const int64_t panelHeight=int64_t(result.source.bottom)-result.source.top;
    const int64_t top=fromTop ? int64_t(pictureTop)+gap : int64_t(pictureBottom)-gap-panelHeight;
    const int64_t bottom=top+panelHeight;
    if (top < pictureTop || bottom > pictureBottom || bottom <= top ||
        (nearBarDistance==0 && (fromTop ? top<=result.source.top : top>=result.source.top)))
        return result;
    result.destination = {result.source.left, static_cast<int>(top),
        result.source.right, static_cast<int>(bottom)};
    result.valid = true;
    return result;
}

// Keep the complete moved panel inside the source rectangle selected by the
// normal auto-aspect/zoom path. Detection and original-card cleanup coordinates
// remain in the full source raster; only destination placement is constrained.
inline SubtitleCutPasteGeometry FitSubtitleToVisiblePicture(SubtitleCutPasteGeometry g,
    SubtitleBoxRect visible, int gap=0)
{
    if(!g.valid || !visible.Valid() || gap<0) { g.valid=false;return g; }
    // Reconcile only the few raster rows where the independent subtitle edge
    // differs from the AR owner's physical edge. These are composition-local
    // coordinates: neither detection nor AR state is changed. A real viewport
    // crop farther inside the picture remains placement-only. Using the physical
    // edge also prevents a bright picture sliver from bypassing card ownership
    // through the shader's unconditional black-bar glyph path.
    constexpr int edgeTolerance=3;
    if(std::abs(visible.top-g.pictureTop)<=edgeTolerance) {
        // Cleanup attached to the sampled bar must meet the physical bar too.
        // Otherwise its last row bypasses reconstruction and enters the blur
        // as live picture/backing. This never enlarges glyph ownership.
        if(g.generatedCleanup.Valid() && g.generatedCleanup.top==g.pictureTop)
            g.generatedCleanup.top=visible.top;
        g.pictureTop=visible.top;
    }
    if(std::abs(visible.bottom-g.pictureBottom)<=edgeTolerance) {
        if(g.generatedCleanup.Valid() && g.generatedCleanup.bottom==g.pictureBottom)
            g.generatedCleanup.bottom=visible.bottom;
        g.pictureBottom=visible.bottom;
    }
    visible.top=(std::max)(visible.top,g.pictureTop);
    visible.bottom=(std::min)(visible.bottom,g.pictureBottom);
    const int64_t w=int64_t(g.destination.right)-g.destination.left;
    const int64_t h=int64_t(g.destination.bottom)-g.destination.top;
    if(!visible.Valid() || w>int64_t(visible.right)-visible.left ||
        h+gap>int64_t(visible.bottom)-visible.top) {g.valid=false;return g;}
    const bool fromTop=g.fromTopBar;
    const int left=(std::max)(visible.left,(std::min)(g.destination.left,int(int64_t(visible.right)-w)));
    const int top=fromTop ? visible.top+gap : int(int64_t(visible.bottom)-gap-h);
    g.destination={left,top,int(left+w),int(top+h)};
    g.placementLimits=visible;
    // Reconciled physical bar edges can expose a few rows of accepted ink.
    // A deeper viewport crop does not change these physical edge coordinates.
    g.pictureIntrusion |= g.content.Valid() && g.content.top<g.pictureBottom && g.content.bottom>g.pictureTop;
    return g;
}

// The centered cue box owns padding and placement. Extension to the bar is
// backing only and never participates in the glyph pivot. Capture stays immutable.
inline SubtitleCutPasteGeometry ApplySubtitleTextReduction(SubtitleCutPasteGeometry g,
    int reductionPercent,SubtitleBoxPadding padding=SubtitleBoxPadding(0),
    bool roundedCorners=true,bool floatBackground=false)
{
    if(!g.valid || !g.source.Valid() || !g.destination.Valid())return g;
    const auto ink=g.layoutContent.Valid()?g.layoutContent:g.content;
    if(!ink.Valid()) {g.valid=false;return g;}
    const float scale=float(100-(std::max)(0,(std::min)(75,reductionPercent)))/100.0f;
    const int side=(std::max)(0,padding.sides);
    // Both sides meet the larger configured minimum, so visual centering does
    // not sacrifice top headroom or bottom clearance.
    const int vertical=(std::max)(0,(std::max)(padding.top,padding.bottom));
    int w=int(std::ceil(scale*(ink.right-ink.left)))+2*side;
    int h=int(std::ceil(scale*(ink.bottom-ink.top)))+2*vertical;
    const auto limits=g.placementLimits;
    auto cleanup=g.layoutCoverage.Valid()?g.layoutCoverage:g.generatedCleanup;
    if(limits.Valid() && cleanup.Valid())cleanup={
        (std::max)(cleanup.left,limits.left),(std::max)(cleanup.top,limits.top),
        (std::min)(cleanup.right,limits.right),(std::min)(cleanup.bottom,limits.bottom)};
    if(cleanup.Valid()) {w=(std::max)(w,cleanup.right-cleanup.left);h=(std::max)(h,cleanup.bottom-cleanup.top);}
    if(limits.Valid()) {
        const int availableHeight=g.fromTopBar?limits.bottom-g.destination.top:g.destination.bottom-limits.top;
        // If symmetric requested margins cannot fit, retain the accepted cue
        // with the largest equal margins that do fit; never discard it merely
        // because centering made an otherwise valid margin larger.
        if(std::ceil(scale*(ink.bottom-ink.top))>availableHeight ||
            std::ceil(scale*(ink.right-ink.left))>limits.right-limits.left) {g.valid=false;return g;}
        h=(std::min)(h,availableHeight);w=(std::min)(w,limits.right-limits.left);
    }
    const float centerX=(g.destination.left+g.destination.right)*0.5f;
    int left=int(std::floor(centerX-w*0.5f)),right=left+w;
    if(cleanup.Valid()) {left=(std::min)(left,cleanup.left);right=(std::max)(right,cleanup.right);w=right-left;}
    if(limits.Valid()) {
        if(w>limits.right-limits.left) {g.valid=false;return g;}
        left=(std::max)(limits.left,(std::min)(left,limits.right-w));right=left+w;
    }
    // Existing placement supplies the bar-facing box edge, already offset.
    int top=g.fromTopBar?g.destination.top:g.destination.bottom-h;
    int bottom=g.fromTopBar?top+h:g.destination.bottom;
    if(cleanup.Valid()) {
        if(g.fromTopBar)bottom=(std::max)(bottom,cleanup.bottom);
        else top=(std::min)(top,cleanup.top);
    }
    if(limits.Valid() && (top<limits.top || bottom>limits.bottom)) {g.valid=false;return g;}
    g.destination={left,top,right,bottom};
    g.glyphScale=scale;g.centeredGlyphMapping=true;
    g.glyphTranslateX=(left+right)*0.5f-scale*(ink.left+ink.right)*0.5f;
    g.glyphTranslateY=(top+bottom)*0.5f-scale*(ink.top+ink.bottom)*0.5f;
    g.roundedCorners=roundedCorners;
    g.extendToBar=!floatBackground || g.pictureIntrusion || g.generatedCleanup.Valid();
    return g;
}

// Signed edge exclusion consumed by the existing Smart HDR analysis policy.
// Positive protects the lower edge; negative protects the upper edge. Include
// both the displayed overlay and vacated backing, without moving picture geometry.
inline float SubtitleHdrProtectionPixels(const SubtitleCutPasteGeometry& g, bool composed,
    int analysisPictureTop,int analysisPictureBottom)
{
    if(!composed || !g.valid || analysisPictureBottom<=analysisPictureTop)return 0.0f;
    if(g.fromTopBar) {
        const int end=g.generatedCleanup.Valid()?(std::max)(g.destination.bottom,g.generatedCleanup.bottom):g.destination.bottom;
        return -float((std::max)(0,(std::min)(end,analysisPictureBottom)-analysisPictureTop));
    }
    const int start=g.generatedCleanup.Valid()?(std::min)(g.destination.top,g.generatedCleanup.top):g.destination.top;
    return float((std::max)(0,analysisPictureBottom-(std::max)(start,analysisPictureTop)));
}

struct SubtitleCutPasteSample
{
    int x, y;
    bool clear = false;
};

// CPU oracle for the historical unscaled copy operation (glyphScale == 1).
// Reduced glyphs instead use keyed area filtering in the shader.
// Always read the immutable CURRENT frame;
// destination wins where the source and destination rectangles overlap.
inline SubtitleCutPasteSample MapSubtitleCutPastePixel(
    const SubtitleCutPasteGeometry& geometry, int x, int y)
{
    auto contains = [x,y](const SubtitleBoxRect& r) {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    };
    if (!geometry.valid) return {x,y,false};
    if (contains(geometry.destination))
        return {x - geometry.destination.left + geometry.source.left,
            y - geometry.destination.top + geometry.source.top, false};
    return {x,y,contains(geometry.source)};
}

// Geometry alone is cached. The renderer samples fresh pixels every frame.
// A one-row independently proved bar fringe cannot move an established cue.
class SubtitleCutPastePresentation
{
public:
    void Reset() { m_geometry={};m_reference={};m_cue=0;m_policy=m_continuity=0;m_sourcePanel={};m_relativeDisplay=false; }
    SubtitleCutPasteGeometry Consume(const SubtitleBoxResult& text,
        const SubtitleBoxPreview& preview, SubtitleBoxPadding padding={}, int gap=0,
        const SubtitleBoxRect* displayBounds=nullptr)
    {
        if (!text.detected || !text.bounds.Valid() || !preview.available ||
            !preview.current.analyzed || !SubtitleBoxLookahead::HasBarEvidence(preview.current) || text.workLimit ||
            (text.nearBarEligibilityMeasured && !text.nearBarEligible)) {
            Reset();return {};
        }
        const auto& current=preview.current;
        const auto& display=displayBounds && displayBounds->Valid()?*displayBounds:text.bounds;
        const auto padded=ExpandSubtitleBox(display,current.width,current.height,padding);
        const auto content=SubtitleGlyphCaptureBounds(text,current.width,current.height);
        const auto glyphLines=SubtitleGlyphCaptureLines(text,current.width,current.height);
        const auto& capture=text.capturePanelMeasured?text.capturePanel:text.sourcePanel;
        SubtitleBoxRect captureInPicture{(std::max)(0,capture.left),
            (std::max)(text.capturePanelMeasured?0:current.pictureTop,capture.top),
            (std::min)(current.width,capture.right),
            (std::min)(text.capturePanelMeasured?current.height:current.pictureBottom,capture.bottom)};
        if(!captureInPicture.Valid())captureInPicture={};
        const bool sameCapture=(!captureInPicture.Valid() && !m_geometry.pictureCapture.Valid()) ||
            SubtitleBoxLookahead::SameLine(captureInPicture,m_geometry.pictureCapture,0);
        bool sameLines=true;
        for(size_t i=0;i<glyphLines.size();++i) {
            const auto& a=glyphLines[i];const auto& b=m_geometry.glyphLines[i];
            sameLines &= a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        }
        const auto& old=m_geometry.source;
        const auto& oldContent=m_geometry.content;
        const bool same=sameCapture && sameLines && m_relativeDisplay==(displayBounds!=nullptr) && m_geometry.valid && !current.discontinuity && m_cue==text.cue &&
            m_policy==preview.policyGeneration && m_continuity==preview.continuityGeneration &&
            m_padding==padding && m_gap==gap &&
            m_sourcePanel.left==text.sourcePanel.left && m_sourcePanel.top==text.sourcePanel.top &&
            m_sourcePanel.right==text.sourcePanel.right && m_sourcePanel.bottom==text.sourcePanel.bottom &&
            SubtitleBoxLookahead::SameContext(m_reference,current) &&
            oldContent.left==content.left && oldContent.top==content.top &&
            oldContent.right==content.right && oldContent.bottom==content.bottom &&
            old.left==padded.left && old.top==padded.top &&
            old.right==padded.right && old.bottom==padded.bottom;
        if (same && m_geometry.destination.top>=current.pictureTop &&
            m_geometry.destination.bottom<=current.pictureBottom) return m_geometry;
        const bool sameDisplayCue=m_relativeDisplay==(displayBounds!=nullptr) && m_geometry.valid && !current.discontinuity && m_cue==text.cue &&
            m_policy==preview.policyGeneration && m_continuity==preview.continuityGeneration &&
            m_padding==padding && m_gap==gap && SubtitleBoxLookahead::SameContext(m_reference,current);
        auto next=ComputeSubtitleCutPaste(display,current.width,current.height,
            current.pictureTop,current.pictureBottom,padding,gap,&text.sourcePanel,current.nearBarDistance,
            text.capturePanelMeasured?&text.capturePanel:nullptr,
            text.nearBarEligibilityMeasured && text.nearBarEligible);
        if(next.valid) {
            // The frozen display envelope controls appearance, never clipping.
            // Accepted current glyphs must fit even when their corrected bounds
            // exceed the acquired padding. Keep that envelope for the cue, while
            // current measured ownership alone still controls extraction/cleanup.
            auto envelope=next.source;
            if(content.Valid()) envelope={
                (std::min)(envelope.left,content.left),(std::min)(envelope.top,content.top),
                (std::max)(envelope.right,content.right),(std::max)(envelope.bottom,content.bottom)};
            if(sameDisplayCue) envelope={
                (std::min)(envelope.left,m_geometry.source.left),(std::min)(envelope.top,m_geometry.source.top),
                (std::max)(envelope.right,m_geometry.source.right),(std::max)(envelope.bottom,m_geometry.source.bottom)};
            next.destination.left-=next.source.left-envelope.left;
            next.destination.right+=envelope.right-next.source.right;
            const int extraHeight=(next.source.top-envelope.top)+(envelope.bottom-next.source.bottom);
            if(next.fromTopBar)next.destination.bottom+=extraHeight;
            else next.destination.top-=extraHeight;
            next.source=envelope;
            if(next.destination.top<current.pictureTop || next.destination.bottom>current.pictureBottom)
                next.valid=false;
        }
        // Freeze a display-only glyph envelope for the cue; current masks remain
        // authoritative for extraction. A cue that touched picture never floats
        // later merely because a weaker measurement temporarily lost its card.
        if(next.valid) {
            auto ink=next.layoutContent;
            if(content.Valid())ink={(std::min)(ink.left,content.left),(std::min)(ink.top,content.top),
                (std::max)(ink.right,content.right),(std::max)(ink.bottom,content.bottom)};
            if(sameDisplayCue && m_geometry.layoutContent.Valid())ink={
                (std::min)(ink.left,m_geometry.layoutContent.left),(std::min)(ink.top,m_geometry.layoutContent.top),
                (std::max)(ink.right,m_geometry.layoutContent.right),(std::max)(ink.bottom,m_geometry.layoutContent.bottom)};
            next.layoutContent=ink;
            if(sameDisplayCue && m_geometry.layoutCoverage.Valid()) {
                const auto old=m_geometry.layoutCoverage;
                next.layoutCoverage=next.layoutCoverage.Valid()?SubtitleBoxRect{
                    (std::min)(old.left,next.layoutCoverage.left),(std::min)(old.top,next.layoutCoverage.top),
                    (std::max)(old.right,next.layoutCoverage.right),(std::max)(old.bottom,next.layoutCoverage.bottom)}:old;
            }
            bool currentInkIntrusion=false;
            for(int line=0;line<text.lineCount && line<3;++line) {
                const auto inkLine=text.lineBounds[line];
                currentInkIntrusion |= inkLine.Valid() && inkLine.top<current.pictureBottom && inkLine.bottom>current.pictureTop;
            }
            next.pictureIntrusion=next.pictureIntrusion || currentInkIntrusion || (sameDisplayCue && m_geometry.pictureIntrusion);
        }
        m_geometry=next;
        m_geometry.content=content;m_geometry.glyphLines=glyphLines;
        m_reference=current;m_reference.ink.reset();m_reference.text={};
        m_cue=text.cue;m_policy=preview.policyGeneration;m_continuity=preview.continuityGeneration;
        m_padding=padding;m_gap=gap;m_sourcePanel=text.sourcePanel;m_relativeDisplay=displayBounds!=nullptr;
        return m_geometry;
    }
private:
    SubtitleCutPasteGeometry m_geometry;
    SubtitleBoxObservation m_reference;
    uint64_t m_cue=0,m_policy=0,m_continuity=0;
    SubtitleBoxRect m_sourcePanel;
    SubtitleBoxPadding m_padding;
    int m_gap=0;
    bool m_relativeDisplay=false;
};
