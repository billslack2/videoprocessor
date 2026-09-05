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
		if (value == "bt1886")
			return GammaRequest::BT1886;
		if (value == "1.8")
			return GammaRequest::GAMMA18;
		if (value == "2.0")
			return GammaRequest::GAMMA20;
		if (value == "2.2")
			return GammaRequest::GAMMA22;
		if (value == "2.4")
			return GammaRequest::GAMMA24;
		if (value == "2.6")
			return GammaRequest::GAMMA26;
		if (value == "2.8")
			return GammaRequest::GAMMA28;
		if (value == "auto")
			return GammaRequest::AUTO;
		return GammaRequest::UNSUPPORTED;
	}

	SdrAdjustGamma ParseSdrAdjustGamma(const std::string& value)
	{
		if (value == "auto") return SdrAdjustGamma::AUTO;
		if (value == "off" || value == "no") return SdrAdjustGamma::OFF;
		return SdrAdjustGamma::ON;
	}

	SdrGammaDecision ResolveSdrGamma(
		SdrAdjustGamma requested,
		bool inputIsSdr,
		bool outputSafe,
		GammaRequest configuredOutputGamma,
		SdrTransfer declaredSource,
		SdrTransfer actualTarget)
	{
		SdrGammaDecision result;
		result.requested = requested;
		result.declaredSource = declaredSource;
		result.effectiveSource = declaredSource;
		result.actualTarget = actualTarget;
		if (!inputIsSdr)
		{
			result.action = SdrGammaAction::NOT_APPLICABLE;
			result.reason = "input transfer is not SDR";
			return result;
		}
		if (!outputSafe)
		{
			result.action = SdrGammaAction::BLOCKED;
			result.reason = "the accepted output contract is unsafe";
			return result;
		}
		if (actualTarget == SdrTransfer::UNKNOWN ||
			actualTarget == SdrTransfer::OTHER)
		{
			result.action = SdrGammaAction::ADJUST;
			result.reason = "the accepted SDR output transfer is unknown";
			return result;
		}

		bool suppress = requested == SdrAdjustGamma::OFF;
		if (requested == SdrAdjustGamma::AUTO)
		{
			const bool commonAmbiguousSource =
				declaredSource == SdrTransfer::BT1886 ||
				declaredSource == SdrTransfer::GAMMA22 ||
				declaredSource == SdrTransfer::SRGB;
			suppress = configuredOutputGamma == GammaRequest::AUTO &&
				actualTarget == SdrTransfer::SRGB && commonAmbiguousSource;
		}
		if (suppress)
		{
			result.action = SdrGammaAction::SUPPRESS;
			result.effectiveSource = actualTarget;
			result.reason = requested == SdrAdjustGamma::AUTO
				? "automatic sRGB output with an ambiguous common SDR source"
				: "SDR source treated as already encoded for the accepted output transfer";
		}
		else
		{
			result.action = SdrGammaAction::ADJUST;
			result.reason = requested == SdrAdjustGamma::AUTO
				? "explicit or non-sRGB output honors source and output metadata"
				: "source and output transfer metadata are honored";
		}
		return result;
	}

	SdrTransfer ResolveCalibrationTargetTransfer(
		GammaRequest configuredOutputGamma,
		SdrTransfer acceptedOutputTransfer)
	{
		switch (configuredOutputGamma)
		{
		case GammaRequest::SRGB: return SdrTransfer::SRGB;
		case GammaRequest::BT1886: return SdrTransfer::BT1886;
		case GammaRequest::GAMMA18: return SdrTransfer::GAMMA18;
		case GammaRequest::GAMMA20: return SdrTransfer::GAMMA20;
		case GammaRequest::GAMMA22: return SdrTransfer::GAMMA22;
		case GammaRequest::GAMMA24: return SdrTransfer::GAMMA24;
		case GammaRequest::GAMMA26: return SdrTransfer::GAMMA26;
		case GammaRequest::GAMMA28: return SdrTransfer::GAMMA28;
		case GammaRequest::AUTO:
			return acceptedOutputTransfer;
		default:
			return acceptedOutputTransfer;
		}
	}

	Plan MakePlan(const Request& request)
	{
		Plan result;
		result.request = request;
		// Advanced-color/BT.2020 presentation must use a flip-model swapchain.
		// Do not let an explicit composed request silently put a calibrated target
		// on the legacy BitBlt/DWM path.
		// AUTO prefers the modern flip model. The renderer separately forces its
		// embedded WS_CHILD preview to COMPOSED, so this changes only top-level
		// windowed/fullscreen hosts. Flip preserves DWM composition when needed
		// while making DXGI frame statistics available for presentation timing.
		result.useBlit = request.primaries == PrimariesRequest::BT2020
			? false
			: request.presentation == PresentationRequest::COMPOSED;

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
			case GammaRequest::GAMMA22:
				if (!request.allowLimitedG22Experiment)
				{
					result.valid = false;
					result.reason =
						"Limited/Gamma-2.2 is a disabled Output Experiments transport";
					break;
				}
				result.desiredEncoding =
					request.primaries == PrimariesRequest::BT2020
						? DxgiEncoding::STUDIO_G22_P2020
						: DxgiEncoding::STUDIO_G22_P709;
				result.targetTransfer = TargetTransfer::GAMMA22;
				result.reason =
					"experimental Limited/Gamma-2.2 transport; verify pixels and display response";
				break;
			default:
				result.valid = false;
				result.reason =
					"requested output gamma has no matching limited-range DXGI declaration";
				break;
			}
			return result;
		}

		// The display's calibrated transfer and the Windows SDR surface
		// declaration are separate contracts.  A Full RGB G22/P709 swapchain is
		// the normal Windows presentation path; a calibrated display may still
		// require a BT.1886 or pure-power target, as madVR's calibration page
		// supports. Keep the standard surface declaration and apply the display
		// target in the renderer rather than treating it as a DXGI transport
		// request.
		switch (request.gamma)
		{
		case GammaRequest::AUTO:
		case GammaRequest::SRGB:
			break;
		case GammaRequest::BT1886:
			result.targetTransfer = TargetTransfer::BT1886;
			break;
		case GammaRequest::GAMMA18:
			result.targetTransfer = TargetTransfer::GAMMA18;
			break;
		case GammaRequest::GAMMA20:
			result.targetTransfer = TargetTransfer::GAMMA20;
			break;
		case GammaRequest::GAMMA22:
			result.targetTransfer = TargetTransfer::GAMMA22;
			break;
		case GammaRequest::GAMMA24:
			result.targetTransfer = TargetTransfer::GAMMA24;
			break;
		case GammaRequest::GAMMA26:
			result.targetTransfer = TargetTransfer::GAMMA26;
			break;
		case GammaRequest::GAMMA28:
			result.targetTransfer = TargetTransfer::GAMMA28;
			break;
		default:
			result.valid = false;
			result.reason =
				"requested display calibration transfer is unsupported";
			break;
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
			result.safeToRender = !plan.strictContract;
			result.reason = plan.reason;
			return result;
		}
		if (plan.strictContract && !evidence.vpOwnsPresentation)
		{
			result.safeToRender = false;
			result.reason =
				"the strict calibrated-display contract requires VP-owned presentation";
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
			result.targetTransfer = plan.targetTransfer;
			result.requestedEncodingActive = true;
			result.reason = evidence.fullRestoreRequired
				? "restored Full/sRGB after SetColorSpace1 and capability checks"
				: "using the standard Windows Full/sRGB swapchain contract";
			return result;
		}

		if (!evidence.hasSwapchain3)
		{
			result.safeToRender = !plan.strictContract;
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
			result.safeToRender = !plan.strictContract;
			result.reason =
				"the requested studio color space was not advertised for presentation";
			return result;
		}
		if (!evidence.setSucceeded)
		{
			result.safeToRender = !plan.strictContract;
			result.reason = "SetColorSpace1 rejected the requested studio color space";
			return result;
		}
		if (!evidence.presentSupportedAfterSet)
		{
			result.safeToRender = !plan.strictContract;
			result.reason =
				"the requested studio color space was not present-capable after configuration";
			return result;
		}

		result.encoding = plan.desiredEncoding;
		result.targetTransfer = plan.targetTransfer;
		result.requestedEncodingActive = true;
		result.reason =
			plan.strictContract
				? "DXGI accepted the nominal Full-G22 declaration; pure-2.2 pixel response remains unverified"
				: "DXGI advertised and accepted the requested color space";
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
					const uint16_t value = channels[channel];
					const size_t bucket = value == 0 ? 0 :
						value <= 3 ? 1 : value <= 15 ? 2 :
						value <= 31 ? 3 : value <= 63 ? 4 :
						value <= 79 ? 5 : value <= 127 ? 6 : 7;
					++result.nearBlackBuckets[bucket];
				}
				++result.sampledPixels;
			}
		}
		return result;
	}

	uint16_t ExpandR10ToR16(uint16_t value)
	{
		value = static_cast<uint16_t>(value & 0x3ffu);
		return static_cast<uint16_t>((value << 6) | (value >> 4));
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
		case GammaRequest::BT1886: return "BT.1886";
		case GammaRequest::GAMMA18: return "1.8";
		case GammaRequest::GAMMA20: return "2.0";
		case GammaRequest::GAMMA22: return "2.2";
		case GammaRequest::GAMMA24: return "2.4";
		case GammaRequest::GAMMA26: return "2.6";
		case GammaRequest::GAMMA28: return "2.8";
		default: return "UNSUPPORTED";
		}
	}

	const char* ToString(SdrAdjustGamma value)
	{
		switch (value)
		{
		case SdrAdjustGamma::AUTO: return "AUTO";
		case SdrAdjustGamma::OFF: return "OFF";
		default: return "ON";
		}
	}

	const char* ToString(SdrTransfer value)
	{
		switch (value)
		{
		case SdrTransfer::BT1886: return "BT1886";
		case SdrTransfer::SRGB: return "sRGB";
		case SdrTransfer::GAMMA18: return "GAMMA18";
		case SdrTransfer::GAMMA20: return "GAMMA20";
		case SdrTransfer::GAMMA22: return "GAMMA22";
		case SdrTransfer::GAMMA24: return "GAMMA24";
		case SdrTransfer::GAMMA26: return "GAMMA26";
		case SdrTransfer::GAMMA28: return "GAMMA28";
		case SdrTransfer::OTHER: return "OTHER";
		default: return "UNKNOWN";
		}
	}

	const char* ToString(SdrGammaAction value)
	{
		switch (value)
		{
		case SdrGammaAction::SUPPRESS: return "SUPPRESS";
		case SdrGammaAction::NOT_APPLICABLE: return "NOT_APPLICABLE";
		case SdrGammaAction::BLOCKED: return "BLOCKED";
		default: return "ADJUST";
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

	const char* ToString(TargetTransfer value)
	{
		switch (value)
		{
		case TargetTransfer::SWAPCHAIN: return "sRGB";
		case TargetTransfer::BT1886: return "BT.1886";
		case TargetTransfer::GAMMA18: return "Pure Gamma 1.8";
		case TargetTransfer::GAMMA20: return "Pure Gamma 2.0";
		case TargetTransfer::GAMMA22: return "Pure Gamma 2.2";
		case TargetTransfer::GAMMA24: return "Pure Gamma 2.4";
		case TargetTransfer::GAMMA26: return "Pure Gamma 2.6";
		case TargetTransfer::GAMMA28: return "Pure Gamma 2.8";
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
