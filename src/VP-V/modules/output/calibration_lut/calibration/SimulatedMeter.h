// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration (the module README, "Tests"): a
// simulated colorimeter. The reading is E * XYZ (E: the colorimeter's matrix
// error against the display's spectra, as an uncorrected CCSS/CCMX leaves
// it) with proportional noise per component and an absolute low-light noise
// floor; the integration time follows the luminance (MeterTimingModel).

#include "../pattern_lib/ColorMath.h"
#include "../pattern_lib/PatternPlan.h"

#include <random>

namespace Calibration
{
	struct SimulatedMeterParams
	{
		double proportionalNoise = 0.002;   // 1 sigma, relative per component
		double floorNits = 0.0004;          // 1 sigma, absolute per component
		Mat3 matrixError;                   // identity = a perfect spectral correction
		MeterTimingModel timing;
		unsigned seed = 1;
	};

	struct MeterReading
	{
		Vec3 xyz = { 0, 0, 0 };
		double seconds = 0.0;
	};

	class SimulatedMeter
	{
	public:
		explicit SimulatedMeter(const SimulatedMeterParams& params = SimulatedMeterParams());

		MeterReading Read(const Vec3& trueXyz);
		// The noiseless reading (the matrix error only).
		Vec3 Ideal(const Vec3& trueXyz) const { return p.matrixError * trueXyz; }
		const SimulatedMeterParams& Params() const { return p; }

		// A deterministic matrix error with entries perturbed by up to +-amount
		// (rows renormalised so that Y of an equal-energy white is kept).
		static Mat3 MatrixError(double amount, unsigned seed);

	private:
		SimulatedMeterParams p;
		std::mt19937 rng;
	};
}
