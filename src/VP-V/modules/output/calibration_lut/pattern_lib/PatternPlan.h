// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration (the module README, "Measurements",
// "Verify"): the patch sets a run measures and how long they take.
//
// Pure: no renderer or meter. A patch's rgb is a normalised signal triplet:
//  - PatchSpace::Device: the display's input signal with the LUT bypassed,
//    0 = code 16 and 1 = code 235 on a limited link; below 0 / above 1 are
//    BTB / WTW codes (the ExtendedRange restore keeps them).
//  - PatchSpace::TargetSignal: the calibration LUT's input (full-domain RGB in
//    the slot's gamut and transfer); verification patches go through the LUT.
//
// Time = settle + meter integration, where the integration time follows the
// patch luminance (a colorimeter counts longer in the dark).

#include <string>
#include <vector>

namespace Calibration
{
	enum class PatchSpace
	{
		Device,
		TargetSignal,
	};

	enum class PatchKind
	{
		Black,
		Greyscale,
		NearBlack,
		Btb,
		Wtw,
		Primary,
		Secondary,
		Cube,
		DriftReference,
		ColorChecker,
		SkinTone,
		VerifyNearBlack,
		VerifyGrey,
		VerifyRandom,
	};

	const char* ToString(PatchKind kind);

	struct Patch
	{
		double rgb[3] = { 0.0, 0.0, 0.0 };
		PatchKind kind = PatchKind::Greyscale;
		PatchSpace space = PatchSpace::Device;
		std::string label;
		// False for drift references and verification patches.
		bool usedForFit = true;
	};

	// Code <-> normalised device signal on an 8-bit limited link (16..235).
	double LimitedCodeToSignal(double code8);
	double SignalToLimitedCode(double signal);

	struct GreyscaleOptions
	{
		double stepPercent = 5.0;            // the main ramp
		double nearBlackStepPercent = 1.0;   // below nearBlackTopPercent
		double nearBlackTopPercent = 10.0;
		std::vector<int> btbCodes = { 4, 8, 12, 15 };
		std::vector<int> wtwCodes = { 240, 254 };
	};

	std::vector<Patch> GreyscalePatches(const GreyscaleOptions& options = GreyscaleOptions());
	// R, G, B, C, M, Y at each level (percent).
	std::vector<Patch> PrimarySecondaryPatches(const std::vector<double>& levelsPercent = { 25.0, 50.0, 75.0, 100.0 });
	// n x n x n device cube (n >= 2), R fastest.
	std::vector<Patch> CubePatches(int n);
	// The verification set (target signal): ColorChecker 24, skin tones,
	// near-black and greys, and `randomCount` deterministic random colours
	// that avoid a fit cube's lattice (`avoidCubeN`, 0 = none).
	std::vector<Patch> VerificationPatches(int randomCount = 20, unsigned seed = 20261017u, int avoidCubeN = 9);
	// The ColorChecker 24 patches only (sRGB-encoded reference colours).
	std::vector<Patch> ColorChecker24();

	// A drift reference (device white) inserted after every `interval` patches.
	std::vector<Patch> InsertDriftReferences(const std::vector<Patch>& patches, int interval = 50);

	enum class PlanKind
	{
		Quick,     // greyscale + primaries/secondaries (matrix)
		Cube5,
		Standard,  // 9^3
		Cube13,
		Thorough,  // 17^3
		Verify,
	};

	int CubeSizeFor(PlanKind kind);  // 0 = no cube
	std::vector<Patch> BuildPlan(PlanKind kind, int driftInterval = 50);

	// A colorimeter's integration time against luminance:
	//   t = clamp(minSeconds + countNitSeconds / max(Y, floorNits), .., maxSeconds)
	struct MeterTimingModel
	{
		double minSeconds = 0.6;
		double maxSeconds = 8.0;
		double countNitSeconds = 1.5;   // nit-seconds for one adequate count
		double floorNits = 1e-4;
		double overheadSeconds = 0.15;  // per reading (transfer, parse)

		double IntegrationSeconds(double luminanceNits) const;
	};

	// A first guess at the display before it is measured (time estimates only).
	struct LuminanceGuess
	{
		double peakNits = 50.0;
		double blackNits = 0.05;
		double gamma = 2.2;
		double weights[3] = { 0.2126, 0.7152, 0.0722 };

		double Luminance(const double deviceRgb[3]) const;
	};

	struct PlanEstimate
	{
		int patches = 0;
		double settleSeconds = 0.0;
		double integrationSeconds = 0.0;
		double TotalSeconds() const { return settleSeconds + integrationSeconds; }
		double TotalMinutes() const { return TotalSeconds() / 60.0; }
	};

	// settleSeconds: the display's settle after a pattern change (measured
	// display latency plus margin); never a fixed frame count.
	PlanEstimate EstimateTime(const std::vector<Patch>& patches, double settleSeconds,
		const MeterTimingModel& meter = MeterTimingModel(), const LuminanceGuess& display = LuminanceGuess());
}
