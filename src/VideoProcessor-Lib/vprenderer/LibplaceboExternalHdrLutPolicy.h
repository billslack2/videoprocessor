#pragma once

#include <string>

namespace LibplaceboExternalHdrLut
{
	enum class ToneMappingMode
	{
		PIXEL_SHADERS,
		PASS_THROUGH,
		EXTERNAL_3DLUT
	};

	enum class Primaries
	{
		UNKNOWN,
		BT709,
		P3_D65,
		BT2020
	};

	enum class Slot
	{
		NONE,
		BT709,
		P3_D65,
		BT2020
	};

	enum class EffectiveMode
	{
		PIXEL_SHADERS,
		PASS_THROUGH,
		EXTERNAL_3DLUT
	};

	enum class MetadataOwner
	{
		INTERNAL_PIPELINE,
		SOURCE_PASSTHROUGH,
		EXTERNAL_LUT
	};

	enum class FinalCalibrationStage
	{
		APPLY,
		MASK
	};

	struct AvailableSlots
	{
		bool bt709 = false;
		bool p3D65 = false;
		bool bt2020 = false;
	};

	struct Selection
	{
		EffectiveMode effectiveMode = EffectiveMode::PIXEL_SHADERS;
		MetadataOwner metadataOwner = MetadataOwner::INTERNAL_PIPELINE;
		bool useExternalLut = false;
		Slot slot = Slot::NONE;
		bool requiresExplicitPrimariesTransform = false;
		const char* reason = "internal pixel shaders selected";
		FinalCalibrationStage finalCalibrationStage =
			FinalCalibrationStage::APPLY;
	};

	inline std::string NormalizeToken(std::string value)
	{
		for (char& character : value)
			if (character >= 'A' && character <= 'Z')
				character = static_cast<char>(character - 'A' + 'a');
		return value;
	}

	inline ToneMappingMode ParseToneMappingMode(const std::string& value)
	{
		const std::string normalized = NormalizeToken(value);
		// HDR passthrough is intentionally disabled for this beta slice. Preserve
		// safe playback for legacy configuration by resolving it to VP's internal
		// HDR-to-SDR tone mapper.
		if (normalized == "passthrough") return ToneMappingMode::PIXEL_SHADERS;
		if (normalized == "external_3dlut") return ToneMappingMode::EXTERNAL_3DLUT;
		// Omitted and unrecognized values preserve the v1.3.004-beta behavior.
		return ToneMappingMode::PIXEL_SHADERS;
	}

	inline Primaries ParsePrimaries(const std::string& value)
	{
		const std::string normalized = NormalizeToken(value);
		if (normalized == "bt709" || normalized == "rec709") return Primaries::BT709;
		if (normalized == "p3_d65") return Primaries::P3_D65;
		if (normalized == "bt2020") return Primaries::BT2020;
		return Primaries::UNKNOWN;
	}

	inline bool IsValidMetadataPeakNits(double peakNits)
	{
		return peakNits >= 1.0 && peakNits <= 10000.0;
	}

	inline Selection Select(ToneMappingMode mode, bool inputIsPq,
		Primaries sourcePrimaries, const AvailableSlots& slots)
	{
		if (mode == ToneMappingMode::PIXEL_SHADERS)
			return { EffectiveMode::PIXEL_SHADERS,
				MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
				"internal pixel shaders selected" };
		if (mode == ToneMappingMode::PASS_THROUGH)
			return { EffectiveMode::PIXEL_SHADERS,
				MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
				"HDR passthrough is disabled; internal pixel shaders selected" };
		if (!inputIsPq)
			return { EffectiveMode::PIXEL_SHADERS,
				MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
				"external 3D LUT v1 accepts HDR10/PQ input only" };

		auto selected = [](Slot slot, bool transformed) -> Selection
		{
			return { EffectiveMode::EXTERNAL_3DLUT,
				MetadataOwner::EXTERNAL_LUT, true, slot, transformed,
				transformed ? "fallback slot selected with explicit primaries transform" :
				"exact input-gamut slot selected", FinalCalibrationStage::MASK };
		};

		switch (sourcePrimaries)
		{
		case Primaries::BT709:
			// The reverse wide-gamut fallback order is not authoritative in the
			// madVR evidence, so v1 deliberately accepts only the exact slot.
			if (slots.bt709) return selected(Slot::BT709, false);
			break;
		case Primaries::P3_D65:
			if (slots.p3D65) return selected(Slot::P3_D65, false);
			if (slots.bt2020) return selected(Slot::BT2020, true);
			if (slots.bt709) return selected(Slot::BT709, true);
			break;
		case Primaries::BT2020:
			if (slots.bt2020) return selected(Slot::BT2020, false);
			if (slots.p3D65) return selected(Slot::P3_D65, true);
			if (slots.bt709) return selected(Slot::BT709, true);
			break;
		default:
			return { EffectiveMode::PIXEL_SHADERS,
				MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
				"input primaries are unknown" };
		}
		return { EffectiveMode::PIXEL_SHADERS,
			MetadataOwner::INTERNAL_PIPELINE, false, Slot::NONE, false,
			"no compatible external 3D LUT slot is loaded" };
	}
}
