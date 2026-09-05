#pragma once

#include <string>

namespace LibplaceboCalibrationLut
{
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

	struct Selection
	{
		bool enabled = false;
		Slot slot = Slot::NONE;
		const char* reason = "display calibration LUT disabled";
	};

	enum class ReloadFailureAction
	{
		DETACH,
		RETAIN_LAST_KNOWN_GOOD
	};

	inline ReloadFailureAction ResolveReloadFailure(
		bool sameContract, bool preserveSameContractOnFailure)
	{
		return sameContract && preserveSameContractOnFailure
			? ReloadFailureAction::RETAIN_LAST_KNOWN_GOOD
			: ReloadFailureAction::DETACH;
	}

	inline std::string NormalizeToken(std::string value)
	{
		for (char& character : value)
			if (character >= 'A' && character <= 'Z')
				character = static_cast<char>(character - 'A' + 'a');
		return value;
	}

	inline Primaries ParsePrimaries(const std::string& value)
	{
		const std::string normalized = NormalizeToken(value);
		if (normalized == "bt709" || normalized == "rec709")
			return Primaries::BT709;
		if (normalized == "p3_d65" || normalized == "display_p3")
			return Primaries::P3_D65;
		if (normalized == "bt2020")
			return Primaries::BT2020;
		return Primaries::UNKNOWN;
	}

	// A calibration Cube belongs to the configured display/DTM target. Source
	// primaries never participate in this decision, and there is deliberately no
	// cross-gamut fallback: applying a Cube characterized for another target is
	// worse than falling back to the uncalibrated libplacebo result.
	inline Selection Select(bool enabled, Primaries targetPrimaries,
		bool bt709Available, bool p3D65Available, bool bt2020Available)
	{
		if (!enabled)
			return {};

		switch (targetPrimaries)
		{
		case Primaries::BT709:
			return bt709Available
				? Selection{ true, Slot::BT709, "BT.709 calibration target selected" }
				: Selection{ false, Slot::NONE, "BT.709 calibration LUT not configured" };
		case Primaries::P3_D65:
			return p3D65Available
				? Selection{ true, Slot::P3_D65, "P3-D65 calibration target selected" }
				: Selection{ false, Slot::NONE, "P3-D65 calibration LUT not configured" };
		case Primaries::BT2020:
			return bt2020Available
				? Selection{ true, Slot::BT2020, "BT.2020 calibration target selected" }
				: Selection{ false, Slot::NONE, "BT.2020 calibration LUT not configured" };
		default:
			return { false, Slot::NONE, "calibration target primaries unknown" };
		}
	}
}
