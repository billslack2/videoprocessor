#pragma once
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <cstdlib>

// Presentation-only estimate. Never replaces detector evidence or glyph bounds.
struct SubtitleBackgroundGuardPolicy {
    bool enabled = true;
    int top = 12, sides = 20; // Pixels at 1080 lines; scaled to source height.
    bool operator==(const SubtitleBackgroundGuardPolicy& other) const {
        return enabled==other.enabled && top==other.top && sides==other.sides;
    }
};

inline bool GuardSubtitleBackgroundPanel(SubtitleBoxRect& panel,
    const SubtitleBoxResult& text, int width, int height, int pictureTop,
    int pictureBottom, int nearBarDistance, SubtitleBackgroundGuardPolicy policy,
    bool retainFallback = false)
{
    if(!policy.enabled || !panel.Valid() || width<=0 || height<=0 ||
        pictureTop<0 || pictureBottom>height || pictureTop>=pictureBottom ||
        text.lineCount<1 || text.lineCount>3)return false;
    SubtitleBoxRect ink{};
    int lineHeight=0;
    for(int i=0;i<text.lineCount;++i) {
        const auto row=text.lineBounds[i];
        if(!row.Valid() || row.left<0 || row.top<0 || row.right>width || row.bottom>height)return false;
        lineHeight=(std::max)(lineHeight,row.bottom-row.top);
        ink=ink.Valid()?SubtitleBoxRect{(std::min)(ink.left,row.left),(std::min)(ink.top,row.top),
            (std::max)(ink.right,row.right),(std::max)(ink.bottom,row.bottom)}:row;
    }
    const bool bottom=pictureBottom<height && ink.bottom>=pictureBottom-(std::max)(0,nearBarDistance);
    const bool top=pictureTop>0 && ink.top<=pictureTop+(std::max)(0,nearBarDistance);
    if(top==bottom)return false; // Require one known adjacent physical bar.
    const int sidePad=int((int64_t((std::max)(0,(std::min)(200,policy.sides)))*height+540)/1080);
    const int topPad=int((int64_t((std::max)(0,(std::min)(200,policy.top)))*height+540)/1080);
    const int left=(std::max)(0,ink.left-panel.left),right=(std::max)(0,panel.right-ink.right);
    const int above=bottom?(std::max)(0,ink.top-panel.top):(std::max)(0,panel.bottom-ink.bottom);
    const int sideLimit=(std::max)(2*lineHeight,4*sidePad);
    const int topLimit=(std::max)(2*lineHeight,4*topPad);
    const bool excessive=above>topLimit || (std::max)(left,right)>sideLimit;
    const bool uneven=std::abs(left-right)>lineHeight &&
        (std::max)(left,right)>2*(std::min)(left,right)+sidePad;
    if(!retainFallback && !excessive && !uneven)return false;
    // Intersect with measured coverage: the heuristic cannot authorize removing
    // new picture pixels. The existing near-bar cleanup closes a measured gap.
    SubtitleBoxRect cap{(std::max)(0,ink.left-sidePad),
        top?pictureTop:(std::max)(pictureTop,ink.top-topPad),
        (std::min)(width,ink.right+sidePad),
        bottom?pictureBottom:(std::min)(pictureBottom,ink.bottom+topPad)};
    // Detector bounds include native sampling/antialias fringe around rows.
    if(text.bounds.Valid()) {
        cap.left=(std::min)(cap.left,(std::max)(0,text.bounds.left));
        cap.right=(std::max)(cap.right,(std::min)(width,text.bounds.right));
        cap.top=(std::min)(cap.top,(std::max)(pictureTop,text.bounds.top));
        cap.bottom=(std::max)(cap.bottom,(std::min)(pictureBottom,text.bounds.bottom));
    }
    cap={(std::max)(panel.left,cap.left),(std::max)(panel.top,cap.top),
        (std::min)(panel.right,cap.right),(std::min)(panel.bottom,cap.bottom)};
    if(!cap.Valid())return false;
    // A fallback must preserve every measured picture-side glyph row.
    for(int i=0;i<text.lineCount;++i) {
        const auto row=text.lineBounds[i];
        if(row.top<pictureBottom && row.bottom>pictureTop &&
            (row.left<cap.left || row.right>cap.right ||
             (std::max)(row.top,pictureTop)<cap.top || (std::min)(row.bottom,pictureBottom)>cap.bottom))return false;
    }
    panel=cap;
    return true;
}
