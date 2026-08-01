#include "pch.h"
#include "CppUnitTest.h"

#include <GlyphSegmentationProviderContract.h>

#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(GlyphSegmentationProviderContractTests)
	{
	public:
		static GlyphSegmentationRequest ValidRequest(uint64_t token = 1)
		{
			static std::vector<uint16_t> luma(320 * 180, 64 << 6);
			GlyphSegmentationRequest request;
			request.requestToken = token;
			request.sourceSequence = 41;
			request.generation = { 3, 5, 7 };
			request.rasterWidth = 320;
			request.rasterHeight = 180;
			request.source.kind = GlyphSegmentationP010SourceKind::CpuPlanes;
			request.source.luma = luma.data();
			request.source.lumaStrideBytes = 320 * sizeof(uint16_t);
			request.activePicture = { 0.0f, 0.13f, 1.0f, 0.87f };
			request.roiCount = 1;
			request.rois[0].bounds = { 0.10f, 0.87f, 0.90f, 1.0f };
			request.rois[0].context = GlyphSegmentationRoiContext::BottomBar;
			request.rois[0].backingP50 = 64;
			request.rois[0].backingP72 = 65;
			request.rois[0].trustedBar = true;
			return request;
		}

		TEST_METHOD(RequestRequiresP010SourceTrustedRoiAndNormalizedGeometry)
		{
			GlyphSegmentationRequest request = ValidRequest();
			Assert::IsTrue(request.IsValid());
			request.rois[0].trustedBar = false;
			Assert::IsFalse(request.IsValid());
			request = ValidRequest();
			request.activePicture.bottom = 1.1f;
			Assert::IsFalse(request.IsValid());
			request = ValidRequest();
			request.source.luma = nullptr;
			Assert::IsFalse(request.IsValid());
		}

		TEST_METHOD(ResultBindsToExactlyOneFrameGenerationAndContainsSoftMasks)
		{
			const GlyphSegmentationRequest request = ValidRequest(13);
			auto interior = std::make_shared<const std::vector<uint8_t>>(12, 255);
			auto edge = std::make_shared<const std::vector<uint8_t>>(12, 96);
			GlyphSegmentationResult result;
			result.requestToken = request.requestToken;
			result.sourceSequence = request.sourceSequence;
			result.generation = request.generation;
			result.rasterWidth = request.rasterWidth;
			result.rasterHeight = request.rasterHeight;
			result.status = GlyphSegmentationResultStatus::Completed;
			result.memberCount = 1;
			result.members[0].glyphBounds = { 0.25f, 0.88f, 0.60f, 0.94f };
			result.members[0].captureBounds = { 0.20f, 0.83f, 0.65f, 0.99f };
			result.members[0].mask = {
				{ 0.25f, 0.88f, 0.60f, 0.94f }, 4, 3, interior, edge };
			result.members[0].confidence = 0.93f;
			Assert::IsTrue(result.Matches(request));
			Assert::IsTrue(result.IsUsable());

			GlyphSegmentationRequest wrongGeneration = request;
			++wrongGeneration.generation.activePicture;
			Assert::IsFalse(result.Matches(wrongGeneration));
			result.members[0].mask.edge =
				std::make_shared<const std::vector<uint8_t>>(11, 96);
			Assert::IsFalse(result.IsUsable());
		}

		TEST_METHOD(LatestOnlyGateRejectsStaleCompletionAndUnavailableProviderIsInert)
		{
			GlyphSegmentationLatestOnlyGate gate;
			const uint64_t first = gate.IssueToken();
			const uint64_t latest = gate.IssueToken();
			GlyphSegmentationResult result;
			result.requestToken = first;
			Assert::IsFalse(gate.Accepts(result));
			result.requestToken = latest;
			Assert::IsTrue(gate.Accepts(result));
			gate.Invalidate();
			Assert::IsFalse(gate.Accepts(result));

			UnavailableGlyphSegmentationProvider provider;
			Assert::IsFalse(provider.IsAvailable());
			Assert::IsFalse(provider.Submit(ValidRequest(latest)));
			Assert::IsFalse(provider.TryTakeLatest(result));
			Assert::AreEqual(static_cast<int>(GlyphSegmentationResultStatus::Unavailable),
				static_cast<int>(result.status));
		}
	};
}
