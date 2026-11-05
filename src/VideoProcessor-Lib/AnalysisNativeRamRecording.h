#pragma once
#include "AnalysisDiagnosticTrace.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <thread>

// Opt-in diagnostic acquisition only. The video producer neither copies pixels
// nor waits for this worker or storage. Two source owners include the active copy.
// Private RAM is a rolling window, not a claim that every input was recorded.
class AnalysisNativeRamRecording final {
public:
    static constexpr uint64_t MaxBytes=16ull*1024*1024*1024;
    static constexpr size_t MaxEvents=65536;
    enum class Outcome { Pending, IngressBusy, RetainFailed, Copied, Evicted,
        Duplicate, ContextChanged, Invalid, CopyFailed, Written, WriteFailed, Cancelled };
    struct Event {
        AnalysisDiagnosticFramePacketV1 frame{};
        Outcome outcome=Outcome::Pending;
    };
    struct Saved {
        size_t event=0;
        std::unique_ptr<uint8_t[]> pixels;
        size_t bytes=0;
    };
private:
    static constexpr uint64_t Closed=uint64_t(1)<<63;
    struct Item {AnalysisDiagnosticFramePacketV1 frame{};void* owner=nullptr;size_t event=0;};
    struct Cell {std::atomic<uint64_t> sequence{0};Item item;};
    std::array<Cell,2> m_cells;
    std::atomic<uint64_t> m_enqueue{0},m_gate{0},m_eventCount{0},m_owners{0};
    uint64_t m_dequeue=0,m_allocated=0;
    const uint64_t m_budget,m_maxFrame;
    std::unique_ptr<Event[]> m_events;
    std::deque<Saved> m_saved;
    AnalysisDiagnosticFramePacketV1 m_first{};
    uint64_t m_lastAccepted=0,m_lastSource=0,m_lastTimestamp=0;
    std::atomic<bool> m_closed{false},m_cancel{false};
    bool m_abortCopies=false;
    std::atomic<uint64_t> m_copied{0},m_ownerPeak{0},m_unlogged{0};
    const char* m_endReason="stopped";
    HANDLE m_wake=nullptr;
    std::thread m_worker;
    static AnalysisDiagnosticFramePacketV1 Metadata(AnalysisDiagnosticFramePacketV1 f) noexcept {
        f.data=nullptr;f.ownerContext=nullptr;f.retainOwner=nullptr;f.releaseOwner=nullptr;return f;
    }
    bool Pop(Item& item) noexcept {
        auto& cell=m_cells[m_dequeue%2];
        if(cell.sequence.load(std::memory_order_acquire)!=m_dequeue+1)return false;
        item=cell.item;cell.sequence.store(m_dequeue+2,std::memory_order_release);++m_dequeue;return true;
    }
    bool SameContext(const AnalysisDiagnosticFramePacketV1& b) const noexcept {
        const auto& a=m_first;
        return a.transportGeneration==b.transportGeneration && a.sourceFormatGeneration==b.sourceFormatGeneration &&
            a.viewportGeneration==b.viewportGeneration && a.rendererGeneration==b.rendererGeneration &&
            a.analysisDomainKey==b.analysisDomainKey && a.policyGeneration==b.policyGeneration &&
            a.continuityGeneration==b.continuityGeneration && a.width==b.width && a.height==b.height &&
            a.stride0==b.stride0 && a.stride1==b.stride1 && a.format==b.format && a.encoding==b.encoding &&
            a.colorspace==b.colorspace && a.analysisSampleRange==b.analysisSampleRange &&
            a.rgbBitAlignment==b.rgbBitAlignment && a.dataBytes==b.dataBytes &&
            a.frameDuration100ns==b.frameDuration100ns && a.scriptHint==b.scriptHint &&
            a.nearBarDistance==b.nearBarDistance && a.optimizationMode==b.optimizationMode &&
            (a.captureTimestamp!=0)==(b.captureTimestamp!=0);
    }
    void Copy(Item& item) {
        auto& event=m_events[item.event];const auto& f=item.frame;
        if(m_abortCopies || m_cancel.load(std::memory_order_acquire)){event.outcome=Outcome::Cancelled;return;}
        if((f.schemaVersion && f.schemaVersion!=1) || (f.structSize && f.structSize<sizeof(f)) || !f.data || !f.dataBytes || f.dataBytes>m_maxFrame || f.dataBytes>m_budget ||
            !f.acceptedSequence || f.width<=0 || f.height<=0){
            event.outcome=Outcome::Invalid;m_endReason="invalid-source";m_abortCopies=true;Stop();return;
        }
        if(m_first.acceptedSequence) {
            if(!SameContext(f) || f.discontinuity || f.acceptedSequence<m_lastAccepted ||
                (f.sourceFrameNumber && f.sourceFrameNumber<m_lastSource) ||
                (f.captureTimestamp && f.captureTimestamp<m_lastTimestamp)) {
                event.outcome=Outcome::ContextChanged;m_endReason="source-context-changed";m_abortCopies=true;Stop();return;
            }
            if(f.acceptedSequence==m_lastAccepted || f.cadenceRepeat ||
                (f.sourceFrameNumber && f.sourceFrameNumber==m_lastSource)) {
                event.outcome=Outcome::Duplicate;return;
            }
        } else m_first=Metadata(f);
        m_lastAccepted=f.acceptedSequence;m_lastSource=f.sourceFrameNumber;m_lastTimestamp=f.captureTimestamp;
        Saved current;current.event=item.event;current.bytes=static_cast<size_t>(f.dataBytes);
        if(m_allocated+f.dataBytes>m_budget) {
            // Format is fixed for the session, hence the oldest allocation fits.
            current.pixels=std::move(m_saved.front().pixels);
            m_events[m_saved.front().event].outcome=Outcome::Evicted;m_saved.pop_front();
        } else {
            MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
            if(!GlobalMemoryStatusEx(&memory) || memory.ullAvailPhys<f.dataBytes+1024ull*1024*1024)
                throw std::bad_alloc();
            current.pixels.reset(new uint8_t[current.bytes]);m_allocated+=current.bytes;
        }
        memcpy(current.pixels.get(),f.data,current.bytes);
        m_saved.push_back(std::move(current));event.outcome=Outcome::Copied;
        m_copied.fetch_add(1,std::memory_order_release);
    }
    void Run() noexcept {
        for(;;) {
            Item item;
            if(!Pop(item)) {
                if(m_closed.load(std::memory_order_acquire) && !m_owners.load(std::memory_order_acquire) &&
                    !(m_gate.load(std::memory_order_acquire)&~Closed))break;
                WaitForSingleObject(m_wake,25);continue;
            }
            if(item.owner) {
                try {Copy(item);} catch(...) {
                    m_events[item.event].outcome=Outcome::CopyFailed;m_endReason="copy-or-memory-failure";m_abortCopies=true;Stop();
                }
                item.frame.releaseOwner(item.owner);
            }
            m_owners.fetch_sub(1,std::memory_order_release);
        }
    }
public:
    explicit AnalysisNativeRamRecording(uint64_t bytes,uint64_t maxFrame=AnalysisDiagnosticTrace::MAX_FRAME_BYTES)
        :m_budget(bytes),m_maxFrame(maxFrame) {
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
        if(!bytes || bytes>MaxBytes || !GlobalMemoryStatusEx(&memory) ||
            bytes+sizeof(Event)*MaxEvents>memory.ullAvailPhys/2)
            throw std::invalid_argument("native RAM budget unavailable");
        m_events.reset(new Event[MaxEvents]);
        m_cells[0].sequence=0;m_cells[1].sequence=1;
        m_wake=CreateEventA(nullptr,FALSE,FALSE,nullptr);
        if(!m_wake)throw std::runtime_error("native RAM wake event unavailable");
        try{m_worker=std::thread([this]{Run();});}catch(...){CloseHandle(m_wake);throw;}
    }
    ~AnalysisNativeRamRecording(){Cancel();Join();CloseHandle(m_wake);}
    AnalysisNativeRamRecording(const AnalysisNativeRamRecording&)=delete;
    AnalysisNativeRamRecording& operator=(const AnalysisNativeRamRecording&)=delete;
    bool Active() const noexcept {return !m_closed.load(std::memory_order_acquire);}
    void Stop() noexcept {m_gate.fetch_or(Closed,std::memory_order_acq_rel);m_closed.store(true,std::memory_order_release);SetEvent(m_wake);}
    void Cancel() noexcept {m_cancel.store(true,std::memory_order_release);Stop();}
    void Join(){Stop();if(m_worker.joinable())m_worker.join();}
    // Only the copier/writer calls Join. This bounded producer has no recorder mutex.
    void Offer(const AnalysisDiagnosticFramePacketV1& f) noexcept {
        const auto gate=m_gate.fetch_add(1,std::memory_order_acq_rel);
        if(gate&Closed){m_gate.fetch_sub(1,std::memory_order_release);return;}
        struct Lease {AnalysisNativeRamRecording& s;~Lease(){s.m_gate.fetch_sub(1,std::memory_order_release);SetEvent(s.m_wake);}} lease{*this};
        const auto index=m_eventCount.fetch_add(1,std::memory_order_relaxed);
        if(index>=MaxEvents){m_unlogged.fetch_add(1);Stop();return;}
        auto& event=m_events[index];event.frame=Metadata(f);event.outcome=Outcome::IngressBusy;
        if(!f.data || !f.dataBytes || f.dataBytes>m_maxFrame || f.dataBytes>m_budget ||
            !f.acceptedSequence || f.width<=0 || f.height<=0 ||
            (f.schemaVersion && f.schemaVersion!=1) || (f.structSize && f.structSize<sizeof(f))) {
            event.outcome=Outcome::Invalid;return;
        }
        auto owners=m_owners.load(std::memory_order_relaxed);
        if(owners>=2 || !m_owners.compare_exchange_strong(owners,owners+1,std::memory_order_acq_rel))return;
        auto peak=m_ownerPeak.load();while(peak<owners+1 && !m_ownerPeak.compare_exchange_weak(peak,owners+1)){}
        auto position=m_enqueue.load(std::memory_order_relaxed);Cell* cell=nullptr;
        for(unsigned n=0;n<2;++n) {
            auto& candidate=m_cells[position%2];const auto sequence=candidate.sequence.load(std::memory_order_acquire);
            if(sequence==position && m_enqueue.compare_exchange_strong(position,position+1)){cell=&candidate;break;}
            if(sequence<position)break;position=m_enqueue.load(std::memory_order_relaxed);
        }
        if(!cell){m_owners.fetch_sub(1,std::memory_order_release);return;}
        Item item;item.frame=f;item.event=static_cast<size_t>(index);
        if(f.ownerContext && f.retainOwner && f.releaseOwner)try{item.owner=f.retainOwner(f.ownerContext);}catch(...){}
        event.outcome=item.owner?Outcome::Pending:Outcome::RetainFailed;
        item.frame.ownerContext=nullptr;cell->item=item;
        cell->sequence.store(position+1,std::memory_order_release);SetEvent(m_wake);
    }
    uint64_t Copied() const noexcept{return m_copied.load(std::memory_order_acquire);}
    uint64_t OwnerPeak() const noexcept{return m_ownerPeak.load();}
    uint64_t AllocatedBytesAfterJoin() const noexcept{return m_allocated;}
    size_t EventCountAfterJoin() const noexcept{return static_cast<size_t>(std::min<uint64_t>(MaxEvents,m_eventCount.load()));}
    const Event& EventAfterJoin(size_t index) const{return m_events[index];}
    // Caller joins first; no concurrent mutation while writing. Callback returns
    // Written/WriteFailed/Cancelled. A failed write stops subsequent disk offers.
    template<class Write> void DrainAfterJoin(Write write) {
        bool failed=false;
        for(auto& saved:m_saved) {
            auto& event=m_events[saved.event];
            if(failed || m_cancel.load())event.outcome=Outcome::Cancelled;
            else {
                auto f=event.frame;f.data=saved.pixels.get();
                event.outcome=write(f,saved.event+1);
                failed=event.outcome!=Outcome::Written;
            }
            saved.pixels.reset();
        }
        m_saved.clear();
    }
    static const char* Name(Outcome value) noexcept {
        switch(value){
        case Outcome::Pending:return "pending";case Outcome::IngressBusy:return "ingress-busy";
        case Outcome::RetainFailed:return "retain-failed";case Outcome::Copied:return "resident";
        case Outcome::Evicted:return "ram-evicted";case Outcome::Duplicate:return "duplicate";
        case Outcome::ContextChanged:return "context-changed";case Outcome::Invalid:return "invalid-source";
        case Outcome::CopyFailed:return "copy-failed";case Outcome::Written:return "written";
        case Outcome::WriteFailed:return "write-failed";case Outcome::Cancelled:return "cancelled";
        }return "invalid";
    }
    void WriteIndexAfterJoin(std::ostream& out) const {
        out<<"{\"kind\":\"limits\",\"schema\":1,\"pixelBudgetBytes\":"<<m_budget
            <<",\"acquisitionTap\":\"accepted-input\",\"pictureBoundsAvailable\":false,\"semanticSettingsAvailable\":false"
            <<",\"metadataBytes\":"<<sizeof(Event)*MaxEvents<<",\"allocatedPixelBytes\":"<<m_allocated
            <<",\"sourceOwnerPeak\":"<<OwnerPeak()<<",\"unloggedOffers\":"<<m_unlogged.load()
            <<",\"endReason\":\""<<m_endReason<<"\",\"completeStreamClaim\":false}\n";
        for(size_t i=0;i<EventCountAfterJoin();++i){const auto& e=m_events[i];const auto& f=e.frame;
            out<<"{\"index\":"<<i+1<<",\"outcome\":\""<<Name(e.outcome)<<"\",\"transportGeneration\":"<<f.transportGeneration
                <<",\"acceptedSequence\":"<<f.acceptedSequence<<",\"sourceFrameNumber\":"<<f.sourceFrameNumber
                <<",\"captureTimestamp\":"<<f.captureTimestamp<<",\"sourceFormatGeneration\":"<<f.sourceFormatGeneration
                <<",\"viewportGeneration\":"<<f.viewportGeneration<<",\"rendererGeneration\":"<<f.rendererGeneration
                <<",\"analysisDomainKey\":"<<f.analysisDomainKey<<",\"policyGeneration\":"<<f.policyGeneration
                <<",\"continuityGeneration\":"<<f.continuityGeneration<<",\"bytes\":"<<f.dataBytes
                <<",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"stride0\":"<<f.stride0<<",\"stride1\":"<<f.stride1
                <<",\"format\":"<<f.format<<",\"encoding\":"<<f.encoding<<",\"colorspace\":"<<f.colorspace
                <<",\"sampleRange\":"<<f.analysisSampleRange<<",\"rgbBitAlignment\":"<<f.rgbBitAlignment
                <<",\"frameDuration100ns\":"<<f.frameDuration100ns<<",\"discontinuity\":"<<f.discontinuity<<"}\n";
        }
    }
};
