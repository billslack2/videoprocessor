// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "MeterCatalog.h"

#include <algorithm>
#include <cctype>

namespace Calibration
{
	const std::vector<MeterModel>& MeterModels()
	{
		// Order matters for MatchInstrument: the more specific names first
		// (i1 Pro 3 before i1 Pro 2 before i1 Pro; Spyder X2 before Spyder X).
		static const std::vector<MeterModel> models = {
			{ "i1d3", "Calibrite Display Pro / Plus (HL), i1Display Pro / Plus, ColorChecker Display", MeterKind::Colorimeter,
				false, true, 0.002, { "i1 displaypro", "i1display pro", "i1d3", "colorchecker display", "calibrite display",
				"display pro", "display plus" } },
			{ "colormunki_display", "ColorMunki Display, i1Display Studio", MeterKind::Colorimeter, false, true, 0.005,
				{ "colormunki display", "colormunki smile", "i1display studio" } },
			{ "i1d2", "i1Display 2 / LT, Huey Pro", MeterKind::Colorimeter, false, false, 0.05,
				{ "i1 display 2", "i1 display lt", "i1 display", "huey" } },
			{ "spyderx2", "Datacolor Spyder X2", MeterKind::Colorimeter, false, true, 0.01, { "spyderx2", "spyder x2" } },
			{ "spyderx", "Datacolor Spyder X", MeterKind::Colorimeter, false, true, 0.02, { "spyderx", "spyder x" } },
			{ "spyder5", "Datacolor Spyder 5", MeterKind::Colorimeter, false, true, 0.02, { "spyder5", "spyder 5" } },
			{ "spyder4", "Datacolor Spyder 4", MeterKind::Colorimeter, false, true, 0.02, { "spyder4", "spyder 4" } },
			{ "dtp94", "X-Rite DTP94", MeterKind::Colorimeter, false, false, 0.02, { "dtp94", "optix" } },
			{ "i1pro3", "X-Rite i1Pro 3", MeterKind::Spectro, true, false, 0.2, { "i1 pro 3", "i1pro3", "i1pro 3" } },
			{ "i1pro2", "X-Rite i1Pro 2", MeterKind::Spectro, true, false, 0.2, { "i1 pro 2", "i1pro2", "i1pro 2" } },
			{ "i1pro", "X-Rite i1Pro", MeterKind::Spectro, true, false, 0.3, { "i1 pro", "i1pro" } },
			{ "colormunki_photo", "X-Rite ColorMunki Photo / Design", MeterKind::Spectro, true, false, 0.5,
				{ "colormunki" } },
			{ "jeti", "JETI specbos / spectraval", MeterKind::Spectro, false, false, 0.05, { "jeti", "specbos", "spectraval" } },
			{ "klein_k10", "Klein K10-A", MeterKind::Colorimeter, false, false, 0.0005, { "klein", "k10" } },
		};
		return models;
	}

	namespace
	{
		std::string Lower(const std::string& text)
		{
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}

	const MeterModel* FindMeterModel(const std::string& id)
	{
		const std::string wanted = Lower(id);
		for (const MeterModel& m : MeterModels())
			if (wanted == m.id) return &m;
		return nullptr;
	}

	const MeterModel* MatchInstrument(const std::string& description)
	{
		const std::string text = Lower(description);
		if (text.empty()) return nullptr;
		// ColorMunki Display is an i1d3 family member but has its own entry;
		// check it before the generic "colormunki" spectro match.
		for (const MeterModel& m : MeterModels())
		{
			if (std::string(m.id) == "colormunki_photo") continue;
			for (const char* match : m.matches)
				if (text.find(match) != std::string::npos)
				{
					// "i1 displaypro, colormunki display": the i1d3 entry comes first.
					return &m;
				}
		}
		return FindMeterModel("colormunki_photo") && text.find("colormunki") != std::string::npos ?
			FindMeterModel("colormunki_photo") : nullptr;
	}

	const MeterModel* ResolveMeterModel(const std::string& configuredId, const std::string& description)
	{
		const std::string id = Lower(configuredId);
		if (!id.empty() && id != "auto" && id != "other")
			if (const MeterModel* m = FindMeterModel(id)) return m;
		return MatchInstrument(description);
	}

	double GenericFloorNits() { return 0.05; }
}
