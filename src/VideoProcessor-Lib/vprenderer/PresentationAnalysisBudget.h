#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Uses observed display phase only to admit optional analysis. Never waits,
// changes source PTS, or advances a crop decision. A ticket has a fixed deadline:
// missing it cannot roll work forward into another display interval.
class PresentationAnalysisBudget {
public:
    struct Ticket {
        int64_t deadlineQpc=0;
        double allowanceMs=0;
        double nextBoundaryLeadMs=0;
        double targetLeadMs=0;
        double mandatoryMs=0;
        double reserveMs=0;
        bool phaseKnown=false;
        bool skippedBoundary=false;
    };
    void Observe(uint64_t generation, int64_t sync, int64_t now, int64_t frequency,
        double hz, bool stable, double presentationWorkMs) {
        if (generation != epoch) { reserveMs=2.0; mandatoryMs=0.0; epoch=generation; }
        if (std::isfinite(presentationWorkMs) && presentationWorkMs>=0) {
            mandatoryMs=presentationWorkMs;
            reserveMs=(std::max)(2.0,(std::max)(presentationWorkMs*1.25,reserveMs*0.95));
        }
        syncQpc=0;
        if (!stable || frequency<=0 || sync<=0 || sync>now || !std::isfinite(hz) || hz<10 || hz>240) return;
        const double period=double(frequency)/hz;
        if (double(now-sync)>period*4) return;
        syncQpc=sync; frequencyQpc=frequency; periodQpc=period;
    }
    Ticket Begin(uint64_t generation, int64_t now, int64_t frequency, bool repeated, bool backlog) const {
        Ticket ticket;
        if (repeated || backlog || frequency<=0 || now<=0) return ticket;
        double allowance=1.0; // Unqualified display timing: bounded cache/dispatch work only.
        if (generation==epoch && syncQpc>0 && frequency==frequencyQpc && now>=syncQpc &&
            double(now-syncQpc)<=periodQpc*4) {
            ticket.phaseKnown=true;
            ticket.mandatoryMs=mandatoryMs;
            ticket.reserveMs=reserveMs;
            const double periodMs=periodQpc*1000.0/frequency;
            const double elapsed=double(now-syncQpc);
            ticket.nextBoundaryLeadMs=(periodQpc-std::fmod(elapsed,periodQpc))*1000.0/frequency;
            ticket.targetLeadMs=ticket.nextBoundaryLeadMs;
            // If measured mandatory preparation/rendering cannot reach the
            // immediate vblank even without optional work, the following
            // boundary is the earliest attainable target. A safety reserve
            // alone never moves a presentation to another boundary.
            if (mandatoryMs>=periodMs) {
                allowance=0.0;
            } else {
                if (mandatoryMs>ticket.nextBoundaryLeadMs) {
                    ticket.targetLeadMs+=periodMs;
                    ticket.skippedBoundary=true;
                }
                allowance=(std::min)(2.0,ticket.targetLeadMs-reserveMs);
            }
        }
        ticket.allowanceMs=(std::max)(0.0,allowance);
        if (ticket.allowanceMs>=0.1)
            ticket.deadlineQpc=now+int64_t(ticket.allowanceMs*frequency/1000.0);
        return ticket;
    }
private:
    uint64_t epoch=0;
    int64_t syncQpc=0,frequencyQpc=0;
    double periodQpc=0,reserveMs=2.0,mandatoryMs=0.0;
};