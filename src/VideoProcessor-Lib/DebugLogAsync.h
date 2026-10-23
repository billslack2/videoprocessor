#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <system_error>

namespace DebugLogAsync
{
    struct Metrics
    {
        uint64_t enqueued = 0, processed = 0, droppedFull = 0;
        // Retained for telemetry compatibility. Admission no longer has a mutex.
        uint64_t producerContentions = 0, truncated = 0;
        uint64_t queueDepth = 0, highWater = 0, capacity = 0, memoryBytes = 0;
        uint64_t poolInUse = 0, poolHighWater = 0;
        uint64_t writeCount = 0, writeBytes = 0, writeFailures = 0;
        uint64_t writeTotalUs = 0, writePeakUs = 0;
        uint64_t flushCount = 0, flushTotalUs = 0, flushPeakUs = 0;
    };

    struct TransportTestAccess;

    // Windows sequenced lists provide atomic, nonblocking pool/publication
    // handoff. Each producer owns its record until publication. The sole writer
    // detaches and reverses completed publications, preserving publication FIFO.
    // A paused producer cannot own a queue lock or hide other producers' records.
    // Default pool is about 32 MiB; own a default Transport statically/on the heap.
    template<size_t Capacity = 8192, size_t MessageBytes = 4096, size_t BatchSize = 256>
    class Transport
    {
        struct Node;
        static_assert(Capacity > 0 && Capacity <= 65535 && MessageBytes > 1 &&
            MessageBytes <= UINT32_MAX && BatchSize > 0, "Invalid bounded log storage");
    public:
        static constexpr size_t MessageCapacity = MessageBytes;
        static constexpr size_t BatchCapacity = BatchSize;
        static constexpr DWORD FlushIntervalMs = 250;
        static constexpr size_t WriterBufferBytes = (MessageBytes + 64) * BatchSize;
        struct Record
        {
            char message[MessageBytes]{};
            std::time_t timestamp = 0;
            uint32_t length = 0;
        };

        Transport()
        {
            InitializeSListHead(&m_free);
            InitializeSListHead(&m_pending);
            m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!m_wake) throw std::system_error(GetLastError(), std::system_category(), "Log writer event");
        }
        ~Transport() { CloseHandle(m_wake); }
        Transport(const Transport&) = delete;
        Transport& operator=(const Transport&) = delete;

        // Lifecycle operations require the previous consumer/producers to have
        // finished before reopening. Admission stays closed while storage resets.
        void Open()
        {
            m_admission.store(Closed, std::memory_order_relaxed);
            InitializeSListHead(&m_free);
            InitializeSListHead(&m_pending);
            m_draining = nullptr;
            ResetEvent(m_wake);
            for (auto& node : m_records) InterlockedPushEntrySList(&m_free, &node.link);
            m_enqueued = 0; m_processed = 0; m_droppedFull = 0; m_truncated = 0;
            m_depth = 0; m_highWater = 0; m_poolInUse = 0; m_poolHighWater = 0;
            m_writeCount = 0; m_writeBytes = 0; m_writeFailures = 0;
            m_writeTotalUs = 0; m_writePeakUs = 0;
            m_flushCount = 0; m_flushTotalUs = 0; m_flushPeakUs = 0;
            m_admission.store(0, std::memory_order_release);
        }

        void Close()
        {
            m_admission.fetch_or(Closed, std::memory_order_acq_rel);
            SetEvent(m_wake);
        }

        bool TryPublish(const char* text, std::time_t timestamp, bool truncated = false)
        {
            Node* node = ReserveRecord();
            if (!node) return false;
            PublishReserved(node, text, timestamp, truncated);
            return true;
        }

        Metrics Snapshot() const
        {
            Metrics result;
            result.enqueued = m_enqueued.load(std::memory_order_relaxed);
            result.processed = m_processed.load(std::memory_order_relaxed);
            result.droppedFull = m_droppedFull.load(std::memory_order_relaxed);
            result.truncated = m_truncated.load(std::memory_order_relaxed);
            result.queueDepth = m_depth.load(std::memory_order_relaxed);
            result.highWater = m_highWater.load(std::memory_order_relaxed);
            result.capacity = Capacity;
            result.memoryBytes = sizeof(*this) + sizeof(Record) * BatchSize + WriterBufferBytes;
            result.poolInUse = m_poolInUse.load(std::memory_order_relaxed);
            result.poolHighWater = m_poolHighWater.load(std::memory_order_relaxed);
            result.writeCount = m_writeCount.load(std::memory_order_relaxed);
            result.writeBytes = m_writeBytes.load(std::memory_order_relaxed);
            result.writeFailures = m_writeFailures.load(std::memory_order_relaxed);
            result.writeTotalUs = m_writeTotalUs.load(std::memory_order_relaxed);
            result.writePeakUs = m_writePeakUs.load(std::memory_order_relaxed);
            result.flushCount = m_flushCount.load(std::memory_order_relaxed);
            result.flushTotalUs = m_flushTotalUs.load(std::memory_order_relaxed);
            result.flushPeakUs = m_flushPeakUs.load(std::memory_order_relaxed);
            return result;
        }

        // Only the writer allocates its scratch batch, formats timestamps or
        // calls the sink. Nodes return to the pool before any disk operation.
        template<typename Sink>
        void Run(Sink& sink)
        {
            using Clock = std::chrono::steady_clock;
            auto batch = std::make_unique<std::array<Record, BatchSize>>();
            auto lastFlush = Clock::now(), lastReport = lastFlush;
            bool needsFlush = false;
            for (;;)
            {
                const size_t count = TakeBatch(*batch);
                if (count)
                {
                    const auto started = Clock::now();
                    const size_t bytes = sink.WriteBatch(batch->data(), count);
                    TrackWrite(bytes, ElapsedUs(started), true);
                    m_processed.fetch_add(count, std::memory_order_relaxed);
                    needsFlush = true;
                }
                const bool drainedAndClosed =
                    m_admission.load(std::memory_order_acquire) == Closed &&
                    m_depth.load(std::memory_order_relaxed) == 0;
                const auto now = Clock::now();
                if (drainedAndClosed || now - lastReport >= std::chrono::seconds(5))
                {
                    const auto started = Clock::now();
                    const size_t bytes = sink.Report(Snapshot());
                    TrackWrite(bytes, ElapsedUs(started), false);
                    needsFlush = true;
                    lastReport = now;
                }
                if (needsFlush && (drainedAndClosed ||
                    now - lastFlush >= std::chrono::milliseconds(FlushIntervalMs)))
                {
                    const auto started = Clock::now();
                    sink.Flush();
                    const uint64_t elapsedUs = ElapsedUs(started);
                    m_flushCount.fetch_add(1, std::memory_order_relaxed);
                    m_flushTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
                    RaisePeak(m_flushPeakUs, elapsedUs);
                    needsFlush = false;
                    lastFlush = Clock::now();
                }
                if (drainedAndClosed) return;
                if (!count) WaitForSingleObject(m_wake, FlushIntervalMs);
            }
        }

    private:
        struct alignas(MEMORY_ALLOCATION_ALIGNMENT) Node
        {
            SLIST_ENTRY link{}; // First member: entry pointer is also the node pointer.
            Record record;
            Node* drainNext = nullptr; // Private to the writer after detachment.
        };
        static_assert(offsetof(Node, link) == 0, "SLIST entry must begin its aligned node");
        static constexpr uint64_t Closed = uint64_t(1) << 63;
        Node* ReserveRecord()
        {
            // Closed flag and admitted-producer count share one atomic. Shutdown
            // cannot finish draining while a pre-close producer is publishing.
            const uint64_t admission = m_admission.fetch_add(1, std::memory_order_acq_rel);
            if (admission & Closed) { EndPublication(); return nullptr; }
            auto* entry = InterlockedPopEntrySList(&m_free);
            if (!entry)
            {
                m_droppedFull.fetch_add(1, std::memory_order_relaxed);
                EndPublication();
                return nullptr; // Only genuine pool exhaustion drops an open-session record.
            }
            Node* node = reinterpret_cast<Node*>(entry);
            RaisePeak(m_poolHighWater, m_poolInUse.fetch_add(1, std::memory_order_relaxed) + 1);
            return node;
        }
        void PublishReserved(Node* node, const char* text, std::time_t timestamp, bool truncated)
        {
            size_t length = 0;
            if (text) while (length + 1 < MessageBytes && text[length]) ++length;
            if (length) std::memcpy(node->record.message, text, length);
            node->record.message[length] = '\0';
            node->record.length = static_cast<uint32_t>(length);
            node->record.timestamp = timestamp;
            m_enqueued.fetch_add(1, std::memory_order_relaxed);
            if (truncated || (text && text[length])) m_truncated.fetch_add(1, std::memory_order_relaxed);
            const uint64_t depth = m_depth.fetch_add(1, std::memory_order_relaxed) + 1;
            RaisePeak(m_highWater, depth);
            InterlockedPushEntrySList(&m_pending, &node->link);
            // Sparse traffic coalesces for up to 250 ms. A full batch wakes the
            // writer; each ordinary log call no longer wakes a disk-writing thread.
            if (depth == BatchSize) SetEvent(m_wake);
            EndPublication();
        }

        static void RaisePeak(std::atomic<uint64_t>& peak, uint64_t value)
        {
            uint64_t previous = peak.load(std::memory_order_relaxed);
            while (previous < value && !peak.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
        }
        static uint64_t ElapsedUs(std::chrono::steady_clock::time_point started)
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
        }
        void TrackWrite(size_t bytes, uint64_t elapsedUs, bool payload)
        {
            if (!bytes) { if (payload) ++m_writeFailures; return; }
            ++m_writeCount;
            m_writeBytes.fetch_add(bytes, std::memory_order_relaxed);
            m_writeTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
            RaisePeak(m_writePeakUs, elapsedUs);
        }
        void EndPublication()
        {
            const uint64_t previous = m_admission.fetch_sub(1, std::memory_order_acq_rel);
            if (previous == Closed + 1) SetEvent(m_wake);
        }
        size_t TakeBatch(std::array<Record, BatchSize>& batch)
        {
            if (!m_draining)
            {
                auto* entry = InterlockedFlushSList(&m_pending);
                while (entry)
                {
                    auto* next = entry->Next;
                    Node* node = reinterpret_cast<Node*>(entry);
                    node->drainNext = m_draining;
                    m_draining = node;
                    entry = next;
                }
            }
            size_t count = 0;
            while (m_draining && count < BatchSize)
            {
                Node* node = m_draining;
                m_draining = node->drainNext;
                auto& record = batch[count++];
                record.length = node->record.length;
                record.timestamp = node->record.timestamp;
                std::memcpy(record.message, node->record.message, record.length + 1);
                m_depth.fetch_sub(1, std::memory_order_relaxed);
                m_poolInUse.fetch_sub(1, std::memory_order_relaxed);
                InterlockedPushEntrySList(&m_free, &node->link);
            }
            return count;
        }
        friend struct TransportTestAccess;
        alignas(MEMORY_ALLOCATION_ALIGNMENT) SLIST_HEADER m_free{}, m_pending{};
        std::array<Node, Capacity> m_records{};
        Node* m_draining = nullptr;
        HANDLE m_wake = nullptr;
        std::atomic<uint64_t> m_admission{Closed};
        std::atomic<uint64_t> m_enqueued{0}, m_processed{0}, m_droppedFull{0}, m_truncated{0};
        std::atomic<uint64_t> m_depth{0}, m_highWater{0}, m_poolInUse{0}, m_poolHighWater{0};
        std::atomic<uint64_t> m_writeCount{0}, m_writeBytes{0}, m_writeFailures{0}, m_writeTotalUs{0}, m_writePeakUs{0};
        std::atomic<uint64_t> m_flushCount{0}, m_flushTotalUs{0}, m_flushPeakUs{0};
    };
}
