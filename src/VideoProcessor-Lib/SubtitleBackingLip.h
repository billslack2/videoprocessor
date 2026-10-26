#pragma once
#include <SubtitleCutPaste.h>
#include <SubtitleCueContinuation.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Exact-current cleanup only. Never an acquired card, glyph/capture mask,
// retained display envelope, or aspect-ratio certificate.
enum SubtitleBackingLipContextMismatch : uint32_t
{
    LipContextNoSequence=1u<<0,
    LipContextPreviewUnavailable=1u<<1,
    LipContextNotAnalyzed=1u<<2,
    LipContextDiscontinuity=1u<<3,
    LipContextAnalysisRefresh=1u<<4,
    LipContextCadenceSkipped=1u<<5,
    LipContextGlyphLocalTop=1u<<6,
    LipContextGlyphLocalBottom=1u<<7,
    LipContextBarAuthorityUnavailable=1u<<8,
    LipContextBarEvidenceUnavailable=1u<<9,
    LipContextIdentityMismatch=1u<<10,
    LipContextPreviewPolicyMismatch=1u<<11,
    LipContextCurrentPolicyMismatch=1u<<12,
    LipContextPreviewContinuityMismatch=1u<<13,
    LipContextCurrentContinuityMismatch=1u<<14,
    LipContextCueSideTopUnavailable=1u<<15,
    LipContextCueSideBottomUnavailable=1u<<16,
    LipContextCurrentAnchorTrackedEdge=1u<<17,
    LipContextLocalContinuationUnavailable=1u<<18
};

enum SubtitleBackingLipEdgeEvidence : uint32_t
{
    LipEdgeTopCurrent=1u<<0,
    LipEdgeBottomCurrent=1u<<1,
    LipEdgeTopCaptionBacked=1u<<2,
    LipEdgeBottomCaptionBacked=1u<<3,
    LipEdgeTopReference=1u<<4,
    LipEdgeBottomReference=1u<<5,
    LipEdgeTopNearbyReference=1u<<6,
    LipEdgeBottomNearbyReference=1u<<7
};

inline bool HasExactCurrentLocalGlyphContinuationForLip(const SubtitleCueContinuation* certificate,
    const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
    const ActivePictureFrameIdentity& identity,uint64_t policy,uint64_t continuity)
{
    if(!accepted.held || accepted.confirmationPending || accepted.workLimit || accepted.revised ||
        !certificate || !certificate->valid || !certificate->localGlyphsOnly ||
        certificate->backingVerifiedBounds.Valid() || !certificate->ink ||
        !certificate->ink->paletteValid || !certificate->sampledBounds.Valid() ||
        !preview.available || !preview.current.analyzed || preview.current.discontinuity ||
        preview.current.analysisRefresh || preview.current.pendingRefresh ||
        preview.policyGeneration!=policy || preview.continuityGeneration!=continuity ||
        preview.current.policyGeneration!=policy || preview.current.continuityGeneration!=continuity ||
        !SameAnalysisFrameIdentity(preview.current.identity,identity) ||
        !preview.current.sharedPicture.AvailableFor(identity,preview.current.width,preview.current.height) ||
        !certificate->Matches(identity,policy,continuity,accepted,preview.current.sharedPicture.bounds) ||
        !preview.current.text.detected || preview.current.text.held ||
        preview.current.text.confirmationPending || preview.current.text.workLimit || preview.current.text.revised ||
        !preview.current.text.nearBarEligibilityMeasured || !preview.current.text.nearBarEligible ||
        !preview.current.ink || !preview.current.ink->rawEvidenceComplete || !preview.current.ink->paletteValid)
        return false;
    auto localGlyphs=accepted;localGlyphs.held=false;
    const auto glyphCapture=SubtitleGlyphCaptureBounds(localGlyphs,preview.current.width,preview.current.height);
    const bool sampleContainsGlyphs=glyphCapture.Valid() &&
        certificate->sampledBounds.left<=glyphCapture.left && certificate->sampledBounds.top<=glyphCapture.top &&
        certificate->sampledBounds.right>=glyphCapture.right && certificate->sampledBounds.bottom>=glyphCapture.bottom;
    return sampleContainsGlyphs && SameFreshSubtitleGlyphs(localGlyphs,preview.current.text,0);
}

struct CurrentSubtitleBackingLipProof
{
    bool valid=false;
    bool pixelInspectionPerformed=false,candidateRetained=false;
    uint64_t candidateAgeFrames=0;
    uint32_t contextMismatchMask=0,edgeEvidenceMask=0;
    const char* reason="not-evaluated";
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration=0,continuityGeneration=0;
    int width=0,height=0,pictureTop=0,pictureBottom=0;
    bool fromTopBar=false;
    SubtitleBoxRect measured,cleanup,blackInterior;
    size_t samples=0,cornerPixels=0;
    unsigned closureSamples=0,closureDark=0,closureLongestDarkRun=0;
    bool attachmentSeamClipped=false;
    bool compositionEdgeClipped=false;
    unsigned roundingRows=0;
    // A remembered outline nominates probes only. Every body pixel and the
    // surrounding boundary are revalidated against the selected native frame.
    std::array<SubtitleBoxRect,128> bodyRows{};
    unsigned bodyRowCount=0;
    bool revalidatedOutline=false;
    bool outlineBaseAnchored=false;
};

inline CurrentSubtitleBackingLipProof ProveCurrentSubtitleBackingLip(
    const AnalysisLumaSource& source,const SubtitleBoxPreview& preview,
    const SubtitleBoxResult& accepted,const ActivePictureFrameIdentity& expectedIdentity,
    uint64_t policy,uint64_t continuity,size_t maxSamples=65536,
    const CurrentSubtitleBackingLipProof* candidate=nullptr,
    const SubtitleCutPasteGeometry* composition=nullptr,
    const SubtitleCueContinuation* glyphContinuity=nullptr)
{
    CurrentSubtitleBackingLipProof result;
    result.identity=expectedIdentity;result.policyGeneration=policy;result.continuityGeneration=continuity;
    result.width=source.width;result.height=source.height;
    const auto reject=[&](const char* why){result.reason=why;return result;};
    const auto& current=preview.current;
    result.pictureTop=current.pictureTop;result.pictureBottom=current.pictureBottom;
    const bool localGlyphContinuation=HasExactCurrentLocalGlyphContinuationForLip(
        glyphContinuity,preview,accepted,expectedIdentity,policy,continuity);
    auto acceptedForProof=accepted;
    if(localGlyphContinuation)acceptedForProof.held=false;
    auto candidateEpoch=candidate?candidate->identity:ActivePictureFrameIdentity{};
    candidateEpoch.acceptedSequence=expectedIdentity.acceptedSequence;
    candidateEpoch.sourceFrameNumber=expectedIdentity.sourceFrameNumber;
    candidateEpoch.captureTimestamp=expectedIdentity.captureTimestamp;
    const bool candidateContext=candidate && candidate->valid &&
        SameAnalysisFrameIdentity(candidateEpoch,expectedIdentity) &&
        candidate->policyGeneration==policy && candidate->continuityGeneration==continuity &&
        expectedIdentity.acceptedSequence>=candidate->identity.acceptedSequence &&
        expectedIdentity.acceptedSequence-candidate->identity.acceptedSequence<=8 &&
        candidate->width==source.width && candidate->height==source.height;
    // The cached outline is only a coordinate candidate. It is tied to the
    // same cue/epoch below and cannot authorize cleanup until every current
    // body and boundary sample has been revalidated.
    const bool boundedOutline=candidateContext && candidate->bodyRowCount>=2 &&
        candidate->bodyRowCount<=128 && candidate->measured.Valid() && candidate->blackInterior.Valid() &&
        candidate->cleanup.left==candidate->blackInterior.left && candidate->cleanup.top==candidate->blackInterior.top &&
        candidate->cleanup.right==candidate->blackInterior.right && candidate->cleanup.bottom==candidate->blackInterior.bottom &&
        candidate->blackInterior.left>=candidate->measured.left && candidate->blackInterior.top>=candidate->measured.top &&
        candidate->blackInterior.right<=candidate->measured.right && candidate->blackInterior.bottom<=candidate->measured.bottom &&
        candidate->pictureTop>=0 && candidate->pictureBottom<=source.height &&
        candidate->pictureTop<candidate->pictureBottom &&
        candidate->measured.top>=candidate->pictureTop && candidate->measured.bottom<=candidate->pictureBottom;
    if(current.pictureTop>0 && current.barEvidence.top==current.pictureTop)result.edgeEvidenceMask|=LipEdgeTopCurrent;
    if(current.pictureBottom<source.height && current.barEvidence.bottom==current.pictureBottom)result.edgeEvidenceMask|=LipEdgeBottomCurrent;
    if(current.barEvidence.captionBackedTop)result.edgeEvidenceMask|=LipEdgeTopCaptionBacked;
    if(current.barEvidence.captionBackedBottom)result.edgeEvidenceMask|=LipEdgeBottomCaptionBacked;
    if(current.barEvidence.topRevalidatedAtReference)result.edgeEvidenceMask|=LipEdgeTopReference;
    if(current.barEvidence.bottomRevalidatedAtReference)result.edgeEvidenceMask|=LipEdgeBottomReference;
    if(current.barEvidence.topRevalidatedNearby)result.edgeEvidenceMask|=LipEdgeTopNearbyReference;
    if(current.barEvidence.bottomRevalidatedNearby)result.edgeEvidenceMask|=LipEdgeBottomNearbyReference;
    if(!expectedIdentity.acceptedSequence)result.contextMismatchMask|=LipContextNoSequence;
    if(!preview.available)result.contextMismatchMask|=LipContextPreviewUnavailable;
    if(!current.analyzed)result.contextMismatchMask|=LipContextNotAnalyzed;
    if(current.discontinuity)result.contextMismatchMask|=LipContextDiscontinuity;
    if(current.analysisRefresh)result.contextMismatchMask|=LipContextAnalysisRefresh;
    if(current.cadenceSkipped)result.contextMismatchMask|=LipContextCadenceSkipped;
    if(current.currentAnchorUsesTrackedEdge)result.contextMismatchMask|=LipContextCurrentAnchorTrackedEdge;
    if(accepted.held && !localGlyphContinuation)
        result.contextMismatchMask|=LipContextLocalContinuationUnavailable;
    if(current.glyphLocalTop)result.contextMismatchMask|=LipContextGlyphLocalTop;
    if(current.glyphLocalBottom)result.contextMismatchMask|=LipContextGlyphLocalBottom;
    if(!current.barAuthority)result.contextMismatchMask|=LipContextBarAuthorityUnavailable;
    if(!current.barEvidence.available)result.contextMismatchMask|=LipContextBarEvidenceUnavailable;
    if(!SameAnalysisFrameIdentity(current.identity,expectedIdentity))result.contextMismatchMask|=LipContextIdentityMismatch;
    if(preview.policyGeneration!=policy)result.contextMismatchMask|=LipContextPreviewPolicyMismatch;
    if(current.policyGeneration!=policy)result.contextMismatchMask|=LipContextCurrentPolicyMismatch;
    if(preview.continuityGeneration!=continuity)result.contextMismatchMask|=LipContextPreviewContinuityMismatch;
    if(current.continuityGeneration!=continuity)result.contextMismatchMask|=LipContextCurrentContinuityMismatch;
    const uint32_t sideLocalMask=LipContextGlyphLocalTop|LipContextGlyphLocalBottom;
    if(result.contextMismatchMask&~sideLocalMask)return reject("lip-current-context-unavailable");
    if(!source.IsValid() || source.width<64 || source.height<48 || source.width>8192 || source.height>4320 ||
        source.generation!=expectedIdentity.transportGeneration || current.width!=source.width || current.height!=source.height ||
        current.pictureTop<0 || current.pictureBottom>source.height || current.pictureTop>=current.pictureBottom)
        return reject("lip-source-or-physical-base-mismatch");
    if(!acceptedForProof.cue || !acceptedForProof.detected || acceptedForProof.held ||
        acceptedForProof.confirmationPending || acceptedForProof.workLimit || acceptedForProof.revised ||
        acceptedForProof.observations<1 || !current.text.detected || current.text.held ||
        current.text.confirmationPending || current.text.workLimit || current.text.revised ||
        !current.text.nearBarEligibilityMeasured || !current.text.nearBarEligible ||
        !acceptedForProof.nearBarEligibilityMeasured || !acceptedForProof.nearBarEligible ||
        !SameFreshSubtitleGlyphs(acceptedForProof,current.text,0) ||
        acceptedForProof.lineCount<1 || acceptedForProof.lineCount>3 || !current.ink ||
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
    const auto captureLines=SubtitleGlyphCaptureLines(acceptedForProof,source.width,source.height);
    for(int i=0;i<acceptedForProof.lineCount;++i){
        const auto& row=acceptedForProof.lineBounds[i];
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
                    result.pixelInspectionPerformed=true;
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
    // Cleanup only needs the physical edge on the subtitle's own side.
    // Missing or tracked evidence on the opposite edge cannot veto a fresh
    // local contour, but it cannot enlarge the returned cleanup rectangle.
    const bool cueSideGlyphLocal=allTop?current.glyphLocalTop:current.glyphLocalBottom;
    const int edgeStep=(std::max)(1,source.height/540);
    const bool cueSideRawEdge=allTop?
        current.barEvidence.topDepth>=2*edgeStep+1 && current.barEvidence.topSupport>=26 &&
            current.barEvidence.topBoundarySupport>=8 && current.barEvidence.topBlackFraction>=0.85 &&
            current.barEvidence.topMaxIntrusion<25:
        current.barEvidence.bottomDepth>=2*edgeStep+1 && current.barEvidence.bottomSupport>=26 &&
            current.barEvidence.bottomBoundarySupport>=8 && current.barEvidence.bottomBlackFraction>=0.85 &&
            current.barEvidence.bottomMaxIntrusion<25;
    const bool cueSideCurrent=current.barAuthority && current.barEvidence.available && cueSideRawEdge && (allTop?
        current.pictureTop>0 && current.barEvidence.top==current.pictureTop &&
            !cueSideGlyphLocal && !current.barEvidence.captionBackedTop && !current.barEvidence.topRevalidatedAtReference &&
            !current.barEvidence.topRevalidatedNearby:
        current.pictureBottom<source.height && current.barEvidence.bottom==current.pictureBottom &&
            !cueSideGlyphLocal && !current.barEvidence.captionBackedBottom && !current.barEvidence.bottomRevalidatedAtReference &&
            !current.barEvidence.bottomRevalidatedNearby);
    const bool candidateCueSideWithinTolerance=boundedOutline && candidate->fromTopBar==allTop &&
        (allTop?std::abs(int64_t(current.pictureTop)-candidate->pictureTop)<=3:
            std::abs(int64_t(current.pictureBottom)-candidate->pictureBottom)<=3);
    // A caption-backed cue-side edge can revalidate an existing contour only
    // when it moved outward by at most three rows. It can never acquire a lip.
    const bool captionEdgeOutward=candidateCueSideWithinTolerance && !cueSideGlyphLocal && (allTop?
        current.barEvidence.captionBackedTop && current.pictureTop<candidate->pictureTop:
        current.barEvidence.captionBackedBottom && current.pictureBottom>candidate->pictureBottom);
    if(!cueSideCurrent && !captionEdgeOutward) {
        result.contextMismatchMask|=allTop?LipContextCueSideTopUnavailable:LipContextCueSideBottomUnavailable;
        return reject("lip-current-context-unavailable");
    }
    if(candidateCueSideWithinTolerance && (cueSideCurrent || captionEdgeOutward)) {
        if(allTop)result.pictureTop=(std::max)(result.pictureTop,candidate->pictureTop);
        else result.pictureBottom=(std::min)(result.pictureBottom,candidate->pictureBottom);
        result.outlineBaseAnchored=(allTop?result.pictureTop!=current.pictureTop:
            result.pictureBottom!=current.pictureBottom) || captionEdgeOutward;
    }
    if(composition) {
        if(!composition->valid || composition->fromTopBar!=allTop ||
            composition->pictureTop<0 || composition->pictureBottom>source.height ||
            composition->pictureTop>=composition->pictureBottom)
            return reject("lip-composition-context-unavailable");
        // Native bars own backing detection; stable display geometry only
        // limits which proved pixels are visible. Neither interval may enlarge
        // the current cleanup proof into unmeasured picture or subtitle glyphs.
        result.pictureTop=(std::max)(result.pictureTop,composition->pictureTop);
        result.pictureBottom=(std::min)(result.pictureBottom,composition->pictureBottom);
        if(result.pictureTop>=result.pictureBottom)
            return reject("lip-composition-context-unavailable");
        result.compositionEdgeClipped=allTop?result.pictureTop!=current.pictureTop:
            result.pictureBottom!=current.pictureBottom;
    }
    const int glyphHeight=SubtitleTypicalLineHeight(acceptedForProof);
    if(glyphHeight<4 || glyphHeight>source.height/4)
        return reject("lip-glyph-height-unavailable");
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
        result.pixelInspectionPerformed=true;
        if(!source.Sample(x,y,value)){failedRead=true;return false;}
        return true;
    };
    // A scene shadow can touch the immediate outside row of a finite card.
    // Search a bounded collar into picture content for the same distributed
    // closure. These probes NEVER enlarge the removal rectangle or skip a
    // nonblack pixel in its body. All reads share the caller's work budget.
    const auto closure=[&](int left,int right,int edgeY,bool requireBrightEnds,bool remembered=false) {
        if(left>=right)return false;
        const int limit=(std::min)(32,(std::max)(1,glyphHeight/2));
        for(int offset=0;offset<=limit;offset=offset==0?1:offset*2) {
            const int y=edgeY+(allTop?offset:-offset);
            if(y<result.pictureTop || y>=result.pictureBottom)break;
            std::array<unsigned,8> total{},bright{};
            const int bins=remembered?4:8;
            unsigned run=0;bool endsBright=true;
            result.closureSamples=result.closureDark=result.closureLongestDarkRun=0;
            for(int x=left;x<right;++x) {
                AnalysisLumaSample value;
                if(!sample(x,y,value))return false;
                const int bin=(std::min)(bins-1,int(int64_t(x-left)*bins/(right-left)));
                ++total[bin];++result.closureSamples;
                if(value.luma<=darkLimit) {
                    ++result.closureDark;++run;
                    if(x==left || x==right-1)endsBright=false;
                    result.closureLongestDarkRun=(std::max)(result.closureLongestDarkRun,run);
                } else {++bright[bin];run=0;}
            }
            // First acquisition remains strict. An already finite, unchanged
            // outline can tolerate a small moving scene shadow above it: keep
            // the same global 10% dark limit and distributed bright support.
            // The relaxed collar never contributes pixels to the cleanup.
            const unsigned maxRun=remembered?
                unsigned((std::min)({64,(std::max)(6,glyphHeight),(std::max)(6,(right-left)/16)})):
                unsigned((std::min)({32,(std::max)(3,glyphHeight/3),(std::max)(3,(right-left)/32)}));
            bool supported=(!requireBrightEnds || endsBright) && result.closureDark*10<=result.closureSamples &&
                result.closureLongestDarkRun<=maxRun;
            for(int bin=0;bin<bins;++bin)
                supported &= total[bin] && bright[bin]*3>=total[bin]*2;
            if(supported)return true;
            // An interior bright hole is not a card boundary. A collar may
            // corroborate an already mostly exposed edge, never search past
            // an ambiguous black row or a shadow reaching either endpoint.
            if(offset==0 && ((requireBrightEnds && !endsBright) ||
                (result.closureSamples-result.closureDark)*3<result.closureSamples*2))return false;
        }
        return false;
    };
    if(candidateCueSideWithinTolerance &&
        (allTop?candidate->pictureTop==result.pictureTop:candidate->pictureBottom==result.pictureBottom) &&
        candidate->measured.top>=result.pictureTop && candidate->measured.bottom<=result.pictureBottom) {
        // Once a finite card has been measured, scene texture beside it must
        // not repeatedly grow/shrink its outline. Require the SAME current
        // glyphs (caller), exact current ownership (above), and exhaustive black
        // body evidence. This is not a timed hold and cannot authorize AR/capture.
        unsigned leftBright=0,rightBright=0;
        size_t edgePixels=0;
        // Native card antialiasing changes with the scene immediately outside
        // its rounded edge. Permit only a two-pixel collar INSIDE the already
        // measured mask; the interior stays exhaustively black/neutral.
        const int collar=(std::min)(2,(std::max)(1,glyphHeight/8));
        for(unsigned row=0;row<candidate->bodyRowCount;++row) {
            const auto& r=candidate->bodyRows[row];
            if(!inRaster(r) || r.bottom!=r.top+1 || r.left<3 || r.right>source.width-3 ||
                r.top<candidate->measured.top || r.bottom>candidate->measured.bottom ||
                r.left<candidate->measured.left || r.right>candidate->measured.right)
                return reject("lip-outline-invalid");
            if(candidate->blackInterior.left<r.left || candidate->blackInterior.right>r.right ||
                r.top<candidate->blackInterior.top || r.top>=candidate->blackInterior.bottom)
                return reject("lip-outline-interior-not-in-current-body");
            for(int x=r.left;x<r.right;++x) {
                AnalysisLumaSample value;
                if(!sample(x,r.top,value))return reject(exhausted?"lip-work-budget":"lip-native-sample-unavailable");
                const bool inPreviousInterior=x>=candidate->blackInterior.left && x<candidate->blackInterior.right;
                if(inPreviousInterior && (value.luma>darkLimit ||
                    std::abs(int(value.chromaU)-512)>32 || std::abs(int(value.chromaV)-512)>32))
                    return reject("lip-outline-body-changed");
                if(value.luma>darkLimit) {
                    if(x>=r.left+collar && x<r.right-collar)return reject("lip-outline-body-changed");
                    ++edgePixels;
                }
                if(x>=r.left+6 && x<r.right-6 &&
                    (std::abs(int(value.chromaU)-512)>32 || std::abs(int(value.chromaV)-512)>32))
                    return reject("lip-outline-body-changed");
            }
            // Distributed flank evidence tolerates isolated scene shadows. No
            // outside pixel is ever added to the remembered removal rectangle.
            bool l=false,rgt=false;
            for(int dx=1;dx<=3;++dx) {
                AnalysisLumaSample a,b;
                if(!sample(r.left-dx,r.top,a) || !sample(r.right+dx-1,r.top,b))
                    return reject(exhausted?"lip-work-budget":"lip-native-sample-unavailable");
                l|=a.luma>darkLimit;rgt|=b.luma>darkLimit;
            }
            leftBright+=l;rightBright+=rgt;
        }
        const size_t uncertain=candidate->cornerPixels+edgePixels;
        const size_t area=size_t(candidate->measured.right-candidate->measured.left)*candidate->bodyRowCount;
        if(uncertain*100>area*5 || uncertain>size_t(glyphHeight)*glyphHeight/4)
            return reject("lip-outline-edge-area-too-large");
        const auto& last=candidate->bodyRows[candidate->bodyRowCount-1];
        const int closureY=allTop?last.bottom:last.top-1;
        if(closureY<result.pictureTop || closureY>=result.pictureBottom)
            return reject("lip-outline-closure-unavailable");
        const int left=last.left+6,right=last.right-6;
        if(leftBright*2<candidate->bodyRowCount || rightBright*2<candidate->bodyRowCount ||
            !closure(left,right,closureY,false,true))
            return reject(exhausted?"lip-work-budget":failedRead?"lip-native-sample-unavailable":"lip-outline-boundary-occluded");
        result.measured=candidate->measured;
        // Preserve the original rectangle only after every pixel passes current
        // luma/chroma checks; this prevents rounded-edge cleanup jitter.
        result.blackInterior=candidate->blackInterior;
        if(!result.blackInterior.Valid())return reject("lip-outline-no-black-interior");
        // The rectangular mask is the exhaustive black intersection. Rounded
        // corner pixels remain live picture and are never sent to reconstruction.
        result.cleanup=result.blackInterior;
        result.cornerPixels=candidate->cornerPixels;
        result.bodyRows=candidate->bodyRows;result.bodyRowCount=candidate->bodyRowCount;
        result.roundingRows=candidate->roundingRows;result.revalidatedOutline=true;
        result.valid=true;result.reason="current-revalidated-backing-outline";return result;
    }
    // A larger glyph-to-picture gap is accepted only when the finite,
    // attached contour above is freshly proved. Keep the distance bounded to
    // two typical line heights; no broad bar/strip fallback is granted.
    const int gap=allTop?result.pictureTop-glyphs.bottom:glyphs.top-result.pictureBottom;
    if(gap<0 || gap>(std::max)(32,2*glyphHeight))
        return reject("lip-glyphs-too-far-from-edge");
    // Reuse chroma from each exhaustive native luma probe; no second sampling
    // pass, image conversion, allocation, or persistent image storage.
    std::array<AnalysisLumaSample,8192> rowPixels;
    struct Row {int left=0,right=0,y=0;};
    Row first,last;
    std::array<int,128> neutralLeftByRow{},neutralRightByRow{};
    int rows=0,commonLeft=0,commonRight=source.width,minLeft=source.width,maxRight=0;
    size_t rowBodyPixels=0;
    bool closed=false,seamNeedsCorroboration=false;
    for(int depth=0;depth<=depthLimit;++depth){
        const int y=allTop?result.pictureTop+depth:result.pictureBottom-1-depth;
        if(y<result.pictureTop || y>=result.pictureBottom)break;
        if(!sample(center,y,rowPixels[center]))break;
        if(rowPixels[center].luma>darkLimit){
            if(!rows)return reject("lip-no-attached-black-backing");
            const int left=last.left+6,right=last.right-6;
            if(left>=right)return reject("lip-top-closure-too-narrow");
            // The backing body remains exhaustively black. The adjacent
            // OUTSIDE row is scene content and can contain small shadows.
            // Require distributed contrast, without adding that row to cleanup
            // or searching past an ambiguous closure into more picture.
            if(!closure(left,right,y,true)) {
                if(exhausted || failedRead)break;
                return reject("lip-top-closure-occluded");
            }
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
        // Rounded corners above the text need not cover every lower glyph's
        // horizontal projection. The bar-attached row must cover all glyphs;
        // later rows remain bounded by the measured monotone card outline.
        if(!rows && (left>glyphs.left || right<glyphs.right))
            return reject("lip-black-span-does-not-cover-glyphs");
        bool finiteFlanks=true;
        for(int distance=0;distance<3;++distance){
            AnalysisLumaSample a,b;
            if(!sample(left-1-distance,y,a) || !sample(right+distance,y,b))break;
            finiteFlanks &= a.luma>darkLimit && b.luma>darkLimit;
        }
        if(exhausted || failedRead)break;
        if(!finiteFlanks){
            // Only the raster row immediately adjoining the physical bar may
            // touch unrelated scene black. It supplies no horizontal envelope.
            // All further rows must have the original strict finite flanks.
            if(rows)return reject("lip-flank-occluded");
            seamNeedsCorroboration=true;
        }
        if(rows==1 && seamNeedsCorroboration){
            // The next independently finite row may NARROW the seam rectangle,
            // never enlarge it into the attached scene. Its retained pixels
            // were already exhaustively black/neutral on the previous row.
            const int maximumSeamContact=(std::max)(1,glyphHeight/4);
            if(left<first.left || right>first.right || left-first.left>maximumSeamContact ||
                first.right-right>maximumSeamContact)
                return reject("lip-attachment-seam-unbounded");
            for(int x=left;x<right;++x){
                AnalysisLumaSample value;if(!sample(x,first.y,value))break;
                if(value.luma>darkLimit)return reject("lip-attachment-seam-body-mismatch");
            }
            if(exhausted || failedRead)break;
            first.left=last.left=left;first.right=last.right=right;
            neutralLeftByRow[0]=(std::max)(neutralLeftByRow[0],left);
            neutralRightByRow[0]=(std::min)(neutralRightByRow[0],right);
            commonLeft=minLeft=neutralLeftByRow[0];commonRight=maxRight=neutralRightByRow[0];
            rowBodyPixels=size_t(right-left);result.attachmentSeamClipped=true;
            result.bodyRows[0]={left,first.y,right,first.y+1};
        }
        if(rows){
            const int maximumRowInset=(std::max)(1,glyphHeight/8);
            if(left<last.left || right>last.right || left-last.left>maximumRowInset ||
                last.right-right>maximumRowInset ||
                left-first.left>glyphHeight/2 || first.right-right>glyphHeight/2)
                return reject("lip-non-card-side-geometry");
        }else first={left,right,y};
        // Decoded 4:2:2 chroma may bleed into a bounded edge collar. Trim
        // only contiguous nonneutral pixels touching either edge; every pixel
        // retained for cleanup must be neutral on acquisition and revalidation.
        const auto neutral=[&](int x){return std::abs(int(rowPixels[x].chromaU)-512)<=32 &&
            std::abs(int(rowPixels[x].chromaV)-512)<=32;};
        int neutralLeft=left,neutralRight=right;
        while(neutralLeft<(std::min)(right,left+6) && !neutral(neutralLeft))++neutralLeft;
        while(neutralRight>(std::max)(left,right-6) && !neutral(neutralRight-1))--neutralRight;
        if(neutralLeft>=neutralRight)return reject("lip-colored-interior");
        for(int x=neutralLeft;x<neutralRight;++x)
            if(!neutral(x))return reject("lip-colored-interior");
        neutralLeftByRow[rows]=neutralLeft;neutralRightByRow[rows]=neutralRight;
        if(rows && (left>first.left || right<first.right))++result.roundingRows;
        result.bodyRows[rows]={left,y,right,y+1};
        result.bodyRowCount=unsigned(rows+1);
        last={left,right,y};++rows;
        commonLeft=(std::max)(commonLeft,neutralLeftByRow[rows-1]);
        commonRight=(std::min)(commonRight,neutralRightByRow[rows-1]);
        minLeft=(std::min)(minLeft,left);maxRight=(std::max)(maxRight,right);
        rowBodyPixels+=size_t(right-left);
    }
    if(exhausted)return reject("lip-work-budget");
    if(failedRead)return reject("lip-native-sample-unavailable");
    if(!closed)return reject("lip-top-closure-unavailable");
    if(rows<(seamNeedsCorroboration?3:2))return reject("lip-insufficient-native-row-support");
    result.blackInterior={commonLeft,allTop?result.pictureTop:last.y,commonRight,
        allTop?last.y+1:result.pictureBottom};
    if(!result.blackInterior.Valid())return reject("lip-no-common-cleanup-interior");
    // The measured rows establish a finite backing outline. Cleanup uses only
    // their common luma-dark and chroma-neutral intersection; edge fringes
    // trimmed for chroma bleed and rounded live corners remain outside it.
    result.measured={minLeft,result.blackInterior.top,maxRight,result.blackInterior.bottom};
    result.cornerPixels=size_t(maxRight-minLeft)*size_t(rows)-rowBodyPixels;
    const size_t boundingPixels=size_t(maxRight-minLeft)*size_t(rows);
    if(result.cornerPixels>size_t(glyphHeight)*size_t(glyphHeight)/4 ||
        result.cornerPixels*100>boundingPixels*5)
        return reject("lip-rounded-corner-area-too-large");
    if(result.roundingRows>unsigned((std::max)(1,glyphHeight/3)))
        return reject("lip-rounded-corner-depth-too-large");
    result.cleanup=result.blackInterior; // Preserve rounded pixels outside the exhaustive black interior.
    result.valid=true;result.reason="current-finite-rounded-backing-lip";
    return result;
}

// Apply only to a local composition AFTER all stable layout caches. The exact
// current source owns cleanup; the caller never writes this value into a cache.
inline SubtitleCutPasteGeometry ApplyCurrentSubtitleBackingLip(
    SubtitleCutPasteGeometry geometry,const CurrentSubtitleBackingLipProof& proof,
    const ActivePictureFrameIdentity& expectedIdentity,uint64_t policy,uint64_t continuity,
    const char** applicationReason=nullptr)
{
    const auto reject=[&](const char* why){if(applicationReason)*applicationReason=why;return geometry;};
    const auto contains=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b){return a.Valid() && b.Valid() &&
        b.left>=a.left && b.top>=a.top && b.right<=a.right && b.bottom<=a.bottom;};
    if(!geometry.valid || !proof.valid)return reject("lip-application-unavailable");
    if(geometry.generatedCleanup.Valid() || geometry.cleanupLimit.Valid())
        return reject("lip-application-existing-cleanup");
    if(!SameAnalysisFrameIdentity(proof.identity,expectedIdentity) ||
        proof.policyGeneration!=policy || proof.continuityGeneration!=continuity)
        return reject("lip-application-context-mismatch");
    // Current native pixels own backing proof. The displayed picture may be
    // wider than that independent physical interval; it supplies no new cleanup
    // authority. Every proved cleanup pixel must fit BOTH picture intervals.
    // Neither edge, the glyph anchor, nor the AR owner's crop is changed.
    const SubtitleBoxRect picture{0,proof.pictureTop,proof.width,proof.pictureBottom};
    const SubtitleBoxRect composedPicture{0,geometry.pictureTop,proof.width,geometry.pictureBottom};
    if(proof.width<1 || proof.height<1 || proof.pictureTop<0 || proof.pictureBottom>proof.height ||
        !picture.Valid() || geometry.pictureTop<0 || geometry.pictureBottom>proof.height ||
        !composedPicture.Valid() || geometry.fromTopBar!=proof.fromTopBar ||
        !contains(picture,proof.cleanup) || !contains(composedPicture,proof.cleanup))
        return reject("lip-application-physical-binding-mismatch");
    if(!contains(proof.measured,proof.blackInterior) ||
        !contains(proof.blackInterior,proof.cleanup) ||
        proof.cleanup.left!=proof.blackInterior.left || proof.cleanup.top!=proof.blackInterior.top ||
        proof.cleanup.right!=proof.blackInterior.right || proof.cleanup.bottom!=proof.blackInterior.bottom ||
        (proof.fromTopBar?proof.blackInterior.top!=proof.pictureTop:proof.blackInterior.bottom!=proof.pictureBottom))
        return reject("lip-application-invalid-cleanup");
    geometry.generatedCleanup=proof.cleanup;
    if(applicationReason)*applicationReason="lip-current-cleanup-applied";
    return geometry;
}

// State contains coordinates only, never pixels or reusable cleanup authority.
// Pixel contradictions retire it immediately. A narrow same-cue window may
// retain coordinates across uninspected recoverable context gaps; any later
// cleanup still requires fresh cue-side, body, flank and closure proofs.
// Mode/source/layout resets also prevent transfer to another cue or epoch.
class SubtitleBackingLipContinuity
{
    CurrentSubtitleBackingLipProof m_outline;
    SubtitleBoxResult m_text;
public:
    void Reset() { m_outline={};m_text={}; }
    CurrentSubtitleBackingLipProof Consume(const AnalysisLumaSource& source,
        const SubtitleBoxPreview& preview,const SubtitleBoxResult& text,
        const ActivePictureFrameIdentity& identity,uint64_t policy,uint64_t continuity,
        size_t maxSamples=65536,const SubtitleCutPasteGeometry* composition=nullptr,
        const SubtitleCueContinuation* glyphContinuity=nullptr)
    {
        auto matchingText=text;
        const bool localGlyphContinuation=HasExactCurrentLocalGlyphContinuationForLip(
            glyphContinuity,preview,text,identity,policy,continuity);
        if(localGlyphContinuation)matchingText.held=false;
        auto previous=m_outline.identity;
        previous.acceptedSequence=identity.acceptedSequence;
        previous.sourceFrameNumber=identity.sourceFrameNumber;
        previous.captureTimestamp=identity.captureTimestamp;
        const bool matches=m_outline.valid && SameAnalysisFrameIdentity(previous,identity) &&
            m_outline.policyGeneration==policy && m_outline.continuityGeneration==continuity &&
            identity.acceptedSequence>=m_outline.identity.acceptedSequence &&
            identity.acceptedSequence-m_outline.identity.acceptedSequence<=8 &&
            matchingText.cue==m_text.cue && SameFreshSubtitleGlyphs(m_text,matchingText,0);
        auto proof=ProveCurrentSubtitleBackingLip(source,preview,text,identity,policy,continuity,
            maxSamples,matches?&m_outline:nullptr,composition,glyphContinuity);
        proof.candidateAgeFrames=matches?
            identity.acceptedSequence-m_outline.identity.acceptedSequence:0;
        if(proof.valid) {
            m_outline=proof;m_text=matchingText;
        } else if(matches && !proof.pixelInspectionPerformed && !preview.current.discontinuity &&
            proof.reason && std::strcmp(proof.reason,"lip-current-context-unavailable")==0) {
            const uint32_t recoverableContext=LipContextPreviewUnavailable|LipContextNotAnalyzed|
                LipContextAnalysisRefresh|LipContextCadenceSkipped|LipContextGlyphLocalTop|
                LipContextGlyphLocalBottom|LipContextCueSideTopUnavailable|
                LipContextCueSideBottomUnavailable;
            if((proof.contextMismatchMask&~recoverableContext)==0)
                proof.candidateRetained=true;
            else Reset();
        } else {
            // Pixel contradictions, cue/epoch changes, budget failures and
            // invalid candidate geometry retire the outline immediately.
            Reset();
        }
        // Context-only failures preserve coordinates for a short same-cue
        // window. This returned proof stays invalid, so it grants no cleanup;
        // a later frame must pass the complete current-pixel proof again.
        return proof;
    }
};
