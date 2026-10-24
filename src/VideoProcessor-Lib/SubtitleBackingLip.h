#pragma once
#include <SubtitleCutPaste.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Cleanup-only proof of one nominated physical plane. A nearby/reference AR
// nomination supplies a coordinate, never authority: this function reads the
// exact current boundary and bar itself, without changing raw bar evidence.
struct CurrentSubtitleBackingPlaneProof {
    bool valid=false;
    int plane=0,blackFloor=0;
    bool fromTopBar=false;
    size_t samples=0;
    unsigned boundarySupport=0,boundaryZones=0,maxIntrusion=0;
    double blackFraction=0.0;
    const char* reason="plane-invalid-source";
};

inline CurrentSubtitleBackingPlaneProof ProveCurrentSubtitleBackingPlane(
    const AnalysisLumaSource& source,int plane,bool fromTopBar,size_t maxSamples)
{
    CurrentSubtitleBackingPlaneProof out;out.plane=plane;out.fromTopBar=fromTopBar;
    const auto reject=[&](const char* why){out.reason=why;return out;};
    if(!source.IsValid() || source.width<64 || source.height<48 ||
        source.width>8192 || source.height>4320 || plane<=0 || plane>=source.height)
        return out;
    const int step=(std::max)(1,source.height/540);
    const int depth=fromTopBar?plane:source.height-plane;
    if(depth<2*step+1 || depth>=source.height/3)return reject("plane-invalid-depth");
    constexpr int columns=128;
    std::array<int,columns> xs{};
    for(int i=0;i<columns;++i)xs[i]=(source.width*(4*columns+96*(2*i+1)))/(200*columns);
    const auto row=[&](int d){return fromTopBar?d:source.height-1-d;};
    bool exhausted=false;
    const auto sample=[&](int x,int y,AnalysisLumaSample& value){
        if(out.samples>=maxSamples){exhausted=true;return false;}
        ++out.samples;
        if(!source.Sample(x,y,value)) {
            exhausted=source.readBudget && source.readBudget->exhausted;return false;
        }
        return true;
    };
    const auto failed=[&](){return reject(exhausted?"plane-work-budget":"plane-native-sample-unavailable");};
    const auto neutral=[](const AnalysisLumaSample& value){
        return std::abs(int(value.chromaU)-512)<=32 && std::abs(int(value.chromaV)-512)<=32;
    };
    std::array<int,columns*3> levels{};unsigned count=0,neutralCount=0;
    for(int d:{0,step,2*step})for(int x:xs){
        AnalysisLumaSample value;if(!sample(x,row(d),value))return failed();
        levels[count++]=value.luma;neutralCount+=neutral(value);
    }
    std::nth_element(levels.begin(),levels.begin()+count/2,levels.begin()+count);
    out.blackFloor=levels[count/2];
    if(out.blackFloor>96 || neutralCount*100<count*95)return reject("plane-nonneutral-bar-floor");
    // Require contrast at THIS native row, with distributed scene support.
    // The wider nine-row window corroborates sharpness but cannot move the
    // edge to a neighboring row or turn the top of a caption into a plane.
    for(int i=0;i<columns;++i){
        std::array<int,9> values{};
        for(int k=0;k<9;++k){
            AnalysisLumaSample value;if(!sample(xs[i],row(depth-4+k),value))return failed();
            values[k]=value.luma;
        }
        const int jump=values[4]-values[3];
        const int neighboringRise=(std::max)({0,values[1]-values[0],values[8]-values[7]});
        if(jump>=8 && jump>neighboringRise*3/2+2){
            ++out.boundarySupport;out.boundaryZones|=1u<<(i/32);
        }
    }
    unsigned zones=0;for(unsigned bits=out.boundaryZones;bits;bits>>=1)zones+=bits&1;
    if(out.boundarySupport<8 || zones<2)return reject("plane-no-distributed-exact-boundary");
    size_t total=0,blackCount=0;
    for(int d=0;d<depth;d=(d+step>=depth && d!=depth-1)?depth-1:d+step){
        unsigned intrusion=0;
        for(int x:xs){
            AnalysisLumaSample value;if(!sample(x,row(d),value))return failed();
            const bool black=neutral(value) && std::abs(int(value.luma)-out.blackFloor)<=12;
            ++total;blackCount+=black;intrusion=black?0:intrusion+1;
            out.maxIntrusion=(std::max)(out.maxIntrusion,intrusion);
        }
    }
    out.blackFraction=total?double(blackCount)/total:0.0;
    if(out.blackFraction<0.85 || out.maxIntrusion>=columns/5)return reject("plane-current-bar-content-conflict");
    out.valid=true;out.reason="current-exact-cue-side-plane";return out;
}

// Exact-current cleanup only. Never an acquired card, glyph/capture mask,
// retained display envelope, or aspect-ratio certificate.
struct CurrentSubtitleBackingLipProof
{
    bool valid=false;
    const char* reason="not-evaluated";
    // Bits identify the rejected current-context predicate; plane sampling is
    // included in samples even when that independent proof fails.
    uint32_t contextRejectMask=0;
    bool cuePlaneVerified=false;
    size_t cuePlaneSamples=0;
    const char* cuePlaneReason="not-required";
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
    const SubtitleCueContinuation* completeCue=nullptr)
{
    CurrentSubtitleBackingLipProof result;
    result.identity=expectedIdentity;result.policyGeneration=policy;result.continuityGeneration=continuity;
    result.width=source.width;result.height=source.height;
    const auto reject=[&](const char* why){result.reason=why;return result;};
    const auto& current=preview.current;
    // A complete local inspector is an explicit alternate source of current
    // cue pixels, not an observation with fabricated bar/AR flags. It proves
    // only its own cue side; the native plane below is always checked again.
    const bool completeLocal=completeCue && completeCue->MatchesSource(expectedIdentity,policy,continuity,accepted) &&
        completeCue->currentInk && completeCue->currentInk->rawEvidenceComplete;
    const int sourcePictureTop=completeLocal?completeCue->pictureTop:current.pictureTop;
    const int sourcePictureBottom=completeLocal?completeCue->pictureBottom:current.pictureBottom;
    const auto& currentText=completeLocal?completeCue->accepted:current.text;
    const auto& currentInk=completeLocal?completeCue->currentInk:current.ink;
    result.pictureTop=sourcePictureTop;result.pictureBottom=sourcePictureBottom;
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
    const bool independentPhysical=!completeLocal && current.independentBarAnalysis &&
        !current.barTrackingAuthority && !current.currentAnchorUsesTrackedEdge &&
        !current.barEvidence.captionBackedTop && !current.barEvidence.captionBackedBottom &&
        !current.barEvidence.topRevalidatedAtReference && !current.barEvidence.topRevalidatedNearby &&
        !current.barEvidence.bottomRevalidatedAtReference && !current.barEvidence.bottomRevalidatedNearby;
    // A caption-backed plane can fall two rows beyond the independently
    // proved picture edge. It cannot acquire a card. For the same recent cue,
    // nominate only the old finite outline, inside BOTH current pictures.
    // Every old body pixel and the surrounding boundary are checked below.
    const bool boundedOutline=candidateContext && candidate->bodyRowCount>=2 &&
        candidate->bodyRowCount<=128 && candidate->measured.Valid() &&
        candidate->measured.top>=sourcePictureTop && candidate->measured.bottom<=sourcePictureBottom &&
        (candidate->fromTopBar?
            std::abs(int64_t(sourcePictureTop)-candidate->pictureTop)<=3:
            std::abs(int64_t(sourcePictureBottom)-candidate->pictureBottom)<=3);
    const bool captionEdgeOutward=boundedOutline && (candidate->fromTopBar?
        current.barEvidence.captionBackedTop && sourcePictureTop<candidate->pictureTop:
        current.barEvidence.captionBackedBottom && sourcePictureBottom>candidate->pictureBottom);
    // Only the cue-side plane bounds this finite cleanup. A changing or
    // revalidated opposite bar must not retire an unchanged current card.
    const bool unchangedCuePlane=boundedOutline && !current.barTrackingAuthority &&
        !current.currentAnchorUsesTrackedEdge && (candidate->fromTopBar?
            sourcePictureTop==candidate->pictureTop &&
                !current.barEvidence.captionBackedTop && !current.barEvidence.topRevalidatedAtReference &&
                !current.barEvidence.topRevalidatedNearby:
            sourcePictureBottom==candidate->pictureBottom &&
                !current.barEvidence.captionBackedBottom && !current.barEvidence.bottomRevalidatedAtReference &&
                !current.barEvidence.bottomRevalidatedNearby);
    bool anchorOutlineBase=boundedOutline &&
        (independentPhysical || captionEdgeOutward || unchangedCuePlane);
    result.contextRejectMask=(!expectedIdentity.acceptedSequence?1u<<0:0) |
        (!completeLocal && !preview.available?1u<<1:0) | (!completeLocal && !current.analyzed?1u<<2:0) |
        (current.discontinuity?1u<<3:0) | (!completeLocal && current.analysisRefresh?1u<<4:0) |
        (!completeLocal && current.cadenceSkipped?1u<<5:0) | (!completeLocal && (current.glyphLocalTop || current.glyphLocalBottom)?1u<<6:0) |
        (!completeLocal && !current.barAuthority?1u<<7:0) | (!completeLocal && !current.barEvidence.available?1u<<8:0) |
        (!SameAnalysisFrameIdentity(current.identity,expectedIdentity)?1u<<9:0) |
        ((preview.policyGeneration!=policy || current.policyGeneration!=policy)?1u<<10:0) |
        ((preview.continuityGeneration!=continuity || current.continuityGeneration!=continuity)?1u<<11:0);
    if(result.contextRejectMask)return reject("lip-current-context-unavailable");
    if(!source.IsValid() || source.width<64 || source.height<48 || source.width>8192 || source.height>4320 ||
        source.generation!=expectedIdentity.transportGeneration || current.width!=source.width || current.height!=source.height ||
        sourcePictureTop<0 || sourcePictureBottom>source.height || sourcePictureTop>=sourcePictureBottom ||
        (!completeLocal && (current.barEvidence.top!=sourcePictureTop || current.barEvidence.bottom!=sourcePictureBottom)) ||
        (completeLocal && (completeCue->physicalBase.rasterWidth!=source.width || completeCue->physicalBase.rasterHeight!=source.height)))
        return reject("lip-source-or-physical-base-mismatch");
    if(!accepted.cue || !accepted.detected || accepted.held || accepted.workLimit || accepted.revised || accepted.observations<1 ||
        !currentText.detected || currentText.held || currentText.workLimit ||
        !currentText.nearBarEligibilityMeasured || !currentText.nearBarEligible ||
        !accepted.nearBarEligibilityMeasured || !accepted.nearBarEligible ||
        !SameFreshSubtitleGlyphs(accepted,currentText,0) ||
        accepted.lineCount<1 || accepted.lineCount>3 || !currentInk ||
        !currentInk->rawEvidenceComplete || !currentInk->paletteValid)
        return reject("lip-current-owned-cue-unavailable");
    const auto& ink=*currentInk;
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
    bool allTop=sourcePictureTop>0,allBottom=sourcePictureBottom<source.height;
    SubtitleBoxRect glyphs{};
    const auto captureLines=SubtitleGlyphCaptureLines(accepted,source.width,source.height);
    for(int i=0;i<accepted.lineCount;++i){
        const auto& row=accepted.lineBounds[i];
        if(!inRaster(row))return reject("lip-invalid-glyph-bounds");
        allTop &= row.bottom<=sourcePictureTop;allBottom &= row.top>=sourcePictureBottom;
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
                        return reject(source.readBudget && source.readBudget->exhausted?
                            "lip-work-budget":"lip-pending-native-glyph-unavailable");
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
    if(completeLocal && completeCue->fromTopBar!=allTop)return reject("lip-complete-cue-side-mismatch");
    CurrentSubtitleBackingPlaneProof currentPlane;
    if(completeLocal || (!independentPhysical && !captionEdgeOutward && !unchangedCuePlane)) {
        // A complete current cue may arrive with a nearby/reference plane,
        // including on cold start. Verify its own physical side directly;
        // the opposite edge and the origin of the nomination grant nothing.
        const bool captionPlane=allTop?current.barEvidence.captionBackedTop:current.barEvidence.captionBackedBottom;
        if(!completeLocal && (current.barTrackingAuthority || current.currentAnchorUsesTrackedEdge || captionPlane)) {
            result.contextRejectMask|=1u<<12;
            result.cuePlaneReason="plane-tracked-or-caption-derived";
            return reject("lip-current-context-unavailable");
        }
        currentPlane=ProveCurrentSubtitleBackingPlane(source,allTop?sourcePictureTop:sourcePictureBottom,
            allTop,maxSamples-result.samples);
        result.cuePlaneSamples=currentPlane.samples;result.samples+=currentPlane.samples;
        result.cuePlaneVerified=currentPlane.valid;result.cuePlaneReason=currentPlane.reason;
        if(!currentPlane.valid) {
            result.contextRejectMask|=1u<<13;
            return reject(std::strcmp(currentPlane.reason,"plane-work-budget")==0?"lip-work-budget":"lip-current-plane-unavailable");
        }
        anchorOutlineBase=boundedOutline;
    }
    if(anchorOutlineBase) {
        result.pictureTop=(std::max)(result.pictureTop,candidate->pictureTop);
        result.pictureBottom=(std::min)(result.pictureBottom,candidate->pictureBottom);
        result.outlineBaseAnchored=result.pictureTop!=sourcePictureTop ||
            result.pictureBottom!=sourcePictureBottom || !independentPhysical;
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
        result.compositionEdgeClipped=allTop?result.pictureTop!=sourcePictureTop:
            result.pictureBottom!=sourcePictureBottom;
    }
    const int glyphHeight=SubtitleTypicalLineHeight(accepted);
    const int gap=allTop?result.pictureTop-glyphs.bottom:glyphs.top-result.pictureBottom;
    if(glyphHeight<4 || glyphHeight>source.height/4 || gap<0 || gap>glyphHeight/2)
        return reject("lip-glyphs-too-far-from-edge");
    const int floor=result.cuePlaneVerified?currentPlane.blackFloor:
        (allTop?current.barEvidence.topFloor:current.barEvidence.bottomFloor);
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
        if(!source.Sample(x,y,value)) {
            exhausted=source.readBudget && source.readBudget->exhausted;
            failedRead=!exhausted;return false;
        }
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
    if(candidateContext && candidate->fromTopBar==allTop && candidate->bodyRowCount>=2 && candidate->bodyRowCount<=128 &&
        (allTop?candidate->pictureTop==result.pictureTop:candidate->pictureBottom==result.pictureBottom) &&
        candidate->measured.top>=result.pictureTop && candidate->measured.bottom<=result.pictureBottom) {
        // Once a finite card has been measured, scene texture beside it must
        // not repeatedly grow/shrink its outline. Require the SAME current
        // glyphs (caller), exact current ownership (above), and exhaustive black
        // body evidence. This is not a timed hold and cannot authorize AR/capture.
        unsigned leftBright=0,rightBright=0;
        int currentInteriorLeft=0,currentInteriorRight=source.width;
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
            currentInteriorLeft=(std::max)(currentInteriorLeft,r.left+collar);
            currentInteriorRight=(std::min)(currentInteriorRight,r.right-collar);
            for(int x=r.left;x<r.right;++x) {
                AnalysisLumaSample value;
                if(!sample(x,r.top,value))return reject(exhausted?"lip-work-budget":"lip-native-sample-unavailable");
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
        result.measured=candidate->measured;result.cleanup=candidate->cleanup;
        // Recompute from the original row outline each time; do not repeatedly
        // shrink an already revalidated intersection, or certify the AA collar.
        result.blackInterior={currentInteriorLeft,candidate->measured.top,currentInteriorRight,candidate->measured.bottom};
        if(!result.blackInterior.Valid())return reject("lip-outline-no-black-interior");
        result.cornerPixels=candidate->cornerPixels;
        result.bodyRows=candidate->bodyRows;result.bodyRowCount=candidate->bodyRowCount;
        result.roundingRows=candidate->roundingRows;result.revalidatedOutline=true;
        result.valid=true;result.reason="current-revalidated-backing-outline";return result;
    }
    // Reuse chroma from each exhaustive native luma probe; no second sampling
    // pass, image conversion, allocation, or persistent image storage.
    std::array<AnalysisLumaSample,8192> rowPixels;
    struct Row {int left=0,right=0,y=0;};
    Row first,last;
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
                if(value.luma>darkLimit || (x>=left+6 && x<right-6 &&
                    (std::abs(int(value.chromaU)-512)>32 || std::abs(int(value.chromaV)-512)>32)))
                    return reject("lip-attachment-seam-body-mismatch");
            }
            if(exhausted || failedRead)break;
            first.left=last.left=left;first.right=last.right=right;
            commonLeft=minLeft=left;commonRight=maxRight=right;
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
        // Decoded 4:2:2 chroma may bleed across a few black edge pixels. Luma
        // remains exhaustive there; the interior must be neutral throughout.
        for(int x=left+6;x<right-6;++x)
            if(std::abs(int(rowPixels[x].chromaU)-512)>32 || std::abs(int(rowPixels[x].chromaV)-512)>32)
                return reject("lip-colored-interior");
        if(rows && (left>first.left || right<first.right))++result.roundingRows;
        result.bodyRows[rows]={left,y,right,y+1};
        result.bodyRowCount=unsigned(rows+1);
        last={left,right,y};++rows;
        commonLeft=(std::max)(commonLeft,left);commonRight=(std::min)(commonRight,right);
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
        proof.cleanup.left!=proof.measured.left || proof.cleanup.top!=proof.measured.top ||
        proof.cleanup.right!=proof.measured.right || proof.cleanup.bottom!=proof.measured.bottom ||
        (proof.fromTopBar?proof.measured.top!=proof.pictureTop:proof.measured.bottom!=proof.pictureBottom))
        return reject("lip-application-invalid-cleanup");
    geometry.generatedCleanup=proof.cleanup;
    if(applicationReason)*applicationReason="lip-current-cleanup-applied";
    return geometry;
}

// State contains coordinates only, never pixels or a reusable cleanup authority.
// A failed current validation immediately retires it. Mode/source/layout resets
// retire it too; it cannot transfer to changed text, a new cue, or an epoch.
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
        const SubtitleCueContinuation* completeCue=nullptr)
    {
        auto previous=m_outline.identity;
        previous.acceptedSequence=identity.acceptedSequence;
        previous.sourceFrameNumber=identity.sourceFrameNumber;
        previous.captureTimestamp=identity.captureTimestamp;
        const bool matches=m_outline.valid && SameAnalysisFrameIdentity(previous,identity) &&
            m_outline.policyGeneration==policy && m_outline.continuityGeneration==continuity &&
            identity.acceptedSequence>=m_outline.identity.acceptedSequence &&
            identity.acceptedSequence-m_outline.identity.acceptedSequence<=8 &&
            text.cue==m_text.cue && SameFreshSubtitleGlyphs(m_text,text,0);
        auto proof=ProveCurrentSubtitleBackingLip(source,preview,text,identity,policy,continuity,
            maxSamples,matches?&m_outline:nullptr,composition,completeCue);
        if(proof.valid) {m_outline=proof;m_text=text;} else Reset();
        return proof;
    }
};
