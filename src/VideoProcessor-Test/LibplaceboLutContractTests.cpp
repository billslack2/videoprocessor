#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/LibplaceboLutContract.h>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <libplacebo/colorspace.h>
#pragma warning(pop)

#include <limits>
#include <string>


using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace LibplaceboLutContract;

namespace
{
	const char* const EDID_HASH =
		"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const char* const CUBE_HASH =
		"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
	const char* const ATTESTATION_HASH =
		"1111111111111111111111111111111111111111111111111111111111111111";
	const char* const ANALYZER_HASH =
		"2222222222222222222222222222222222222222222222222222222222222222";

	CarrierIdentity MakeCarrierIdentity()
	{
		CarrierIdentity identity;
		identity.schemaVersion = CARRIER_IDENTITY_SCHEMA_VERSION;
		identity.edidSha256 = EDID_HASH;
		identity.displayConfigAdapter = { true, 1, 2 };
		identity.displayConfigTargetKnown = true;
		identity.displayConfigTargetId = 3;
		identity.outputTechnology = OutputTechnology::HDMI;
		identity.activeWidth = 3840;
		identity.activeHeight = 2160;
		identity.refreshNumerator = 24000;
		identity.refreshDenominator = 1001;
		identity.scanlineOrderingKnown = true;
		identity.scanlineOrdering = 1;
		identity.scalingKnown = true;
		identity.scaling = 2;
		identity.rendererAdapter = { true, 1, 2 };
		identity.driverVersionKnown = true;
		identity.driverVersion = 0x0001000200030004ULL;
		return identity;
	}

	ResolvedContract MakeContract(
		InputPrimaries primaries = InputPrimaries::REC709,
		InputTransfer transfer = InputTransfer::BT1886,
		DisplayModeAuthority authority = DisplayModeAuthority::MANUAL_ATTESTED)
	{
		ResolvedContract contract;
		contract.role = Role::TARGET_DISPLAY_CALIBRATION;
		contract.scope = Scope::VIDEO_PICTURE;
		contract.attachmentStage =
			AttachmentStage::TARGET_NATIVE_POST_ENCODE_PRE_DITHER;
		contract.nativeOutputSemantics =
			NativeOutputSemantics::OPAQUE_DEVICE_DRIVE;
		contract.origin = ContractOrigin::SELECTED_RENDERER_PROFILE;
		contract.inputPrimaries = primaries;
		contract.inputTransfer = transfer;
		contract.inputRange = InputRange::FULL;
		contract.authoringCodeDepth = 10;
		contract.referenceWhite = { 100.0, ReferenceOrigin::EXPLICIT_PROFILE };
		contract.referenceBlack = { 0.005, ReferenceOrigin::EXPLICIT_PROFILE };
		contract.cube.canonicalPath = "C:\\calibration\\display.cube";
		contract.cube.contentSha256 = CUBE_HASH;
		contract.cube.cubeSize = 65;
		contract.cube.reloadGeneration = 5;
		contract.installation.directDeliveryAuthority =
			DirectDeliveryAuthority::EXTERNAL_ATTESTED;
		contract.installation.externalColorManagement =
			ExternalColorManagement::NONE_ATTESTED;
		contract.installation.displayMode =
			primaries == InputPrimaries::BT2020
				? "Projector SDR BT.2020"
				: "Projector SDR Rec.709";
		contract.installation.displayModeAuthority = authority;
		contract.installation.attestationRecordSha256 = ATTESTATION_HASH;
		if (authority == DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED)
			contract.installation.externalAnalyzerRecordSha256 = ANALYZER_HASH;
		contract.expectedCarrierIdentity = MakeCarrierIdentity();
		contract.expectedCarrierGeneration = 17;
		contract.calibrationContractGeneration = 3;
		return contract;
	}

	ActivationContext MakeContext(const ResolvedContract& contract)
	{
		ActivationContext context;
		context.resource.canonicalPath = contract.cube.canonicalPath;
		context.resource.contentSha256 = contract.cube.contentSha256;
		context.resource.cubeSize = contract.cube.cubeSize;
		context.resource.reloadGeneration = contract.cube.reloadGeneration;
		context.currentCalibrationContractGeneration =
			contract.calibrationContractGeneration;

		auto& evidence = context.carrier;
		evidence.safeToRender = true;
		evidence.declaredDxgiColorSpaceActive = true;
		evidence.presentationModel = PresentationModel::FLIP;
		evidence.vpOwnsPresentation = true;
		evidence.topLevelPresenter = true;
		evidence.dxgiColorSpaceTag = DxgiCarrierTag::FULL_G22_P709;
		evidence.surfaceFormat = SurfaceFormat::R10G10B10A2_UNORM;
		evidence.sampleBitDepth = 10;
		evidence.colorBitDepth = 10;
		evidence.bitShift = 0;
		evidence.displayBitDepth = 10;
		evidence.finalTargetSystem = TargetSystem::RGB;
		evidence.finalTargetPrimaries = contract.inputPrimaries;
		evidence.acceptedOutputTransfer = contract.inputTransfer;
		evidence.finalTargetTransfer = contract.inputTransfer;
		evidence.finalTargetRange = contract.inputRange;
		evidence.finalTargetWhiteNits = contract.referenceWhite.nits;
		evidence.finalTargetBlackNits = contract.referenceBlack.nits;
		evidence.advancedColor = AdvancedColorState::DISABLED;
		evidence.targetIcc = TargetIccState::ABSENT;
		evidence.targetBt2020 =
			contract.inputPrimaries == InputPrimaries::BT2020;
		evidence.nvidiaBt2020SetAndReadbackVerified =
			contract.installation.displayModeAuthority ==
				DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED;
		evidence.identity = contract.expectedCarrierIdentity;
		evidence.carrierGeneration = contract.expectedCarrierGeneration;
		return context;
	}

	void AssertRejection(Rejection expected, Rejection actual)
	{
		Assert::AreEqual(static_cast<int>(expected), static_cast<int>(actual));
		Assert::IsFalse(std::string(ShortReason(actual)).empty());
	}
}

namespace Tests
{
	TEST_CLASS(LibplaceboLutContractTests)
	{
	public:
		TEST_METHOD(Rec709AndSdrBt2020ProfileMatrixIsExplicit)
		{
			for (const InputTransfer transfer : {
				InputTransfer::SRGB, InputTransfer::GAMMA22,
				InputTransfer::GAMMA24, InputTransfer::BT1886 })
			{
				const auto contract = MakeContract(InputPrimaries::REC709, transfer);
				Assert::IsTrue(IsAccepted(
					ValidateActivation(contract, MakeContext(contract))));
			}

			for (const InputTransfer transfer : {
				InputTransfer::GAMMA22, InputTransfer::BT1886 })
			{
				const auto contract = MakeContract(InputPrimaries::BT2020, transfer);
				Assert::IsTrue(IsAccepted(
					ValidateActivation(contract, MakeContext(contract))));
			}

			for (const InputTransfer transfer : {
				InputTransfer::SRGB, InputTransfer::GAMMA24 })
			{
				const auto contract = MakeContract(InputPrimaries::BT2020, transfer);
				AssertRejection(Rejection::PROFILE_COMBINATION_UNSUPPORTED,
					ValidateResolvedContract(contract));
			}
		}

		TEST_METHOD(StageAndNativeOutputSemanticsArePartOfTheContract)
		{
			auto contract = MakeContract();
			contract.attachmentStage = AttachmentStage::UNKNOWN;
			AssertRejection(Rejection::ATTACHMENT_STAGE_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.attachmentStage = AttachmentStage::OTHER;
			AssertRejection(Rejection::ATTACHMENT_STAGE_UNSUPPORTED,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.nativeOutputSemantics = NativeOutputSemantics::OTHER;
			AssertRejection(Rejection::NATIVE_OUTPUT_UNSUPPORTED,
				ValidateResolvedContract(contract));
		}

		TEST_METHOD(AutoOrUnknownFieldsNeverActAsWildcards)
		{
			auto contract = MakeContract();
			contract.inputPrimaries = InputPrimaries::UNKNOWN;
			AssertRejection(Rejection::PRIMARIES_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.inputTransfer = InputTransfer::UNKNOWN;
			AssertRejection(Rejection::TRANSFER_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.inputRange = InputRange::UNKNOWN;
			AssertRejection(Rejection::RANGE_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceWhite.origin = ReferenceOrigin::UNKNOWN;
			AssertRejection(Rejection::REFERENCE_WHITE_ORIGIN_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceBlack.origin = ReferenceOrigin::UNKNOWN;
			AssertRejection(Rejection::REFERENCE_BLACK_ORIGIN_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract(
				InputPrimaries::REC709, InputTransfer::SRGB);
			contract.referenceWhite.origin =
				static_cast<ReferenceOrigin>(999);
			AssertRejection(Rejection::REFERENCE_WHITE_ORIGIN_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract(
				InputPrimaries::REC709, InputTransfer::SRGB);
			contract.referenceBlack.origin =
				static_cast<ReferenceOrigin>(999);
			AssertRejection(Rejection::REFERENCE_BLACK_ORIGIN_UNKNOWN,
				ValidateResolvedContract(contract));
		}

		TEST_METHOD(V1RejectsP3LimitedEightBitAndHdrInputProfiles)
		{
			auto contract = MakeContract();
			contract.inputPrimaries = InputPrimaries::P3_D65;
			AssertRejection(Rejection::PRIMARIES_UNSUPPORTED,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.inputRange = InputRange::LIMITED;
			AssertRejection(Rejection::FULL_RANGE_REQUIRED,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.authoringCodeDepth = 8;
			AssertRejection(Rejection::R10_AUTHORING_REQUIRED,
				ValidateResolvedContract(contract));

			for (const InputTransfer transfer : {
				InputTransfer::PQ, InputTransfer::HLG, InputTransfer::OTHER })
			{
				contract = MakeContract();
				contract.inputTransfer = transfer;
				AssertRejection(Rejection::TRANSFER_UNSUPPORTED,
					ValidateResolvedContract(contract));
			}
		}

		TEST_METHOD(PowerAndBt1886ProfilesRequireExplicitAuthoringLuminance)
		{
			for (const InputTransfer transfer : {
				InputTransfer::GAMMA22, InputTransfer::GAMMA24,
				InputTransfer::BT1886 })
			{
				auto contract = MakeContract(InputPrimaries::REC709, transfer);
				contract.referenceWhite.origin = ReferenceOrigin::INHERITED_TARGET;
				AssertRejection(Rejection::REFERENCE_WHITE_NOT_EXPLICIT,
					ValidateResolvedContract(contract));

				contract = MakeContract(InputPrimaries::REC709, transfer);
				contract.referenceBlack.origin = ReferenceOrigin::INHERITED_TARGET;
				AssertRejection(Rejection::REFERENCE_BLACK_NOT_EXPLICIT,
					ValidateResolvedContract(contract));
			}

			auto srgb = MakeContract(InputPrimaries::REC709, InputTransfer::SRGB);
			srgb.referenceWhite.origin = ReferenceOrigin::INHERITED_TARGET;
			srgb.referenceBlack.origin = ReferenceOrigin::INHERITED_TARGET;
			Assert::IsTrue(IsAccepted(ValidateResolvedContract(srgb)));
		}

		TEST_METHOD(ReferenceLuminanceIsFiniteBoundedOrderedAndCanonical)
		{
			auto contract = MakeContract();
			contract.referenceWhite.nits =
				(std::numeric_limits<double>::quiet_NaN)();
			AssertRejection(Rejection::REFERENCE_WHITE_NOT_FINITE,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceWhite.nits = 39.999;
			AssertRejection(Rejection::REFERENCE_WHITE_OUT_OF_RANGE,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceBlack.nits =
				(std::numeric_limits<double>::infinity)();
			AssertRejection(Rejection::REFERENCE_BLACK_NOT_FINITE,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceBlack.nits = -0.001;
			AssertRejection(Rejection::REFERENCE_BLACK_NEGATIVE,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceBlack.nits = 1e-7;
			AssertRejection(
				Rejection::REFERENCE_BLACK_BELOW_LIBPLACEBO_FLOOR,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.referenceBlack.nits = contract.referenceWhite.nits;
			AssertRejection(Rejection::REFERENCE_BLACK_NOT_BELOW_WHITE,
				ValidateResolvedContract(contract));
		}

		TEST_METHOD(SemanticZeroBlackUsesTheLibplaceboKnownBlackSentinel)
		{
			float adapted = 0.0f;
			const ReferenceLuminance semanticBlack = {
				0.0, ReferenceOrigin::EXPLICIT_PROFILE };
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::NONE),
				static_cast<int>(ResolveLibplaceboBlackNits(
					semanticBlack, adapted)));
			Assert::AreEqual(PL_COLOR_HDR_BLACK, adapted, 0.0f);

			const ReferenceLuminance atFloor = {
				static_cast<double>(PL_COLOR_HDR_BLACK),
				ReferenceOrigin::EXPLICIT_PROFILE };
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::NONE),
				static_cast<int>(ResolveLibplaceboBlackNits(atFloor, adapted)));
			Assert::AreEqual(PL_COLOR_HDR_BLACK, adapted, 0.0f);
		}

		TEST_METHOD(SemanticBlackAdapterRejectsUnknownAndInvalidValues)
		{
			float adapted = 1.0f;
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::UNKNOWN),
				static_cast<int>(ResolveLibplaceboBlackNits({}, adapted)));
			Assert::AreEqual(0.0f, adapted, 0.0f);

			ReferenceLuminance value = {
				(std::numeric_limits<double>::quiet_NaN)(),
				ReferenceOrigin::EXPLICIT_PROFILE };
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::NOT_FINITE),
				static_cast<int>(ResolveLibplaceboBlackNits(value, adapted)));

			value.nits = -1.0;
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::NEGATIVE),
				static_cast<int>(ResolveLibplaceboBlackNits(value, adapted)));

			value.nits = (std::numeric_limits<double>::denorm_min)();
			Assert::AreEqual(
				static_cast<int>(SemanticBlackRejection::NOT_REPRESENTABLE),
				static_cast<int>(ResolveLibplaceboBlackNits(value, adapted)));

			value.nits = 1e-7;
			Assert::AreEqual(
				static_cast<int>(
					SemanticBlackRejection::BELOW_LIBPLACEBO_FLOOR),
				static_cast<int>(ResolveLibplaceboBlackNits(value, adapted)));
		}

		TEST_METHOD(CubeInstallationAndTypedRouteMustBeFullyBound)
		{
			auto contract = MakeContract();
			contract.cube.contentSha256 = "not-a-hash";
			AssertRejection(Rejection::CUBE_CONTENT_IDENTITY_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.cube.reloadGeneration = 0;
			AssertRejection(Rejection::CUBE_RELOAD_GENERATION_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.installation.displayMode = "   ";
			AssertRejection(Rejection::DISPLAY_MODE_MISSING,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.installation.attestationRecordSha256.clear();
			AssertRejection(Rejection::ATTESTATION_RECORD_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract();
			contract.expectedCarrierIdentity.edidSha256.clear();
			AssertRejection(Rejection::EXPECTED_CARRIER_IDENTITY_INCOMPLETE,
				ValidateResolvedContract(contract));
		}

		TEST_METHOD(ActiveCubeAndContractGenerationsAreAtomicBindings)
		{
			const auto contract = MakeContract();
			auto context = MakeContext(contract);
			context.resource.contentSha256 = EDID_HASH;
			AssertRejection(Rejection::ACTIVE_RESOURCE_CONTENT_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			++context.resource.reloadGeneration;
			AssertRejection(Rejection::ACTIVE_RESOURCE_GENERATION_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			++context.currentCalibrationContractGeneration;
			AssertRejection(Rejection::CONTRACT_GENERATION_MISMATCH,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(CarrierRequiresEveryAttestedTopLevelVpOwnedR10FlipFact)
		{
			const auto contract = MakeContract();
			auto context = MakeContext(contract);
			context.carrier.safeToRender = false;
			AssertRejection(Rejection::CARRIER_UNSAFE,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.declaredDxgiColorSpaceActive = false;
			AssertRejection(Rejection::DXGI_COLOR_SPACE_NOT_ACTIVE,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.presentationModel = PresentationModel::BITBLT;
			AssertRejection(Rejection::PRESENTATION_NOT_FLIP,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.vpOwnsPresentation = false;
			AssertRejection(Rejection::PRESENTATION_NOT_VP_OWNED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.topLevelPresenter = false;
			AssertRejection(Rejection::PRESENTATION_NOT_TOP_LEVEL,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.dxgiColorSpaceTag = DxgiCarrierTag::OTHER;
			AssertRejection(Rejection::DXGI_CARRIER_TAG_UNSUPPORTED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.surfaceFormat = SurfaceFormat::OTHER;
			AssertRejection(Rejection::SURFACE_FORMAT_UNSUPPORTED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.sampleBitDepth = 8;
			AssertRejection(Rejection::SAMPLE_DEPTH_UNSUPPORTED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.colorBitDepth = 8;
			AssertRejection(Rejection::COLOR_DEPTH_UNSUPPORTED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.bitShift = 1;
			AssertRejection(Rejection::BIT_SHIFT_UNSUPPORTED,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.displayBitDepth = 8;
			AssertRejection(Rejection::DISPLAY_DEPTH_UNSUPPORTED,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(InertP709CarrierIsIndependentFromBt2020TargetAndNativeOutput)
		{
			const auto contract = MakeContract(
				InputPrimaries::BT2020, InputTransfer::BT1886);
			const auto context = MakeContext(contract);
			Assert::AreEqual(
				static_cast<int>(DxgiCarrierTag::FULL_G22_P709),
				static_cast<int>(context.carrier.dxgiColorSpaceTag));
			Assert::AreEqual(
				static_cast<int>(InputPrimaries::BT2020),
				static_cast<int>(context.carrier.finalTargetPrimaries));
			Assert::AreEqual(
				static_cast<int>(NativeOutputSemantics::OPAQUE_DEVICE_DRIVE),
				static_cast<int>(contract.nativeOutputSemantics));
			Assert::IsTrue(IsAccepted(ValidateActivation(contract, context)));
		}

		TEST_METHOD(CarrierMustMatchFinalRgbTransferRangeAndSemanticLuminance)
		{
			const auto contract = MakeContract();
			auto context = MakeContext(contract);
			context.carrier.finalTargetSystem = TargetSystem::OTHER;
			AssertRejection(Rejection::TARGET_SYSTEM_NOT_RGB,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.finalTargetPrimaries = InputPrimaries::BT2020;
			AssertRejection(Rejection::TARGET_PRIMARIES_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.acceptedOutputTransfer = InputTransfer::GAMMA22;
			AssertRejection(Rejection::ACCEPTED_OUTPUT_TRANSFER_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.finalTargetTransfer = InputTransfer::GAMMA22;
			AssertRejection(Rejection::TARGET_TRANSFER_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.finalTargetRange = InputRange::LIMITED;
			AssertRejection(Rejection::TARGET_RANGE_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.finalTargetWhiteNits = 203.0;
			AssertRejection(Rejection::TARGET_WHITE_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.finalTargetBlackNits = 0.0;
			AssertRejection(Rejection::TARGET_BLACK_MISMATCH,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(CarrierRequiresDetectedColorManagementState)
		{
			const auto contract = MakeContract();
			auto context = MakeContext(contract);
			context.carrier.advancedColor = AdvancedColorState::UNKNOWN;
			AssertRejection(Rejection::ADVANCED_COLOR_NOT_CONFIRMED_OFF,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.targetIcc = TargetIccState::ATTACHED;
			AssertRejection(Rejection::TARGET_ICC_NOT_CONFIRMED_ABSENT,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(DisplayModeAuthorityIsBoundToTheResolvedProfile)
		{
			auto rec709 = MakeContract();
			rec709.installation.displayModeAuthority =
				DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED;
			rec709.installation.externalAnalyzerRecordSha256 = ANALYZER_HASH;
			AssertRejection(Rejection::DISPLAY_MODE_AUTHORITY_UNSUPPORTED,
				ValidateResolvedContract(rec709));

			const auto manualBt2020 = MakeContract(
				InputPrimaries::BT2020, InputTransfer::GAMMA22,
				DisplayModeAuthority::MANUAL_ATTESTED);
			Assert::IsTrue(IsAccepted(ValidateActivation(
				manualBt2020, MakeContext(manualBt2020))));

			const auto nvidiaBt2020 = MakeContract(
				InputPrimaries::BT2020, InputTransfer::GAMMA22,
				DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED);
			Assert::IsTrue(IsAccepted(ValidateActivation(
				nvidiaBt2020, MakeContext(nvidiaBt2020))));
		}

		TEST_METHOD(NvidiaBt2020AuthorityNeedsStoredAnalyzerAndLiveReadbackProof)
		{
			auto contract = MakeContract(
				InputPrimaries::BT2020, InputTransfer::BT1886,
				DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED);
			contract.installation.externalAnalyzerRecordSha256.clear();
			AssertRejection(Rejection::EXTERNAL_ANALYZER_RECORD_UNKNOWN,
				ValidateResolvedContract(contract));

			contract = MakeContract(
				InputPrimaries::BT2020, InputTransfer::BT1886,
				DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED);
			auto context = MakeContext(contract);
			context.carrier.nvidiaBt2020SetAndReadbackVerified = false;
			AssertRejection(Rejection::BT2020_SIGNAL_EVIDENCE_INCOMPLETE,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			context.carrier.targetBt2020 = false;
			AssertRejection(Rejection::BT2020_TARGET_NOT_ACTIVE,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(TypedCarrierIdentityAndGenerationRejectRouteChanges)
		{
			const auto contract = MakeContract();
			auto context = MakeContext(contract);
			context.carrier.identity.driverVersionKnown = false;
			AssertRejection(Rejection::CARRIER_IDENTITY_INCOMPLETE,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			++context.carrier.identity.displayConfigTargetId;
			AssertRejection(Rejection::CARRIER_IDENTITY_MISMATCH,
				ValidateActivation(contract, context));

			context = MakeContext(contract);
			++context.carrier.carrierGeneration;
			AssertRejection(Rejection::CARRIER_GENERATION_MISMATCH,
				ValidateActivation(contract, context));
		}

		TEST_METHOD(EveryRejectionHasAnOsdSafeReason)
		{
			for (int value = static_cast<int>(Rejection::ROLE_UNKNOWN);
				value <= static_cast<int>(Rejection::CARRIER_GENERATION_MISMATCH);
				++value)
			{
				const std::string reason =
					ShortReason(static_cast<Rejection>(value));
				Assert::IsFalse(reason.empty());
				Assert::IsTrue(reason.size() <= 32);
			}

			for (int value = static_cast<int>(SemanticBlackRejection::UNKNOWN);
				value <= static_cast<int>(
					SemanticBlackRejection::BELOW_LIBPLACEBO_FLOOR);
				++value)
			{
				Assert::IsFalse(std::string(ShortReason(
					static_cast<SemanticBlackRejection>(value))).empty());
			}
		}
	};
}
