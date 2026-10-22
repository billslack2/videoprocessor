#pragma once
#include <SubtitleCutPaste.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Explicit bar extension owns an original-size backing independently of the
// reduced glyph transform. Coordinates remain in the pre-crop source domain.
// Apply after final anchoring; never feed this display-only result into caches.
inline SubtitleCutPasteGeometry PreserveExtendedSubtitleBackingWidth(
    SubtitleCutPasteGeometry g, SubtitleBoxPadding padding, bool configuredExtendToBar)
{
    if(!configuredExtendToBar || !g.valid || !g.extendToBar ||
        !g.centeredGlyphMapping || !g.destination.Valid() || !g.placementLimits.Valid())return g;
    const auto ink=g.layoutContent.Valid()?g.layoutContent:g.content;
    if(!ink.Valid())return g;
    const auto limits=g.placementLimits;
    const int64_t available=int64_t(limits.right)-limits.left;
    const int64_t original=int64_t(ink.right)-ink.left+2*int64_t((std::max)(0,padding.sides));
    const int64_t current=int64_t(g.destination.right)-g.destination.left;
    if(available<=0 || original<=0 || current<=0 || current>=original)return g;
    const int64_t width=(std::min)(available,(std::max)(current,original));
    const double center=double(g.glyphTranslateX)+double(g.glyphScale)*
        (double(ink.left)+double(ink.right))*0.5;
    if(!std::isfinite(center))return g;
    const double proposed=std::floor(center-double(width)*0.5);
    const int64_t left=proposed<=limits.left?int64_t(limits.left):
        proposed>=double(int64_t(limits.right)-width)?int64_t(limits.right)-width:int64_t(proposed);
    // Keep any wider previously measured coverage without moving the glyphs.
    g.destination.left=int((std::max)(int64_t(limits.left),
        (std::min)(int64_t(g.destination.left),left)));
    g.destination.right=int((std::min)(int64_t(limits.right),
        (std::max)(int64_t(g.destination.right),left+width)));
    return g;
}
