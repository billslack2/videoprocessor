// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "PatternPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace Calibration
{
	const char* ToString(PatchKind kind)
	{
		switch (kind)
		{
		case PatchKind::Black: return "black";
		case PatchKind::Greyscale: return "greyscale";
		case PatchKind::NearBlack: return "near_black";
		case PatchKind::Btb: return "btb";
		case PatchKind::Wtw: return "wtw";
		case PatchKind::Primary: return "primary";
		case PatchKind::Secondary: return "secondary";
		case PatchKind::Cube: return "cube";
		case PatchKind::DriftReference: return "drift_reference";
		case PatchKind::ColorChecker: return "colorchecker";
		case PatchKind::SkinTone: return "skin_tone";
		case PatchKind::VerifyNearBlack: return "verify_near_black";
		case PatchKind::VerifyGrey: return "verify_grey";
		case PatchKind::VerifyRandom: return "verify_random";
		}
		return "unknown";
	}

	double LimitedCodeToSignal(double code8) { return (code8 - 16.0) / 219.0; }
	double SignalToLimitedCode(double signal) { return 16.0 + 219.0 * signal; }

	namespace
	{
		Patch Make(double r, double g, double b, PatchKind kind, PatchSpace space, const std::string& label,
			bool usedForFit = true)
		{
			Patch p;
			p.rgb[0] = r; p.rgb[1] = g; p.rgb[2] = b;
			p.kind = kind;
			p.space = space;
			p.label = label;
			p.usedForFit = usedForFit;
			return p;
		}

		std::string Percent(const char* prefix, double percent)
		{
			char text[64];
			std::snprintf(text, sizeof(text), "%s %.4g%%", prefix, percent);
			return text;
		}

		bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
	}

	std::vector<Patch> GreyscalePatches(const GreyscaleOptions& options)
	{
		std::vector<Patch> result;
		std::vector<double> levels;
		const double step = (std::max)(0.1, options.stepPercent);
		for (double p = 0.0; p <= 100.0 + 1e-9; p += step)
			levels.push_back((std::min)(p, 100.0));
		if (!Near(levels.back(), 100.0)) levels.push_back(100.0);
		const double fine = (std::max)(0.05, options.nearBlackStepPercent);
		for (double p = fine; p < options.nearBlackTopPercent - 1e-9; p += fine)
			levels.push_back(p);
		std::sort(levels.begin(), levels.end());
		levels.erase(std::unique(levels.begin(), levels.end(), Near), levels.end());

		for (const int code : options.btbCodes)
		{
			const double s = LimitedCodeToSignal(code);
			char label[32];
			std::snprintf(label, sizeof(label), "BTB code %d", code);
			result.push_back(Make(s, s, s, PatchKind::Btb, PatchSpace::Device, label));
		}
		for (const double p : levels)
		{
			const double s = p / 100.0;
			PatchKind kind = PatchKind::Greyscale;
			if (Near(p, 0.0)) kind = PatchKind::Black;
			else if (p < options.nearBlackTopPercent - 1e-9 && std::fmod(p + 1e-9, step) > 2e-9) kind = PatchKind::NearBlack;
			result.push_back(Make(s, s, s, kind, PatchSpace::Device, Percent("grey", p)));
		}
		for (const int code : options.wtwCodes)
		{
			const double s = LimitedCodeToSignal(code);
			char label[32];
			std::snprintf(label, sizeof(label), "WTW code %d", code);
			result.push_back(Make(s, s, s, PatchKind::Wtw, PatchSpace::Device, label));
		}
		return result;
	}

	std::vector<Patch> PrimarySecondaryPatches(const std::vector<double>& levelsPercent)
	{
		static const struct { const char* name; double r, g, b; bool secondary; } colours[] = {
			{ "red", 1, 0, 0, false }, { "green", 0, 1, 0, false }, { "blue", 0, 0, 1, false },
			{ "cyan", 0, 1, 1, true }, { "magenta", 1, 0, 1, true }, { "yellow", 1, 1, 0, true },
		};
		std::vector<Patch> result;
		for (const auto& c : colours)
			for (const double p : levelsPercent)
			{
				const double s = p / 100.0;
				result.push_back(Make(c.r * s, c.g * s, c.b * s, c.secondary ? PatchKind::Secondary : PatchKind::Primary,
					PatchSpace::Device, Percent(c.name, p)));
			}
		return result;
	}

	std::vector<Patch> CubePatches(int n)
	{
		std::vector<Patch> result;
		if (n < 2) return result;
		result.reserve(static_cast<size_t>(n) * n * n);
		for (int b = 0; b < n; ++b)
			for (int g = 0; g < n; ++g)
				for (int r = 0; r < n; ++r)
				{
					char label[48];
					std::snprintf(label, sizeof(label), "cube%d %d,%d,%d", n, r, g, b);
					result.push_back(Make(static_cast<double>(r) / (n - 1), static_cast<double>(g) / (n - 1),
						static_cast<double>(b) / (n - 1), PatchKind::Cube, PatchSpace::Device, label));
				}
		return result;
	}

	std::vector<Patch> ColorChecker24()
	{
		// The commonly published sRGB (D65) 8-bit renderings of the 24 patches.
		static const struct { const char* name; int r, g, b; } cc[] = {
			{ "dark skin", 115, 82, 68 }, { "light skin", 194, 150, 130 }, { "blue sky", 98, 122, 157 },
			{ "foliage", 87, 108, 67 }, { "blue flower", 133, 128, 177 }, { "bluish green", 103, 189, 170 },
			{ "orange", 214, 126, 44 }, { "purplish blue", 80, 91, 166 }, { "moderate red", 193, 90, 99 },
			{ "purple", 94, 60, 108 }, { "yellow green", 157, 188, 64 }, { "orange yellow", 224, 163, 46 },
			{ "blue", 56, 61, 150 }, { "green", 70, 148, 73 }, { "red", 175, 54, 60 },
			{ "yellow", 231, 199, 31 }, { "magenta", 187, 86, 149 }, { "cyan", 8, 133, 161 },
			{ "white 9.5", 243, 243, 242 }, { "neutral 8", 200, 200, 200 }, { "neutral 6.5", 160, 160, 160 },
			{ "neutral 5", 122, 122, 121 }, { "neutral 3.5", 85, 85, 85 }, { "black 2", 52, 52, 52 },
		};
		std::vector<Patch> result;
		for (const auto& c : cc)
			result.push_back(Make(c.r / 255.0, c.g / 255.0, c.b / 255.0, PatchKind::ColorChecker,
				PatchSpace::TargetSignal, std::string("CC ") + c.name, false));
		return result;
	}

	std::vector<Patch> VerificationPatches(int randomCount, unsigned seed, int avoidCubeN)
	{
		std::vector<Patch> result = ColorChecker24();
		// A spread of skin tones from light to dark.
		static const struct { const char* name; int r, g, b; } skin[] = {
			{ "skin 1", 255, 224, 196 }, { "skin 2", 241, 194, 167 }, { "skin 3", 224, 172, 138 },
			{ "skin 4", 198, 140, 104 }, { "skin 5", 168, 113, 79 }, { "skin 6", 141, 85, 54 },
			{ "skin 7", 105, 62, 40 }, { "skin 8", 72, 44, 30 },
		};
		for (const auto& s : skin)
			result.push_back(Make(s.r / 255.0, s.g / 255.0, s.b / 255.0, PatchKind::SkinTone,
				PatchSpace::TargetSignal, s.name, false));
		for (const double p : { 1.0, 2.0, 3.0, 5.0, 7.5 })
			result.push_back(Make(p / 100, p / 100, p / 100, PatchKind::VerifyNearBlack, PatchSpace::TargetSignal,
				Percent("verify grey", p), false));
		for (const double p : { 0.0, 25.0, 50.0, 75.0, 100.0 })
			result.push_back(Make(p / 100, p / 100, p / 100, PatchKind::VerifyGrey, PatchSpace::TargetSignal,
				Percent("verify grey", p), false));

		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> uniform(0.0, 1.0);
		int made = 0;
		while (made < randomCount)
		{
			double v[3] = { uniform(rng), uniform(rng), uniform(rng) };
			if (avoidCubeN >= 2)
			{
				// At least a third of a lattice step from every fit cube node.
				const double step = 1.0 / (avoidCubeN - 1);
				bool onLattice = true;
				for (int k = 0; k < 3; ++k)
				{
					const double offset = std::fabs(v[k] / step - std::floor(v[k] / step + 0.5));
					if (offset > 1.0 / 3.0) onLattice = false;
				}
				if (onLattice) continue;
			}
			char label[32];
			std::snprintf(label, sizeof(label), "random %d", made + 1);
			result.push_back(Make(v[0], v[1], v[2], PatchKind::VerifyRandom, PatchSpace::TargetSignal, label, false));
			++made;
		}
		return result;
	}

	std::vector<Patch> InsertDriftReferences(const std::vector<Patch>& patches, int interval)
	{
		std::vector<Patch> result;
		const PatchSpace space = patches.empty() ? PatchSpace::Device : patches.front().space;
		const Patch reference = Make(1, 1, 1, PatchKind::DriftReference, space, "drift white", false);
		result.push_back(reference);
		int since = 0;
		for (const Patch& p : patches)
		{
			result.push_back(p);
			if (interval > 0 && ++since >= interval)
			{
				result.push_back(reference);
				since = 0;
			}
		}
		if (since != 0) result.push_back(reference);
		return result;
	}

	int CubeSizeFor(PlanKind kind)
	{
		switch (kind)
		{
		case PlanKind::Cube5: return 5;
		case PlanKind::Standard: return 9;
		case PlanKind::Cube13: return 13;
		case PlanKind::Thorough: return 17;
		default: return 0;
		}
	}

	std::vector<Patch> BuildPlan(PlanKind kind, int driftInterval)
	{
		std::vector<Patch> plan;
		if (kind == PlanKind::Verify)
			return InsertDriftReferences(VerificationPatches(), driftInterval);
		const std::vector<Patch> grey = GreyscalePatches();
		const std::vector<Patch> prim = PrimarySecondaryPatches();
		plan.insert(plan.end(), grey.begin(), grey.end());
		plan.insert(plan.end(), prim.begin(), prim.end());
		const int n = CubeSizeFor(kind);
		if (n >= 2)
		{
			const std::vector<Patch> cube = CubePatches(n);
			plan.insert(plan.end(), cube.begin(), cube.end());
		}
		return InsertDriftReferences(plan, driftInterval);
	}

	double MeterTimingModel::IntegrationSeconds(double luminanceNits) const
	{
		const double y = (std::max)(luminanceNits, floorNits);
		const double t = minSeconds + countNitSeconds / y;
		return (std::min)((std::max)(t, minSeconds), maxSeconds) + overheadSeconds;
	}

	double LuminanceGuess::Luminance(const double rgb[3]) const
	{
		double y = 0.0;
		for (int k = 0; k < 3; ++k)
		{
			const double s = (std::max)(0.0, rgb[k]);
			y += weights[k] * std::pow(s, gamma);
		}
		return blackNits + (peakNits - blackNits) * y;
	}

	PlanEstimate EstimateTime(const std::vector<Patch>& patches, double settleSeconds,
		const MeterTimingModel& meter, const LuminanceGuess& display)
	{
		PlanEstimate e;
		e.patches = static_cast<int>(patches.size());
		for (const Patch& p : patches)
		{
			e.settleSeconds += (std::max)(0.0, settleSeconds);
			e.integrationSeconds += meter.IntegrationSeconds(display.Luminance(p.rgb));
		}
		return e;
	}
}
