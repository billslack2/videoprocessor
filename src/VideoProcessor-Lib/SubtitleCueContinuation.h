#pragma once
#include <SubtitleBoxLookahead.h>
#include <SubtitleCueContinuationProof.h>
#include <SubtitleCueSidePlane.h>

// Current complete source proof is independent of unrelated AR hints. Only
// incomplete missing-edge evidence uses a non-renewing lease. Raw worker/AR
// observations are never rewritten.
class SubtitleCueContinuationGuard {
public:
    void Reset() { m_valid=false;m_incompleteCurrent=false; m_reference={}; m_baseline.reset();m_baselineNativeSamples.clear();m_currentNativeSamples.clear();m_acquiredInk.reset(); m_cached={}; m_elapsed=0;m_missingStarted=false; }
    bool HasReference() const { return m_valid; }
    bool NeedsCurrentPixelRevalidation(const SubtitleBoxPreview& preview) const {
        return m_valid && (PrivateReference() || m_incompleteCurrent || !preview.current.text.detected ||
            !SubtitleBoxLookahead::SameContext(m_reference,preview.current) ||
            !CurrentCueSide(preview.current,m_fromTop,m_fromTop?m_reference.pictureTop:m_reference.pictureBottom) ||
            !preview.current.sharedPicture.Matches(m_reference.sharedPicture));
    }
    // Keep the last seed result through Reset so cursor cleanup cannot erase it.
    const char* LastSeedReason() const { return m_lastSeedReason; }
    // Retained through Reset; consumers must match currentIdentity before logging.
    const SubtitleCuePixelMismatch& LastPixelMismatch() const { return m_lastPixelMismatch; }
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
        if(!ValidBase(base,c.width,c.height))return reject("invalid-native-base");
        bool fromTop=false;
        if(!CueSide(c.text,c.pictureTop,c.pictureBottom,fromTop))
            return reject("cue-side-placement-unavailable");
        const int plane=fromTop?c.pictureTop:c.pictureBottom;
        const bool currentSide=CurrentCueSide(c,fromTop,plane);
        const bool privateSide=fromTop?c.privateCuePlaneTop:c.privateCuePlaneBottom;
        // A nearby/reference coordinate may nominate an INTERNAL candidate,
        // only after the exact current native plane is independently proved.
        // It does not replace raw evidence or grant complete continuation.
        const bool nominatedSide=c.barAuthority && c.barEvidence.available &&
            !c.barTrackingAuthority && !c.currentAnchorUsesTrackedEdge && plane>0 && plane<c.height &&
            (fromTop?c.barEvidence.top==plane && !c.barEvidence.captionBackedTop &&
                (c.barEvidence.topRevalidatedNearby || c.barEvidence.topRevalidatedAtReference):
                c.barEvidence.bottom==plane && !c.barEvidence.captionBackedBottom &&
                (c.barEvidence.bottomRevalidatedNearby || c.barEvidence.bottomRevalidatedAtReference));
        if(!currentSide && !nominatedSide)return reject("cue-side-placement-unavailable");
        if(!(fromTop?c.text.topSearchComplete:c.text.bottomSearchComplete) ||
            (!currentSide && !CompleteOwnBarCoverage(c,fromTop,plane)))
            return reject("cue-side-search-incomplete");
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
        if(!currentSide || privateSide) {
            const auto ownPlane=privateSide?
                ProveCurrentSubtitleCaptionPlane(source,c.text,c.ink.get(),plane,fromTop,65536):
                ProveCurrentSubtitleCueSidePlane(source,plane,fromTop,65536);
            if(!ownPlane.valid)return reject(ownPlane.reason);
        }
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
        std::vector<AnalysisLumaSample> nativeSamples;
        const size_t referenceLimit=privateSide && currentSide && CompleteCoverage(c,fromTop)?
            SubtitlePrivateCueReferenceSampleLimit:size_t(65536);
        auto sampled=Sample(*c.ink,roi,source,referenceLimit,samples,reason,&nativeSamples);
        if(!sampled)return reject(reason);
        // Acquisition grouping supplies ownership; pixels are freshly sampled.
        const size_t words=sampled->rawInk.size();
        if(c.ink->ownedInk.size()!=words)return reject("owned-grid-mismatch");
        for(size_t i=0;i<words;++i)sampled->ownedInk[i]=c.ink->ownedInk[i]&sampled->rawInk[i];
        if(!currentSide) {
            auto nativeCandidate=acceptedObservation;nativeCandidate.ink=sampled;
            if(!SubtitleBoxLookahead::ConfidentSameInk(acceptedObservation,nativeCandidate))
                return reject("candidate-current-glyphs-unproved");
        }
        m_baselineNativeSamples=std::move(nativeSamples);
        m_reference=acceptedObservation;m_reference.ink=sampled;
        m_baseline=sampled;m_acquiredInk=c.ink;m_fromTop=fromTop;m_base=base;m_roi=roi;m_last=c.identity;m_elapsed=0;m_missingStarted=false;m_valid=true;m_cached={};
        m_format=source.format;m_encoding=source.encoding;m_range=source.range;m_colorspace=source.colorspace;
        m_sourceGeneration=source.generation;
        // A nominated cold candidate has not earned any continuation lease.
        // Its first continuation must complete the configured current scan.
        m_incompleteCurrent=!currentSide || privateSide;
        m_lastSeedReason=currentSide?"seeded":"seeded-current-cue-side-plane";
        return true;
    }
    // Admission predicate only; current pixels still have to pass InspectCurrent.
    bool CanInspectCurrentObservation(const SubtitleBoxObservation& c) const {
        return m_valid && !c.discontinuity && Context(c,m_reference) &&
            c.identity.acceptedSequence>=m_last.acceptedSequence &&
            c.policyGeneration==m_reference.policyGeneration &&
            c.continuityGeneration==m_reference.continuityGeneration;
    }
    // A queue-only witness nominates private state for a new COMPLETE proof.
    // It is never a continuation certificate or permission to renew a lease.
    bool CanInspectAfterBacklogRetirement(const SubtitleBoxObservation& c,const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity* witness,bool backlogOnly) const {
        if(!backlogOnly || !witness || !m_valid || c.discontinuity || !Context(c,m_reference) ||
            !ValidSource(c,source) || !SameAnalysisFrameIdentity(*witness,m_last) ||
            c.policyGeneration!=m_reference.policyGeneration || !m_reference.continuityGeneration ||
            c.continuityGeneration<=m_reference.continuityGeneration ||
            source.format!=m_format || source.encoding!=m_encoding || source.range!=m_range ||
            source.colorspace!=m_colorspace || source.generation!=m_sourceGeneration ||
            c.identity.acceptedSequence<=m_last.acceptedSequence ||
            c.identity.captureTimestamp<=m_last.captureTimestamp)return false;
        const auto gap=c.identity.acceptedSequence-m_last.acceptedSequence;
        return !(c.identity.sourceFrameNumber || m_last.sourceFrameNumber) ||
            (m_last.sourceFrameNumber && c.identity.sourceFrameNumber>m_last.sourceFrameNumber &&
             c.identity.sourceFrameNumber-m_last.sourceFrameNumber==gap);
    }
    bool CopyBacklogInspectionCandidateTo(SubtitleCueContinuationGuard& trial,
        const SubtitleBoxObservation& c,const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity* witness,bool backlogOnly) const {
        if(&trial==this || !CanInspectAfterBacklogRetirement(c,source,witness,backlogOnly))return false;
        trial=*this;
        trial.m_reference.continuityGeneration=c.continuityGeneration;
        trial.m_cached.valid=false;trial.m_cached.completeCurrentCue=false;trial.m_cached.candidateRetained=false;
        trial.m_cached.ink.reset();trial.m_cached.currentInk.reset();
        trial.m_incompleteCurrent=true;
        return true;
    }
    const ActivePictureFrameIdentity& LastVerifiedIdentity() const {return m_last;}
    uint64_t ReferenceContinuityGeneration() const {return m_valid?m_reference.continuityGeneration:0;}
    SubtitleCueContinuation InspectCurrent(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        size_t maxSamples=SubtitleCurrentCueInspectionSampleLimit) {
        SubtitleCueContinuation out;const auto& raw=preview.current;
        out.identity=raw.identity;out.templateIdentity=m_reference.identity;out.previousIdentity=m_last;
        out.policyGeneration=preview.policyGeneration;out.continuityGeneration=preview.continuityGeneration;
        out.fromTopBar=m_fromTop;
        const auto fail=[&](const char* reason) {out.reason=reason;Reset();return out;};
        if(!CanInspectCurrentObservation(raw) || !ValidSource(raw,source) ||
            preview.policyGeneration!=m_reference.policyGeneration ||
            preview.continuityGeneration!=m_reference.continuityGeneration ||
            source.format!=m_format || source.encoding!=m_encoding || source.range!=m_range ||
            source.colorspace!=m_colorspace || source.generation!=m_sourceGeneration)
            return fail("complete-context-changed");
        const int plane=m_fromTop?m_reference.pictureTop:m_reference.pictureBottom;
        const int rawPlane=m_fromTop?raw.pictureTop:raw.pictureBottom;
        if(raw.analyzed && raw.barAuthority && raw.barEvidence.available &&
            rawPlane>0 && rawPlane<source.height && rawPlane!=plane)
            return fail("complete-cue-side-changed");
        if(m_cached.MatchesSource(raw.identity,preview.policyGeneration,preview.continuityGeneration))return m_cached;
        const auto remaining=source.readBudget && source.readBudget->used<source.readBudget->limit?
            source.readBudget->limit-source.readBudget->used:(source.readBudget?0:maxSamples);
        AnalysisLumaReadBudget budget{(std::min)(maxSamples,remaining)};
        auto currentSource=source;currentSource.readBudget=&budget;
        // Charge the outer synchronous frame budget exactly once on every exit.
        struct Charge {AnalysisLumaReadBudget* outer;AnalysisLumaReadBudget& local;size_t remaining;
            ~Charge(){if(outer){outer->used+=local.used;outer->exhausted|=local.exhausted && local.limit==remaining;}}
        } charge{source.readBudget,budget,remaining};
        const size_t startReads=0;
        const auto finish=[&](const char* reason) {
            out.samples=static_cast<unsigned>(currentSource.readBudget->used-startReads);
            out.reason=currentSource.readBudget->exhausted?"complete-sample-budget":reason;
            out.candidateRetained=!out.valid && m_valid;
            m_incompleteCurrent=out.candidateRetained;return out;
        };
        unsigned verifiedSamples=0;const char* reason="not-eligible";
        auto fresh=Verify(raw,currentSource,maxSamples,verifiedSamples,reason,true);
        if(!fresh) {
            // Native contradiction ends ownership. Exhausted/incomplete work
            // retains only an unrendered candidate, never a cleanup lease.
            if(!budget.exhausted && (std::strcmp(reason,"glyph-proof-failed")==0 ||
                std::strcmp(reason,"new-ink-outside-cue")==0 || std::strcmp(reason,"backing-changed")==0))Reset();
            return finish(reason);
        }
        // Re-prove only the acquired side against CURRENT pixels. The opposite
        // retained coordinate is never submitted as measured bar evidence.
        SubtitleBarEvidence neutral;neutral.bottom=source.height;
        const auto bars=RevalidateSubtitleBarEvidence(currentSource,neutral,
            m_fromTop?plane:0,m_fromTop?source.height:plane,fresh.get(),&m_reference.text.bounds);
        bool cueSideProved=m_fromTop?bars.topRevalidatedAtReference:bars.bottomRevalidatedAtReference;
        if(!cueSideProved && !budget.exhausted && budget.used<budget.limit &&
            (!source.readBudget || !source.readBudget->exhausted)) {
            // Display padding can cross the plane while every actual glyph
            // line stays inside its bar. In that case the envelope activates
            // an unnecessary owned-ink mask and can exhaust its mask limits.
            // Preserve masked successes; retry only this all-bar case with an
            // exact native edge, never a nearby glyph-supplied boundary jump.
            const auto& envelope=m_reference.text.bounds;
            bool allLinesInCueBar=m_reference.text.lineCount>0 && m_reference.text.lineCount<=3;
            for(int i=0;i<m_reference.text.lineCount && allLinesInCueBar;++i) {
                const auto& line=m_reference.text.lineBounds[i];
                allLinesInCueBar=InRaster(line,source.width,source.height) &&
                    (m_fromTop?line.bottom<=plane:line.top>=plane);
            }
            if(allLinesInCueBar && InRaster(envelope,source.width,source.height) &&
                envelope.top<plane && envelope.bottom>plane)
                cueSideProved=ProveCurrentSubtitleCueSidePlane(currentSource,plane,m_fromTop,65536).valid;
        }
        const bool privateSide=m_fromTop?m_reference.privateCuePlaneTop:m_reference.privateCuePlaneBottom;
        if(privateSide && !budget.exhausted) {
            // The acquired footprint only nominates this early plane. The
            // complete current detector footprint is independently checked below.
            cueSideProved=ProveCurrentSubtitleCaptionPlane(currentSource,m_reference.text,
                m_acquiredInk.get(),plane,m_fromTop,65536).valid;
        }
        if(!cueSideProved)return finish("complete-cue-side-unproved");
        auto local=raw;
        const bool reusable=preview.available && raw.analyzed && !raw.analysisRefresh && !raw.pendingRefresh &&
            !raw.cadenceSkipped && CurrentWorkerAtReprovedCuePlane(raw,m_fromTop,plane) &&
            CompleteCoverage(raw,m_fromTop) && raw.text.detected && !raw.text.held &&
            !raw.text.confirmationPending && !raw.text.workLimit;
        if(!reusable) {
            SubtitleBoxDetector detector;detector.SetScriptHint(raw.scriptHint);
            detector.SetNearBarDistance(raw.nearBarDistance);detector.SetOptimizationMode(raw.optimizationMode);
            local.pictureTop=m_fromTop?plane:0;local.pictureBottom=m_fromTop?source.height:plane;
            local.text=detector.Analyze(currentSource,local.pictureTop,local.pictureBottom,
                raw.identity.acceptedSequence,raw.identity.viewportGeneration,true);
            local.ink=detector.InkSnapshot();
        }
        if(currentSource.readBudget->exhausted)return finish("complete-sample-budget");
        if(!CompleteCoverage(local,m_fromTop) || local.text.workLimit)
            return finish("complete-search-incomplete");
        if(!local.text.detected || local.text.held || local.text.confirmationPending ||
            !SameSlots(local.text,m_reference.text) || !NoNewBarInk(local)) {
            Reset();return finish("complete-cue-changed");
        }
        if(privateSide) {
            const auto currentPlane=ProveCurrentSubtitleCaptionPlane(currentSource,local.text,
                local.ink.get(),plane,m_fromTop,65536);
            if(!currentPlane.valid)return finish(currentPlane.reason);
        }
        // The full current detector must agree with the acquired strokes too;
        // unchanged rectangles alone cannot certify replacement text.
        // The ink matcher uses an acquisition-coordinate comparison wrapper;
        // this private wrapper is never returned as measured physical evidence.
        auto comparison=m_reference;comparison.identity=raw.identity;
        comparison.text=local.text;comparison.ink=local.ink;
        if(!SubtitleBoxLookahead::ConfidentSameInk(m_reference,comparison)) {
            Reset();return finish("complete-detector-ink-changed");
        }
        out.valid=out.completeCurrentCue=true;out.oppositeBarUnavailable=true;
        out.physicalBase=m_base;out.sampledBounds=m_roi;out.backingVerifiedBounds=m_reference.text.sourcePanel;
        out.pictureTop=m_reference.pictureTop;out.pictureBottom=m_reference.pictureBottom;
        out.accepted=m_reference.text;out.accepted.held=false;
        out.ink=fresh;out.currentInk=local.ink;out.searchStep=local.text.searchStep;
        out.verifiedSearch={0,m_fromTop?0:local.text.bottomSearchStart,source.width,
            m_fromTop?local.text.topSearchEnd:source.height};
        out=finish("current-complete-cue");m_last=raw.identity;m_elapsed=0;m_missingStarted=false;m_cached=out;
        return out;
    }
    SubtitleCueContinuation Prepare(const SubtitleBoxPreview& preview,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,int holdMs,double frameMs,size_t maxSamples=SubtitleCurrentCueInspectionSampleLimit) {
        SubtitleCueContinuation out;const auto& c=preview.current;
        out.identity=c.identity;out.templateIdentity=m_reference.identity;out.previousIdentity=m_last;
        out.policyGeneration=preview.policyGeneration;out.continuityGeneration=preview.continuityGeneration;
        auto fail=[&](const char* why) {out.reason=why;m_valid=false;m_cached=out;return out;};
        if(!m_valid)return out;
        if(PrivateReference() || m_incompleteCurrent || c.text.detected || !preview.available ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) ||
            !c.sharedPicture.Matches(m_reference.sharedPicture))
            return InspectCurrent(preview,source,maxSamples);
        if(!preview.available || !c.analyzed || c.glyphLocalTop || c.glyphLocalBottom || c.discontinuity || c.pendingRefresh || c.analysisRefresh ||
            c.text.workLimit || !ValidSource(c,source) || !SameBase(base,m_base) ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) || !SameBase(base,c.sharedPicture.bounds) ||
            !Context(c,m_reference) || preview.policyGeneration!=m_reference.policyGeneration ||
            preview.continuityGeneration!=m_reference.continuityGeneration ||
            c.policyGeneration!=preview.policyGeneration || c.continuityGeneration!=preview.continuityGeneration ||
            source.format!=m_format || source.encoding!=m_encoding || source.range!=m_range ||
            source.colorspace!=m_colorspace || source.generation!=m_sourceGeneration)
            return fail("context-changed");

        bool topCue=false,bottomCue=false;
        for(int i=0;i<m_reference.text.lineCount;++i) {
            topCue|=m_reference.text.lineBounds[i].top<m_reference.pictureTop;
            bottomCue|=m_reference.text.lineBounds[i].bottom>m_reference.pictureBottom;
        }
        const bool missingBottom=bottomCue && !topCue && m_reference.pictureBottom<c.height &&
            c.pictureBottom==c.height && c.pictureTop==m_reference.pictureTop && c.pictureTop>0;
        const bool missingTop=topCue && !bottomCue && m_reference.pictureTop>0 &&
            c.pictureTop==0 && c.pictureBottom==m_reference.pictureBottom && c.pictureBottom<c.height;
        // The opposite bar can lose its measurement while this cue and its
        // own bar remain independently detected. That context change cannot
        // certify new glyphs; recheck the exact acquired native pixels instead.
        const bool sameDetectedSlots=[&]() {
            if(!c.text.detected || c.text.held || c.text.confirmationPending ||
                c.text.lineCount<1 || c.text.lineCount>3 ||
                c.text.lineCount!=m_reference.text.lineCount)return false;
            for(int i=0;i<c.text.lineCount;++i)
                if(!SubtitleCueContinuation::SameRect(c.text.lineBounds[i],
                    m_reference.text.lineBounds[i]))return false;
            return true;
        }();
        const bool missingOpposite=sameDetectedSlots &&
            ((bottomCue && !topCue && m_reference.pictureTop>0 && c.pictureTop==0 &&
                c.pictureBottom==m_reference.pictureBottom && c.pictureBottom<c.height) ||
             (topCue && !bottomCue && m_reference.pictureBottom<c.height && c.pictureBottom==c.height &&
                c.pictureTop==m_reference.pictureTop && c.pictureTop>0));
        if(c.text.detected && !missingOpposite)return fail("current-detector-positive");
        if((!missingBottom && !missingTop && !missingOpposite) ||
            !SubtitleBoxLookahead::HasBarEvidence(c))return fail("not-missing-cue-edge");
        if(SameAnalysisFrameIdentity(c.identity,m_last))return m_cached;
        if(c.identity.acceptedSequence!=m_last.acceptedSequence+1)return fail("sequence-gap");
        if(!std::isfinite(frameMs) || frameMs<=0)return fail("invalid-source-duration");
        m_missingStarted=true;m_elapsed+=frameMs;out.elapsedMs=m_elapsed;m_last=c.identity;
        if(m_elapsed>(std::max)(0,(std::min)(250,holdMs)))return fail("lease-expired");
        auto fresh=Verify(c,source,maxSamples,out.samples,out.reason);
        if(!fresh)return fail(out.reason);
        out.valid=true;out.reason=missingOpposite?"current-cue-pixels-opposite-edge":"current-cue-pixels";out.physicalBase=m_base;out.sampledBounds=m_roi;out.backingVerifiedBounds=m_reference.text.sourcePanel;
        out.pictureTop=m_reference.pictureTop;out.pictureBottom=m_reference.pictureBottom;
        out.accepted=m_reference.text;out.accepted.held=!missingOpposite;out.oppositeBarUnavailable=missingOpposite;out.ink=fresh;m_cached=out;return out;
    }
    // Advances only the source cursor after fresh verification. It neither
    // replaces acquisition evidence nor renews its finite lease once an edge is missing.
    bool AdvanceVerified(const SubtitleBoxPreview& preview,const SubtitleBoxResult& accepted,const AnalysisLumaSource& source,
        const ActivePictureBounds& base,int holdMs,double frameMs,size_t maxSamples=65536) {
        const auto& c=preview.current;
        if(accepted.confirmationPending){Reset();return false;}
        if(!m_valid || PrivateReference())return false; // Private planes require a COMPLETE current inspection.
        SubtitleCueContinuation ownership;ownership.valid=true;ownership.accepted=m_reference.text;
        if(!ownership.MatchesCue(accepted)) {Reset();return false;}
        if(!preview.available || !c.analyzed || c.glyphLocalTop || c.glyphLocalBottom || c.discontinuity || c.pendingRefresh || c.text.workLimit ||
            !ValidSource(c,source) || !SameBase(base,m_base) || !Context(c,m_reference) ||
            !c.sharedPicture.AvailableFor(c.identity,c.width,c.height) || !SameBase(base,c.sharedPicture.bounds) ||
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
        if(!Verify(c,source,maxSamples,samples,reason)) {Reset();return false;}
        m_last=c.identity;m_cached={};return true;
    }
private:
    bool PrivateReference() const { return m_reference.privateCuePlaneTop || m_reference.privateCuePlaneBottom; }
    static bool ValidBase(const ActivePictureBounds& b,int w,int h) {
        return b.rasterWidth==w && b.rasterHeight==h && b.left>=0 && b.right<=w && b.left<b.right &&
            b.top>=0 && b.bottom<=h && b.top<b.bottom;
    }
    static bool CueSide(const SubtitleBoxResult& text,int top,int bottom,bool& fromTop) {
        bool upper=false,lower=false;
        for(int i=0;i<text.lineCount;++i) {upper|=text.lineBounds[i].top<top;lower|=text.lineBounds[i].bottom>bottom;}
        fromTop=upper;return upper!=lower;
    }
    static bool SameSlots(const SubtitleBoxResult& a,const SubtitleBoxResult& b) {
        if(a.lineCount!=b.lineCount || a.lineCount<1 || a.lineCount>3)return false;
        for(int i=0;i<a.lineCount;++i)if(!SubtitleCueContinuation::SameRect(a.lineBounds[i],b.lineBounds[i]))return false;
        return true;
    }
    static bool CurrentCueSide(const SubtitleBoxObservation& c,bool top,int plane) {
        if((top?c.privateCuePlaneTop:c.privateCuePlaneBottom) &&
            !(top?c.privateCuePlaneBottom:c.privateCuePlaneTop) && c.analyzed &&
            !c.analysisRefresh && !c.pendingRefresh && !c.discontinuity &&
            c.barAuthority && c.barEvidence.available && !c.barTrackingAuthority && !c.currentAnchorUsesTrackedEdge &&
            !c.glyphLocalTop && !c.glyphLocalBottom && !c.independentBarAnalysis &&
            plane>0 && plane<c.height && (top?c.pictureTop==plane:c.pictureBottom==plane) &&
            CompleteCoverage(c,top))return true;
        return c.barAuthority && c.barEvidence.available && !c.barTrackingAuthority && !c.currentAnchorUsesTrackedEdge &&
            plane>0 && plane<c.height && (top?c.pictureTop==plane && c.barEvidence.top==plane &&
                !c.barEvidence.captionBackedTop && !c.barEvidence.topRevalidatedAtReference && !c.barEvidence.topRevalidatedNearby:
                c.pictureBottom==plane && c.barEvidence.bottom==plane && !c.barEvidence.captionBackedBottom &&
                !c.barEvidence.bottomRevalidatedAtReference && !c.barEvidence.bottomRevalidatedNearby);
    }
    // Candidate acquisition requires actual bar coverage, including all
    // currently owned glyphs. A narrow worker pass can meet this while its
    // configured picture/companion extension remains unread. That extension
    // MUST pass CompleteCoverage in InspectCurrent before any output proof.
    static bool CompleteOwnBarCoverage(const SubtitleBoxObservation& c,bool top,int plane) {
        if(!c.ink || !c.ink->paletteValid || !c.ink->rawEvidenceComplete || c.text.workLimit ||
            !(top?c.text.topSearchComplete:c.text.bottomSearchComplete))return false;
        const int step=SubtitleBoxDetector::SamplingStep(c.width,c.height);
        if(c.text.searchStep!=step || c.ink->step!=step ||
            c.ink->width!=(c.width+step-1)/step || c.ink->height!=(c.height+step-1)/step ||
            c.ink->sourceRows.size()!=size_t(c.ink->height))return false;
        return top?c.text.topSearchEnd>=plane:c.text.bottomSearchStart<=plane;
    }
    // This nominates current WORKER coverage only after InspectCurrent has
    // freshly re-proved the exact acquired native plane above. Nearby/reference
    // provenance cannot defeat that new pixel proof, nor supply it by itself.
    // Tracking, caption-derived placement and incomplete coverage still cannot
    // substitute for the independent full current inspector.
    static bool CurrentWorkerAtReprovedCuePlane(const SubtitleBoxObservation& c,bool top,int plane) {
        if((top?c.privateCuePlaneTop:c.privateCuePlaneBottom) && CurrentCueSide(c,top,plane))return true;
        return c.barAuthority && c.barEvidence.available && !c.glyphLocalTop && !c.glyphLocalBottom &&
            !c.barTrackingAuthority && !c.currentAnchorUsesTrackedEdge && plane>0 && plane<c.height &&
            (top?c.pictureTop==plane && c.barEvidence.top==plane && !c.barEvidence.captionBackedTop:
                c.pictureBottom==plane && c.barEvidence.bottom==plane && !c.barEvidence.captionBackedBottom);
    }
    static bool CompleteCoverage(const SubtitleBoxObservation& c,bool top) {
        if(!c.ink || !c.ink->paletteValid || !c.ink->rawEvidenceComplete || !c.text.configuredSearchComplete ||
            !(top?c.text.topSearchComplete:c.text.bottomSearchComplete) || c.text.workLimit)return false;
        const int step=SubtitleBoxDetector::SamplingStep(c.width,c.height);
        const int rows=(c.height+step-1)/step;
        const int depth=(std::max)(24,rows/7)+(c.nearBarDistance+step-1)/step;
        const int required=top?(std::min)(c.height,((c.pictureTop+step-1)/step+depth)*step):
            (std::max)(0,((std::min)(rows-1,(c.pictureBottom+step-1)/step)-depth)*step);
        return c.text.searchStep==step && (top?c.text.topSearchEnd>=required:c.text.bottomSearchStart<=required);
    }
    bool NoNewBarInk(const SubtitleBoxObservation& c) const {
        if(!c.ink || !m_acquiredInk)return false;
        const auto& now=*c.ink;const auto& prior=*m_acquiredInk;
        if(now.width!=prior.width || now.height!=prior.height || now.step!=prior.step ||
            now.sourceRows.size()!=size_t(now.height) || prior.sourceRows.size()!=size_t(prior.height))return false;
        const int plane=m_fromTop?m_reference.pictureTop:m_reference.pictureBottom;
        for(int y=0;y<now.height;++y) {
            const int sy=now.sourceRows[size_t(y)];if(m_fromTop?sy>=plane:sy<plane)continue;
            if(prior.sourceRows[size_t(y)]!=sy)return false;
            for(int x=0;x<now.width;++x) {
                const int sx=x*now.step;bool ownedLine=false;
                for(int line=0;line<m_reference.text.lineCount;++line) {
                    const auto& b=m_reference.text.lineBounds[line];
                    ownedLine|=sx>=b.left && sx<b.right && sy>=b.top && sy<b.bottom;
                }
                if(!ownedLine && now.Get(x,y) && !prior.Get(x,y))return false;
            }
        }
        return true;
    }
    std::shared_ptr<SubtitleInkSnapshot> Verify(const SubtitleBoxObservation& c,const AnalysisLumaSource& source,
        size_t maxSamples,unsigned& samples,const char*& reason,bool completePictureInspection=false) const {
        m_lastPixelMismatch={};
        auto fresh=Sample(*m_baseline,m_roi,source,maxSamples,samples,reason,&m_currentNativeSamples);
        if(!fresh)return {};
        size_t sampleIndex=0;
        const int searchStep=SubtitleBoxDetector::SamplingStep(c.width,c.height);
        const int searchRows=(c.height+searchStep-1)/searchStep;
        const int searchDepth=(std::max)(24,searchRows/7)+(c.nearBarDistance+searchStep-1)/searchStep;
        const int searchEdge=m_fromTop?(std::min)(c.height,((m_reference.pictureTop+searchStep-1)/searchStep+searchDepth)*searchStep):
            (std::max)(0,((std::min)(searchRows-1,(m_reference.pictureBottom+searchStep-1)/searchStep)-searchDepth)*searchStep);
        for(int y=0;y<fresh->height;++y) {
            const int sy=fresh->sourceRows[size_t(y)];if(sy<m_roi.top || sy>=m_roi.bottom)continue;
            for(int x=(m_roi.left+fresh->step-1)/fresh->step;x<fresh->width && x*fresh->step<m_roi.right;++x) {
                const int sx=x*fresh->step;bool line=false;
                for(int i=0;i<m_reference.text.lineCount;++i) {
                    const auto& b=m_reference.text.lineBounds[i];
                    line|=sx>=b.left && sx<b.right && sy>=b.top && sy<b.bottom;
                }
                const auto& panel=m_reference.text.sourcePanel;
                if(!line && fresh->Get(x,y) && !m_baseline->Get(x,y)) {
                    const auto contains=[&](const SubtitleBoxRect& b) {
                        return b.Valid() && sx>=b.left && sx<b.right && sy>=b.top && sy<b.bottom;
                    };
                    bool protectedSource=contains(panel) || contains(m_reference.text.bounds) ||
                        contains(m_reference.text.capturePanel) || m_baseline->Get(x,y,true);
                    for(int i=0;i<m_reference.text.lineCount;++i)
                        protectedSource|=contains(m_reference.text.lineCapturePanels[i]);
                    // Only a FULL configured inspection may judge an unrelated
                    // picture point. It is outside every acquired extraction/
                    // cleanup envelope and inside that inspection's own-side
                    // scan. Bar ink, glyphs, backing and partial proofs stay strict.
                    const bool deferPicture=completePictureInspection && !protectedSource &&
                        sy>=m_reference.pictureTop && sy<m_reference.pictureBottom &&
                        (m_fromTop?sy<searchEdge:sy>=searchEdge);
                    if(deferPicture) {++sampleIndex;continue;}
                    if(sampleIndex<m_baselineNativeSamples.size() && sampleIndex<m_currentNativeSamples.size()) {
                        auto& d=m_lastPixelMismatch;d.valid=true;d.x=sx;d.y=sy;
                        d.prior=m_baselineNativeSamples[sampleIndex];d.current=m_currentNativeSamples[sampleIndex];
                        d.priorInk=m_baseline->Get(x,y);d.currentInk=fresh->Get(x,y);
                        d.priorBlack=m_baseline->Black(x,y);d.currentBlack=fresh->Black(x,y);
                        d.inkFloor=m_baseline->inkFloor;d.inkU=m_baseline->inkU;
                        d.inkV=m_baseline->inkV;d.blackLimit=m_baseline->blackLimit;
                        d.inLine=line;d.inSourcePanel=panel.Valid() && sx>=panel.left && sx<panel.right && sy>=panel.top && sy<panel.bottom;
                        d.inCueBar=m_fromTop?sy<m_reference.pictureTop:sy>=m_reference.pictureBottom;
                        d.inPicture=sy>=m_reference.pictureTop && sy<m_reference.pictureBottom;
                        d.templateIdentity=m_reference.identity;d.currentIdentity=c.identity;
                    }
                    reason="new-ink-outside-cue";return {};
                }
                ++sampleIndex;
                if(panel.Valid() && sx>=panel.left && sx<panel.right && sy>=panel.top && sy<panel.bottom &&
                    m_baseline->Black(x,y) && !fresh->Black(x,y)){reason="backing-changed";return {};}
            }
        }
        // Geometry is local to comparison: raw measurement and AR stay untouched.
        auto comparison=m_reference;comparison.identity=c.identity;
        comparison.ink=fresh;
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
        const AnalysisLumaSource& source,size_t budget,unsigned& samples,const char*& reason,
        std::vector<AnalysisLumaSample>* nativeValues=nullptr) {
        if(nativeValues)nativeValues->clear();
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
                if(nativeValues)nativeValues->push_back(s);
                ++samples;const size_t i=size_t(y)*p.width+x;const uint64_t bit=uint64_t(1)<<(i%64);
                if(s.luma>=p.inkFloor && std::abs(int(s.chromaU)-p.inkU)<=80 && std::abs(int(s.chromaV)-p.inkV)<=80)f->rawInk[i/64]|=bit;
                if(s.luma<=p.blackLimit)f->blackBacking[i/64]|=bit;
            }
        }
        // Coverage is deliberately bounded to sampledBounds, never whole raster.
        f->rawEvidenceComplete=false;return f;
    }
    bool m_valid=false,m_missingStarted=false,m_incompleteCurrent=false;const char* m_lastSeedReason="not-attempted";
    SubtitleBoxObservation m_reference;ActivePictureBounds m_base;
    std::shared_ptr<SubtitleInkSnapshot> m_baseline;
    std::vector<AnalysisLumaSample> m_baselineNativeSamples;
    mutable std::vector<AnalysisLumaSample> m_currentNativeSamples;
    mutable SubtitleCuePixelMismatch m_lastPixelMismatch;
    std::shared_ptr<const SubtitleInkSnapshot> m_acquiredInk;bool m_fromTop=false;SubtitleBoxRect m_roi;
    ActivePictureFrameIdentity m_last;double m_elapsed=0;SubtitleCueContinuation m_cached;
    AnalysisLumaFormat m_format=AnalysisLumaFormat::P010;VideoFrameEncoding m_encoding=VideoFrameEncoding::UNKNOWN;
    AnalysisLumaRange m_range=AnalysisLumaRange::Unknown;ColorSpace m_colorspace=ColorSpace::UNKNOWN;uint64_t m_sourceGeneration=0;
};
