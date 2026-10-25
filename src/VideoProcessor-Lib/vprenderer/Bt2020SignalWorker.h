#pragma once

#include <vprenderer/Bt2020SignalController.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Bt2020Signal
{
    // Only the worker touches Controller and Backend, including destruction.
    // The bounded mailbox and cached status never share a lock with driver I/O.
    template<class Backend> class WorkerService
    {
    public:
        struct Snapshot
        {
            uint64_t generation = 0, completedGeneration = 0;
            uint64_t requests = 0, coalesced = 0, duplicates = 0, obsoleteCompletions = 0;
            uint64_t driverCalls = 0, driverTotalUs = 0, driverPeakUs = 0;
            uint64_t driverActiveAgeUs = 0, requestAgeUs = 0, requestToServicePeakUs = 0;
            bool active = false, verified = false, failed = false, pending = false;
            std::string target;
            std::string status = "not requested";
        };
        using Factory = std::function<std::unique_ptr<Backend>()>;
        WorkerService() : WorkerService([] { return std::make_unique<Backend>(); }) {}
        explicit WorkerService(Factory factory) : factory_(std::move(factory))
        { worker_ = std::thread([this] { Run(); }); }
        ~WorkerService() { Stop(); }
        WorkerService(const WorkerService&) = delete;
        WorkerService& operator=(const WorkerService&) = delete;

        void Request(const std::string& display, bool enabled, bool restart)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++snapshot_.requests;
            if (retiring_ || stopping_ || exited_) return;
            if (hasIntent_ && display == intent_.display && enabled == intent_.enabled && !restart)
            { ++snapshot_.duplicates; return; }
            if (pending_) ++snapshot_.coalesced;
            const bool keepRestart = pending_ && intent_.display == display &&
                intent_.enabled == enabled && intent_.restart;
            intent_ = { display, enabled, restart || keepRestart, NowUs() };
            hasIntent_ = pending_ = true;
            pendingSinceUs_ = intent_.submittedUs;
            snapshot_.generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            snapshot_.target = display;
            snapshot_.active = snapshot_.verified = false;
            snapshot_.failed = enabled && display.empty();
            snapshot_.pending = true;
            snapshot_.status = enabled && display.empty() ? "BT.2020 target unavailable" :
                enabled ? "BT.2020 (pending; wire unverified)" :
                "Rec.709 / BT.2020 off (pending; wire unverified)";
            wake_.notify_one();
        }
        Snapshot ReadSnapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto result = snapshot_;
            result.driverCalls = driverCalls_.load(std::memory_order_relaxed);
            result.driverTotalUs = driverTotalUs_.load(std::memory_order_relaxed);
            result.driverPeakUs = driverPeakUs_.load(std::memory_order_relaxed);
            const auto now = NowUs(), active = driverStartedUs_.load(std::memory_order_relaxed);
            result.driverActiveAgeUs = active && now >= active ? now - active : 0;
            result.requestAgeUs = result.pending && pendingSinceUs_ && now >= pendingSinceUs_
                ? now - pendingSinceUs_ : 0;
            return result;
        }
        bool IsActive() const { std::lock_guard<std::mutex> lock(mutex_); return snapshot_.active; }
        bool IsReadbackVerified() const { std::lock_guard<std::mutex> lock(mutex_); return snapshot_.verified; }
        bool Failed() const { std::lock_guard<std::mutex> lock(mutex_); return snapshot_.failed; }
        std::string Status() const { std::lock_guard<std::mutex> lock(mutex_); return snapshot_.status; }

        // Lifecycle-only: the retirement caller may wait, but NVAPI stays on the
        // worker. Retrying after failure retains Controller's dirty ownership.
        bool Restore()
        {
            std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
            std::unique_lock<std::mutex> lock(mutex_);
            if (abandon_ || exited_ || stopping_) return exited_ && finalRestored_;
            retiring_ = true;
            pending_ = false;
            snapshot_.generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            snapshot_.active = snapshot_.verified = false;
            snapshot_.pending = true;
            snapshot_.status = "BT.2020 restoration pending; wire unverified";
            pendingSinceUs_ = NowUs();
            const auto ticket = ++restoreRequested_;
            wake_.notify_one();
            completed_.wait(lock, [this, ticket] { return restoreCompleted_ >= ticket || exited_; });
            return restoreCompleted_ >= ticket && restoreSucceeded_;
        }
        void AbandonPendingRestoreForShutdown()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            abandon_ = retiring_ = true;
            pending_ = false;
            snapshot_.generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
            snapshot_.active = snapshot_.verified = snapshot_.pending = false;
            snapshot_.status = "BT.2020 restoration abandoned; wire unverified";
            wake_.notify_one();
        }
        bool Stop() noexcept
        {
            std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = retiring_ = true;
                pending_ = false;
                generation_.fetch_add(1, std::memory_order_acq_rel);
            }
            wake_.notify_one();
            if (worker_.joinable()) worker_.join();
            return finalRestored_;
        }
    private:
        static uint64_t NowUs()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
        static void Peak(std::atomic<uint64_t>& value, uint64_t candidate)
        {
            auto old = value.load(std::memory_order_relaxed);
            while (old < candidate && !value.compare_exchange_weak(old, candidate, std::memory_order_relaxed)) {}
        }
        struct TimedBackend
        {
            using Frame = typename Backend::Frame;
            Backend& backend;
            WorkerService& service;
            template<class Call> bool Invoke(Call&& call)
            {
                const auto start = NowUs();
                service.driverStartedUs_.store(start, std::memory_order_relaxed);
                bool result;
                try { result = call(); }
                catch (...) { Complete(start); throw; }
                Complete(start);
                return result;
            }
            void Complete(uint64_t start)
            {
                const auto elapsed = NowUs() - start;
                service.driverCalls_.fetch_add(1, std::memory_order_relaxed);
                service.driverTotalUs_.fetch_add(elapsed, std::memory_order_relaxed);
                Peak(service.driverPeakUs_, elapsed);
                service.driverStartedUs_.store(0, std::memory_order_relaxed);
            }
            bool Read(const std::string& display, Frame& frame)
            { return Invoke([&] { return backend.Read(display, frame); }); }
            bool Write(const std::string& display, Frame& frame)
            { return Invoke([&] { return backend.Write(display, frame); }); }
            Color GetColor(const Frame& frame) const { return backend.GetColor(frame); }
            void SetColor(Frame& frame, Color color) { backend.SetColor(frame, color); }
            void Report(const std::string& display, bool enabled, unsigned attempt, bool accepted, bool verified)
            { backend.Report(display, enabled, attempt, accepted, verified); }
        };
        struct Intent
        {
            std::string display;
            bool enabled = false, restart = false;
            uint64_t submittedUs = 0;
        };
        void Run() noexcept
        {
            try
            {
                auto backend = factory_();
                if (!backend) throw std::runtime_error("BT.2020 backend unavailable");
                TimedBackend timed{ *backend, *this };
                Controller<TimedBackend> controller(timed);
                uint64_t servicedGeneration = 0;
                for (;;)
                {
                    Intent request;
                    bool requestPending = false, restore = false, abandon = false, stop = false;
                    uint64_t ticket = 0, version = 0;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        wake_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                            return stopping_ || abandon_ || pending_ || restoreRequested_ > restoreCompleted_;
                        });
                        version = generation_.load(std::memory_order_acquire);
                        abandon = abandon_;
                        stop = stopping_;
                        restore = restoreRequested_ > restoreCompleted_;
                        ticket = restoreRequested_;
                        if (!abandon && !stop && !restore && pending_)
                        {
                            request = intent_;
                            requestPending = true;
                            pending_ = false;
                            snapshot_.requestToServicePeakUs = (std::max)(snapshot_.requestToServicePeakUs,
                                NowUs() - request.submittedUs);
                            servicedGeneration = version;
                        }
                    }
                    if (abandon) controller.Abandon();
                    if (stop)
                    {
                        const bool restored = abandon || controller.Retire();
                        std::lock_guard<std::mutex> lock(mutex_);
                        finalRestored_ = restored;
                        exited_ = true;
                        completed_.notify_all();
                        return; // Backend, including NVAPI unload, is destroyed here.
                    }
                    if (abandon)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        restoreSucceeded_ = false;
                        restoreCompleted_ = restoreRequested_;
                        completed_.notify_all();
                        wake_.wait(lock, [this] { return stopping_; });
                        continue;
                    }
                    const auto current = [this, version] {
                        return generation_.load(std::memory_order_acquire) == version;
                    };
                    if (restore)
                    {
                        const bool restored = controller.Retire(current);
                        std::lock_guard<std::mutex> lock(mutex_);
                        restoreSucceeded_ = restored;
                        restoreCompleted_ = ticket;
                        if (current())
                        {
                            snapshot_.completedGeneration = version;
                            snapshot_.pending = false;
                            snapshot_.failed = !restored;
                            snapshot_.status = restored ? "BT.2020 restored; wire unverified" :
                                "BT.2020 restoration FAILED; wire unverified";
                        }
                        else ++snapshot_.obsoleteCompletions;
                        completed_.notify_all();
                        continue;
                    }
                    if (requestPending)
                        controller.Request(request.display, request.enabled, NowUs() / 1000, request.restart);
                    controller.Pump(NowUs() / 1000, current);
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (current() && servicedGeneration == version)
                    {
                        snapshot_.completedGeneration = version;
                        snapshot_.pending = false;
                        snapshot_.active = controller.IsActive();
                        snapshot_.verified = controller.IsVerified();
                        snapshot_.failed = controller.Failed();
                        snapshot_.status = controller.Status();
                    }
                    else if (!current()) ++snapshot_.obsoleteCompletions;
                }
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot_.active = snapshot_.verified = snapshot_.pending = false;
                snapshot_.failed = true;
                snapshot_.status = "BT.2020 worker failed; wire unverified";
                exited_ = true;
                finalRestored_ = false;
                completed_.notify_all();
            }
        }
        Factory factory_;
        mutable std::mutex mutex_;
        std::mutex lifecycleMutex_;
        std::condition_variable wake_, completed_;
        std::thread worker_;
        Intent intent_;
        Snapshot snapshot_;
        std::atomic<uint64_t> generation_{ 0 }, driverCalls_{ 0 }, driverTotalUs_{ 0 };
        std::atomic<uint64_t> driverPeakUs_{ 0 }, driverStartedUs_{ 0 };
        uint64_t restoreRequested_ = 0, restoreCompleted_ = 0, pendingSinceUs_ = 0;
        bool hasIntent_ = false, pending_ = false, retiring_ = false, stopping_ = false;
        bool abandon_ = false, exited_ = false, restoreSucceeded_ = false, finalRestored_ = false;
    };
}
