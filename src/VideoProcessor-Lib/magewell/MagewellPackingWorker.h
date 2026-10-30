#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

// A single bounded CPU job. SDK operations and frame callbacks stay on the
// capture thread; the next DMA can run while this worker finishes packing.
// Wait must precede reuse of either job buffer, including on stop/format change.
class MagewellPackingWorker
{
public:
    struct Result { bool success = false; double milliseconds = 0; };
    void Submit(std::function<bool()> job)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pending || !job) throw std::logic_error("Magewell packing job already pending or empty");
        if (!m_thread.joinable()) m_thread = std::thread([this] { Run(); });
        m_job = std::move(job);
        m_pending = m_busy = true;
        m_changed.notify_all();
    }
    Result Wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_pending) return {};
        m_changed.wait(lock, [this] { return !m_busy; });
        m_pending = false;
        auto failure = std::exchange(m_failure, nullptr);
        if (failure) std::rethrow_exception(failure);
        return m_result;
    }
    ~MagewellPackingWorker()
    {
        try { Wait(); } catch (...) {}
        { std::lock_guard<std::mutex> lock(m_mutex); m_stopping = true; m_changed.notify_all(); }
        if (m_thread.joinable()) m_thread.join();
    }
    MagewellPackingWorker() = default;
    MagewellPackingWorker(const MagewellPackingWorker&) = delete;
    MagewellPackingWorker& operator=(const MagewellPackingWorker&) = delete;
private:
    void Run() noexcept
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;)
        {
            m_changed.wait(lock, [this] { return m_stopping || bool(m_job); });
            if (m_stopping) return;
            auto job = std::exchange(m_job, {});
            lock.unlock();
            Result result;
            std::exception_ptr failure;
            const auto start = std::chrono::steady_clock::now();
            try { result.success = job(); } catch (...) { failure = std::current_exception(); }
            result.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            job = {}; // Release captures before signalling that buffers can be retired.
            lock.lock();
            m_result = result; m_failure = failure; m_busy = false;
            m_changed.notify_all();
        }
    }
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_pending = false, m_busy = false, m_stopping = false;
    std::function<bool()> m_job;
    Result m_result;
    std::exception_ptr m_failure;
    std::thread m_thread;
};
