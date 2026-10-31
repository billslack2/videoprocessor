#include "pch.h"
#include "CppUnitTest.h"
#include <CaptureCallbackBoundary.h>
#include <blackmagic_decklink/DeckLinkCaptureDelivery.h>
#include <cmath>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(CaptureResyncHardeningTests)
	{
	public:
        TEST_METHOD(DeckLinkDeliveryMeasuresActualRateAtCommonInputTimings)
        {
            for (const double fps : { 24.0, 23.976, 50.0, 59.94, 60.0 })
            {
                DeckLinkCaptureDelivery delivery;
                delivery.Reset(fps);
                const auto period = static_cast<uint32_t>(std::round(1000000.0 / fps));
                delivery.Observe(1000000, period, 10.0);
                const int frames = static_cast<int>(std::ceil(fps * 5.0));
                for (int i = 1; i <= frames; ++i)
                    delivery.Observe(1000000 + int64_t(i) * period, period, 10.0 + i / fps);
                const auto stats = delivery.Snapshot(10.0 + frames / fps);
                Assert::AreEqual(fps, stats.expectedFps, 0.00001);
                Assert::AreEqual(fps, stats.deliveredFps, 0.00001);
                Assert::IsTrue(stats.windowSeconds >= 5.0);
                Assert::AreEqual(uint64_t(0), stats.recentMissedFrames);
                Assert::IsFalse(stats.losslessRgb24Capture);
            }
        }

        TEST_METHOD(DeckLinkDeliveryCountsMissingIntervalsSeparatelyFromDeliveredFrames)
        {
            DeckLinkCaptureDelivery delivery;
            delivery.Reset(50.0);
            delivery.Observe(1000000, 20000, 0.0);
            uint64_t total = 0;
            for (int i = 1; i <= 125; ++i)
                total += delivery.Observe(1000000 + int64_t(i) * 40000, 20000, i * 0.04).missedFrames;
            const auto stats = delivery.Snapshot(5.0);
            Assert::AreEqual(25.0, stats.deliveredFps, 0.00001);
            Assert::AreEqual(uint64_t(125), stats.recentMissedFrames);
            Assert::AreEqual(uint64_t(125), total);
            // The following complete window starts fresh; session totals belong to the device.
            for (int i = 1; i <= 250; ++i)
                delivery.Observe(6000000 + int64_t(i) * 20000, 20000, 5.0 + i * 0.02);
            const auto recovered = delivery.Snapshot(10.0);
            Assert::AreEqual(50.0, recovered.deliveredFps, 0.00001);
            Assert::AreEqual(uint64_t(0), recovered.recentMissedFrames);
        }

        TEST_METHOD(DeckLinkDeliveryUsesWallTimeInsteadOfClaimingHardwareRate)
        {
            DeckLinkCaptureDelivery delivery;
            delivery.Reset(50.0);
            delivery.Observe(1000000, 20000, 0.0);
            for (int i = 1; i <= 125; ++i)
                delivery.Observe(1000000 + int64_t(i) * 20000, 20000, i * 0.04);
            const auto stats = delivery.Snapshot(5.0);
            Assert::AreEqual(25.0, stats.deliveredFps, 0.00001);
            Assert::AreEqual(uint64_t(0), stats.recentMissedFrames);
        }

        TEST_METHOD(DeckLinkDeliveryResetsOnSignalLossFormatChangeAndStop)
        {
            DeckLinkCaptureDelivery delivery;
            delivery.Reset(50.0);
            delivery.Observe(1000000, 20000, 0.0);
            delivery.Observe(6000000, 20000, 5.0);
            Assert::IsTrue(delivery.Snapshot(5.0).windowSeconds > 0.0);
            // Signal loss and reacquisition do not count the outage as missed input frames.
            delivery.Reset(50.0);
            Assert::AreEqual(0.0, delivery.Snapshot(30.0).windowSeconds);
            Assert::AreEqual(uint64_t(0), delivery.Observe(31000000, 20000, 30.0).missedFrames);
            delivery.Reset(24.0);
            Assert::AreEqual(24.0, delivery.Snapshot(31.0).expectedFps);
            Assert::AreEqual(uint64_t(0), delivery.Observe(32000000, 41667, 31.0).missedFrames);
            delivery.Reset();
            Assert::AreEqual(0.0, delivery.Snapshot(32.0).expectedFps);
            Assert::AreEqual(0.0, delivery.Snapshot(32.0).windowSeconds);
        }

        TEST_METHOD(DeckLinkDeliveryRejectsDuplicateBackwardAndInvalidTimestamps)
        {
            for (const int64_t discontinuity : { int64_t(6000000), int64_t(500000), int64_t(-1) })
            {
                DeckLinkCaptureDelivery delivery;
                delivery.Reset(50.0);
                delivery.Observe(1000000, 20000, 0.0);
                delivery.Observe(6000000, 20000, 5.0);
                Assert::AreEqual(uint64_t(0), delivery.Observe(discontinuity, 20000, 5.1).missedFrames);
                Assert::AreEqual(0.0, delivery.Snapshot(5.1).windowSeconds);
            }
            DeckLinkCaptureDelivery delivery;
            delivery.Reset(50.0);
            Assert::AreEqual(uint64_t(0), delivery.Observe(1000000, 0, 0.0).missedFrames);
            Assert::AreEqual(0.0, delivery.Snapshot(0.0).windowSeconds);
        }

        TEST_METHOD(DeckLinkDeliveryAcceptsZeroAnchorAndQuantizedHardwareJitter)
        {
            DeckLinkCaptureDelivery delivery;
            delivery.SetExpectedFps(50.0);
            delivery.Observe(0, 20000, 0.0);
            Assert::AreEqual(uint64_t(2), delivery.Observe(60000, 20000, 0.06).missedFrames);
            Assert::AreEqual(uint64_t(0), delivery.Observe(79999, 20000, 0.08).missedFrames);
            Assert::AreEqual(uint64_t(0), delivery.Observe(100001, 20000, 0.10).missedFrames);
            Assert::AreEqual(0.0, delivery.Snapshot(0.10).windowSeconds);
        }

        TEST_METHOD(DeckLinkDeliveryExpiresCompletedWindowsWhenCallbacksStop)
        {
            DeckLinkCaptureDelivery delivery;
            delivery.Reset(50.0);
            delivery.Observe(1000000, 20000, 0.0);
            delivery.Observe(6000000, 20000, 5.0);
            Assert::IsTrue(delivery.Snapshot(10.0).windowSeconds > 0.0);
            Assert::AreEqual(0.0, delivery.Snapshot(10.01).windowSeconds);
        }

		TEST_METHOD(PreservesCallbackResults)
		{
			int reports = 0;
			for (HRESULT expected : { S_OK, S_FALSE, E_INVALIDARG })
				Assert::AreEqual<HRESULT>(expected, CaptureCallbackBoundary(
					[&]() { return expected; }, [&](const char*) { ++reports; }));
			Assert::AreEqual(0, reports);
		}

		TEST_METHOD(ContainsFrameFailureAndAllowsNextCallback)
		{
			std::string diagnostic;
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw std::runtime_error("frame bytes unavailable"); },
				[&](const char* error) { diagnostic = error; }));
			Assert::IsTrue(diagnostic == "frame bytes unavailable");
			Assert::AreEqual<HRESULT>(S_OK, CaptureCallbackBoundary(
				[]() { return S_OK; }, [](const char*) {}));
		}

		TEST_METHOD(ContainsUnknownExceptionAndFailingReporter)
		{
			bool reported = false;
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw 42; },
				[&](const char*) { reported = true; throw std::bad_alloc(); }));
			Assert::IsTrue(reported);
			Assert::AreEqual<HRESULT>(E_FAIL, CaptureCallbackBoundary(
				[]() -> HRESULT { throw std::bad_alloc(); },
				[](const char*) { throw std::runtime_error("logger failed"); }));
		}

		TEST_METHOD(ExhaustedAllocatorReturnsAndRecoversAfterSampleRelease)
		{
			HRESULT result = S_OK;
			CComPtr<IMemAllocator> allocator = new CMemAllocator(NAME("resync test"), nullptr, &result);
			Assert::AreEqual<HRESULT>(S_OK, result);
			ALLOCATOR_PROPERTIES requested{ 1, 256, 1, 0 }, actual{};
			Assert::AreEqual<HRESULT>(S_OK, allocator->SetProperties(&requested, &actual));
			Assert::AreEqual<HRESULT>(S_OK, allocator->Commit());
			CComPtr<IMediaSample> held, next;
			Assert::AreEqual<HRESULT>(S_OK, allocator->GetBuffer(&held, nullptr, nullptr, AM_GBF_NOWAIT));
			Assert::AreEqual<HRESULT>(VFW_E_TIMEOUT, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
			Assert::IsTrue(next == nullptr);
			held.Release();
			Assert::AreEqual<HRESULT>(S_OK, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
			next.Release();
			Assert::AreEqual<HRESULT>(S_OK, allocator->Decommit());
			Assert::AreEqual<HRESULT>(VFW_E_NOT_COMMITTED, allocator->GetBuffer(&next, nullptr, nullptr, AM_GBF_NOWAIT));
		}
	};
}
