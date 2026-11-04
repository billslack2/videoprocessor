#pragma once
#include <SubtitleBoxLookahead.h>
#include <atomic>
#include <memory>

// A source ticket freezes search intent, not evidence. Native measurements,
// current ink and cleanup receipts still need their existing current-pixel
// validation. A later availability change cannot rename an already queued scan.
class SourceAnalysisTicket final {
public:
    explicit SourceAnalysisTicket(const SubtitleBoxObservation& request)
        : m_identity(request.identity),m_nomination(request.sharedPicture),
          m_policy(request.policyGeneration),m_continuity(request.continuityGeneration),
          m_width(request.width),m_height(request.height),m_nearBar(request.nearBarDistance),
          m_optimization(request.optimizationMode),m_script(request.scriptHint),
          m_stride(request.analysisStride),m_discontinuity(request.discontinuity) {}
    bool Valid() const {
        return m_identity.transportGeneration && m_identity.sourceFormatGeneration &&
            m_identity.analysisDomainKey && m_identity.acceptedSequence &&
            m_width>0 && m_height>0 && (m_stride==1 || m_stride==2);
    }
    bool MatchesRequestContext(const SubtitleBoxObservation& request) const {
        return Valid() && SameAnalysisFrameIdentity(m_identity,request.identity) &&
            m_policy==request.policyGeneration && m_continuity==request.continuityGeneration &&
            m_width==request.width && m_height==request.height &&
            m_nearBar==request.nearBarDistance && m_optimization==request.optimizationMode &&
            m_script==request.scriptHint && m_stride==request.analysisStride &&
            m_discontinuity==request.discontinuity;
    }
    bool BindRequest(SubtitleBoxObservation& request) const {
        // Never relabel completed pixels or turn a result into a request.
        if(!MatchesRequestContext(request) || request.analyzed || request.ink ||
            request.pendingRefresh || request.analysisRefresh)return false;
        request.sharedPicture=m_nomination;
        return true;
    }
    bool Accepts(const SubtitleBoxObservation& result) const {
        return MatchesRequestContext(result) && result.sharedPicture.Matches(m_nomination);
    }
    const SubtitlePictureAuthority& Nomination() const {return m_nomination;}
    // The slot belongs to one QueuedFrame. Bounded CAS publication does no pixel
    // work and never waits for optional workers. Context rollback cannot revive
    // an old ticket. Old jobs retain their immutable ticket and fail selection.
    static std::shared_ptr<const SourceAnalysisTicket> Acquire(
        std::shared_ptr<const SourceAnalysisTicket>* slot,const SubtitleBoxObservation& request) {
        if(!slot)return {};
        auto existing=std::atomic_load(slot);
        for(unsigned attempt=0;attempt<2;++attempt) {
            if(existing && existing->MatchesRequestContext(request))return existing;
            if(existing && (!SameAnalysisFrameIdentity(existing->m_identity,request.identity) ||
                request.policyGeneration<existing->m_policy ||
                request.continuityGeneration<existing->m_continuity ||
                (request.policyGeneration==existing->m_policy &&
                    request.continuityGeneration==existing->m_continuity)))return {};
            auto next=std::make_shared<const SourceAnalysisTicket>(request);
            if(!next->Valid())return {};
            if(std::atomic_compare_exchange_strong(slot,&existing,next))return next;
        }
        return {};
    }
private:
    ActivePictureFrameIdentity m_identity;
    SubtitlePictureAuthority m_nomination;
    uint64_t m_policy=0,m_continuity=0;
    int m_width=0,m_height=0,m_nearBar=0,m_optimization=0;
    SubtitleScriptHint m_script=SubtitleScriptHint::Latin;
    unsigned m_stride=1;
    bool m_discontinuity=false;
};
