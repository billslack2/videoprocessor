// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "SimulatedDisplay.h"

#include <algorithm>
#include <cmath>

namespace Calibration
{
	SimulatedDisplay::SimulatedDisplay(const SimulatedDisplayParams& params) : p(params)
	{
		const Mat3 rgb = RgbToXyz(p.native);
		for (int c = 0; c < 3; ++c) primaries.SetColumn(c, Scale(rgb.Column(c), p.peakNits));
		blackXyz = XyToXyz(p.blackXy, p.blackNits);
		cntr = p.cntrReference;
		lpw = p.lpwMax;
	}

	void SimulatedDisplay::SetLpw(int value)
	{
		if (value != lpw) fromAbove = value < lpw;
		lpw = value;
	}

	double SimulatedDisplay::Gain(int value) const
	{
		const int c = (std::min)(value, p.gainSaturatesAt);
		return p.gainAtReference * (1.0 + p.gainPerStep * (c - p.cntrReference));
	}

	double SimulatedDisplay::RelativeFlux(int level, bool above) const
	{
		const double x = (std::min)((std::max)(static_cast<double>(level) / p.lpwMax, 0.0), 1.0);
		double f = std::pow(x, p.fluxExponent);
		if (above) f *= 1.0 + p.fluxHysteresis * (1.0 - x);
		return f;
	}

	double SimulatedDisplay::CodeToSignal(double d) const
	{
		const double scale = std::ldexp(1.0, p.linkBits - 8);
		double code = 16.0 * scale + 219.0 * scale * d;
		code = (std::min)((std::max)(code, 1.0 * scale), 254.0 * scale);
		if (!p.sigmaDelta) code = std::floor(code + 0.5);
		double code8 = code / scale;
		code8 = (std::max)(code8, static_cast<double>(p.btbClipCode));
		code8 = (std::min)(code8, static_cast<double>(p.wtwClipCode));
		return (code8 - 16.0) / 219.0 + p.blackLift;
	}

	double SimulatedDisplay::Eotf(int c, double e) const
	{
		const double g = p.gamma[c], t = p.toe;
		const double norm = std::pow(1.0 + t, g) - std::pow(t, g);
		return (std::pow((std::max)(e + t, 0.0), g) - std::pow(t, g)) / norm;
	}

	Vec3 SimulatedDisplay::Emit(const Vec3& device) const
	{
		Vec3 e = { CodeToSignal(device[0]), CodeToSignal(device[1]), CodeToSignal(device[2]) };
		const double x = p.crosstalk;
		const Vec3 mixed = { (1 - 2 * x) * e[0] + x * e[1] + x * e[2], x * e[0] + (1 - 2 * x) * e[1] + x * e[2],
			x * e[0] + x * e[1] + (1 - 2 * x) * e[2] };
		const double gain = Gain(cntr), reference = Gain(p.cntrReference);
		Vec3 light;
		for (int c = 0; c < 3; ++c)
			light[c] = (std::min)(Eotf(c, mixed[c]) * gain, 1.0) / reference;
		const double flux = RelativeFlux(lpw, fromAbove);
		return Scale(Add(blackXyz, primaries * light), flux);
	}

	Vec3 SimulatedDisplay::EmitCode8(int r, int g, int b) const
	{
		return Emit({ (r - 16.0) / 219.0, (g - 16.0) / 219.0, (b - 16.0) / 219.0 });
	}

	std::vector<TimedSample> SimulatedDisplay::SampleLedStep(double from, double to, double command, double start,
		double duration, double rateHz, double proportionalNoise, double floorNoise, std::mt19937& rng) const
	{
		std::vector<TimedSample> out;
		if (!(rateHz > 0.0) || !(duration > 0.0)) return out;
		std::normal_distribution<double> normal(0.0, 1.0);
		const double tau = (std::max)(p.ledTauSeconds, 1e-6);
		const double period = 1.0 / rateHz;
		const double sub = (std::min)(period / 8.0, tau / 50.0);
		auto input = [&](double t)
		{
			const double s = t - command - p.ledDeadTimeSeconds;
			if (s <= 0.0) return from;
			if (p.ledRampSeconds > 0.0 && s < p.ledRampSeconds) return from + (to - from) * s / p.ledRampSeconds;
			return to;
		};
		double y = from;
		double t = start;
		const size_t count = static_cast<size_t>(std::floor(duration * rateHz));
		for (size_t i = 0; i <= count; ++i)
		{
			const double target = start + i * period;
			while (t < target - 1e-12)
			{
				const double h = (std::min)(sub, target - t);
				const double u = input(t + 0.5 * h);
				y = u + (y - u) * std::exp(-h / tau);
				t += h;
			}
			TimedSample s;
			s.seconds = target;
			s.value = y * (1.0 + proportionalNoise * normal(rng)) + floorNoise * normal(rng);
			out.push_back(s);
		}
		return out;
	}
}
