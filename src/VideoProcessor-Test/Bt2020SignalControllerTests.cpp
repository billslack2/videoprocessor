#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/Bt2020SignalController.h>
#include <vprenderer/Bt2020SignalWorker.h>
#include <future>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace
{
    struct FakeAvi
    {
        struct Frame { Bt2020Signal::Color color{}; int unrelated = 1; };
        struct WriteRecord { std::string display; Frame frame; };
        std::map<std::string, Frame> displays;
        std::vector<WriteRecord> writes;
        bool readable = true, writable = true, echo = true;
        bool Read(const std::string& name, Frame& frame)
        { if (!readable) return false; frame = displays[name]; return true; }
        bool Write(const std::string& name, Frame& frame)
        {
            writes.push_back({name, frame});
            if (writable && echo) displays[name] = frame;
            return writable;
        }
        static Bt2020Signal::Color GetColor(const Frame& frame) { return frame.color; }
        static void SetColor(Frame& frame, Bt2020Signal::Color color) { frame.color = color; }
        void Report(const std::string&, bool, unsigned, bool, bool) {}
    };
    struct DriverGate
    {
        std::string target;
        bool write = false, used = false;
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, released = false;
        DriverGate(std::string name, bool isWrite) : target(std::move(name)), write(isWrite) {}
        void Enter()
        {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true; changed.notify_all();
            changed.wait(lock, [this] { return released; });
        }
        bool WaitEntered()
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, std::chrono::seconds(2), [this] { return entered; });
        }
        void Release()
        { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
    };
    struct BlockingAviState
    {
        std::mutex mutex;
        std::vector<std::shared_ptr<DriverGate>> gates;
        std::map<std::string, FakeAvi::Frame> displays;
        std::vector<FakeAvi::WriteRecord> writes;
        std::vector<std::thread::id> backendThreads;
        bool writable = true;
        unsigned destructions = 0;
        void SetWritable(bool value) { std::lock_guard<std::mutex> lock(mutex); writable = value; }
        std::shared_ptr<DriverGate> Begin(const std::string& target, bool write)
        {
            std::lock_guard<std::mutex> lock(mutex);
            backendThreads.push_back(std::this_thread::get_id());
            for (const auto& gate : gates)
                if (!gate->used && gate->target == target && gate->write == write)
                { gate->used = true; return gate; }
            return {};
        }
        std::vector<FakeAvi::WriteRecord> Writes()
        { std::lock_guard<std::mutex> lock(mutex); return writes; }
        Bt2020Signal::Color ColorOf(const std::string& target)
        { std::lock_guard<std::mutex> lock(mutex); return displays[target].color; }
        void CheckWorkerOwnership()
        {
            std::lock_guard<std::mutex> lock(mutex);
            Assert::AreEqual(1u, destructions);
            Assert::IsFalse(backendThreads.empty());
            for (const auto& thread : backendThreads)
                Assert::IsTrue(thread == backendThreads.front() && thread != std::this_thread::get_id(),
                    L"Backend construction, calls, restoration and destruction must all use the worker");
        }
    };
    struct BlockingAvi
    {
        using Frame = FakeAvi::Frame;
        std::shared_ptr<BlockingAviState> state;
        explicit BlockingAvi(std::shared_ptr<BlockingAviState> value) : state(std::move(value))
        { std::lock_guard<std::mutex> lock(state->mutex); state->backendThreads.push_back(std::this_thread::get_id()); }
        ~BlockingAvi()
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->backendThreads.push_back(std::this_thread::get_id()); ++state->destructions;
        }
        bool Read(const std::string& target, Frame& frame)
        {
            if (const auto gate = state->Begin(target, false)) gate->Enter();
            std::lock_guard<std::mutex> lock(state->mutex);
            const auto item = state->displays.emplace(target, Frame{ Bt2020Signal::Rec709(), 1 });
            frame = item.first->second; return true;
        }
        bool Write(const std::string& target, Frame& frame)
        {
            if (const auto gate = state->Begin(target, true)) gate->Enter();
            std::lock_guard<std::mutex> lock(state->mutex);
            state->writes.push_back({ target, frame });
            if (state->writable) state->displays[target] = frame;
            return state->writable;
        }
        static Bt2020Signal::Color GetColor(const Frame& frame) { return frame.color; }
        static void SetColor(Frame& frame, Bt2020Signal::Color color) { frame.color = color; }
        void Report(const std::string&, bool, unsigned, bool, bool) {}
    };
    using SignalWorker = Bt2020Signal::WorkerService<BlockingAvi>;
    struct ReleaseDriverGates
    {
        std::shared_ptr<BlockingAviState> state;
        ~ReleaseDriverGates() { for (const auto& gate : state->gates) gate->Release(); }
    };
    template<class Predicate> bool WaitForSignal(Predicate predicate)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    using Controller = Bt2020Signal::Controller<FakeAvi>;
    void Finish(Controller& controller, uint64_t start)
    { for (auto offset : { 0, 500, 1500, 3000, 5000, 8000 }) controller.Pump(start + offset); }
}
namespace UnitTests
{
    TEST_CLASS(Bt2020SignalControllerTests)
    {
    public:
        TEST_METHOD(WorkerReadStallKeepsRequestsAndStatusResponsiveAndSkipsObsoleteSet)
        {
            auto state = std::make_shared<BlockingAviState>();
            auto gate = std::make_shared<DriverGate>("Old", false);
            state->gates.push_back(gate);
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            ReleaseDriverGates release{ state };
            worker.Request("Old", true, false);
            Assert::IsTrue(gate->WaitEntered());
            auto publication = std::async(std::launch::async, [&] {
                for (int i = 0; i < 20; ++i) worker.Request("Intermediate" + std::to_string(i), true, false);
                worker.Request("Newest", true, false);
                (void)worker.Status(); (void)worker.IsActive();
                (void)worker.IsReadbackVerified(); (void)worker.Failed();
                return worker.ReadSnapshot();
            });
            const bool returnedWhileDriverBlocked = publication.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
            if (!returnedWhileDriverBlocked) gate->Release();
            const auto pending = publication.get();
            gate->Release();
            Assert::IsTrue(returnedWhileDriverBlocked,
                L"Render-side publication and cached queries must return before the driver is released");
            Assert::IsTrue(pending.target == "Newest" && pending.pending && !pending.active);
            Assert::IsTrue(pending.coalesced >= 20 && pending.driverActiveAgeUs > 0);
            Assert::IsTrue(WaitForSignal([&] { return worker.IsReadbackVerified(); }));
            const auto ready = worker.ReadSnapshot();
            Assert::IsTrue(ready.target == "Newest" && ready.completedGeneration == ready.generation);
            Assert::IsTrue(ready.obsoleteCompletions >= 1 && ready.driverCalls > 0 && ready.driverPeakUs > 0);
            for (const auto& write : state->Writes())
                Assert::IsTrue(write.display == "Newest",
                    L"An obsolete stalled read must not issue SET or service superseded mailbox targets");
            Assert::IsTrue(worker.Restore());
            Assert::IsTrue(worker.Stop());
            state->CheckWorkerOwnership();
        }

        TEST_METHOD(WorkerWriteStallCannotPublishObsoleteSuccessAndRestoresOldTarget)
        {
            auto state = std::make_shared<BlockingAviState>();
            auto oldWrite = std::make_shared<DriverGate>("A", true);
            auto newRead = std::make_shared<DriverGate>("B", false);
            state->gates = { oldWrite, newRead };
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            ReleaseDriverGates release{ state };
            worker.Request("A", true, false);
            Assert::IsTrue(oldWrite->WaitEntered());
            auto publication = std::async(std::launch::async, [&] {
                worker.Request("B", true, false);
                return worker.ReadSnapshot();
            });
            const bool responsive = publication.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready;
            if (!responsive) oldWrite->Release();
            const auto pending = publication.get();
            oldWrite->Release();
            Assert::IsTrue(responsive);
            Assert::IsTrue(newRead->WaitEntered());
            const auto duringNewRead = worker.ReadSnapshot();
            Assert::IsTrue(duringNewRead.target == "B" && !duringNewRead.active && !duringNewRead.verified);
            Assert::AreEqual(pending.generation, duringNewRead.generation);
            Assert::IsTrue(duringNewRead.completedGeneration < duringNewRead.generation,
                L"Completion of A's unavoidable in-flight SET must not publish success for B");
            Assert::IsTrue(state->ColorOf("A") == Bt2020Signal::Rec709(),
                L"The old target remains owned and is disengaged before the new target succeeds");
            newRead->Release();
            Assert::IsTrue(WaitForSignal([&] { return worker.IsReadbackVerified(); }));
            Assert::IsTrue(state->ColorOf("B") == Bt2020Signal::Bt2020());
            Assert::IsTrue(worker.Restore());
            Assert::IsTrue(state->ColorOf("B") == Bt2020Signal::Rec709());
            Assert::IsTrue(worker.Stop());
            state->CheckWorkerOwnership();
        }

        TEST_METHOD(WorkerRetirementRetriesDirtyFailureWithoutReopeningEnableRequests)
        {
            auto state = std::make_shared<BlockingAviState>();
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            worker.Request("Projector", true, false);
            Assert::IsTrue(WaitForSignal([&] { return worker.IsReadbackVerified(); }));
            state->SetWritable(false);
            Assert::IsFalse(worker.Restore());
            Assert::IsTrue(worker.Failed());
            const auto failedGeneration = worker.ReadSnapshot().generation;
            worker.Request("Projector", true, true);
            Assert::AreEqual(failedGeneration, worker.ReadSnapshot().generation,
                L"Retirement is terminal for new enables; only restoration may retry");
            state->SetWritable(true);
            Assert::IsTrue(worker.Restore());
            Assert::IsTrue(state->ColorOf("Projector") == Bt2020Signal::Rec709());
            const auto restoredWrites = state->Writes().size();
            Assert::IsTrue(worker.Stop());
            Assert::AreEqual(restoredWrites, state->Writes().size(),
                L"Successful restoration must not be repeated by destruction");
            state->CheckWorkerOwnership();
        }

        TEST_METHOD(WorkerShutdownAbandonDoesNotRetryFailedRestoration)
        {
            auto state = std::make_shared<BlockingAviState>();
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            worker.Request("Projector", true, false);
            Assert::IsTrue(WaitForSignal([&] { return worker.IsReadbackVerified(); }));
            state->SetWritable(false);
            Assert::IsFalse(worker.Restore());
            const auto writes = state->Writes().size();
            worker.AbandonPendingRestoreForShutdown();
            Assert::IsTrue(worker.Stop());
            Assert::AreEqual(writes, state->Writes().size(),
                L"The explicit shutdown exception must cancel destructor restoration too");
            state->CheckWorkerOwnership();
        }

        TEST_METHOD(WorkerPendingRestartSurvivesOrdinaryDuplicateRequests)
        {
            auto state = std::make_shared<BlockingAviState>();
            auto gate = std::make_shared<DriverGate>("Projector", false);
            state->gates.push_back(gate);
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            ReleaseDriverGates release{ state };
            worker.Request("Projector", true, false);
            Assert::IsTrue(gate->WaitEntered());
            worker.Request("Projector", true, true);
            const auto restarted = worker.ReadSnapshot().generation;
            worker.Request("Projector", true, false);
            Assert::AreEqual(restarted, worker.ReadSnapshot().generation);
            gate->Release();
            Assert::IsTrue(WaitForSignal([&] { return worker.IsReadbackVerified(); }));
            Assert::AreEqual(1ull, static_cast<unsigned long long>(worker.ReadSnapshot().duplicates));
            const auto completed = worker.ReadSnapshot();
            Assert::AreEqual(restarted, completed.completedGeneration);
            Assert::IsTrue(worker.Restore());
        }

        TEST_METHOD(WorkerRetirementDuringSlowReadCancelsEnableBeforeAnySet)
        {
            auto state = std::make_shared<BlockingAviState>();
            auto gate = std::make_shared<DriverGate>("Projector", false);
            state->gates.push_back(gate);
            SignalWorker worker([state] { return std::make_unique<BlockingAvi>(state); });
            ReleaseDriverGates release{ state };
            worker.Request("Projector", true, false);
            Assert::IsTrue(gate->WaitEntered());
            auto restore = std::async(std::launch::async, [&] { return worker.Restore(); });
            const bool restorePublished = WaitForSignal([&] {
                return worker.Status().find("restoration pending") != std::string::npos;
            });
            gate->Release();
            Assert::IsTrue(restore.get());
            Assert::IsTrue(restorePublished);
            Assert::IsTrue(state->Writes().empty(),
                L"A canceled enable read must never advance to SET during retirement");
            Assert::IsTrue(worker.Stop());
            state->CheckWorkerOwnership();
        }
        TEST_METHOD(RepeatsAfterSuccessAndStopsAtEightSeconds)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); Finish(c, 0); c.Pump(100000);
            Assert::AreEqual(size_t(6), io.writes.size());
            Assert::IsTrue(c.IsVerified());
            for (auto& write : io.writes) Assert::IsTrue(write.frame.color == Bt2020Signal::Bt2020());
        }
        TEST_METHOD(F5CancelsDelayedF6AndRepeatedlyClearsTheFlag)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            c.Request("Epson", false, 100, false); Finish(c, 100);
            Assert::AreEqual(size_t(7), io.writes.size());
            for (size_t i = 1; i < io.writes.size(); ++i)
                Assert::IsTrue(io.writes[i].frame.color == Bt2020Signal::Rec709());
            Assert::IsFalse(c.IsActive());
        }
        TEST_METHOD(F6CancelsDelayedF5)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            c.Request("Epson", false, 10, false); c.Pump(10);
            c.Request("Epson", true, 20, false); Finish(c, 20);
            for (size_t i = 2; i < io.writes.size(); ++i)
                Assert::IsTrue(io.writes[i].frame.color == Bt2020Signal::Bt2020());
        }
        TEST_METHOD(ModeChangeAfterSuccessUsesFreshFrameAndRetransmits)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            io.displays["Epson"] = { Bt2020Signal::Rec709(), 42 };
            c.Pump(499); Assert::AreEqual(size_t(1), io.writes.size());
            c.Pump(500);
            Assert::IsTrue(io.writes.back().frame.color == Bt2020Signal::Bt2020());
            Assert::AreEqual(42, io.writes.back().frame.unrelated);
            Finish(c, 10000); // Delayed wake does not execute a catch-up burst.
            c.Request("Epson", true, 30000, true); c.Pump(30000);
            Assert::IsTrue(c.IsVerified());
        }
        TEST_METHOD(TransientReadFailureDoesNotDisableDesiredState)
        {
            FakeAvi io; Controller c(io); io.readable = false;
            c.Request("Epson", true, 0, false); c.Pump(0);
            Assert::IsFalse(c.Failed());
            io.readable = true; c.Pump(500);
            Assert::IsTrue(c.IsVerified());
        }
        TEST_METHOD(UnverifiedSendStillOwnsDisengagement)
        {
            FakeAvi io; Controller c(io); io.echo = false;
            c.Request("Epson", true, 0, false); c.Pump(0);
            Assert::IsTrue(c.IsActive()); Assert::IsFalse(c.IsVerified());
            c.Request("Epson", false, 10, false); c.Pump(10);
            Assert::IsTrue(io.writes.back().frame.color == Bt2020Signal::Rec709());
        }
        TEST_METHOD(FailedSetStillRequiresCleanupAndRetainsItAfterFailure)
        {
            FakeAvi io; Controller c(io); io.writable = false;
            c.Request("Epson", true, 0, false); Finish(c, 0);
            Assert::IsTrue(c.Failed()); Assert::IsFalse(c.Retire());
            auto count = io.writes.size(); c.Pump(50000);
            Assert::AreEqual(count, io.writes.size());
            io.writable = true; Assert::IsTrue(c.Retire());
            Assert::IsFalse(io.writes.back().frame.color == Bt2020Signal::Bt2020());
        }
        TEST_METHOD(OutputChangeClearsOldOutputWithoutTouchingPrimary)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            c.Request("Second", true, 10, false); Finish(c, 10);
            Assert::IsTrue(io.displays["Second"].color == Bt2020Signal::Bt2020());
            Assert::IsFalse(io.displays["Epson"].color == Bt2020Signal::Bt2020());
            Assert::IsTrue(io.displays.find("Primary") == io.displays.end());
            c.Request("", true, 10000, false); c.Pump(10000);
            Assert::IsTrue(c.Failed());
            Assert::IsFalse(io.displays["Second"].color == Bt2020Signal::Bt2020());
        }
        TEST_METHOD(RetirementNeverRestoresInheritedBt2020OrStaleTimingFields)
        {
            FakeAvi io; Controller c(io);
            io.displays["Epson"] = { Bt2020Signal::Bt2020(), 1 };
            c.Request("Epson", true, 0, false); c.Pump(0);
            io.displays["Epson"].unrelated = 99;
            Assert::IsTrue(c.Retire());
            Assert::IsTrue(io.writes.back().frame.color == Bt2020Signal::Rec709());
            Assert::AreEqual(99, io.writes.back().frame.unrelated);
            auto count = io.writes.size(); c.Pump(9000);
            Assert::AreEqual(count, io.writes.size());
        }
        TEST_METHOD(DisengagementFailureIsReportedAndCanRecoverAfterModeset)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            io.writable = false;
            c.Request("Epson", false, 10, false); Finish(c, 10);
            Assert::IsTrue(c.Status().find("FAILED") != std::string::npos);
            io.writable = true;
            c.Request("Epson", false, 9000, true); Finish(c, 9000);
            Assert::IsTrue(c.Status().find("readback matched") != std::string::npos);
            Assert::IsTrue(io.displays["Epson"].color == Bt2020Signal::Rec709());
        }
        TEST_METHOD(ReleasedOutputIsNotWrittenAgainDuringRetirement)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); c.Pump(0);
            c.Request("Second", true, 10, false); Finish(c, 10);
            const auto count = io.writes.size();
            Assert::IsTrue(c.Retire());
            Assert::AreEqual(count + 1, io.writes.size());
            Assert::IsTrue(io.writes.back().display == "Second");
        }
        TEST_METHOD(UnsupportedOutputStopsAfterBoundedAttemptsWithoutWriting)
        {
            FakeAvi io; Controller c(io); io.readable = false;
            c.Request("Monitor", true, 0, false); Finish(c, 0);
            Assert::IsTrue(c.Failed()); Assert::IsTrue(io.writes.empty());
            c.Pump(100000); Assert::IsTrue(io.writes.empty());
            Assert::IsTrue(c.Retire());
        }
        TEST_METHOD(UnrelatedProfileUpdatesDoNotRestartRetries)
        {
            FakeAvi io; Controller c(io);
            c.Request("Epson", true, 0, false); Finish(c, 0);
            c.Request("Epson", true, 9000, false); c.Pump(9000);
            Assert::AreEqual(size_t(6), io.writes.size());
            c.Request("Epson", true, 10000, true); c.Pump(10000);
            Assert::AreEqual(size_t(7), io.writes.size());
        }
    };
}
