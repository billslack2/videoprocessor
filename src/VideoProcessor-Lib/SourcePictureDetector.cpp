#include <pch.h>
#include <SourcePictureDetector.h>
#include <OwnedSourcePictureObservationCache.h>
#include <SourcePictureCoarseColor.h>
#include <AnalysisDiagnosticHooks.h>
#include <DebugLog.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>

bool SourcePictureDetectorEnabled() {
    // One production geometry producer. A process environment cannot silently
    // restore the superseded nomination/OSD observer and its conflicting rules.
    // Legacy extraction remains an explicit reference API for offline tests.
    return true;
}
ActivePictureEvidence MissingSourcePictureEvidence(const AnalysisLumaSource& source,const char* reason) {
    ActivePictureEvidence result;
    // Missing geometry is not zero-code black. Nominal range is available
    // without sampling; evaluated remains false until a source reference exists.
    result.blackReference.floor=source.range==AnalysisLumaRange::Full?0:64;
    result.blackReference.threshold=std::min(104,result.blackReference.floor+24);
    result.blackReference.observedFloor=result.blackReference.floor;
    result.blackReference.observedThreshold=result.blackReference.threshold;
    result.blackReference.mode=source.range==AnalysisLumaRange::Full?
        ActivePictureBlackReferenceMode::FULL_NOMINAL:ActivePictureBlackReferenceMode::LEGACY;
    result.detector=ActivePictureDetectorKind::SourceSpatialUnavailable;
    result.reason=reason;
    result.physicalEdges.sourceGeneration=source.generation;
    result.physicalEdges.reason=reason;
    return result;
}
bool CurrentSourceTransitionSupportsHypothesis(const SourcePictureObservation& facts,const ActivePictureBounds& hypothesis) {
    if(!facts.complete || !facts.rectangleAvailable || !facts.transitionProfilesComplete || facts.outcome!=SourcePictureOutcome::Measured ||
        hypothesis.rasterWidth!=facts.width || hypothesis.rasterHeight!=facts.height ||
        hypothesis.left!=facts.measuredBounds.left || hypothesis.right!=facts.measuredBounds.right)return false;
    for(unsigned side:{1u,3u}) {
        const int selected=side==1?facts.measuredBounds.top:facts.measuredBounds.bottom;
        const int value=side==1?hypothesis.top:hypothesis.bottom;
        if(value==selected)continue;
        std::array<bool,128> rays{};std::array<unsigned,4> regions{};unsigned support=0,broad=0;
        for(const auto& p:facts.transitionProfiles)if(p.side==side && selected>=p.minimum && selected<=p.maximum &&
            value>=p.transitionMinimum && value<=p.transitionMaximum && p.ray<128)rays[p.ray]=true;
        for(unsigned i=0;i<128;++i)if(rays[i]){++support;++regions[i/32];}
        for(auto n:regions)if(n>=2)++broad;
        if(support<32 || (broad<3 && !(regions[0]>=12 && regions[3]>=12)))return false;
    }
    return true;
}
std::shared_ptr<const SourcePictureBoundaryReference> SourcePictureBoundaryReference::Capture(
    const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity,const SourcePictureObservation& facts,
    const ActivePictureBounds* compatibleHypothesis,const AnalysisEvaluationBudget* budget) {
    if(!source.IsValid() || !identity.transportGeneration || !identity.acceptedSequence || !identity.sourceFormatGeneration || !identity.analysisDomainKey ||
        (source.generation!=identity.transportGeneration && source.generation!=identity.sourceFormatGeneration) ||
        !facts.complete || !facts.rectangleAvailable || !facts.transitionProfilesComplete || facts.sourceGeneration!=source.generation ||
        facts.width!=source.width || facts.height!=source.height || facts.measuredBounds.left!=0 || facts.measuredBounds.right!=source.width)return {};
    if(compatibleHypothesis && !CurrentSourceTransitionSupportsHypothesis(facts,*compatibleHypothesis))return {};
    auto reference=std::shared_ptr<SourcePictureBoundaryReference>(new SourcePictureBoundaryReference);
    reference->hypothesis_=compatibleHypothesis?*compatibleHypothesis:facts.measuredBounds;
    reference->identity_=identity;reference->interpretation_=source;reference->interpretation_.data=nullptr;
    reference->interpretation_.dataBytes=0;reference->interpretation_.backingOwner.reset();reference->measured_=facts.measuredBounds;
    reference->windows_={source.height,-1,source.height,-1};std::array<std::array<bool,128>,2> coverage{};std::set<uint64_t> seen;
    ValidatedAnalysisLumaSampler read(source);
    for(const auto& p:facts.transitionProfiles) {
        if(p.side!=1 && p.side!=3)continue;
        if(p.ray>=128)return {};
        const unsigned side=p.side==1?0:1;
        const int selected=side==0?facts.measuredBounds.top:facts.measuredBounds.bottom;
        if(selected<p.minimum || selected>p.maximum)continue;
        coverage[side][p.ray]=true;reference->windows_[2*side]=std::min(reference->windows_[2*side],p.readMinimum);
        reference->windows_[2*side+1]=std::max(reference->windows_[2*side+1],p.readMaximum);
        const int x=int(uint64_t(2*p.ray+1)*source.width/256);
        for(int y=std::max(0,p.readMinimum);y<=std::min(source.height-1,p.readMaximum);++y) {
            CheckAnalysisEvaluationBudget(budget);const uint64_t key=(uint64_t(uint32_t(y))<<32)|uint32_t(x);
            if(!seen.insert(key).second)continue;if(reference->samples_.size()>=16384)return {};
            AnalysisLumaSample sample;if(!read.Sample(x,y,sample))return {};reference->samples_.push_back({x,y,sample});
        }
    }
    for(const auto& side:coverage) {
        unsigned support=0,broad=0;std::array<unsigned,4> regions{};
        for(unsigned i=0;i<128;++i)if(side[i]){++support;++regions[i/32];}
        for(auto n:regions)if(n>=2)++broad;
        if(support<32 || (broad<3 && !(regions[0]>=12&&regions[3]>=12)))return {};
    }
    return reference;
}
bool SourcePictureBoundaryReference::MatchesCurrent(const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& id,const SourcePictureObservation* facts,const AnalysisEvaluationBudget* budget) const {
    const auto& a=interpretation_;
    if(!source.IsValid() || samples_.empty() || id.transportGeneration!=identity_.transportGeneration ||
        id.sourceFormatGeneration!=identity_.sourceFormatGeneration || id.viewportGeneration!=identity_.viewportGeneration ||
        id.rendererGeneration!=identity_.rendererGeneration || id.analysisDomainKey!=identity_.analysisDomainKey ||
        id.acceptedSequence<identity_.acceptedSequence || id.sourceFrameNumber<identity_.sourceFrameNumber ||
        (id.captureTimestamp&&identity_.captureTimestamp&&id.captureTimestamp<identity_.captureTimestamp) ||
        source.generation!=a.generation || source.width!=a.width || source.height!=a.height || source.rowBytes!=a.rowBytes ||
        source.chromaRowBytes!=a.chromaRowBytes || source.format!=a.format || source.encoding!=a.encoding ||
        source.colorspace!=a.colorspace || source.range!=a.range || source.rgbBitAlignment!=a.rgbBitAlignment)return false;
    if(facts && (facts->sourceGeneration!=source.generation || facts->width!=source.width || facts->height!=source.height))return false;
    if(facts && facts->rectangleAvailable) {
        const auto& b=facts->measuredBounds;
        if(b.left!=measured_.left || b.right!=measured_.right || b.top<windows_[0] || b.top>windows_[1] || b.bottom<windows_[2] || b.bottom>windows_[3])return false;
    }
    ValidatedAnalysisLumaSampler read(source);
    for(const auto& old:samples_) {
        CheckAnalysisEvaluationBudget(budget);AnalysisLumaSample now;if(!read.Sample(old.x,old.y,now))return false;
        if(std::abs(int(now.luma)-int(old.value.luma))>4 || std::abs(int(now.chromaU)-int(old.value.chromaU))>4 ||
           std::abs(int(now.chromaV)-int(old.value.chromaV))>4)return false;
    }
    return true;
}

static SourcePictureDetectionResult VerifySourcePictureObservation(const AnalysisLumaSource& source,
    std::shared_ptr<const SourcePictureObservation> observation,const AnalysisEvaluationBudget* budget,
    const ActivePictureEvidence* currentVerification=nullptr) {
    SourcePictureDetectionResult result;
    result.evidence=MissingSourcePictureEvidence(source);
    result.observation=std::move(observation);
    if(!result.observation)return result;
    const auto& facts=*result.observation;
    auto& evidence=result.evidence;
    evidence.reason=facts.reason;
    evidence.physicalEdges.reason=facts.reason;
    if(facts.sourceGeneration==source.generation && facts.width==source.width && facts.height==source.height &&
        facts.blackReference.evaluated)evidence.blackReference=facts.blackReference;
    if(!source.IsValid() || !facts.complete || facts.sourceGeneration!=source.generation ||
        facts.width!=source.width || facts.height!=source.height)return result;
    const bool measured=facts.outcome==SourcePictureOutcome::Measured && facts.rectangleAvailable;
    const bool canvas=facts.outcome==SourcePictureOutcome::CanvasEstimate &&
        facts.canvasSupported && facts.blankMarginsUnresolved && facts.topologyComplete;
    if(!measured && !canvas) {
        evidence.physicalEdges.evaluated=true;
        evidence.physicalEdges.reason=facts.reason;
        return result;
    }
    // Canvas inference may retain the complete input, never assert exact physical
    // edges or certify a discarded margin. Measured geometry stays unrounded.
    ActivePictureBounds nominee=measured?facts.measuredBounds:ActivePictureBounds{};
    if(measured && facts.verificationBoundsAvailable) {
        const auto& guard=facts.verificationBounds;
        if(guard.rasterWidth!=source.width || guard.rasterHeight!=source.height ||
            guard.left<0 || guard.top<0 || guard.right>source.width || guard.bottom>source.height ||
            guard.left>nominee.left || guard.top>nominee.top || guard.right<nominee.right || guard.bottom<nominee.bottom)
            return result;
        nominee=guard;
    }
    if(canvas) {
        nominee={0,0,source.width,source.height,source.width,source.height,
            double(source.width)/source.height,ActivePictureBounds::BarAxes::NONE};
    }
    try { evidence=currentVerification?*currentVerification:VerifySuppliedActivePictureCandidate(source,nominee,budget); }
    catch(const AnalysisEvaluationCancelled&) {
        evidence=MissingSourcePictureEvidence(source,"source-spatial-candidate-verification-budget-exhausted");
    }
    if(!evidence.blackReference.evaluated && facts.blackReference.evaluated)
        evidence.blackReference=facts.blackReference;
    evidence.detector=measured?ActivePictureDetectorKind::SourceSpatialMeasured:ActivePictureDetectorKind::SourceSpatialCanvas;
    auto& physical=evidence.physicalEdges;
    physical.evaluated=true;physical.sourceGeneration=source.generation;physical.samples=facts.samples;
    physical.conservativeBounds=evidence.trustedBounds;
    physical.estimateAvailable=measured;
    physical.estimate=measured?facts.measuredBounds:ActivePictureBounds{};
    physical.positionMinimum=facts.edgeMinimum;physical.positionMaximum=facts.edgeMaximum;
    physical.transitionMinimum=facts.transitionMinimum;physical.transitionMaximum=facts.transitionMaximum;
    const bool full=measured && nominee.left==0 && nominee.top==0 &&
        nominee.right==source.width && nominee.bottom==source.height;
    physical.confidence=canvas?PhysicalPictureEdgeConfidence::SOURCE_CANVAS_ESTIMATE:
        full?PhysicalPictureEdgeConfidence::SOURCE_RASTER_EXTENT:(facts.filteredEdgeMask || !facts.transitionProfilesComplete)?
        PhysicalPictureEdgeConfidence::BOUNDED_FILTERED_EDGE:PhysicalPictureEdgeConfidence::STRONG_ABRUPT_EDGE;
    physical.reason=facts.reason;
    return result;
}

SourcePictureDetectionResult DetectSourcePictureForWorker(const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget) {
    // Search cheaply in color, refine the nominated brackets at native source
    // coordinates, then demand the same strict current-pixel verification used
    // by the established path. A downsampled proposal alone is never evidence.
    try {
        const auto proposal=ProposeSourcePictureFromCoarseColor(source,budget);
        if(proposal.physicalContinuityContradicted) {
            // A sampled native gap contradicts the continuous picture hypothesis.
            // Do not let a second, differently sampled search overwrite that fact
            // with physical/crop authority for this same source. Unknown keeps
            // current video pixels; ordinary absent proposals still fall back below.
            SourcePictureDetectionResult unresolved;
            unresolved.evidence=MissingSourcePictureEvidence(source,"source-color-native-discontinuity");
            unresolved.evidence.physicalEdges.evaluated=true;
            unresolved.evidence.physicalEdges.samples=proposal.samples;
            auto facts=std::make_shared<SourcePictureObservation>();
            facts->sourceGeneration=source.generation;facts->width=source.width;facts->height=source.height;
            facts->outcome=SourcePictureOutcome::Partial;facts->samples=proposal.samples;
            facts->transitionProfilesComplete=false;facts->reason="source-color-native-discontinuity";
            unresolved.observation=std::move(facts);
            return unresolved;
        }
        if(proposal.available) {
            const auto verified=VerifySuppliedActivePictureCandidate(source,proposal.verificationBounds,budget);
            if(verified.sourceCandidateVerificationComplete &&
                (verified.classification==ActivePictureClassification::BAR_CROP_TRUSTED ||
                 verified.classification==ActivePictureClassification::FULL_RASTER_TRUSTED)) {
                auto facts=std::make_shared<SourcePictureObservation>();
                facts->sourceGeneration=source.generation;facts->width=source.width;facts->height=source.height;
                facts->complete=true;facts->rectangleAvailable=true;facts->outcome=SourcePictureOutcome::Measured;
                facts->measuredBounds=proposal.physicalEstimate;facts->blackReference=verified.blackReference;
                facts->verificationBoundsAvailable=true;facts->verificationBounds=proposal.verificationBounds;
                facts->edgeMinimum=facts->transitionMinimum=proposal.bracketMinimum;
                facts->edgeMaximum=facts->transitionMaximum=proposal.bracketMaximum;
                // No component topology or per-ray transition certificate was
                // produced by this search. Do not fabricate either for temporal reuse.
                facts->transitionProfilesComplete=false;facts->samples=proposal.samples;
                facts->reason="current-color-proposal-native-refinement-and-verification";
                return VerifySourcePictureObservation(source,std::move(facts),budget,&verified);
            }
        }
    } catch(const AnalysisEvaluationCancelled&) {
        SourcePictureDetectionResult cancelled;
        cancelled.evidence=MissingSourcePictureEvidence(source,"source-color-search-budget-cancelled");
        return cancelled; // Never start another scan after this job's deadline.
    }
    // Ambiguous colors, detached overlays or a failed native proof retain the
    // established topology detector; no old geometry or pixels are substituted.
    return VerifySourcePictureObservation(source,
        std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(source,budget)),budget);
}

SourcePictureDetectionResult DetectPreparedSourcePictureBounded(const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& identity,const SourcePictureSupportRequest* supportRequest) {
    const auto started=std::chrono::steady_clock::now();
    auto deadline=started+std::chrono::milliseconds(24);
    AnalysisEvaluationBudget budget([](void* value) {
        return std::chrono::steady_clock::now()>=*static_cast<std::chrono::steady_clock::time_point*>(value);
    },&deadline);
    SourcePictureDetectionResult result;
    static thread_local OwnedSourcePictureObservationCache factsCache;
    try {
        const auto lookupStarted=std::chrono::steady_clock::now();
        auto ready=factsCache.Find(source,identity);
        const double lookupMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lookupStarted).count();
        if(ready) {
            // Byte equality proves only geometry facts. Current exclusion and
            // subtitle verification still run with this frame's own identity.
            result=VerifySourcePictureObservation(source,std::move(ready),&budget);
            result.geometryReused=true;
        } else {
            result=DetectSourcePictureForWorker(source,&budget);
            if(result.observation && result.observation->complete)
                factsCache.Store(source,identity,result.observation,0,true);
        }
        result.geometryLookupMs=lookupMs;
    }
    catch(const AnalysisEvaluationCancelled&) {
        result.evidence=MissingSourcePictureEvidence(source,"source-picture-worker-budget-cancelled");
    }
    // Bind only an explicitly valid current source generation. Both queued
    // format-generation and live transport-generation adapters are supported.
    if(source.IsValid() && identity.transportGeneration && identity.sourceFormatGeneration &&
        identity.acceptedSequence && identity.analysisDomainKey && source.generation &&
        (source.generation==identity.transportGeneration || source.generation==identity.sourceFormatGeneration)) {
        result.evidence.currentSourceIdentity=identity;
        result.evidence.currentSourceIdentityAvailable=true;
    }
    // Current support is optional presentation evidence. Budget failure here
    // cannot erase a completed current physical measurement above.
    auto supportResult=supportRequest?std::make_shared<SourcePictureSupportResult>():nullptr;
    if(supportResult)supportResult->currentSupport.identity=identity; // Terminal negative attempts remain source-bound.
    if(supportRequest && result.evidence.currentSourceIdentityAvailable) {
        try {
            const auto* facts=result.observation.get();
            const auto* measured=facts&&facts->complete&&facts->rectangleAvailable?&facts->measuredBounds:nullptr;
            bool sameMeasured=false;
            if(supportRequest && supportRequest->boundary) {
                supportResult->boundaryEncodingEquivalent=supportRequest->boundary->MatchesCurrent(source,identity,facts,&budget);
                const auto& seed=supportRequest->boundary->MeasuredBounds();
                sameMeasured=measured && measured->left==seed.left && measured->top==seed.top && measured->right==seed.right && measured->bottom==seed.bottom;
            }
            if(supportRequest && supportRequest->physicalHypothesisAvailable) {
                const auto& hypothesis=supportRequest->physicalHypothesis;
                const auto* bound=supportRequest->boundary?&supportRequest->boundary->BoundHypothesis():nullptr;
                const bool boundMatches=bound && hypothesis.rasterWidth==bound->rasterWidth && hypothesis.rasterHeight==bound->rasterHeight &&
                    hypothesis.left==bound->left && hypothesis.top==bound->top && hypothesis.right==bound->right && hypothesis.bottom==bound->bottom;
                supportResult->physicalHypothesisCompatible=(supportResult->boundaryEncodingEquivalent && boundMatches) ||
                    (facts && CurrentSourceTransitionSupportsHypothesis(*facts,hypothesis));
                if(supportResult->physicalHypothesisCompatible)supportResult->retainedPhysicalHypothesis=supportRequest->physicalHypothesis;
            }
            const bool candidateCompatible=supportResult->boundaryEncodingEquivalent || sameMeasured || supportResult->physicalHypothesisCompatible;
            const auto* retained=supportRequest&&supportRequest->retainedSupportAvailable&&candidateCompatible?&supportRequest->retainedSupport:nullptr;
            const auto& native=result.evidence;
            const bool currentNativeSafe=native.sourceCandidateVerificationComplete &&
                (native.classification==ActivePictureClassification::BAR_CROP_TRUSTED || native.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
            if(currentNativeSafe) {
                // This source has already paid for strict native verification.
                // An outward union discards a subset of those proved bars, so
                // it needs no second sampling pass and grants no new native AR.
                auto sampling=native.trustedBounds;
                const bool validRetained=retained && retained->rasterWidth==source.width && retained->rasterHeight==source.height &&
                    retained->left>=0 && retained->top>=0 && retained->right<=source.width && retained->bottom<=source.height &&
                    retained->right>retained->left && retained->bottom>retained->top;
                if(validRetained) {
                    sampling.left=std::min(sampling.left,retained->left);sampling.top=std::min(sampling.top,retained->top);
                    sampling.right=std::max(sampling.right,retained->right);sampling.bottom=std::max(sampling.bottom,retained->bottom);
                    sampling.aspectRatio=double(sampling.right-sampling.left)/(sampling.bottom-sampling.top);
                }
                auto& support=supportResult->currentSupport;support.evaluated=support.verified=true;support.identity=identity;
                support.samplingBounds=sampling;support.verification=native.candidateVerification;
                support.retainedCandidateVerified=validRetained && sampling.left==retained->left &&
                    sampling.top==retained->top && sampling.right==retained->right && sampling.bottom==retained->bottom;support.reason="current-native-proof-covers-support-aperture";
            } else supportResult->currentSupport=VerifyCurrentSourceSupport(source,identity,measured,retained,&budget);
            const auto* hypothesis=supportResult->physicalHypothesisCompatible?&supportResult->retainedPhysicalHypothesis:nullptr;
            const auto* bound=supportRequest->boundary?&supportRequest->boundary->BoundHypothesis():nullptr;
            const bool sameBinding=!hypothesis || (bound && hypothesis->left==bound->left && hypothesis->top==bound->top &&
                hypothesis->right==bound->right && hypothesis->bottom==bound->bottom);
            if(supportResult->boundaryEncodingEquivalent && sameBinding)supportResult->boundaryReference=supportRequest->boundary;
            else if(facts)supportResult->boundaryReference=SourcePictureBoundaryReference::Capture(source,identity,*facts,hypothesis,&budget);
        } catch(const AnalysisEvaluationCancelled&) {
            if(!supportResult->currentSupport.verified)supportResult->currentSupport.reason="current-support-budget-exhausted";
        }
    }
    if(supportRequest) {
        auto supportTrace=AnalysisTraceRecord(identity,nullptr,"source-picture-support",supportResult->currentSupport.reason);
        supportTrace.flags=(supportResult->currentSupport.verified?1u:0u)|(supportResult->currentSupport.retainedCandidateVerified?2u:0u)|
            (supportResult->boundaryEncodingEquivalent?4u:0u)|(supportResult->physicalHypothesisCompatible?8u:0u);
        supportTrace.counters[0]=supportResult->currentSupport.samples;
        if(supportResult->boundaryReference) {
            supportTrace.counters[1]=supportResult->boundaryReference->SeedIdentity().acceptedSequence;
            supportTrace.counters[2]=supportResult->boundaryReference->SeedIdentity().sourceFrameNumber;
            AnalysisTraceRect(supportTrace.previous,supportResult->boundaryReference->MeasuredBounds());
        }
        AnalysisTraceRect(supportTrace.measured,result.evidence.physicalEdges.estimate);
        AnalysisTraceRect(supportTrace.candidate,supportResult->retainedPhysicalHypothesis);
        AnalysisTraceRect(supportTrace.published,supportResult->currentSupport.samplingBounds);
        AnalysisDiagnosticTrace::Emit(supportTrace);
    }
    result.support=std::move(supportResult);
    auto trace=AnalysisTraceRecord(identity,nullptr,"source-picture-active",result.evidence.physicalEdges.reason);
    trace.resultCode=static_cast<int32_t>(result.evidence.detector);
    trace.flags=(result.evidence.available?1u:0u)|(result.observation?2u:0u);
    trace.metrics[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    trace.metrics[1]=result.geometryLookupMs;
    if(result.geometryReused)trace.flags|=64u;
    if(result.observation) {
        const auto& facts=*result.observation;
        trace.flags|=(facts.complete?4u:0u)|(facts.rectangleAvailable?8u:0u)|
            (facts.canvasSupported?16u:0u)|(facts.blankMarginsUnresolved?32u:0u);
        trace.flags|=facts.topologyComplete?512u:0u;
        if(!facts.topologyComplete && facts.components.size()>=512)trace.flags|=1024u;
        trace.counters[0]=facts.samples;trace.counters[1]=facts.components.size();
        trace.counters[2]=static_cast<uint64_t>(facts.outcome);
        const auto& dark=facts.darkTerminal;
        trace.flags|=(dark.evaluated?128u:0u)|(dark.accepted?256u:0u);
        trace.counters[3]=dark.evaluated?uint64_t(dark.side+1):0;
        trace.counters[4]=dark.supportingRays;trace.counters[5]=dark.supportingRegions;
        trace.counters[6]=static_cast<uint64_t>(source.format);trace.counters[7]=static_cast<uint64_t>(source.encoding);
        trace.metrics[2]=facts.blackReference.floor;trace.metrics[3]=facts.blackReference.threshold;
        trace.metrics[4]=dark.earlyMean;trace.metrics[5]=dark.earlyScore;
        trace.metrics[6]=dark.integratedMean;trace.metrics[7]=dark.integratedScore;
        for(unsigned i=0;i<4;++i)trace.metrics[8+i]=dark.regionalMean[i];
        trace.metrics[12]=static_cast<int>(source.range);trace.metrics[13]=static_cast<int>(source.colorspace);
        trace.metrics[14]=dark.lateMean;trace.metrics[15]=facts.lumaQuantum;
        AnalysisTraceRect(trace.measured,facts.measuredBounds);
    }
    if(result.observation) {
        const auto& contact=result.observation->rasterContact;
        trace.flags|=(contact.opposed?2048u:0u)|(contact.sparseEvaluated?4096u:0u)|(contact.sparseAccepted?8192u:0u)|
            (contact.insetVeto?16384u:0u)|(contact.glyphVeto?32768u:0u);
        trace.metrics[16]=contact.peakContrast;trace.metrics[17]=contact.contactPatches;
        trace.metrics[18]=contact.textureRegions;trace.metrics[19]=contact.sparseSide+1;
        for(unsigned i=0;i<4;++i)trace.metrics[20+i]=contact.textureSupport[i];
    }
    if(result.observation)for(unsigned side=0;side<4;++side) {
        const auto& proof=result.observation->relativeMatteContacts[side];
        if(!proof.evaluated)continue;
        auto contact=AnalysisTraceRecord(identity,nullptr,"source-relative-matte-contact",
            proof.accepted?"current-bar-relative-contact":"insufficient-relative-contact-proof");
        contact.flags=(proof.accepted?1u:0u)|(proof.referencesMatched?2u:0u);
        contact.counters[0]=side;contact.counters[1]=proof.coverage;
        contact.counters[2]=proof.support;contact.counters[3]=proof.regions;
        for(unsigned c=0;c<3;++c) {
            contact.metrics[c]=proof.reference[c];contact.metrics[3+c]=proof.noise[c];
            contact.metrics[6+c]=proof.texture[c];
        }
        contact.metrics[9]=proof.lumaMargin;contact.metrics[10]=proof.chromaMargin;
        AnalysisDiagnosticTrace::Emit(contact);
    }
    if(result.observation && result.observation->perpendicularInset.evaluated) {
        const auto& proof=result.observation->perpendicularInset;
        auto contact=AnalysisTraceRecord(identity,nullptr,"source-perpendicular-inset",
            proof.accepted?"current-cross-edge-contact":proof.ambiguous?"ambiguous-cross-edge-insets":"insufficient-cross-edge-contact");
        contact.flags=(proof.accepted?1u:0u)|(proof.ambiguous?2u:0u);
        contact.counters[0]=proof.side;contact.counters[1]=proof.support;
        contact.counters[2]=proof.regions;contact.counters[3]=proof.eligibleCandidates;
        contact.metrics[0]=proof.coordinate;contact.metrics[1]=proof.minimum;contact.metrics[2]=proof.maximum;
        contact.metrics[3]=proof.probeSpacing;
        for(unsigned i=0;i<2;++i) {contact.metrics[4+i]=proof.nearest[i];contact.metrics[6+i]=proof.outside[i];}
        AnalysisDiagnosticTrace::Emit(contact);
    }
    if(result.observation && result.observation->rectangleAvailable) {
        const auto& bounds=result.observation->measuredBounds;
        if(bounds.left || bounds.top || bounds.right<source.width || bounds.bottom<source.height) {
            const auto& evidence=result.evidence;const auto& check=evidence.candidateVerification;
            auto crop=AnalysisTraceRecord(identity,nullptr,"source-crop-verification",evidence.reason.c_str());
            crop.flags=(evidence.sourceCandidateVerificationComplete?1u:0u)|
                (evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED?2u:0u);
            crop.counters[0]=static_cast<uint64_t>(check.vertical);crop.counters[1]=static_cast<uint64_t>(check.horizontal);
            crop.counters[2]=check.samples;crop.counters[3]=static_cast<uint64_t>(evidence.classification);
            crop.metrics[0]=check.firstMismatchX;crop.metrics[1]=check.firstMismatchY;
            const auto* matte=bounds.top?&evidence.top.measuredMatteReference:
                bounds.bottom<source.height?&evidence.bottom.measuredMatteReference:
                bounds.left?&evidence.left.measuredMatteReference:&evidence.right.measuredMatteReference;
            if(check.firstMismatchX>=0 && check.firstMismatchY>=0) {
                if(check.firstMismatchX<bounds.left)matte=&evidence.left.measuredMatteReference;
                else if(check.firstMismatchX>=bounds.right)matte=&evidence.right.measuredMatteReference;
                else if(check.firstMismatchY<bounds.top)matte=&evidence.top.measuredMatteReference;
                else if(check.firstMismatchY>=bounds.bottom)matte=&evidence.bottom.measuredMatteReference;
            }
            if(matte->state==ActivePictureMatteReferenceState::QUALIFIED)crop.flags|=8u;
            crop.metrics[2]=matte->y;crop.metrics[3]=matte->u;crop.metrics[4]=matte->v;
            AnalysisLumaSample pixel;
            if(check.firstMismatchX>=0 && check.firstMismatchY>=0 && source.Sample(check.firstMismatchX,check.firstMismatchY,pixel)) {
                crop.flags|=4u;crop.metrics[5]=pixel.luma;crop.metrics[6]=pixel.chromaU;crop.metrics[7]=pixel.chromaV;
            }
            AnalysisTraceRect(crop.measured,bounds);AnalysisTraceRect(crop.candidate,check.nomination);
            AnalysisTraceRect(crop.published,evidence.trustedBounds);AnalysisDiagnosticTrace::Emit(crop);
        }
    }
    AnalysisTraceRect(trace.published,result.evidence.trustedBounds);
    // Low-rate experimental health telemetry separates completed source facts
    // from accepted crop authority; no pixels or presentation decisions change.
    struct Health {
        uint64_t tick=0,generation=0,jobs=0,measured=0,canvas=0,verified=0,budget=0,reused=0;
        double totalMs=0,peakMs=0,lookupMs=0;
    };
    static thread_local Health health;
    const auto now=GetTickCount64();
    if(!health.tick || health.generation!=source.generation) {
        health={};health.tick=now;health.generation=source.generation;
    }
    ++health.jobs;health.reused+=result.geometryReused;health.lookupMs+=result.geometryLookupMs;
    health.measured+=result.evidence.physicalEdges.estimateAvailable;
    health.canvas+=result.evidence.detector==ActivePictureDetectorKind::SourceSpatialCanvas;
    health.verified+=result.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED;
    health.budget+=result.evidence.reason.find("budget")!=std::string::npos;
    health.totalMs+=trace.metrics[0];health.peakMs=std::max(health.peakMs,trace.metrics[0]);
    if(now-health.tick>=2000) {
        const auto& edge=result.evidence.physicalEdges.estimate;
        DebugLog::Log("VP spatial detector interval: worker=%lu format=%s generation=%llu source=%llu jobs=%llu measured=%llu canvas=%llu unknown=%llu raw_verified=%llu budget_exhausted=%llu geometry_reused=%llu lookup_avg_ms=%.3f retained_bytes=%llu avg_ms=%.3f peak_ms=%.3f last_physical=%d,%d-%d,%d last_reason=%s",
            GetCurrentThreadId(),AnalysisLumaFormatName(source),source.generation,identity.acceptedSequence,
            health.jobs,health.measured,health.canvas,health.jobs-health.measured-health.canvas,health.verified,
            health.budget,health.reused,health.lookupMs/health.jobs,static_cast<unsigned long long>(factsCache.RetainedSourceBytes()),health.totalMs/health.jobs,health.peakMs,edge.left,edge.top,edge.right,edge.bottom,result.evidence.reason.c_str());
        health={};health.tick=now;health.generation=source.generation;
    }
    AnalysisDiagnosticTrace::Emit(trace);return result;
}
