#pragma once
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <SubtitleBarEvidence.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>

// A strongly acquired bar plane may be carried forward only when every new
// frame freshly revalidates that exact plane from its own pixels. The identity
// advances on a successful weak-edge frame while the geometry stays fixed.
struct SubtitleBarTrackingReference
{
    ActivePictureFrameIdentity identity;
    int width = 0, height = 0, pictureTop = 0, pictureBottom = 0;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    bool valid = false;

    bool IsImmediatePredecessor(const ActivePictureFrameIdentity& next,
        int nextWidth, int nextHeight, uint64_t nextPolicyGeneration = 0,
        uint64_t nextContinuityGeneration = 0) const
    {
        return valid && width == nextWidth && height == nextHeight &&
            policyGeneration == nextPolicyGeneration &&
            continuityGeneration == nextContinuityGeneration &&
            identity.transportGeneration == next.transportGeneration &&
            identity.sourceFormatGeneration == next.sourceFormatGeneration &&
            identity.viewportGeneration == next.viewportGeneration &&
            identity.rendererGeneration == next.rendererGeneration &&
            identity.acceptedSequence + 1 == next.acceptedSequence;
    }
};

// Snapshot of the SAME bar authority consumed by Classic. Queued scans may
// speculate with a snapshot, but presentation must match the current decision.
struct SubtitlePictureAuthority {
    bool required=false;
    ActivePictureBounds bounds;
    ActivePictureFrameIdentity identity;
    bool allowAssistedNomination=false; // Explicit display-policy permission, never implied by withdrawal.
    bool Matches(const SubtitlePictureAuthority& other) const {
        return required==other.required && allowAssistedNomination==other.allowAssistedNomination && (!required ||
            (bounds.left==other.bounds.left && bounds.top==other.bounds.top &&
             bounds.right==other.bounds.right && bounds.bottom==other.bounds.bottom &&
             bounds.rasterWidth==other.bounds.rasterWidth && bounds.rasterHeight==other.bounds.rasterHeight &&
             identity.transportGeneration==other.identity.transportGeneration &&
             identity.sourceFormatGeneration==other.identity.sourceFormatGeneration &&
             identity.viewportGeneration==other.identity.viewportGeneration &&
             identity.rendererGeneration==other.identity.rendererGeneration));
    }
    bool AllowsAssistedNominationFor(const ActivePictureFrameIdentity& frame,int width,int height) const {
        auto current=*this;current.identity=frame;
        return required && allowAssistedNomination && Matches(current) && !AvailableFor(frame,width,height);
    }
    bool AvailableFor(const ActivePictureFrameIdentity& frame,int width,int height) const {
        auto next=*this;next.identity=frame;
        return required && Matches(next) && bounds.rasterWidth==width && bounds.rasterHeight==height &&
            bounds.left>=0 && bounds.right<=width && bounds.left<bounds.right &&
            bounds.top>=0 && bounds.bottom<=height && bounds.top<bounds.bottom &&
            (bounds.top>0 || bounds.bottom<height);
    }
};

// A measurement belongs to source pixels, never retained presentation geometry.
struct SubtitleBoxObservation
{
    SubtitlePictureAuthority sharedPicture;
    // Independent detector ROI only; never native bar or crop authority.
    bool assistedSourceCandidate = false;
    ActivePictureBounds assistedBounds;
    int nearBarDistance=0;
    int optimizationMode=0;
    ActivePictureFrameIdentity identity;
    SubtitleBoxResult text;
    int width = 0, height = 0, pictureTop = 0, pictureBottom = 0;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    bool analysisRefresh = false; // Cheap worker fingerprint continuation, not a full scan.
    bool pendingRefresh = false; // Current native pixels revalidated while worker is pending.
    bool analyzed = false, barAuthority = false, barTrackingAuthority = false;
    bool currentAnchorUsesTrackedEdge = false, discontinuity = false;
    bool trackedBottomHypothesisAttempted = false, trackedBottomHypothesisDetected = false;
    bool trackedBottomHypothesisCrosses = false, trackedBottomHypothesisProved = false;
    SubtitleBoxRect trackedBottomHypothesisAnchor;
    int trackedBottomHypothesisSupport = 0, trackedBottomHypothesisMaxIntrusion = 0;
    double trackedBottomHypothesisBlackFraction = 0.0;
    bool hasBarTrackingReference = false;
    ActivePictureFrameIdentity barTrackingReferenceIdentity;
    int barTrackingReferenceTop = 0, barTrackingReferenceBottom = 0;
    // Preserve the reference supplied to every attempt, including misses. A
    // cached negative result is stale as soon as a new predecessor becomes
    // available and may no longer be reused for that incoming frame.
    SubtitleBarTrackingReference incomingBarTrackingReference;
    double analysisMs = 0.0;
    SubtitleBarEvidence barEvidence;
    std::shared_ptr<const SubtitleInkSnapshot> ink;
};

struct SubtitleBoxPreview
{
    SubtitleBoxObservation current;
    SubtitleBoxResult text;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    unsigned matchingFrames = 0;
    bool available = false, futureAvailable = false, currentLinesConfirmed = false;
    double newScanMs = 0.0;
    unsigned newScanFrames = 0;
    // Preserve the full queued lookahead for fixed-pixel row confirmation.
    // Dropout bridging below remains explicitly capped at two frames.
    std::array<SubtitleBoxObservation,7> following;
    unsigned followingCount = 0;
};

namespace SubtitleBoxLookahead
{
    constexpr size_t MaxFrames = 8;

    inline bool SamePanelTopEdge(const SubtitlePanelTopEdge& a,
        const SubtitlePanelTopEdge& b, int tolerance)
    {
        if (!a.Valid() || !b.Valid() || std::abs(a.y-b.y)>tolerance) return false;
        const int overlap=(std::min)(a.right,b.right)-(std::max)(a.left,b.left);
        return overlap>0 && overlap*2>=(std::min)(a.right-a.left,b.right-b.left);
    }

    inline bool HasBarEvidence(const SubtitleBoxObservation& observation)
    {
        return observation.barAuthority || observation.barTrackingAuthority;
    }

    inline SubtitleBarTrackingReference AdvanceBarTrackingReference(
        const SubtitleBoxObservation& observation)
    {
        SubtitleBarTrackingReference result;
        if (!observation.analyzed || observation.discontinuity || observation.assistedSourceCandidate ||
            !HasBarEvidence(observation)) return result;
        result.identity = observation.identity;
        result.width = observation.width; result.height = observation.height;
        result.pictureTop = observation.barEvidence.captionBackedTop?0:observation.pictureTop;
        result.pictureBottom = observation.barEvidence.captionBackedBottom?observation.height:observation.pictureBottom;
        result.policyGeneration = observation.policyGeneration;
        result.continuityGeneration = observation.continuityGeneration;
        result.valid = result.pictureTop > 0 || result.pictureBottom < result.height;
        return result;
    }

    inline SubtitleBoxObservation Measure(SubtitleBoxDetector& scanner,
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureFrameIdentity& identity, bool discontinuity,
        const SubtitleBarTrackingReference& priorBar = {},
        uint64_t policyGeneration = 0, uint64_t continuityGeneration = 0,
        const SubtitlePictureAuthority& sharedPicture = {})
    {
        SubtitleBoxObservation result;
        const auto start = std::chrono::steady_clock::now();
        result.identity = identity; result.analyzed = true;
        result.nearBarDistance=scanner.NearBarDistance();
        result.optimizationMode=scanner.OptimizationMode();
        result.width = source.width; result.height = source.height;
        result.policyGeneration = policyGeneration;
        result.continuityGeneration = continuityGeneration;
        result.discontinuity = discontinuity;
        result.incomingBarTrackingReference = priorBar;
        result.sharedPicture=sharedPicture;
        if(sharedPicture.required) {
            result.barEvidence.reason="shared-classic-authority-unavailable";
            // No independent edge scan, reference retention, or opposite-bar
            // hypothesis may overturn Classic's current/retained authority.
            if(sharedPicture.AvailableFor(identity,source.width,source.height) && source.IsValid()) {
                result.pictureTop=sharedPicture.bounds.top;result.pictureBottom=sharedPicture.bounds.bottom;
                result.barAuthority=true;result.barEvidence.available=true;
                result.barEvidence.top=result.pictureTop;result.barEvidence.bottom=result.pictureBottom;
                result.barEvidence.reason="shared-classic-picture-authority";
                scanner.Reset();
                result.text=scanner.Analyze(source,result.pictureTop,result.pictureBottom,
                    identity.acceptedSequence,identity.viewportGeneration);
                result.ink=scanner.InkSnapshot();
            }
            result.analysisMs=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count();
            return result;
        }

        // Raw crop evidence is retained by the caller for its own purpose. Its
        // connected-picture veto cannot decide whether edge-crossing text exists.
        (void)raw;
        result.barEvidence = ExtractSubtitleBarEvidence(source);
        const auto& bounds = result.barEvidence;
        result.barAuthority = bounds.available;
        result.pictureTop = bounds.top; result.pictureBottom = bounds.bottom;
        if (!discontinuity && priorBar.IsImmediatePredecessor(identity,source.width,source.height,
            policyGeneration,continuityGeneration))
        {
            result.hasBarTrackingReference = true;
            result.barTrackingReferenceIdentity = priorBar.identity;
            result.barTrackingReferenceTop = priorBar.pictureTop;
            result.barTrackingReferenceBottom = priorBar.pictureBottom;
            result.barEvidence = RevalidateSubtitleBarEvidence(source,bounds,
                priorBar.pictureTop,priorBar.pictureBottom);
            result.barTrackingAuthority = result.barEvidence.topRevalidatedAtReference ||
                result.barEvidence.bottomRevalidatedAtReference;
            if (result.barEvidence.topRevalidatedAtReference)
                result.pictureTop = priorBar.pictureTop;
            if (result.barEvidence.bottomRevalidatedAtReference)
                result.pictureBottom = priorBar.pictureBottom;
        }
        // If a current caption itself crosses a previously proved lower bar
        // plane, its bright strokes can trip the generic long-intrusion veto.
        // Scan provisionally at that old plane, then let only detector-owned
        // glyph pixels explain the intrusion. The independent masked check
        // still requires fresh distributed edge evidence and a mostly black
        // unmasked bar. This path never changes native active-picture bounds.
        bool provisionalMeasurement = false;
        if (!discontinuity && priorBar.IsImmediatePredecessor(identity,source.width,source.height,
                policyGeneration,continuityGeneration) && priorBar.pictureBottom<source.height &&
            !result.barEvidence.bottomRevalidatedAtReference &&
            bounds.bottom==source.height &&
            !(bounds.bottom<source.height &&
                std::abs(bounds.bottom-priorBar.pictureBottom)>(std::max)(2,source.height/540)))
        {
            result.trackedBottomHypothesisAttempted=true;
            scanner.Reset();
            const auto provisional=scanner.Analyze(source,result.pictureTop,priorBar.pictureBottom,
                identity.acceptedSequence,identity.viewportGeneration);
            const auto provisionalInk=scanner.InkSnapshot();
            result.trackedBottomHypothesisDetected=provisional.detected;
            result.trackedBottomHypothesisAnchor=provisional.anchor;
            result.trackedBottomHypothesisCrosses=provisional.anchor.Valid() &&
                provisional.anchor.top<priorBar.pictureBottom &&
                provisional.anchor.bottom>=priorBar.pictureBottom;
            if (result.trackedBottomHypothesisDetected && result.trackedBottomHypothesisCrosses && provisionalInk)
            {
                auto masked=RevalidateSubtitleBarEvidence(source,bounds,priorBar.pictureTop,
                    priorBar.pictureBottom,provisionalInk.get(),&provisional.anchor);
                result.trackedBottomHypothesisSupport=masked.bottomReferenceBoundarySupport;
                result.trackedBottomHypothesisBlackFraction=masked.bottomReferenceBlackFraction;
                result.trackedBottomHypothesisMaxIntrusion=masked.bottomReferenceMaxIntrusion;
                result.trackedBottomHypothesisProved=masked.bottomRevalidatedAtReference;
                if (masked.bottomRevalidatedAtReference)
                {
                    result.barEvidence=masked;
                    result.barTrackingAuthority=true;
                    result.pictureBottom=priorBar.pictureBottom;
                    result.text=provisional;
                    result.ink=provisionalInk;
                    provisionalMeasurement=true;
                }
            }
        }
        // A hidden boundary must not prevent recognition of a plainly black-
        // backed caption. Require the opposite encoded bar, then provisionally
        // scan only the outer third. Accept only a current black strip whose
        // nonblack samples are explained by this detector's owned glyphs.
        const bool missingBottom=bounds.top>0 && result.pictureBottom==source.height;
        const bool missingTop=bounds.bottom<source.height && result.pictureTop==0;
        if(!provisionalMeasurement && (missingBottom || missingTop)) {
            scanner.Reset();
            const int probeTop=missingTop?source.height/3:result.pictureTop;
            const int probeBottom=missingBottom?source.height*2/3:result.pictureBottom;
            const auto candidate=scanner.Analyze(source,probeTop,probeBottom,
                identity.acceptedSequence,identity.viewportGeneration);
            const auto candidateInk=scanner.InkSnapshot();
            int plane=0;
            if(ProveSubtitleCaptionStrip(source,candidate,candidateInk.get(),missingBottom,
                missingBottom?bounds.bottomFloor:bounds.topFloor,plane)) {
                if(missingBottom) {
                    result.pictureBottom=plane;result.barEvidence.bottom=plane;
                    result.barEvidence.captionBackedBottom=true;
                    result.barEvidence.reason="caption-backed-bottom-strip";
                } else {
                    result.pictureTop=plane;result.barEvidence.top=plane;
                    result.barEvidence.captionBackedTop=true;
                    result.barEvidence.reason="caption-backed-top-strip";
                }
                // The proved strip contains every accepted row; reuse the
                // same measurement rather than pay for another full scan.
                result.text=candidate;result.ink=candidateInk;
                provisionalMeasurement=true;
            }
        }
        if (result.barAuthority || result.barTrackingAuthority)
        {
            if (!provisionalMeasurement) {
                scanner.Reset();
                result.text = scanner.Analyze(source, result.pictureTop, result.pictureBottom,
                    identity.acceptedSequence, identity.viewportGeneration);
                result.ink = scanner.InkSnapshot();
            }
            const auto& anchor=result.text.anchor;
            // Any glyph ink in a tracked bar depends on that boundary, even if
            // the whole line sits well inside the bar instead of crossing it.
            const bool anchorInTrackedTopBar=anchor.Valid() && anchor.top<result.pictureTop;
            const bool anchorInTrackedBottomBar=anchor.Valid() && anchor.bottom>result.pictureBottom;
            const bool topStrongAtAnchor=bounds.top>0 && bounds.top==result.pictureTop;
            const bool bottomStrongAtAnchor=bounds.bottom<source.height &&
                bounds.bottom==result.pictureBottom;
            result.currentAnchorUsesTrackedEdge=
                (result.barEvidence.topRevalidatedAtReference && !topStrongAtAnchor &&
                    anchorInTrackedTopBar) ||
                (result.barEvidence.bottomRevalidatedAtReference && !bottomStrongAtAnchor &&
                    anchorInTrackedBottomBar);
        }
        result.analysisMs = std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        return result;
    }

    inline bool CanReuseMeasurement(const SubtitleBoxObservation& observation,
        const ActivePictureFrameIdentity& identity, int width, int height,
        uint64_t policyGeneration, uint64_t continuityGeneration,
        bool discontinuity, const SubtitleBarTrackingReference& currentReference)
    {
        if (!observation.analyzed || !SameActivePictureFrameIdentity(observation.identity,identity) ||
            observation.width != width || observation.height != height ||
            observation.policyGeneration != policyGeneration ||
            observation.continuityGeneration != continuityGeneration ||
            observation.discontinuity != discontinuity) return false;
        // Exact-frame reuse is safe for detector geometry, but cannot promote
        // an assisted measurement into the native bar-tracking reference.
        if (observation.assistedSourceCandidate)
            return !observation.analysisRefresh && !observation.pendingRefresh &&
                !observation.text.held && !observation.text.workLimit &&
                observation.sharedPicture.AllowsAssistedNominationFor(identity,width,height);
        if (observation.text.detected && observation.barAuthority &&
            !observation.currentAnchorUsesTrackedEdge &&
            observation.pictureTop==observation.barEvidence.top &&
            observation.pictureBottom==observation.barEvidence.bottom)
            return true; // This frame supplied its own bar and glyph evidence.
        const auto& measuredReference = observation.incomingBarTrackingReference;
        if (measuredReference.valid != currentReference.valid) return false;
        if (measuredReference.valid &&
            (measuredReference.width != currentReference.width ||
            measuredReference.height != currentReference.height ||
            measuredReference.pictureTop != currentReference.pictureTop ||
            measuredReference.pictureBottom != currentReference.pictureBottom ||
            measuredReference.policyGeneration != currentReference.policyGeneration ||
            measuredReference.continuityGeneration != currentReference.continuityGeneration ||
            !SameActivePictureFrameIdentity(measuredReference.identity,currentReference.identity))) return false;
        if (!observation.barTrackingAuthority) return true;
        return observation.hasBarTrackingReference &&
            currentReference.IsImmediatePredecessor(identity,width,height,
                policyGeneration,continuityGeneration) &&
            SameActivePictureFrameIdentity(observation.barTrackingReferenceIdentity,
                currentReference.identity) &&
            observation.barTrackingReferenceTop == currentReference.pictureTop &&
            observation.barTrackingReferenceBottom == currentReference.pictureBottom;
    }

    inline bool SameMeasurementProvenance(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        return a.assistedSourceCandidate == b.assistedSourceCandidate &&
            (!a.assistedSourceCandidate ||
             (a.assistedBounds.left == b.assistedBounds.left && a.assistedBounds.top == b.assistedBounds.top &&
              a.assistedBounds.right == b.assistedBounds.right && a.assistedBounds.bottom == b.assistedBounds.bottom &&
              a.assistedBounds.rasterWidth == b.assistedBounds.rasterWidth &&
              a.assistedBounds.rasterHeight == b.assistedBounds.rasterHeight));
    }

    inline bool SameContext(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        const auto& x = a.identity; const auto& y = b.identity;
        return SameMeasurementProvenance(a,b) && x.transportGeneration == y.transportGeneration &&
            x.sourceFormatGeneration == y.sourceFormatGeneration &&
            x.viewportGeneration == y.viewportGeneration && x.rendererGeneration == y.rendererGeneration &&
            a.width == b.width && a.height == b.height && a.nearBarDistance==b.nearBarDistance &&
            // A measured encoded edge can wobble by about half a percent as
            // scene pixels change. Current strong or fixed-plane current-pixel
            // proof is required on both frames; a lost/changed edge breaks it.
            (a.pictureTop>0)==(b.pictureTop>0) &&
            (a.pictureBottom<a.height)==(b.pictureBottom<b.height) &&
            std::abs(a.pictureTop-b.pictureTop)<(std::max)(1,a.height/180) &&
            std::abs(a.pictureBottom-b.pictureBottom)<(std::max)(1,a.height/180) &&
            HasBarEvidence(a) && HasBarEvidence(b);
    }

    template<size_t N>
    inline bool SimilarSignature(const std::array<uint64_t, N>& a,
        const std::array<uint64_t, N>& b)
    {
        int difference = 0, total = 0;
        auto bits = [](uint64_t value) { int n = 0; for (; value; value &= value - 1) ++n; return n; };
        for (size_t i = 0; i < a.size(); ++i)
        { difference += bits(a[i] ^ b[i]); total += bits(a[i] | b[i]); }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SimilarSignature(const std::array<uint8_t, 1024>& a,
        const std::array<uint8_t, 1024>& b)
    {
        unsigned difference = 0, total = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            difference += static_cast<unsigned>(std::abs(int(a[i])-int(b[i])));
            total += (std::max)(a[i],b[i]);
        }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SameLine(const SubtitleBoxRect& a, const SubtitleBoxRect& b, int tolerance)
    {
        return a.Valid() && b.Valid() && std::abs(a.left-b.left) <= tolerance &&
            std::abs(a.right-b.right) <= tolerance && std::abs(a.top-b.top) <= tolerance &&
            std::abs(a.bottom-b.bottom) <= tolerance;
    }

    inline bool ContainsCurrentInk(const SubtitleBoxRect& box, const SubtitleBoxResult& text)
    {
        if (!box.Valid()) return false;
        for (int i = 0; i < text.lineCount; ++i)
        {
            const auto& line = text.lineBounds[i];
            if (line.left < box.left || line.top < box.top ||
                line.right > box.right || line.bottom > box.bottom) return false;
        }
        return true;
    }

    inline bool ContainsMatchedInk(const SubtitleBoxRect& box, const SubtitleBoxResult& current,
        const SubtitleBoxResult& accepted, int tolerance)
    {
        for (int i = 0; i < current.lineCount; ++i)
            for (int j = 0; j < accepted.lineCount; ++j)
                if (SameLine(current.lineBounds[i],accepted.lineBounds[j],tolerance) &&
                    SimilarSignature(current.lineSignatures[i],accepted.lineSignatures[j]))
                {
                    const auto& line = current.lineBounds[i];
                    if (line.left < box.left || line.top < box.top ||
                        line.right > box.right || line.bottom > box.bottom) return false;
                }
        return true;
    }

    inline bool SameCue(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        if (!SameContext(a,b) || !a.text.detected || !b.text.detected) return false;
        const int tolerance = (std::max)(4, a.height / 180);
        // Every common-height line must agree. An unchanged bottom line cannot
        // lend confirmation to a changed upper line. Missing lines may be added.
        int matches = 0;
        for (int i = 0; i < a.text.lineCount; ++i)
            for (int j = 0; j < b.text.lineCount; ++j)
            {
                const auto& x = a.text.lineBounds[i]; const auto& y = b.text.lineBounds[j];
                if (std::abs(x.top-y.top) > tolerance || std::abs(x.bottom-y.bottom) > tolerance) continue;
                if (!SameLine(x,y,tolerance) ||
                    !SimilarSignature(a.text.lineSignatures[i], b.text.lineSignatures[j])) return false;
                ++matches;
            }
        return matches > 0 && matches == (std::min)(a.text.lineCount,b.text.lineCount);
    }

    inline int BarAnchorLine(const SubtitleBoxObservation& observation)
    {
        int best=-1,bestIntrusion=0;
        for(int i=0;i<observation.text.lineCount;++i) {
            const auto& line=observation.text.lineBounds[i];
            const int intrusion=(std::max)(0,observation.pictureTop-line.top)+
                (std::max)(0,line.bottom-observation.pictureBottom);
            if(intrusion>bestIntrusion) {best=i;bestIntrusion=intrusion;}
        }
        if(best>=0)return best;
        // A verified opaque card close to the bar is an equally valid anchor.
        // Proximity belongs to the detector's unpadded backing measurement;
        // cleanup fringes and display padding cannot create this authority.
        if(observation.nearBarDistance<=0 || !observation.text.nearBarEligibilityMeasured ||
            !observation.text.nearBarEligible)return -1;
        int nearest=(std::numeric_limits<int>::max)();
        for(int i=0;i<observation.text.lineCount;++i) {
            const auto& line=observation.text.lineBounds[i];if(!line.Valid())continue;
            int gap=(std::numeric_limits<int>::max)();
            if(observation.pictureTop>0)gap=(std::min)(gap,line.top-observation.pictureTop);
            if(observation.pictureBottom<observation.height)
                gap=(std::min)(gap,observation.pictureBottom-line.bottom);
            if(gap<nearest) {best=i;nearest=gap;}
        }
        return best;
    }

    // Confirm one subtitle row independently at fixed source coordinates.
    // This lets a stable bar-anchored caption survive a separately grouped
    // scene row, while rejecting rows whose bright pixels drift between frames.
    inline bool SameLinePixels(const SubtitleBoxObservation& a, int aLine,
        const SubtitleBoxObservation& b, int bLine)
    {
        if (!SameContext(a,b) || !a.ink || !b.ink || aLine<0 || bLine<0 ||
            aLine>=a.text.lineCount || bLine>=b.text.lineCount) return false;
        const auto& old=*a.ink; const auto& now=*b.ink;
        if (old.width!=now.width || old.height!=now.height || old.step<=0 ||
            old.step!=now.step || old.sourceRows!=now.sourceRows) return false;
        const auto& ar=a.text.lineBounds[aLine]; const auto& br=b.text.lineBounds[bLine];
        const int tolerance=(std::max)(4,a.height/180);
        if (!ar.Valid() || !br.Valid() || std::abs(ar.top-br.top)>tolerance ||
            std::abs(ar.bottom-br.bottom)>tolerance) return false;
        const int left=(std::max)(0,(std::min)(ar.left,br.left)/old.step);
        const int right=(std::min)(old.width,((std::max)(ar.right,br.right)+old.step-1)/old.step);
        const int top=(std::max)(0,(std::min)(ar.top,br.top)/old.step);
        const int bottom=(std::min)(old.height,((std::max)(ar.bottom,br.bottom)+old.step-1)/old.step);
        unsigned ownedInk=0,covered=0;
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            if(!old.Get(x,y,true)) continue;
            ++ownedInk;
            covered+=now.Get(x,y);
        }
        return ownedInk>=4 && covered*100>=ownedInk*90;
    }

    inline bool SameCrowdedLine(const SubtitleBoxRect& a,const SubtitleBoxRect& b,int yTolerance)
    {
        return a.Valid() && b.Valid() && std::abs(a.left-b.left)<=2 &&
            std::abs(a.right-b.right)<=2 && std::abs(a.top-b.top)<=yTolerance &&
            std::abs(a.bottom-b.bottom)<=yTolerance;
    }

    inline double OwnedLinePixelCoverage(const SubtitleBoxObservation& reference,int line,
        const SubtitleBoxObservation& current)
    {
        if(!SameContext(reference,current) || !reference.ink || !current.ink || line<0 ||
            line>=reference.text.lineCount) return 0.0;
        const auto& old=*reference.ink;const auto& now=*current.ink;
        if(old.width!=now.width || old.height!=now.height || old.step<=0 || old.step!=now.step ||
            old.sourceRows!=now.sourceRows) return 0.0;
        const auto& bounds=reference.text.lineBounds[line];
        const int left=(std::max)(0,bounds.left/old.step);
        const int right=(std::min)(old.width,(bounds.right+old.step-1)/old.step);
        const int top=(std::max)(0,bounds.top/old.step);
        const int bottom=(std::min)(old.height,(bounds.bottom+old.step-1)/old.step);
        unsigned owned=0,covered=0;
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x)
            if(old.Get(x,y,true)) {++owned;covered+=now.Get(x,y);}
        return owned>=4?double(covered)/owned:0.0;
    }

    inline bool OwnedLinePixelsVisible(const SubtitleBoxObservation& reference,int line,
        const SubtitleBoxObservation& current)
    {
        return OwnedLinePixelCoverage(reference,line,current)>=0.90;
    }

    // Find new glyph-like components just outside a cached line. Coordinates
    // are converted from source pixels through the snapshot's sampled rows;
    // previously present scene details are ignored.
    inline bool FindNewInkExtension(const SubtitleInkSnapshot& reference,
        const SubtitleInkSnapshot& current, const SubtitleBoxRect& bounds,
        int sourceWidth, int sourceHeight, SubtitleBoxRect& extension)
    {
        extension={};
        if (!bounds.Valid()) return true;
        if (reference.width!=current.width || reference.height!=current.height ||
            reference.step<=0 || reference.step!=current.step ||
            reference.sourceRows.size()!=size_t(reference.height) ||
            current.sourceRows.size()!=size_t(current.height)) return true;
        const int leftLine=(std::max)(0,bounds.left/current.step);
        const int rightLine=(std::min)(current.width,(bounds.right+current.step-1)/current.step);
        const auto first=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),bounds.top);
        const auto last=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),bounds.bottom);
        const int topLine=int(first-current.sourceRows.begin());
        const int bottomLine=int(last-current.sourceRows.begin());
        const int lineHeight=bottomLine-topLine;
        if (leftLine>=rightLine || topLine<0 || bottomLine>current.height || lineHeight<=0) return true;
        // Side growth is searched only outside the learned line; a newly
        // appearing row may be wider or RTL edge-aligned, so scan the complete
        // bounded frame width in its vertical band.
        const int left=0;
        const int right=current.width;
        const int top=(std::max)(0,topLine-2*lineHeight-1);
        const int bottom=(std::min)(current.height,bottomLine+2*lineHeight+1);
        if (left>=right || top>=bottom) return true;
        const int roiWidth=right-left,roiHeight=bottom-top;
        // Connected words in Arabic can span several nominal glyph heights.
        // Keep the candidate bounded by both the cue line scale and frame width;
        // baseline, density, novelty, and cue checks below still decide whether
        // it belongs to the established subtitle.
        const int maximumComponentWidth=(std::min)(current.width*98/100,
            (std::max)(lineHeight*14,8));
        std::vector<uint8_t> visited(size_t(roiWidth)*roiHeight,0);
        std::vector<int> pending;
        std::vector<SubtitleBoxRect> candidates;
        auto scanSide=[&](int xStart,int xEnd,int yStart,int yEnd) {
        for (int y=yStart;y<yEnd;++y)
            for (int x=xStart;x<xEnd;++x)
            {
                if (!current.Get(x,y)) continue;
                const size_t start=size_t(y-top)*roiWidth+(x-left);
                if (visited[start]) continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y,pixels=0,newPixels=0;
                for (size_t at=0;at<pending.size();++at)
                {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    ++pixels;minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);
                    const int sourceY=current.sourceRows[size_t(cy)];
                    const auto oldRow=std::lower_bound(reference.sourceRows.begin(),
                        reference.sourceRows.end(),sourceY);
                    const bool existed=oldRow!=reference.sourceRows.end() && *oldRow==sourceY &&
                        reference.Get(cx,int(oldRow-reference.sourceRows.begin()));
                    newPixels+=!existed;
                    for (int dy=-1;dy<=1;++dy)for (int dx=-1;dx<=1;++dx)
                    {
                        const int nx=cx+dx,ny=cy+dy;
                        if (nx<xStart || nx>=xEnd || ny<top || ny>=bottom || !current.Get(nx,ny)) continue;
                        const size_t next=size_t(ny-top)*roiWidth+(nx-left);
                        if (!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                    }
                }
                const int componentWidth=maxX-minX+1,componentHeight=maxY-minY+1;
                const int boxArea=componentWidth*componentHeight;
                if (pixels>=4 && componentHeight>=(std::max)(3,lineHeight/2) &&
                    componentWidth>=1 && componentWidth<=maximumComponentWidth &&
                    pixels*100>=boxArea*10 && pixels*100<=boxArea*100 &&
                    newPixels*100>=pixels*70)
                {
                    candidates.push_back({minX,minY,maxX+1,maxY+1});
                }
            }
        };
        scanSide(left,leftLine,top,bottom);
        scanSide(rightLine,right,top,bottom);
        scanSide(left,right,top,topLine);
        scanSide(left,right,bottomLine,bottom);
        // A stable bright scene fragment is not automatically a new word.
        // Side extensions share the acquired baseline and plausible word gap.
        // A new row needs at least two aligned components; the full detector
        // remains responsible for independently acquiring singleton lines.
        for(const auto& seed:candidates) {
            const int seedHeight=seed.bottom-seed.top;
            if(seedHeight*3<lineHeight*2 || seedHeight>lineHeight*2)continue;
            SubtitleBoxRect group=seed;
            int count=0,nearestGap=sourceWidth;
            std::vector<uint8_t> joined(candidates.size(),0);
            bool grew=true;
            while(grew) {
                grew=false;
                for(size_t i=0;i<candidates.size();++i) {
                    const auto& candidate=candidates[i];
                    const int candidateHeight=candidate.bottom-candidate.top;
                    const int groupGap=(std::max)(group.left-candidate.right,candidate.left-group.right);
                    if(joined[i] || groupGap>lineHeight*3 || candidateHeight*3<lineHeight*2 ||
                        candidateHeight>lineHeight*2 ||
                        std::abs(candidate.top+candidate.bottom-seed.top-seed.bottom)>lineHeight*2/3)continue;
                    joined[i]=1;grew=true;++count;
                    group={(std::min)(group.left,candidate.left),(std::min)(group.top,candidate.top),
                        (std::max)(group.right,candidate.right),(std::max)(group.bottom,candidate.bottom)};
                    const int gap=(std::max)(leftLine-candidate.right,candidate.left-rightLine);
                    nearestGap=(std::min)(nearestGap,gap);
                }
            }
            const bool sameBaseline=std::abs(group.top+group.bottom-topLine-bottomLine)<=lineHeight*2/3;
            // Match the detector's 98% raster guard. A large-font line may
            // legitimately occupy more than 90% of the frame; rejecting it
            // here would make an established cue miss that confirmed growth.
            if(group.right-group.left>=current.width*98/100)continue;
            if(sameBaseline) {
                if(nearestGap>lineHeight*3)continue;
            } else {
                const int rowGap=(std::max)(topLine-group.bottom,group.top-bottomLine);
                if(count<2 || group.right-group.left<lineHeight || rowGap>lineHeight*2 ||
                    std::abs(group.left+group.right-leftLine-rightLine)>
                        (std::max)(lineHeight*8,(rightLine-leftLine)*2/3))continue;
            }
            const SubtitleBoxRect sourceBounds{
                (std::max)(0,group.left*current.step),current.sourceRows[size_t(group.top)],
                (std::min)(sourceWidth,group.right*current.step),
                (std::min)(sourceHeight,current.sourceRows[size_t(group.bottom-1)]+current.step)};
            if(!extension.Valid())extension=sourceBounds;
            else extension={(std::min)(extension.left,sourceBounds.left),(std::min)(extension.top,sourceBounds.top),
                (std::max)(extension.right,sourceBounds.right),(std::max)(extension.bottom,sourceBounds.bottom)};
        }
        return extension.Valid();
    }

    // Revalidate the actual acquisition pixels at fixed source coordinates.
    // Normalized signatures alone can hide one changed word in a long line.
    // Owned ink supplies positive glyph evidence. Compare glyph interiors,
    // including holes, without treating changing inter-glyph picture pixels as
    // subtitle identity. Newly appearing glyph-sized components still reject.
    // Small overlapping horizontal windows
    // prevent unrelated unchanged words from diluting a local text change.
    inline bool StableBlackBacking(const SubtitleBoxObservation&,const SubtitleBoxObservation&);
    struct InkMatchDiagnostics {
        unsigned minimumCoverage=100, maximumDifference=0;
        unsigned componentOwned=0,componentCovered=0,tinyFringeAllowances=0;
        int line=-1;
        const char* reason="not-compared";
        bool backingConfirmed=false;
    };
    inline bool SameInkAtReference(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current,unsigned minimumLineCoveragePercent=95, bool backedTracking=false,
        InkMatchDiagnostics* diagnostic=nullptr, bool allowTinyFringeLoss=false)
    {
        if(diagnostic)*diagnostic={};
        auto reject=[&](const char* reason){if(diagnostic)diagnostic->reason=reason;return false;};
        if (!SameContext(reference,current) || !reference.ink || !current.ink ||
            !reference.text.detected || reference.text.workLimit || current.text.workLimit ||
            reference.text.lineCount<1 || reference.text.lineCount>3) return reject("context-or-pixel-proof");
        const auto& old=*reference.ink; const auto& now=*current.ink;
        const size_t words=(size_t(old.width)*old.height+63)/64;
        if (old.width<=0 || old.height<=0 || old.width>960 || old.height>540 || old.step<=0 ||
            old.width!=now.width || old.height!=now.height || old.step!=now.step ||
            old.width!=(reference.width+old.step-1)/old.step ||
            old.height!=(reference.height+old.step-1)/old.step ||
            old.sourceRows.size()!=size_t(old.height) || now.sourceRows.size()!=size_t(now.height) ||
            old.rawInk.size()!=words || old.ownedInk.size()!=words || now.rawInk.size()!=words) return reject("context-or-pixel-proof");
        struct Counts { unsigned owned=0,covered=0,different=0,total=0,added=0; };
        unsigned currentBarInk=0;
        bool tinyFringeUsed=false;
        for (int line=0;line<reference.text.lineCount;++line)
        {
            if(diagnostic)diagnostic->line=line;
            bool tinyFringeOnLine=false;
            const auto& bounds=reference.text.lineBounds[line];
            if (!bounds.Valid()) return reject("context-or-pixel-proof");
            const int left=(std::max)(0,bounds.left/old.step);
            const int right=(std::min)(old.width,(bounds.right+old.step-1)/old.step);
            const int top=(std::max)(0,bounds.top/old.step);
            const int bottom=(std::min)(old.height,(bounds.bottom+old.step-1)/old.step);
            if (left>=right || top>=bottom) return reject("context-or-pixel-proof");
            const int roiWidth=right-left,roiHeight=bottom-top;
            std::vector<uint8_t> glyphEnvelope(size_t(roiWidth)*roiHeight,0);
            std::vector<uint8_t> visited(glyphEnvelope.size(),0);
            std::vector<int> pending;
            // Ownership is fixed at acquisition. Flood each owned component
            // to preserve its interior holes while excluding surrounding scene.
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                const size_t start=size_t(y-top)*roiWidth+x-left;
                if(visited[start] || !old.Get(x,y,true))continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y;
                unsigned owned=0,covered=0;
                bool missingInterior=false;
                for(size_t at=0;at<pending.size();++at) {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);++owned;
                    const auto row=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[cy]);
                    const bool present=row!=now.sourceRows.end() && *row==old.sourceRows[cy] &&
                        now.Get(cx,int(row-now.sourceRows.begin()));
                    covered+=present;
                    if(!present) {
                        bool fringe=false;
                        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                            fringe |= !old.Get(cx+dx,cy+dy,true);
                        missingInterior |= !fringe;
                    }
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int nx=cx+dx,ny=cy+dy;
                        if(nx<left || nx>=right || ny<top || ny>=bottom || !old.Get(nx,ny,true))continue;
                        const size_t next=size_t(ny-top)*roiWidth+nx-left;
                        if(!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                    }
                }
                if(!backedTracking && owned>=4 && covered*100<owned*85) {
                    // At this sampling scale one fringe pixel is 17--20% of a
                    // tiny dot. Retention may tolerate that quantization only
                    // once per cue, with fresh backing and near-exact line proof.
                    if(!allowTinyFringeLoss || tinyFringeUsed || missingInterior ||
                        owned<5 || owned>6 || covered+1!=owned) {
                        if(diagnostic) {diagnostic->componentOwned=owned;diagnostic->componentCovered=covered;}
                        return reject("component-coverage");
                    }
                    if(diagnostic)++diagnostic->tinyFringeAllowances;
                    tinyFringeUsed=tinyFringeOnLine=true;
                }
                for(int yy=minY;yy<=maxY;++yy)for(int xx=minX;xx<=maxX;++xx)
                    glyphEnvelope[size_t(yy-top)*roiWidth+xx-left]=1;
            }
            // A new glyph in previously empty line space cannot be silently
            // ignored as background. Small transient scene fragments are not
            // sufficient; require a component with the scale of this line.
            std::fill(visited.begin(),visited.end(),0);
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                const size_t start=size_t(y-top)*roiWidth+x-left;
                const auto row=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[y]);
                const int nowY=row!=now.sourceRows.end() && *row==old.sourceRows[y] ? int(row-now.sourceRows.begin()) : -1;
                if(visited[start] || glyphEnvelope[start] || old.Get(x,y) || nowY<0 || !now.Get(x,nowY))continue;
                visited[start]=1;pending.clear();pending.push_back(int(start));
                int minX=x,maxX=x,minY=y,maxY=y;
                unsigned detachedPixels=0;
                int detachedTop=bottom,detachedBottom=top;
                for(size_t at=0;at<pending.size();++at) {
                    const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                    bool touchesOldGlyph=false;
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
                        touchesOldGlyph=touchesOldGlyph || old.Get(cx+dx,cy+dy,true);
                    if(!touchesOldGlyph) {
                        ++detachedPixels;
                        detachedTop=(std::min)(detachedTop,cy);
                        detachedBottom=(std::max)(detachedBottom,cy+1);
                    }
                    minX=(std::min)(minX,cx);maxX=(std::max)(maxX,cx);
                    minY=(std::min)(minY,cy);maxY=(std::max)(maxY,cy);
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                        const int nx=cx+dx,ny=cy+dy;
                        if(nx<left || nx>=right || ny<top || ny>=bottom)continue;
                        const size_t next=size_t(ny-top)*roiWidth+nx-left;
                        if(visited[next] || glyphEnvelope[next] || old.Get(nx,ny))continue;
                        const auto rr=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),old.sourceRows[ny]);
                        if(rr==now.sourceRows.end() || *rr!=old.sourceRows[ny] || !now.Get(nx,int(rr-now.sourceRows.begin())))continue;
                        visited[next]=1;pending.push_back(int(next));
                    }
                }
                // Ignore only the one-sample fringe attached to acquired
                // strokes. A tall newly filled interior remains contradictory
                // even when its attached fringe outnumbers detached pixels.
                if(detachedPixels>=4 &&
                    detachedBottom-detachedTop>=(std::max)(3,roiHeight/2))return reject("new-detached-component");
            }
            std::vector<Counts> columns(size_t(right-left));
            Counts all;
            for (int y=top;y<bottom;++y)
            {
                const int sourceY=old.sourceRows[y];
                const auto found=std::lower_bound(now.sourceRows.begin(),now.sourceRows.end(),sourceY);
                const int row=found!=now.sourceRows.end() && *found==sourceY ?
                    int(found-now.sourceRows.begin()) : -1;
                for (int x=left;x<right;++x)
                {
                    const bool owned=old.Get(x,y,true), before=old.Get(x,y);
                    // A shifted explicitly sampled boundary row is not evidence
                    // for the same source pixels. Do not interpolate a glyph.
                    if (row<0) { if (owned || before) return reject("source-row-unavailable"); continue; }
                    const bool after=now.Get(x,row);
                    auto& c=columns[size_t(x-left)];
                    c.owned+=owned; c.covered+=owned&&after;
                    if(glyphEnvelope[size_t(y-top)*roiWidth+x-left]) {
                        c.different+=before!=after; c.total+=before||after; c.added+=!before&&after;
                    }
                    if (owned && after)
                    {
                        bool barMatch=false;
                        for (int dy=-1;dy<=1;++dy)
                            for (int dx=-1;dx<=1;++dx)
                                if (now.Get(x+dx,row+dy))
                                {
                                    const int matchedRow=row+dy;
                                    if (matchedRow>=0 && matchedRow<now.height &&
                                        ((current.pictureTop>0 && now.sourceRows[size_t(matchedRow)]<current.pictureTop+current.nearBarDistance) ||
                                         (current.pictureBottom<current.height && now.sourceRows[size_t(matchedRow)]>=current.pictureBottom-current.nearBarDistance)))
                                        barMatch=true;
                                }
                        currentBarInk+=barMatch;
                    }
                }
            }
            for (const auto& c:columns) {
                all.owned+=c.owned; all.covered+=c.covered;
                all.different+=c.different; all.total+=c.total; all.added+=c.added;
            }
            if(diagnostic) {
                diagnostic->minimumCoverage=(std::min)(diagnostic->minimumCoverage,
                    all.owned?all.covered*100/all.owned:0u);
                diagnostic->maximumDifference=(std::max)(diagnostic->maximumDifference,
                    all.total?all.different*100/all.total:0u);
            }
            if(tinyFringeOnLine && (all.covered*100<all.owned*98 ||
                all.different*100>all.total*2))return reject("tiny-component-line-change");
            if (all.owned<6 || all.covered*100<all.owned*minimumLineCoveragePercent ||
                all.different*100>all.total*(backedTracking ? 45u : 8u) ||
                (backedTracking && all.added*100>all.total*3)) {
                return reject("line-coverage-or-difference");
            }
            const int window=(std::max)(4,bottom-top);
            for (int start=0;start<right-left;start+=(std::max)(1,window/2))
            {
                Counts local;
                for (int x=start;x<(std::min)(right-left,start+window);++x) {
                    const auto& c=columns[size_t(x)];
                    local.owned+=c.owned;local.covered+=c.covered;
                    local.different+=c.different;local.total+=c.total;local.added+=c.added;
                }
                if (local.owned>=6 && (local.covered*100<local.owned*(backedTracking ? 25u : 85u) ||
                    local.different*100>local.total*(backedTracking ? 75u : 15u) ||
                    (backedTracking && local.added*100>local.total*8))) {
                    return reject("local-glyph-change");
                }
            }
        }
        if(diagnostic)diagnostic->reason="glyphs-match";
        // A picture-side pattern alone cannot preserve subtitle eligibility.
        if(reference.text.nearBarEligibilityMeasured) {
            if(!reference.text.nearBarEligible)return reject("context-or-pixel-proof");
            return currentBarInk>0 || (current.nearBarDistance>0 && StableBlackBacking(reference,current));
        }
        if(currentBarInk>0)return true;
        const auto& panel=reference.text.sourcePanel;
        return current.nearBarDistance>0 && panel.Valid() &&
            ((current.pictureTop>0 && panel.top<=current.pictureTop+current.nearBarDistance) ||
             (current.pictureBottom<current.height && panel.bottom>=current.pictureBottom-current.nearBarDistance)) &&
            StableBlackBacking(reference,current);
    }

    // Revalidate black pixels around each learned line. Split into horizontal
    // quarters so a surviving black corner cannot hide an exposed bright panel.
    // This is independent of glyph grouping and never grows the accepted box.
    inline bool StableBlackBacking(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current)
    {
        if(!SameContext(reference,current) || !reference.ink || !current.ink ||
            reference.text.lineCount<1 || reference.text.lineCount>3)return false;
        const auto& old=*reference.ink;const auto& now=*current.ink;
        const size_t words=(size_t(old.width)*old.height+63)/64;
        if(old.width<=0 || old.height<=0 || old.step<=0 || old.width!=now.width ||
            old.height!=now.height || old.step!=now.step || old.sourceRows!=now.sourceRows ||
            old.blackBacking.size()!=words || now.blackBacking.size()!=words)return false;
        for(int line=0;line<reference.text.lineCount;++line) {
            const auto& box=reference.text.lineBounds[line];
            if(!box.Valid())return false;
            const int left=(std::max)(0,box.left/old.step-1);
            const int right=(std::min)(old.width,(box.right+old.step-1)/old.step+1);
            const int top=(std::max)(0,box.top/old.step-1);
            const int bottom=(std::min)(old.height,(box.bottom+old.step-1)/old.step+1);
            unsigned expected[4]{},covered[4]{};
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                if(!old.Black(x,y) || old.Get(x,y))continue;
                const int bin=(std::min)(3,4*(x-left)/(std::max)(1,right-left));
                ++expected[bin];covered[bin]+=now.Black(x,y);
            }
            for(int bin=0;bin<4;++bin)
                if(expected[bin]<4 || covered[bin]*100<expected[bin]*90)return false;
        }
        return true;
    }

    // Retention can tolerate a small whole-line coverage fluctuation once
    // black backing is freshly proved. Component, local-window and added-ink
    // vetoes remain, with one tightly bounded tiny-fringe quantization allowance;
    // this does not authorize new glyph ownership.
    inline bool ConfidentSameInk(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current, InkMatchDiagnostics* diagnostic=nullptr)
    {
        const bool backed=StableBlackBacking(reference,current);
        const bool match=SameInkAtReference(reference,current,backed?90:95,false,diagnostic,backed);
        if(diagnostic)diagnostic->backingConfirmed=backed;
        return match;
    }

    inline bool BackedPartialInkAtReference(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current)
    {
        if(!StableBlackBacking(reference,current))return false;
        const bool samePanel=SameLine(reference.text.sourcePanel,current.text.sourcePanel,
            (std::max)(2,reference.ink->step));
        // A freshly measured identical card supplies additional evidence, but
        // blackness alone never proves text is still present on every line.
        return SameInkAtReference(reference,current,samePanel ? 60 : 70,true);
    }

    inline bool SameCrowdedBacking(const SubtitleBoxObservation& a,int aLine,
        const SubtitleBoxObservation& b,int bLine,int tolerance)
    {
        if(aLine<0 || aLine>=a.text.lineCount || bLine<0 || bLine>=b.text.lineCount)return false;
        if(a.text.capturePanelMeasured || b.text.capturePanelMeasured) {
            if(!a.text.capturePanelMeasured || !b.text.capturePanelMeasured)return false;
            const auto& ap=a.text.lineCapturePanels[aLine].Valid()?
                a.text.lineCapturePanels[aLine]:a.text.capturePanel;
            const auto& bp=b.text.lineCapturePanels[bLine].Valid()?
                b.text.lineCapturePanels[bLine]:b.text.capturePanel;
            // Live opaque ownership replaces the old broad contrast-edge hint.
            // An empty measured card must never fall back to cleanup/edge data.
            return ap.Valid() && bp.Valid() && SameLine(ap,bp,tolerance);
        }
        // Deprecated compatibility for callers without measured opaque cards.
        return SamePanelTopEdge(a.text.panelTopEdges[aLine],b.text.panelTopEdges[bLine],tolerance);
    }

    // A component/grouping budget failure is unknown, not a blank subtitle.
    // Only complete independent CURRENT samples may rescue an established cue;
    // the failed grouping itself supplies no acquisition or extension authority.
    inline bool WorkLimitHasCurrentCuePixels(const SubtitleBoxObservation& reference,
        const SubtitleBoxObservation& current)
    {
        if(!current.analyzed || !current.text.workLimit || current.analysisRefresh ||
            current.pendingRefresh || !current.ink || !current.ink->rawEvidenceComplete ||
            current.ink==reference.ink)return false;
        auto sampled=current;sampled.text.workLimit=false;
        return StableBlackBacking(reference,sampled) && SameInkAtReference(reference,sampled);
    }

    inline SubtitleBoxPreview Resolve(const SubtitleBoxObservation* frames, size_t count,
        uint64_t policy, uint64_t continuity)
    {
        SubtitleBoxPreview result;
        if (!frames || !count) return result;
        result.current=frames[0];result.policyGeneration=policy;result.continuityGeneration=continuity;
        if (!frames[0].analyzed) return result;
        result.available = true; result.current = frames[0]; result.text = frames[0].text;
        result.policyGeneration = policy; result.continuityGeneration = continuity;
        // Queued but unfinished analysis is unknown. It cannot veto a clear
        // current onset, nor lend independent proof through a later ready frame.
        size_t readyCount=1;
        for(;readyCount<(std::min)(count,MaxFrames);++readyCount) {
            const auto& previous=frames[readyCount-1];const auto& next=frames[readyCount];
            const auto& a=previous.identity;const auto& b=next.identity;
            if(!next.analyzed || next.pendingRefresh || next.analysisRefresh || next.text.workLimit || next.discontinuity ||
                b.acceptedSequence!=a.acceptedSequence+1 ||
                a.transportGeneration!=b.transportGeneration || a.sourceFormatGeneration!=b.sourceFormatGeneration ||
                a.viewportGeneration!=b.viewportGeneration || a.rendererGeneration!=b.rendererGeneration ||
                previous.width!=next.width || previous.height!=next.height ||
                previous.nearBarDistance!=next.nearBarDistance || !SameMeasurementProvenance(previous,next) ||
                previous.policyGeneration!=next.policyGeneration ||
                previous.continuityGeneration!=next.continuityGeneration)break;
        }
        count=readyCount;
        result.futureAvailable = count > 1;
        result.followingCount=static_cast<unsigned>((std::min)(count-1,result.following.size()));
        for (unsigned i=0;i<result.followingCount;++i) result.following[i]=frames[i+1];
        if (!HasBarEvidence(frames[0]) || !frames[0].text.detected || frames[0].text.workLimit) { result.text = {}; return result; }
        result.matchingFrames = 1;
        for (size_t i = 1; i < (std::min)(count,MaxFrames); ++i)
        {
            const auto& next = frames[i];
            bool cueMatches=SameCue(frames[0],next) && SameCue(frames[i-1],next);
            // The only relaxed path is a crowded grouping (three or more
            // candidate rows). Require two independently matching rows and
            // the bar anchor; ordinary one/two-row cue changes keep the
            // original all-lines SameCue rule.
            if(!cueMatches && frames[0].text.lineCount>=3 && next.text.lineCount>=2 &&
                next.analyzed && !next.discontinuity &&
                next.identity.acceptedSequence==frames[i-1].identity.acceptedSequence+1 &&
                SameContext(frames[0],next) && next.text.detected && HasBarEvidence(next)) {
                const int currentAnchor=BarAnchorLine(frames[0]);
                const int nextAnchor=BarAnchorLine(next);
                int confirmedRows=0;
                const int tol=(std::max)(4,frames[0].height/180);
                bool anchorMatch=currentAnchor>=0 && nextAnchor>=0 &&
                    SameLine(frames[0].text.lineBounds[currentAnchor],next.text.lineBounds[nextAnchor],tol) &&
                    SimilarSignature(frames[0].text.lineSignatures[currentAnchor],next.text.lineSignatures[nextAnchor]) &&
                    SameLinePixels(frames[0],currentAnchor,next,nextAnchor);
                if(anchorMatch) {
                    for(int a=0;a<frames[0].text.lineCount;++a) for(int b=0;b<next.text.lineCount;++b)
                        if(a!=currentAnchor && b!=nextAnchor &&
                            SameCrowdedLine(frames[0].text.lineBounds[a],next.text.lineBounds[b],tol) &&
                            SimilarSignature(frames[0].text.lineSignatures[a],next.text.lineSignatures[b]) &&
                            SameLinePixels(frames[0],a,next,b)) { ++confirmedRows; break; }
                    cueMatches=confirmedRows>=1;
                }
            }
            if (!next.analyzed || next.discontinuity ||
                next.identity.acceptedSequence != frames[i-1].identity.acceptedSequence + 1 ||
                !cueMatches) break;
            ++result.matchingFrames;
        }
        const int tolerance = (std::max)(4,frames[0].height/180);
        size_t crowdedFarthest=0;int crowdedAnchorLine=-1;
        if(frames[0].text.lineCount>=3) {
            crowdedAnchorLine=BarAnchorLine(frames[0]);
            if(crowdedAnchorLine>=0) for(size_t i=1;i<(std::min)(count,MaxFrames);++i) {
                const auto& prev=frames[i-1];const auto& next=frames[i];
                if(!next.analyzed || next.discontinuity || !next.text.detected || !HasBarEvidence(next) ||
                    !SameContext(frames[0],next) ||
                    next.identity.acceptedSequence!=prev.identity.acceptedSequence+1) break;
                bool anchorVisible=false;
                for(int line=0;line<next.text.lineCount;++line)
                    if((next.text.lineBounds[line].top<next.pictureTop ||
                        next.text.lineBounds[line].bottom>next.pictureBottom || line==BarAnchorLine(next)) &&
                        SameLine(frames[0].text.lineBounds[crowdedAnchorLine],next.text.lineBounds[line],tolerance) &&
                        SimilarSignature(frames[0].text.lineSignatures[crowdedAnchorLine],next.text.lineSignatures[line]) &&
                        SameLinePixels(frames[0],crowdedAnchorLine,next,line)) {anchorVisible=true;break;}
                if(!anchorVisible) break;
                crowdedFarthest=i;
            }
        }
        result.currentLinesConfirmed = true;
        std::array<int,3> acceptedLines{}; int acceptedCount=0;
        int nearestLine=-1;unsigned nearestGap=(std::numeric_limits<unsigned>::max)();
        if(frames[0].text.lineCount>=3 && crowdedAnchorLine>=0) {
            const auto& anchor=frames[0].text.lineBounds[crowdedAnchorLine];
            for(int line=0;line<frames[0].text.lineCount;++line) if(line!=crowdedAnchorLine) {
                const auto& candidate=frames[0].text.lineBounds[line];
                const unsigned gap=candidate.bottom<=anchor.top?unsigned(anchor.top-candidate.bottom):
                    (anchor.bottom<=candidate.top?unsigned(candidate.top-anchor.bottom):0u);
                if(gap<nearestGap) {nearestGap=gap;nearestLine=line;}
            }
        }
        for (int line = 0; line < frames[0].text.lineCount; ++line)
        {
            unsigned support = 0;
            const size_t supportEnd=(frames[0].text.lineCount>=3)?crowdedFarthest+1:result.matchingFrames;
            for (size_t frame = 0; frame < supportEnd; ++frame)
                for (int other = 0; other < frames[frame].text.lineCount; ++other)
                    if ((frames[0].text.lineCount>=3?
                        SameCrowdedLine(frames[0].text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance):
                        SameLine(frames[0].text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance)) &&
                        SimilarSignature(frames[0].text.lineSignatures[line],frames[frame].text.lineSignatures[other]) &&
                        (frames[0].text.lineCount<3 || SameLinePixels(frames[0],line,frames[frame],other)))
                    { ++support; break; }
            const unsigned required=(frames[0].text.lineCount>=3 && supportEnd>=4)?3u:2u;
            const double farthestCoverage=frames[0].text.lineCount<3?1.0:
                OwnedLinePixelCoverage(frames[0],line,frames[crowdedFarthest]);
            const bool fullHorizon=supportEnd>=4;
            // A consistent verified opaque card may resolve a borderline
            // picture-side row, but only with matching current/future glyph
            // signatures and at least 90% of its fixed glyph pixels present.
            // It cannot relax the mandatory bar/card anchor or four-frame window.
            unsigned panelMatches=0;bool farthestPanelMatch=false;
            if(fullHorizon && line!=crowdedAnchorLine)
                for(size_t frame=0;frame<supportEnd;++frame)
                    for(int other=0;other<frames[frame].text.lineCount;++other)
                        if(SameCrowdedLine(frames[0].text.lineBounds[line],
                                frames[frame].text.lineBounds[other],tolerance) &&
                            SimilarSignature(frames[0].text.lineSignatures[line],
                                frames[frame].text.lineSignatures[other]) &&
                            SameCrowdedBacking(frames[0],line,frames[frame],other,tolerance) &&
                            SameLinePixels(frames[0],line,frames[frame],other)) {
                            ++panelMatches;
                            if(frame==crowdedFarthest) farthestPanelMatch=true;
                            break;
                        }
            const bool stablePanel=panelMatches>=3 && farthestPanelMatch;
            const double minimumCoverage=(line==crowdedAnchorLine || stablePanel)?0.90:0.975;
            const int anchorHeight=crowdedAnchorLine>=0?
                frames[0].text.lineBounds[crowdedAnchorLine].bottom-
                    frames[0].text.lineBounds[crowdedAnchorLine].top:0;
            const int candidateHeight=frames[0].text.lineBounds[line].bottom-
                frames[0].text.lineBounds[line].top;
            const bool nearestInShortWindow=line==nearestLine &&
                nearestGap<=unsigned((std::max)(anchorHeight,candidateHeight)*3/2) &&
                farthestCoverage>=0.90;
            const bool farthestPixels=frames[0].text.lineCount<3 ||
                (line==crowdedAnchorLine?farthestCoverage>=minimumCoverage:
                    (fullHorizon?farthestCoverage>=minimumCoverage:nearestInShortWindow));
            if(support>=required && farthestPixels) acceptedLines[acceptedCount++]=line;
            else result.currentLinesConfirmed=false;
        }
        if(frames[0].text.lineCount>=3 && acceptedCount==0) {
            result.text={};
        } else if(frames[0].text.lineCount>=3 && acceptedCount<frames[0].text.lineCount) {
            result.text.bounds=frames[0].text.lineBounds[acceptedLines[0]];
            result.text.lineCount=0;
            result.text.sourcePanel={};
            result.text.linePanels={};
            result.text.capturePanel={};
            result.text.lineCapturePanels={};
            for(int k=0;k<acceptedCount;++k) {
                const int line=acceptedLines[k];
                result.text.lineBounds[k]=frames[0].text.lineBounds[line];
                result.text.lineSignatures[k]=frames[0].text.lineSignatures[line];
                result.text.panelTopEdges[k]=frames[0].text.panelTopEdges[line];
                const auto panel=frames[0].text.linePanels[line];
                result.text.linePanels[k]=panel;
                const auto interior=frames[0].text.lineCapturePanels[line];
                result.text.lineCapturePanels[k]=interior;
                if(interior.Valid()) {
                    auto& coverage=result.text.capturePanel;
                    coverage=coverage.Valid()?SubtitleBoxRect{(std::min)(coverage.left,interior.left),
                        (std::min)(coverage.top,interior.top),(std::max)(coverage.right,interior.right),
                        (std::max)(coverage.bottom,interior.bottom)}:interior;
                }
                if(panel.Valid()) {
                    auto& coverage=result.text.sourcePanel;
                    coverage=coverage.Valid()?SubtitleBoxRect{(std::min)(coverage.left,panel.left),
                        (std::min)(coverage.top,panel.top),(std::max)(coverage.right,panel.right),
                        (std::max)(coverage.bottom,panel.bottom)}:panel;
                }
                const auto& r=frames[0].text.lineBounds[line]; auto& box=result.text.bounds;
                box={(std::min)(box.left,r.left),(std::min)(box.top,r.top),
                    (std::max)(box.right,r.right),(std::max)(box.bottom,r.bottom)};
                ++result.text.lineCount;
            }
            result.text.anchor=frames[0].text.anchor;
        }
        if(frames[0].text.lineCount>=3) result.currentLinesConfirmed=acceptedCount>0;
        // In the crowded-grouping path the current frame is authoritative
        // after per-row pruning. The legacy whole-box future union would
        // reintroduce the very transient row this gate removed.
        if(frames[0].text.lineCount>=3) return result;
        for (size_t i = 1; i < result.matchingFrames; ++i)
        {
            const auto& next = frames[i];
            bool companionsConfirmed = true;
            // A transient picture-side stroke group must not enlarge the box
            // just because the actual bar subtitle persists beneath it.
            for (int line = 0; line < next.text.lineCount; ++line)
            {
                bool presentNow = false;
                for (int current = 0; current < frames[0].text.lineCount; ++current)
                    presentNow = presentNow || (SameLine(next.text.lineBounds[line],
                        frames[0].text.lineBounds[current],tolerance) &&
                        SimilarSignature(next.text.lineSignatures[line],frames[0].text.lineSignatures[current]));
                if (presentNow) continue;
                // Future ink cannot authorize expansion onto unbacked current
                // picture pixels. Only current measured opaque-card coverage
                // can admit a picture-side companion missed by grouping.
                const auto& candidate=next.text.lineBounds[line];
                const auto& panel=frames[0].text.capturePanelMeasured?frames[0].text.capturePanel:frames[0].text.sourcePanel;
                const int pictureFirst=(std::max)(candidate.top,frames[0].pictureTop);
                const int pictureLast=(std::min)(candidate.bottom,frames[0].pictureBottom);
                if(pictureFirst<pictureLast && (!panel.Valid() || candidate.left<panel.left ||
                    candidate.right>panel.right || pictureFirst<panel.top || pictureLast>panel.bottom)) {
                    companionsConfirmed=false;break;
                }
                unsigned support = 0;
                for (size_t frame = 0; frame < result.matchingFrames; ++frame)
                    for (int other = 0; other < frames[frame].text.lineCount; ++other)
                        if (SameLine(next.text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance) &&
                            SimilarSignature(next.text.lineSignatures[line],frames[frame].text.lineSignatures[other]))
                        { ++support; break; }
                companionsConfirmed = companionsConfirmed && support >= 2;
                if(frames[0].text.lineCount>=2 && frames[0].text.lineCount<3 &&
                    next.text.lineCount>frames[0].text.lineCount) {
                    // A future-only picture row may enlarge a two-line current
                    // cue only after a complete four-observation window proves
                    // both the current pixels and a stable near-exact row track.
                    unsigned trackSupport=0;
                    if(result.matchingFrames>=4 &&
                        OwnedLinePixelCoverage(next,line,frames[0])>=0.975 &&
                        OwnedLinePixelCoverage(next,line,frames[result.matchingFrames-1])>=0.975) {
                        for(size_t frame=1;frame<result.matchingFrames;++frame)
                            for(int other=0;other<frames[frame].text.lineCount;++other)
                                if(SameCrowdedLine(next.text.lineBounds[line],
                                    frames[frame].text.lineBounds[other],tolerance) &&
                                    SimilarSignature(next.text.lineSignatures[line],
                                        frames[frame].text.lineSignatures[other])) {
                                    ++trackSupport;break;
                                }
                    }
                    companionsConfirmed=companionsConfirmed && trackSupport>=3;
                }
            }
            if (!companionsConfirmed) continue;
            // Future grouping may reveal an already present line, but future
            // pixels must never create a missing line on the current frame.
            if (next.text.lineCount>frames[0].text.lineCount &&
                !SameInkAtReference(next,frames[0])) continue;
            const auto& r = next.text.bounds;
            auto& box = result.text.bounds;
            box = {(std::min)(box.left,r.left),(std::min)(box.top,r.top),
                (std::max)(box.right,r.right),(std::max)(box.bottom,r.bottom)};
            if (next.text.lineCount > result.text.lineCount)
            {
                result.text.lineCount = next.text.lineCount;
                result.text.lineBounds = next.text.lineBounds;
                result.text.lineSignatures = next.text.lineSignatures;
                result.text.panelTopEdges = next.text.panelTopEdges;
                result.text.linePanels = next.text.linePanels;
                if(result.text.capturePanelMeasured) {
                    for(int line=0;line<result.text.lineCount;++line) {
                        const auto& r=next.text.lineCapturePanels[line];const auto& card=result.text.capturePanel;
                        auto& interior=result.text.lineCapturePanels[line];
                        interior={(std::max)(r.left,card.left),(std::max)(r.top,card.top),
                            (std::min)(r.right,card.right),(std::min)(r.bottom,card.bottom)};
                        if(!interior.Valid())interior={};
                    }
                }
                result.text.signature = next.text.signature;
            }
        }
        return result;
    }

    inline bool IsCurrent(const SubtitleBoxPreview& result,
        const ActivePictureFrameIdentity& identity, uint64_t policy, uint64_t continuity)
    {
        return result.available && SameActivePictureFrameIdentity(result.current.identity,identity) &&
            result.policyGeneration == policy && result.continuityGeneration == continuity;
    }
}

// Only presentation advances this state. Looking into future pixels never moves
// the current cue forward, and repeated output cannot manufacture confirmation.
class SubtitleBoxPresentation
{
public:
    void Reset() {
        m_previous = {}; m_reference = {}; m_inkReference = {}; m_result = {};
        m_barTrackingReference = {};
        m_expansionEvidence.clear();
        m_hasPrevious = false; m_bridgedFrames = 0;
        m_diagnosticWeakMs=0;m_decisionReason="reset";m_referenceSamples=0;m_referenceRefresh="reset";m_refreshedReference={};
        m_weakTemplateMilliseconds = 0; m_weakTemplateExhausted = false;
    }
    const char* DecisionReason() const { return m_decisionReason; }
    const SubtitleBoxLookahead::InkMatchDiagnostics& InkDiagnostics() const { return m_inkDiagnostic; }
    double WeakHoldMilliseconds() const { return m_diagnosticWeakMs; }
    SubtitleBarTrackingReference BarTrackingReferenceFor(
        const ActivePictureFrameIdentity& next, int width, int height,
        uint64_t policyGeneration = 0, uint64_t continuityGeneration = 0) const
    {
        return m_barTrackingReference.IsImmediatePredecessor(next,width,height,
            policyGeneration,continuityGeneration)
            ? m_barTrackingReference : SubtitleBarTrackingReference{};
    }
    // Detection regions are proposals, not negative evidence outside them.
    // Re-read established glyphs and their immediate backing from this frame
    // before continuity decides whether a partial grouping is a new caption.
    // No remembered image pixels or ownership bits are copied into the result.
    bool RefreshReferencePixels(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source) {
        m_referenceSamples=0; m_referenceRefresh="not-eligible"; m_refreshedReference={};
        if(!preview.available || !m_hasPrevious || !m_result.detected ||
            preview.current.discontinuity || preview.current.pendingRefresh ||
            preview.current.identity.acceptedSequence!=m_previous.current.identity.acceptedSequence+1 ||
            preview.policyGeneration!=m_previous.policyGeneration ||
            preview.continuityGeneration!=m_previous.continuityGeneration ||
            !SubtitleBoxLookahead::SameContext(m_inkReference,preview.current) ||
            !source.IsValid() || source.width!=preview.current.width || source.height!=preview.current.height ||
            !m_inkReference.ink || !m_inkReference.ink->paletteValid)return false;
        const auto& palette=*m_inkReference.ink;
        if(!preview.current.ink || preview.current.ink->width!=palette.width ||
            preview.current.ink->height!=palette.height || preview.current.ink->step!=palette.step ||
            preview.current.ink->sourceRows!=palette.sourceRows || palette.step<=0)return false;
        auto fresh=std::make_shared<SubtitleInkSnapshot>(*preview.current.ink);
        const size_t words=(size_t(fresh->width)*fresh->height+63)/64;
        if(fresh->rawInk.size()!=words || fresh->blackBacking.size()!=words)return false;
        for(int line=0;line<m_result.lineCount && line<3;++line) {
            const auto& box=m_result.lineBounds[line];
            if(!box.Valid())return false;
            const int left=(std::max)(0,box.left/fresh->step-1);
            const int right=(std::min)(fresh->width,(box.right+fresh->step-1)/fresh->step+1);
            const int top=(std::max)(0,box.top/fresh->step-1);
            const int bottom=(std::min)(fresh->height,(box.bottom+fresh->step-1)/fresh->step+1);
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                if(m_referenceSamples>=65536) {m_referenceRefresh="budget";return false;}
                AnalysisLumaSample pixel;
                if(!source.Sample(x*fresh->step,fresh->sourceRows[y],pixel)) {
                    m_referenceRefresh="sample-failed";return false;
                }
                ++m_referenceSamples;
                const size_t i=size_t(y)*fresh->width+x;const uint64_t bit=uint64_t(1)<<(i%64);
                const bool bright=pixel.luma>=palette.inkFloor &&
                    std::abs(int(pixel.chromaU)-palette.inkU)<=80 &&
                    std::abs(int(pixel.chromaV)-palette.inkV)<=80;
                fresh->rawInk[i/64]=(fresh->rawInk[i/64]&~bit)|(bright?bit:0);
                fresh->blackBacking[i/64]=(fresh->blackBacking[i/64]&~bit)|
                    (pixel.luma<=palette.blackLimit?bit:0);
            }
        }
        // This uses the acquisition palette only to revalidate that caption.
        // Never replace the detector snapshot used to acquire a new caption.
        m_refreshedReference=preview.current;
        m_refreshedReference.ink=std::move(fresh);
        m_referenceRefresh="current-reference-pixels";
        return true;
    }
    unsigned ReferenceSampleCount() const {return m_referenceSamples;}
    const char* ReferenceRefreshReason() const {return m_referenceRefresh;}
    bool ReusePreviewForRepeatedSourceFrame(const ActivePictureFrameIdentity& identity,
        int width, int height, bool discontinuity,
        uint64_t policyGeneration, uint64_t continuityGeneration,
        SubtitleBoxPreview& preview) const
    {
        if (!m_hasPrevious || !m_previous.available ||
            !SameActivePictureFrameIdentity(m_previous.current.identity,identity) ||
            m_previous.current.width != width || m_previous.current.height != height ||
            m_previous.current.discontinuity != discontinuity ||
            m_previous.policyGeneration != policyGeneration ||
            m_previous.continuityGeneration != continuityGeneration) return false;
        preview = m_previous;
        preview.newScanMs = 0.0;
        preview.newScanFrames = 0;
        return true;
    }
    SubtitleBoxResult Consume(const SubtitleBoxPreview& preview, int holdMilliseconds = 250,
        double sourceFrameMilliseconds = 1000.0/60.0)
    {
        if (!preview.available) { Reset(); m_decisionReason="preview-unavailable"; return {}; }
        // Assisted source proof is checked by the caller before Consume. A
        // failed current proof must not revive the old cue through repeated-
        // frame, fixed-template, or pending-worker continuation.
        if (preview.current.assistedSourceCandidate &&
            (!preview.current.barAuthority || !preview.current.analyzed ||
             !preview.current.text.detected || preview.current.text.workLimit ||
             preview.current.text.held || preview.current.pendingRefresh || preview.current.analysisRefresh)) {
            Reset();m_decisionReason="assisted-current-proof-unavailable";return {};
        }
        if (m_hasPrevious && SubtitleBoxLookahead::SameMeasurementProvenance(m_previous.current,preview.current) &&
            SameActivePictureFrameIdentity(m_previous.current.identity,preview.current.identity) &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration) return m_result;
        auto continuityPreview=preview;
        if(m_refreshedReference.ink &&
            SameActivePictureFrameIdentity(m_refreshedReference.identity,preview.current.identity) &&
            SubtitleBoxLookahead::SameContext(m_refreshedReference,preview.current))
            continuityPreview.current.ink=m_refreshedReference.ink;
        m_refreshedReference={};
        const auto& continuity=continuityPreview.current;
        bool same = m_hasPrevious && !preview.current.discontinuity &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration &&
            preview.current.identity.acceptedSequence == m_previous.current.identity.acceptedSequence + 1 &&
            SubtitleBoxLookahead::SameCue(m_previous.current,preview.current) &&
            SubtitleBoxLookahead::SameCue(m_reference,preview.current) &&
            preview.text.lineCount >= m_result.lineCount && m_result.detected &&
            ((preview.text.lineCount > m_result.lineCount &&
                (!preview.futureAvailable || preview.currentLinesConfirmed ||
                    SubtitleBoxLookahead::ContainsMatchedInk(m_result.bounds,preview.current.text,m_result,
                (std::max)(4,preview.current.height/180)))) ||
                SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,preview.current.text));
        const bool currentTemplatePixelsMatch=
            SubtitleBoxLookahead::ConfidentSameInk(m_inkReference,continuity,&m_inkDiagnostic);
        // Coarse grouping signatures preserve geometry, but cannot overrule a
        // changed local glyph at the fixed acquisition coordinates.
        if(m_hasPrevious && m_result.detected && m_inkReference.ink && !currentTemplatePixelsMatch)same=false;
        const bool groupingMatches=SubtitleBoxLookahead::SameCue(m_reference,preview.current);
        const bool expansionStillVisible=ExpansionStillVisible(continuityPreview);
        bool workLimitCurrentPixelsMatch=SubtitleBoxLookahead::WorkLimitHasCurrentCuePixels(m_inkReference,continuity);
        // Learned extensions also need current pixels: the resource-limit path
        // must not borrow missing extension ink from a queued future frame.
        if(workLimitCurrentPixelsMatch)for(const auto& evidence:m_expansionEvidence)
            if(!SameExtensionInk(evidence.ink,continuity.ink,evidence.bounds)) {
                workLimitCurrentPixelsMatch=false;break;
            }
        const bool continuationContextEligible=m_hasPrevious && m_result.detected &&
            !preview.current.discontinuity &&
            preview.current.identity.acceptedSequence==m_previous.current.identity.acceptedSequence+1 &&
            m_previous.policyGeneration==preview.policyGeneration &&
            m_previous.continuityGeneration==preview.continuityGeneration &&
            SubtitleBoxLookahead::SameContext(m_inkReference,preview.current) &&
            (!preview.current.text.workLimit || workLimitCurrentPixelsMatch) && expansionStillVisible;
        const bool currentTemplateEligible=continuationContextEligible && currentTemplatePixelsMatch;
        // Segmentation can lose a few sampled glyph pixels for several frames.
        // Retain only an already acquired, unchanged box when
        // every learned component, local window, glyph interior and bar pixel
        // still passes the normal checks; relax only whole-line coverage.
        const bool detectedSubset=preview.current.text.detected &&
            preview.current.text.lineCount>0 &&
            preview.current.text.lineCount<=m_inkReference.text.lineCount &&
            SubtitleBoxLookahead::SameCue(m_inkReference,preview.current) &&
            SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,preview.current.text);
        // Count source time, not renderer refreshes. Repeated presentations return
        // above without spending or renewing the grace period. Only strict current
        // glyph proof renews it; a blank/changed cue still fails the pixel tests.
        const double frameMs=std::isfinite(sourceFrameMilliseconds) && sourceFrameMilliseconds>0.0
            ? sourceFrameMilliseconds : 1000.0/60.0;
        const double holdMs=(std::max)(0,(std::min)(1000,holdMilliseconds));
        // Crowded Resolve marks currentLinesConfirmed when ANY row survives.
        // A confirmed subset is not proof that the entire caption changed.
        const bool confirmedReplacement=preview.current.text.detected && preview.text.detected &&
            preview.text.lineCount==preview.current.text.lineCount &&
            preview.currentLinesConfirmed && preview.matchingFrames>=2 &&
            preview.current.text.lineCount==m_inkReference.text.lineCount &&
            SubtitleBoxLookahead::SameLine(preview.current.text.bounds,m_inkReference.text.bounds,
                (std::max)(4,preview.current.height/180)) &&
            !SubtitleBoxLookahead::SameCue(m_inkReference,preview.current);
        const bool backedPartialEligible=continuationContextEligible && !currentTemplatePixelsMatch &&
            !confirmedReplacement &&
            (!preview.current.text.detected || SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,preview.current.text)) &&
            SubtitleBoxLookahead::BackedPartialInkAtReference(m_inkReference,continuity);
        const bool weakTemplateEligible=!m_weakTemplateExhausted &&
            m_weakTemplateMilliseconds+frameMs<=holdMs+0.000001 && continuationContextEligible &&
            !currentTemplatePixelsMatch &&
            (((!preview.current.text.detected || detectedSubset) &&
                SubtitleBoxLookahead::SameInkAtReference(m_inkReference,continuity,90)) ||
                backedPartialEligible || workLimitCurrentPixelsMatch) &&
            NoNewInkOutsideReference(continuity);
        SubtitleBoxRect novelExtension,confirmedExtension;
        const bool hasNovelExtension=currentTemplateEligible && preview.current.ink &&
            GatherNewInkExtension(*preview.current.ink,novelExtension);
        const bool extensionConfirmed=hasNovelExtension &&
            FindConfirmedInkExtension(preview,novelExtension,confirmedExtension) &&
            ExtensionHasCurrentBacking(preview.current,confirmedExtension) &&
            CanAttachConfirmedExtension(m_result,confirmedExtension);
        // An established glyph template belongs to CURRENT pixels. Detector
        // grouping may jump or fail while those exact strokes remain visible;
        // do not let a future grouping vote move the box away from current ink.
        // Changed/missing glyphs, a bar/context break, and discontinuities still
        // invalidate this fixed-coordinate proof. Newly added lines are handled
        // separately below and need their own current-plus-future evidence.
        bool templateStillVisible=(currentTemplateEligible &&
            // A total detector miss can be ambiguous when new glyph-like ink
            // has appeared outside the learned lines (such as a newly split
            // second line). Keep it only when the new ink is either absent or
            // already confirmed in the current and queued source frames.
            (preview.current.text.detected || !hasNovelExtension || extensionConfirmed)) ||
            weakTemplateEligible;
        if(currentTemplateEligible) {
            m_weakTemplateMilliseconds=0;
            m_weakTemplateExhausted=false;
        } else if(weakTemplateEligible) {
            m_weakTemplateMilliseconds+=frameMs;
            if(m_weakTemplateMilliseconds+frameMs>holdMs+0.000001)
                m_weakTemplateExhausted=true;
        }
        if(!expansionStillVisible) { templateStillVisible=false; same=false; }
        bool captureContainsReference=true;
        if(currentTemplateEligible && m_inkDiagnostic.backingConfirmed && m_result.capturePanelMeasured) {
            const auto contains=[](const SubtitleBoxRect& outer,const SubtitleBoxRect& inner) {
                return outer.Valid() && outer.left<=inner.left && outer.right>=inner.right &&
                    outer.top<=inner.top && outer.bottom>=inner.bottom;
            };
            for(const auto* candidate : {&preview.current.text,&preview.text}) {
              const auto& now=*candidate;
              for(int i=0;i<m_result.lineCount;++i) {
                const auto& line=m_result.lineBounds[i];
                bool hasLine=false;
                for(int j=0;j<now.lineCount;++j)hasLine|=contains(now.lineBounds[j],line);
                auto inside=line;
                inside.top=(std::max)(inside.top,preview.current.pictureTop);
                inside.bottom=(std::min)(inside.bottom,preview.current.pictureBottom);
                captureContainsReference &= hasLine && (!inside.Valid() ||
                    (now.capturePanelMeasured && contains(now.capturePanel,inside)));
              }
            }
        }
        const bool templateHeld=templateStillVisible &&
            (weakTemplateEligible || !groupingMatches || !captureContainsReference ||
                preview.current.text.lineCount<m_result.lineCount || preview.text.lineCount<m_result.lineCount);
        if (templateStillVisible) same=true;
        auto result = preview.text;
        const bool trackedCueConfirmed = preview.current.barTrackingAuthority &&
            preview.current.text.detected && preview.matchingFrames >= 2 &&
            preview.currentLinesConfirmed;
        if (!SubtitleBoxLookahead::HasBarEvidence(preview.current) ||
            (!preview.current.text.detected && !templateStillVisible) ||
            (!preview.current.barAuthority && !same && !trackedCueConfirmed) ||
            (preview.current.currentAnchorUsesTrackedEdge && !same && !trackedCueConfirmed) ||
            (!same && preview.futureAvailable &&
                (preview.matchingFrames < 2 || !preview.currentLinesConfirmed))) result = {};
        if (templateHeld) { result=m_result;result.held=true;result.revised=false; }
        const bool bridged=!result.detected && CanBridge(preview);
        if (bridged) {
            result=m_result; result.held=true; result.revised=false;
            ++m_bridgedFrames;
        }
        if (result.detected && !bridged && !templateHeld)
        {
            result.held=false; m_bridgedFrames=0;
            if (same)
            {
                // Fresh matching strokes still support the established cue,
                // but one unmatched companion cannot enlarge its stable box.
                if (preview.futureAvailable && !preview.currentLinesConfirmed &&
                    result.lineCount > m_result.lineCount) result = m_result;
                const auto& old = m_result.bounds;
                if (result.lineCount > m_result.lineCount)
                {
                    result.bounds = {(std::min)(old.left,result.bounds.left),(std::min)(old.top,result.bounds.top),
                        (std::max)(old.right,result.bounds.right),(std::max)(old.bottom,result.bounds.bottom)};
                    m_reference.text = result;
                    m_inkReference = {};
                }
                else result.bounds = old;
                result.lineCount = (std::max)(result.lineCount,m_result.lineCount);
                result.cue = m_result.cue; result.observations = m_result.observations + 1;
                result.revised = !SubtitleBoxLookahead::SameLine(old,result.bounds,0);
            }
            else
            {
                result.cue = ++m_nextCue; result.observations = 1; result.revised = false;
                m_reference = preview.current; m_reference.text = result;
                m_inkReference = AcquisitionReference(preview.current,result);
                m_expansionEvidence.clear();
            }
        }
        // Discovery remains active even when segmentation reports exactly the
        // old lines. A new word or second line outside the acquired glyph mask
        // must either be proved in current and next pixels or bypass this frame.
        if(result.detected && same && extensionConfirmed) {
            // The same verified extent must reach both the stable display box
            // and extraction. Expanding only bounds silently dropped the new
            // glyphs at the shader's per-line capture mask.
            AttachConfirmedExtension(result,confirmedExtension);
            if(preview.current.text.capturePanelMeasured) {
                // Only the proved extension itself may enlarge held capture
                // authority. A newer broad cleanup fringe cannot authorize ink.
                SubtitleBoxRect inside{confirmedExtension.left,
                    (std::max)(confirmedExtension.top,preview.current.pictureTop),confirmedExtension.right,
                    (std::min)(confirmedExtension.bottom,preview.current.pictureBottom)};
                if(inside.Valid()) {
                    const auto& currentCard=preview.current.text.capturePanel;
                    const auto& priorCard=result.capturePanelMeasured?result.capturePanel:result.sourcePanel;
                    const auto& cleanup=preview.current.text.sourcePanel;
                    const bool provedSameCard=currentCard.Valid() && priorCard.Valid() && cleanup.Valid() &&
                        inside.left>=currentCard.left && inside.top>=currentCard.top &&
                        inside.right<=currentCard.right && inside.bottom<=currentCard.bottom &&
                        (std::min)(priorCard.right,currentCard.right)>(std::max)(priorCard.left,currentCard.left) &&
                        (std::min)(priorCard.bottom,currentCard.bottom)>(std::max)(priorCard.top,currentCard.top);
                    if(provedSameCard) {
                        // The full current card may have become measurable only
                        // after grouping recovered. Keep its removal footprint,
                        // including its edge guard, without promoting that guard
                        // to glyph authority. Unrelated/distant cards cannot grow it.
                        auto& old=result.sourcePanel;
                        old=old.Valid()?SubtitleBoxRect{(std::min)(old.left,cleanup.left),
                            (std::min)(old.top,cleanup.top),(std::max)(old.right,cleanup.right),
                            (std::max)(old.bottom,cleanup.bottom)}:cleanup;
                    }
                    auto& card=result.capturePanel;
                    card=card.Valid()?SubtitleBoxRect{(std::min)(card.left,inside.left),
                        (std::min)(card.top,inside.top),(std::max)(card.right,inside.right),
                        (std::max)(card.bottom,inside.bottom)}:inside;
                }
                result.capturePanelMeasured=true;
            }
            const auto old=result.bounds;
            result.bounds={(std::min)(old.left,confirmedExtension.left),(std::min)(old.top,confirmedExtension.top),
                (std::max)(old.right,confirmedExtension.right),(std::max)(old.bottom,confirmedExtension.bottom)};
            result.revised=!SubtitleBoxLookahead::SameLine(old,result.bounds,0);
            if(result.revised)result.held=false;
            CaptureExtensionInk(preview.current,confirmedExtension);
        }
        if (result.detected && !bridged && !templateHeld) result.held=false;
        m_diagnosticWeakMs=m_weakTemplateMilliseconds;
        m_decisionReason = result.detected
            ? (preview.current.pendingRefresh ? "worker-pending-current-pixels" : weakTemplateEligible ? (workLimitCurrentPixelsMatch ? "grace-resource-limit-current-ink" : backedPartialEligible ? "grace-black-panel-partial-ink" : "grace-current-ink") : bridged ? "lookahead-bridge" : templateHeld ? "strict-current-ink" : "detected")
            : preview.current.discontinuity ? "discontinuity"
            : !SubtitleBoxLookahead::HasBarEvidence(preview.current) ? "bar-evidence-lost"
            : preview.current.text.workLimit ? "analysis-work-limit"
            : m_weakTemplateMilliseconds+frameMs>holdMs+0.000001 && m_weakTemplateMilliseconds>0 ? "grace-expired"
            : !expansionStillVisible ? "companion-ink-changed"
            : !currentTemplatePixelsMatch ? "glyph-proof-lost-or-changed" : "unconfirmed-detection";
        if (!result.detected) {
            m_inkReference={};m_expansionEvidence.clear();
            m_bridgedFrames=0;
            m_weakTemplateMilliseconds=0; m_weakTemplateExhausted=false;
        }
        else if (!bridged && (!m_inkReference.ink ||
            m_inkReference.text.lineCount!=result.lineCount) &&
            preview.current.text.lineCount==result.lineCount && preview.current.ink &&
            SubtitleBoxLookahead::SameCue(m_reference,preview.current))
            m_inkReference=preview.current;
        m_previous = preview;
        // Keep only accepted cue evidence as the temporal reference. Rejected
        // transient companions must not poison the next frame's comparison.
        m_previous.current.text = result;
        m_result = result; m_hasPrevious = true;
        m_barTrackingReference = SubtitleBoxLookahead::AdvanceBarTrackingReference(preview.current);
        return result;
    }
private:
    static SubtitleBoxObservation AcquisitionReference(const SubtitleBoxObservation& current,
        const SubtitleBoxResult& accepted) {
        auto reference=current;
        // Pruning confirms a subset, not ownership of every detector proposal.
        // Future-enriched rows must not borrow ownership from a smaller current mask.
        if(!current.ink || accepted.lineCount<1 || accepted.lineCount>current.text.lineCount ||
            current.text.lineCount>3)return reference;
        for(int i=0;i<accepted.lineCount;++i) {
            const auto& row=accepted.lineBounds[i];bool owned=false;
            for(int j=0;j<current.text.lineCount;++j) {
                const auto& source=current.text.lineBounds[j];
                owned |= row.Valid() && source.Valid() && row.left>=source.left &&
                    row.right<=source.right && row.top>=source.top && row.bottom<=source.bottom;
            }
            if(!owned)return reference;
        }
        reference.text=accepted;
        return reference;
    }
    SubtitleBoxObservation m_refreshedReference;
    unsigned m_referenceSamples=0;
    const char* m_referenceRefresh="not-eligible";
    // A confirmed extension has current fixed-pixel proof and current black-card
    // ownership. Attach it to its existing baseline, or retain a separate row;
    // never turn a multi-row union into a picture-sized extraction rectangle.
    static bool AttachConfirmedExtension(SubtitleBoxResult& result,const SubtitleBoxRect& extension)
    {
        if(!extension.Valid() || result.lineCount<=0 || result.lineCount>3)return false;
        const int height=extension.bottom-extension.top;
        int selected=-1,bestOverlap=0,largestHeight=0;
        bool overlapsOtherRow=false;
        for(int i=0;i<result.lineCount;++i) {
            const auto& line=result.lineBounds[i];if(!line.Valid())continue;
            const int lineHeight=line.bottom-line.top;
            largestHeight=(std::max)(largestHeight,lineHeight);
            const int overlap=(std::min)(line.bottom,extension.bottom)-(std::max)(line.top,extension.top);
            overlapsOtherRow|=overlap>0;
            if(overlap>bestOverlap && overlap*2>=(std::min)(height,lineHeight) &&
                height*2<=lineHeight*3) {selected=i;bestOverlap=overlap;}
        }
        if(selected>=0) {
            auto& line=result.lineBounds[selected];
            line={(std::min)(line.left,extension.left),(std::min)(line.top,extension.top),
                (std::max)(line.right,extension.right),(std::max)(line.bottom,extension.bottom)};
            return true;
        }
        if(overlapsOtherRow || result.lineCount>=3 || largestHeight<=0 ||
            height*2<largestHeight || height*2>largestHeight*3)return false;
        result.lineBounds[result.lineCount++]=extension;
        return true;
    }
    static bool CanAttachConfirmedExtension(SubtitleBoxResult result,const SubtitleBoxRect& extension)
    {
        return AttachConfirmedExtension(result,extension);
    }
    static bool ExtensionHasCurrentBacking(const SubtitleBoxObservation& current,
        const SubtitleBoxRect& extension)
    {
        const int first=(std::max)(extension.top,current.pictureTop);
        const int last=(std::min)(extension.bottom,current.pictureBottom);
        if(first>=last) return true;
        const auto& panel=current.text.capturePanelMeasured?current.text.capturePanel:current.text.sourcePanel;
        return panel.Valid() && extension.left>=panel.left && extension.right<=panel.right &&
            first>=panel.top && last<=panel.bottom;
    }
    bool GatherNewInkExtension(const SubtitleInkSnapshot& current,SubtitleBoxRect& extension) const
    {
        extension={};
        if(!m_inkReference.ink || !m_inkReference.text.detected)return false;
        auto reference=*m_inkReference.ink;
        // Already accepted extensions are part of discovery's reference, while
        // their own fixed masks are independently revalidated every frame.
        for(const auto& learned:m_expansionEvidence) {
            if(!learned.ink || learned.ink->rawInk.size()!=reference.rawInk.size() ||
                learned.ink->sourceRows!=reference.sourceRows)continue;
            for(size_t i=0;i<reference.rawInk.size();++i)reference.rawInk[i]|=learned.ink->rawInk[i];
        }
        for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
            SubtitleBoxRect found;
            if(!SubtitleBoxLookahead::FindNewInkExtension(reference,current,
                m_inkReference.text.lineBounds[line],m_inkReference.width,m_inkReference.height,found) ||
                !found.Valid())continue;
            if(!extension.Valid())extension=found;
            else extension={(std::min)(extension.left,found.left),(std::min)(extension.top,found.top),
                (std::max)(extension.right,found.right),(std::max)(extension.bottom,found.bottom)};
        }
        return extension.Valid();
    }
    bool NoNewInkOutsideReference(const SubtitleBoxObservation& current) const
    {
        if(!m_inkReference.ink || !current.ink || !m_inkReference.text.detected)return false;
        SubtitleBoxRect extension;
        return !GatherNewInkExtension(*current.ink,extension);
    }
    struct ExpansionEvidence {
        std::shared_ptr<const SubtitleInkSnapshot> ink;
        SubtitleBoxRect bounds;
    };
    bool ExtensionInkComponentsMatch(const SubtitleInkSnapshot& expected,
        const SubtitleInkSnapshot& current,const SubtitleBoxRect& bounds) const
    {
        if(expected.width!=current.width || expected.height!=current.height ||
            expected.step<=0 || expected.step!=current.step ||
            expected.sourceRows.size()!=size_t(expected.height) ||
            current.sourceRows.size()!=size_t(current.height))return false;
        const int left=(std::max)(0,bounds.left/expected.step);
        const int right=(std::min)(expected.width,(bounds.right+expected.step-1)/expected.step);
        const auto first=std::lower_bound(expected.sourceRows.begin(),expected.sourceRows.end(),bounds.top);
        const auto last=std::lower_bound(expected.sourceRows.begin(),expected.sourceRows.end(),bounds.bottom);
        const int top=int(first-expected.sourceRows.begin());
        const int bottom=int(last-expected.sourceRows.begin());
        if(left>=right || top>=bottom || bottom>expected.height)return false;
        const int roiWidth=right-left,roiHeight=bottom-top;
        std::vector<uint8_t> visited(size_t(roiWidth)*roiHeight,0);
        std::vector<int> currentRows(size_t(roiHeight),-1),pending;
        for(int y=top;y<bottom;++y) {
            const int sourceY=expected.sourceRows[size_t(y)];
            const auto row=std::lower_bound(current.sourceRows.begin(),current.sourceRows.end(),sourceY);
            if(row!=current.sourceRows.end() && *row==sourceY)
                currentRows[size_t(y-top)]=int(row-current.sourceRows.begin());
        }
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            if(!expected.Get(x,y))continue;
            const size_t start=size_t(y-top)*roiWidth+(x-left);
            if(visited[start])continue;
            visited[start]=1;pending.clear();pending.push_back(int(start));
            unsigned pixels=0,available=0,covered=0;
            for(size_t at=0;at<pending.size();++at) {
                const int index=pending[at],cx=left+index%roiWidth,cy=top+index/roiWidth;
                ++pixels;
                const int currentY=currentRows[size_t(cy-top)];
                if(currentY>=0) {
                    ++available;
                    covered+=current.Get(cx,currentY);
                }
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if(dx==0 && dy==0)continue;
                    const int nx=cx+dx,ny=cy+dy;
                    if(nx<left || nx>=right || ny<top || ny>=bottom ||
                        !expected.Get(nx,ny))continue;
                    const size_t next=size_t(ny-top)*roiWidth+(nx-left);
                    if(!visited[next]) {visited[next]=1;pending.push_back(int(next));}
                }
            }
            // A removed glyph must not hide inside the global line coverage.
            // Tiny isolated fragments are ignored; accepted extension
            // components already contain at least four raw-ink pixels.
            if(pixels>=4 && available>=4 && covered*100<available*85)return false;
        }
        return true;
    }
    bool SameExtensionInk(const std::shared_ptr<const SubtitleInkSnapshot>& reference,
        const std::shared_ptr<const SubtitleInkSnapshot>& current,const SubtitleBoxRect& bounds,
        unsigned minimumCoverageBasisPoints=8500) const
    {
        if(!reference || !current || !bounds.Valid() || reference->width!=current->width ||
            reference->height!=current->height || reference->step!=current->step ||
            reference->step<=0 || reference->sourceRows.size()!=size_t(reference->height) ||
            current->sourceRows.size()!=size_t(current->height)) return false;
        const auto novel=BuildNovelExtensionInk(*current,bounds);
        if(!novel) return false;
        const int left=(std::max)(0,bounds.left/reference->step);
        const int right=(std::min)(reference->width,(bounds.right+reference->step-1)/reference->step);
        unsigned owned=0,covered=0,different=0,total=0;
        for(int y=0;y<reference->height;++y) {
            const int sourceY=reference->sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom) continue;
            const auto rowIt=std::lower_bound(current->sourceRows.begin(),current->sourceRows.end(),sourceY);
            if(rowIt==current->sourceRows.end() || *rowIt!=sourceY) continue;
            const int row=int(rowIt-current->sourceRows.begin());
            for(int x=left;x<right;++x) {
                const bool before=reference->Get(x,y),after=novel->Get(x,row);
                owned+=before;covered+=before&&after;different+=before!=after;total+=before||after;
            }
        }
        return owned>=6 && covered*10000>=owned*minimumCoverageBasisPoints && total>0 && different*100<=total*20 &&
            ExtensionInkComponentsMatch(*reference,*novel,bounds);
    }
    bool ExtensionInkIsCompatibleSubset(const std::shared_ptr<const SubtitleInkSnapshot>& expected,
        const std::shared_ptr<const SubtitleInkSnapshot>& current,const SubtitleBoxRect& bounds) const
    {
        if(!expected || !current || !m_inkReference.ink || !bounds.Valid() ||
            expected->width!=current->width || expected->height!=current->height ||
            expected->step!=current->step || expected->step<=0 ||
            expected->sourceRows.size()!=size_t(expected->height) ||
            current->sourceRows.size()!=size_t(current->height) ||
            m_inkReference.ink->sourceRows.size()!=size_t(expected->height))return false;
        const int left=(std::max)(0,bounds.left/current->step);
        const int right=(std::min)(current->width,(bounds.right+current->step-1)/current->step);
        for(int y=0;y<current->height;++y) {
            const int sourceY=current->sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom)continue;
            const auto baseRow=std::lower_bound(m_inkReference.ink->sourceRows.begin(),
                m_inkReference.ink->sourceRows.end(),sourceY);
            const auto expectedRow=std::lower_bound(expected->sourceRows.begin(),
                expected->sourceRows.end(),sourceY);
            if(baseRow==m_inkReference.ink->sourceRows.end() || *baseRow!=sourceY ||
                expectedRow==expected->sourceRows.end() || *expectedRow!=sourceY)continue;
            const int oldY=int(baseRow-m_inkReference.ink->sourceRows.begin());
            const int knownY=int(expectedRow-expected->sourceRows.begin());
            for(int x=left;x<right;++x) {
                const int sourceX=x*current->step;
                bool inAcquiredLine=false;
                for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
                    const auto& lineBounds=m_inkReference.text.lineBounds[line];
                    if(lineBounds.Valid() && sourceX>=lineBounds.left && sourceX<lineBounds.right &&
                        sourceY>=lineBounds.top && sourceY<lineBounds.bottom) {
                        inAcquiredLine=true;break;
                    }
                }
                if(inAcquiredLine || m_inkReference.ink->Get(x,oldY) || !current->Get(x,y))continue;
                // A one-frame dropout may remove learned pixels, but it may
                // not introduce any glyph strokes outside the accepted mask.
                if(!expected->Get(x,knownY))return false;
            }
        }
        return true;
    }
    bool ExpansionStillVisible(const SubtitleBoxPreview& preview) const
    {
        for(const auto& evidence:m_expansionEvidence) {
            if(SameExtensionInk(evidence.ink,preview.current.ink,evidence.bounds))continue;
            // Permit a one-frame mask dropout when no conflicting replacement
            // strokes are present and the immediately following source frame
            // restores the acquired glyphs. Changed glyphs must take precedence.
            if(preview.current.ink &&
                !ExtensionInkIsCompatibleSubset(evidence.ink,preview.current.ink,evidence.bounds))return false;
            if(!SameExtensionInkInFollowing(preview,evidence))return false;
        }
        return true;
    }
    std::shared_ptr<SubtitleInkSnapshot> BuildNovelExtensionInk(
        const SubtitleInkSnapshot& source,const SubtitleBoxRect& bounds) const
    {
        if(source.width<=0 || source.height<=0 || source.step<=0) return {};
        const size_t words=(size_t(source.width)*source.height+63)/64;
        if(source.rawInk.size()!=words) return {};
        if(!m_inkReference.ink || !bounds.Valid() || source.width!=m_inkReference.ink->width ||
            source.height!=m_inkReference.ink->height || source.step!=m_inkReference.ink->step ||
            source.sourceRows.size()!=size_t(source.height) ||
            m_inkReference.ink->sourceRows.size()!=size_t(source.height)) return {};
        auto novel=std::make_shared<SubtitleInkSnapshot>(source);
        std::fill(novel->rawInk.begin(),novel->rawInk.end(),0);
        novel->ownedInk.assign(words,0);
        const int left=(std::max)(0,bounds.left/source.step);
        const int right=(std::min)(source.width,(bounds.right+source.step-1)/source.step);
        auto insideAcquiredLine=[&](int x,int sourceY) {
            const int sourceX=x*source.step;
            for(int line=0;line<m_inkReference.text.lineCount && line<3;++line) {
                const auto& lineBounds=m_inkReference.text.lineBounds[line];
                if(lineBounds.Valid() && sourceX>=lineBounds.left && sourceX<lineBounds.right &&
                    sourceY>=lineBounds.top && sourceY<lineBounds.bottom)return true;
            }
            return false;
        };
        unsigned count=0;
        for(int y=0;y<source.height;++y) {
            const int sourceY=source.sourceRows[size_t(y)];
            if(sourceY<bounds.top || sourceY>=bounds.bottom) continue;
            const auto oldRow=std::lower_bound(m_inkReference.ink->sourceRows.begin(),
                m_inkReference.ink->sourceRows.end(),sourceY);
            if(oldRow==m_inkReference.ink->sourceRows.end() || *oldRow!=sourceY) continue;
            const int oldY=int(oldRow-m_inkReference.ink->sourceRows.begin());
            for(int x=left;x<right;++x) if(source.Get(x,y) &&
                !m_inkReference.ink->Get(x,oldY) && !insideAcquiredLine(x,sourceY)) {
                const size_t bit=size_t(y)*source.width+x;
                novel->rawInk[bit/64]|=uint64_t{1}<<(bit%64);
                novel->ownedInk[bit/64]|=uint64_t{1}<<(bit%64);++count;
            }
        }
        return count>=6 ? novel : std::shared_ptr<SubtitleInkSnapshot>{};
    }
    bool SameExtensionInkInFollowing(const SubtitleBoxPreview& preview,
        const ExpansionEvidence& evidence) const
    {
        if(!preview.futureAvailable || !preview.followingCount) return false;
        const auto& next=preview.following[0];
        return next.analyzed && !next.discontinuity && !next.text.workLimit && next.ink &&
            next.identity.acceptedSequence==preview.current.identity.acceptedSequence+1 &&
            SubtitleBoxLookahead::SameContext(m_reference,next) &&
            SameExtensionInk(evidence.ink,next.ink,evidence.bounds);
    }
    void CaptureExtensionInk(const SubtitleBoxObservation& source,const SubtitleBoxRect& bounds)
    {
        if(!source.ink)return;
        auto captured=BuildNovelExtensionInk(*source.ink,bounds);
        if(!captured)return;
        for(auto& evidence:m_expansionEvidence) {
            if(!evidence.ink || evidence.ink->width!=captured->width ||
                evidence.ink->height!=captured->height || evidence.ink->step!=captured->step ||
                evidence.ink->sourceRows!=captured->sourceRows ||
                evidence.ink->rawInk.size()!=captured->rawInk.size() ||
                evidence.ink->ownedInk.size()!=captured->ownedInk.size())continue;
            for(size_t i=0;i<captured->rawInk.size();++i) {
                captured->rawInk[i]|=evidence.ink->rawInk[i];
                captured->ownedInk[i]|=evidence.ink->ownedInk[i];
            }
            const SubtitleBoxRect accumulatedBounds{
                (std::min)(evidence.bounds.left,bounds.left),
                (std::min)(evidence.bounds.top,bounds.top),
                (std::max)(evidence.bounds.right,bounds.right),
                (std::max)(evidence.bounds.bottom,bounds.bottom)};
            evidence={std::move(captured),accumulatedBounds};
            return;
        }
        m_expansionEvidence.push_back({std::move(captured),bounds});
    }
    bool FindConfirmedInkExtension(const SubtitleBoxPreview& preview,
        const SubtitleBoxRect& now,SubtitleBoxRect& extension) const
    {
        extension={};
        if(!m_inkReference.ink || !preview.futureAvailable || preview.followingCount==0 ||
            !preview.current.ink || !now.Valid()) return false;
        const auto& next=preview.following[0];
        if(!next.analyzed || next.discontinuity || next.text.workLimit || !next.ink ||
            next.identity.acceptedSequence!=preview.current.identity.acceptedSequence+1 ||
            !SubtitleBoxLookahead::SameContext(m_reference,next) ||
            !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,next)) return false;
        SubtitleBoxRect later;
        if(!GatherNewInkExtension(*next.ink,later)) return false;
        const int tolerance=(std::max)(4,preview.current.height/180);
        if(std::abs(now.left-later.left)>tolerance || std::abs(now.top-later.top)>tolerance ||
            std::abs(now.right-later.right)>tolerance || std::abs(now.bottom-later.bottom)>tolerance)
            return false;
        auto candidate=BuildNovelExtensionInk(*preview.current.ink,now);
        if(!candidate || !SameExtensionInk(candidate,next.ink,now)) return false;
        // A new vertical row can be a bright scene edge above or below a
        // correctly measured subtitle. Require the entire configured
        // lookahead horizon and fixed-pixel agreement before letting it grow
        // the presentation envelope. Horizontal word extensions retain the
        // two-frame fast path because they stay on an acquired baseline.
        const bool addsRow=now.top<m_inkReference.text.bounds.top ||
            now.bottom>m_inkReference.text.bounds.bottom;
        if(addsRow) {
            if(preview.followingCount<3)return false;
            SubtitleBoxRect expected=now;
            for(unsigned i=0;i<3;++i) {
                const auto& observed=preview.following[i];
                if(!observed.analyzed || observed.discontinuity || observed.text.workLimit || !observed.ink ||
                    observed.identity.acceptedSequence!=preview.current.identity.acceptedSequence+i+1 ||
                    !SubtitleBoxLookahead::SameContext(m_reference,observed))return false;
                for(int line=0;line<m_inkReference.text.lineCount && line<3;++line)
                    if(!SubtitleBoxLookahead::OwnedLinePixelsVisible(m_inkReference,line,observed))return false;
                SubtitleBoxRect laterRow;
                if(!GatherNewInkExtension(*observed.ink,laterRow) ||
                    std::abs(expected.left-laterRow.left)>2 || std::abs(expected.top-laterRow.top)>2 ||
                    std::abs(expected.right-laterRow.right)>2 || std::abs(expected.bottom-laterRow.bottom)>2 ||
                    !SameExtensionInk(candidate,observed.ink,now,9750))return false;
            }
        }
        extension=now;
        return true;
    }
    bool CanBridge(const SubtitleBoxPreview& preview) const
    {
        if (!m_hasPrevious || !m_result.detected || m_bridgedFrames>=2 ||
            !preview.futureAvailable || preview.current.discontinuity ||
            !ExpansionStillVisible(preview) ||
            (m_expansionEvidence.empty() && !NoNewInkOutsideReference(preview.current)) ||
            m_previous.policyGeneration!=preview.policyGeneration ||
            m_previous.continuityGeneration!=preview.continuityGeneration ||
            preview.current.identity.acceptedSequence!=m_previous.current.identity.acceptedSequence+1 ||
            m_inkReference.text.lineCount!=m_result.lineCount ||
            !SubtitleBoxLookahead::SameContext(m_reference,preview.current) ||
            !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,preview.current)) return false;
        const auto* prior=&preview.current;
        const unsigned available=(std::min)(preview.followingCount,2-m_bridgedFrames);
        for (unsigned i=0;i<available;++i)
        {
            const auto& next=preview.following[i];
            if (!next.analyzed || next.discontinuity || next.text.workLimit ||
                next.identity.acceptedSequence!=prior->identity.acceptedSequence+1 ||
                !SubtitleBoxLookahead::SameContext(m_reference,next) ||
                !SubtitleBoxLookahead::SameInkAtReference(m_inkReference,next) ||
                !ExpansionInkMatches(next.ink)) return false;
            if (next.text.lineCount==m_result.lineCount &&
                SubtitleBoxLookahead::SameCue(m_reference,next) &&
                SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,next.text)) return true;
            prior=&next;
        }
        return false;
    }
    bool ExpansionInkMatches(const std::shared_ptr<const SubtitleInkSnapshot>& current) const
    {
        for(const auto& evidence:m_expansionEvidence)
            if(!SameExtensionInk(evidence.ink,current,evidence.bounds))return false;
        return true;
    }
    SubtitleBoxPreview m_previous;
    SubtitleBoxObservation m_reference, m_inkReference;
    std::vector<ExpansionEvidence> m_expansionEvidence;
    unsigned m_bridgedFrames = 0;
    double m_diagnosticWeakMs=0;
    SubtitleBoxLookahead::InkMatchDiagnostics m_inkDiagnostic;
    const char* m_decisionReason = "reset";
    double m_weakTemplateMilliseconds = 0.0;
    bool m_weakTemplateExhausted = false;
    SubtitleBoxResult m_result;
    SubtitleBarTrackingReference m_barTrackingReference;
    uint64_t m_nextCue = 0;
    bool m_hasPrevious = false;
};

// Transition-only diagnostics. Suspected flashes are evidence, not an assertion
// that the source subtitle never disappeared. No retained pixels are rendered.
class SubtitleStabilityTelemetry {
public:
    struct Event {
        bool changed=false, detectorLost=false, detectorRegained=false;
        bool outputLost=false, outputRegained=false, suspectedFlash=false;
        bool holdStarted=false, holdEnded=false, cueChanged=false;
        double gapMs=0;
        uint64_t losses=0, flashes=0;
    };
    void Reset() { *this=SubtitleStabilityTelemetry{}; }
    Event Observe(const SubtitleBoxObservation& current,const SubtitleBoxResult& result,
        bool visible,double frameMs) {
        Event event;
        if(m_hasPrevious && SameActivePictureFrameIdentity(m_previous.identity,current.identity))return event;
        if(m_hasPrevious && (current.discontinuity ||
            current.identity.acceptedSequence!=m_previous.identity.acceptedSequence+1 ||
            m_previous.identity.transportGeneration!=current.identity.transportGeneration ||
            m_previous.identity.sourceFormatGeneration!=current.identity.sourceFormatGeneration ||
            m_previous.identity.viewportGeneration!=current.identity.viewportGeneration ||
            m_previous.identity.rendererGeneration!=current.identity.rendererGeneration ||
            m_previous.policyGeneration!=current.policyGeneration ||
            m_previous.continuityGeneration!=current.continuityGeneration)) Reset();
        frameMs=std::isfinite(frameMs) && frameMs>0 ? frameMs : 1000.0/60.0;
        const bool observed=current.text.detected;
        if(m_hasPrevious) {
            event.detectorLost=m_observed && !observed;
            event.detectorRegained=!m_observed && observed;
            event.outputLost=m_visible && !visible;
            event.outputRegained=!m_visible && visible;
            event.holdStarted=!m_held && result.held;
            event.holdEnded=m_held && !result.held;
            event.cueChanged=m_visible && visible && m_cue!=result.cue;
        }
        if(!visible && m_lastVisible.text.detected)m_gapMs+=frameMs;
        if(event.outputLost)++m_losses;
        if(event.outputRegained) {
            auto accepted=current;accepted.text=result;
            event.gapMs=m_gapMs;
            event.suspectedFlash=m_gapMs>0 && m_gapMs<=500.0 &&
                SubtitleBoxLookahead::SameCue(m_lastVisible,accepted);
            if(event.suspectedFlash)++m_flashes;
        }
        if(visible) {
            m_lastVisible=current;m_lastVisible.text=result;m_lastVisible.ink.reset();m_gapMs=0;
        } else if(m_gapMs>500.0) m_lastVisible={};
        event.changed=(!m_hasPrevious && (observed || visible)) || event.detectorLost || event.detectorRegained ||
            event.outputLost || event.outputRegained || event.holdStarted || event.holdEnded || event.cueChanged ||
            m_previous.pendingRefresh!=current.pendingRefresh;
        event.losses=m_losses;event.flashes=m_flashes;
        m_previous=current;m_previous.ink.reset();m_hasPrevious=true;
        m_observed=observed;m_visible=visible;m_held=result.held;m_cue=result.cue;
        return event;
    }
private:
    SubtitleBoxObservation m_previous,m_lastVisible;
    bool m_hasPrevious=false,m_observed=false,m_visible=false,m_held=false;
    uint64_t m_cue=0,m_losses=0,m_flashes=0;
    double m_gapMs=0;
};
