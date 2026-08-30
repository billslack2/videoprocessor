#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboOutputPolicy.h>
#include <vprenderer/LibplaceboExternalHdrLutPolicy.h>
#include <ActiveOutputSweepPolicy.h>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboOutput;

namespace Tests
{
	TEST_CLASS(LibplaceboOutputPolicyTests)
	{
	public:
		TEST_METHOD(ExternalHdrLutDefaultsToExistingPixelShaderMode)
		{
			using namespace LibplaceboExternalHdrLut;
			Assert::AreEqual(static_cast<int>(ToneMappingMode::PIXEL_SHADERS),
				static_cast<int>(ParseToneMappingMode("")));
			Assert::AreEqual(static_cast<int>(ToneMappingMode::PIXEL_SHADERS),
				static_cast<int>(ParseToneMappingMode("invalid")));
			Assert::AreEqual(static_cast<int>(ToneMappingMode::EXTERNAL_3DLUT),
				static_cast<int>(ParseToneMappingMode("external_3dlut")));
			Assert::AreEqual(static_cast<int>(ToneMappingMode::EXTERNAL_3DLUT),
				static_cast<int>(ParseToneMappingMode("EXTERNAL_3DLUT")));
			Assert::AreEqual(static_cast<int>(ToneMappingMode::PIXEL_SHADERS),
				static_cast<int>(ParseToneMappingMode("passthrough")));
			Assert::AreEqual(static_cast<int>(
				LibplaceboExternalHdrLut::Primaries::P3_D65),
				static_cast<int>(ParsePrimaries("P3_D65")));
		}

		TEST_METHOD(DisabledHdrPassthroughFallsBackToInternalToneMapping)
		{
			using namespace LibplaceboExternalHdrLut;
			const auto shaders = Select(ToneMappingMode::PIXEL_SHADERS, true,
				LibplaceboExternalHdrLut::Primaries::BT2020, {});
			Assert::AreEqual(static_cast<int>(EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(shaders.effectiveMode));
			Assert::AreEqual(static_cast<int>(MetadataOwner::INTERNAL_PIPELINE),
				static_cast<int>(shaders.metadataOwner));
			Assert::AreEqual(static_cast<int>(FinalCalibrationStage::APPLY),
				static_cast<int>(shaders.finalCalibrationStage));

			const auto passthrough = Select(ToneMappingMode::PASS_THROUGH, true,
				LibplaceboExternalHdrLut::Primaries::BT2020, {});
			Assert::AreEqual(static_cast<int>(EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(passthrough.effectiveMode));
			Assert::AreEqual(static_cast<int>(MetadataOwner::INTERNAL_PIPELINE),
				static_cast<int>(passthrough.metadataOwner));
			Assert::AreEqual(static_cast<int>(FinalCalibrationStage::APPLY),
				static_cast<int>(passthrough.finalCalibrationStage));
			Assert::IsTrue(std::string(passthrough.reason).find("passthrough") !=
				std::string::npos);
		}

		TEST_METHOD(ExternalHdrLutUsesExactSlotBeforeFallback)
		{
			using namespace LibplaceboExternalHdrLut;
			const AvailableSlots all{ true, true, true };
			const auto p3 = Select(ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::P3_D65, all);
			Assert::IsTrue(p3.useExternalLut);
			Assert::AreEqual(static_cast<int>(FinalCalibrationStage::MASK),
				static_cast<int>(p3.finalCalibrationStage));
			Assert::AreEqual(static_cast<int>(Slot::P3_D65),
				static_cast<int>(p3.slot));
			Assert::IsFalse(p3.requiresExplicitPrimariesTransform);

			const auto bt2020 = Select(ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::BT2020, all);
			Assert::AreEqual(static_cast<int>(Slot::BT2020),
				static_cast<int>(bt2020.slot));
			Assert::IsFalse(bt2020.requiresExplicitPrimariesTransform);
		}

		TEST_METHOD(ExternalHdrLutFallbackOrderIsFrozenAndExplicit)
		{
			using namespace LibplaceboExternalHdrLut;
			const auto p3To2020 = Select(ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::P3_D65,
				{ true, false, true });
			Assert::AreEqual(static_cast<int>(Slot::BT2020),
				static_cast<int>(p3To2020.slot));
			Assert::IsTrue(p3To2020.requiresExplicitPrimariesTransform);

			const auto bt2020ToP3 = Select(ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::BT2020,
				{ true, true, false });
			Assert::AreEqual(static_cast<int>(Slot::P3_D65),
				static_cast<int>(bt2020ToP3.slot));
			Assert::IsTrue(bt2020ToP3.requiresExplicitPrimariesTransform);

			const auto bt709NoReverseFallback = Select(
				ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::BT709,
				{ false, true, true });
			Assert::IsFalse(bt709NoReverseFallback.useExternalLut);
		}

		TEST_METHOD(ExternalHdrLutRejectsNonPqAndUnknownInput)
		{
			using namespace LibplaceboExternalHdrLut;
			const auto nonPq = Select(ToneMappingMode::EXTERNAL_3DLUT, false,
				LibplaceboExternalHdrLut::Primaries::BT2020,
				{ false, false, true });
			Assert::IsFalse(nonPq.useExternalLut);
			Assert::AreEqual(static_cast<int>(EffectiveMode::PIXEL_SHADERS),
				static_cast<int>(nonPq.effectiveMode));
			Assert::AreEqual(static_cast<int>(MetadataOwner::INTERNAL_PIPELINE),
				static_cast<int>(nonPq.metadataOwner));
			Assert::AreEqual(static_cast<int>(FinalCalibrationStage::APPLY),
				static_cast<int>(nonPq.finalCalibrationStage));
			Assert::IsFalse(Select(ToneMappingMode::EXTERNAL_3DLUT, true,
				LibplaceboExternalHdrLut::Primaries::UNKNOWN,
				{ true, true, true }).useExternalLut);
		}

		TEST_METHOD(SdrGammaMissingOrOnPreservesCurrentManagedBehavior)
		{
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::ON),
				static_cast<int>(ParseSdrAdjustGamma("")));
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::AUTO),
				static_cast<int>(ParseSdrAdjustGamma("auto")));
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::OFF),
				static_cast<int>(ParseSdrAdjustGamma("off")));
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::OFF),
				static_cast<int>(ParseSdrAdjustGamma("no")));
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::ON),
				static_cast<int>(ParseSdrAdjustGamma("yes")));
			Assert::AreEqual(static_cast<int>(SdrAdjustGamma::ON),
				static_cast<int>(ParseSdrAdjustGamma("invalid")));
			const auto decision = ResolveSdrGamma(SdrAdjustGamma::ON, true, true,
				GammaRequest::AUTO, SdrTransfer::BT1886, SdrTransfer::SRGB);
			Assert::AreEqual(static_cast<int>(SdrGammaAction::ADJUST),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<int>(SdrTransfer::BT1886),
				static_cast<int>(decision.effectiveSource));
		}

		TEST_METHOD(SdrGammaOffMatchesAcceptedSdrTarget)
		{
			for (const SdrTransfer target : { SdrTransfer::SRGB,
				SdrTransfer::GAMMA22, SdrTransfer::GAMMA24 })
			{
				const auto decision = ResolveSdrGamma(SdrAdjustGamma::OFF, true, true,
					GammaRequest::GAMMA22, SdrTransfer::BT1886, target);
				Assert::AreEqual(static_cast<int>(SdrGammaAction::SUPPRESS),
					static_cast<int>(decision.action));
				Assert::AreEqual(static_cast<int>(target),
					static_cast<int>(decision.effectiveSource));
			}
			const auto unknown = ResolveSdrGamma(SdrAdjustGamma::OFF, true, true,
				GammaRequest::AUTO, SdrTransfer::BT1886, SdrTransfer::UNKNOWN);
			Assert::AreEqual(static_cast<int>(SdrGammaAction::ADJUST),
				static_cast<int>(unknown.action));
			Assert::AreEqual(static_cast<int>(SdrTransfer::BT1886),
				static_cast<int>(unknown.effectiveSource));
		}

		TEST_METHOD(SdrGammaAutoMatchesMpvCommonSdrToAutomaticSrgbPolicy)
		{
			for (const SdrTransfer source : { SdrTransfer::BT1886,
				SdrTransfer::GAMMA22, SdrTransfer::SRGB })
			{
				const auto decision = ResolveSdrGamma(SdrAdjustGamma::AUTO, true, true,
					GammaRequest::AUTO, source, SdrTransfer::SRGB);
				Assert::AreEqual(static_cast<int>(SdrGammaAction::SUPPRESS),
					static_cast<int>(decision.action));
			}
			Assert::AreEqual(static_cast<int>(SdrGammaAction::ADJUST),
				static_cast<int>(ResolveSdrGamma(SdrAdjustGamma::AUTO, true, true,
					GammaRequest::SRGB, SdrTransfer::BT1886,
					SdrTransfer::SRGB).action));
			Assert::AreEqual(static_cast<int>(SdrGammaAction::ADJUST),
				static_cast<int>(ResolveSdrGamma(SdrAdjustGamma::AUTO, true, true,
					GammaRequest::AUTO, SdrTransfer::GAMMA24,
					SdrTransfer::SRGB).action));
		}

		TEST_METHOD(SdrGammaPolicyDoesNotModifyHdrOrUnsafeOutput)
		{
			const auto hdr = ResolveSdrGamma(SdrAdjustGamma::OFF, false, true,
				GammaRequest::AUTO, SdrTransfer::OTHER, SdrTransfer::SRGB);
			Assert::AreEqual(static_cast<int>(SdrGammaAction::NOT_APPLICABLE),
				static_cast<int>(hdr.action));
			Assert::AreEqual(static_cast<int>(SdrTransfer::OTHER),
				static_cast<int>(hdr.effectiveSource));
			const auto blocked = ResolveSdrGamma(SdrAdjustGamma::OFF, true, false,
				GammaRequest::AUTO, SdrTransfer::BT1886, SdrTransfer::SRGB);
			Assert::AreEqual(static_cast<int>(SdrGammaAction::BLOCKED),
				static_cast<int>(blocked.action));
			Assert::AreEqual(static_cast<int>(SdrTransfer::BT1886),
				static_cast<int>(blocked.effectiveSource));
		}

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

		TEST_METHOD(CalibratedDirectSelectsTheProductionDirectPlan)
		{
			Assert::AreEqual(
				static_cast<int>(PresentationRequest::DIRECT),
				static_cast<int>(ParsePresentation("calibrated_direct")));
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

		TEST_METHOD(FullPureG22IsAStandardDisplayCalibrationTarget)
		{
			Request request;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.strictContract);
			Assert::IsFalse(plan.requiresDxgiOverride);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(plan.targetTransfer));

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(FullPureG22SeparatesDisplayPixelsFromWindowsRgbDeclaration)
		{
			Request request;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.strictContract);
			Assert::IsFalse(plan.requiresDxgiOverride);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(plan.desiredEncoding));
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(plan.targetTransfer));

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(static_cast<int>(DxgiEncoding::FULL_G22_P709),
				static_cast<int>(actual.encoding));
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(FullPureG22SupportsComposedWindowsPresentation)
		{
			Request request;
			request.presentation = PresentationRequest::COMPOSED;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsTrue(plan.useBlit);
			Evidence evidence;
			evidence.presentationModel = PresentationModel::BITBLT;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.safeToRender);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(actual.targetTransfer));
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

		TEST_METHOD(FullGamma24IsAStandardDisplayCalibrationTarget)
		{
			Request request;
			request.range = RangeRequest::FULL;
			request.gamma = GammaRequest::GAMMA24;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.requiresDxgiOverride);
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA24),
				static_cast<int>(plan.targetTransfer));

			Evidence evidence;
			evidence.presentationModel = PresentationModel::FLIP;
			const Actual actual = Finalize(plan, evidence);
			Assert::IsTrue(actual.requestedEncodingActive);
			Assert::AreEqual(
				static_cast<int>(TargetTransfer::GAMMA24),
				static_cast<int>(actual.targetTransfer));
		}

		TEST_METHOD(AutoRangeExplicitGamma22UsesTheCalibratedDisplayTarget)
		{
			Request request;
			request.gamma = GammaRequest::GAMMA22;
			const Plan plan = MakePlan(request);
			Assert::IsTrue(plan.valid);
			Assert::IsFalse(plan.requiresDxgiOverride);
			Assert::AreEqual(static_cast<int>(TargetTransfer::GAMMA22),
				static_cast<int>(plan.targetTransfer));
		}

		TEST_METHOD(FullRgbSupportsMadvrStyleCalibratedDisplayTransfers)
		{
			struct Case
			{
				const char* gamma;
				TargetTransfer target;
			};
			for (const Case& test : {
				Case{ "bt1886", TargetTransfer::BT1886 },
				Case{ "1.8", TargetTransfer::GAMMA18 },
				Case{ "2.0", TargetTransfer::GAMMA20 },
				Case{ "2.2", TargetTransfer::GAMMA22 },
				Case{ "2.4", TargetTransfer::GAMMA24 },
				Case{ "2.6", TargetTransfer::GAMMA26 },
				Case{ "2.8", TargetTransfer::GAMMA28 } })
			{
				Request request;
				request.range = RangeRequest::FULL;
				request.gamma = ParseGamma(test.gamma);
				const Plan plan = MakePlan(request);
				Assert::IsTrue(plan.valid);
				Assert::IsFalse(plan.requiresDxgiOverride);
				Assert::AreEqual(static_cast<int>(test.target),
					static_cast<int>(plan.targetTransfer));
			}
		}

		TEST_METHOD(LimitedRgbRejectsDisplayTransfersWithoutMatchingDxgiDeclarations)
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
