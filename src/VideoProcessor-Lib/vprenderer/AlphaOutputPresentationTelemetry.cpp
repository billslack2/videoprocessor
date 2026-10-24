#include <pch.h>
#include "AlphaOutputPresentationTelemetry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
void Increment(uint64_t& value,uint64_t amount=1) {
    const auto maximum=(std::numeric_limits<uint64_t>::max)();
    value=amount>maximum-value?maximum:value+amount;
}
}

bool AlphaOutputPresentationTelemetry::ValidRate(double hz) {
    return std::isfinite(hz)&&hz>=10.0&&hz<=500.0;
}

bool AlphaOutputPresentationTelemetry::SameRate(double a,double b) {
    // The host's monitor qualification can move slightly between observations.
    return ValidRate(a)&&ValidRate(b)&&std::abs(a-b)/(std::max)(a,b)<=0.001;
}

uint32_t AlphaOutputPresentationTelemetry::IntendedMaximumRefreshes(
    double displayHz,double scheduledOutputHz) {
    if(!ValidRate(displayHz)||!ValidRate(scheduledOutputHz))return 0;
    return static_cast<uint32_t>((std::max)(1.0,std::ceil(displayHz/scheduledOutputHz)));
}

bool AlphaOutputPresentationTelemetry::SameIntent(
    const AlphaOutputSubmission& a,const AlphaOutputSubmission& b) {
    return a.generation==b.generation&&a.presentationEpoch==b.presentationEpoch&&
        a.intendedMaximumRefreshes==b.intendedMaximumRefreshes&&
        SameRate(a.expectedDisplayHz,b.expectedDisplayHz);
}

bool AlphaOutputPresentationTelemetry::SamePoll(
    const AlphaDxgiPresentationSample& a,const AlphaDxgiPresentationSample& b) {
    return a.generation==b.generation&&a.timingStatus==b.timingStatus&&
        a.frameStatisticsResult==b.frameStatisticsResult&&a.available==b.available&&
        a.disjoint==b.disjoint&&a.presentCount==b.presentCount&&
        a.presentRefreshCount==b.presentRefreshCount&&a.syncRefreshCount==b.syncRefreshCount&&
        a.syncQpc==b.syncQpc&&a.qpcFrequency==b.qpcFrequency&&
        (a.expectedDisplayHz==b.expectedDisplayHz||SameRate(a.expectedDisplayHz,b.expectedDisplayHz));
}

void AlphaOutputPresentationTelemetry::ClearRecordsAndAnchor() {
    m_next=m_count=0;m_hasPoll=false;m_hasAnchor=false;
}

void AlphaOutputPresentationTelemetry::BreakObservation(
    AlphaOutputPresentationAvailability availability) {
    m_hasAnchor=false;
    m_snapshot.availability=availability;
    Increment(m_snapshot.observationGaps);
}

void AlphaOutputPresentationTelemetry::ResetContinuity() {
    ClearRecordsAndAnchor();
    m_hasSubmission=false;m_uncertainSubmission=false;
    m_snapshot.availability=AlphaOutputPresentationAvailability::Unavailable;
    m_snapshot.timingStatus=AlphaPresentationTimingStatus::NoSwapchain;
    m_snapshot.frameStatisticsResult=0;
}

void AlphaOutputPresentationTelemetry::ResetSession() {
    ResetContinuity();
    m_snapshot={};
}

bool AlphaOutputPresentationTelemetry::RecordSubmission(
    const AlphaOutputSubmission& submission) {
    if(!submission.generation||!submission.presentId||!submission.intendedMaximumRefreshes||
        !ValidRate(submission.expectedDisplayHz)) {
        m_uncertainSubmission=true;
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        return false;
    }
    if(m_hasSubmission&&submission.generation==m_lastSubmission.generation&&
        submission.presentationEpoch==m_lastSubmission.presentationEpoch&&
        submission.presentId<=m_lastSubmission.presentId) {
        // A failed/occluded Present may leave GetLastPresentCount unchanged.
        // Keep the original intent, and do not correlate it across this doubt.
        m_uncertainSubmission=true;
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        return false;
    }
    if(m_hasSubmission&&!SameIntent(submission,m_lastSubmission)) {
        ClearRecordsAndAnchor();
        Increment(m_snapshot.observationGaps);
        m_snapshot.availability=AlphaOutputPresentationAvailability::Limited;
        m_hasSubmission=false;
    }
    if(m_uncertainSubmission) {
        ClearRecordsAndAnchor();
        m_uncertainSubmission=false;
    }
    m_records[m_next]=submission;
    m_next=(m_next+1)%Capacity;
    m_count=(std::min)(Capacity,m_count+1);
    m_lastSubmission=submission;m_hasSubmission=true;
    if(m_snapshot.availability==AlphaOutputPresentationAvailability::Unavailable)
        m_snapshot.availability=AlphaOutputPresentationAvailability::Warming;
    return true;
}

const AlphaOutputSubmission* AlphaOutputPresentationTelemetry::Find(
    uint64_t generation,uint32_t presentId) const {
    for(size_t offset=0;offset<m_count;++offset) {
        const auto& record=m_records[(m_next+Capacity-1-offset)%Capacity];
        if(record.generation==generation&&record.presentId==presentId)return &record;
    }
    return nullptr;
}

bool AlphaOutputPresentationTelemetry::Observe(const AlphaDxgiPresentationSample& sample) {
    m_snapshot.timingStatus=sample.timingStatus;
    m_snapshot.frameStatisticsResult=sample.frameStatisticsResult;
    if(m_hasPoll&&SamePoll(sample,m_lastPoll))return false;
    m_lastPoll=sample;m_hasPoll=true;
    if(sample.disjoint||sample.timingStatus==AlphaPresentationTimingStatus::Disjoint) {
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        return false;
    }
    if(!sample.available||sample.timingStatus!=AlphaPresentationTimingStatus::Available||
        sample.qpcFrequency<=0||sample.syncQpc<=0||!ValidRate(sample.expectedDisplayHz)) {
        BreakObservation(AlphaOutputPresentationAvailability::Unavailable);
        return false;
    }
    const auto* matched=Find(sample.generation,sample.presentCount);
    if(m_uncertainSubmission||!matched||!SameRate(sample.expectedDisplayHz,matched->expectedDisplayHz)) {
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        return false;
    }
    const auto priorState=m_snapshot.availability;
    if(!m_hasAnchor) {
        m_anchorSubmission=*matched;m_anchorSample=sample;m_hasAnchor=true;
        m_snapshot.availability=priorState==AlphaOutputPresentationAvailability::Limited?
            AlphaOutputPresentationAvailability::Limited:AlphaOutputPresentationAvailability::Warming;
        return true;
    }
    // Never infer the fate of skipped present IDs or wrap counters. Repeated
    // samples with advancing sync fields are also not proof of a repeated image.
    const bool adjacent=m_anchorSample.presentCount<(std::numeric_limits<uint32_t>::max)()&&
        sample.presentCount==m_anchorSample.presentCount+1;
    // Actual scanout refresh and the scheduler clock sample may refer to
    // different instants; queue lateness can put PresentRefresh after SyncRefresh.
    const bool monotonic=sample.presentRefreshCount>m_anchorSample.presentRefreshCount&&
        sample.syncRefreshCount>m_anchorSample.syncRefreshCount&&sample.syncQpc>m_anchorSample.syncQpc;
    bool cadenceValid=false;
    if(monotonic&&sample.qpcFrequency==m_anchorSample.qpcFrequency) {
        const auto refreshes=sample.syncRefreshCount-m_anchorSample.syncRefreshCount;
        const double seconds=static_cast<double>(sample.syncQpc-m_anchorSample.syncQpc)/sample.qpcFrequency;
        const double measured=refreshes/seconds;
        cadenceValid=ValidRate(measured)&&
            std::abs(measured-matched->expectedDisplayHz)/matched->expectedDisplayHz<=0.01;
    }
    if(!monotonic||!cadenceValid||!SameIntent(*matched,m_anchorSubmission)||
        !SameRate(sample.expectedDisplayHz,m_anchorSample.expectedDisplayHz)) {
        // Do not use a contradictory clock/refresh point as the next anchor:
        // its bad counter could otherwise manufacture an excessive interval.
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        return false;
    }
    if(!adjacent||!Find(m_anchorSubmission.generation,m_anchorSubmission.presentId)) {
        BreakObservation(AlphaOutputPresentationAvailability::Limited);
        // Keep the new directly matched point, never the unknown interval.
        m_anchorSubmission=*matched;m_anchorSample=sample;m_hasAnchor=true;
        return true;
    }
    const auto refreshes=sample.presentRefreshCount-m_anchorSample.presentRefreshCount;
    Increment(m_snapshot.verifiedIntervals);
    if(refreshes>matched->intendedMaximumRefreshes) {
        Increment(m_snapshot.observedLongHolds);
        Increment(m_snapshot.excessRefreshes,refreshes-matched->intendedMaximumRefreshes);
    }
    m_snapshot.availability=AlphaOutputPresentationAvailability::Observing;
    m_anchorSubmission=*matched;m_anchorSample=sample;
    return true;
}
