#pragma once
#include <atomic>
#include <chrono>
#include <Windows.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// Optional immutable measurements only. The presentation thread never waits for
// a job or for this worker's mutex. Each slot belongs to one captured frame.
template<class Result, size_t Capacity = 8>
class OptionalAnalysisWorker {
    static_assert(Capacity>0,"Optional work queue must be bounded and nonempty");
public:
    struct Slot {
        std::shared_ptr<const Result> ready;
        std::atomic<bool> pending{false};
    };
    struct Metrics {
        uint64_t submitted=0,completed=0,failed=0,duplicate=0,contended=0,full=0,cancelled=0;
        uint64_t queueUs=0,wallUs=0,cpuUs=0,cpuSamples=0,queuePeakUs=0,wallPeakUs=0;
        uint64_t queued=0,queueHighWater=0,activeAgeUs=0;
    };
    // Approximate observational snapshot; never used to admit evidence or wait.
    Metrics Snapshot() const {
        Metrics m;
#define VP_WORKER_READ(name) m.name=name.load(std::memory_order_relaxed)
        VP_WORKER_READ(submitted);VP_WORKER_READ(completed);VP_WORKER_READ(failed);
        VP_WORKER_READ(duplicate);VP_WORKER_READ(contended);VP_WORKER_READ(full);VP_WORKER_READ(cancelled);
        VP_WORKER_READ(queueUs);VP_WORKER_READ(wallUs);VP_WORKER_READ(cpuUs);VP_WORKER_READ(cpuSamples);
        VP_WORKER_READ(queuePeakUs);VP_WORKER_READ(wallPeakUs);VP_WORKER_READ(queued);VP_WORKER_READ(queueHighWater);
#undef VP_WORKER_READ
        const auto start=activeStartUs.load(std::memory_order_relaxed),now=NowUs();
        m.activeAgeUs=start && now>=start ? now-start : 0;
        return m;
    }
    ~OptionalAnalysisWorker() { Stop(); }
    bool Submit(const std::shared_ptr<Slot>& slot, uint64_t generation,
        uint64_t sequence, uint64_t minimumSequence, std::function<Result()> measure) {
        if (!slot || !generation || sequence < minimumSequence) return false;
        if(slot->pending.exchange(true)){++duplicate;return false;}
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || stopping) { ++contended;slot->pending.store(false); return false; }
        for (auto it = jobs.begin(); it != jobs.end();) {
            if (it->generation != generation || it->sequence < minimumSequence) {
                ++cancelled;it->slot->pending.store(false); it = jobs.erase(it);
            } else ++it;
        }
        queued.store(jobs.size(),std::memory_order_relaxed);
        const bool replaceFurthest=jobs.size()>=Capacity &&
            jobs.back().generation==generation && sequence<jobs.back().sequence;
        if (jobs.size()>=Capacity && !replaceFurthest) {
            ++full;slot->pending.store(false);return false;
        }
        try {
            if (!worker.joinable()) worker = std::thread([this] { Run(); });
            // Retain arrival order among equal sequences. The active job is
            // already outside this queue and is never interrupted/replaced.
            auto where=jobs.begin();
            while (where!=jobs.end() && where->sequence<=sequence) ++where;
            jobs.insert(where,{slot,generation,sequence,std::move(measure),NowUs()});
            if (replaceFurthest) {
                // Insert successfully before discarding existing work. A nearer
                // source must not be rejected merely because future work is full.
                jobs.back().slot->pending.store(false);++cancelled;jobs.pop_back();
            }
            ++submitted;queued.store(jobs.size(),std::memory_order_relaxed);
            Peak(queueHighWater,jobs.size());
        } catch (...) { slot->pending.store(false); return false; }
        changed.notify_one();
        return true;
    }
    static std::shared_ptr<const Result> Ready(const std::shared_ptr<Slot>& slot) {
        return slot ? std::atomic_load(&slot->ready) : nullptr;
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            for (auto& job : jobs) job.slot->pending.store(false);
            cancelled.fetch_add(jobs.size(),std::memory_order_relaxed);
            jobs.clear();queued.store(0,std::memory_order_relaxed);
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }
private:
    struct Job { std::shared_ptr<Slot> slot; uint64_t generation,sequence; std::function<Result()> measure; uint64_t enqueuedUs=0; };
    std::atomic<uint64_t> submitted{0},completed{0},failed{0},duplicate{0},contended{0},full{0},cancelled{0};
    std::atomic<uint64_t> queueUs{0},wallUs{0},cpuUs{0},cpuSamples{0},queuePeakUs{0},wallPeakUs{0};
    std::atomic<uint64_t> queued{0},queueHighWater{0},activeStartUs{0};
    static uint64_t NowUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static void Peak(std::atomic<uint64_t>& value,uint64_t candidate) {
        auto old=value.load(std::memory_order_relaxed);
        while(old<candidate && !value.compare_exchange_weak(old,candidate,std::memory_order_relaxed)) {}
    }
    static bool CpuUs(uint64_t& value) {
        FILETIME created{},exited{},kernel{},user{};
        if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return false;
        const uint64_t k=(uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime;
        const uint64_t u=(uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime;
        value=(k+u)/10;return true;
    }
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Job> jobs;
    std::thread worker;
    bool stopping = false;
    void Run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this] { return stopping || !jobs.empty(); });
                if (stopping) return;
                job = std::move(jobs.front()); jobs.pop_front();
                queued.store(jobs.size(),std::memory_order_relaxed);
            }
            const uint64_t start=NowUs(),wait=start-job.enqueuedUs;
            activeStartUs.store(start,std::memory_order_relaxed);

            uint64_t cpuStart=0,cpuEnd=0;const bool cpuValid=CpuUs(cpuStart);
            try {
                std::shared_ptr<const Result> result = std::make_shared<Result>(job.measure());
                std::atomic_store(&job.slot->ready,std::move(result));
            } catch (...) { ++failed; /* Unavailable optional evidence never fails playback. */ }
            const uint64_t elapsed=NowUs()-start;
            queueUs.fetch_add(wait,std::memory_order_relaxed);Peak(queuePeakUs,wait);
            wallUs.fetch_add(elapsed,std::memory_order_relaxed);Peak(wallPeakUs,elapsed);
            if(CpuUs(cpuEnd) && cpuValid && cpuEnd>=cpuStart){
                cpuUs.fetch_add(cpuEnd-cpuStart,std::memory_order_relaxed);++cpuSamples;
            }
            activeStartUs.store(0,std::memory_order_relaxed);++completed;
            job.slot->pending.store(false);
        }
    }
};

// Only a contiguous ready prefix is evidence. Later ready frames cannot bridge
// a missing source, and a new pending tail must not starve an older ready head.
class OptionalAnalysisPrefix {
public:
    bool Admit(size_t index,bool available) {
        blocked=blocked || !available || index!=count;
        if(blocked)return false;
        ++count;return true;
    }
    size_t Size() const { return count; }
private:
    size_t count=0;
    bool blocked=false;
};
