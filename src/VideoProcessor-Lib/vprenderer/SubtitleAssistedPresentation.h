#pragma once
#include "SubtitleAssistedAcquisition.h"
#include <SubtitleCutPaste.h>
namespace AlphaSourceCrop
{
struct SubtitleAssistedPresentationInput
{
    SubtitleAssistedAcquisitionDecision proof;
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration=0, continuityGeneration=0;
    const SubtitleBoxObservation* observation=nullptr;
    const SubtitleBoxResult* acceptedCue=nullptr;
    SubtitleCutPasteGeometry move;
    int backgroundMode=3;
    bool compositionSucceeded=false;
};
struct SubtitleAssistedPresentationDecision
{
    bool available=false, composed=false;
    ActivePictureBounds bounds;
    const char* reason="assisted-proof-unavailable";
};
// The source proof must audit every nonblack excluded native pixel against the
// current owned mask AND exact SubtitleGlyphCaptureBounds. This selector never
// derives source evidence from the composed image or declares native admission.
inline SubtitleAssistedPresentationDecision SelectSubtitleAssistedPresentation(
    const SubtitleAssistedPresentationInput& input)
{
    SubtitleAssistedPresentationDecision result;
    auto same=[](const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
    };
    auto rectSame=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    };
    auto contains=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.Valid() && b.Valid() && a.left<=b.left && a.top<=b.top && a.right>=b.right && a.bottom>=b.bottom;
    };
    const auto& proof=input.proof;const auto& b=proof.candidate;
    if(!proof.confirmed || proof.inhibited || !input.observation ||
        !SameActivePictureFrameIdentity(proof.identity,input.identity) ||
        proof.policyGeneration!=input.policyGeneration || proof.continuityGeneration!=input.continuityGeneration)
        return result;
    const auto& o=*input.observation;
    if(!SameActivePictureFrameIdentity(o.identity,input.identity) || o.policyGeneration!=input.policyGeneration ||
        o.continuityGeneration!=input.continuityGeneration || !o.assistedSourceCandidate || !same(o.assistedBounds,b) ||
        o.width!=b.rasterWidth || o.height!=b.rasterHeight || o.pictureTop!=b.top || o.pictureBottom!=b.bottom ||
        !o.analyzed || o.pendingRefresh || o.analysisRefresh || o.discontinuity ||
        !o.text.detected || o.text.held || o.text.workLimit || o.text.lineCount<1 || o.text.lineCount>3 ||
        !o.ink || !o.ink->rawEvidenceComplete || o.ink->rawInk.empty() || o.ink->ownedInk.empty()) {
        result.reason="assisted-observation-not-current";return result;
    }
    const auto& ink=*o.ink;
    const int step=SubtitleBoxDetector::SamplingStep(b.rasterWidth,b.rasterHeight);
    const size_t words=(size_t(ink.width)*size_t(ink.height)+63)/64;
    if(ink.step!=step || ink.width!=(b.rasterWidth+step-1)/step || ink.height!=(b.rasterHeight+step-1)/step ||
        ink.sourceRows.size()!=size_t(ink.height) || ink.rawInk.size()!=words || ink.ownedInk.size()!=words)
        {result.reason="invalid-current-owned-grid";return result;}
    bool hasOwned=false;
    for(size_t n=0;n<words;++n) {
        if(ink.ownedInk[n]&~ink.rawInk[n]) {result.reason="owned-grid-not-raw";return result;}
        hasOwned=hasOwned || ink.ownedInk[n]!=0;
    }
    if(!hasOwned) {result.reason="missing-current-owned-glyphs";return result;}
    int previousRow=-1;
    for(int y:ink.sourceRows) {
        if(y<=previousRow || y<0 || y>=b.rasterHeight) {result.reason="invalid-current-owned-rows";return result;}
        previousRow=y;
    }
    const SubtitleBoxRect raster{0,0,b.rasterWidth,b.rasterHeight};
    const SubtitleBoxRect candidate{b.left,b.top,b.right,b.bottom};
    const auto capture=SubtitleGlyphCaptureBounds(o.text,b.rasterWidth,b.rasterHeight);
    if(!contains(raster,candidate) || b.left!=0 || b.right!=b.rasterWidth ||
        !contains(raster,capture)) {result.reason="invalid-assisted-footprint";return result;}
    const int guard=SubtitleBoxDetector::SamplingStep(b.rasterWidth,b.rasterHeight)+1;
    auto protectedBounds=proof.protectedBounds;
    if(protectedBounds.left!=0 || protectedBounds.right!=b.rasterWidth ||
        protectedBounds.rasterWidth!=b.rasterWidth || protectedBounds.rasterHeight!=b.rasterHeight ||
        protectedBounds.top<0 || protectedBounds.bottom>b.rasterHeight || protectedBounds.top>b.top || protectedBounds.bottom<b.bottom)
        {result.reason="invalid-protected-envelope";return result;}
    protectedBounds.top=(std::min)(protectedBounds.top,(std::max)(0,capture.top-guard));
    protectedBounds.bottom=(std::max)(protectedBounds.bottom,(std::min)(b.rasterHeight,capture.bottom+guard));
    protectedBounds.top &= ~1;
    protectedBounds.bottom=(std::min)(b.rasterHeight,(protectedBounds.bottom+1)&~1);
    protectedBounds.aspectRatio=double(b.rasterWidth)/(protectedBounds.bottom-protectedBounds.top);
    protectedBounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
    result.available=true;result.bounds=protectedBounds;result.reason="protected-original-subtitle-envelope";
    if(!input.compositionSucceeded) return result;
    auto uncertainComposition=[&]() {
        result.available=false; result.bounds={}; result.reason="uncertified-composition-footprint"; return result;
    };
    if(!input.acceptedCue || !input.move.valid ||
        (input.backgroundMode!=3 && input.backgroundMode!=5)) return uncertainComposition();
    const auto& accepted=*input.acceptedCue;const auto& move=input.move;
    if(!accepted.detected || accepted.held || accepted.workLimit || accepted.lineCount!=o.text.lineCount ||
        !rectSame(accepted.bounds,o.text.bounds) || !rectSame(move.content,capture) ||
        move.pictureTop!=b.top || move.pictureBottom!=b.bottom || !contains(raster,move.source) ||
        !contains(move.source,move.content) || !contains(candidate,move.destination)) return uncertainComposition();
    const auto lines=SubtitleGlyphCaptureLines(o.text,b.rasterWidth,b.rasterHeight);
    for(int n=0;n<accepted.lineCount;++n)
        if(!rectSame(accepted.lineBounds[n],o.text.lineBounds[n]) || !rectSame(move.glyphLines[n],lines[n]) ||
            !contains(move.content,lines[n])) return uncertainComposition();
    auto panel=move.destination;panel.left-=6;panel.right+=6;
    const bool fromTop=move.source.top+move.source.bottom<move.pictureTop+move.pictureBottom;
    if(move.extendToBar) {if(fromTop)panel.top=(std::min)(panel.top,move.pictureTop);else panel.bottom=(std::max)(panel.bottom,move.pictureBottom);}
    if(!contains(candidate,panel))return uncertainComposition();
    result.composed=true;result.bounds=b;result.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
    result.reason="current-composition-assisted-presentation";return result;
}
}



