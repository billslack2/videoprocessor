#pragma once

#include <cstddef>
#include <cstdint>
#include <string>


namespace LibplaceboOutput
{
	enum class PresentationRequest
	{
		AUTO,
		COMPOSED,
		DIRECT
	};

	enum class PresentationModel
	{
		UNKNOWN,
		BITBLT,
		FLIP
	};

	enum class RangeRequest
	{
		AUTO,
		FULL,
		LIMITED
	};

	enum class GammaRequest
	{
		AUTO,
		SRGB,
		BT1886,
		GAMMA18,
		GAMMA20,
		GAMMA22,
		GAMMA24,
		GAMMA26,
		GAMMA28,
		UNSUPPORTED
	};

	enum class PrimariesRequest
	{
		REC709,
		BT2020
	};

	enum class DxgiEncoding
	{
		FULL_G22_P709,
		STUDIO_G22_P709,
		STUDIO_G24_P709,
		FULL_G22_P2020,
		STUDIO_G22_P2020,
		STUDIO_G24_P2020
	};

	enum class TargetTransfer
	{
		SWAPCHAIN,
		BT1886,
		GAMMA18,
		GAMMA20,
		GAMMA22,
		GAMMA24,
		GAMMA26,
		GAMMA28
	};

	enum class SdrAdjustGamma
	{
		AUTO,
		ON,
		OFF
	};

	// Renderer-independent transfer names used by the SDR gamma policy. The
	// libplacebo adapter translates these to/from pl_color_transfer at the frame
	// boundary, keeping this policy directly unit-testable.
	enum class SdrTransfer
	{
		UNKNOWN,
		BT1886,
		SRGB,
		GAMMA18,
		GAMMA20,
		GAMMA22,
		GAMMA24,
		GAMMA26,
		GAMMA28,
		OTHER
	};

	enum class SdrGammaAction
	{
		ADJUST,
		SUPPRESS,
		NOT_APPLICABLE,
		BLOCKED
	};

	struct SdrGammaDecision
	{
		SdrAdjustGamma requested = SdrAdjustGamma::ON;
		SdrGammaAction action = SdrGammaAction::ADJUST;
		SdrTransfer declaredSource = SdrTransfer::UNKNOWN;
		SdrTransfer effectiveSource = SdrTransfer::UNKNOWN;
		SdrTransfer actualTarget = SdrTransfer::UNKNOWN;
		std::string reason;
	};

	struct Request
	{
		PresentationRequest presentation = PresentationRequest::AUTO;
		RangeRequest range = RangeRequest::AUTO;
		GammaRequest gamma = GammaRequest::AUTO;
		PrimariesRequest primaries = PrimariesRequest::REC709;
		bool allowLimitedG22Experiment = false;
		bool allowFullG22Experiment = false;
		bool vpOwnedPresenter = false;
	};

	// The display target is independent of the DXGI transport.  In particular,
	// VP's projector-compatible SDR BT.2020 mode renders BT.2020 pixels while
	// deliberately retaining the P709/sRGB swapchain contract and using the
	// NVIDIA AVI InfoFrame for HDMI BT.2020 signalling.
	enum class SdrTargetPrimaries
	{
		REC709,
		P3_D65,
		BT2020,
	};

	struct SdrOutputContract
	{
		SdrTargetPrimaries target = SdrTargetPrimaries::REC709;
		Request transport;
		bool reportBt2020ToDisplay = false;
	};

	struct Plan
	{
		Request request;
		bool useBlit = true;
		bool valid = true;
		bool requiresDxgiOverride = false;
		bool strictContract = false;
		DxgiEncoding desiredEncoding = DxgiEncoding::FULL_G22_P709;
		TargetTransfer targetTransfer = TargetTransfer::SWAPCHAIN;
		std::string reason;
	};

	struct Evidence
	{
		PresentationModel presentationModel = PresentationModel::UNKNOWN;
		bool vpOwnsPresentation = false;
		bool hasSwapchain3 = false;
		bool presentSupportedBeforeSet = false;
		bool setSucceeded = false;
		bool presentSupportedAfterSet = false;
		bool fullRestoreRequired = false;
		bool fullRestorePresentSupportedBeforeSet = false;
		bool fullRestoreSetSucceeded = false;
		bool fullRestorePresentSupportedAfterSet = false;
	};

	struct Actual
	{
		PresentationModel presentationModel = PresentationModel::UNKNOWN;
		DxgiEncoding encoding = DxgiEncoding::FULL_G22_P709;
		TargetTransfer targetTransfer = TargetTransfer::SWAPCHAIN;
		bool requestedEncodingActive = false;
		bool safeToRender = true;
		std::string reason;
	};

	struct VerifiedTransition
	{
		bool presentSupportedBeforeSet = false;
		bool setSucceeded = false;
		bool presentSupportedAfterSet = false;
	};

	struct PackedR10Stats
	{
		uint64_t sampledPixels = 0;
		uint16_t minimum[3] = { 1023, 1023, 1023 };
		uint16_t maximum[3] = {};
		uint64_t sum[3] = {};
		uint64_t channelsBelowStudioBlack = 0;
		uint64_t channelsAboveStudioWhite = 0;
		// Per-channel code-value populations. These intentionally isolate the
		// near-black region where sRGB and pure-power transfer curves diverge.
		uint64_t nearBlackBuckets[8] = {};
	};

	// NvAPI documents an InfoFrame SET as a one-shot transmission to the
	// display. Some drivers subsequently return their automatic state from GET.
	enum class OneShotSignalAcceptance
	{
		FAILED,
		SET_ACCEPTED,
		READBACK_VERIFIED
	};

	template<typename CheckPresent, typename SetColorSpace>
	VerifiedTransition ExecuteVerifiedTransition(
		CheckPresent&& checkPresent,
		SetColorSpace&& setColorSpace)
	{
		VerifiedTransition result;
		result.presentSupportedBeforeSet = checkPresent();
		if (!result.presentSupportedBeforeSet)
			return result;
		result.setSucceeded = setColorSpace();
		if (!result.setSucceeded)
			return result;
		result.presentSupportedAfterSet = checkPresent();
		return result;
	}

	PresentationRequest ParsePresentation(const std::string& value);
	RangeRequest ParseRange(const std::string& value);
	GammaRequest ParseGamma(const std::string& value);
	SdrAdjustGamma ParseSdrAdjustGamma(const std::string& value);
	SdrGammaDecision ResolveSdrGamma(
		SdrAdjustGamma requested,
		bool inputIsSdr,
		bool outputSafe,
		GammaRequest configuredOutputGamma,
		SdrTransfer declaredSource,
		SdrTransfer actualTarget);
	// Resolve the pixel-domain transfer independently from LUT attachment.
	// Explicit display calibration wins; Auto retains the transfer of the
	// accepted output contract. Enabling an identity LUT must not change gamma.
	SdrTransfer ResolveCalibrationTargetTransfer(
		GammaRequest configuredOutputGamma,
		SdrTransfer acceptedOutputTransfer);
	Plan MakePlan(const Request& request);
	SdrOutputContract MakeSdrOutputContract(
		Request requestedTransport,
		SdrTargetPrimaries target,
		bool reportBt2020ToDisplay);
	Actual Finalize(const Plan& plan, const Evidence& evidence);
	bool ShouldFallbackToComposed(const Plan& plan, const Actual& actual);
	PackedR10Stats AnalyzePackedR10(
		const uint32_t* pixels,
		size_t rowPitchWords,
		unsigned int width,
		unsigned int height,
		unsigned int sampleStep = 1);
	uint16_t ExpandR10ToR16(uint16_t value);
	OneShotSignalAcceptance ClassifyOneShotSignal(
		bool setSucceeded,
		bool readbackSucceeded,
		bool readbackMatches);
	const char* ToString(PresentationRequest value);
	const char* ToString(PresentationModel value);
	const char* ToString(RangeRequest value);
	const char* ToString(GammaRequest value);
	const char* ToString(SdrAdjustGamma value);
	const char* ToString(SdrTransfer value);
	const char* ToString(SdrGammaAction value);
	const char* ToString(PrimariesRequest value);
	const char* ToString(DxgiEncoding value);
	const char* ToString(TargetTransfer value);
	const char* ToRangeString(DxgiEncoding value);
	const char* ToGammaString(DxgiEncoding value);
}
