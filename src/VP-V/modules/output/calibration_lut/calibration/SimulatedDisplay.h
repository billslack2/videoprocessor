// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration (the module README, "Tests"): a
// simulated display for the CPU tests and for dry runs of the later session.
//
// Signal path, per channel, for a device triplet d (0 = code 16, 1 = 235):
//   link:      code = black + span * d on an N-bit limited link (N from the
//              parameters), clamped to the legal codes 1..254 (scaled);
//              sigma-delta on: the meter sees the temporal average (no
//              rounding); off: rounded to whole codes
//   display:   codes below btbClipCode show as that code (BTB clipped),
//              above wtwClipCode as that code (WTW clipped)
//   crosstalk: e' = C e (rows sum to 1, so greys are unaffected)
//   EOTF:      toe-offset power per channel of e + blackLift (analytic
//              above 1; a lift makes BTB codes visible down to true black)
//   DMD:       drive = L * gain(CNTR), clipped at 1 (the DMD's maximum);
//              gain rises per CNTR step and stops at gainSaturatesAt
//   light:     XYZ = flux(LPW1) * (K + sum_c drive_c / gain(ref) * P_c)
// with narrow-band primaries P_c and a non-D65 native white. LPW1 -> flux
// has up/down hysteresis; the LED moves after a dead time, optionally ramps
// internally, then follows a first-order lag.

#include "../pattern_lib/ColorMath.h"
#include "LedCharacterize.h"

#include <random>
#include <vector>

namespace Calibration
{
	struct SimulatedDisplayParams
	{
		Primaries native = { { 0.700, 0.298 }, { 0.190, 0.760 }, { 0.137, 0.052 }, { 0.300, 0.322 } };
		double peakNits = 48.0;                  // native white, e = 1, reference CNTR, full LED
		double gamma[3] = { 2.30, 2.20, 2.36 };
		double toe = 0.008;
		double blackNits = 0.035;
		Chromaticity blackXy = { 0.280, 0.290 };
		double crosstalk = 0.02;
		int linkBits = 8;
		bool sigmaDelta = true;
		int btbClipCode = 16;                    // 8-bit code units
		int wtwClipCode = 254;
		double blackLift = 0.0;                  // display brightness raised: code 16 shows above true black
		int cntrReference = 50;
		double gainAtReference = 0.75;
		double gainPerStep = 0.03;               // relative, per CNTR step
		int gainSaturatesAt = 70;
		int lpwMax = 255;
		double fluxExponent = 0.85;
		double fluxHysteresis = 0.012;           // brighter when approached from above, at low levels
		double ledDeadTimeSeconds = 0.080;
		double ledTauSeconds = 0.120;
		double ledRampSeconds = 0.0;
	};

	class SimulatedDisplay
	{
	public:
		explicit SimulatedDisplay(const SimulatedDisplayParams& params = SimulatedDisplayParams());

		const SimulatedDisplayParams& Params() const { return p; }
		void SetCntr(int value) { cntr = value; }
		int Cntr() const { return cntr; }
		// Steady state after a level change (the direction sets the hysteresis branch).
		void SetLpw(int lpw);
		int Lpw() const { return lpw; }

		// Steady-state XYZ (cd/m2) for a device triplet at the current CNTR / LPW1.
		Vec3 Emit(const Vec3& device) const;
		// What a limited 8-bit code triplet shows.
		Vec3 EmitCode8(int r, int g, int b) const;

		double RelativeFlux(int lpw, bool fromAbove) const;
		double Gain(int cntr) const;

		// The LED's relative light (flux) around a commanded change at
		// commandSeconds, sampled at rateHz from startSeconds for durationSeconds.
		// Noise: proportional and absolute (Gaussian), from rng.
		std::vector<TimedSample> SampleLedStep(double fromFlux, double toFlux, double commandSeconds,
			double startSeconds, double durationSeconds, double rateHz, double proportionalNoise, double floorNoise,
			std::mt19937& rng) const;

	private:
		double Eotf(int channel, double e) const;
		double CodeToSignal(double d) const;

		SimulatedDisplayParams p;
		Mat3 primaries;     // channel light -> XYZ (sums to the native white at peak)
		Vec3 blackXyz;
		int cntr = 50;
		int lpw = 255;
		bool fromAbove = false;
	};
}
