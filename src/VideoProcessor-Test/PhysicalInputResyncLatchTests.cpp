#include "pch.h"
#include "CppUnitTest.h"
#include <PhysicalInputResyncLatch.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
TEST_CLASS(PhysicalInputResyncLatchTests)
{
    static const ACaptureDevice* SourceA()
    {
        static int identity;
        return reinterpret_cast<const ACaptureDevice*>(&identity);
    }
    static const ACaptureDevice* SourceB()
    {
        static int identity;
        return reinterpret_cast<const ACaptureDevice*>(&identity);
    }
    static CaptureDeviceCardStateComPtr Card(InputLocked locked = InputLocked::YES,
        unsigned width = 3840, unsigned height = 2160,
        unsigned timeScale = 60000, unsigned frameDuration = 1001,
        bool interlaced = false,
        DisplayMode::FieldOrder order = DisplayMode::FieldOrder::Unknown)
    {
        CaptureDeviceCardStateComPtr state = new CaptureDeviceCardState();
        state->inputLocked = locked;
        state->inputDisplayMode = std::make_shared<DisplayMode>(
            width, height, interlaced, timeScale, frameDuration, order);
        state->inputEncoding = ColorFormat::YCbCr422;
        state->inputBitDepth = BitDepth::BITDEPTH_10BIT;
        return state;
    }
    static bool Observe(PhysicalInputResyncLatch& latch, uint64_t sequence,
        const CaptureDeviceCardStateComPtr& state)
    {
        return latch.Observe(SourceA(), 7, sequence, *state);
    }
public:
    TEST_METHOD(StartupUnknownOrUnlockedThenInitialLockOnlyEstablishesBaseline)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card(InputLocked::UNKNOWN)));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::NO)));
        Assert::IsFalse(Observe(latch, 3, Card()));
        Assert::IsFalse(Observe(latch, 4, Card()));
    }

    TEST_METHOD(SameFormatLossAndReacquisitionNominatesExactlyOnce)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        for (uint64_t sequence = 2; sequence <= 5; ++sequence)
            Assert::IsFalse(Observe(latch, sequence, Card(InputLocked::NO)));
        Assert::IsTrue(Observe(latch, 6, Card()));
        for (uint64_t sequence = 7; sequence <= 12; ++sequence)
            Assert::IsFalse(Observe(latch, sequence, Card()));
        Assert::IsFalse(Observe(latch, 13, Card(InputLocked::NO)));
        Assert::IsTrue(Observe(latch, 14, Card()));
    }

    TEST_METHOD(UnknownNeitherArmsLossNorCancelsPreviouslyObservedLoss)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::UNKNOWN)));
        Assert::IsFalse(Observe(latch, 3, Card()));
        Assert::IsFalse(Observe(latch, 4, Card(InputLocked::NO)));
        Assert::IsFalse(Observe(latch, 5, Card(InputLocked::UNKNOWN)));
        Assert::IsTrue(Observe(latch, 6, Card()));
    }

    TEST_METHOD(CompleteNativeSizeRateAndWireEncodingChangesNominateReset)
    {
        for (int change = 0; change < 7; ++change)
        {
            PhysicalInputResyncLatch latch;
            latch.BeginRun(SourceA(), 7);
            Assert::IsFalse(Observe(latch, 1, Card()));
            auto changed = Card();
            switch (change)
            {
            case 0: changed = Card(InputLocked::YES, 1920, 2160); break;
            case 1: changed = Card(InputLocked::YES, 3840, 1080); break;
            case 2: changed = Card(InputLocked::YES, 3840, 2160, 24000, 1001); break;
            case 3: changed->inputEncoding = ColorFormat::RGB444; break;
            case 4: changed->inputBitDepth = BitDepth::BITDEPTH_8BIT; break;
            case 5: changed->inputBitDepth = BitDepth::BITDEPTH_12BIT; break;
            case 6: changed = Card(InputLocked::YES, 3840, 2160, 60000, 1001,
                true, DisplayMode::FieldOrder::TopFirst); break;
            }
            Assert::IsTrue(Observe(latch, 2, changed));
            Assert::IsFalse(Observe(latch, 3, changed));
        }
    }

    TEST_METHOD(KnownInterlacedFieldOrderChangeIsNativeButProgressiveOrderIsNot)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::TopFirst)));
        Assert::IsTrue(Observe(latch, 2, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::BottomFirst)));
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::YES, 3840, 2160,
            60000, 1001, false, DisplayMode::FieldOrder::TopFirst)));
    }

    TEST_METHOD(UnknownFieldOrderCannotEraseKnownOrderOfUnchangedNativeSignal)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::TopFirst)));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::Unknown)));
        Assert::IsTrue(Observe(latch, 3, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::BottomFirst)));
        Assert::IsFalse(Observe(latch, 4, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::Unknown)));
        Assert::IsFalse(Observe(latch, 5, Card(InputLocked::YES, 1920, 1080,
            30000, 1001, true, DisplayMode::FieldOrder::BottomFirst)));
    }

    TEST_METHOD(EquivalentRateFractionsAndInformationalMetadataDoNotReset)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        auto same = Card(InputLocked::YES, 3840, 2160, 120000, 2002);
        same->other.push_back(CString(_T("HDR metadata updated")));
        same->other.push_back(CString(_T("PCIe link width: 8")));
        Assert::IsFalse(Observe(latch, 2, same));
        same->other.clear();
        Assert::IsFalse(Observe(latch, 3, same));
    }

    TEST_METHOD(PartialOrUnlockedFormatCannotInventFormatChange)
    {
        for (int missing = 0; missing < 3; ++missing)
        {
            PhysicalInputResyncLatch latch;
            latch.BeginRun(SourceA(), 7);
            Assert::IsFalse(Observe(latch, 1, Card()));
            auto partial = Card(InputLocked::YES, 1920, 1080);
            switch (missing)
            {
            case 0: partial->inputDisplayMode.reset(); break;
            case 1: partial->inputEncoding = ColorFormat::UNKNOWN; break;
            case 2: partial->inputBitDepth = BitDepth::UNKNOWN; break;
            }
            Assert::IsFalse(Observe(latch, 2, partial));
            Assert::IsFalse(Observe(latch, 3, Card()));
            Assert::IsFalse(Observe(latch, 4, Card(InputLocked::NO, 1920, 1080)));
            Assert::IsTrue(Observe(latch, 5, Card(InputLocked::YES, 1920, 1080)));
            Assert::IsFalse(Observe(latch, 6, Card(InputLocked::YES, 1920, 1080)));
        }
    }

    TEST_METHOD(RelockBeforeCompleteFormatDoesNotResetAgainWhenFormatArrives)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::NO)));
        auto partial = Card();
        partial->inputDisplayMode.reset();
        Assert::IsTrue(Observe(latch, 3, partial));
        Assert::IsFalse(Observe(latch, 4, partial));
        Assert::IsFalse(Observe(latch, 5, Card(InputLocked::YES, 1920, 1080)));
        Assert::IsFalse(Observe(latch, 6, Card(InputLocked::YES, 1920, 1080)));
        Assert::IsTrue(Observe(latch, 7, Card()));
    }

    TEST_METHOD(StaleSourceRunAndPublicationCannotArmOrOverwriteCurrentBaseline)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 10, Card()));
        const auto lost = Card(InputLocked::NO);
        Assert::IsFalse(latch.Observe(SourceB(), 7, 100, *lost));
        Assert::IsFalse(latch.Observe(SourceA(), 6, 100, *lost));
        Assert::IsFalse(latch.Observe(nullptr, 7, 100, *lost));
        Assert::IsFalse(latch.Observe(SourceA(), 0, 100, *lost));
        Assert::IsFalse(Observe(latch, 0, lost));
        Assert::IsFalse(Observe(latch, 9, lost));
        Assert::IsFalse(Observe(latch, 10, lost));
        Assert::IsFalse(Observe(latch, 11, Card()));
        Assert::IsFalse(Observe(latch, 12, lost));
        Assert::IsFalse(latch.Observe(SourceB(), 7, 200, *Card()));
        Assert::IsFalse(latch.Observe(SourceA(), 8, 200, *Card()));
        Assert::IsTrue(Observe(latch, 13, Card()));
    }

    TEST_METHOD(NewRunAndExplicitResetDoNotCarryPriorLossOrFormat)
    {
        PhysicalInputResyncLatch latch;
        latch.BeginRun(SourceA(), 7);
        Assert::IsFalse(Observe(latch, 1, Card()));
        Assert::IsFalse(Observe(latch, 2, Card(InputLocked::NO)));
        latch.BeginRun(SourceA(), 8);
        Assert::IsFalse(latch.Observe(SourceA(), 7, 3, *Card()));
        Assert::IsFalse(latch.Observe(SourceA(), 8, 1,
            *Card(InputLocked::YES, 1920, 1080)));
        latch.Reset();
        Assert::IsFalse(latch.Observe(SourceA(), 8, 2, *Card(InputLocked::NO)));
        Assert::IsFalse(latch.Observe(SourceA(), 8, 3, *Card()));
        latch.BeginRun(SourceB(), 9);
        Assert::IsFalse(latch.Observe(SourceB(), 9, 1, *Card()));
    }
};
}
