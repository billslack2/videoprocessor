#include "pch.h"
#include "CppUnitTest.h"

#include <SceneDetector.h>

#include <algorithm>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(SceneDetectorTests)
	{
	public:
		static SceneDetectorInput Input(const std::vector<uint16_t>& pixels,
			uint64_t sequence, uint64_t generation = 1)
		{
			return { pixels.data(), 64, 36, 64 * sizeof(uint16_t), sequence,
				static_cast<int64_t>(sequence * 417083), generation, 417083, true };
		}

		TEST_METHOD(ReportsWarmingThenActiveForIdenticalP010Frames)
		{
			SceneDetector detector;
			std::vector<uint16_t> frame(64 * 36, static_cast<uint16_t>(512 << 6));
			const SceneDetectorResult first = detector.Analyze(Input(frame, 1));
			const SceneDetectorResult second = detector.Analyze(Input(frame, 2));

			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Warming), static_cast<int>(first.status));
			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Active), static_cast<int>(second.status));
			Assert::IsFalse(first.safeBoundary);
			Assert::IsFalse(second.safeBoundary);
		}

		TEST_METHOD(ReportsOneBoundaryForANearBlackInterval)
		{
			SceneDetector detector;
			std::vector<uint16_t> bright(64 * 36, static_cast<uint16_t>(512 << 6));
			std::vector<uint16_t> black(64 * 36, 0);
			detector.Analyze(Input(bright, 1));
			const SceneDetectorResult boundary = detector.Analyze(Input(black, 2));
			const SceneDetectorResult continued = detector.Analyze(Input(black, 3));

			Assert::IsTrue(boundary.safeBoundary);
			Assert::IsTrue(boundary.eventId > 0);
			Assert::AreEqual(static_cast<uint64_t>(2), boundary.sourceSequence);
			Assert::IsFalse(continued.safeBoundary);
		}

		TEST_METHOD(ConfirmsAHardCutAfterTheNewSceneSettles)
		{
			SceneDetector detector;
			std::vector<uint16_t> oldScene(64 * 36, static_cast<uint16_t>(256 << 6));
			std::vector<uint16_t> newScene(64 * 36, static_cast<uint16_t>(768 << 6));
			detector.Analyze(Input(oldScene, 1));
			const SceneDetectorResult candidate = detector.Analyze(Input(newScene, 2));
			const SceneDetectorResult confirmed = detector.Analyze(Input(newScene, 3));

			Assert::IsFalse(candidate.safeBoundary);
			Assert::IsTrue(confirmed.safeBoundary);
			Assert::AreEqual(static_cast<uint8_t>(1), confirmed.eventFramesBack);
			Assert::AreEqual(static_cast<uint64_t>(3), confirmed.sourceSequence);
		}

		TEST_METHOD(ReportsFailedForAnInvalidP010Input)
		{
			SceneDetector detector;
			SceneDetectorInput input;
			input.enabled = true;
			const SceneDetectorResult result = detector.Analyze(input);

			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Failed), static_cast<int>(result.status));
		}

		TEST_METHOD(TightAndPaddedP010AdaptersProduceIdenticalResults)
		{
			SceneDetector tightDetector;
			SceneDetector paddedDetector;
			constexpr size_t width = 64;
			constexpr size_t height = 36;
			constexpr size_t paddedWidth = 72;
			const uint16_t levels[] = { 256, 768, 768, 768 };
			for (uint64_t frameIndex = 0; frameIndex < _countof(levels); ++frameIndex)
			{
				std::vector<uint16_t> tight(width * height,
					static_cast<uint16_t>(levels[frameIndex] << 6));
				std::vector<uint16_t> padded(paddedWidth * height, 0xffff);
				for (size_t row = 0; row < height; ++row)
					std::fill_n(padded.data() + row * paddedWidth, width,
						static_cast<uint16_t>(levels[frameIndex] << 6));

				const uint64_t sequence = frameIndex + 1;
				const SceneDetectorResult tightResult = tightDetector.Analyze(Input(tight, sequence));
				const SceneDetectorResult paddedResult = paddedDetector.Analyze({
					padded.data(), width, height, paddedWidth * sizeof(uint16_t),
					sequence, static_cast<int64_t>(sequence * 417083), 1, 417083, true });

				Assert::AreEqual(static_cast<int>(tightResult.status), static_cast<int>(paddedResult.status));
				Assert::AreEqual(tightResult.safeBoundary, paddedResult.safeBoundary);
				Assert::AreEqual(tightResult.eventId, paddedResult.eventId);
				Assert::AreEqual(static_cast<unsigned int>(tightResult.eventFramesBack),
					static_cast<unsigned int>(paddedResult.eventFramesBack));
				Assert::AreEqual(static_cast<unsigned int>(tightResult.averageLuma),
					static_cast<unsigned int>(paddedResult.averageLuma));
				Assert::AreEqual(tightResult.sourceSequence, paddedResult.sourceSequence);
				Assert::AreEqual(tightResult.generation, paddedResult.generation);
			}
		}

		TEST_METHOD(AOneFrameFlashDoesNotBecomeASceneBoundary)
		{
			SceneDetector detector;
			std::vector<uint16_t> base(64 * 36, static_cast<uint16_t>(256 << 6));
			std::vector<uint16_t> flash(64 * 36, static_cast<uint16_t>(768 << 6));
			Assert::IsFalse(detector.Analyze(Input(base, 1)).safeBoundary);
			Assert::IsFalse(detector.Analyze(Input(flash, 2)).safeBoundary);
			Assert::IsFalse(detector.Analyze(Input(base, 3)).safeBoundary);
			Assert::IsFalse(detector.Analyze(Input(base, 4)).safeBoundary);
		}

		TEST_METHOD(ResetsHistoryWhenTheRendererGenerationChanges)
		{
			SceneDetector detector;
			std::vector<uint16_t> bright(64 * 36, static_cast<uint16_t>(512 << 6));
			detector.Analyze(Input(bright, 1, 1));
			const SceneDetectorResult afterReset = detector.Analyze(Input(bright, 2, 2));

			Assert::AreEqual(static_cast<int>(SceneDetectorStatus::Warming), static_cast<int>(afterReset.status));
			Assert::IsFalse(afterReset.safeBoundary);
			Assert::AreEqual(static_cast<uint64_t>(2), afterReset.generation);
		}
	};
}
