#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>

namespace DebugLogAsync
{
    struct Metrics
    {
        uint64_t enqueued = 0;
        uint64_t processed = 0;
        uint64_t droppedFull = 0;
        uint64_t producerContentions = 0;
        uint64_t truncated = 0;
        uint64_t queueDepth = 0;
        uint64_t highWater = 0;
        uint64_t flushCount = 0;
        uint64_t flushTotalUs = 0;
        uint64_t flushPeakUs = 0;
    };

    struct TransportTestAccess;

    // Only bounded copies/bookkeeping run under this mutex. Producers never
    // wait for it: contention drops the new record. Full queues retain the old
    // logger's drop-oldest policy, with every loss now counted explicitly.
    // The default transport is about 4 MiB; own it statically or on the heap.
    template<size_t Capacity = 1000, size_t MessageBytes = 4096, size_t BatchSize = 50>
    class Transport
    {
        static_assert(Capacity > 0 && MessageBytes > 1 && BatchSize > 0,
            "The async log transport needs nonempty bounded storage");
    public:
        struct Record
        {
            char message[MessageBytes]{};
            std::time_t timestamp = 0;
        };

        // Lifecycle operations are called before/after the consumer thread.
        void Open()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_head = m_count = 0;
            m_enqueued = 0; m_processed = 0; m_droppedFull = 0;
            m_producerContentions = 0; m_truncated = 0;
            m_depth = 0; m_highWater = 0;
            m_flushCount = 0; m_flushTotalUs = 0; m_flushPeakUs = 0;
            m_accepting.store(true, std::memory_order_release);
        }

        void Close()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_accepting.store(false, std::memory_order_release);
            }
            m_available.notify_one();
        }

        bool TryPublish(const char* text, std::time_t timestamp, bool truncated = false)
        {
            if (!m_accepting.load(std::memory_order_acquire)) return false;
            std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
            if (!lock.owns_lock())
            {
                m_producerContentions.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (!m_accepting.load(std::memory_order_relaxed)) return false;
            if (m_count == Capacity)
            {
                m_head = (m_head + 1) % Capacity;
                --m_count;
                m_droppedFull.fetch_add(1, std::memory_order_relaxed);
            }
            Record& record = m_records[(m_head + m_count) % Capacity];
            size_t length = 0;
            if (text) while (length + 1 < MessageBytes && text[length]) ++length;
            if (length) std::memcpy(record.message, text, length);
            record.message[length] = '\0';
            record.timestamp = timestamp;
            ++m_count;
            m_enqueued.fetch_add(1, std::memory_order_relaxed);
            if (truncated) m_truncated.fetch_add(1, std::memory_order_relaxed);
            m_depth.store(m_count, std::memory_order_relaxed);
            if (m_count > m_highWater.load(std::memory_order_relaxed))
                m_highWater.store(m_count, std::memory_order_relaxed);
            lock.unlock();
            m_available.notify_one();
            return true;
        }

        Metrics Snapshot() const
        {
            Metrics result;
            result.enqueued = m_enqueued.load(std::memory_order_relaxed);
            result.processed = m_processed.load(std::memory_order_relaxed);
            result.droppedFull = m_droppedFull.load(std::memory_order_relaxed);
            result.producerContentions = m_producerContentions.load(std::memory_order_relaxed);
            result.truncated = m_truncated.load(std::memory_order_relaxed);
            result.queueDepth = m_depth.load(std::memory_order_relaxed);
            result.highWater = m_highWater.load(std::memory_order_relaxed);
            result.flushCount = m_flushCount.load(std::memory_order_relaxed);
            result.flushTotalUs = m_flushTotalUs.load(std::memory_order_relaxed);
            result.flushPeakUs = m_flushPeakUs.load(std::memory_order_relaxed);
            return result;
        }

        // Sink owns all formatting and I/O. No sink call can run while the
        // producer mutex is held, including idle, batch and shutdown flushes.
        // Tests inject a sink that stalls writes/flushes at these exact points.
        template<typename Sink>
        void Run(Sink& sink)
        {
            using Clock = std::chrono::steady_clock;
            std::array<Record, BatchSize> batch;
            auto lastFlush = Clock::now();
            auto lastReport = lastFlush;
            size_t sinceFlush = 0;
            for (;;)
            {
                bool drainedAndClosed = false;
                const size_t count = TakeBatch(batch, drainedAndClosed);
                for (size_t index = 0; index < count; ++index)
                {
                    sink.Write(batch[index]);
                    m_processed.fetch_add(1, std::memory_order_relaxed);
                    ++sinceFlush;
                }
                const auto now = Clock::now();
                if (drainedAndClosed || now - lastReport >= std::chrono::seconds(5))
                {
                    sink.Report(Snapshot());
                    ++sinceFlush;
                    lastReport = now;
                }
                if (sinceFlush && (drainedAndClosed || sinceFlush >= BatchSize ||
                    now - lastFlush >= std::chrono::milliseconds(100)))
                {
                    const auto started = Clock::now();
                    sink.Flush();
                    const uint64_t elapsedUs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
                    m_flushCount.fetch_add(1, std::memory_order_relaxed);
                    m_flushTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
                    if (elapsedUs > m_flushPeakUs.load(std::memory_order_relaxed))
                        m_flushPeakUs.store(elapsedUs, std::memory_order_relaxed);
                    sinceFlush = 0;
                    lastFlush = Clock::now();
                }
                if (drainedAndClosed) return;
            }
        }

    private:
        size_t TakeBatch(std::array<Record, BatchSize>& batch, bool& drainedAndClosed)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_available.wait_for(lock, std::chrono::milliseconds(100), [this]
            {
                return m_count != 0 || !m_accepting.load(std::memory_order_relaxed);
            });
            const size_t count = (std::min)(m_count, BatchSize);
            for (size_t index = 0; index < count; ++index)
                batch[index] = m_records[(m_head + index) % Capacity];
            m_head = (m_head + count) % Capacity;
            m_count -= count;
            m_depth.store(m_count, std::memory_order_relaxed);
            drainedAndClosed = m_count == 0 && !m_accepting.load(std::memory_order_relaxed);
            return count;
        }

        friend struct TransportTestAccess;
        std::array<Record, Capacity> m_records{};
        mutable std::mutex m_mutex;
        std::condition_variable m_available;
        std::atomic<bool> m_accepting{false};
        size_t m_head = 0, m_count = 0;
        std::atomic<uint64_t> m_enqueued{0}, m_processed{0}, m_droppedFull{0};
        std::atomic<uint64_t> m_producerContentions{0}, m_truncated{0};
        std::atomic<uint64_t> m_depth{0}, m_highWater{0};
        std::atomic<uint64_t> m_flushCount{0}, m_flushTotalUs{0}, m_flushPeakUs{0};
    };
}
