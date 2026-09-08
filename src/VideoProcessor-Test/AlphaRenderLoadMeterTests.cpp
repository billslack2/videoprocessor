#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaRenderLoadMeter.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaRenderLoadMeterTests)
	{
	public:
		TEST_METHOD(DefaultOsdPolicyPublishesFirstSampleAndKeepsAveraging)
		{
			AlphaRenderLoadMeter meter;
			meter.AddPass(2000000);
			meter.CommitFrame(2.0, 0.1, 16.67, true);
			Assert::IsTrue(meter.Snapshot().gpuValid);
			Assert::IsFalse(meter.Snapshot().settling);
			meter.AddPass(4000000);
			meter.CommitFrame(4.0, 0.1, 16.67, true);
			Assert::AreEqual(3.0, meter.Snapshot().gpu.average, 0.001);
		}
		TEST_METHOD(ExplicitWarmupStillHoldsSamples)
		{
			AlphaRenderLoadMeter meter(100.0, 100.0);
			meter.AddPass(2000000);
			meter.CommitFrame(2.0, 0.1, 16.67, true);
			Assert::IsTrue(meter.Snapshot().settling);
			Assert::IsFalse(meter.Snapshot().gpuValid);
		}
		TEST_METHOD(SumsResolvedLibplaceboPasses)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(1500000);
			meter.AddPass(2500000);
			meter.CommitFrame(2.0, 0.1, 16.67, true);

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsTrue(snapshot.gpuValid);
			Assert::AreEqual<size_t>(1, snapshot.gpuFrames);
			Assert::AreEqual(4.0, snapshot.gpu.average, 0.001);
			Assert::AreEqual(2, snapshot.gpuPasses);
		}

		TEST_METHOD(BeginFrameDiscardsFailedFramePendingPasses)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(9000000); // failed render: no CommitFrame
			meter.BeginFrame();
			meter.AddPass(3000000);
			meter.CommitFrame(2.0, 0.1, 16.67, true);

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual(3.0, snapshot.gpu.average, 0.001);
			Assert::AreEqual(1, snapshot.gpuPasses);
		}

		TEST_METHOD(WorstLoadUsesItsOwnDisplayPeriod)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(8000000);
			meter.CommitFrame(1.0, 0.1, 40.0, true);
			meter.BeginFrame();
			meter.AddPass(7000000);
			meter.CommitFrame(1.0, 0.1, 10.0, true);

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual(8.0, snapshot.gpu.peak, 0.001);
			Assert::IsTrue(snapshot.gpuLoadPercentValid);
			Assert::AreEqual(70.0, snapshot.gpuLoadPercent, 0.001);
			Assert::AreEqual(7.0, snapshot.gpuWorstLoadMs, 0.001);
			Assert::AreEqual(10.0,
				snapshot.gpuWorstLoadFramePeriodMs, 0.001);
			Assert::AreEqual(70.0, snapshot.sessionGpuPercent, 0.001);
		}

		TEST_METHOD(SuppressesPercentagesWithoutDisplayPeriod)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(5000000);
			meter.CommitFrame(1.0, 0.1, 41.67, false);

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::IsTrue(snapshot.gpuValid);
			Assert::IsFalse(snapshot.gpuLoadPercentValid);
			Assert::IsFalse(snapshot.sessionGpuPercentValid);
		}

		TEST_METHOD(PipelineChangeClearsOldSessionPeak)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(9000000);
			meter.CommitFrame(1.0, 0.1, 16.67, true);
			meter.ResetForPipelineChange();
			meter.BeginFrame();
			meter.AddPass(3000000);
			meter.CommitFrame(1.0, 0.1, 16.67, true);

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual(3.0, snapshot.sessionGpuPeakMs, 0.001);
			Assert::AreEqual(static_cast<uint64_t>(1),
				snapshot.sessionGpuFrames);
		}

		TEST_METHOD(BacklogResetPreservesSessionPeak)
		{
			AlphaRenderLoadMeter meter(0.0, 0.0);
			meter.BeginFrame();
			meter.AddPass(6000000);
			meter.CommitFrame(1.0, 0.1, 16.67, true);
			meter.Reset();

			const RendererRenderLoad snapshot = meter.Snapshot();
			Assert::AreEqual(6.0, snapshot.sessionGpuPeakMs, 0.001);
			Assert::AreEqual<size_t>(0, snapshot.frames);
		}
	};
}
