#pragma once

#include <cstdint>
#include <string>


namespace LibplaceboLutContract
{
	constexpr uint32_t CARRIER_IDENTITY_SCHEMA_VERSION = 1;
	constexpr unsigned int MAX_CUBE_SIZE = 128;

	enum class Role
	{
		UNKNOWN,
		TARGET_DISPLAY_CALIBRATION,
		OTHER
	};

	enum class Scope
	{
		UNKNOWN,
		VIDEO_PICTURE,
		WHOLE_FRAME,
		OTHER
	};

	// The v1 cube is a libplacebo target/native LUT. It consumes the fully
	// encoded target representation and precedes final video dithering.
	enum class AttachmentStage
	{
		UNKNOWN,
		TARGET_NATIVE_POST_ENCODE_PRE_DITHER,
		OTHER
	};

	// Native LUT output is device drive. It must not acquire the Rec.709 or
	// BT.2020 label that describes only the LUT's input reference space.
	enum class NativeOutputSemantics
	{
		UNKNOWN,
		OPAQUE_DEVICE_DRIVE,
		OTHER
	};

	enum class InputPrimaries
	{
		UNKNOWN,
		REC709,
		BT2020,
		P3_D65,
		OTHER
	};

	enum class InputTransfer
	{
		UNKNOWN,
		SRGB,
		GAMMA22,
		GAMMA24,
		BT1886,
		PQ,
		HLG,
		OTHER
	};

	enum class InputRange
	{
		UNKNOWN,
		FULL,
		LIMITED
	};

	// A resolved value may inherit a concrete value from the final target, but
	// must never retain AUTO/unknown semantics. Black-scaled power and BT.1886
	// profiles additionally require explicitly authored Lw/Lb values.
	enum class ReferenceOrigin
	{
		UNKNOWN,
		EXPLICIT_PROFILE,
		INHERITED_TARGET,
		DERIVED_DEFAULT
	};

	struct ReferenceLuminance
	{
		double nits = 0.0;
		ReferenceOrigin origin = ReferenceOrigin::UNKNOWN;
	};

	enum class ContractOrigin
	{
		UNKNOWN,
		SELECTED_RENDERER_PROFILE
	};

	enum class PresentationModel
	{
		UNKNOWN,
		BITBLT,
		FLIP
	};

	// This is only the inert Windows/DXGI carrier declaration. It says nothing
	// about the opaque post-LUT native device-drive values.
	enum class DxgiCarrierTag
	{
		UNKNOWN,
		FULL_G22_P709,
		OTHER
	};

	enum class SurfaceFormat
	{
		UNKNOWN,
		R10G10B10A2_UNORM,
		OTHER
	};

	enum class TargetSystem
	{
		UNKNOWN,
		RGB,
		OTHER
	};

	enum class DirectDeliveryAuthority
	{
		UNKNOWN,
		EXTERNAL_ATTESTED
	};

	enum class DisplayModeAuthority
	{
		UNKNOWN,
		MANUAL_ATTESTED,
		NVIDIA_EXTERNAL_VERIFIED
	};

	enum class AdvancedColorState
	{
		UNKNOWN,
		DISABLED,
		ENABLED
	};

	enum class TargetIccState
	{
		UNKNOWN,
		ABSENT,
		ATTACHED
	};

	enum class ExternalColorManagement
	{
		UNKNOWN,
		NONE_ATTESTED,
		PRESENT
	};

	enum class OutputTechnology
	{
		UNKNOWN,
		HDMI,
		DISPLAYPORT,
		DVI,
		INTERNAL,
		OTHER_KNOWN
	};

	struct AdapterLuidIdentity
	{
		bool known = false;
		int32_t highPart = 0;
		uint32_t lowPart = 0;
	};

	// Versioned, typed identity of the presentation route. Unknown topology,
	// timing, EDID, adapter, or driver facts fail closed.
	struct CarrierIdentity
	{
		uint32_t schemaVersion = 0;
		std::string edidSha256;
		AdapterLuidIdentity displayConfigAdapter;
		bool displayConfigTargetKnown = false;
		uint32_t displayConfigTargetId = 0;
		OutputTechnology outputTechnology = OutputTechnology::UNKNOWN;
		uint32_t activeWidth = 0;
		uint32_t activeHeight = 0;
		uint32_t refreshNumerator = 0;
		uint32_t refreshDenominator = 0;
		bool scanlineOrderingKnown = false;
		uint32_t scanlineOrdering = 0;
		bool scalingKnown = false;
		uint32_t scaling = 0;
		AdapterLuidIdentity rendererAdapter;
		bool driverVersionKnown = false;
		uint64_t driverVersion = 0;
	};

	struct CubeIdentity
	{
		std::string canonicalPath;
		std::string contentSha256;
		unsigned int cubeSize = 0;
		uint64_t reloadGeneration = 0;
	};

	// These are immutable declarations from the selected calibrated profile,
	// not facts inferred from a flip swap effect or a DXGI color-space label.
	struct InstallationContract
	{
		DirectDeliveryAuthority directDeliveryAuthority =
			DirectDeliveryAuthority::UNKNOWN;
		ExternalColorManagement externalColorManagement =
			ExternalColorManagement::UNKNOWN;
		std::string displayMode;
		DisplayModeAuthority displayModeAuthority =
			DisplayModeAuthority::UNKNOWN;
		// Canonical hash of the selected mode, direct-delivery, physical-link,
		// external-color-management, and projector-mode attestation record.
		std::string attestationRecordSha256;
		// Required in addition to NVAPI SET/readback for NVIDIA authority.
		std::string externalAnalyzerRecordSha256;
	};

	// This is the immutable, fully resolved reference space presented to the
	// cube. authoringCodeDepth describes the C/(2^n-1) authoring grid; the
	// shader still evaluates continuous normalized coordinates before dither.
	struct ResolvedContract
	{
		Role role = Role::UNKNOWN;
		Scope scope = Scope::UNKNOWN;
		AttachmentStage attachmentStage = AttachmentStage::UNKNOWN;
		NativeOutputSemantics nativeOutputSemantics =
			NativeOutputSemantics::UNKNOWN;
		ContractOrigin origin = ContractOrigin::UNKNOWN;
		InputPrimaries inputPrimaries = InputPrimaries::UNKNOWN;
		InputTransfer inputTransfer = InputTransfer::UNKNOWN;
		InputRange inputRange = InputRange::UNKNOWN;
		unsigned int authoringCodeDepth = 0;
		ReferenceLuminance referenceWhite;
		ReferenceLuminance referenceBlack;
		CubeIdentity cube;
		InstallationContract installation;
		CarrierIdentity expectedCarrierIdentity;
		uint64_t expectedCarrierGeneration = 0;
		uint64_t calibrationContractGeneration = 0;
	};

	// Renderer/OS probing populates only observed runtime facts here. Semantic
	// target Lw/Lb remain doubles so zero is compared as zero, never as the
	// libplacebo PL_COLOR_HDR_BLACK adapter sentinel.
	struct CarrierEvidence
	{
		bool safeToRender = false;
		bool declaredDxgiColorSpaceActive = false;
		PresentationModel presentationModel = PresentationModel::UNKNOWN;
		bool vpOwnsPresentation = false;
		bool topLevelPresenter = false;
		DxgiCarrierTag dxgiColorSpaceTag = DxgiCarrierTag::UNKNOWN;
		SurfaceFormat surfaceFormat = SurfaceFormat::UNKNOWN;
		unsigned int sampleBitDepth = 0;
		unsigned int colorBitDepth = 0;
		unsigned int bitShift = 0;
		unsigned int displayBitDepth = 0;
		TargetSystem finalTargetSystem = TargetSystem::UNKNOWN;
		InputPrimaries finalTargetPrimaries = InputPrimaries::UNKNOWN;
		InputTransfer acceptedOutputTransfer = InputTransfer::UNKNOWN;
		InputTransfer finalTargetTransfer = InputTransfer::UNKNOWN;
		InputRange finalTargetRange = InputRange::UNKNOWN;
		double finalTargetWhiteNits = 0.0;
		double finalTargetBlackNits = 0.0;
		AdvancedColorState advancedColor = AdvancedColorState::UNKNOWN;
		TargetIccState targetIcc = TargetIccState::UNKNOWN;
		bool targetBt2020 = false;
		bool nvidiaBt2020SetAndReadbackVerified = false;
		CarrierIdentity identity;
		uint64_t carrierGeneration = 0;
	};

	struct ActiveResourceEvidence
	{
		std::string canonicalPath;
		std::string contentSha256;
		unsigned int cubeSize = 0;
		uint64_t reloadGeneration = 0;
	};

	struct ActivationContext
	{
		CarrierEvidence carrier;
		ActiveResourceEvidence resource;
		uint64_t currentCalibrationContractGeneration = 0;
	};

	enum class Rejection
	{
		NONE,
		ROLE_UNKNOWN,
		ROLE_UNSUPPORTED,
		SCOPE_UNKNOWN,
		SCOPE_UNSUPPORTED,
		ATTACHMENT_STAGE_UNKNOWN,
		ATTACHMENT_STAGE_UNSUPPORTED,
		NATIVE_OUTPUT_UNKNOWN,
		NATIVE_OUTPUT_UNSUPPORTED,
		CONTRACT_ORIGIN_UNKNOWN,
		PRIMARIES_UNKNOWN,
		PRIMARIES_UNSUPPORTED,
		TRANSFER_UNKNOWN,
		TRANSFER_UNSUPPORTED,
		PROFILE_COMBINATION_UNSUPPORTED,
		RANGE_UNKNOWN,
		FULL_RANGE_REQUIRED,
		AUTHORING_DEPTH_UNKNOWN,
		R10_AUTHORING_REQUIRED,
		REFERENCE_WHITE_ORIGIN_UNKNOWN,
		REFERENCE_WHITE_DERIVED,
		REFERENCE_WHITE_NOT_EXPLICIT,
		REFERENCE_WHITE_NOT_FINITE,
		REFERENCE_WHITE_OUT_OF_RANGE,
		REFERENCE_WHITE_NOT_REPRESENTABLE,
		REFERENCE_BLACK_ORIGIN_UNKNOWN,
		REFERENCE_BLACK_DERIVED,
		REFERENCE_BLACK_NOT_EXPLICIT,
		REFERENCE_BLACK_NOT_FINITE,
		REFERENCE_BLACK_NEGATIVE,
		REFERENCE_BLACK_NOT_REPRESENTABLE,
		REFERENCE_BLACK_BELOW_LIBPLACEBO_FLOOR,
		REFERENCE_BLACK_NOT_BELOW_WHITE,
		REFERENCE_FLOAT_PAIR_INVALID,
		CUBE_PATH_UNKNOWN,
		CUBE_CONTENT_IDENTITY_UNKNOWN,
		CUBE_SIZE_UNSUPPORTED,
		CUBE_RELOAD_GENERATION_UNKNOWN,
		DIRECT_DELIVERY_NOT_ATTESTED,
		EXTERNAL_COLOR_MANAGEMENT_NOT_ATTESTED_OFF,
		DISPLAY_MODE_MISSING,
		DISPLAY_MODE_AUTHORITY_UNSUPPORTED,
		ATTESTATION_RECORD_UNKNOWN,
		EXTERNAL_ANALYZER_RECORD_UNKNOWN,
		EXPECTED_CARRIER_IDENTITY_INCOMPLETE,
		EXPECTED_CARRIER_GENERATION_UNKNOWN,
		CONTRACT_GENERATION_UNKNOWN,
		ACTIVE_RESOURCE_PATH_MISMATCH,
		ACTIVE_RESOURCE_CONTENT_MISMATCH,
		ACTIVE_RESOURCE_SIZE_MISMATCH,
		ACTIVE_RESOURCE_GENERATION_MISMATCH,
		CURRENT_CONTRACT_GENERATION_UNKNOWN,
		CONTRACT_GENERATION_MISMATCH,
		CARRIER_UNSAFE,
		DXGI_COLOR_SPACE_NOT_ACTIVE,
		PRESENTATION_NOT_FLIP,
		PRESENTATION_NOT_VP_OWNED,
		PRESENTATION_NOT_TOP_LEVEL,
		DXGI_CARRIER_TAG_UNSUPPORTED,
		SURFACE_FORMAT_UNSUPPORTED,
		SAMPLE_DEPTH_UNSUPPORTED,
		COLOR_DEPTH_UNSUPPORTED,
		BIT_SHIFT_UNSUPPORTED,
		DISPLAY_DEPTH_UNSUPPORTED,
		TARGET_SYSTEM_NOT_RGB,
		TARGET_PRIMARIES_MISMATCH,
		ACCEPTED_OUTPUT_TRANSFER_MISMATCH,
		TARGET_TRANSFER_MISMATCH,
		TARGET_RANGE_MISMATCH,
		TARGET_WHITE_MISMATCH,
		TARGET_BLACK_MISMATCH,
		ADVANCED_COLOR_NOT_CONFIRMED_OFF,
		TARGET_ICC_NOT_CONFIRMED_ABSENT,
		BT2020_TARGET_NOT_ACTIVE,
		BT2020_SIGNAL_EVIDENCE_INCOMPLETE,
		CARRIER_IDENTITY_INCOMPLETE,
		CARRIER_IDENTITY_MISMATCH,
		CARRIER_GENERATION_UNKNOWN,
		CARRIER_GENERATION_MISMATCH
	};

	enum class SemanticBlackRejection
	{
		NONE,
		UNKNOWN,
		NOT_FINITE,
		NEGATIVE,
		NOT_REPRESENTABLE,
		BELOW_LIBPLACEBO_FLOOR
	};

	Rejection ValidateResolvedContract(const ResolvedContract& contract);
	// This is the only public activation gate. It validates the contract first,
	// then its bound resource/generations, then every carrier predicate.
	Rejection ValidateActivation(
		const ResolvedContract& contract,
		const ActivationContext& context);
	bool IsAccepted(Rejection rejection);
	const char* ShortReason(Rejection rejection);

	// A literal zero is semantically valid display black, but libplacebo treats
	// hdr.min_luma == 0 as unknown. Preserve zero in ResolvedContract and use
	// this adapter only when populating libplacebo target metadata.
	SemanticBlackRejection ResolveLibplaceboBlackNits(
		const ReferenceLuminance& semanticBlack,
		float& libplaceboBlackNits);
	const char* ShortReason(SemanticBlackRejection rejection);

	const char* ToString(InputPrimaries value);
	const char* ToString(InputTransfer value);
	const char* ToString(InputRange value);
	const char* ToString(ReferenceOrigin value);
	const char* ToString(DisplayModeAuthority value);
}
