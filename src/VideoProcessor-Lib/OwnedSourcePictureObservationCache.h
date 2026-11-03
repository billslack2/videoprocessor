#pragma once
#include <SourcePictureMeasurement.h>
#include <ActivePictureDecisionTimeline.h>
#include <cstring>
#include <memory>

// One worker-local immutable pixel owner and COMPLETE source facts only. This
// grants no crop permission, caption ownership, history vote or presentation lease.
class OwnedSourcePictureObservationCache {
public:
    static constexpr size_t MaximumSourceBytes=64u*1024u*1024u;
    void Clear() { source_={};identity_={};facts_.reset();continuity_=0; }
    bool Store(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity,
        std::shared_ptr<const SourcePictureObservation> facts,uint64_t continuityGeneration=0) {
        Clear();
        if(!Eligible(source,identity) || !facts || !facts->complete ||
            facts->outcome==SourcePictureOutcome::NotMeasured || facts->sourceGeneration!=source.generation ||
            facts->width!=source.width || facts->height!=source.height)return false;
        source_=source;identity_=identity;facts_=std::move(facts);continuity_=continuityGeneration;
        return true;
    }
    std::shared_ptr<const SourcePictureObservation> Find(const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity,uint64_t continuityGeneration=0) const {
        if(!facts_ || !Eligible(source,identity) || continuityGeneration!=continuity_ ||
            identity.transportGeneration!=identity_.transportGeneration ||
            identity.sourceFormatGeneration!=identity_.sourceFormatGeneration ||
            identity.analysisDomainKey!=identity_.analysisDomainKey ||
            identity.acceptedSequence<identity_.acceptedSequence ||
            source.dataBytes!=source_.dataBytes || source.width!=source_.width || source.height!=source_.height ||
            source.rowBytes!=source_.rowBytes || source.chromaRowBytes!=source_.chromaRowBytes ||
            source.format!=source_.format || source.encoding!=source_.encoding || source.colorspace!=source_.colorspace ||
            source.rgbBitAlignment!=source_.rgbBitAlignment || source.range!=source_.range)return {};
        // Compare every retained byte, including padding. A padding change only
        // loses a hit. A hash or sparse sample cannot establish pixel equality.
        if(std::memcmp(source.data,source_.data,source.dataBytes)!=0)return {};
        auto rebound=std::make_shared<SourcePictureObservation>(*facts_);
        rebound->sourceGeneration=source.generation;
        return rebound;
    }
    size_t RetainedSourceBytes() const { return facts_?source_.dataBytes:0; }
private:
    static bool Eligible(const AnalysisLumaSource& source,const ActivePictureFrameIdentity& identity) {
        return source.backingOwner && source.dataBytes<=MaximumSourceBytes && source.IsValid() && source.generation &&
            identity.transportGeneration && identity.sourceFormatGeneration && identity.analysisDomainKey && identity.acceptedSequence;
    }
    AnalysisLumaSource source_;
    ActivePictureFrameIdentity identity_;
    std::shared_ptr<const SourcePictureObservation> facts_;
    uint64_t continuity_=0;
};
