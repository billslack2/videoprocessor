#include "pch.h"
#include "CppUnitTest.h"

#include <libplacebo/LibplaceboOutputPolicy.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboOutput;

namespace Tests
{
	TEST_CLASS(LibplaceboOutputPolicyTests)
	{
	public:
		TEST_METHOD(AutoBaselinePreservesComposedFullSrgb)
		{
			const Plan plan = MakePlan({});
			Assert::IsTrue(plan.useBlit);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.requiresDxgiOverride);

			Evidence evidence;
			evidence.presentationModel = PresentationModel::BITBLT;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::SWAPCHAIN),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(DirectRequestsFlipButReportsActualModel)
		{
			Request request;
			request.presentation = PresentationRequest::DIRECT;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.useBlit);

			Evidence evidence;
			evidence.presentationModel = PresentationModel::BITBLT;
			const Actual actual = Finalize(plan, evidence);
			Assert::AreEqual(
				static_cast<int>(PresentationModel::BITBLT),
				static_cast<int>(actual.presentationModel));
		}

		TEST_METHOD(LimitedG22IsRejectedWithoutExactRendererTransfer)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.valid);
			Assert::IsFalse(Finalize(plan, {}).requestedEncodingActive);
		}

		TEST_METHOD(LimitedAutoUsesExactStudioG24AndFlipCandidate)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.useBlit);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::STUDIO_G24_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::GAMMA24),
				static_cast<int>(plan.targetTransfer));
		}

		TEST_METHOD(LimitedG24UsesOnlyStudioG24)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			request.gamma = GammaRequest::GAMMA24;
			const Plan plan = MakePlan(request);

			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = true;
			const Actual actual = Finalize(plan, evidence);

			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::STUDIO_G24_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::GAMMA24),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(FullBt2020RequiresVerifiedP2020Encoding)
		{
			Request request;
			request.primaries = PrimariesRequest::BT2020;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsTrue(plan.requiresDxgiOverride);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P2020),
				static_cast<int>(plan.desiredEncoding));

			const Actual fallback = Finalize(plan, {});
			Assert::IsFalse(fallback.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(fallback.encoding));

			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = true;
			const Actual accepted = Finalize(plan, evidence);
			Assert::IsTrue(accepted.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P2020),
				static_cast<int>(accepted.encoding));
		}

		TEST_METHOD(LimitedBt2020UsesVerifiedStudioG24P2020)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			request.primaries = PrimariesRequest::BT2020;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.requiresDxgiOverride);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::STUDIO_G24_P2020),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::GAMMA24),
				static_cast<int>(plan.targetTransfer));
		}

		TEST_METHOD(SetSuccessWithoutPreAdvertisedSupportFallsBackFull)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			const Plan plan = MakePlan(request);

			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = false;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = true;
			const Actual actual = Finalize(plan, evidence);

			Assert::IsFalse(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::SWAPCHAIN),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(AdvertisedSupportWithSetFailureFallsBackFull)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			const Plan plan = MakePlan(request);

			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = false;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsFalse(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
		}

		TEST_METHOD(PostCheckFailureWithSuccessfulRestoreFallsBackSafely)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = false;
			evidence.fullRestoreRequired = true;
			evidence.fullRestorePresentSupportedBeforeSet = true;
			evidence.fullRestoreSetSucceeded = true;
			evidence.fullRestorePresentSupportedAfterSet = true;
			const Actual actual = Finalize(MakePlan(request), evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsFalse(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
		}

		TEST_METHOD(FailedFullRestoreBlocksRendering)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = false;
			evidence.fullRestoreRequired = true;
			const Actual actual = Finalize(MakePlan(request), evidence);
			Assert::IsFalse(actual.safeToRender);
			Assert::IsFalse(actual.requestedEncodingActive);
		}

		TEST_METHOD(FullSetSuccessWithoutPresentSupportBlocksRendering)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = false;
			evidence.fullRestoreRequired = true;
			evidence.fullRestorePresentSupportedBeforeSet = true;
			evidence.fullRestoreSetSucceeded = true;
			evidence.fullRestorePresentSupportedAfterSet = false;
			const Actual actual = Finalize(MakePlan(request), evidence);
			Assert::IsFalse(actual.safeToRender);
			Assert::IsFalse(actual.requestedEncodingActive);
		}

		TEST_METHOD(PreviousStudioWithoutSwapchain3BlocksRendering)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			Evidence evidence;
			evidence.fullRestoreRequired = true;
			const Actual actual = Finalize(MakePlan(request), evidence);
			Assert::IsFalse(actual.safeToRender);
		}

		TEST_METHOD(VerifiedRestoreAllowsReturnToFullContract)
		{
			Request request;
			request.range = RangeRequest::FULL;
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			evidence.fullRestoreRequired = true;
			evidence.fullRestorePresentSupportedBeforeSet = true;
			evidence.fullRestoreSetSucceeded = true;
			evidence.fullRestorePresentSupportedAfterSet = true;
			const Actual actual = Finalize(MakePlan(request), evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
		}

		TEST_METHOD(VerifiedTransitionEnforcesCheckSetCheckOrder)
		{
			std::string calls;
			int checkCount = 0;
			const VerifiedTransition transition = ExecuteVerifiedTransition(
				[&]()
				{
					calls += "C";
					return ++checkCount <= 2;
				},
				[&]()
				{
					calls += "S";
					return true;
				});
			Assert::AreEqual("CSC", calls.c_str());
			Assert::IsTrue(transition.presentSupportedBeforeSet);
			Assert::IsTrue(transition.setSucceeded);
			Assert::IsTrue(transition.presentSupportedAfterSet);
		}

		TEST_METHOD(VerifiedTransitionDoesNotSetWithoutPreSupport)
		{
			std::string calls;
			const VerifiedTransition transition = ExecuteVerifiedTransition(
				[&]()
				{
					calls += "C";
					return false;
				},
				[&]()
				{
					calls += "S";
					return true;
				});
			Assert::AreEqual("C", calls.c_str());
			Assert::IsFalse(transition.setSucceeded);
		}

		TEST_METHOD(VerifiedTransitionDoesNotPostCheckAfterSetFailure)
		{
			std::string calls;
			const VerifiedTransition transition = ExecuteVerifiedTransition(
				[&]()
				{
					calls += "C";
					return true;
				},
				[&]()
				{
					calls += "S";
					return false;
				});
			Assert::AreEqual("CS", calls.c_str());
			Assert::IsTrue(transition.presentSupportedBeforeSet);
			Assert::IsFalse(transition.setSucceeded);
			Assert::IsFalse(transition.presentSupportedAfterSet);
		}

		TEST_METHOD(AutoLimitedFailureRequiresComposedFallback)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			const Plan plan = MakePlan(request);
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(ShouldFallbackToComposed(plan, actual));
		}

		TEST_METHOD(DirectLimitedFailureStaysOnRequestedPresentationPath)
		{
			Request request;
			request.presentation = PresentationRequest::DIRECT;
			request.range = RangeRequest::LIMITED;
			const Plan plan = MakePlan(request);
			Evidence evidence;
			evidence.hasSwapchain3 = true;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsFalse(ShouldFallbackToComposed(plan, actual));
		}

		TEST_METHOD(MissingSwapchain3FallsBackFull)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			const Actual actual = Finalize(MakePlan(request), {});
			Assert::IsFalse(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
		}

		TEST_METHOD(FullGamma24IsRejectedWithoutTargetMutation)
		{
			Request request;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA24;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.valid);

			const Actual actual = Finalize(plan, {});
			Assert::IsFalse(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::SWAPCHAIN),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(AutoRangeExplicitGamma22IsRejected)
		{
			Request request;
			request.gamma = GammaRequest::GAMMA22;
			Assert::IsFalse(MakePlan(request).valid);
		}

		TEST_METHOD(UndeclarableOutputGammasAreRejected)
		{
			for (const char* gamma : { "bt1886", "1.8", "2.0", "2.6", "2.8" })
			{
				Request request;
				request.range = RangeRequest::LIMITED;
				request.gamma = ParseGamma(gamma);
				Assert::IsFalse(MakePlan(request).valid);
			}
		}

		TEST_METHOD(PackedR10DiagnosticsExtractChannelsAndStudioExcursions)
		{
			const uint32_t pixels[] = {
				0u | (64u << 10) | (940u << 20),
				1023u | (500u << 10) | (63u << 20)
			};
			const PackedR10Stats stats = AnalyzePackedR10(pixels, 2, 2, 1);
			Assert::AreEqual(2ull, stats.sampledPixels);
			Assert::AreEqual(0, static_cast<int>(stats.minimum[0]));
			Assert::AreEqual(64, static_cast<int>(stats.minimum[1]));
			Assert::AreEqual(63, static_cast<int>(stats.minimum[2]));
			Assert::AreEqual(1023, static_cast<int>(stats.maximum[0]));
			Assert::AreEqual(500, static_cast<int>(stats.maximum[1]));
			Assert::AreEqual(940, static_cast<int>(stats.maximum[2]));
			Assert::AreEqual(2ull, stats.channelsBelowStudioBlack);
			Assert::AreEqual(1ull, stats.channelsAboveStudioWhite);
		}

		TEST_METHOD(PackedR10DiagnosticsRespectsRowPitchAndSampleStep)
		{
			const uint32_t pixels[] = {
				100u, 200u, 999u,
				300u, 400u, 999u
			};
			const PackedR10Stats stats = AnalyzePackedR10(pixels, 3, 2, 2, 2);
			Assert::AreEqual(1ull, stats.sampledPixels);
			Assert::AreEqual(100, static_cast<int>(stats.minimum[0]));
			Assert::AreEqual(100ull, stats.sum[0]);
		}
	};
}
