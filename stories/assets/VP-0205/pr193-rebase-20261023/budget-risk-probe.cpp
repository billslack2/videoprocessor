#include "../../src/VideoProcessor-Lib/vprenderer/PresentationAnalysisBudget.h"
#include <cstdio>
int main() {
    constexpr int64_t frequency = 1000000, start = 1000000;
    PresentationAnalysisBudget budget;
    budget.Observe(1, start, start+1000, frequency, 60, true, 1.0);
    budget.ObserveGpu(1, start+1000, frequency, 8.0);
    // A later cheap result is fresh, while the old 8ms peak remains in history.
    const int64_t sync = start+500000, observed = sync+1000;
    budget.Observe(1, sync, observed, frequency, 60, true, 1.0);
    budget.ObserveGpu(1, observed, frequency, 2.0);
    const auto t = budget.Begin(1, sync+11667, frequency, false, false);
    std::printf("{\"recentCpuMs\":1,\"recentGpuMs\":2,\"retainedGpuPeakMs\":%.6f,\"nextBoundaryLeadMs\":%.6f,\"targetLeadMs\":%.6f,\"reserveMs\":%.6f,\"allowanceMs\":%.6f,\"skippedBoundary\":%s}\n",t.gpuTailMs,t.nextBoundaryLeadMs,t.targetLeadMs,t.reserveMs,t.allowanceMs,t.skippedBoundary?"true":"false");
}