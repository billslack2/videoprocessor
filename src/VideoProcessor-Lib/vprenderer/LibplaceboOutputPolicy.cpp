#include <pch.h>

#include "LibplaceboOutputPolicy.h"

#include <algorithm>

namespace LibplaceboOutput
{
	OneShotSignalAcceptance ClassifyOneShotSignal(
		bool setSucceeded,
		bool readbackSucceeded,
		bool readbackMatches)
	{
		if (!setSucceeded)
			return OneShotSignalAcceptance::FAILED;
		return readbackSucceeded && readbackMatches
			? OneShotSignalAcceptance::READBACK_VERIFIED
			: OneShotSignalAcceptance::SET_ACCEPTED;
	}

	PresentationRequest ParsePresentation(const std::string& value)
	{
		if (value == "composed")
			return PresentationRequest::COMPOSED;
		if (value == "direct")
			return PresentationRequest::DIRECT;
		return PresentationRequest::AUTO;
	}

	RangeRequest ParseRange(const std::string& value)
	{
		if (value == "full")
			return RangeRequest::FULL;
		if (value == "limited")
			return RangeRequest::LIMITED;
		return RangeRequest::AUTO;
	}

	GammaRequest ParseGamma(const std::string& value)
	{
		if (value == "srgb")
			return GammaRequest::SRGB;
		if (value == "2.2")
			return GammaRequest::GAMMA22;
		if (value == "2.4")
			return GammaRequest::GAMMA24;
		if (value == "auto")
			return GammaRequest::AUTO;
		return GammaRequest::UNSUPPORTED;
	}

	Plan MakePlan(const Request& request)
	{
		Plan result;
		result.request = request;
		// Advanced-color/BT.2020 presentation must use a flip-model swapchain.
		// Do not let an explicit composed request silently put a calibrated target
		// on the legacy BitBlt/DWM path.
		result.useBlit = request.primaries == PrimariesRequest::BT2020
			? false
			: request.presentation == PresentationRequest::COMPOSED ||
				(request.presentation == PresentationRequest::AUTO &&
					request.range != RangeRequest::LIMITED);

		if (request.range == RangeRequest::LIMITED)
		{
			result.requiresDxgiOverride = true;
			switch (request.gamma)
			{
			case GammaRequest::AUTO:
				// libplacebo 7.360.1 has no exact BT.709 piecewise target
				// transfer for DXGI Studio G22. G24 has an exact renderer/DXGI
				// representation and is therefore the safe AUTO choice.
				result.desiredEncoding =
					request.primaries == PrimariesRequest::BT2020
						? DxgiEncoding::STUDIO_G24_P2020
						: DxgiEncoding::STUDIO_G24_P709;
				result.targetTransfer = TargetTransfer::GAMMA24;
				break;
			case GammaRequest::GAMMA24:
				result.desiredEncoding =
					request.primaries == PrimariesRequest::BT2020
						? DxgiEncoding::STUDIO_G24_P2020
						: DxgiEncoding::STUDIO_G24_P709;
				result.targetTransfer = TargetTransfer::GAMMA24;
				break;
			default:
				result.valid = false;
				result.reason =
					"requested output gamma has no matching limited-range DXGI declaration";
				break;
			}
			return result;
		}

		// DXGI has no Full RGB gamma-2.4 Rec.709 declaration. Full G22 is
		// specifically the sRGB piecewise transfer, not an arbitrary power curve.
		if (request.gamma != GammaRequest::AUTO &&
			request.gamma != GammaRequest::SRGB)
		{
			result.valid = false;
			result.reason =
				"requested output gamma has no matching full-range DXGI declaration";
		}
		if (result.valid && request.primaries == PrimariesRequest::BT2020)
		{
			result.requiresDxgiOverride = true;
			result.desiredEncoding = DxgiEncoding::FULL_G22_P2020;
		}
		return result;
	}

	SdrOutputContract MakeSdrOutputContract(
		Request requestedTransport,
		SdrTargetPrimaries target,
		bool reportBt2020ToDisplay)
	{
		// P2020 DXGI was previously tried as the transport for F6. On VP's
		// NVIDIA/projector path it produced BT.2020-target pixels interpreted as
		// Rec.709. Keep transport representation and pixel target separate.
		requestedTransport.primaries = PrimariesRequest::REC709;
		return { target, requestedTransport,
			target == SdrTargetPrimaries::BT2020 && reportBt2020ToDisplay };
	}

	Actual Finalize(const Plan& plan, const Evidence& evidence)
	{
		Actual result;
		result.presentationModel = evidence.presentationModel;

		const bool fullRestoreVerified =
			evidence.fullRestorePresentSupportedBeforeSet &&
			evidence.fullRestoreSetSucceeded &&
			evidence.fullRestorePresentSupportedAfterSet;
		if (evidence.fullRestoreRequired && !evidence.hasSwapchain3)
		{
			result.safeToRender = false;
			result.reason =
				"the previous studio state cannot be restored without IDXGISwapChain3";
			return result;
		}
		if (evidence.fullRestoreRequired &&
			!fullRestoreVerified)
		{
			result.safeToRender = false;
			result.reason =
				"the Full/sRGB restore did not pass Check/Set/Check verification";
			return result;
		}
		if (!plan.valid)
		{
			result.reason = plan.reason;
			return result;
		}
		if (plan.request.primaries == PrimariesRequest::BT2020 &&
			evidence.presentationModel != PresentationModel::FLIP)
		{
			result.safeToRender = false;
			result.reason =
				"BT.2020 output requires a flip-model DXGI swapchain";
			return result;
		}

		if (!plan.requiresDxgiOverride)
		{
			result.requestedEncodingActive = true;
			result.reason = evidence.fullRestoreRequired
				? "restored Full/sRGB after SetColorSpace1 and capability checks"
				: "using the libplacebo-negotiated Full/sRGB swapchain contract";
			return result;
		}

		if (!evidence.hasSwapchain3)
		{
			result.reason = "IDXGISwapChain3 is unavailable";
			return result;
		}
		if (evidence.setSucceeded &&
			!evidence.presentSupportedAfterSet &&
			!evidence.fullRestoreRequired)
		{
			result.safeToRender = false;
			result.reason =
				"studio state was changed without a successful Full/sRGB restore";
			return result;
		}
		if (!evidence.presentSupportedBeforeSet)
		{
			result.reason =
				"the requested studio color space was not advertised for presentation";
			return result;
		}
		if (!evidence.setSucceeded)
		{
			result.reason = "SetColorSpace1 rejected the requested studio color space";
			return result;
		}
		if (!evidence.presentSupportedAfterSet)
		{
			result.reason =
				"the requested studio color space was not present-capable after configuration";
			return result;
		}

		result.encoding = plan.desiredEncoding;
		result.targetTransfer = plan.targetTransfer;
		result.requestedEncodingActive = true;
		result.reason =
			"DXGI advertised and accepted the requested studio color space";
		return result;
	}

	bool ShouldFallbackToComposed(const Plan& plan, const Actual& actual)
	{
		return plan.request.presentation == PresentationRequest::AUTO &&
			plan.request.range == RangeRequest::LIMITED &&
			!actual.requestedEncodingActive;
	}

	PackedR10Stats AnalyzePackedR10(
		const uint32_t* pixels,
		size_t rowPitchWords,
		unsigned int width,
		unsigned int height,
		unsigned int sampleStep)
	{
		PackedR10Stats result;
		if (!pixels || rowPitchWords < width || width == 0 || height == 0)
			return result;

		sampleStep = std::max(1u, sampleStep);
		for (unsigned int y = 0; y < height; y += sampleStep)
		{
			const uint32_t* row = pixels + static_cast<size_t>(y) * rowPitchWords;
			for (unsigned int x = 0; x < width; x += sampleStep)
			{
				const uint32_t packed = row[x];
				const uint16_t channels[3] = {
					static_cast<uint16_t>(packed & 0x3FFu),
					static_cast<uint16_t>((packed >> 10) & 0x3FFu),
					static_cast<uint16_t>((packed >> 20) & 0x3FFu)
				};
				for (size_t channel = 0; channel < 3; ++channel)
				{
					result.minimum[channel] =
						std::min(result.minimum[channel], channels[channel]);
					result.maximum[channel] =
						std::max(result.maximum[channel], channels[channel]);
					result.sum[channel] += channels[channel];
					if (channels[channel] < 64)
						++result.channelsBelowStudioBlack;
					if (channels[channel] > 940)
						++result.channelsAboveStudioWhite;
				}
				++result.sampledPixels;
			}
		}
		return result;
	}

	const char* ToString(PresentationRequest value)
	{
		switch (value)
		{
		case PresentationRequest::AUTO: return "AUTO";
		case PresentationRequest::COMPOSED: return "COMPOSED";
		case PresentationRequest::DIRECT: return "DIRECT";
		}
		return "UNKNOWN";
	}

	const char* ToString(PresentationModel value)
	{
		switch (value)
		{
		case PresentationModel::BITBLT: return "COMPOSED/BITBLT";
		case PresentationModel::FLIP: return "FLIP/DIRECT-ELIGIBLE";
		default: return "UNKNOWN";
		}
	}

	const char* ToString(RangeRequest value)
	{
		switch (value)
		{
		case RangeRequest::AUTO: return "AUTO";
		case RangeRequest::FULL: return "FULL";
		case RangeRequest::LIMITED: return "LIMITED";
		}
		return "UNKNOWN";
	}

	const char* ToString(GammaRequest value)
	{
		switch (value)
		{
		case GammaRequest::AUTO: return "AUTO";
		case GammaRequest::SRGB: return "sRGB";
		case GammaRequest::GAMMA22: return "2.2";
		case GammaRequest::GAMMA24: return "2.4";
		default: return "UNSUPPORTED";
		}
	}

	const char* ToString(PrimariesRequest value)
	{
		return value == PrimariesRequest::BT2020 ? "BT.2020" : "Rec.709";
	}

	const char* ToString(DxgiEncoding value)
	{
		switch (value)
		{
		case DxgiEncoding::FULL_G22_P709: return "RGB_FULL_G22_NONE_P709";
		case DxgiEncoding::STUDIO_G22_P709: return "RGB_STUDIO_G22_NONE_P709";
		case DxgiEncoding::STUDIO_G24_P709: return "RGB_STUDIO_G24_NONE_P709";
		case DxgiEncoding::FULL_G22_P2020: return "RGB_FULL_G22_NONE_P2020";
		case DxgiEncoding::STUDIO_G22_P2020: return "RGB_STUDIO_G22_NONE_P2020";
		case DxgiEncoding::STUDIO_G24_P2020: return "RGB_STUDIO_G24_NONE_P2020";
		}
		return "UNKNOWN";
	}

	const char* ToRangeString(DxgiEncoding value)
	{
		return value == DxgiEncoding::FULL_G22_P709 ||
			value == DxgiEncoding::FULL_G22_P2020 ? "FULL" : "LIMITED";
	}

	const char* ToGammaString(DxgiEncoding value)
	{
		return value == DxgiEncoding::STUDIO_G24_P709 ||
			value == DxgiEncoding::STUDIO_G24_P2020 ? "2.4" :
			value == DxgiEncoding::FULL_G22_P709 ||
			value == DxgiEncoding::FULL_G22_P2020 ? "sRGB/G22" : "BT.709/G22";
	}
}
