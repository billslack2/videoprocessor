#pragma once
#include <vprenderer/BufferedPictureExpansion.h>
#include <RememberedEdgeReturn.h>

namespace SourceCadenceNativeHandoffDetail {
inline bool SameBounds(const ActivePictureBounds& a,const ActivePictureBounds& b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
        a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
}
inline bool ValidBounds(const ActivePictureBounds& b) {
    return b.rasterWidth>0 && b.rasterHeight>0 && b.left>=0 && b.top>=0 &&
        b.right>b.left && b.bottom>b.top && b.right<=b.rasterWidth && b.bottom<=b.rasterHeight;
}
inline bool SameContext(const ActivePictureFrameIdentity& a,const ActivePictureFrameIdentity& b) {
    return a.transportGeneration==b.transportGeneration && a.sourceFormatGeneration==b.sourceFormatGeneration &&
        a.analysisDomainKey==b.analysisDomainKey && a.viewportGeneration==b.viewportGeneration &&
        a.rendererGeneration==b.rendererGeneration;
}
inline bool ChangedPublication(const ActivePictureTransitionDecision& d,const ActivePictureBounds& retained) {
    return d.publish && d.stable && d.state==ActivePictureTransitionState::STABLE &&
        (d.authoritativeClassification==ActivePictureClassification::BAR_CROP_TRUSTED ||
         d.authoritativeClassification==ActivePictureClassification::FULL_RASTER_TRUSTED) &&
        ValidBounds(d.bounds) && d.bounds.rasterWidth==retained.rasterWidth &&
        d.bounds.rasterHeight==retained.rasterHeight && !SameBounds(d.bounds,retained);
}
inline bool CurrentDecision(const ActivePictureFrameDecision& d,const ActivePictureFrameIdentity& current,
    const ActivePictureBounds& retained,uint64_t policy,uint64_t continuity) {
    return SameAnalysisFrameIdentity(d.effectiveIdentity,current) &&
        SameContext(d.observationIdentity,current) &&
        d.observationIdentity.acceptedSequence>=current.acceptedSequence &&
        d.observationIdentity.sourceFrameNumber>=current.sourceFrameNumber &&
        d.observationIdentity.captureTimestamp>=current.captureTimestamp &&
        d.lookaheadPolicyGeneration==policy && d.continuityGeneration==continuity &&
        d.proofFrameCount>0 && d.proofFrameCount<=ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES+1 &&
        d.transition.authorityOrigin==ActivePictureAuthorityOrigin::NATIVE && ChangedPublication(d.transition,retained);
}
}

// A presentation-only veto for the one-input first-Ready warmup bridge.
// It never adopts geometry, advances native history or grants caption proof.
inline bool SourceCadenceHasCurrentNativeHandoff(const ActivePictureFrameIdentity& current,
    const ActivePictureBounds& retained,uint64_t policy,uint64_t continuity,
    const ActivePictureFrameDecision* scheduled,
    const AlphaSourceCrop::BufferedPictureExpansionProof* buffered,
    const GuardedRememberedEdgeReturnCertificate* remembered)
{
    using namespace SourceCadenceNativeHandoffDetail;
    if(!current.transportGeneration || !current.acceptedSequence || !current.sourceFrameNumber ||
        !current.captureTimestamp || !current.analysisDomainKey || !policy || !continuity || !ValidBounds(retained))return false;
    if(scheduled && CurrentDecision(*scheduled,current,retained,policy,continuity))return true;
    if(buffered && buffered->valid && CurrentDecision(buffered->decision,current,retained,policy,continuity))return true;
    if(!remembered || !remembered->available || !remembered->diagnostic.allPass ||
        remembered->policyGeneration!=policy || remembered->continuityGeneration!=continuity ||
        remembered->frameCount<ActivePictureTransitionModel::CLEAR_TRANSITION_CONFIRMATIONS ||
        remembered->frameCount>remembered->identities.size())return false;
    const auto& n=remembered->nomination;
    const size_t expected=size_t((std::min)((std::min)(remembered->configuredFuture,remembered->actualFuture),
        uint32_t(ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES)))+1;
    if(remembered->frameCount!=expected || remembered->diagnostic.expectedFrames!=expected ||
        remembered->diagnostic.passedFrames!=expected || !n.available || !n.guarded || n.shadowOnly ||
        n.policyGeneration!=policy || n.sourceGeneration!=current.transportGeneration ||
        n.sourceSequence!=current.acceptedSequence || n.rendererGeneration!=current.rendererGeneration ||
        n.viewportGeneration!=current.viewportGeneration || n.sourceFormatGeneration!=current.sourceFormatGeneration ||
        !SameBounds(n.establishedBase,retained) || !SameAnalysisFrameIdentity(remembered->identities[0],current) ||
        !remembered->currentEvidence.available ||
        remembered->currentEvidence.authorityOrigin!=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN ||
        remembered->currentEvidence.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        !SameBounds(remembered->currentEvidence.trustedBounds,remembered->decision.bounds) ||
        !SameBounds(remembered->diagnostic.presentation,remembered->decision.bounds) ||
        remembered->decision.authorityOrigin!=ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN ||
        !ChangedPublication(remembered->decision,retained))return false;
    for(size_t i=1;i<expected;++i) {
        const auto& id=remembered->identities[i];const auto& previous=remembered->identities[i-1];
        if(!SameContext(id,current) || id.acceptedSequence<current.acceptedSequence ||
            id.acceptedSequence-current.acceptedSequence!=i || id.sourceFrameNumber<current.sourceFrameNumber ||
            id.sourceFrameNumber-current.sourceFrameNumber!=i || id.captureTimestamp<=previous.captureTimestamp)return false;
    }
    return true;
}
