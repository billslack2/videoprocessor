#include "LibplaceboLutContract.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#pragma warning(push)
#pragma warning(disable: 4244)
#include <libplacebo/colorspace.h>
#pragma warning(pop)

namespace LibplaceboLutContract
{
	namespace
	{
		bool IsRepresentableAsPositiveFloat(double value)
		{
			if (value <= 0.0 ||
				value > static_cast<double>((std::numeric_limits<float>::max)()))
			{
				return false;
			}
			const float converted = static_cast<float>(value);
			return std::isfinite(converted) && converted > 0.0f;
		}

		bool HasNonWhitespace(const std::string& value)
		{
			return std::any_of(value.begin(), value.end(), [](unsigned char c)
			{
				return std::isspace(c) == 0;
			});
		}

		bool IsSha256(const std::string& value)
		{
			return value.size() == 64 &&
				std::all_of(value.begin(), value.end(), [](unsigned char c)
				{
					return std::isxdigit(c) != 0;
				});
		}

		bool RequiresExplicitLuminance(InputTransfer transfer)
		{
			return transfer == InputTransfer::GAMMA22 ||
				transfer == InputTransfer::GAMMA24 ||
				transfer == InputTransfer::BT1886;
		}

		bool SameAdapter(
			const AdapterLuidIdentity& first,
			const AdapterLuidIdentity& second)
		{
			return first.known == second.known &&
				first.highPart == second.highPart &&
				first.lowPart == second.lowPart;
		}

		bool IsComplete(const CarrierIdentity& identity)
		{
			return identity.schemaVersion == CARRIER_IDENTITY_SCHEMA_VERSION &&
				IsSha256(identity.edidSha256) &&
				IsSha256(identity.monitorDevicePathSha256) &&
				identity.displayConfigAdapter.known &&
				identity.displayConfigTargetKnown &&
				identity.connectorInstanceKnown &&
				(identity.outputTechnology == OutputTechnology::HDMI ||
				 identity.outputTechnology == OutputTechnology::DISPLAYPORT ||
				 identity.outputTechnology == OutputTechnology::DVI ||
				 identity.outputTechnology == OutputTechnology::INTERNAL) &&
				identity.activeWidth > 0 &&
				identity.activeHeight > 0 &&
				identity.refreshNumerator > 0 &&
				identity.refreshDenominator > 0 &&
				identity.scanlineOrderingKnown &&
				identity.scalingKnown &&
				identity.rendererAdapter.known &&
				identity.driverVersionKnown &&
				identity.driverVersion > 0;
		}

		bool SameCarrierIdentity(
			const CarrierIdentity& expected,
			const CarrierIdentity& actual)
		{
			return expected.schemaVersion == actual.schemaVersion &&
				expected.edidSha256 == actual.edidSha256 &&
				expected.monitorDevicePathSha256 ==
					actual.monitorDevicePathSha256 &&
				SameAdapter(expected.displayConfigAdapter,
					actual.displayConfigAdapter) &&
				expected.displayConfigTargetKnown ==
					actual.displayConfigTargetKnown &&
				expected.displayConfigTargetId ==
					actual.displayConfigTargetId &&
				expected.connectorInstanceKnown ==
					actual.connectorInstanceKnown &&
				expected.connectorInstance == actual.connectorInstance &&
				expected.outputTechnology == actual.outputTechnology &&
				expected.activeWidth == actual.activeWidth &&
				expected.activeHeight == actual.activeHeight &&
				expected.refreshNumerator == actual.refreshNumerator &&
				expected.refreshDenominator == actual.refreshDenominator &&
				expected.scanlineOrderingKnown ==
					actual.scanlineOrderingKnown &&
				expected.scanlineOrdering == actual.scanlineOrdering &&
				expected.scalingKnown == actual.scalingKnown &&
				expected.scaling == actual.scaling &&
				SameAdapter(expected.rendererAdapter, actual.rendererAdapter) &&
				expected.driverVersionKnown == actual.driverVersionKnown &&
				expected.driverVersion == actual.driverVersion;
		}

		Rejection ValidateCarrier(
			const ResolvedContract& contract,
			const CarrierEvidence& evidence)
		{
			if (!evidence.safeToRender)
				return Rejection::CARRIER_UNSAFE;
			if (!evidence.declaredDxgiColorSpaceActive)
				return Rejection::DXGI_COLOR_SPACE_NOT_ACTIVE;
			if (evidence.presentationModel != PresentationModel::FLIP)
				return Rejection::PRESENTATION_NOT_FLIP;
			if (!evidence.vpOwnsPresentation)
				return Rejection::PRESENTATION_NOT_VP_OWNED;
			if (!evidence.topLevelPresenter)
				return Rejection::PRESENTATION_NOT_TOP_LEVEL;
			if (evidence.dxgiColorSpaceTag != DxgiCarrierTag::FULL_G22_P709)
				return Rejection::DXGI_CARRIER_TAG_UNSUPPORTED;
			if (evidence.surfaceFormat != SurfaceFormat::R10G10B10A2_UNORM)
				return Rejection::SURFACE_FORMAT_UNSUPPORTED;
			if (evidence.sampleBitDepth != 10)
				return Rejection::SAMPLE_DEPTH_UNSUPPORTED;
			if (evidence.colorBitDepth != 10)
				return Rejection::COLOR_DEPTH_UNSUPPORTED;
			if (evidence.bitShift != 0)
				return Rejection::BIT_SHIFT_UNSUPPORTED;
			if (evidence.displayBitDepth != 10)
				return Rejection::DISPLAY_DEPTH_UNSUPPORTED;
			if (evidence.finalTargetSystem != TargetSystem::RGB)
				return Rejection::TARGET_SYSTEM_NOT_RGB;
			if (evidence.finalTargetPrimaries != contract.inputPrimaries)
				return Rejection::TARGET_PRIMARIES_MISMATCH;
			if (evidence.acceptedOutputTransfer != contract.inputTransfer)
				return Rejection::ACCEPTED_OUTPUT_TRANSFER_MISMATCH;
			if (evidence.finalTargetTransfer != contract.inputTransfer)
				return Rejection::TARGET_TRANSFER_MISMATCH;
			if (evidence.finalTargetRange != contract.inputRange)
				return Rejection::TARGET_RANGE_MISMATCH;
			if (evidence.finalTargetWhiteNits != contract.referenceWhite.nits)
				return Rejection::TARGET_WHITE_MISMATCH;
			if (evidence.finalTargetBlackNits != contract.referenceBlack.nits)
				return Rejection::TARGET_BLACK_MISMATCH;
			if (evidence.advancedColor != AdvancedColorState::DISABLED)
				return Rejection::ADVANCED_COLOR_NOT_CONFIRMED_OFF;
			if (evidence.targetIcc != TargetIccState::ABSENT)
				return Rejection::TARGET_ICC_NOT_CONFIRMED_ABSENT;

			if (contract.inputPrimaries == InputPrimaries::REC709)
			{
				if (evidence.targetBt2020)
					return Rejection::TARGET_PRIMARIES_MISMATCH;
			}
			else if (contract.inputPrimaries == InputPrimaries::BT2020)
			{
				if (!evidence.targetBt2020)
					return Rejection::BT2020_TARGET_NOT_ACTIVE;
				if (contract.installation.displayModeAuthority ==
					DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED &&
					!evidence.nvidiaBt2020SetAndReadbackVerified)
				{
					return Rejection::BT2020_SIGNAL_EVIDENCE_INCOMPLETE;
				}
			}

			if (!IsComplete(evidence.identity))
				return Rejection::CARRIER_IDENTITY_INCOMPLETE;
			if (!SameCarrierIdentity(
				contract.expectedCarrierIdentity, evidence.identity))
			{
				return Rejection::CARRIER_IDENTITY_MISMATCH;
			}
			if (evidence.carrierGeneration == 0)
				return Rejection::CARRIER_GENERATION_UNKNOWN;
			if (evidence.carrierGeneration != contract.expectedCarrierGeneration)
				return Rejection::CARRIER_GENERATION_MISMATCH;
			return Rejection::NONE;
		}
	}

	bool IsCompleteCarrierIdentity(const CarrierIdentity& identity)
	{
		return IsComplete(identity);
	}

	Rejection ValidateResolvedContract(const ResolvedContract& contract)
	{
		if (contract.role == Role::UNKNOWN)
			return Rejection::ROLE_UNKNOWN;
		if (contract.role != Role::TARGET_DISPLAY_CALIBRATION)
			return Rejection::ROLE_UNSUPPORTED;
		if (contract.scope == Scope::UNKNOWN)
			return Rejection::SCOPE_UNKNOWN;
		if (contract.scope != Scope::VIDEO_PICTURE)
			return Rejection::SCOPE_UNSUPPORTED;
		if (contract.attachmentStage == AttachmentStage::UNKNOWN)
			return Rejection::ATTACHMENT_STAGE_UNKNOWN;
		if (contract.attachmentStage !=
			AttachmentStage::TARGET_NATIVE_POST_ENCODE_PRE_DITHER)
		{
			return Rejection::ATTACHMENT_STAGE_UNSUPPORTED;
		}
		if (contract.nativeOutputSemantics == NativeOutputSemantics::UNKNOWN)
			return Rejection::NATIVE_OUTPUT_UNKNOWN;
		if (contract.nativeOutputSemantics !=
			NativeOutputSemantics::OPAQUE_DEVICE_DRIVE)
		{
			return Rejection::NATIVE_OUTPUT_UNSUPPORTED;
		}
		if (contract.origin != ContractOrigin::SELECTED_RENDERER_PROFILE)
			return Rejection::CONTRACT_ORIGIN_UNKNOWN;

		if (contract.inputPrimaries == InputPrimaries::UNKNOWN)
			return Rejection::PRIMARIES_UNKNOWN;
		if (contract.inputPrimaries != InputPrimaries::REC709 &&
			contract.inputPrimaries != InputPrimaries::BT2020)
		{
			return Rejection::PRIMARIES_UNSUPPORTED;
		}
		if (contract.inputTransfer == InputTransfer::UNKNOWN)
			return Rejection::TRANSFER_UNKNOWN;
		if (contract.inputTransfer == InputTransfer::PQ ||
			contract.inputTransfer == InputTransfer::HLG ||
			contract.inputTransfer == InputTransfer::OTHER)
		{
			return Rejection::TRANSFER_UNSUPPORTED;
		}

		const bool rec709Transfer =
			contract.inputTransfer == InputTransfer::SRGB ||
			contract.inputTransfer == InputTransfer::GAMMA22 ||
			contract.inputTransfer == InputTransfer::GAMMA24 ||
			contract.inputTransfer == InputTransfer::BT1886;
		const bool bt2020Transfer =
			contract.inputTransfer == InputTransfer::GAMMA22 ||
			contract.inputTransfer == InputTransfer::BT1886;
		if ((contract.inputPrimaries == InputPrimaries::REC709 &&
			 !rec709Transfer) ||
			(contract.inputPrimaries == InputPrimaries::BT2020 &&
			 !bt2020Transfer))
		{
			return Rejection::PROFILE_COMBINATION_UNSUPPORTED;
		}

		if (contract.inputRange == InputRange::UNKNOWN)
			return Rejection::RANGE_UNKNOWN;
		if (contract.inputRange != InputRange::FULL)
			return Rejection::FULL_RANGE_REQUIRED;
		if (contract.authoringCodeDepth == 0)
			return Rejection::AUTHORING_DEPTH_UNKNOWN;
		if (contract.authoringCodeDepth != 10)
			return Rejection::R10_AUTHORING_REQUIRED;

		if (contract.referenceWhite.origin == ReferenceOrigin::UNKNOWN)
			return Rejection::REFERENCE_WHITE_ORIGIN_UNKNOWN;
		if (contract.referenceWhite.origin == ReferenceOrigin::DERIVED_DEFAULT)
			return Rejection::REFERENCE_WHITE_DERIVED;
		if (contract.referenceWhite.origin != ReferenceOrigin::EXPLICIT_PROFILE &&
			contract.referenceWhite.origin != ReferenceOrigin::INHERITED_TARGET)
		{
			return Rejection::REFERENCE_WHITE_ORIGIN_UNKNOWN;
		}
		if (RequiresExplicitLuminance(contract.inputTransfer) &&
			contract.referenceWhite.origin != ReferenceOrigin::EXPLICIT_PROFILE)
		{
			return Rejection::REFERENCE_WHITE_NOT_EXPLICIT;
		}
		if (!std::isfinite(contract.referenceWhite.nits))
			return Rejection::REFERENCE_WHITE_NOT_FINITE;
		if (contract.referenceWhite.nits < 40.0 ||
			contract.referenceWhite.nits > 500.0)
		{
			return Rejection::REFERENCE_WHITE_OUT_OF_RANGE;
		}
		if (!IsRepresentableAsPositiveFloat(contract.referenceWhite.nits))
			return Rejection::REFERENCE_WHITE_NOT_REPRESENTABLE;

		if (contract.referenceBlack.origin == ReferenceOrigin::UNKNOWN)
			return Rejection::REFERENCE_BLACK_ORIGIN_UNKNOWN;
		if (contract.referenceBlack.origin == ReferenceOrigin::DERIVED_DEFAULT)
			return Rejection::REFERENCE_BLACK_DERIVED;
		if (contract.referenceBlack.origin != ReferenceOrigin::EXPLICIT_PROFILE &&
			contract.referenceBlack.origin != ReferenceOrigin::INHERITED_TARGET)
		{
			return Rejection::REFERENCE_BLACK_ORIGIN_UNKNOWN;
		}
		if (RequiresExplicitLuminance(contract.inputTransfer) &&
			contract.referenceBlack.origin != ReferenceOrigin::EXPLICIT_PROFILE)
		{
			return Rejection::REFERENCE_BLACK_NOT_EXPLICIT;
		}
		if (!std::isfinite(contract.referenceBlack.nits))
			return Rejection::REFERENCE_BLACK_NOT_FINITE;
		if (contract.referenceBlack.nits < 0.0)
			return Rejection::REFERENCE_BLACK_NEGATIVE;
		if (contract.referenceBlack.nits >= contract.referenceWhite.nits)
			return Rejection::REFERENCE_BLACK_NOT_BELOW_WHITE;

		float adaptedBlack = 0.0f;
		const SemanticBlackRejection blackRejection =
			ResolveLibplaceboBlackNits(contract.referenceBlack, adaptedBlack);
		switch (blackRejection)
		{
		case SemanticBlackRejection::NOT_REPRESENTABLE:
			return Rejection::REFERENCE_BLACK_NOT_REPRESENTABLE;
		case SemanticBlackRejection::BELOW_LIBPLACEBO_FLOOR:
			return Rejection::REFERENCE_BLACK_BELOW_LIBPLACEBO_FLOOR;
		default:
			break;
		}
		if (blackRejection != SemanticBlackRejection::NONE)
			return Rejection::REFERENCE_BLACK_NOT_REPRESENTABLE;
		const float adaptedWhite =
			static_cast<float>(contract.referenceWhite.nits);
		if (!std::isfinite(adaptedWhite) || !(adaptedBlack < adaptedWhite))
			return Rejection::REFERENCE_FLOAT_PAIR_INVALID;

		if (!HasNonWhitespace(contract.cube.canonicalPath))
			return Rejection::CUBE_PATH_UNKNOWN;
		if (!IsSha256(contract.cube.contentSha256))
			return Rejection::CUBE_CONTENT_IDENTITY_UNKNOWN;
		if (contract.cube.cubeSize < 2 ||
			contract.cube.cubeSize > MAX_CUBE_SIZE)
		{
			return Rejection::CUBE_SIZE_UNSUPPORTED;
		}
		if (contract.cube.reloadGeneration == 0)
			return Rejection::CUBE_RELOAD_GENERATION_UNKNOWN;

		if (contract.installation.directDeliveryAuthority !=
			DirectDeliveryAuthority::EXTERNAL_ATTESTED)
		{
			return Rejection::DIRECT_DELIVERY_NOT_ATTESTED;
		}
		if (contract.installation.externalColorManagement !=
			ExternalColorManagement::NONE_ATTESTED)
		{
			return Rejection::EXTERNAL_COLOR_MANAGEMENT_NOT_ATTESTED_OFF;
		}
		if (!HasNonWhitespace(contract.installation.displayMode))
			return Rejection::DISPLAY_MODE_MISSING;
		if (contract.inputPrimaries == InputPrimaries::REC709)
		{
			if (contract.installation.displayModeAuthority !=
				DisplayModeAuthority::MANUAL_ATTESTED)
			{
				return Rejection::DISPLAY_MODE_AUTHORITY_UNSUPPORTED;
			}
		}
		else if (contract.installation.displayModeAuthority !=
			DisplayModeAuthority::MANUAL_ATTESTED &&
			contract.installation.displayModeAuthority !=
			DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED)
		{
			return Rejection::DISPLAY_MODE_AUTHORITY_UNSUPPORTED;
		}
		if (!IsSha256(contract.installation.attestationRecordSha256))
			return Rejection::ATTESTATION_RECORD_UNKNOWN;
		if (contract.installation.displayModeAuthority ==
			DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED &&
			!IsSha256(contract.installation.externalAnalyzerRecordSha256))
		{
			return Rejection::EXTERNAL_ANALYZER_RECORD_UNKNOWN;
		}

		if (!IsComplete(contract.expectedCarrierIdentity))
			return Rejection::EXPECTED_CARRIER_IDENTITY_INCOMPLETE;
		if (contract.expectedCarrierGeneration == 0)
			return Rejection::EXPECTED_CARRIER_GENERATION_UNKNOWN;
		if (contract.calibrationContractGeneration == 0)
			return Rejection::CONTRACT_GENERATION_UNKNOWN;
		return Rejection::NONE;
	}

	Rejection ValidateActivation(
		const ResolvedContract& contract,
		const ActivationContext& context)
	{
		const Rejection contractRejection = ValidateResolvedContract(contract);
		if (contractRejection != Rejection::NONE)
			return contractRejection;

		if (context.resource.canonicalPath != contract.cube.canonicalPath)
			return Rejection::ACTIVE_RESOURCE_PATH_MISMATCH;
		if (context.resource.contentSha256 != contract.cube.contentSha256)
			return Rejection::ACTIVE_RESOURCE_CONTENT_MISMATCH;
		if (context.resource.cubeSize != contract.cube.cubeSize)
			return Rejection::ACTIVE_RESOURCE_SIZE_MISMATCH;
		if (context.resource.reloadGeneration != contract.cube.reloadGeneration)
			return Rejection::ACTIVE_RESOURCE_GENERATION_MISMATCH;
		if (context.currentCalibrationContractGeneration == 0)
			return Rejection::CURRENT_CONTRACT_GENERATION_UNKNOWN;
		if (context.currentCalibrationContractGeneration !=
			contract.calibrationContractGeneration)
		{
			return Rejection::CONTRACT_GENERATION_MISMATCH;
		}
		return ValidateCarrier(contract, context.carrier);
	}

	bool IsAccepted(Rejection rejection)
	{
		return rejection == Rejection::NONE;
	}

	const char* ShortReason(Rejection rejection)
	{
		switch (rejection)
		{
		case Rejection::ROLE_UNKNOWN: return "role unresolved";
		case Rejection::ROLE_UNSUPPORTED: return "unsupported LUT role";
		case Rejection::SCOPE_UNKNOWN: return "scope unresolved";
		case Rejection::SCOPE_UNSUPPORTED: return "unsupported LUT scope";
		case Rejection::ATTACHMENT_STAGE_UNKNOWN: return "stage unresolved";
		case Rejection::ATTACHMENT_STAGE_UNSUPPORTED: return "wrong LUT stage";
		case Rejection::NATIVE_OUTPUT_UNKNOWN: return "native output unresolved";
		case Rejection::NATIVE_OUTPUT_UNSUPPORTED: return "wrong native output";
		case Rejection::CONTRACT_ORIGIN_UNKNOWN: return "origin unresolved";
		case Rejection::PRIMARIES_UNKNOWN: return "primaries unresolved";
		case Rejection::PRIMARIES_UNSUPPORTED: return "primaries unsupported";
		case Rejection::TRANSFER_UNKNOWN: return "transfer unresolved";
		case Rejection::TRANSFER_UNSUPPORTED: return "transfer unsupported";
		case Rejection::PROFILE_COMBINATION_UNSUPPORTED: return "profile unsupported";
		case Rejection::RANGE_UNKNOWN: return "range unresolved";
		case Rejection::FULL_RANGE_REQUIRED: return "full range required";
		case Rejection::AUTHORING_DEPTH_UNKNOWN: return "code depth unresolved";
		case Rejection::R10_AUTHORING_REQUIRED: return "R10 authoring required";
		case Rejection::REFERENCE_WHITE_ORIGIN_UNKNOWN: return "white origin unresolved";
		case Rejection::REFERENCE_WHITE_DERIVED: return "derived white rejected";
		case Rejection::REFERENCE_WHITE_NOT_EXPLICIT: return "explicit white required";
		case Rejection::REFERENCE_WHITE_NOT_FINITE: return "white is not finite";
		case Rejection::REFERENCE_WHITE_OUT_OF_RANGE: return "white outside 40-500";
		case Rejection::REFERENCE_WHITE_NOT_REPRESENTABLE: return "white not representable";
		case Rejection::REFERENCE_BLACK_ORIGIN_UNKNOWN: return "black origin unresolved";
		case Rejection::REFERENCE_BLACK_DERIVED: return "derived black rejected";
		case Rejection::REFERENCE_BLACK_NOT_EXPLICIT: return "explicit black required";
		case Rejection::REFERENCE_BLACK_NOT_FINITE: return "black is not finite";
		case Rejection::REFERENCE_BLACK_NEGATIVE: return "black is negative";
		case Rejection::REFERENCE_BLACK_NOT_REPRESENTABLE: return "black not representable";
		case Rejection::REFERENCE_BLACK_BELOW_LIBPLACEBO_FLOOR: return "black below LP floor";
		case Rejection::REFERENCE_BLACK_NOT_BELOW_WHITE: return "black not below white";
		case Rejection::REFERENCE_FLOAT_PAIR_INVALID: return "invalid float Lb/Lw";
		case Rejection::CUBE_PATH_UNKNOWN: return "cube path unresolved";
		case Rejection::CUBE_CONTENT_IDENTITY_UNKNOWN: return "cube hash unresolved";
		case Rejection::CUBE_SIZE_UNSUPPORTED: return "cube size unsupported";
		case Rejection::CUBE_RELOAD_GENERATION_UNKNOWN: return "cube generation missing";
		case Rejection::DIRECT_DELIVERY_NOT_ATTESTED: return "direct not attested";
		case Rejection::EXTERNAL_COLOR_MANAGEMENT_NOT_ATTESTED_OFF: return "external CM not off";
		case Rejection::DISPLAY_MODE_MISSING: return "display mode missing";
		case Rejection::DISPLAY_MODE_AUTHORITY_UNSUPPORTED: return "mode authority invalid";
		case Rejection::ATTESTATION_RECORD_UNKNOWN: return "attestation hash missing";
		case Rejection::EXTERNAL_ANALYZER_RECORD_UNKNOWN: return "analyzer proof missing";
		case Rejection::EXPECTED_CARRIER_IDENTITY_INCOMPLETE: return "expected route incomplete";
		case Rejection::EXPECTED_CARRIER_GENERATION_UNKNOWN: return "expected route stale";
		case Rejection::CONTRACT_GENERATION_UNKNOWN: return "contract generation missing";
		case Rejection::ACTIVE_RESOURCE_PATH_MISMATCH: return "active cube path changed";
		case Rejection::ACTIVE_RESOURCE_CONTENT_MISMATCH: return "active cube content changed";
		case Rejection::ACTIVE_RESOURCE_SIZE_MISMATCH: return "active cube size changed";
		case Rejection::ACTIVE_RESOURCE_GENERATION_MISMATCH: return "active cube stale";
		case Rejection::CURRENT_CONTRACT_GENERATION_UNKNOWN: return "current contract missing";
		case Rejection::CONTRACT_GENERATION_MISMATCH: return "contract generation changed";
		case Rejection::CARRIER_UNSAFE: return "carrier unsafe";
		case Rejection::DXGI_COLOR_SPACE_NOT_ACTIVE: return "DXGI tag not active";
		case Rejection::PRESENTATION_NOT_FLIP: return "flip required";
		case Rejection::PRESENTATION_NOT_VP_OWNED: return "VP owner required";
		case Rejection::PRESENTATION_NOT_TOP_LEVEL: return "top-level required";
		case Rejection::DXGI_CARRIER_TAG_UNSUPPORTED: return "wrong DXGI carrier tag";
		case Rejection::SURFACE_FORMAT_UNSUPPORTED: return "R10 surface required";
		case Rejection::SAMPLE_DEPTH_UNSUPPORTED: return "10-bit samples required";
		case Rejection::COLOR_DEPTH_UNSUPPORTED: return "10-bit color required";
		case Rejection::BIT_SHIFT_UNSUPPORTED: return "unshifted R10 required";
		case Rejection::DISPLAY_DEPTH_UNSUPPORTED: return "10-bit display required";
		case Rejection::TARGET_SYSTEM_NOT_RGB: return "RGB target required";
		case Rejection::TARGET_PRIMARIES_MISMATCH: return "target primaries mismatch";
		case Rejection::ACCEPTED_OUTPUT_TRANSFER_MISMATCH: return "accepted TRC mismatch";
		case Rejection::TARGET_TRANSFER_MISMATCH: return "target TRC mismatch";
		case Rejection::TARGET_RANGE_MISMATCH: return "target range mismatch";
		case Rejection::TARGET_WHITE_MISMATCH: return "target white mismatch";
		case Rejection::TARGET_BLACK_MISMATCH: return "target black mismatch";
		case Rejection::ADVANCED_COLOR_NOT_CONFIRMED_OFF: return "Advanced Color not off";
		case Rejection::TARGET_ICC_NOT_CONFIRMED_ABSENT: return "target ICC not absent";
		case Rejection::BT2020_TARGET_NOT_ACTIVE: return "BT.2020 target inactive";
		case Rejection::BT2020_SIGNAL_EVIDENCE_INCOMPLETE: return "BT.2020 proof incomplete";
		case Rejection::CARRIER_IDENTITY_INCOMPLETE: return "current route incomplete";
		case Rejection::CARRIER_IDENTITY_MISMATCH: return "presentation route changed";
		case Rejection::CARRIER_GENERATION_UNKNOWN: return "route generation missing";
		case Rejection::CARRIER_GENERATION_MISMATCH: return "route generation changed";
		default: return "";
		}
	}

	SemanticBlackRejection ResolveLibplaceboBlackNits(
		const ReferenceLuminance& semanticBlack,
		float& libplaceboBlackNits)
	{
		libplaceboBlackNits = 0.0f;
		if (semanticBlack.origin == ReferenceOrigin::UNKNOWN)
			return SemanticBlackRejection::UNKNOWN;
		if (!std::isfinite(semanticBlack.nits))
			return SemanticBlackRejection::NOT_FINITE;
		if (semanticBlack.nits < 0.0)
			return SemanticBlackRejection::NEGATIVE;
		if (semanticBlack.nits == 0.0)
		{
			libplaceboBlackNits = PL_COLOR_HDR_BLACK;
			return SemanticBlackRejection::NONE;
		}
		if (!IsRepresentableAsPositiveFloat(semanticBlack.nits))
			return SemanticBlackRejection::NOT_REPRESENTABLE;
		if (semanticBlack.nits < static_cast<double>(PL_COLOR_HDR_BLACK))
			return SemanticBlackRejection::BELOW_LIBPLACEBO_FLOOR;
		libplaceboBlackNits = static_cast<float>(semanticBlack.nits);
		return SemanticBlackRejection::NONE;
	}

	const char* ShortReason(SemanticBlackRejection rejection)
	{
		switch (rejection)
		{
		case SemanticBlackRejection::UNKNOWN: return "semantic black unresolved";
		case SemanticBlackRejection::NOT_FINITE: return "semantic black not finite";
		case SemanticBlackRejection::NEGATIVE: return "semantic black negative";
		case SemanticBlackRejection::NOT_REPRESENTABLE: return "semantic black unrepresentable";
		case SemanticBlackRejection::BELOW_LIBPLACEBO_FLOOR: return "semantic black below floor";
		default: return "";
		}
	}

	const char* ToString(InputPrimaries value)
	{
		switch (value)
		{
		case InputPrimaries::REC709: return "REC709";
		case InputPrimaries::BT2020: return "BT2020";
		case InputPrimaries::P3_D65: return "P3-D65";
		case InputPrimaries::OTHER: return "OTHER";
		default: return "UNKNOWN";
		}
	}

	const char* ToString(InputTransfer value)
	{
		switch (value)
		{
		case InputTransfer::SRGB: return "sRGB";
		case InputTransfer::GAMMA22: return "GAMMA22";
		case InputTransfer::GAMMA24: return "GAMMA24";
		case InputTransfer::BT1886: return "BT1886";
		case InputTransfer::PQ: return "PQ";
		case InputTransfer::HLG: return "HLG";
		case InputTransfer::OTHER: return "OTHER";
		default: return "UNKNOWN";
		}
	}

	const char* ToString(InputRange value)
	{
		switch (value)
		{
		case InputRange::FULL: return "FULL";
		case InputRange::LIMITED: return "LIMITED";
		default: return "UNKNOWN";
		}
	}

	const char* ToString(ReferenceOrigin value)
	{
		switch (value)
		{
		case ReferenceOrigin::EXPLICIT_PROFILE: return "explicit_profile";
		case ReferenceOrigin::INHERITED_TARGET: return "inherited_target";
		case ReferenceOrigin::DERIVED_DEFAULT: return "derived_default";
		default: return "unknown";
		}
	}

	const char* ToString(DisplayModeAuthority value)
	{
		switch (value)
		{
		case DisplayModeAuthority::MANUAL_ATTESTED: return "manual_attested";
		case DisplayModeAuthority::NVIDIA_EXTERNAL_VERIFIED:
			return "nvidia_external_verified";
		default: return "unknown";
		}
	}
}
