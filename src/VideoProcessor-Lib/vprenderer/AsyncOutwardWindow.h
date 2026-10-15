#pragma once

#include <vprenderer/BufferedPictureExpansion.h>
#include <array>

namespace AlphaSourceCrop {
// An immutable future-start broad-picture certificate context. The raw
// contributor identities include their analysis domain; transport matching
// alone is insufficient after a same-generation format change.
struct AsyncOutwardWindowContext {
    std::array<ActivePictureFrameIdentity, OUTWARD_PICTURE_CONFIRMATIONS_REQUIRED> contributors{};
    ActivePictureBounds base, subtitleBase;
    uint64_t policyGeneration=0, continuityGeneration=0;
    uint64_t subtitleEvidenceSourceGeneration=0;
    VerticalBarPresentationAction owner=VerticalBarPresentationAction::NONE;
};

inline bool SameAsyncOutwardBounds(const ActivePictureBounds& a, const ActivePictureBounds& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
        a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight &&
        a.trustedBarAxes==b.trustedBarAxes;
}

inline bool SameAsyncOutwardContext(const AsyncOutwardWindowContext& a,
    const AsyncOutwardWindowContext& b) {
    if (!SameAsyncOutwardBounds(a.base,b.base) ||
        !SameAsyncOutwardBounds(a.subtitleBase,b.subtitleBase) ||
        a.policyGeneration!=b.policyGeneration ||
        a.continuityGeneration!=b.continuityGeneration ||
        a.subtitleEvidenceSourceGeneration!=b.subtitleEvidenceSourceGeneration ||
        a.owner!=b.owner) return false;
    for (size_t i=0;i<a.contributors.size();++i)
        if (!SameAnalysisFrameIdentity(a.contributors[i],b.contributors[i])) return false;
    return true;
}

// Pre-render queue membership has transport identity only. The source render
// must also match the prepared decoded analysis domain before using pixels.
inline bool AsyncOutwardAnalysisCurrent(const AsyncOutwardWindowContext& proof,
    const ActivePictureFrameIdentity& current, uint64_t analysisDomainKey) {
    return analysisDomainKey!=0 &&
        SameActivePictureFrameIdentity(proof.contributors.front(),current) &&
        proof.contributors.front().analysisDomainKey==analysisDomainKey;
}

inline bool AsyncOutwardContextCurrent(const AsyncOutwardWindowContext& proof,
    const ActivePictureFrameIdentity& current, const ActivePictureBounds& base,
    const ActivePictureBounds& subtitleBase, VerticalBarPresentationAction owner,
    uint64_t subtitleEvidenceSourceGeneration, uint64_t policyGeneration,
    uint64_t continuityGeneration) {
    return proof.policyGeneration!=0 && proof.continuityGeneration!=0 &&
        proof.policyGeneration==policyGeneration &&
        proof.continuityGeneration==continuityGeneration &&
        SameActivePictureFrameIdentity(proof.contributors.front(),current) &&
        SameAsyncOutwardBounds(proof.base,base) &&
        proof.owner==owner &&
        (owner==VerticalBarPresentationAction::NONE ||
            (owner==VerticalBarPresentationAction::TRANSLATE &&
             SameAsyncOutwardBounds(proof.subtitleBase,subtitleBase) &&
             proof.subtitleEvidenceSourceGeneration!=0 &&
             proof.subtitleEvidenceSourceGeneration==subtitleEvidenceSourceGeneration));
}
}
