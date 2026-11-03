#pragma once

#include "SubtitleBoxLookahead.h"
#include "SubtitleCutPaste.h"

// A private request for current-pixel verification against an independently
// accepted native base. This never rebinds the original detector observation or
// supplies crop, absence, or erasure evidence by itself.
inline bool BuildSubtitleCurrentCueProofKey(const SubtitleBoxObservation& sourceKey,
    const ActivePictureFrameIdentity& exactIdentity,const ActivePictureBounds& nativeBase,
    bool nativeContextEligible,SubtitleBoxObservation& outKey)
{
    outKey={};
    if(!nativeContextEligible || sourceKey.discontinuity ||
        !exactIdentity.transportGeneration || !exactIdentity.acceptedSequence ||
        !SameAnalysisFrameIdentity(sourceKey.identity,exactIdentity) ||
        sourceKey.width<=0 || sourceKey.height<=0 ||
        nativeBase.rasterWidth!=sourceKey.width || nativeBase.rasterHeight!=sourceKey.height ||
        nativeBase.left<0 || nativeBase.right>sourceKey.width || nativeBase.left>=nativeBase.right ||
        nativeBase.top<0 || nativeBase.bottom>sourceKey.height || nativeBase.top>=nativeBase.bottom ||
        (nativeBase.top==0 && nativeBase.bottom==sourceKey.height))return false;
    outKey.identity=exactIdentity;
    outKey.width=sourceKey.width;outKey.height=sourceKey.height;
    outKey.nearBarDistance=sourceKey.nearBarDistance;
    outKey.optimizationMode=sourceKey.optimizationMode;
    outKey.scriptHint=sourceKey.scriptHint;
    outKey.analysisStride=sourceKey.analysisStride;
    outKey.cadenceSkipped=sourceKey.cadenceSkipped;
    outKey.policyGeneration=sourceKey.policyGeneration;
    outKey.continuityGeneration=sourceKey.continuityGeneration;
    outKey.sharedPicture.required=true;
    outKey.sharedPicture.bounds=nativeBase;
    outKey.sharedPicture.identity=exactIdentity;
    return true;
}

// Only bounded metadata is examined on the presentation thread. The worker has
// already verified this source's pixels throughout the actual glyph captures,
// cleanup, destination and excluded bars. A rounded grouping box may vary by one
// sampling cell inside capture or a worker-sealed black erasure fringe. The
// latter changes only the metadata admission, never glyph capture/ownership. Card boxes
// from a fresh grouping pass are not substituted for the verified operation.
inline bool CurrentSubtitleCueFitsVerifiedOperation(const SubtitleBoxResult& current,
    const SubtitleBoxResult& accepted,const SubtitleCutPasteGeometry& operation,int sampleStep,
    const std::array<SubtitleBoxRect,3>* verifiedSemanticLineBounds=nullptr)
{
    if(!current.detected || current.held || current.confirmationPending || current.workLimit ||
        !accepted.detected || accepted.held || accepted.confirmationPending || accepted.workLimit ||
        current.lineCount<1 || current.lineCount>3 || current.lineCount!=accepted.lineCount)return false;
    const auto sameRect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    };
    if(!operation.valid) {
        // Classic has no dense operation-footprint witness: retain exact shape.
        if(!sameRect(current.bounds,accepted.bounds) || !sameRect(current.anchor,accepted.anchor) ||
            !sameRect(current.capturePanel,accepted.capturePanel) ||
            current.signature!=accepted.signature || current.anchorSignature!=accepted.anchorSignature)return false;
        for(int i=0;i<current.lineCount;++i)
            if(!sameRect(current.lineBounds[i],accepted.lineBounds[i]) ||
                !sameRect(current.lineCapturePanels[i],accepted.lineCapturePanels[i]) ||
                current.lineSignatures[i]!=accepted.lineSignatures[i])return false;
        return true;
    }
    if(sampleStep<1 || sampleStep>8)return false;
    unsigned matched=0;
    for(int i=0;i<current.lineCount;++i) {
        bool found=false;
        for(int j=0;j<accepted.lineCount;++j) {
            if(matched&(1u<<j))continue;
            const auto& line=current.lineBounds[i];
            const auto& capture=operation.glyphLines[j];
            const auto& semantic=verifiedSemanticLineBounds?(*verifiedSemanticLineBounds)[j]:capture;
            // Only the typed worker receipt supplies wider bounds in production.
            // Check their bounded shape again here; the immutable capture stays exact.
            if(!capture.Valid() || !semantic.Valid() ||
                semantic.left>capture.left || semantic.top>capture.top ||
                semantic.right<capture.right || semantic.bottom<capture.bottom ||
                capture.left-semantic.left>sampleStep || capture.top-semantic.top>sampleStep ||
                semantic.right-capture.right>sampleStep || semantic.bottom-capture.bottom>sampleStep ||
                !SubtitleBoxLookahead::SameLine(line,accepted.lineBounds[j],sampleStep) ||
                line.left<semantic.left || line.top<semantic.top ||
                line.right>semantic.right || line.bottom>semantic.bottom ||
                !SubtitleBoxLookahead::SimilarSignature(current.lineSignatures[i],accepted.lineSignatures[j]))continue;
            matched|=1u<<j;found=true;break;
        }
        if(!found)return false;
    }
    return true;
}
