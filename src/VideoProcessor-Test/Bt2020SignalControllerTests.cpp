#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/Bt2020SignalController.h>
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
    using Controller = Bt2020Signal::Controller<FakeAvi>;
    void Finish(Controller& controller, uint64_t start)
    { for (auto offset : { 0, 500, 1500, 3000, 5000, 8000 }) controller.Pump(start + offset); }
}
namespace UnitTests
{
    TEST_CLASS(Bt2020SignalControllerTests)
    {
    public:
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
