#pragma once

#include <cmath>
#include <cstdint>

#include <vprenderer/LibplaceboExternalHdrLutPolicy.h>


namespace LibplaceboHdr10Output
{
	using Primaries = LibplaceboExternalHdrLut::Primaries;

	struct Chromaticity
	{
		uint16_t x = 0;
		uint16_t y = 0;
	};

	// DXGI_HDR_METADATA_HDR10-compatible values without coupling the pure policy
	// to Windows headers. Chromaticities use 0.00002 units. Maximum mastering
	// luminance uses whole nits; minimum mastering luminance uses 0.0001-nit
	// units.
	struct StaticMetadata
	{
		Chromaticity red;
		Chromaticity green;
		Chromaticity blue;
		Chromaticity white;
		uint32_t maxMasteringLuminance = 0;
		uint32_t minMasteringLuminance = 0;
		uint16_t maxContentLightLevel = 0;
		uint16_t maxFrameAverageLightLevel = 0;
	};

	struct MetadataResult
	{
		bool valid = false;
		StaticMetadata metadata;
		const char* reason = "metadata primaries are unknown";
	};

	inline Chromaticity EncodeChromaticity(double x, double y)
	{
		return { static_cast<uint16_t>(std::llround(x * 50000.0)),
			static_cast<uint16_t>(std::llround(y * 50000.0)) };
	}

	inline MetadataResult BuildStaticMetadata(Primaries primaries,
		double peakNits)
	{
		MetadataResult result;
		if (!LibplaceboExternalHdrLut::IsValidMetadataPeakNits(peakNits))
		{
			result.reason = "metadata peak must be between 1 and 10000 nits";
			return result;
		}
		switch (primaries)
		{
		case Primaries::BT709:
			result.metadata.red = EncodeChromaticity(0.640, 0.330);
			result.metadata.green = EncodeChromaticity(0.300, 0.600);
			result.metadata.blue = EncodeChromaticity(0.150, 0.060);
			break;
		case Primaries::P3_D65:
			result.metadata.red = EncodeChromaticity(0.680, 0.320);
			result.metadata.green = EncodeChromaticity(0.265, 0.690);
			result.metadata.blue = EncodeChromaticity(0.150, 0.060);
			break;
		case Primaries::BT2020:
			result.metadata.red = EncodeChromaticity(0.708, 0.292);
			result.metadata.green = EncodeChromaticity(0.170, 0.797);
			result.metadata.blue = EncodeChromaticity(0.131, 0.046);
			break;
		default:
			return result;
		}
		result.metadata.white = EncodeChromaticity(0.3127, 0.3290);
		result.metadata.maxMasteringLuminance =
			static_cast<uint32_t>(std::llround(peakNits));
		// V1 does not invent content measurements from the mastering target.
		result.metadata.minMasteringLuminance = 0;
		result.metadata.maxContentLightLevel = 0;
		result.metadata.maxFrameAverageLightLevel = 0;
		result.valid = true;
		result.reason = "complete HDR10 static metadata";
		return result;
	}

	struct Evidence
	{
		bool topLevelWindow = false;
		bool vpOwnedPresentation = false;
		bool flipPresentation = false;
		bool r10Swapchain = false;
		bool advancedColorActive = false;
		bool hasSwapchain3 = false;
		bool g2084SupportedBeforeSet = false;
		bool g2084SetSucceeded = false;
		bool g2084SupportedAfterSet = false;
		bool hasSwapchain4 = false;
		bool metadataSetSucceeded = false;
		bool hdrCarrierWasActive = false;
		bool rollbackMetadataClearSucceeded = false;
		bool rollbackSdrSetSucceeded = false;
		bool rollbackSdrVerified = false;
	};

	struct Activation
	{
		bool active = false;
		bool fallbackRequired = true;
		bool restoreSdrColorSpace = false;
		bool clearHdrMetadata = true;
		bool safeToPresentInternalSdr = false;
		const char* reason = "HDR10 output contract is incomplete";
	};

	inline Activation Evaluate(const MetadataResult& metadata,
		const Evidence& evidence)
	{
		Activation result;
		if (!metadata.valid)
			result.reason = metadata.reason;
		else if (!evidence.topLevelWindow)
			result.reason = "HDR10 output requires a top-level render window";
		else if (!evidence.vpOwnedPresentation)
			result.reason = "HDR10 output requires VP-owned presentation";
		else if (!evidence.flipPresentation)
			result.reason = "HDR10 output requires flip-model presentation";
		else if (!evidence.r10Swapchain)
			result.reason = "HDR10 output requires an R10G10B10A2 swapchain";
		else if (!evidence.advancedColorActive)
			result.reason = "Windows Advanced Color is not active";
		else if (!evidence.hasSwapchain3)
			result.reason = "IDXGISwapChain3 is unavailable";
		else if (!evidence.g2084SupportedBeforeSet)
			result.reason = "G2084/P2020 is not supported before SetColorSpace1";
		else if (!evidence.g2084SetSucceeded)
			result.reason = "SetColorSpace1(G2084/P2020) failed";
		else if (!evidence.g2084SupportedAfterSet)
			result.reason = "G2084/P2020 was not verified after SetColorSpace1";
		else if (!evidence.hasSwapchain4)
			result.reason = "IDXGISwapChain4 is unavailable";
		else if (!evidence.metadataSetSucceeded)
			result.reason = "SetHDRMetaData(HDR10) failed";
		else
		{
			result.active = true;
			result.fallbackRequired = false;
			result.clearHdrMetadata = false;
			result.reason = "API-accepted full-range PQ/BT.2020 HDR10 output";
			return result;
		}

		// A newly mutated or previously active HDR carrier must be fully rolled
		// back before the internal SDR renderer may present again. If rollback
		// cannot be verified, the caller must suppress Present and recreate.
		const bool rollbackRequired = evidence.hdrCarrierWasActive ||
			evidence.g2084SetSucceeded || evidence.metadataSetSucceeded;
		result.restoreSdrColorSpace = rollbackRequired;
		result.clearHdrMetadata = rollbackRequired;
		result.safeToPresentInternalSdr = !rollbackRequired ||
			(evidence.rollbackMetadataClearSucceeded &&
			 evidence.rollbackSdrSetSucceeded &&
			 evidence.rollbackSdrVerified);
		return result;
	}

	enum class CarrierPhase
	{
		SDR,
		HDR10_ACTIVE,
		SUPPRESS_RECREATE
	};

	// The renderer supplies the DXGI operations while this policy owns their
	// ordering. Keeping the mutation sequence here makes every partial failure
	// deterministic and independently testable without a real display.
	class CarrierOperations
	{
	public:
		virtual ~CarrierOperations() = default;
		virtual bool CheckHdrColorSpaceSupport() = 0;
		virtual bool SetHdrColorSpace() = 0;
		virtual bool SetHdrMetadata(const StaticMetadata& metadata) = 0;
		virtual bool ClearHdrMetadata() = 0;
		virtual bool CheckSdrColorSpaceSupport() = 0;
		virtual bool SetSdrColorSpace() = 0;
		virtual bool RecheckSdrColorSpaceSupportAfterSet() = 0;
	};

	struct CarrierState
	{
		CarrierPhase phase = CarrierPhase::SDR;
		uint64_t swapchainGeneration = 0;
		uint64_t applicationGeneration = 0;
		uint64_t lutTransactionGeneration = 0;
		MetadataResult metadata;
		Evidence evidence;
		Activation activation;

		bool Current(uint64_t swapchain, uint64_t application,
			uint64_t lutTransaction) const
		{
			return phase == CarrierPhase::HDR10_ACTIVE &&
				swapchain != 0 && application != 0 && lutTransaction != 0 &&
				swapchainGeneration == swapchain &&
				applicationGeneration == application &&
				lutTransactionGeneration == lutTransaction && activation.active;
		}

		bool ExternalHdrPresentAllowed(uint64_t swapchain, uint64_t application,
			uint64_t lutTransaction) const
		{
			return Current(swapchain, application, lutTransaction);
		}
	};

	inline bool PreflightReady(const MetadataResult& metadata,
		const Evidence& evidence)
	{
		return metadata.valid && evidence.topLevelWindow &&
			evidence.vpOwnedPresentation && evidence.flipPresentation &&
			evidence.r10Swapchain && evidence.advancedColorActive &&
			evidence.hasSwapchain3 && evidence.hasSwapchain4;
	}

	inline void Rollback(CarrierState& state, CarrierOperations& operations)
	{
		state.evidence.hdrCarrierWasActive =
			state.evidence.hdrCarrierWasActive ||
			state.phase == CarrierPhase::SUPPRESS_RECREATE ||
			state.phase == CarrierPhase::HDR10_ACTIVE ||
			state.evidence.g2084SetSucceeded ||
			state.evidence.metadataSetSucceeded;
		state.evidence.rollbackMetadataClearSucceeded =
			operations.ClearHdrMetadata();
		const bool sdrSupportedBeforeSet =
			operations.CheckSdrColorSpaceSupport();
		state.evidence.rollbackSdrSetSucceeded = sdrSupportedBeforeSet &&
			operations.SetSdrColorSpace();
		state.evidence.rollbackSdrVerified =
			state.evidence.rollbackSdrSetSucceeded &&
			operations.RecheckSdrColorSpaceSupportAfterSet();
		// Evaluate the post-rollback carrier, not the superseded activation.
		state.evidence.g2084SetSucceeded = false;
		state.evidence.g2084SupportedAfterSet = false;
		state.evidence.metadataSetSucceeded = false;
		state.activation = Evaluate(state.metadata, state.evidence);
		state.phase = state.activation.safeToPresentInternalSdr ?
			CarrierPhase::SDR : CarrierPhase::SUPPRESS_RECREATE;
	}

	inline const CarrierState& Activate(CarrierState& state,
		uint64_t swapchainGeneration, uint64_t applicationGeneration,
		uint64_t lutTransactionGeneration, const MetadataResult& metadata,
		Evidence evidence, CarrierOperations& operations)
	{
		if (state.swapchainGeneration == swapchainGeneration)
		{
			if (state.phase == CarrierPhase::SUPPRESS_RECREATE)
				return state;
			if (state.phase == CarrierPhase::HDR10_ACTIVE)
			{
				Rollback(state, operations);
				if (state.phase != CarrierPhase::SDR)
					return state;
			}
		}
		// A different generation proves that the prior DXGI object was replaced;
		// only that boundary may discard a suppressed carrier without rollback.
		state = {};
		state.swapchainGeneration = swapchainGeneration;
		state.applicationGeneration = applicationGeneration;
		state.lutTransactionGeneration = lutTransactionGeneration;
		state.metadata = metadata;
		state.evidence = evidence;
		if (swapchainGeneration == 0 || applicationGeneration == 0 ||
			lutTransactionGeneration == 0)
			return state;

		if (!PreflightReady(metadata, evidence))
		{
			state.activation = Evaluate(metadata, evidence);
			return state;
		}

		state.evidence.g2084SupportedBeforeSet =
			operations.CheckHdrColorSpaceSupport();
		if (state.evidence.g2084SupportedBeforeSet)
		{
			state.evidence.g2084SetSucceeded = operations.SetHdrColorSpace();
			if (state.evidence.g2084SetSucceeded)
			{
				state.evidence.g2084SupportedAfterSet =
					operations.CheckHdrColorSpaceSupport();
				if (state.evidence.g2084SupportedAfterSet)
					state.evidence.metadataSetSucceeded =
						operations.SetHdrMetadata(metadata.metadata);
			}
		}

		state.activation = Evaluate(metadata, state.evidence);
		if (state.activation.active)
		{
			state.phase = CarrierPhase::HDR10_ACTIVE;
			return state;
		}

		if (state.activation.clearHdrMetadata ||
			state.activation.restoreSdrColorSpace)
			Rollback(state, operations);
		return state;
	}
}
