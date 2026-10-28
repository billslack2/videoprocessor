#pragma once
#include <SubtitleBoxLookahead.h>
#include <SubtitleCueContinuationProof.h>

// Fresh native verification of fixed-source glyphs after a picture edge becomes
// unavailable. Detector misses have a non-renewing lease; healthy current positives
// must pass each new source. Worker measurements and picture authority stay unchanged.
class SubtitleCueContinuationGuard {
public:
    void Reset() { m_valid=false; m_reference={}; m_baseline.reset(); m_cached={}; m_elapsed=0;m_missingStarted=false;m_localGlyphsOnly=false; }
    bool HasReference() const { return m_valid; }
    // Keep the last seed result through Reset so cursor cleanup cannot erase it.
    const char* LastSeedReason() const { return m_lastSeedReason; }
    bool NeedsCurrentProof(const SubtitleBoxPreview& preview) const {
        if(!m_valid || !preview.available)return false;
        const auto& c=preview.current;
        return EdgeLossKind(c)!=0 || LocalCurrentBarEvidence(c,preview.policyGeneration,
            preview.continuityGeneration,m_base);
    }
    bool Remember(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& base,const AnalysisLumaSource& source) {
        const auto& c=preview.current;
        const auto reject=[&](const char* reason) {m_lastSeedReason=reason;return false;};
        if(!preview.available || !c.analyzed || c.glyphLocalTop || c.glyphLocalBottom || c.discontinuity || c.pendingRefresh || c.analysisRefresh)
            return reject("observation-not-current");
        if(!c.text.detected || c.text.held || c.text.confirmationPending || c.text.workLimit || !accepted.detected || accepted.held || accepted.confirmationPending ||
            accepted.workLimit || accepted.lineCount<1 || accepted.lineCount>3)
            return reject("cue-not-strong");
        if(!c.ink || !c.ink->paletteValid || !c.ink->rawEvidenceComplete)
            return reject("ink-unavailable");
        if(!ValidSource(c,source))return reject("source-mismatch");
        const bool sharedCurrent=c.sharedPicture.AvailableFor(c.identity,c.width,c.height);
        const bool localSeed=!sharedCurrent &&
            LocalCurrentBarEvidence(c,preview.policyGeneration,preview.continuityGeneration,base);
        if(!sharedCurrent && !localSeed)return reject("shared-hint-unavailable");
        if(sharedCurrent && !SameBase(base,c.sharedPicture.bounds))return reject("native-base-shared-hint-mismatch");
        if(localSeed && accepted.observations<2)return reject("local-seed-not-established");
        const bool physicalEdgesAligned=base.top==c.pictureTop && base.bottom==c.pictureBottom;
        // Different sampler/native edges may nominate fixed bar glyphs only.
        // This does not reconcile, sample or certify the intervening picture strips.
        if(!physicalEdgesAligned && !BarOnlyCue(c,accepted,base))
            return reject("native-base-physical-edge-mismatch");
        // An unavailable shared snapshot may seed only an already established
        // current cue that is wholly in one physical bar. Its geometry remains
        // glyph-local and cannot be promoted into picture or backing authority.
        if(localSeed && !BarOnlyCue(c,accepted,base))return reject("local-seed-not-fixed-bar-cue");
        if(c.pictureTop<=0 || c.pictureBottom>=c.height)
            return reject("seed-physical-context-incomplete");
        if(preview.policyGeneration!=c.policyGeneration || preview.continuityGeneration!=c.continuityGeneration)
            return reject("measurement-epoch-mismatch");
        // Rendering the same strong source frame does not renew a later lease.
        if(m_valid && Context(c,m_reference) && c.identity.acceptedSequence<=m_last.acceptedSequence) {
            if(!SameAnalysisFrameIdentity(c.identity,m_reference.identity) || !SameBase(base,m_base))
                return reject("nonmonotonic-reference");
            m_lastSeedReason="same-source-reference";return true;
        }
        auto acceptedObservation=c;acceptedObservation.text=accepted;
        if(!SubtitleBoxLookahead::SameCue(c,acceptedObservation))return reject("accepted-cue-mismatch");
        SubtitleBoxRect roi{};
        auto merge=[&](const SubtitleBoxRect& b) {
            if(!roi.Valid())roi=b;
            else {roi.left=(std::min)(roi.left,b.left);roi.top=(std::min)(roi.top,b.top);
                roi.right=(std::max)(roi.right,b.right);roi.bottom=(std::max)(roi.bottom,b.bottom);}
        };
        for(int i=0;i<accepted.lineCount;++i) {
            const auto& b=accepted.lineBounds[i];if(!InRaster(b,c.width,c.height))return reject("invalid-line-bounds");
            const int h=b.bottom-b.top;
            merge({(std::max)(0,b.left-3*h),(std::max)(0,b.top-2*h),
                (std::min)(c.width,b.right+3*h),(std::min)(c.height,b.bottom+2*h)});
        }
        if(accepted.sourcePanel.Valid()) {
            if(!InRaster(accepted.sourcePanel,c.width,c.height))return reject("invalid-source-panel");
            merge(accepted.sourcePanel);
        }
        unsigned samples=0;const char* reason="not-eligible";
        auto sampled=Sample(*c.ink,roi,source,65536,samples,reason);
        if(!sampled)return reject(reason);
        // Acquisition grouping supplies ownership; pixels are freshly sampled.
        const size_t words=sampled->rawInk.size();
        if(c.ink->ownedInk.size()!=words)return reject("owned-grid-mismatch");
        for(size_t i=0;i<words;++i)sampled->ownedInk[i]=c.ink->ownedInk[i]&sampled->rawInk[i];
        m_reference=acceptedObservation;m_reference.ink=sampled;
        m_baseline=sampled;m_base=base;m_roi=roi;m_last=c.identity;m_elapsed=0;m_missingStarted=false;m_valid=true;m_cached={};
        m_localGlyphsOnly=localSeed || !physicalEdgesAligned;
        m_format=source.format;m_encoding=source.encoding;m_range=source.range;m_colorspace=source.colorspace;
        m_sourceGeneration=source.generation;
        m_lastSeedReason=localSeed?"seeded-local-unshared-bar-glyphs":
            m_localGlyphsOnly?"seeded-local-bar-glyphs":"seeded";
        return true;
    }
    SubtitleCueContinuation Prepare(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,int holdMs,double frameMs,size_t maxSamples=65536) {
        SubtitleCueContinuation out;const auto& c=preview.current;
        out.identity=c.identity;out.templateIdentity=m_reference.identity;out.previousIdentity=m_last;
        out.policyGeneration=preview.policyGeneration;out.continuityGeneration=preview.continuityGeneration;
        // Diagnostic coordinates are retained on rejection too; valid still gates authority.
        out.physicalBase=m_base;out.pictureTop=m_reference.pictureTop;out.pictureBottom=m_reference.pictureBottom;
        auto fail=[&](const char* why) {out.reason=why;m_valid=false;m_cached=out;return out;};
        if(!m_valid)return out;
        const bool sharedCurrent=c.sharedPicture.AvailableFor(c.identity,c.width,c.height);
        const bool localCurrent=!sharedCurrent &&
            ((LocalCurrentBarEvidence(c,preview.policyGeneration,preview.continuityGeneration,m_base) &&
                CueSideMatchesReference(c,m_reference.text,m_reference,m_base)) ||
             LocalCandidateCueSideEvidence(c,preview.policyGeneration,preview.continuityGeneration,
                m_base,m_reference.text,m_reference));
        if(!preview.available || !c.analyzed || c.glyphLocalTop || c.glyphLocalBottom || c.discontinuity || c.pendingRefresh || c.analysisRefresh ||
            c.text.workLimit || !ValidSource(c,source) || !SameBase(base,m_base) ||
            (!sharedCurrent && !localCurrent) || (sharedCurrent && !SameBase(base,c.sharedPicture.bounds)) ||
            !Context(c,m_reference) || preview.policyGeneration!=m_reference.policyGeneration ||
            preview.continuityGeneration!=m_reference.continuityGeneration ||
            c.policyGeneration!=preview.policyGeneration || c.continuityGeneration!=preview.continuityGeneration ||
            source.format!=m_format || source.encoding!=m_encoding || source.range!=m_range ||
            source.colorspace!=m_colorspace || source.generation!=m_sourceGeneration)
            return fail("context-changed");
        const int edgeLoss=EdgeLossKind(c);
        if((!edgeLoss && !localCurrent) || !SubtitleBoxLookahead::HasBarEvidence(c))
            return fail("not-missing-cue-edge");
        if(edgeLoss==2 && (c.cadenceSkipped || c.text.held || c.text.confirmationPending || c.text.revised ||
            !c.ink || !c.ink->rawEvidenceComplete || !c.ink->paletteValid))
            return fail("current-detector-proof-not-fresh");
        if(c.text.detected) {
            // The opposite edge can be unavailable while this bar still yields
            // a positive grouping. Grouping alone cannot retain or replace text.
            auto comparison=c;comparison.pictureTop=m_reference.pictureTop;
            comparison.pictureBottom=m_reference.pictureBottom;
            if(c.text.lineCount!=m_reference.text.lineCount ||
                !SubtitleBoxLookahead::SameCue(m_reference,comparison))
                return fail("current-cue-grouping-changed");
        }
        if(SameAnalysisFrameIdentity(c.identity,m_last))return m_cached;
        if(c.identity.acceptedSequence!=m_last.acceptedSequence+1)return fail("sequence-gap");
        if(edgeLoss==2 && ((c.identity.sourceFrameNumber && m_last.sourceFrameNumber &&
            c.identity.sourceFrameNumber<=m_last.sourceFrameNumber) ||
            (c.identity.captureTimestamp && m_last.captureTimestamp && c.identity.captureTimestamp<=m_last.captureTimestamp)))
            return fail("source-not-fresh");
        if(!std::isfinite(frameMs) || frameMs<=0)return fail("invalid-source-duration");
        m_missingStarted=true;m_elapsed+=frameMs;out.elapsedMs=m_elapsed;m_last=c.identity;
        // A detector miss spends the absolute non-renewing grace. An exact
        // healthy positive on the cue side is freshly verified below each frame;
        // it does not reset that grace or renew the acquisition/physical baseline.
        // Only an actual detector miss spends the finite grace. A positive
        // local cue is re-proved from this frame's glyph and cue-side pixels;
        // that proof does not renew or acquire any picture/backing authority.
        if(edgeLoss==1 && m_elapsed>(std::max)(0,(std::min)(250,holdMs)))
            return fail("lease-expired");
        auto fresh=Verify(c,source,maxSamples,out.samples,out.reason,
            edgeLoss==2 || localCurrent,&out);
        if(!fresh)return fail(out.reason);
        out.valid=true;out.localGlyphsOnly=m_localGlyphsOnly || localCurrent || edgeLoss==2;
        out.reason=localCurrent?"current-local-cue-pixels":
            edgeLoss==2?"current-cue-opposite-edge-pixels":"current-cue-pixels";
        out.physicalBase=m_base;out.sampledBounds=m_roi;
        out.backingVerifiedBounds=out.localGlyphsOnly?SubtitleBoxRect{}:m_reference.text.sourcePanel;
        out.pictureTop=m_reference.pictureTop;out.pictureBottom=m_reference.pictureBottom;
        out.accepted=m_reference.text;out.accepted.held=true;out.ink=fresh;m_cached=out;return out;
    }
    // Advances only the source cursor after fresh verification. It neither
    // replaces acquisition evidence nor renews its finite lease once an edge is missing.
    bool AdvanceVerified(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,int holdMs,double frameMs,size_t maxSamples=65536) {
        const auto& c=preview.current;
        if(accepted.confirmationPending){Reset();return false;}
        if(!m_valid)return false;
        SubtitleCueContinuation ownership;ownership.valid=true;ownership.accepted=m_reference.text;
        if(!ownership.MatchesCue(accepted)) {Reset();return false;}
        const bool sharedCurrent=c.sharedPicture.AvailableFor(c.identity,c.width,c.height);
        const bool localCurrent=!sharedCurrent &&
            ((LocalCurrentBarEvidence(c,preview.policyGeneration,preview.continuityGeneration,m_base) &&
                CueSideMatchesReference(c,m_reference.text,m_reference,m_base)) ||
             LocalCandidateCueSideEvidence(c,preview.policyGeneration,preview.continuityGeneration,
                m_base,m_reference.text,m_reference));
        if(!preview.available || !c.analyzed || c.glyphLocalTop || c.glyphLocalBottom || c.discontinuity || c.pendingRefresh || c.text.workLimit ||
            !ValidSource(c,source) || !SameBase(base,m_base) || !Context(c,m_reference) ||
            (!sharedCurrent && !localCurrent) || (sharedCurrent && !SameBase(base,c.sharedPicture.bounds)) ||
            preview.policyGeneration!=m_reference.policyGeneration || preview.continuityGeneration!=m_reference.continuityGeneration ||
            c.policyGeneration!=preview.policyGeneration || c.continuityGeneration!=preview.continuityGeneration ||
            source.format!=m_format || source.encoding!=m_encoding || source.range!=m_range ||
            source.colorspace!=m_colorspace || source.generation!=m_sourceGeneration ||
            !SubtitleBoxLookahead::SameContext(m_reference,c) ||
            (c.text.detected && !SubtitleBoxLookahead::SameCue(m_reference,c))) {Reset();return false;}
        if(SameAnalysisFrameIdentity(c.identity,m_last))return true;
        if(c.identity.acceptedSequence!=m_last.acceptedSequence+1 || !std::isfinite(frameMs) || frameMs<=0) {Reset();return false;}
        if(m_missingStarted)m_elapsed+=frameMs;
        if(m_elapsed>(std::max)(0,(std::min)(250,holdMs))) {Reset();return false;}
        unsigned samples=0;const char* reason="not-eligible";
        if(!Verify(c,source,maxSamples,samples,reason,localCurrent)) {Reset();return false;}
        if(localCurrent)m_localGlyphsOnly=true;
        m_last=c.identity;m_cached={};return true;
    }
private:
    static bool SameCandidateBase(const SubtitleInspectionCandidate& candidate,
        const ActivePictureBounds& base,const SubtitleBoxObservation& c,
        uint64_t policy,uint64_t continuity) {
        const auto& b=candidate.bounds;
        return c.inspectionCandidateUsed && candidate.AvailableFor(c.identity,c.width,c.height,policy,continuity) &&
            b.left==base.left && b.top==base.top && b.right==base.right && b.bottom==base.bottom &&
            b.rasterWidth==base.rasterWidth && b.rasterHeight==base.rasterHeight;
    }
    static bool FreshEdge(const SubtitleBarEvidence& e,bool bottom) {
        if(bottom) return e.bottom>0 && !e.captionBackedBottom &&
            ((e.bottomRevalidatedAtReference || e.bottomRevalidatedNearby) ?
                e.bottomReferenceBoundarySupport>0 : e.bottomBoundarySupport>0);
        return e.top>0 && !e.captionBackedTop &&
            ((e.topRevalidatedAtReference || e.topRevalidatedNearby) ?
                e.topReferenceBoundarySupport>0 : e.topBoundarySupport>0);
    }
    static bool LocalCurrentBarEvidence(const SubtitleBoxObservation& c,uint64_t policy,
        uint64_t continuity,const ActivePictureBounds& base) {
        if(!c.sharedPicture.required || !c.pictureAuthorityUnavailableForMeasurement ||
            !c.barAuthority || !c.barEvidence.available || c.barTrackingAuthority ||
            c.hasBarTrackingReference || c.currentAnchorUsesTrackedEdge ||
            c.analysisRefresh || c.pendingRefresh || c.discontinuity ||
            c.glyphLocalTop || c.glyphLocalBottom || c.barEvidence.captionBackedTop ||
            c.barEvidence.captionBackedBottom || c.pictureTop<=0 ||
            c.pictureBottom>=c.height || c.pictureTop>=c.pictureBottom ||
            c.pictureTop!=c.barEvidence.top || c.pictureBottom!=c.barEvidence.bottom ||
            c.barEvidence.top<=0 || c.barEvidence.bottom>=c.height ||
            c.barEvidence.top>=c.barEvidence.bottom || base.rasterWidth!=c.width ||
            base.rasterHeight!=c.height || base.left!=0 || base.right!=c.width ||
            base.top<=0 || base.bottom>=c.height || base.top>=base.bottom)
            return false;
        const bool untrackedIndependent=c.independentBarAnalysis &&
            !c.barEvidence.topRevalidatedAtReference && !c.barEvidence.topRevalidatedNearby &&
            !c.barEvidence.bottomRevalidatedAtReference && !c.barEvidence.bottomRevalidatedNearby;
        const bool exactCandidate=c.inspectionCandidateUsed &&
            SameCandidateBase(c.sharedPicture.inspectionCandidate,base,c,policy,continuity);
        if(untrackedIndependent)return true;
        if(!exactCandidate)return false;
        const auto& e=c.barEvidence;
        return FreshEdge(e,false) && FreshEdge(e,true);
    }
    static bool CueSideMatchesReference(const SubtitleBoxObservation& c,
        const SubtitleBoxResult& text,const SubtitleBoxObservation& reference,
        const ActivePictureBounds& base) {
        if(!BarOnlyCue(reference,text,base))return false;
        bool top=true,bottom=true;
        for(int i=0;i<text.lineCount;++i) {
            const auto& line=text.lineBounds[i];
            top &= line.bottom<=(std::min)(base.top,reference.pictureTop);
            bottom &= line.top>=(std::max)(base.bottom,reference.pictureBottom);
        }
        if(top==bottom)return false;
        return bottom ? c.pictureBottom==reference.pictureBottom :
            c.pictureTop==reference.pictureTop;
    }
    // A fresh inspection candidate can retain only the cue-facing physical
    // edge when its opposite edge is absent. The line must remain beyond both
    // that current plane and the prior base; the missing side stays unknown.
    static bool LocalCandidateCueSideEvidence(const SubtitleBoxObservation& c,uint64_t policy,
        uint64_t continuity,const ActivePictureBounds& base,const SubtitleBoxResult& text,
        const SubtitleBoxObservation& reference) {
        if(!c.sharedPicture.required || !c.pictureAuthorityUnavailableForMeasurement ||
            !c.barAuthority || !c.barEvidence.available || c.barTrackingAuthority ||
            c.hasBarTrackingReference || c.currentAnchorUsesTrackedEdge ||
            c.analysisRefresh || c.pendingRefresh || c.discontinuity || c.glyphLocalTop ||
            c.glyphLocalBottom || base.rasterWidth!=c.width || base.rasterHeight!=c.height ||
            base.left!=0 || base.right!=c.width || base.top<=0 || base.bottom>=c.height ||
            base.top>=base.bottom || !SameCandidateBase(c.sharedPicture.inspectionCandidate,
                base,c,policy,continuity) || !text.detected || text.held ||
            text.confirmationPending || text.workLimit || text.lineCount<1 || text.lineCount>3)
            return false;
        bool bottom=true,top=true;
        for(int i=0;i<text.lineCount;++i) {
            const auto& line=text.lineBounds[i];
            if(!InRaster(line,c.width,c.height) || text.linePanels[i].Valid() ||
                text.lineCapturePanels[i].Valid())return false;
            bottom &= c.pictureBottom>0 && c.pictureBottom<c.height &&
                c.pictureBottom==c.barEvidence.bottom &&
                line.top>=(std::max)(base.bottom,c.pictureBottom);
            top &= c.pictureTop>0 && c.pictureTop<c.height &&
                c.pictureTop==c.barEvidence.top &&
                line.bottom<=(std::min)(base.top,c.pictureTop);
        }
        return bottom!=top && FreshEdge(c.barEvidence,bottom) &&
            CueSideMatchesReference(c,text,reference,base);
    }
    static bool BarOnlyCue(const SubtitleBoxObservation& c,const SubtitleBoxResult& accepted,
        const ActivePictureBounds& base) {
        if(c.pictureTop<=0 || c.pictureBottom>=c.height || c.pictureTop>=c.pictureBottom ||
            base.rasterWidth!=c.width || base.rasterHeight!=c.height || base.left!=0 || base.right!=c.width ||
            base.top<=0 || base.bottom>=c.height || base.top>=base.bottom ||
            accepted.sourcePanel.Valid() || accepted.capturePanel.Valid() || accepted.provenSourcePanel.Valid() ||
            accepted.lineCount<1 || accepted.lineCount>3)return false;
        bool top=true,bottom=true;
        for(int i=0;i<accepted.lineCount;++i) {
            const auto& line=accepted.lineBounds[i];
            if(!InRaster(line,c.width,c.height) || accepted.linePanels[i].Valid() ||
                accepted.lineCapturePanels[i].Valid())return false;
            top &= line.bottom<=(std::min)(base.top,c.pictureTop);
            bottom &= line.top>=(std::max)(base.bottom,c.pictureBottom);
        }
        return top!=bottom;
    }
    int EdgeLossKind(const SubtitleBoxObservation& c) const {
        bool topCue=false,bottomCue=false;
        for(int i=0;i<m_reference.text.lineCount;++i) {
            topCue|=m_reference.text.lineBounds[i].top<m_reference.pictureTop;
            bottomCue|=m_reference.text.lineBounds[i].bottom>m_reference.pictureBottom;
        }
        if(!c.text.detected && ((bottomCue && !topCue && m_reference.pictureBottom<c.height &&
            c.pictureBottom==c.height && c.pictureTop==m_reference.pictureTop && c.pictureTop>0) ||
            (topCue && !bottomCue && m_reference.pictureTop>0 && c.pictureTop==0 &&
             c.pictureBottom==m_reference.pictureBottom && c.pictureBottom<c.height)))return 1;
        if(c.text.detected && BarOnlyCue(m_reference,m_reference.text,m_base) &&
            ((bottomCue && !topCue && c.pictureTop==0 && c.pictureBottom==m_reference.pictureBottom) ||
             (topCue && !bottomCue && c.pictureBottom==c.height && c.pictureTop==m_reference.pictureTop)))return 2;
        return 0;
    }
    std::shared_ptr<SubtitleInkSnapshot> Verify(const SubtitleBoxObservation& c,const AnalysisLumaSource& source,
        size_t maxSamples,unsigned& samples,const char*& reason,bool currentPositiveBarOnly=false,
        SubtitleCueContinuation* diagnostic=nullptr) const {
        auto fresh=Sample(*m_baseline,m_roi,source,maxSamples,samples,reason);
        if(!fresh)return {};
        // The wide sampling ROI can enter a moving picture. Only the healthy
        // current-positive bar-only route can exclude that picture interior
        // from the added-caption-ink veto. Owned glyphs and their adjacent black
        // backing are still compared below, and every new bar sample still vetoes.
        const bool pictureIsNotCaption=currentPositiveBarOnly &&
            (BarOnlyCue(m_reference,m_reference.text,m_base) ||
             LocalCandidateCueSideEvidence(c,m_reference.policyGeneration,
                m_reference.continuityGeneration,m_base,m_reference.text,m_reference));
        const auto mismatch=[&](int x,int y,int sx,int sy) {
            if(!diagnostic)return;
            diagnostic->mismatchX=sx;diagnostic->mismatchY=sy;
            diagnostic->mismatchBandMask=
                (sy>=m_reference.pictureTop && sy<m_reference.pictureBottom?1u:0u) |
                (sy>=c.pictureTop && sy<c.pictureBottom?2u:0u) |
                (sx>=m_base.left && sx<m_base.right && sy>=m_base.top && sy<m_base.bottom?4u:0u);
            diagnostic->mismatchPixelMask=(m_baseline->Get(x,y)?1u:0u)|(fresh->Get(x,y)?2u:0u)|
                (m_baseline->Black(x,y)?4u:0u)|(fresh->Black(x,y)?8u:0u);
        };
        for(int y=0;y<fresh->height;++y) {
            const int sy=fresh->sourceRows[size_t(y)];if(sy<m_roi.top || sy>=m_roi.bottom)continue;
            for(int x=(m_roi.left+fresh->step-1)/fresh->step;x<fresh->width && x*fresh->step<m_roi.right;++x) {
                const int sx=x*fresh->step;bool line=false;
                for(int i=0;i<m_reference.text.lineCount;++i) {
                    const auto& b=m_reference.text.lineBounds[i];
                    line|=sx>=b.left && sx<b.right && sy>=b.top && sy<b.bottom;
                }
                const bool acquiredPicture=sy>=m_reference.pictureTop && sy<m_reference.pictureBottom;
                if(!line && fresh->Get(x,y) && !m_baseline->Get(x,y) &&
                    !(pictureIsNotCaption && acquiredPicture)) {
                    mismatch(x,y,sx,sy);reason="new-ink-outside-cue";return {};
                }
                const auto& panel=m_reference.text.sourcePanel;
                if(panel.Valid() && sx>=panel.left && sx<panel.right && sy>=panel.top && sy<panel.bottom &&
                    m_baseline->Black(x,y) && !fresh->Black(x,y)){mismatch(x,y,sx,sy);reason="backing-changed";return {};}
            }
        }
        // Geometry is local to comparison: raw measurement and AR stay untouched.
        auto comparison=c;comparison.pictureTop=m_reference.pictureTop;comparison.pictureBottom=m_reference.pictureBottom;
        comparison.ink=fresh;
        if((m_localGlyphsOnly || c.text.detected) &&
            !SubtitleBoxLookahead::StableBlackBacking(m_reference,comparison)) {
            reason="bar-backing-changed";return {};
        }
        if(!SubtitleBoxLookahead::ConfidentSameInk(m_reference,comparison)){reason="glyph-proof-failed";return {};}
        for(size_t i=0;i<fresh->rawInk.size();++i)fresh->ownedInk[i]=m_baseline->ownedInk[i]&fresh->rawInk[i];
        return fresh;
    }
    static bool SameBase(const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight;
    }
    static bool InRaster(const SubtitleBoxRect& r,int w,int h) {
        return r.Valid() && r.left>=0 && r.top>=0 && r.right<=w && r.bottom<=h;
    }
    static bool ValidSource(const SubtitleBoxObservation& c,const AnalysisLumaSource& s) {
        return s.IsValid() && s.width==c.width && s.height==c.height &&
            s.generation==c.identity.transportGeneration;
    }
    static bool Context(const SubtitleBoxObservation& a,const SubtitleBoxObservation& b) {
        auto x=a.identity;auto y=b.identity;
        return x.transportGeneration==y.transportGeneration && x.sourceFormatGeneration==y.sourceFormatGeneration &&
            x.analysisDomainKey==y.analysisDomainKey && x.viewportGeneration==y.viewportGeneration &&
            x.rendererGeneration==y.rendererGeneration && a.width==b.width && a.height==b.height &&
            a.scriptHint==b.scriptHint && a.optimizationMode==b.optimizationMode && a.nearBarDistance==b.nearBarDistance;
    }
    static std::shared_ptr<SubtitleInkSnapshot> Sample(const SubtitleInkSnapshot& p,const SubtitleBoxRect& roi,
        const AnalysisLumaSource& source,size_t budget,unsigned& samples,const char*& reason) {
        if(p.step<=0 || p.width<=0 || p.height<=0 || p.width>960 || p.height>540 || p.width!=(source.width+p.step-1)/p.step ||
            p.height!=(source.height+p.step-1)/p.step || p.sourceRows.size()!=size_t(p.height) ||
            !p.paletteValid || p.inkFloor<0 || p.inkFloor>1023 || p.blackLimit<0 || p.blackLimit>1023 ||
            !std::is_sorted(p.sourceRows.begin(),p.sourceRows.end()) ||
            std::adjacent_find(p.sourceRows.begin(),p.sourceRows.end())!=p.sourceRows.end()) {reason="invalid-grid";return {};}
        auto f=std::make_shared<SubtitleInkSnapshot>();
        f->width=p.width;f->height=p.height;f->step=p.step;f->sourceRows=p.sourceRows;
        f->paletteValid=true;f->inkFloor=p.inkFloor;f->inkU=p.inkU;f->inkV=p.inkV;f->blackLimit=p.blackLimit;
        const size_t words=(size_t(p.width)*p.height+63)/64;
        f->rawInk.assign(words,0);f->blackBacking.assign(words,0);f->ownedInk.assign(words,0);
        for(int y=0;y<p.height;++y) {
            const int sy=p.sourceRows[size_t(y)];if(sy<0 || sy>=source.height){reason="invalid-row";return {};}
            if(sy<roi.top || sy>=roi.bottom)continue;
            for(int x=(roi.left+p.step-1)/p.step;x<p.width && x*p.step<roi.right;++x) {
                if(samples>=budget){reason="sample-budget";return {};}
                AnalysisLumaSample s;if(!source.Sample(x*p.step,sy,s)){reason="sample-failed";return {};}
                ++samples;const size_t i=size_t(y)*p.width+x;const uint64_t bit=uint64_t(1)<<(i%64);
                if(s.luma>=p.inkFloor && std::abs(int(s.chromaU)-p.inkU)<=80 && std::abs(int(s.chromaV)-p.inkV)<=80)f->rawInk[i/64]|=bit;
                if(s.luma<=p.blackLimit)f->blackBacking[i/64]|=bit;
            }
        }
        // Coverage is deliberately bounded to sampledBounds, never whole raster.
        f->rawEvidenceComplete=false;return f;
    }
    bool m_valid=false,m_missingStarted=false,m_localGlyphsOnly=false;const char* m_lastSeedReason="not-attempted";
    SubtitleBoxObservation m_reference;ActivePictureBounds m_base;
    std::shared_ptr<SubtitleInkSnapshot> m_baseline;SubtitleBoxRect m_roi;
    ActivePictureFrameIdentity m_last;double m_elapsed=0;SubtitleCueContinuation m_cached;
    AnalysisLumaFormat m_format=AnalysisLumaFormat::P010;VideoFrameEncoding m_encoding=VideoFrameEncoding::UNKNOWN;
    AnalysisLumaRange m_range=AnalysisLumaRange::Unknown;ColorSpace m_colorspace=ColorSpace::UNKNOWN;uint64_t m_sourceGeneration=0;
};
