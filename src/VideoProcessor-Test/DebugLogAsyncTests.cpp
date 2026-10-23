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
    // Deterministic contention injection, with no production logger test hook.
    struct TransportTestAccess
    {
        template<size_t C, size_t L, size_t B>
        static std::unique_lock<std::mutex> Lock(Transport<C, L, B>& transport)
        {
            return std::unique_lock<std::mutex>(transport.m_mutex);
        }
    };
}

namespace
{
    template<typename Queue>
    struct RecordingSink
    {
        std::vector<typename Queue::Record> records;
        std::vector<DebugLogAsync::Metrics> reports;
        unsigned int flushes = 0;
        void Write(const typename Queue::Record& record) { records.push_back(record); }
        void Flush() { ++flushes; }
        void Report(const DebugLogAsync::Metrics& metrics) { reports.push_back(metrics); }
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
        void Write(const typename Queue::Record& record)
        {
            Pause(BlockPoint::Write);
            RecordingSink<Queue>::Write(record);
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

        TEST_METHOD(OverflowPreservesBoundedDropOldestPolicyAndMetrics)
        {
            using Queue = DebugLogAsync::Transport<3, 32, 2>;
            Queue queue;
            queue.Open();
            for (int index = 0; index < 5; ++index)
            {
                const std::string text = std::to_string(index);
                Assert::IsTrue(queue.TryPublish(text.c_str(), index));
            }
            const auto before = queue.Snapshot();
            Assert::AreEqual(uint64_t(3), before.highWater);
            Assert::AreEqual(uint64_t(3), before.queueDepth);
            Assert::AreEqual(uint64_t(2), before.droppedFull);
            queue.Close();
            RecordingSink<Queue> sink;
            queue.Run(sink);
            Assert::AreEqual(static_cast<size_t>(3), sink.records.size());
            for (size_t index = 0; index < 3; ++index)
                Assert::AreEqual(std::to_string(index + 2), std::string(sink.records[index].message));
            const auto after = queue.Snapshot();
            Assert::AreEqual(after.enqueued, after.processed + after.droppedFull);
            Assert::AreEqual(uint64_t(0), after.queueDepth);
        }

        TEST_METHOD(ContendedProducerDropsBeforeBookkeepingLockIsReleased)
        {
            using Queue = DebugLogAsync::Transport<4, 32, 2>;
            Queue queue;
            queue.Open();
            std::promise<void> locked, release;
            auto lockedFuture = locked.get_future();
            auto releaseFuture = release.get_future();
            std::thread holder([&] {
                auto lock = DebugLogAsync::TransportTestAccess::Lock(queue);
                locked.set_value();
                releaseFuture.wait();
            });
            lockedFuture.wait();
            auto producer = std::async(std::launch::async, [&queue] {
                return queue.TryPublish("must-drop", 1);
            });
            const bool finishedBeforeRelease =
                producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            release.set_value();
            holder.join();
            const bool accepted = producer.get();
            Assert::IsTrue(finishedBeforeRelease, L"Producer waited on the logger mutex");
            Assert::IsFalse(accepted);
            Assert::AreEqual(uint64_t(1), queue.Snapshot().producerContentions);
            Assert::AreEqual(uint64_t(0), queue.Snapshot().enqueued);
            queue.Close();
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
                bool accepted = true;
                for (int index = 0; index < 12; ++index)
                    accepted = queue.TryPublish("while-disk-is-blocked", index + 3) && accepted;
                return accepted;
            });
            const bool completedBeforeDisk =
                producers.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            const auto blockedMetrics = queue.Snapshot();
            consumer.Finish();
            const bool accepted = producers.get();
            Assert::IsTrue(paused && completedBeforeDisk && accepted);
            Assert::AreEqual(uint64_t(4), blockedMetrics.queueDepth);
            Assert::AreEqual(uint64_t(4), blockedMetrics.highWater);
            Assert::AreEqual(uint64_t(8), blockedMetrics.droppedFull);
            const auto final = queue.Snapshot();
            Assert::AreEqual(final.enqueued, final.processed + final.droppedFull);
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
            Assert::AreEqual(uint64_t(1600), metrics.enqueued + metrics.producerContentions);
            Assert::AreEqual(metrics.enqueued, metrics.processed + metrics.droppedFull);
            Assert::AreEqual(uint64_t(16), metrics.highWater);
            Assert::AreEqual(static_cast<size_t>(16), sink.records.size());
            std::set<std::string> unique;
            for (const auto& record : sink.records) unique.insert(record.message);
            Assert::AreEqual(sink.records.size(), unique.size());
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
                void Write(const Queue::Record& record) { file << record.message << '\n'; }
                void Flush() { file.flush(); }
                void Report(const DebugLogAsync::Metrics&) {}
            } sink(directory.Path("vp.log"));
            queue.Run(sink);
            sink.file.close();
            Assert::AreEqual(std::string("prior session"), Contents(directory.Path("vp.log.0")));
            Assert::AreEqual(std::string("keep"), Contents(directory.Path("unrelated.log")));
            Assert::AreEqual(std::string("new session record\n"), Contents(directory.Path("vp.log")));
        }
    };
}
