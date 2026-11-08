// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "SimulatedMeter.h"

#include <algorithm>
#include <cmath>

namespace Calibration
{
	SimulatedMeter::SimulatedMeter(const SimulatedMeterParams& params) : p(params), rng(params.seed) {}

	MeterReading SimulatedMeter::Read(const Vec3& trueXyz)
	{
		std::normal_distribution<double> normal(0.0, 1.0);
		const Vec3 ideal = Ideal(trueXyz);
		MeterReading r;
		// Longer integration in the dark averages the floor down.
		r.seconds = p.timing.IntegrationSeconds(ideal[1]);
		const double floorScale = std::sqrt(p.timing.minSeconds / (std::max)(r.seconds - p.timing.overheadSeconds, 1e-3));
		for (int k = 0; k < 3; ++k)
			r.xyz[k] = ideal[k] * (1.0 + p.proportionalNoise * normal(rng)) + p.floorNits * floorScale * normal(rng);
		return r;
	}

	Mat3 SimulatedMeter::MatrixError(double amount, unsigned seed)
	{
		std::mt19937 g(seed);
		std::uniform_real_distribution<double> u(-amount, amount);
		Mat3 m;
		for (int i = 0; i < 3; ++i)
		{
			double sum = 0.0;
			for (int j = 0; j < 3; ++j)
			{
				m.m[i][j] = (i == j ? 1.0 : 0.0) + u(g);
				sum += m.m[i][j];
			}
			for (int j = 0; j < 3; ++j) m.m[i][j] /= sum;
		}
		return m;
	}
}
