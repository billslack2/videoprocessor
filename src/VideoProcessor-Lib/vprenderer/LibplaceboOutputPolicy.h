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
		GAMMA22,
		GAMMA24,
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
		GAMMA24
	};

	struct Request
	{
		PresentationRequest presentation = PresentationRequest::AUTO;
		RangeRequest range = RangeRequest::AUTO;
		GammaRequest gamma = GammaRequest::AUTO;
		PrimariesRequest primaries = PrimariesRequest::REC709;
	};

	struct Plan
	{
		Request request;
		bool useBlit = true;
		bool valid = true;
		bool requiresDxgiOverride = false;
		DxgiEncoding desiredEncoding = DxgiEncoding::FULL_G22_P709;
		TargetTransfer targetTransfer = TargetTransfer::SWAPCHAIN;
		std::string reason;
	};

	struct Evidence
	{
		PresentationModel presentationModel = PresentationModel::UNKNOWN;
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
	Plan MakePlan(const Request& request);
	Actual Finalize(const Plan& plan, const Evidence& evidence);
	bool ShouldFallbackToComposed(const Plan& plan, const Actual& actual);
	PackedR10Stats AnalyzePackedR10(
		const uint32_t* pixels,
		size_t rowPitchWords,
		unsigned int width,
		unsigned int height,
		unsigned int sampleStep = 1);
	const char* ToString(PresentationRequest value);
	const char* ToString(PresentationModel value);
	const char* ToString(RangeRequest value);
	const char* ToString(GammaRequest value);
	const char* ToString(PrimariesRequest value);
	const char* ToString(DxgiEncoding value);
	const char* ToRangeString(DxgiEncoding value);
	const char* ToGammaString(DxgiEncoding value);
}
