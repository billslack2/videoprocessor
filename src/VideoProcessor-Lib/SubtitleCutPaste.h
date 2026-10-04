#pragma once
#include <SubtitleBoxLookahead.h>
#include <algorithm>
#include <cstdint>

struct SubtitleBoxPadding
{
    int sides = 30, top = 30, bottom = 10;
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

struct SubtitleCutPasteGeometry
{
    SubtitleBoxRect source, destination, content;
    int pictureTop = 0, pictureBottom = 0;
    bool valid = false;
};

inline SubtitleCutPasteGeometry ComputeSubtitleCutPaste(const SubtitleBoxRect& bounds,
    int width, int height, int pictureTop, int pictureBottom, SubtitleBoxPadding padding = {}, int gap = 15)
{
    SubtitleCutPasteGeometry result;
    result.pictureTop=pictureTop;result.pictureBottom=pictureBottom;
    if (width <= 0 || height <= 0 || pictureTop < 0 || pictureBottom > height ||
        pictureBottom <= pictureTop || !padding.Valid() || gap < 0 || !bounds.Valid() ||
        (bounds.top >= pictureTop && bounds.bottom <= pictureBottom))
        return result;
    result.content = ExpandSubtitleBox(bounds, width, height, 0);
    result.source = ExpandSubtitleBox(bounds, width, height, padding);
    if (!result.source.Valid()) return result;
    const int64_t bottom = int64_t(pictureBottom) - gap;
    const int64_t top = bottom - (int64_t(result.source.bottom) - result.source.top);
    if (top < pictureTop || bottom > pictureBottom || bottom <= top ||
        top >= result.source.top)
        return result;
    result.destination = {result.source.left, static_cast<int>(top),
        result.source.right, static_cast<int>(bottom)};
    result.valid = true;
    return result;
}

struct SubtitleCutPasteSample
{
    int x, y;
    bool clear = false;
};

// CPU oracle for the GPU operation. Always read the immutable CURRENT frame;
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
    void Reset() { m_geometry={};m_reference={};m_cue=0;m_policy=m_continuity=0; }
    SubtitleCutPasteGeometry Consume(const SubtitleBoxResult& text,
        const SubtitleBoxPreview& preview, SubtitleBoxPadding padding={}, int gap=15)
    {
        if (!text.detected || !text.bounds.Valid() || !preview.available ||
            !preview.current.analyzed || !SubtitleBoxLookahead::HasBarEvidence(preview.current) || text.workLimit) {
            Reset();return {};
        }
        const auto& current=preview.current;
        const auto padded=ExpandSubtitleBox(text.bounds,current.width,current.height,padding);
        const auto content=ExpandSubtitleBox(text.bounds,current.width,current.height,0);
        const auto& old=m_geometry.source;
        const auto& oldContent=m_geometry.content;
        const bool same=m_geometry.valid && !current.discontinuity && m_cue==text.cue &&
            m_policy==preview.policyGeneration && m_continuity==preview.continuityGeneration &&
            m_padding==padding && m_gap==gap &&
            SubtitleBoxLookahead::SameContext(m_reference,current) &&
            oldContent.left==content.left && oldContent.top==content.top &&
            oldContent.right==content.right && oldContent.bottom==content.bottom &&
            old.left==padded.left && old.top==padded.top &&
            old.right==padded.right && old.bottom==padded.bottom;
        if (same && m_geometry.destination.top>=current.pictureTop &&
            m_geometry.destination.bottom<=current.pictureBottom) return m_geometry;
        m_geometry=ComputeSubtitleCutPaste(text.bounds,current.width,current.height,
            current.pictureTop,current.pictureBottom,padding,gap);
        m_reference=current;m_reference.ink.reset();m_reference.text={};
        m_cue=text.cue;m_policy=preview.policyGeneration;m_continuity=preview.continuityGeneration;
        m_padding=padding;m_gap=gap;
        return m_geometry;
    }
private:
    SubtitleCutPasteGeometry m_geometry;
    SubtitleBoxObservation m_reference;
    uint64_t m_cue=0,m_policy=0,m_continuity=0;
    SubtitleBoxPadding m_padding;
    int m_gap=15;
};
