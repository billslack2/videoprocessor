#include "pch.h"
#include <DebugLog.h>
#include <DebugLogAsync.h>
#include <DebugLogRetention.h>
#include "CppUnitTest.h"

#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace DebugLogAsync
{
    // Pause a real reservation before publication, using the production
    // reservation/publication operations rather than a producer mutex hook.
    struct TransportTestAccess
    {
        template<size_t C, size_t L, size_t B>
        static void* Reserve(Transport<C, L, B>& queue) { return queue.ReserveRecord(); }
        template<size_t C, size_t L, size_t B>
        static void Publish(Transport<C, L, B>& queue, void* node, const char* text)
        {
            queue.PublishReserved(static_cast<typename Transport<C, L, B>::Node*>(node), text, 1, false);
        }
    };
}

struct DebugLogTestAccess
{
    static size_t WriteMaximumBatch(const std::string& path)
    {
        using Queue = DebugLogAsync::Transport<>;
        auto batch = std::make_unique<std::array<Queue::Record, Queue::BatchCapacity>>();
        for (auto& record : *batch)
        {
            std::memset(record.message, 'x', sizeof(record.message) - 1);
            record.message[sizeof(record.message) - 1] = '\0';
            record.length = static_cast<uint32_t>(sizeof(record.message) - 1);
            record.timestamp = 1700000000;
        }
        std::ofstream file(path, std::ios::binary);
        DebugLog::FileSink sink(file);
        const size_t bytes = sink.WriteBatch(batch->data(), batch->size());
        sink.Flush();
        return bytes;
    }
};

namespace
{
    template<typename Queue>
    struct RecordingSink
    {
        std::vector<typename Queue::Record> records;
        std::vector<DebugLogAsync::Metrics> reports;
        unsigned int flushes = 0;
        void Write(const typename Queue::Record& record) { records.push_back(record); }
        size_t WriteBatch(const typename Queue::Record* batch, size_t count)
        {
            size_t bytes = 0;
            for (size_t i = 0; i < count; ++i) { Write(batch[i]); bytes += batch[i].length + 1; }
            return bytes;
        }
        void Flush() { ++flushes; }
        size_t Report(const DebugLogAsync::Metrics& metrics) { reports.push_back(metrics); return 0; }
    };

    enum class BlockPoint { Write, Flush };
    template<typename Queue>
    struct BlockingSink : RecordingSink<Queue>
    {
        explicit BlockingSink(BlockPoint point) : point(point), resumed(resume.get_future().share()) {}
        BlockPoint point;
        std::promise<void> entered, resume;
        std::shared_future<void> resumed;
        std::atomic<bool> released{false};
        bool blocked = false;
        void Pause(BlockPoint current)
        {
            if (point == current && !blocked)
            {
                blocked = true;
                entered.set_value();
                resumed.wait();
            }
        }
        void Release()
        {
            if (!released.exchange(true)) resume.set_value();
        }
        size_t WriteBatch(const typename Queue::Record* records, size_t count)
        {
            Pause(BlockPoint::Write);
            return RecordingSink<Queue>::WriteBatch(records, count);
        }
        void Flush()
        {
            Pause(BlockPoint::Flush);
            RecordingSink<Queue>::Flush();
        }
    };

    template<typename Queue>
    struct ConsumerGuard
    {
        Queue& queue;
        BlockingSink<Queue>& sink;
        std::thread thread;
        ConsumerGuard(Queue& queue, BlockingSink<Queue>& sink) :
            queue(queue), sink(sink), thread([&queue, &sink] { queue.Run(sink); }) {}
        void Finish()
        {
            sink.Release();
            queue.Close();
            if (thread.joinable()) thread.join();
        }
        ~ConsumerGuard() { Finish(); }
    };

    void CheckFlushDoesNotBlockProducer(bool periodic)
    {
        using Queue = DebugLogAsync::Transport<8, 128, 2>;
        Queue queue;
        queue.Open();
        queue.TryPublish("before-flush-1", 10);
        if (!periodic) queue.TryPublish("before-flush-2", 11);
        BlockingSink<Queue> sink(BlockPoint::Flush);
        auto entered = sink.entered.get_future();
        ConsumerGuard<Queue> consumer(queue, sink);
        const bool paused = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        auto producer = std::async(std::launch::async, [&queue] {
            return queue.TryPublish("while-flush-blocked", 12);
        });
        const bool producerFinishedBeforeFlush =
            producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        consumer.Finish();
        const bool accepted = producer.get();
        Assert::IsTrue(paused, L"The injected consumer flush did not run");
        Assert::IsTrue(producerFinishedBeforeFlush,
            L"A producer waited for a blocked consumer flush");
        Assert::IsTrue(accepted);
        Assert::AreEqual(static_cast<size_t>(periodic ? 2 : 3), sink.records.size());
        const auto metrics = queue.Snapshot();
        Assert::AreEqual(uint64_t(0), metrics.producerContentions);
        Assert::AreEqual(uint64_t(0), metrics.queueDepth);
        Assert::IsTrue(metrics.flushCount > 0 && metrics.flushPeakUs > 0);
        Assert::IsTrue(metrics.flushTotalUs >= metrics.flushPeakUs);
    }

    std::string forwardedLog;
    void __cdecl CaptureForwardedLog(const char* message) { forwardedLog = message ? message : ""; }

    struct TemporaryLogDirectory
    {
        std::string directory;
        TemporaryLogDirectory()
        {
            char root[MAX_PATH]{};
            GetTempPathA(ARRAYSIZE(root), root);
            directory = std::string(root) + "VideoProcessor-async-log-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            CreateDirectoryA(directory.c_str(), nullptr);
        }
        std::string Path(const char* name) const { return directory + "\\" + name; }
        ~TemporaryLogDirectory()
        {
            for (const char* name : { "vp.log", "vp.log.0", "unrelated.log" })
                DeleteFileA(Path(name).c_str());
            RemoveDirectoryA(directory.c_str());
        }
    };

    std::string Contents(const std::string& path)
    {
        std::ifstream input(path);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
}

namespace VideoProcessorTest
{
    TEST_CLASS(DebugLogAsyncTests)
    {
    public:
        TEST_METHOD(RecordsOwnProducerTextAndCaptureTimestamp)
        {
            using Queue = DebugLogAsync::Transport<4, 32, 2>;
            Queue queue;
            queue.Open();
            char message[] = "captured before worker";
            Assert::IsTrue(queue.TryPublish(message, 123456));
            message[0] = '!';
            queue.Close();
            RecordingSink<Queue> sink;
            queue.Run(sink);
            Assert::AreEqual(static_cast<size_t>(1), sink.records.size());
            Assert::AreEqual(std::string("captured before worker"), std::string(sink.records[0].message));
            Assert::AreEqual(std::time_t(123456), sink.records[0].timestamp);
        }

        TEST_METHOD(OnlyRealCapacityExhaustionDropsAndPreservesAcceptedRecords)
        {
            using Queue = DebugLogAsync::Transport<3, 32, 2>;
            Queue queue;
            queue.Open();
            for (int index = 0; index < 5; ++index)
            {
                const std::string text = std::to_string(index);
                Assert::AreEqual(index < 3, queue.TryPublish(text.c_str(), index));
            }
            const auto before = queue.Snapshot();
            Assert::AreEqual(uint64_t(3), before.highWater);
            Assert::AreEqual(uint64_t(3), before.queueDepth);
            Assert::AreEqual(uint64_t(3), before.poolHighWater);
            Assert::AreEqual(uint64_t(2), before.droppedFull);
            queue.Close();
            RecordingSink<Queue> sink;
            queue.Run(sink);
            Assert::AreEqual(size_t(3), sink.records.size());
            for (size_t index = 0; index < 3; ++index)
                Assert::AreEqual(std::to_string(index), std::string(sink.records[index].message));
            const auto after = queue.Snapshot();
            Assert::AreEqual(after.enqueued, after.processed);
            Assert::AreEqual(uint64_t(0), after.poolInUse);
            Assert::AreEqual(uint64_t(0), after.queueDepth);
        }

        TEST_METHOD(PausedProducerDoesNotBlockOtherPublicationOrDrain)
        {
            using Queue = DebugLogAsync::Transport<4, 64, 2>;
            Queue queue;
            queue.Open();
            void* held = DebugLogAsync::TransportTestAccess::Reserve(queue);
            Assert::IsNotNull(held);
            BlockingSink<Queue> sink(BlockPoint::Write);
            auto entered = sink.entered.get_future();
            ConsumerGuard<Queue> consumer(queue, sink);
            const bool accepted = queue.TryPublish("independent producer", 2);
            const bool drainedBeforeRelease = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            DebugLogAsync::TransportTestAccess::Publish(queue, held, "delayed producer");
            consumer.Finish();
            Assert::IsTrue(accepted && drainedBeforeRelease);
            Assert::AreEqual(size_t(2), sink.records.size());
            Assert::AreEqual(std::string("independent producer"), std::string(sink.records[0].message));
            Assert::AreEqual(std::string("delayed producer"), std::string(sink.records[1].message));
            Assert::AreEqual(uint64_t(0), queue.Snapshot().producerContentions);
            Assert::AreEqual(uint64_t(0), queue.Snapshot().droppedFull);
        }

        TEST_METHOD(ShutdownWaitsForAlreadyReservedProducerAndDrainsItsMessage)
        {
            using Queue = DebugLogAsync::Transport<4, 64, 2>;
            Queue queue;
            queue.Open();
            void* held = DebugLogAsync::TransportTestAccess::Reserve(queue);
            queue.Close();
            RecordingSink<Queue> sink;
            auto consumer = std::async(std::launch::async, [&] { queue.Run(sink); });
            const bool stayedAlive = consumer.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
            Assert::IsFalse(queue.TryPublish("after shutdown", 3));
            DebugLogAsync::TransportTestAccess::Publish(queue, held, "admitted before close");
            consumer.get();
            Assert::IsTrue(stayedAlive);
            Assert::AreEqual(size_t(1), sink.records.size());
            Assert::AreEqual(std::string("admitted before close"), std::string(sink.records[0].message));
            Assert::AreEqual(uint64_t(0), queue.Snapshot().poolInUse);
        }

        TEST_METHOD(SlowDiskWriteCannotBlockProducersOrGrowTheQueue)
        {
            using Queue = DebugLogAsync::Transport<4, 128, 2>;
            Queue queue;
            queue.Open();
            queue.TryPublish("first", 1);
            queue.TryPublish("second", 2);
            BlockingSink<Queue> sink(BlockPoint::Write);
            auto entered = sink.entered.get_future();
            ConsumerGuard<Queue> consumer(queue, sink);
            const bool paused = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            auto producers = std::async(std::launch::async, [&queue] {
                size_t accepted = 0;
                for (int index = 0; index < 12; ++index)
                    if (queue.TryPublish("while-disk-is-blocked", index + 3)) ++accepted;
                return accepted;
            });
            const bool completedBeforeDisk =
                producers.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            const auto blockedMetrics = queue.Snapshot();
            consumer.Finish();
            const size_t accepted = producers.get();
            Assert::IsTrue(paused && completedBeforeDisk);
            Assert::AreEqual(size_t(4), accepted);
            Assert::AreEqual(uint64_t(4), blockedMetrics.queueDepth);
            Assert::AreEqual(uint64_t(4), blockedMetrics.highWater);
            Assert::AreEqual(uint64_t(8), blockedMetrics.droppedFull);
            const auto final = queue.Snapshot();
            Assert::AreEqual(final.enqueued, final.processed);
            Assert::AreEqual(uint64_t(0), final.producerContentions);
        }

        TEST_METHOD(BatchFlushDoesNotOwnProducerLock) { CheckFlushDoesNotBlockProducer(false); }
        TEST_METHOD(PeriodicFlushDoesNotOwnProducerLock) { CheckFlushDoesNotBlockProducer(true); }

        TEST_METHOD(ShutdownDrainsAcceptedRecordsAndRejectsLaterPublication)
        {
            using Queue = DebugLogAsync::Transport<4, 32, 2>;
            Queue queue;
            queue.Open();
            queue.TryPublish("first", 1);
            queue.TryPublish("last", 2);
            queue.Close();
            Assert::IsFalse(queue.TryPublish("after-close", 3));
            RecordingSink<Queue> sink;
            queue.Run(sink);
            Assert::AreEqual(static_cast<size_t>(2), sink.records.size());
            Assert::AreEqual(std::string("last"), std::string(sink.records.back().message));
            Assert::AreEqual(uint64_t(2), queue.Snapshot().processed);
            Assert::IsTrue(sink.flushes > 0 && !sink.reports.empty());
        }

        TEST_METHOD(FinalFlushRejectsLateProducersWithoutWaitingForDisk)
        {
            using Queue = DebugLogAsync::Transport<4, 32, 2>;
            Queue queue;
            queue.Open();
            queue.TryPublish("last accepted", 1);
            queue.Close();
            BlockingSink<Queue> sink(BlockPoint::Flush);
            auto entered = sink.entered.get_future();
            ConsumerGuard<Queue> consumer(queue, sink);
            const bool paused = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            auto producer = std::async(std::launch::async, [&queue] {
                return queue.TryPublish("after shutdown", 2);
            });
            const bool completedBeforeDisk =
                producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            consumer.Finish();
            const bool accepted = producer.get();
            Assert::IsTrue(paused && completedBeforeDisk);
            Assert::IsFalse(accepted);
            Assert::AreEqual(static_cast<size_t>(1), sink.records.size());
            Assert::AreEqual(std::string("last accepted"), std::string(sink.records[0].message));
        }

        TEST_METHOD(ConcurrentProducersAccountForEveryAcceptedRecordAndDrop)
        {
            using Queue = DebugLogAsync::Transport<16, 64, 4>;
            Queue queue;
            queue.Open();
            std::atomic<uint64_t> accepted{0};
            std::vector<std::thread> producers;
            for (int thread = 0; thread < 8; ++thread)
                producers.emplace_back([&queue, &accepted, thread] {
                    for (int item = 0; item < 200; ++item)
                    {
                        const std::string text = std::to_string(thread) + ":" + std::to_string(item);
                        if (queue.TryPublish(text.c_str(), thread * 1000 + item)) ++accepted;
                    }
                });
            for (auto& producer : producers) producer.join();
            queue.Close();
            RecordingSink<Queue> sink;
            queue.Run(sink);
            const auto metrics = queue.Snapshot();
            Assert::AreEqual(accepted.load(), metrics.enqueued);
            Assert::AreEqual(uint64_t(1600), metrics.enqueued + metrics.droppedFull);
            Assert::AreEqual(uint64_t(0), metrics.producerContentions);
            Assert::AreEqual(metrics.enqueued, metrics.processed);
            Assert::AreEqual(uint64_t(16), metrics.highWater);
            Assert::AreEqual(static_cast<size_t>(16), sink.records.size());
            std::set<std::string> unique;
            for (const auto& record : sink.records) unique.insert(record.message);
            Assert::AreEqual(sink.records.size(), unique.size());
        }

        TEST_METHOD(SimultaneousProducersRetainEveryRecordWhenPoolHasRoom)
        {
            using Queue = DebugLogAsync::Transport<4096, 64, 256>;
            auto queue = std::make_unique<Queue>();
            queue->Open();
            std::atomic<bool> start{false};
            std::atomic<uint64_t> rejected{0};
            std::vector<std::thread> producers;
            for (int thread = 0; thread < 8; ++thread)
                producers.emplace_back([&, thread] {
                    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                    for (int item = 0; item < 400; ++item)
                    {
                        const std::string text = std::to_string(thread) + ":" + std::to_string(item);
                        if (!queue->TryPublish(text.c_str(), item)) ++rejected;
                    }
                });
            start.store(true, std::memory_order_release);
            for (auto& producer : producers) producer.join();
            queue->Close();
            RecordingSink<Queue> sink;
            queue->Run(sink);
            Assert::AreEqual(uint64_t(0), rejected.load());
            Assert::AreEqual(size_t(3200), sink.records.size());
            std::set<std::string> unique;
            int previous[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
            for (const auto& record : sink.records)
            {
                unique.insert(record.message);
                int thread = -1, item = -1;
                sscanf_s(record.message, "%d:%d", &thread, &item);
                Assert::IsTrue(thread >= 0 && thread < 8 && item == previous[thread] + 1);
                previous[thread] = item;
            }
            Assert::AreEqual(size_t(3200), unique.size());
            Assert::AreEqual(uint64_t(0), queue->Snapshot().poolInUse);
        }

        TEST_METHOD(ConcurrentConsumerRecyclesNodesWithoutLosingOrDuplicatingAcceptedMessages)
        {
            using Queue = DebugLogAsync::Transport<64, 64, 16>;
            Queue queue;
            queue.Open();
            RecordingSink<Queue> sink;
            std::thread consumer([&] { queue.Run(sink); });
            std::atomic<uint64_t> accepted{0}, rejected{0};
            std::vector<std::thread> producers;
            for (int thread = 0; thread < 8; ++thread)
                producers.emplace_back([&, thread] {
                    for (int item = 0; item < 4000; ++item)
                    {
                        const std::string text = std::to_string(thread) + ":" + std::to_string(item);
                        if (queue.TryPublish(text.c_str(), item)) ++accepted; else ++rejected;
                    }
                });
            for (auto& producer : producers) producer.join();
            queue.Close(); consumer.join();
            Assert::AreEqual(accepted.load(), uint64_t(sink.records.size()));
            Assert::AreEqual(uint64_t(32000), accepted.load() + rejected.load());
            Assert::AreEqual(rejected.load(), queue.Snapshot().droppedFull);
            std::set<std::string> unique;
            for (const auto& record : sink.records) unique.insert(record.message);
            Assert::AreEqual(sink.records.size(), unique.size());
            Assert::IsTrue(queue.Snapshot().poolHighWater <= 64);
            Assert::AreEqual(uint64_t(0), queue.Snapshot().poolInUse);
        }

        TEST_METHOD(SparseTrafficCoalescesWritesAndFlushesWithBoundedScratch)
        {
            using Queue = DebugLogAsync::Transport<128, 64, 64>;
            Queue queue;
            queue.Open();
            RecordingSink<Queue> sink;
            std::thread consumer([&] { queue.Run(sink); });
            const auto started = std::chrono::steady_clock::now();
            for (int i = 0; i < 20; ++i)
            {
                queue.TryPublish("sparse record", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            queue.Close(); consumer.join();
            const auto metrics = queue.Snapshot();
            Assert::AreEqual(size_t(20), sink.records.size());
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            const uint64_t schedulingAllowance = 3 + elapsedMs / Queue::FlushIntervalMs;
            Assert::IsTrue(metrics.writeCount <= schedulingAllowance, L"Sparse records should be written in coalesced batches");
            Assert::IsTrue(metrics.flushCount <= schedulingAllowance, L"Sparse traffic should not flush for each record");
            Assert::IsTrue(metrics.memoryBytes < 32 * 1024, L"Configured test queue and scratch budget grew unexpectedly");
        }

        TEST_METHOD(ReopeningStartsAnIndependentSessionWithResetMetrics)
        {
            using Queue = DebugLogAsync::Transport<2, 16, 2>;
            Queue queue;
            queue.Open();
            queue.TryPublish("first", 1, true);
            queue.Close();
            RecordingSink<Queue> first;
            queue.Run(first);
            Assert::AreEqual(uint64_t(1), queue.Snapshot().truncated);
            queue.Open();
            const auto reset = queue.Snapshot();
            Assert::AreEqual(uint64_t(0), reset.enqueued);
            Assert::AreEqual(uint64_t(0), reset.highWater);
            Assert::AreEqual(uint64_t(0), reset.truncated);
            queue.TryPublish("new-session", 2);
            queue.Close();
            RecordingSink<Queue> second;
            queue.Run(second);
            Assert::AreEqual(static_cast<size_t>(1), second.records.size());
            Assert::AreEqual(std::string("new-session"), std::string(second.records[0].message));
        }

        TEST_METHOD(ExternalSinkKeepsFormattedModuleForwardingAndTruncation)
        {
            struct SinkGuard
            {
                SinkGuard() { DebugLog::SetExternalSink(CaptureForwardedLog); }
                ~SinkGuard() { DebugLog::SetExternalSink(nullptr); }
            } guard;
            DebugLog::Log("pipeline %s %.1f", "test", 3.5);
            Assert::AreEqual(std::string("pipeline test 3.5"), forwardedLog);
            const std::string large(5000, 'a');
            DebugLog::Log("%s", large.c_str());
            Assert::AreEqual(static_cast<size_t>(4095), forwardedLog.size());
            Assert::AreEqual(std::string(4095, 'a'), forwardedLog);
        }

        TEST_METHOD(ProductionFileSinkWritesOneCompleteMaximumSizedBatch)
        {
            TemporaryLogDirectory directory;
            const size_t bytes = DebugLogTestAccess::WriteMaximumBatch(directory.Path("vp.log"));
            const auto content = Contents(directory.Path("vp.log"));
            Assert::AreEqual(size_t((19 + 3 + 4095 + 1) * 256), bytes);
            Assert::AreEqual(bytes, content.size());
            Assert::AreEqual(size_t(256), size_t(std::count(content.begin(), content.end(), '\n')));
            Assert::AreEqual(std::string(4095, 'x'), content.substr(22, 4095));
        }

        TEST_METHOD(TransportAppendPreservesRotatedAndUnrelatedLogFiles)
        {
            TemporaryLogDirectory directory;
            { std::ofstream file(directory.Path("vp.log")); file << "prior session"; }
            { std::ofstream file(directory.Path("unrelated.log")); file << "keep"; }
            const auto rotated = DebugLogRetention::Rotate(directory.Path("vp.log"), 2);
            Assert::IsTrue(rotated.activeReady);
            using Queue = DebugLogAsync::Transport<4, 64, 2>;
            Queue queue;
            queue.Open();
            queue.TryPublish("new session record", 123);
            queue.Close();
            struct FileSink
            {
                std::ofstream file;
                explicit FileSink(const std::string& path) : file(path, std::ios::app) {}
                size_t WriteBatch(const Queue::Record* records, size_t count)
                {
                    size_t bytes = 0;
                    for (size_t i = 0; i < count; ++i) { file << records[i].message << '\n'; bytes += records[i].length + 1; }
                    return bytes;
                }
                void Flush() { file.flush(); }
                size_t Report(const DebugLogAsync::Metrics&) { return 0; }
            } sink(directory.Path("vp.log"));
            queue.Run(sink);
            sink.file.close();
            Assert::AreEqual(std::string("prior session"), Contents(directory.Path("vp.log.0")));
            Assert::AreEqual(std::string("keep"), Contents(directory.Path("unrelated.log")));
            Assert::AreEqual(std::string("new session record\n"), Contents(directory.Path("vp.log")));
        }
    };
}
