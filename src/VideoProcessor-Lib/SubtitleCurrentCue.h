#pragma once
#include <SubtitleCueContinuation.h>
#include <memory>

// Nomination for replacing an earlier no-cue GPU packet of this exact source.
// It grants no new cleanup authority: composition has already consumed these
// current proofs. Held/pending/partial observations cannot seal a replacement.
inline bool CanRecoverCurrentSubtitlePacket(bool exactSourceRetry,bool priorIncomplete,
    bool composed,bool workIncomplete,bool budgetExhausted,const SubtitleBoxResult& accepted,
    const SubtitleBoxPreview& preview,const SubtitleCueContinuation& proof,
    const ActivePictureFrameIdentity& identity,uint64_t policy,uint64_t continuity) {
    if(!exactSourceRetry||!priorIncomplete||!composed||workIncomplete||budgetExhausted||
        !accepted.detected||accepted.held||accepted.confirmationPending||accepted.workLimit)return false;
    // Current beta continuation certifies glyphs only, never a complete packet.
    // Late replacement requires the complete current worker observation below.
    (void)proof;
    const auto& current=preview.current;const auto& text=current.text;
    if(!preview.available||preview.policyGeneration!=policy||preview.continuityGeneration!=continuity||
        current.policyGeneration!=policy||current.continuityGeneration!=continuity||
        !SameAnalysisFrameIdentity(current.identity,identity)||!current.analyzed||
        current.pendingRefresh||current.analysisRefresh||current.cadenceSkipped||
        current.barTrackingAuthority||current.currentAnchorUsesTrackedEdge||
        !text.detected||text.held||text.confirmationPending||text.workLimit||
        (current.pictureTop>0&&!text.topSearchComplete)||
        (current.pictureBottom<current.height&&!text.bottomSearchComplete)||
        (!text.topSearchComplete&&!text.bottomSearchComplete)||
        !current.ink||!current.ink->paletteValid||!current.ink->rawEvidenceComplete||
        accepted.lineCount<1||accepted.lineCount>3||accepted.lineCount!=text.lineCount||
        !SubtitleCueContinuation::SameRect(accepted.bounds,text.bounds)||
        !SubtitleCueContinuation::SameRect(accepted.sourcePanel,text.sourcePanel)||
        !SubtitleCueContinuation::SameRect(accepted.capturePanel,text.capturePanel)||
        accepted.capturePanelMeasured!=text.capturePanelMeasured)return false;
    for(int line=0;line<accepted.lineCount;++line)
        if(!SubtitleCueContinuation::SameRect(accepted.lineBounds[line],text.lineBounds[line])||
            !SubtitleCueContinuation::SameRect(accepted.lineCapturePanels[line],text.lineCapturePanels[line]))return false;
    return true;
}

