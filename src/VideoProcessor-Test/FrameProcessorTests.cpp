#include "pch.h"
#include "CppUnitTest.h"

#include <FrameProcessor.h>

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(FrameProcessorTests)
	{
	public:
		TEST_METHOD(ConversionProducesNeutralMetadataWithoutPresentationOrDelivery)
		{
			uint64_t ticks[] = { 1000, 3560 };
			size_t tickIndex = 0;
			bool converterCalled = false;
			FrameProcessor processor(
				[&](VideoFrame&, IMediaSample*)
				{
					converterCalled = true;
					return S_OK;
				},
				[&]() { return ticks[tickIndex++]; });
			VideoFrame source{};
			IMediaSample* const sample = reinterpret_cast<IMediaSample*>(0x1);
			FrameProcessorInput input;
			input.source = &source;
			input.sample = sample;
			input.epoch = { 12 };
			input.sourceFrameNumber = 99;
			input.captureTimestamp = 123456;
			input.sceneTimingGeneration = 23;

			const FrameProcessorResult result = processor.Process(input);

			Assert::IsTrue(converterCalled);
			Assert::IsTrue(result.producedFrame);
			Assert::AreEqual<HRESULT>(S_OK, result.result);
			Assert::AreEqual<uint32_t>(256, result.processingDurationUs);
			Assert::IsTrue(sample == result.frame.sample);
			Assert::AreEqual<uint64_t>(99, result.frame.frameNumber);
			Assert::AreEqual<uint64_t>(123456, result.frame.captureTimestamp);
			Assert::AreEqual<uint64_t>(12, result.frame.queueEpoch);
			Assert::AreEqual<uint64_t>(23, result.frame.sceneTimingGeneration);
		}

		TEST_METHOD(FailedConversionDoesNotPublishAProcessedFrame)
		{
			FrameProcessor processor(
				[](VideoFrame&, IMediaSample*) { return E_FAIL; },
				[]() { return 1000ULL; });
			VideoFrame source{};
			FrameProcessorInput input;
			input.source = &source;
			input.sample = reinterpret_cast<IMediaSample*>(0x1);

			const FrameProcessorResult result = processor.Process(input);

			Assert::IsFalse(result.producedFrame);
			Assert::AreEqual<HRESULT>(E_FAIL, result.result);
			Assert::IsNull(result.frame.sample);
		}

		TEST_METHOD(SceneAnalysisForwardsBoundaryEvidenceWithoutDelivery)
		{
			FrameProcessor processor;
			SceneDetector detector;
			std::vector<uint16_t> bright(64 * 36, static_cast<uint16_t>(512 << 6));
			std::vector<uint16_t> black(64 * 36, 0);
			auto input = [&](const std::vector<uint16_t>& pixels, uint64_t sequence)
			{
				return SceneAnalysisInput{ pixels.data(), 64, 36,
					64 * sizeof(uint16_t), sequence, static_cast<int64_t>(sequence * 417083),
					1, 417083, &detector };
			};

			const SceneAnalysisResult warming = processor.AnalyzeScene(input(bright, 1));
			const SceneAnalysisResult boundary = processor.AnalyzeScene(input(black, 2));

			Assert::IsTrue(warming.validInput);
			Assert::IsTrue(boundary.validInput);
			Assert::IsTrue(boundary.safeBoundary);
			Assert::AreEqual(0u, static_cast<unsigned int>(boundary.averageLuma));
		}

		TEST_METHOD(ActivePictureAnalysisStateBelongsToTheProcessor)
		{
			FrameProcessor processor;
			P010PlaneView missingFrame;
			missingFrame.width = 1920;
			missingFrame.height = 1080;

			Assert::IsTrue(processor.AnalyzeActivePicture({ missingFrame, 1, 60.0 }).analyzed);
			Assert::IsFalse(processor.AnalyzeActivePicture({ missingFrame, 2, 60.0 }).analyzed);
			processor.ResetActivePicture();
			Assert::IsTrue(processor.AnalyzeActivePicture({ missingFrame, 1, 60.0 }).analyzed);
		}
	};
}
