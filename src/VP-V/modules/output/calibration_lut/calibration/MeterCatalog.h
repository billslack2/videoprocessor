// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: the meters VP drives through ArgyllCMS's spotread
// (the module README, "Meters"). VP never links Argyll
// (AGPLv3): spotread runs as a separate process. Any instrument Argyll
// supports works; this catalogue only tunes VP's behaviour per family
// (whether spotread asks for a sensor calibration, whether a CCSS spectral
// sample set or only a CCMX matrix applies, and how dark the meter reads
// usefully, which steers the near-black strategy).
//
// Low-light floors are approximate, typical figures for a stable reading
// within a few percent; they only decide when VP averages more readings and
// when it marks a patch "below the meter's floor".

#include <string>
#include <vector>

namespace Calibration
{
	enum class MeterKind
	{
		Colorimeter,
		Spectro,
	};

	struct MeterModel
	{
		const char* id;               // [calibration] meter_model
		const char* label;            // shown in Config
		MeterKind kind;
		bool needsSensorCalibration;  // spotread asks for a dark / white-tile calibration
		bool acceptsCcss;             // -X file.ccss (else only -X file.ccmx)
		double floorNits;             // typical usable low-light floor (cd/m2)
		// Lower-case substrings of spotread's instrument description (its -c list).
		std::vector<const char*> matches;
	};

	const std::vector<MeterModel>& MeterModels();
	// nullptr when the id is unknown ("auto" and "other" included).
	const MeterModel* FindMeterModel(const std::string& id);
	// The catalogue entry matching spotread's description of a connected
	// instrument ("X-Rite i1 DisplayPro, ColorMunki Display"); nullptr = unknown
	// (an Argyll instrument VP has no tuning for: it is still driven).
	const MeterModel* MatchInstrument(const std::string& spotreadDescription);
	// The model to use: the configured id unless "auto" / "other", else the
	// connected instrument's entry; nullptr = generic behaviour.
	const MeterModel* ResolveMeterModel(const std::string& configuredId, const std::string& spotreadDescription);
	// What an unknown instrument is treated as.
	double GenericFloorNits();
}
