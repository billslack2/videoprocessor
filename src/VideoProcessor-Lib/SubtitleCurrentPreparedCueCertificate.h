#pragma once
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>
#include <memory>

// A tracker may retain an acquired layout while CURRENT complete glyph pixels
// still prove that same cue. This token authorizes only verification of erasure
// actually prepared for that source. It grants no semantic/native acquisition,
// future-frame ownership, or lease renewal.
class SubtitleCurrentPreparedCueCertificate
{
public:
    template<class Observation>
    bool Matches(const Observation& current,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& physicalBase) const
    {
        return m_valid && current.analyzed && !current.discontinuity &&
            !current.pendingRefresh && !current.analysisRefresh &&
            current.independentBarAnalysis && current.barAuthority && current.barEvidence.available &&
            !current.barTrackingAuthority && !current.currentAnchorUsesTrackedEdge &&
            !current.barEvidence.topRevalidatedAtReference && !current.barEvidence.topRevalidatedNearby &&
            !current.barEvidence.bottomRevalidatedAtReference && !current.barEvidence.bottomRevalidatedNearby &&
            !current.barEvidence.captionBackedTop && !current.barEvidence.captionBackedBottom &&
            !current.glyphLocalTop && !current.glyphLocalBottom && !current.inspectionCandidateUsed &&
            current.ink && current.ink==m_ink && current.ink->rawEvidenceComplete && current.ink->paletteValid &&
            current.text.detected && !current.text.held && !current.text.workLimit &&
            !current.text.confirmationPending && current.text.topSearchComplete && current.text.bottomSearchComplete &&
            current.text.lineCount==accepted.lineCount && accepted.lineCount>=1 && accepted.lineCount<=3 &&
            SameAnalysisFrameIdentity(current.identity,m_identity) &&
            current.policyGeneration==m_policy && current.continuityGeneration==m_continuity &&
            current.width==m_width && current.height==m_height &&
            current.pictureTop==m_pictureTop && current.pictureBottom==m_pictureBottom &&
            current.barEvidence.top==current.pictureTop && current.barEvidence.bottom==current.pictureBottom &&
            current.nearBarDistance==m_nearBar && current.optimizationMode==m_optimization && current.scriptHint==m_script &&
            current.sharedPicture.AvailableFor(current.identity,current.width,current.height) &&
            SameBounds(current.sharedPicture.bounds,m_base) && SameBounds(physicalBase,m_base) &&
            SameResult(current.text,m_raw) && SameResult(accepted,m_accepted) &&
            accepted.detected && accepted.held && !accepted.confirmationPending &&
            !accepted.workLimit && !accepted.revised && accepted.cue!=0;
    }
private:
    friend class SubtitleBoxPresentation;
    template<class Observation>
    void Record(const Observation& current,const SubtitleBoxResult& accepted)
    {
        m_valid=true;m_identity=current.identity;m_policy=current.policyGeneration;m_continuity=current.continuityGeneration;
        m_width=current.width;m_height=current.height;m_pictureTop=current.pictureTop;m_pictureBottom=current.pictureBottom;
        m_nearBar=current.nearBarDistance;m_optimization=current.optimizationMode;m_script=current.scriptHint;
        m_ink=current.ink;m_raw=current.text;m_accepted=accepted;m_base=current.sharedPicture.bounds;
    }
    static bool SameBounds(const ActivePictureBounds& a,const ActivePictureBounds& b)
    {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
    }
    static bool SameRect(const SubtitleBoxRect& a,const SubtitleBoxRect& b)
    { return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom; }
    static bool SameResult(const SubtitleBoxResult& a,const SubtitleBoxResult& b)
    {
        if(a.cue!=b.cue || a.observations!=b.observations || a.lineCount!=b.lineCount ||
            a.detected!=b.detected || a.held!=b.held || a.confirmationPending!=b.confirmationPending ||
            a.revised!=b.revised || a.workLimit!=b.workLimit || a.capturePanelMeasured!=b.capturePanelMeasured ||
            a.nearBarEligibilityMeasured!=b.nearBarEligibilityMeasured || a.nearBarEligible!=b.nearBarEligible ||
            a.topSearchComplete!=b.topSearchComplete || a.bottomSearchComplete!=b.bottomSearchComplete ||
            !SameRect(a.bounds,b.bounds) || !SameRect(a.anchor,b.anchor) || !SameRect(a.sourcePanel,b.sourcePanel) ||
            !SameRect(a.provenSourcePanel,b.provenSourcePanel) || !SameRect(a.capturePanel,b.capturePanel) ||
            a.signature!=b.signature || a.anchorSignature!=b.anchorSignature)return false;
        for(size_t i=0;i<a.lineBounds.size();++i)
            if(!SameRect(a.lineBounds[i],b.lineBounds[i]) || a.lineSignatures[i]!=b.lineSignatures[i] ||
                !SameRect(a.linePanels[i],b.linePanels[i]) || !SameRect(a.provenLinePanels[i],b.provenLinePanels[i]) ||
                !SameRect(a.lineCapturePanels[i],b.lineCapturePanels[i]) ||
                a.panelTopEdges[i].y!=b.panelTopEdges[i].y || a.panelTopEdges[i].left!=b.panelTopEdges[i].left ||
                a.panelTopEdges[i].right!=b.panelTopEdges[i].right ||
                a.panelTopEdges[i].supportBasisPoints!=b.panelTopEdges[i].supportBasisPoints)return false;
        return true;
    }
    bool m_valid=false;
    ActivePictureFrameIdentity m_identity;
    uint64_t m_policy=0,m_continuity=0;
    int m_width=0,m_height=0,m_pictureTop=0,m_pictureBottom=0,m_nearBar=0,m_optimization=0;
    SubtitleScriptHint m_script=SubtitleScriptHint::Latin;
    ActivePictureBounds m_base;
    std::shared_ptr<const SubtitleInkSnapshot> m_ink;
    SubtitleBoxResult m_raw,m_accepted;
};
