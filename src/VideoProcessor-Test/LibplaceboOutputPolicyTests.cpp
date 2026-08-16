#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboOutputPolicy.h>
#include <ActiveOutputSweepPolicy.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboOutput;

namespace Tests
{
	TEST_CLASS(LibplaceboOutputPolicyTests)
	{
	public:
		TEST_METHOD(ActiveSweepExactContractRequiresMetadataAndPresent)
		{
			using namespace ActiveOutputSweepPolicy;
			using namespace RendererOutputContract;
			Expected expected;
			expected.presentation = Presentation::FLIP;
			expected.range = Range::FULL;
			expected.transfer = Transfer::GAMMA22;
			expected.primaries = Primaries::REC709;
			expected.requireVpOwner = true;
			expected.requireDxgiVerification = true;
			expected.swapchainBitDepth = 10;
			Status actual;
			actual.available = true;
			actual.safeToRender = true;
			actual.requestedContractActive = true;
			actual.vpOwnsPresentation = true;
			actual.dxgiAppliedVerified = true;
			actual.swapchainBitDepth = 10;
			actual.presentation = Presentation::FLIP;
			actual.range = Range::FULL;
			actual.transfer = Transfer::GAMMA22;
			actual.primaries = Primaries::REC709;
			Assert::AreEqual(static_cast<int>(Verdict::WAITING),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.successfulPresents = 1;
			actual.displayDelivery = DisplayDeliveryEvidence::PRESENTED;
			Assert::AreEqual(static_cast<int>(Verdict::PASS),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.swapchainBitDepth = 8;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.swapchainBitDepth = 10;
			actual.primaries = Primaries::BT2020;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
		}

		TEST_METHOD(ActiveSweepPhysicalCurveIsMeasurementNotAutomaticPass)
		{
			using namespace ActiveOutputSweepPolicy;
			using namespace RendererOutputContract;
			Expected expected;
			expected.presentation = Presentation::FLIP;
			expected.range = Range::FULL;
			expected.transfer = Transfer::GAMMA22;
			expected.requireVpOwner = true;
			expected.measurementRequired = true;
			Status actual;
			actual.available = true;
			actual.safeToRender = true;
			actual.requestedContractActive = true;
			actual.vpOwnsPresentation = true;
			actual.successfulPresents = 4;
			actual.displayDelivery = DisplayDeliveryEvidence::PRESENTED;
			actual.presentation = Presentation::FLIP;
			actual.range = Range::FULL;
			actual.transfer = Transfer::GAMMA22;
			Assert::AreEqual(static_cast<int>(Verdict::MEASURE),
				static_cast<int>(Evaluate(expected, actual).verdict));
		}

		TEST_METHOD(ActiveSweepExpectedFallbackMustActuallyFallback)
		{
			using namespace ActiveOutputSweepPolicy;
			using namespace RendererOutputContract;
			Expected expected;
			expected.disposition = Disposition::FALLBACK;
			expected.presentation = Presentation::FLIP;
			expected.range = Range::FULL;
			expected.transfer = Transfer::SRGB;
			Status actual;
			actual.available = true;
			actual.safeToRender = true;
			actual.successfulPresents = 1;
			actual.displayDelivery = DisplayDeliveryEvidence::PRESENTED;
			actual.presentation = Presentation::FLIP;
			actual.range = Range::FULL;
			actual.transfer = Transfer::SRGB;
			Assert::AreEqual(static_cast<int>(Verdict::EXPECTED),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.range = Range::LIMITED;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.range = Range::FULL;
			actual.requestedContractActive = true;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
		}

		TEST_METHOD(ActiveSweepUnexpectedFallbackAndBlockedStateFail)
		{
			using namespace ActiveOutputSweepPolicy;
			using namespace RendererOutputContract;
			Expected expected;
			Status actual;
			actual.available = true;
			actual.safeToRender = true;
			actual.successfulPresents = 1;
			actual.displayDelivery = DisplayDeliveryEvidence::PRESENTED;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
			actual.safeToRender = false;
			Assert::AreEqual(static_cast<int>(Verdict::FAIL),
				static_cast<int>(Evaluate(expected, actual).verdict));
			expected.disposition = Disposition::BLOCKED;
			Assert::AreEqual(static_cast<int>(Verdict::EXPECTED),
				static_cast<int>(Evaluate(expected, actual).verdict));
		}

		TEST_METHOD(ActiveSweepComposedSubmissionRequiresVisualDeliveryGrade)
		{
			using namespace ActiveOutputSweepPolicy;
			using namespace RendererOutputContract;
			Expected expected;
			expected.presentation = Presentation::BITBLT;
			expected.range = Range::FULL;
			expected.transfer = Transfer::SRGB;
			Status actual;
			actual.available = true;
			actual.safeToRender = true;
			actual.requestedContractActive = true;
			actual.successfulPresents = 10;
			actual.presentation = Presentation::BITBLT;
			actual.range = Range::FULL;
			actual.transfer = Transfer::SRGB;
			actual.displayDelivery = DisplayDeliveryEvidence::SUBMITTED;
			actual.rendererContent = RendererContentEvidence::NONBLACK;
			const Decision decision = Evaluate(expected, actual);
			Assert::AreEqual(static_cast<int>(Verdict::MEASURE),
				static_cast<int>(decision.verdict));
			Assert::IsTrue(decision.reason.find("display delivery is unverified") !=
				std::string::npos);
		}

		TEST_METHOD(OneShotInfoFrameSetIsAuthoritativeWhenReadbackDoesNotEcho)
		{
			Assert::AreEqual(
				static_cast<int>(OneShotSignalAcceptance::SET_ACCEPTED),
				static_cast<int>(ClassifyOneShotSignal(true, true, false)));
			Assert::AreEqual(
				static_cast<int>(OneShotSignalAcceptance::SET_ACCEPTED),
				static_cast<int>(ClassifyOneShotSignal(true, false, false)));
		}

		TEST_METHOD(OneShotInfoFrameRequiresSuccessfulSet)
		{
			Assert::AreEqual(
				static_cast<int>(OneShotSignalAcceptance::FAILED),
				static_cast<int>(ClassifyOneShotSignal(false, true, true)));
			Assert::AreEqual(
				static_cast<int>(OneShotSignalAcceptance::READBACK_VERIFIED),
				static_cast<int>(ClassifyOneShotSignal(true, true, true)));
		}

		TEST_METHOD(AutoBaselineUsesFlipForPresentationTiming)
		{
			const Plan plan = MakePlan({});
			Assert::IsFalse(plan.useBlit);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.requiresDxgiOverride);

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::SWAPCHAIN),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(Bt2020TargetRetainsProvenP709Transport)
		{
			Request transport;
			// The player hosts Alpha in a child HWND. That can move presentation
			// from flip/direct to composed/bitblt after F6 has been selected.
			// The color target must not be demoted with that transport fallback.
			transport.presentation = PresentationRequest::DIRECT;
			transport.primaries = PrimariesRequest::BT2020;
			const SdrOutputContract contract = MakeSdrOutputContract(
				transport, SdrTargetPrimaries::BT2020, true);
			Assert::AreEqual(static_cast<int>(SdrTargetPrimaries::BT2020),
				static_cast<int>(contract.target));
			Assert::AreEqual(static_cast<int>(PrimariesRequest::REC709),
				static_cast<int>(contract.transport.primaries));
			Assert::IsTrue(contract.reportBt2020ToDisplay);
			const Plan plan = MakePlan(contract.transport);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::IsFalse(plan.requiresDxgiOverride);

			Evidence embeddedPreview;
			embeddedPreview.presentationModel = PresentationModel::BITBLT;
			const Actual actual = Finalize(plan, embeddedPreview);
			Assert::IsTrue(actual.safeToRender);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			// `contract.target` remains BT.2020 above: target and transport are
			// deliberately independent across this fallback.
		}

		TEST_METHOD(Rec709TargetCannotRequestBt2020AviSignaling)
		{
			const SdrOutputContract contract = MakeSdrOutputContract(
				{}, SdrTargetPrimaries::REC709, true);
			Assert::AreEqual(static_cast<int>(SdrTargetPrimaries::REC709),
				static_cast<int>(contract.target));
			Assert::AreEqual(static_cast<int>(PrimariesRequest::REC709),
				static_cast<int>(contract.transport.primaries));
			Assert::IsFalse(contract.reportBt2020ToDisplay);
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

		TEST_METHOD(LimitedG22IsDisabledUnlessTheExperimentIsExplicitlyEnabled)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.valid);
			Assert::IsFalse(Finalize(plan, {}).requestedEncodingActive);
		}

		TEST_METHOD(LimitedG22ExperimentUsesStudioG22AndPureGamma22)
		{
			Request request;
			request.range = RangeRequest::LIMITED;
			request.gamma = GammaRequest::GAMMA22;
			request.allowLimitedG22Experiment = true;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsTrue(plan.requiresDxgiOverride);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::STUDIO_G22_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(plan.targetTransfer));

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = true;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(DxgiEncoding::STUDIO_G22_P709),
				static_cast<int>(actual.encoding));
		}

		TEST_METHOD(FullPureG22IsDisabledUnlessExplicitlyEnabled)
		{
			Request request;
			request.presentation = PresentationRequest::DIRECT;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			request.vpOwnedPresenter = true;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.valid);
			Assert::IsFalse(plan.strictContract);
		}

		TEST_METHOD(FullPureG22ExperimentSeparatesPixelsFromDxgiDeclaration)
		{
			Request request;
			request.presentation = PresentationRequest::DIRECT;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			request.allowFullG22Experiment = true;
			request.vpOwnedPresenter = true;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsTrue(plan.strictContract);
			Assert::IsTrue(plan.requiresDxgiOverride);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(plan.targetTransfer));

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			evidence.vpOwnsPresentation = true;
			evidence.hasSwapchain3 = true;
			evidence.presentSupportedBeforeSet = true;
			evidence.setSucceeded = true;
			evidence.presentSupportedAfterSet = true;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(FullPureG22RequiresVpOwnedDirectAndFailsClosed)
		{
			Request request;
			request.presentation = PresentationRequest::COMPOSED;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			request.allowFullG22Experiment = true;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.valid);
			Assert::IsTrue(plan.strictContract);
			Assert::IsFalse(Finalize(plan, {}).safeToRender);

			request.presentation = PresentationRequest::DIRECT;
			request.vpOwnedPresenter = true;
			const Plan direct = MakePlan(request);
			Assert::IsTrue(direct.valid);
			Assert::IsFalse(Finalize(direct, {}).safeToRender);

			Evidence nonOwner;
			nonOwner.presentationModel = PresentationModel::FLIP;
			nonOwner.hasSwapchain3 = true;
			nonOwner.presentSupportedBeforeSet = true;
			nonOwner.setSucceeded = true;
			nonOwner.presentSupportedAfterSet = true;
			Assert::IsFalse(Finalize(direct, nonOwner).safeToRender);
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
			evidence.presentationModel = PresentationModel::FLIP;
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

		TEST_METHOD(Bt2020ForcesFlipAndRejectsBitBlt)
		{
			Request request;
			request.presentation = PresentationRequest::COMPOSED;
			request.primaries = PrimariesRequest::BT2020;
			const Plan plan = MakePlan(request);
			Assert::IsFalse(plan.useBlit);

			Evidence bitblt;
			bitblt.presentationModel = PresentationModel::BITBLT;
			bitblt.hasSwapchain3 = true;
			bitblt.presentSupportedBeforeSet = true;
			bitblt.setSucceeded = true;
			bitblt.presentSupportedAfterSet = true;
			const Actual actual = Finalize(plan, bitblt);
			Assert::IsFalse(actual.safeToRender);
			Assert::IsFalse(actual.requestedEncodingActive);
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
			Assert::AreEqual(1ull, stats.nearBlackBuckets[0]);
			Assert::AreEqual(1ull, stats.nearBlackBuckets[4]);
			Assert::AreEqual(1ull, stats.nearBlackBuckets[5]);
			Assert::AreEqual(3ull, stats.nearBlackBuckets[7]);
		}

		TEST_METHOD(R10ToPng16MappingPreservesEveryCode)
		{
			for (uint16_t value = 0; value <= 1023; ++value)
				Assert::AreEqual(static_cast<int>(value),
					static_cast<int>(ExpandR10ToR16(value) >> 6));
			Assert::AreEqual(0, static_cast<int>(ExpandR10ToR16(0)));
			Assert::AreEqual(65535, static_cast<int>(ExpandR10ToR16(1023)));
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
