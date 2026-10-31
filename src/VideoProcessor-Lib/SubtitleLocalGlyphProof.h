#pragma once
#include <SubtitleBoxDetector.h>
#include <SubtitleOpaqueCardRefinement.h>
#include <algorithm>
#include <cstdlib>

// Current-pixel caption support for a subtitle placement plane only. This
// certificate never proves a physical bar edge or an active-picture crop.
struct SubtitleLocalGlyphProof
{
    bool accepted = false;
    SubtitleBoxRect verifiedBounds;
    const char* reason = "invalid-candidate";
};

inline SubtitleLocalGlyphProof ProveLocalSubtitleGlyphBacking(
    const AnalysisLumaSource& source, const SubtitleBoxResult& text,
    const SubtitleInkSnapshot* ink, int plane, bool bottom,
    const SubtitleBoxRect* currentFiniteCard = nullptr)
{
    SubtitleLocalGlyphProof result;
    if (!source.IsValid() || !text.detected || text.held || text.workLimit ||
        !text.anchor.Valid() || !text.bounds.Valid() || !ink ||
        !ink->rawEvidenceComplete || !ink->paletteValid ||
        ink->step!=SubtitleBoxDetector::SamplingStep(source.width,source.height) ||
        ink->width<=0 || ink->height<=0 ||
        ink->sourceRows.size()!=size_t(ink->height) ||
        ink->ownedInk.empty() || ink->ownedInk.size()!=ink->rawInk.size() ||
        !text.nearBarEligibilityMeasured || !text.nearBarEligible ||
        text.lineCount<1 || text.lineCount>3 ||
        plane<=0 || plane>=source.height) return result;
    const auto& a=text.anchor;
    const auto& box=text.bounds;
    if(box.left<0 || box.top<0 || box.right>source.width ||
        box.bottom>source.height ||
        box.right-box.left>source.width*4/5 ||
        box.bottom-box.top>source.height/3) {
        result.reason="unbounded-cue-bounds";return result;
    }
    const int minimumGlyphWidth=(std::max)(8*ink->step,
        (std::min)(source.width/12,2*(a.bottom-a.top)));
    if (!(a.top<plane && a.bottom>plane) || a.left<box.left ||
        a.right>box.right || a.top<box.top || a.bottom>box.bottom ||
        a.right-a.left<minimumGlyphWidth || a.right-a.left>source.width*4/5 ||
        std::abs(a.left+a.right-source.width)>source.width/3) {
        result.reason="no-centered-crossing-glyph";return result;
    }
    const int step=ink->step;
    if(bottom?plane<source.height*2/3:plane>source.height/3) {
        result.reason="interior-aperture";return result;
    }
    // Match only the native extraction fringe (sample leading pixels and two
    // antialias pixels), then prove every sampled part of that full rectangle.
    SubtitleBoxRect verified{
        (std::max)(0,box.left-step-1),
        (std::max)(0,box.top-step+1),
        (std::min)(source.width,box.right+2),box.bottom};
    // Only a freshly remeasured finite card may widen the local probe. Every
    // added native pixel is still sampled below with the original density,
    // neutral-backing and intrusion limits. No inferred/cleanup rectangle enters.
    if(currentFiniteCard) {
        const auto& card=*currentFiniteCard;
        const int pictureTop=bottom?verified.top:plane;
        const int pictureBottom=bottom?plane:verified.bottom;
        if(!card.Valid() || card.left<0 || card.top<0 || card.right>source.width ||
            card.bottom>source.height || card.left>verified.left ||
            card.right<verified.right || card.top>pictureTop || card.bottom<pictureBottom) {
            result.reason="finite-card-does-not-cover-capture";return result;
        }
        verified.left=card.left;verified.right=card.right;
        if(bottom)verified.top=card.top;else verified.bottom=card.bottom;
        if(verified.right-verified.left>source.width*4/5 ||
            verified.bottom-verified.top>source.height/3) {
            result.reason="unbounded-finite-card";return result;
        }
    }
    const int firstX=(verified.left+step-1)/step,lastX=(verified.right-1)/step;
    const int columns=lastX-firstX+1;
    if(columns<9 || uint64_t(columns)*uint64_t((verified.bottom-verified.top+step-1)/step+2)>64000) {
        result.reason="local-work-limit";return result;
    }
    // Every source row and column claimed by verifiedBounds is sampled here.
    // The detector's 3x3 owned mask covers only antialiasing next to its
    // accepted strokes; all other pixels must be neutral dark backing.
    unsigned tested[2]{},dark[2]{},owned[2]{},rows[2]{};
    unsigned exactAnchorInk[2]{},currentAnchorInk[2]{};
    unsigned tileTested[2][3]{},tileDark[2][3]{};
    int maxBadRun[2]{};
    int firstRow=source.height,lastRow=-1,priorRow=-1;
    bool ownedOnPictureSide=false,ownedOnBarSide=false;
    const int blackLimit=(std::min)(96,(std::max)(32,ink->blackLimit+12));
    for(int iy=0;iy<ink->height;++iy) {
        const int y=ink->sourceRows[size_t(iy)];
        if(y<verified.top || y>=verified.bottom)continue;
        if(priorRow>=0 && y-priorRow>2*step) {
            result.reason="unread-local-row";return result;
        }
        priorRow=y;firstRow=(std::min)(firstRow,y);lastRow=(std::max)(lastRow,y);
        const int side=bottom?(y>=plane):(y<plane);
        ++rows[side];
        unsigned rowTested=0,rowDark=0;
        int badRun=0;
        for(int ix=firstX;ix<=lastX;++ix) {
            const int x=ix*step;
            AnalysisLumaSample pixel;
            if(!source.Sample(x,y,pixel)) {result.reason="sample-failed";return result;}
            bool stroke=false;
            for(int dy=-1;dy<=1 && !stroke;++dy)
                for(int dx=-1;dx<=1;++dx)
                    if(ink->Get(ix+dx,iy+dy,true)) {stroke=true;break;}
            if(stroke) {
                ++owned[side];badRun=0;
                if(x>=a.left && x<a.right && y>=a.top && y<a.bottom &&
                    ink->Get(ix,iy,true)) {
                    ++exactAnchorInk[side];
                    const bool currentBright=pixel.luma>=ink->inkFloor &&
                        std::abs(int(pixel.chromaU)-ink->inkU)<=80 &&
                        std::abs(int(pixel.chromaV)-ink->inkV)<=80;
                    currentAnchorInk[side]+=currentBright;
                    if(currentBright) {
                        if(side)ownedOnBarSide=true;else ownedOnPictureSide=true;
                    }
                }
                continue;
            }
            const bool neutral=std::abs(int(pixel.chromaU)-512)<=48 &&
                std::abs(int(pixel.chromaV)-512)<=48;
            const bool black=neutral && pixel.luma<=blackLimit;
            ++tested[side];++rowTested;dark[side]+=black;rowDark+=black;
            const int tile=(std::min)(2,(ix-firstX)*3/columns);
            ++tileTested[side][tile];tileDark[side][tile]+=black;
            if(black)badRun=0;
            else {++badRun;maxBadRun[side]=(std::max)(maxBadRun[side],badRun);}
        }
        if(rowTested>=8 && rowDark*100<rowTested*80) {
            result.reason="row-backing-not-dark";return result;
        }
    }
    if(firstRow>verified.top+step || lastRow<verified.bottom-step-1 ||
        !ownedOnPictureSide || !ownedOnBarSide) {
        result.reason="unread-or-unowned-crossing";return result;
    }
    for(int side=0;side<2;++side) {
        int supportedTiles=0;
        for(int tile=0;tile<3;++tile)
            supportedTiles+=tileTested[side][tile]>=8 &&
                tileDark[side][tile]*100>=tileTested[side][tile]*80;
        if(exactAnchorInk[side]<2 || currentAnchorInk[side]<2 ||
            currentAnchorInk[side]*100<exactAnchorInk[side]*80) {
            result.reason="current-owned-glyph-unproved";return result;
        }
        if(owned[side]*100>(owned[side]+tested[side])*50) {
            result.reason="insufficient-unowned-backing";return result;
        }
        if(rows[side]<2 || tested[side]<24 || owned[side]<2 ||
            supportedTiles<2 || dark[side]*100<tested[side]*85 ||
            maxBadRun[side]>4) {
            result.reason="local-backing-unproved";return result;
        }
    }
    result.accepted=true;
    result.reason="current-glyph-local-backing";
    result.verifiedBounds=verified; // The full returned rectangle was just sampled.
    return result;
}

// A dense crossing caption can leave too little whitespace in its tight glyph
// envelope. Seek genuinely witnessed CURRENT card backing, then repeat the same
// local native proof on that wider footprint. Remembered panels never enter.
inline SubtitleLocalGlyphProof ProveFiniteCardLocalSubtitleGlyphBacking(
    const AnalysisLumaSource& source,const SubtitleBoxResult& text,
    const SubtitleInkSnapshot* ink,int pictureTop,int pictureBottom,bool bottom)
{
    SubtitleLocalGlyphProof rejected;
    rejected.reason="finite-card-unavailable";
    if(!source.IsValid() || !text.detected || text.held || text.workLimit ||
        !text.anchor.Valid() || !ink || !ink->rawEvidenceComplete || !ink->paletteValid ||
        pictureTop<=0 || pictureBottom>=source.height || pictureTop>=pictureBottom)
        return rejected;
    const int plane=bottom?pictureBottom:pictureTop;
    if(!(text.anchor.top<plane && text.anchor.bottom>plane))return rejected;
    const int darkLimit=(std::min)(96,(std::max)(32,ink->blackLimit+12));
    SubtitleOpaqueCardRefinement card;
    if(!RefineSubtitleOpaqueCard(source,text.anchor,text.anchor.bottom-text.anchor.top,
        pictureTop,pictureBottom,darkLimit,ink->inkFloor,ink->inkU,ink->inkV,card) ||
        !card.interior.Valid())return rejected;
    auto result=ProveLocalSubtitleGlyphBacking(source,text,ink,plane,bottom,&card.interior);
    if(result.accepted)result.reason="current-finite-card-local-backing";
    return result;
}
