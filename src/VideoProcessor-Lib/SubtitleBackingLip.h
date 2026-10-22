#pragma once
#include <SubtitleCutPaste.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Exact-current cleanup only. Never an acquired card, glyph/capture mask,
// retained display envelope, or aspect-ratio certificate.
struct CurrentSubtitleBackingLipProof
{
    bool valid=false;
    const char* reason="not-evaluated";
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration=0,continuityGeneration=0;
    int width=0,height=0,pictureTop=0,pictureBottom=0;
    bool fromTopBar=false;
    SubtitleBoxRect measured,cleanup,blackInterior;
    size_t samples=0,cornerPixels=0;
    unsigned roundingRows=0;
};

inline CurrentSubtitleBackingLipProof ProveCurrentSubtitleBackingLip(
    const AnalysisLumaSource& source,const SubtitleBoxPreview& preview,
    const SubtitleBoxResult& accepted,const ActivePictureFrameIdentity& expectedIdentity,
    uint64_t policy,uint64_t continuity,size_t maxSamples=65536)
{
    CurrentSubtitleBackingLipProof result;
    result.identity=expectedIdentity;result.policyGeneration=policy;result.continuityGeneration=continuity;
    result.width=source.width;result.height=source.height;
    const auto reject=[&](const char* why){result.reason=why;return result;};
    const auto& current=preview.current;
    result.pictureTop=current.pictureTop;result.pictureBottom=current.pictureBottom;
    if(!expectedIdentity.acceptedSequence || !preview.available || !current.analyzed || current.discontinuity ||
        current.analysisRefresh || current.cadenceSkipped || current.glyphLocalTop || current.glyphLocalBottom ||
        !current.independentBarAnalysis || !current.barAuthority || current.barTrackingAuthority ||
        current.currentAnchorUsesTrackedEdge || !current.barEvidence.available ||
        current.barEvidence.captionBackedTop || current.barEvidence.captionBackedBottom ||
        current.barEvidence.topRevalidatedAtReference || current.barEvidence.topRevalidatedNearby ||
        current.barEvidence.bottomRevalidatedAtReference || current.barEvidence.bottomRevalidatedNearby ||
        !SameAnalysisFrameIdentity(current.identity,expectedIdentity) ||
        preview.policyGeneration!=policy || current.policyGeneration!=policy ||
        preview.continuityGeneration!=continuity || current.continuityGeneration!=continuity)
        return reject("lip-current-context-unavailable");
    if(!source.IsValid() || source.width<64 || source.height<48 || source.width>8192 || source.height>4320 ||
        source.generation!=expectedIdentity.transportGeneration || current.width!=source.width || current.height!=source.height ||
        current.pictureTop<0 || current.pictureBottom>source.height || current.pictureTop>=current.pictureBottom ||
        current.barEvidence.top!=current.pictureTop || current.barEvidence.bottom!=current.pictureBottom)
        return reject("lip-source-or-physical-base-mismatch");
    if(!accepted.cue || !accepted.detected || accepted.held || accepted.workLimit || accepted.revised || accepted.observations<1 ||
        !current.text.detected || current.text.held || current.text.workLimit ||
        !current.text.nearBarEligibilityMeasured || !current.text.nearBarEligible ||
        !accepted.nearBarEligibilityMeasured || !accepted.nearBarEligible ||
        !SameFreshSubtitleGlyphs(accepted,current.text,0) ||
        accepted.lineCount<1 || accepted.lineCount>3 || !current.ink ||
        !current.ink->rawEvidenceComplete || !current.ink->paletteValid)
        return reject("lip-current-owned-cue-unavailable");
    const auto& ink=*current.ink;
    if(ink.width<1 || ink.width>960 || ink.height<1 || ink.height>540 || ink.step<1 || ink.step>16 ||
        ink.width!=(source.width+ink.step-1)/ink.step || ink.height!=(source.height+ink.step-1)/ink.step ||
        ink.sourceRows.size()!=size_t(ink.height) || ink.sourceRows.empty() ||
        ink.sourceRows.front()<0 || ink.sourceRows.back()>=source.height ||
        !std::is_sorted(ink.sourceRows.begin(),ink.sourceRows.end()) ||
        std::adjacent_find(ink.sourceRows.begin(),ink.sourceRows.end())!=ink.sourceRows.end())
        return reject("lip-owned-grid-unavailable");
    const size_t words=(size_t(ink.width)*ink.height+63)/64;
    if(ink.ownedInk.size()!=words || ink.rawInk.size()!=words)return reject("lip-owned-grid-incomplete");
    const auto spend=[&](){if(result.samples>=maxSamples)return false;++result.samples;return true;};
    const auto inRaster=[&](const SubtitleBoxRect& r){return r.Valid() && r.left>=0 && r.top>=0 &&
        r.right<=source.width && r.bottom<=source.height;};
    bool allTop=current.pictureTop>0,allBottom=current.pictureBottom<source.height;
    SubtitleBoxRect glyphs{};
    const auto captureLines=SubtitleGlyphCaptureLines(accepted,source.width,source.height);
    for(int i=0;i<accepted.lineCount;++i){
        const auto& row=accepted.lineBounds[i];
        if(!inRaster(row))return reject("lip-invalid-glyph-bounds");
        allTop &= row.bottom<=current.pictureTop;allBottom &= row.top>=current.pictureBottom;
        glyphs=glyphs.Valid()?SubtitleBoxRect{(std::min)(glyphs.left,row.left),(std::min)(glyphs.top,row.top),
            (std::max)(glyphs.right,row.right),(std::max)(glyphs.bottom,row.bottom)}:row;
        // Pending metadata keeps the acquired mask. Revalidate BOTH positive
        // and negative classifier cells from this exact current native source;
        // the pending flag by itself supplies no glyph or cleanup authority.
        const auto roi=current.pendingRefresh?captureLines[i]:row;
        if(!inRaster(roi))return reject("lip-invalid-glyph-capture");
        const int top=int(std::lower_bound(ink.sourceRows.begin(),ink.sourceRows.end(),roi.top)-ink.sourceRows.begin());
        const int bottom=int(std::lower_bound(ink.sourceRows.begin(),ink.sourceRows.end(),roi.bottom)-ink.sourceRows.begin());
        bool owned=false;
        for(int y=top;y<bottom && (current.pendingRefresh || !owned);++y)
            for(int x=(std::max)(0,roi.left/ink.step);x<ink.width && x*ink.step<roi.right;++x){
                if(!spend())return reject("lip-work-budget");
                const bool wasRaw=ink.Get(x,y),wasOwned=ink.Get(x,y,true);
                if(current.pendingRefresh){
                    AnalysisLumaSample value;
                    if(!source.Sample(x*ink.step,ink.sourceRows[y],value))
                        return reject("lip-pending-native-glyph-unavailable");
                    const bool isRaw=value.luma>=ink.inkFloor &&
                        std::abs(int(value.chromaU)-ink.inkU)<=80 &&
                        std::abs(int(value.chromaV)-ink.inkV)<=80;
                    if(wasRaw!=isRaw)return reject("lip-pending-glyph-classifier-changed");
                }
                owned|=wasOwned && wasRaw;
                if(owned && !current.pendingRefresh)break;
            }
        if(!owned)return reject("lip-current-row-has-no-owned-ink");
    }
    if(allTop==allBottom)return reject("lip-glyphs-not-wholly-in-one-bar");
    result.fromTopBar=allTop;
    const int glyphHeight=SubtitleTypicalLineHeight(accepted);
    const int gap=allTop?current.pictureTop-glyphs.bottom:glyphs.top-current.pictureBottom;
    if(glyphHeight<4 || glyphHeight>source.height/4 || gap<0 || gap>glyphHeight/2)
        return reject("lip-glyphs-too-far-from-edge");
    const int floor=allTop?current.barEvidence.topFloor:current.barEvidence.bottomFloor;
    if(floor<0 || floor>96)return reject("lip-physical-black-floor-unavailable");
    const int darkLimit=floor+4;
    const int depthLimit=(std::min)(128,glyphHeight);
    const int sideMargin=(std::max)(8,2*glyphHeight);
    const int searchLeft=(std::max)(3,glyphs.left-sideMargin);
    const int searchRight=(std::min)(source.width-3,glyphs.right+sideMargin);
    const int center=(glyphs.left+glyphs.right)/2;
    if(searchLeft>=glyphs.left || searchRight<=glyphs.right || searchLeft>=center || searchRight<=center)
        return reject("lip-horizontal-search-unbounded");
    bool exhausted=false,failedRead=false;
    const auto sample=[&](int x,int y,AnalysisLumaSample& value){
        if(!spend()){exhausted=true;return false;}
        if(!source.Sample(x,y,value)){failedRead=true;return false;}
        return true;
    };
    // Reuse chroma from each exhaustive native luma probe; no second sampling
    // pass, image conversion, allocation, or persistent image storage.
    std::array<AnalysisLumaSample,8192> rowPixels;
    struct Row {int left=0,right=0,y=0;};
    Row first,last;
    int rows=0,commonLeft=0,commonRight=source.width,minLeft=source.width,maxRight=0;
    size_t rowBodyPixels=0;
    bool closed=false;
    for(int depth=0;depth<=depthLimit;++depth){
        const int y=allTop?current.pictureTop+depth:current.pictureBottom-1-depth;
        if(y<current.pictureTop || y>=current.pictureBottom)break;
        if(!sample(center,y,rowPixels[center]))break;
        if(rowPixels[center].luma>darkLimit){
            if(!rows)return reject("lip-no-attached-black-backing");
            const int left=last.left+6,right=last.right-6;
            if(left>=right)return reject("lip-top-closure-too-narrow");
            for(int x=left;x<right;++x){
                AnalysisLumaSample value;if(!sample(x,y,value))break;
                if(value.luma<=darkLimit)return reject("lip-top-closure-occluded");
            }
            if(exhausted || failedRead)break;
            closed=true;break;
        }
        if(depth==depthLimit)return reject("lip-depth-search-limit");
        int left=center,right=center+1;
        while(left>searchLeft){
            if(!sample(left-1,y,rowPixels[left-1]) || rowPixels[left-1].luma>darkLimit)break;
            --left;
        }
        if(exhausted || failedRead)break;
        while(right<searchRight){
            if(!sample(right,y,rowPixels[right]) || rowPixels[right].luma>darkLimit)break;
            ++right;
        }
        if(exhausted || failedRead)break;
        if(left<=searchLeft || right>=searchRight)return reject("lip-side-search-limit");
        if(left>glyphs.left || right<glyphs.right)return reject("lip-black-span-does-not-cover-glyphs");
        for(int distance=0;distance<3;++distance){
            AnalysisLumaSample a,b;
            if(!sample(left-1-distance,y,a) || !sample(right+distance,y,b))break;
            if(a.luma<=darkLimit || b.luma<=darkLimit)return reject("lip-flank-occluded");
        }
        if(exhausted || failedRead)break;
        if(rows){
            const int maximumRowInset=(std::max)(1,glyphHeight/8);
            if(left<last.left || right>last.right || left-last.left>maximumRowInset ||
                last.right-right>maximumRowInset ||
                left-first.left>glyphHeight/2 || first.right-right>glyphHeight/2)
                return reject("lip-non-card-side-geometry");
        }else first={left,right,y};
        // Decoded 4:2:2 chroma may bleed across a few black edge pixels. Luma
        // remains exhaustive there; the interior must be neutral throughout.
        for(int x=left+6;x<right-6;++x)
            if(std::abs(int(rowPixels[x].chromaU)-512)>32 || std::abs(int(rowPixels[x].chromaV)-512)>32)
                return reject("lip-colored-interior");
        if(rows && (left>first.left || right<first.right))++result.roundingRows;
        last={left,right,y};++rows;
        commonLeft=(std::max)(commonLeft,left);commonRight=(std::min)(commonRight,right);
        minLeft=(std::min)(minLeft,left);maxRight=(std::max)(maxRight,right);
        rowBodyPixels+=size_t(right-left);
    }
    if(exhausted)return reject("lip-work-budget");
    if(failedRead)return reject("lip-native-sample-unavailable");
    if(!closed)return reject("lip-top-closure-unavailable");
    if(rows<2)return reject("lip-insufficient-native-row-support");
    result.blackInterior={commonLeft,allTop?current.pictureTop:last.y,commonRight,
        allTop?last.y+1:current.pictureBottom};
    if(!result.blackInterior.Valid())return reject("lip-no-common-cleanup-interior");
    // All row bodies are black. Only monotone, bounded rounded corners may
    // contain picture pixels inside the complete backing restoration bounds.
    // This is the same finite-card restoration role as an opaque card union;
    // it is deliberately distinct from the certified black intersection.
    result.measured={minLeft,result.blackInterior.top,maxRight,result.blackInterior.bottom};
    result.cornerPixels=size_t(maxRight-minLeft)*size_t(rows)-rowBodyPixels;
    const size_t boundingPixels=size_t(maxRight-minLeft)*size_t(rows);
    if(result.cornerPixels>size_t(glyphHeight)*size_t(glyphHeight)/4 ||
        result.cornerPixels*100>boundingPixels*5)
        return reject("lip-rounded-corner-area-too-large");
    if(result.roundingRows>unsigned((std::max)(1,glyphHeight/3)))
        return reject("lip-rounded-corner-depth-too-large");
    result.cleanup=result.measured; // No unproved antialias/display expansion.
    result.valid=true;result.reason="current-finite-rounded-backing-lip";
    return result;
}

// Apply only to a local composition AFTER all stable layout caches. The exact
// current source owns cleanup; the caller never writes this value into a cache.
inline SubtitleCutPasteGeometry ApplyCurrentSubtitleBackingLip(
    SubtitleCutPasteGeometry geometry,const CurrentSubtitleBackingLipProof& proof,
    const ActivePictureFrameIdentity& expectedIdentity,uint64_t policy,uint64_t continuity)
{
    const auto contains=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b){return a.Valid() && b.Valid() &&
        b.left>=a.left && b.top>=a.top && b.right<=a.right && b.bottom<=a.bottom;};
    const SubtitleBoxRect picture{0,proof.pictureTop,proof.width,proof.pictureBottom};
    if(!geometry.valid || geometry.generatedCleanup.Valid() || geometry.cleanupLimit.Valid() || !proof.valid ||
        !SameAnalysisFrameIdentity(proof.identity,expectedIdentity) ||
        proof.policyGeneration!=policy || proof.continuityGeneration!=continuity ||
        proof.width<1 || proof.height<1 || proof.pictureTop<0 || proof.pictureBottom>proof.height ||
        proof.pictureTop>=proof.pictureBottom || geometry.pictureTop!=proof.pictureTop ||
        geometry.pictureBottom!=proof.pictureBottom || geometry.fromTopBar!=proof.fromTopBar ||
        !contains(picture,proof.cleanup) || !contains(proof.measured,proof.blackInterior) ||
        proof.cleanup.left!=proof.measured.left || proof.cleanup.top!=proof.measured.top ||
        proof.cleanup.right!=proof.measured.right || proof.cleanup.bottom!=proof.measured.bottom ||
        (proof.fromTopBar?proof.measured.top!=proof.pictureTop:proof.measured.bottom!=proof.pictureBottom))
        return geometry;
    geometry.generatedCleanup=proof.cleanup;
    return geometry;
}
