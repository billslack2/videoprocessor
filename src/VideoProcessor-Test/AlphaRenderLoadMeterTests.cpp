#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaRenderLoadMeter.h>

#include <limits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaRenderLoadMeterTests)
	{
	public:
		TEST_METHOD(RequiresExactGenerationAndSourceSequence)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(4, 20, 1, 2.0, 0.1, 10.0, true);

			Assert::IsFalse(meter.RecordGpuFrame(3, 20, 1, 4.0, 2));
			Assert::IsFalse(meter.RecordGpuFrame(4, 20, 2, 4.0, 2));
			RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsFalse(snapshot.gpuValid);
			Assert::AreEqual(static_cast<uint64_t>(2),
				snapshot.unmatchedGpuSamples);

			Assert::IsTrue(meter.RecordGpuFrame(4, 20, 1, 4.0, 3));
			snapshot = meter.Snapshot();
			Assert::IsTrue(snapshot.gpuValid);
			Assert::AreEqual<size_t>(1, snapshot.gpuFrames);
			Assert::AreEqual(static_cast<uint64_t>(20),
				snapshot.latestGpuSourceSequence);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.latestGpuSubmissionSerial);
			Assert::AreEqual(static_cast<uint64_t>(3),
				snapshot.latestGpuLagFrames);
			Assert::AreEqual(4.0, snapshot.gpu.last, 0.001);
		}

		TEST_METHOD(ResetMakesUnresolvedOldFrameStale)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(1, 7, 1, 2.0, 0.1, 16.67, true);
			meter.Reset();

			Assert::IsFalse(meter.RecordGpuFrame(1, 7, 1, 5.0, 4));
			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsFalse(snapshot.gpuValid);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.unmatchedGpuSamples);
		}

		TEST_METHOD(WarmupFramesMatchButDoNotEnterStatistics)
		{
			AlphaRenderLoadMeter meter(100.0);
			meter.CommitFrame(1, 8, 1, 2.0, 0.1, 16.67, true);
			Assert::IsTrue(meter.RecordGpuFrame(1, 8, 1, 6.0, 2));

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsTrue(snapshot.settling);
			Assert::IsFalse(snapshot.gpuValid);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.warmupGpuSamples);
			Assert::AreEqual(static_cast<uint64_t>(0),
				snapshot.unmatchedGpuSamples);
		}

		TEST_METHOD(RepeatedSourceSequenceMatchesInSubmissionOrder)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(2, 9, 1, 1.0, 0.1, 20.0, true);
			meter.CommitFrame(2, 9, 2, 1.0, 0.1, 20.0, true);

			Assert::IsTrue(meter.RecordGpuFrame(2, 9, 1, 3.0, 2));
			Assert::IsTrue(meter.RecordGpuFrame(2, 9, 2, 7.0, 2));
			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual<size_t>(2, snapshot.gpuFrames);
			Assert::AreEqual(5.0, snapshot.gpu.average, 0.001);
			Assert::AreEqual(7.0, snapshot.gpu.peak, 0.001);
		}

		TEST_METHOD(ReportsSegmentCountForExactMatchedSubmission)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(2, 9, 4, 1.0, 0.1, 20.0, true);

			Assert::IsTrue(meter.RecordGpuFrame(2, 9, 4, 3.0, 2, 7));
			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual<size_t>(7, snapshot.latestGpuSegments);
		}

		TEST_METHOD(WorstLoadIsIndependentFromLargestMillisecondPeak)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(1, 1, 1, 1.0, 0.1, 40.0, true);
			Assert::IsTrue(meter.RecordGpuFrame(1, 1, 1, 8.0, 2));

			// A shorter interval at a faster refresh can consume more budget.
			meter.CommitFrame(1, 2, 2, 1.0, 0.1, 10.0, true);
			Assert::IsTrue(meter.RecordGpuFrame(1, 2, 2, 7.0, 2));

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual(8.0, snapshot.gpu.peak, 0.001);
			Assert::IsTrue(snapshot.gpuLoadPercentValid);
			Assert::AreEqual(70.0, snapshot.gpuLoadPercent, 0.001);
			Assert::AreEqual(7.0, snapshot.gpuWorstLoadMs, 0.001);
			Assert::AreEqual(10.0,
				snapshot.gpuWorstLoadFramePeriodMs, 0.001);
			Assert::AreEqual(8.0, snapshot.sessionGpuPeakMs, 0.001);
			Assert::IsTrue(snapshot.sessionGpuPercentValid);
			Assert::AreEqual(70.0, snapshot.sessionGpuPercent, 0.001);
			Assert::AreEqual(7.0, snapshot.sessionGpuWorstLoadMs, 0.001);
			Assert::AreEqual(10.0,
				snapshot.sessionGpuWorstLoadFramePeriodMs, 0.001);
		}

		TEST_METHOD(SuppressesPercentagesWithoutDisplayPeriod)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(1, 1, 1, 2.0, 0.1, 41.67, false);
			Assert::IsTrue(meter.RecordGpuFrame(1, 1, 1, 5.0, 2));

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsTrue(snapshot.gpuValid);
			Assert::IsFalse(snapshot.gpuLoadPercentValid);
			Assert::AreEqual(0.0, snapshot.gpuLoadPercent, 0.001);
			Assert::IsFalse(snapshot.sessionGpuPercentValid);
			Assert::AreEqual(0.0, snapshot.sessionGpuPercent, 0.001);
		}

		TEST_METHOD(BinaryLookupHandlesCircularWindow)
		{
			AlphaRenderLoadMeter meter(0.0);
			const uint64_t finalSerial = AlphaRenderLoadMeter::CAPACITY + 5;
			for (uint64_t serial = 1; serial <= finalSerial; ++serial)
				meter.CommitFrame(1, serial, serial, 1.0, 0.1, 16.67, true);

			Assert::IsFalse(meter.RecordGpuFrame(1, 1, 1, 2.0, 2));
			Assert::IsTrue(meter.RecordGpuFrame(1, finalSerial, finalSerial,
				4.0, 2));
			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual<size_t>(AlphaRenderLoadMeter::CAPACITY,
				snapshot.frames);
			Assert::AreEqual<size_t>(1, snapshot.gpuFrames);
			Assert::AreEqual(4.0, snapshot.gpu.last, 0.001);
		}

		TEST_METHOD(RejectsInvalidGpuDurations)
		{
			AlphaRenderLoadMeter meter(0.0);
			meter.CommitFrame(1, 1, 1, 2.0, 0.1, 16.67, true);
			Assert::IsFalse(meter.RecordGpuFrame(1, 1, 1,
				std::numeric_limits<double>::infinity(), 2));

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsFalse(snapshot.gpuValid);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.invalidGpuSamples);
		}
	};
}
