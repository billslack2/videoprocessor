#pragma once
#include <SubtitleBoxDetector.h>
#include <algorithm>
#include <cstdlib>

// Current-pixel caption support for a subtitle placement plane only. This
// certificate never proves a physical bar edge or an active-picture crop.
struct SubtitleLocalGlyphProof
{
    bool accepted = false;
    int mismatchX=-1,mismatchY=-1,mismatchLuma=-1;
    int blackFloor = 0; // Minimum freshly sampled neutral-dark support code.
    SubtitleBoxRect verifiedBounds;
    const char* reason = "invalid-candidate";
};

inline SubtitleLocalGlyphProof ProveLocalSubtitleGlyphBacking(
    const AnalysisLumaSource& source, const SubtitleBoxResult& text,
    const SubtitleInkSnapshot* ink, int plane, bool bottom)
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
    const SubtitleBoxRect verified{
        (std::max)(0,box.left-step-1),
        (std::max)(0,box.top-step+1),
        (std::min)(source.width,box.right+2),box.bottom};
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


// Geometry-neutral worker proof for a caption found using established native
// geometry. Only CURRENT detector-owned glyphs and the surrounding sampled
// backing are checked; no physical edge, absence outside this rectangle, or
// AR authority is inferred. The caller binds the result to its exact source
// identity and geometry revision. verifiedBounds is sampled support, not an
// exhaustive native-pixel ownership mask for arbitrary shader extraction.
inline SubtitleLocalGlyphProof ProveCurrentSubtitleGlyphBacking(
    const AnalysisLumaSource& source, const SubtitleBoxResult& text,
    const SubtitleInkSnapshot* ink, size_t maxSamples=64000)
{
    SubtitleLocalGlyphProof result;
    if(!source.IsValid() || !text.detected || text.held || text.confirmationPending ||
        text.workLimit || !text.bounds.Valid() || !text.anchor.Valid() || !ink ||
        !ink->rawEvidenceComplete || !ink->paletteValid ||
        !text.nearBarEligibilityMeasured || !text.nearBarEligible ||
        text.lineCount<1 || text.lineCount>3)return result;
    const int step=ink->step;
    const auto& box=text.bounds;
    const auto& anchor=text.anchor;
    if(step<1 || step>8 || step!=SubtitleBoxDetector::SamplingStep(source.width,source.height) ||
        ink->width<1 || ink->width>960 || ink->height<1 || ink->height>540 ||
        ink->width!=(source.width+step-1)/step || ink->height!=(source.height+step-1)/step ||
        ink->sourceRows.size()!=size_t(ink->height) ||
        ink->inkFloor<0 || ink->inkFloor>1023 || ink->blackLimit<0 || ink->blackLimit>=ink->inkFloor ||
        ink->inkU<0 || ink->inkU>1023 || ink->inkV<0 || ink->inkV>1023) {
        result.reason="invalid-grid-or-palette";return result;
    }
    const size_t words=(size_t(ink->width)*ink->height+63)/64;
    if(ink->rawInk.size()!=words || ink->ownedInk.size()!=words) {
        result.reason="invalid-ink-mask";return result;
    }
    for(size_t i=0;i<words;++i)if(ink->ownedInk[i]&~ink->rawInk[i]) {
        result.reason="invalid-ink-ownership";return result;
    }
    if(box.left<0 || box.top<0 || box.right>source.width || box.bottom>source.height ||
        box.right-box.left>source.width*4/5 || box.bottom-box.top>source.height/3 ||
        anchor.left<box.left || anchor.top<box.top || anchor.right>box.right || anchor.bottom>box.bottom) {
        result.reason="unbounded-current-cue";return result;
    }
    for(int line=0;line<text.lineCount;++line) {
        const auto& r=text.lineBounds[line];
        if(!r.Valid() || r.left<box.left || r.top<box.top || r.right>box.right || r.bottom>box.bottom) {
            result.reason="invalid-current-line";return result;
        }
    }
    // Keep the complete native glyph capture fringe and every nominated
    // current backing margin. Panels nominate the verification footprint;
    // only the fresh samples below grant local support, never their metadata.
    // text.bounds includes detector/display padding. Verify the actual
    // native capture fringe (the same one used by SubtitleGlyphCaptureLines),
    // without padding that may legitimately contain live picture pixels.
    SubtitleBoxRect verified;
    for(int line=0;line<text.lineCount;++line) {
        const auto& r=text.lineBounds[line];
        const SubtitleBoxRect capture{(std::max)(0,r.left-(step-1)-2),
            (std::max)(0,r.top-(step-1)),(std::min)(source.width,r.right+2),r.bottom};
        if(!verified.Valid())verified=capture;
        else verified={(std::min)(verified.left,capture.left),(std::min)(verified.top,capture.top),
            (std::max)(verified.right,capture.right),(std::max)(verified.bottom,capture.bottom)};
    }
    // The detector's capture/card envelopes can include an inferred bridge
    // to the AR plane. This proof certifies current glyph capture only; a
    // broader current finite-card proof owns cleanup independently.
    const int firstX=(verified.left+step-1)/step,lastX=(verified.right-1)/step;
    const int columns=lastX-firstX+1;
    int firstRow=source.height,lastRow=-1,rowCount=0;
    for(int iy=0;iy<ink->height;++iy) {
        const int y=ink->sourceRows[size_t(iy)];
        if(y<0 || y>=source.height || (iy &&
            (y<=ink->sourceRows[size_t(iy-1)] || y-ink->sourceRows[size_t(iy-1)]>2*step))) {
            result.reason="invalid-source-rows";return result;
        }
        if(y>=verified.top && y<verified.bottom) {
            firstRow=(std::min)(firstRow,y);lastRow=y;++rowCount;
        }
    }
    const size_t budget=(std::min)(maxSamples,size_t(64000));
    if(columns<3 || rowCount<2 || !budget || size_t(columns)*size_t(rowCount)>budget) {
        result.reason="local-work-limit";return result;
    }
    if(firstRow>verified.top+step || lastRow<verified.bottom-step-1) {
        result.reason="unread-local-row";return result;
    }
    const auto inLine=[&](int x,int y,int line) {
        const auto& r=text.lineBounds[line];
        return x>=r.left && x<r.right && y>=r.top && y<r.bottom;
    };
    const auto ownedAt=[&](int ix,int iy) {
        if(ix<0 || iy<0 || ix>=ink->width || iy>=ink->height || !ink->Get(ix,iy,true))return false;
        for(int line=0;line<text.lineCount;++line)
            if(inLine(ix*step,ink->sourceRows[size_t(iy)],line))return true;
        return false;
    };
    unsigned lineOwned[3]{},lineBacking[3]{},lineRows[3]{};
    unsigned tiles[3]{},tested=0,backing=0,nonblack=0;
    size_t samples=0;
    int currentBlackFloor=1023;
    const int blackLimit=(std::min)(96,(std::max)(32,ink->blackLimit+12));
    for(int iy=0;iy<ink->height;++iy) {
        const int y=ink->sourceRows[size_t(iy)];
        if(y<verified.top || y>=verified.bottom)continue;
        bool lineHasOwned[3]{};
        for(int ix=firstX;ix<=lastX;++ix) {
            AnalysisLumaSample pixel;
            if(samples>=budget) {result.reason="local-work-limit";return result;}
            ++samples;
            if(!source.Sample(ix*step,y,pixel)) {result.reason="sample-failed";return result;}
            ++tested;
            const bool palette=std::abs(int(pixel.chromaU)-ink->inkU)<=80 &&
                std::abs(int(pixel.chromaV)-ink->inkV)<=80;
            const bool owned=ownedAt(ix,iy);
            if(owned) {
                // A stale mask may not explain even a small missing component.
                if(!palette || pixel.luma<ink->inkFloor) {
                    result.reason="current-owned-glyph-unproved";return result;
                }
                ++nonblack;
                for(int line=0;line<text.lineCount;++line)if(inLine(ix*step,y,line)) {
                    ++lineOwned[line];lineHasOwned[line]=true;
                }
                continue;
            }
            const bool dark=pixel.luma<=blackLimit &&
                std::abs(int(pixel.chromaU)-512)<=48 && std::abs(int(pixel.chromaV)-512)<=48;
            if(dark) {
                currentBlackFloor=(std::min)(currentBlackFloor,int(pixel.luma));
                ++backing;++tiles[(std::min)(2,(ix-firstX)*3/columns)];
                for(int line=0;line<text.lineCount;++line)
                    if(inLine(ix*step,y,line))++lineBacking[line];
                continue;
            }
            // Allow current palette-compatible antialias fringe immediately
            // beside an owned sample, never unrelated brightness/colored scene.
            bool adjacent=false;
            for(int dy=-1;dy<=1 && !adjacent;++dy)for(int dx=-1;dx<=1;++dx)
                adjacent |= ownedAt(ix+dx,iy+dy);
            if(!palette || !adjacent) {
                result.reason="unowned-local-pixel-not-dark";
                result.mismatchX=ix*step;result.mismatchY=y;result.mismatchLuma=pixel.luma;return result;
            }
            ++nonblack;
        }
        for(int line=0;line<text.lineCount;++line)lineRows[line]+=lineHasOwned[line];
    }
    for(int line=0;line<text.lineCount;++line)
        if(lineOwned[line]<6 || lineRows[line]<2 || lineBacking[line]<24) {
            result.reason="current-line-support-incomplete";return result;
        }
    int supportedTiles=0;
    for(int tile=0;tile<3;++tile)supportedTiles+=tiles[tile]>=8;
    if(backing<48 || supportedTiles<2 || nonblack*2>tested) {
        result.reason="insufficient-unowned-backing";return result;
    }
    result.accepted=true;result.blackFloor=currentBlackFloor;
    result.reason="current-glyph-union-backing";
    result.verifiedBounds=verified;
    return result;
}
