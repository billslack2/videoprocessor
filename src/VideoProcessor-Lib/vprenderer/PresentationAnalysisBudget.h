#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Render-thread-owned scheduling estimate, not a deadline guarantee. Uses actual
// display phase without waiting, changing source PTS, or extending a ticket.
class PresentationAnalysisBudget {
public:
    struct Ticket {
        int64_t deadlineQpc=0;
        double allowanceMs=0;
        double nextBoundaryLeadMs=0;
        double targetLeadMs=0;
        double mandatoryMs=0;
        double mandatoryTailMs=0;
        double mandatoryEnvelopeMs=0;
        double gpuTailMs=0;
        double reserveMs=0;
        bool phaseKnown=false;
        bool mandatoryKnown=false;
        bool gpuKnown=false;
        bool skippedBoundary=false;
    };
    // A repeated temporal output may reuse upload/preparation. It must not
    // replace the cost estimate for the next new source's first output.
    void Observe(uint64_t generation, int64_t sync, int64_t now, int64_t frequency,
        double hz, bool stable, double presentationWorkMs, bool repeatedOutput=false) {
        if (frequency<=0 || now<=0) { Reset(generation,0); return; }
        if (generation!=epoch || frequency!=frequencyQpc ||
            (lastObservationQpc>0 && now<(std::max)(lastObservationQpc,lastGpuQpc))) Reset(generation,frequency);
        lastObservationQpc=now;
        AdvanceHistory(now,frequency);
        if (std::isfinite(presentationWorkMs) && presentationWorkMs>=0) {
            auto& bucket=history[historyIndex];
            auto& peak=repeatedOutput?bucket.repeatedPeakMs:bucket.newSourcePeakMs;
            peak=(std::max)(peak,presentationWorkMs);
            bucket.lastQpc=now;
            if (!repeatedOutput) { newSourceMs=presentationWorkMs; newSourceQpc=now; }
        }
        syncQpc=0;
        if (!stable || sync<=0 || sync>now || !std::isfinite(hz) || hz<10 || hz>240) return;
        const double period=double(frequency)/hz;
        if (double(now-sync)>period*4) return;
        syncQpc=sync; periodQpc=period;
    }
    // Feed only already-resolved, exact-generation GPU stage-sum timings.
    // This neither queries nor waits for the GPU. A late old-generation result
    // cannot initialize/reset the current CPU/phase epoch.
    // Runtime supplies the query's actual issue time. The default is only a
    // convenient synthetic observation for deterministic policy tests.
    void ObserveGpu(uint64_t generation,int64_t now,int64_t frequency,double gpuMs,
        int64_t issuedQpc=0) {
        if (generation!=epoch || frequency<=0 || frequency!=frequencyQpc || now<=0 ||
            now<(std::max)(lastObservationQpc,lastGpuQpc) ||
            !std::isfinite(gpuMs) || gpuMs<0) return;
        if (!issuedQpc) issuedQpc=now;
        if (issuedQpc<=0 || issuedQpc>now || syncQpc<=0 || periodQpc<=0 ||
            now<syncQpc || double(now-syncQpc)>periodQpc*4 ||
            double(now-issuedQpc)>periodQpc*4) return;
        AdvanceHistory(now,frequency);
        auto& bucket=history[historyIndex];
        bucket.gpuPeakMs=(std::max)(bucket.gpuPeakMs,gpuMs);
        bucket.gpuQpc=(std::max)(bucket.gpuQpc,issuedQpc);
        lastGpuIssuedQpc=(std::max)(lastGpuIssuedQpc,issuedQpc);
        lastGpuQpc=now;
    }
    Ticket Begin(uint64_t generation, int64_t now, int64_t frequency, bool repeated, bool backlog) const {
        Ticket ticket;
        if (repeated || backlog || frequency<=0 || now<=0) return ticket;
        double allowance=1.0; // Unknown phase: retain the conservative fallback.
        if (generation==epoch && syncQpc>0 && frequency==frequencyQpc &&
            now>=(std::max)(lastObservationQpc,lastGpuQpc) && now>=syncQpc && double(now-syncQpc)<=periodQpc*4) {
            ticket.phaseKnown=true;
            ticket.mandatoryKnown=Recent(newSourceQpc,now,frequency);
            ticket.mandatoryMs=ticket.mandatoryKnown?newSourceMs:0.0;
            for (const auto& bucket:history) {
                if (Recent(bucket.lastQpc,now,frequency))
                    ticket.mandatoryTailMs=(std::max)(ticket.mandatoryTailMs,
                        (std::max)(bucket.newSourcePeakMs,bucket.repeatedPeakMs));
                if (Recent(bucket.gpuQpc,now,frequency)) {
                    ticket.gpuTailMs=(std::max)(ticket.gpuTailMs,bucket.gpuPeakMs);
                }
            }
            ticket.gpuKnown=lastGpuIssuedQpc>0 && now>=lastGpuIssuedQpc &&
                double(now-lastGpuIssuedQpc)<=periodQpc*4;
            // A stale GPU tail may conservatively retain reserve, but cannot
            // establish that a currently reachable boundary should be skipped.
            ticket.mandatoryEnvelopeMs=ticket.mandatoryMs+(ticket.gpuKnown?ticket.gpuTailMs:0.0);
            // CPU submission and GPU execution overlap, so summing their tails
            // can double-count work. Prefer that conservative envelope to
            // spending scanout slack that a low-CPU/high-GPU frame still needs.
            // This remains an estimate, never a guarantee about future work.
            ticket.reserveMs=(std::max)(2.0,(ticket.mandatoryTailMs+ticket.gpuTailMs)*1.25);
            const double periodMs=periodQpc*1000.0/frequency;
            const double elapsed=double(now-syncQpc);
            ticket.nextBoundaryLeadMs=(periodQpc-std::fmod(elapsed,periodQpc))*1000.0/frequency;
            ticket.targetLeadMs=ticket.nextBoundaryLeadMs;
            if (ticket.mandatoryEnvelopeMs>=periodMs) {
                allowance=0.0;
            } else {
                // Only measured mandatory work may establish that the immediate
                // boundary is unreachable. A safety reserve cannot move it.
                if (ticket.mandatoryKnown && ticket.mandatoryEnvelopeMs>ticket.nextBoundaryLeadMs) {
                    ticket.targetLeadMs+=periodMs;
                    ticket.skippedBoundary=true;
                }
                allowance=ticket.targetLeadMs-ticket.reserveMs;
                // Missing/stale GPU timing or only a repeat CPU observation
                // cannot qualify an unrestricted new-source allowance.
                if (!ticket.mandatoryKnown || !ticket.gpuKnown) allowance=(std::min)(1.0,allowance);
            }
        }
        ticket.allowanceMs=(std::max)(0.0,allowance);
        if (ticket.allowanceMs>=0.1)
            ticket.deadlineQpc=now+int64_t(ticket.allowanceMs*frequency/1000.0);
        return ticket;
    }
private:
    // Eight quarter-second maxima retain roughly two seconds of tails at both
    // film and high source rates. Storage/work stay bounded independently of Hz.
    struct HistoryBucket { double newSourcePeakMs=0,repeatedPeakMs=0,gpuPeakMs=0; int64_t lastQpc=0,gpuQpc=0; };
    std::array<HistoryBucket,8> history{};
    size_t historyIndex=0;
    uint64_t epoch=0;
    int64_t syncQpc=0,frequencyQpc=0,lastObservationQpc=0,lastGpuQpc=0,lastGpuIssuedQpc=0;
    int64_t historyStartQpc=0,newSourceQpc=0;
    double periodQpc=0,newSourceMs=0;
    static bool Recent(int64_t sample,int64_t now,int64_t frequency) {
        return sample>0 && now>=sample && double(now-sample)*1000.0/frequency<=2000.0;
    }
    void Reset(uint64_t generation,int64_t frequency) {
        epoch=generation;frequencyQpc=frequency;syncQpc=0;lastObservationQpc=0;lastGpuQpc=0;lastGpuIssuedQpc=0;
        historyStartQpc=0;historyIndex=0;history={};newSourceQpc=0;newSourceMs=0;periodQpc=0;
    }
    void AdvanceHistory(int64_t now,int64_t frequency) {
        if (!historyStartQpc) { historyStartQpc=now; return; }
        const int64_t bucketTicks=(std::max)(int64_t{1},frequency/4);
        const uint64_t steps=static_cast<uint64_t>((now-historyStartQpc)/bucketTicks);
        if (steps>=history.size()) {
            history={};historyIndex=0;historyStartQpc=now;
        } else {
            for (uint64_t i=0;i<steps;++i) {
                historyIndex=(historyIndex+1)%history.size();history[historyIndex]={};
            }
            historyStartQpc+=static_cast<int64_t>(steps)*bucketTicks;
        }
    }
};
