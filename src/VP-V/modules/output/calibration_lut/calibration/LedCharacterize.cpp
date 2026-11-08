// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>
#include "LedCharacterize.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

namespace Calibration
{
	namespace
	{
		double Median(std::vector<double> v)
		{
			if (v.empty()) return 0.0;
			std::sort(v.begin(), v.end());
			const size_t n = v.size();
			return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
		}

		std::vector<double> IsotonicUnweighted(const std::vector<double>& y)
		{
			struct Block { double sum; size_t count; };
			std::vector<Block> blocks;
			for (const double v : y)
			{
				blocks.push_back({ v, 1 });
				while (blocks.size() >= 2)
				{
					Block& b = blocks[blocks.size() - 1];
					Block& a = blocks[blocks.size() - 2];
					if (a.sum / a.count <= b.sum / b.count) break;
					a.sum += b.sum; a.count += b.count;
					blocks.pop_back();
				}
			}
			std::vector<double> out;
			for (const Block& b : blocks)
				for (size_t i = 0; i < b.count; ++i) out.push_back(b.sum / b.count);
			return out;
		}

		// Nelder-Mead minimisation (n <= 3 parameters).
		std::vector<double> NelderMead(const std::function<double(const std::vector<double>&)>& f,
			std::vector<double> start, const std::vector<double>& scale, int iterations)
		{
			const size_t n = start.size();
			std::vector<std::vector<double>> p(n + 1, start);
			for (size_t i = 0; i < n; ++i) p[i + 1][i] += scale[i];
			std::vector<double> v(n + 1);
			for (size_t i = 0; i <= n; ++i) v[i] = f(p[i]);
			for (int it = 0; it < iterations; ++it)
			{
				std::vector<size_t> order(n + 1);
				for (size_t i = 0; i <= n; ++i) order[i] = i;
				std::sort(order.begin(), order.end(), [&v](size_t a, size_t b) { return v[a] < v[b]; });
				std::vector<std::vector<double>> q(n + 1);
				std::vector<double> w(n + 1);
				for (size_t i = 0; i <= n; ++i) { q[i] = p[order[i]]; w[i] = v[order[i]]; }
				p = q; v = w;
				if (std::fabs(v[n] - v[0]) <= 1e-14 * (std::fabs(v[0]) + 1e-30)) break;
				std::vector<double> c(n, 0.0);
				for (size_t i = 0; i < n; ++i)
					for (size_t k = 0; k < n; ++k) c[k] += p[i][k] / n;
				auto along = [&](double t)
				{
					std::vector<double> r(n);
					for (size_t k = 0; k < n; ++k) r[k] = c[k] + t * (p[n][k] - c[k]);
					return r;
				};
				const std::vector<double> xr = along(-1.0);
				const double fr = f(xr);
				if (fr < v[0])
				{
					const std::vector<double> xe = along(-2.0);
					const double fe = f(xe);
					if (fe < fr) { p[n] = xe; v[n] = fe; } else { p[n] = xr; v[n] = fr; }
				}
				else if (fr < v[n - 1]) { p[n] = xr; v[n] = fr; }
				else
				{
					const std::vector<double> xc = fr < v[n] ? along(-0.5) : along(0.5);
					const double fc = f(xc);
					if (fc < (std::min)(fr, v[n])) { p[n] = xc; v[n] = fc; }
					else
						for (size_t i = 1; i <= n; ++i)
						{
							for (size_t k = 0; k < n; ++k) p[i][k] = p[0][k] + 0.5 * (p[i][k] - p[0][k]);
							v[i] = f(p[i]);
						}
				}
			}
			size_t best = 0;
			for (size_t i = 1; i <= n; ++i) if (v[i] < v[best]) best = i;
			return p[best];
		}

		struct Segment
		{
			bool found = false;
			double knee = 0.0;     // where the rising line meets the plateau
			double plateau = 0.0;
		};

		// Rising line then a flat plateau, by exhaustive breakpoint search;
		// found = false when one straight line explains the curve as well.
		Segment FitRiseThenFlat(const std::vector<double>& x, const std::vector<double>& y)
		{
			Segment s;
			const size_t n = x.size();
			if (n < 4) return s;
			auto line = [&](size_t from, size_t to, double& a, double& b)
			{
				double sx = 0, sy = 0, sxx = 0, sxy = 0;
				const double m = static_cast<double>(to - from);
				for (size_t i = from; i < to; ++i) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
				const double den = m * sxx - sx * sx;
				b = den != 0.0 ? (m * sxy - sx * sy) / den : 0.0;
				a = (sy - b * sx) / m;
				double sse = 0.0;
				for (size_t i = from; i < to; ++i) sse += (y[i] - a - b * x[i]) * (y[i] - a - b * x[i]);
				return sse;
			};
			double a0, b0;
			const double sseLine = line(0, n, a0, b0);
			double best = sseLine;
			size_t bestK = 0;
			double bestA = 0, bestB = 0, bestC = 0;
			for (size_t k = 2; k + 2 <= n; ++k)
			{
				double a, b;
				double sse = line(0, k, a, b);
				double c = 0.0;
				for (size_t i = k; i < n; ++i) c += y[i];
				c /= static_cast<double>(n - k);
				for (size_t i = k; i < n; ++i) sse += (y[i] - c) * (y[i] - c);
				if (b > 0.0 && sse < best) { best = sse; bestK = k; bestA = a; bestB = b; bestC = c; }
			}
			if (bestK == 0 || best > 0.5 * sseLine) return s;
			// The part after the knee must be flat, not a gentler rise.
			if (n - bestK >= 3)
			{
				double a, b;
				line(bestK, n, a, b);
				if (b > 0.25 * bestB) return s;
			}
			s.found = true;
			s.plateau = bestC;
			s.knee = (bestC - bestA) / bestB;
			s.knee = (std::min)((std::max)(s.knee, x[bestK - 1]), x[bestK]);
			return s;
		}
	}

	// -------------------------------------------------------------- flux

	std::string FluxFit::ConfigValue() const
	{
		std::string out;
		char text[64];
		for (size_t i = 0; i < table.size(); ++i)
		{
			std::snprintf(text, sizeof(text), "%s%d:%.6f", i ? ", " : "", static_cast<int>(table[i].first), table[i].second);
			out += text;
		}
		return out;
	}

	FluxFit FitFluxTable(const std::vector<FluxStep>& steps, const FluxFitOptions& options)
	{
		FluxFit fit;
		std::map<int, std::vector<double>> up, down;
		for (const FluxStep& s : steps)
		{
			if (s.samples.empty()) continue;
			const double t0 = s.samples.front().seconds, t1 = s.samples.back().seconds;
			const double from = t1 - (t1 - t0) * (std::min)((std::max)(options.settledFraction, 0.05), 1.0);
			std::vector<double> settled;
			for (const TimedSample& x : s.samples)
				if (x.seconds >= from) settled.push_back(x.value);
			(s.direction == StepDirection::Up ? up : down)[s.lpw].push_back(Median(settled));
		}
		std::map<int, FluxPoint> points;
		for (const auto& e : up) { FluxPoint& p = points[e.first]; p.lpw = e.first; p.haveUp = true; p.up = Median(e.second); }
		for (const auto& e : down) { FluxPoint& p = points[e.first]; p.lpw = e.first; p.haveDown = true; p.down = Median(e.second); }
		if (points.size() < 2) { fit.error = "need at least two LPW1 levels"; return fit; }

		const int reference = options.referenceLpw >= 0 ? options.referenceLpw : points.rbegin()->first;
		auto it = points.find(reference);
		if (it == points.end()) { fit.error = "the reference level was not measured"; return fit; }
		auto mean = [](const FluxPoint& p)
		{
			return p.haveUp && p.haveDown ? 0.5 * (p.up + p.down) : (p.haveUp ? p.up : p.down);
		};
		const double norm = mean(it->second);
		if (!(norm > 0.0)) { fit.error = "no light at the reference level"; return fit; }

		std::vector<double> raw;
		for (auto& e : points)
		{
			FluxPoint& p = e.second;
			p.up /= norm;
			p.down /= norm;
			p.flux = mean(p);
			raw.push_back(p.flux);
		}
		const std::vector<double> mono = IsotonicUnweighted(raw);
		size_t i = 0;
		double previous = 0.0;
		for (auto& e : points)
		{
			FluxPoint& p = e.second;
			double v = (std::max)(mono[i++], 1e-6);
			// Strictly rising after printing with six decimals.
			if (v <= previous + 1e-6) v = previous + 1e-6;
			v = std::floor(v * 1e6 + 0.5) / 1e6;
			if (v <= previous) v = previous + 1e-6;
			p.flux = (std::min)(v, 10.0);
			previous = p.flux;
			if (p.haveUp && p.haveDown && p.flux > 0.0)
			{
				p.hysteresis = (p.down - p.up) / p.flux;
				fit.maxHysteresis = (std::max)(fit.maxHysteresis, std::fabs(p.hysteresis));
			}
			fit.points.push_back(p);
			fit.table.push_back(std::make_pair(static_cast<double>(p.lpw), p.flux));
		}
		fit.ok = true;
		return fit;
	}

	// -------------------------------------------------------------- CNTR

	CntrFit FitCntrCurve(const std::vector<CntrSample>& samples, int whiteCode)
	{
		CntrFit fit;
		std::map<int, std::map<int, std::vector<double>>> byCode;
		for (const CntrSample& s : samples) byCode[s.code][s.cntr].push_back(s.luminance);
		if (byCode.empty()) { fit.error = "no samples"; return fit; }

		double usable = -1e9;
		int measuredMax = 0;
		bool whiteSeen = false;
		for (const auto& code : byCode)
		{
			std::vector<double> x, y;
			for (const auto& c : code.second)
			{
				x.push_back(c.first);
				y.push_back(Median(c.second));
				measuredMax = (std::max)(measuredMax, c.first);
			}
			if (x.size() < 4) continue;
			const Segment s = FitRiseThenFlat(x, y);
			const double knee = s.found ? s.knee : x.back();
			usable = (std::max)(usable, knee);
			if (code.first == whiteCode)
			{
				whiteSeen = true;
				fit.clipReached = s.found;
				fit.clipExact = knee;
				fit.plateau = s.found ? s.plateau : y.back();
			}
		}
		if (!whiteSeen) { fit.error = "the white code was not measured"; return fit; }
		if (usable < 0) { fit.error = "too few CNTR steps"; return fit; }
		fit.usableExact = usable;
		fit.cntrMaxUsable = (std::min)((std::max)(static_cast<int>(std::lround(usable)), 50), 100);
		fit.dmdClipCntr = fit.clipReached
			? (std::min)((std::max)(static_cast<int>(std::lround(fit.clipExact)), 50), 100) : -1;
		fit.ok = true;
		return fit;
	}

	// ----------------------------------------------------- step response

	double StepModel(double s, double deadTime, double tau, double ramp)
	{
		s -= deadTime;
		if (s <= 0.0) return 0.0;
		tau = (std::max)(tau, 1e-6);
		if (ramp <= 1e-9) return 1.0 - std::exp(-s / tau);
		if (s < ramp) return (s - tau + tau * std::exp(-s / tau)) / ramp;
		const double atEnd = (ramp - tau + tau * std::exp(-ramp / tau)) / ramp;
		return 1.0 - (1.0 - atEnd) * std::exp(-(s - ramp) / tau);
	}

	StepResponseFit FitStepResponse(const std::vector<TimedSample>& in, double commandSeconds,
		const StepResponseOptions& options)
	{
		StepResponseFit fit;
		std::vector<TimedSample> samples = in;
		std::sort(samples.begin(), samples.end(),
			[](const TimedSample& a, const TimedSample& b) { return a.seconds < b.seconds; });
		std::vector<double> before, tail;
		const double end = samples.empty() ? 0.0 : samples.back().seconds;
		const double tailFrom = end - (end - commandSeconds) * options.settledTailFraction;
		for (const TimedSample& s : samples)
		{
			if (s.seconds < commandSeconds) before.push_back(s.value);
			if (s.seconds >= tailFrom) tail.push_back(s.value);
		}
		if (before.size() < 3 || tail.size() < 3) { fit.error = "need samples before the command and a settled tail"; return fit; }
		fit.from = Median(before);
		fit.to = Median(tail);
		const double span = fit.to - fit.from;
		if (std::fabs(span) < 1e-12) { fit.error = "no step"; return fit; }

		std::vector<double> t, z;
		for (const TimedSample& s : samples)
			if (s.seconds >= commandSeconds)
			{
				t.push_back(s.seconds - commandSeconds);
				z.push_back((s.value - fit.from) / span);
			}
		if (t.size() < 8) { fit.error = "too few samples after the command"; return fit; }

		auto crossing = [&](double level)
		{
			// The first time the (lightly smoothed) record stays above level.
			const size_t w = std::max<size_t>(1, t.size() / 400);
			for (size_t i = 0; i + w < t.size(); ++i)
			{
				double m = 0.0;
				for (size_t k = 0; k < w; ++k) m += z[i + k];
				if (m / w >= level) return t[i];
			}
			return t.back();
		};
		const double t05 = crossing(0.05), t63 = crossing(0.632), t90 = crossing(0.9);

		auto sse = [&](double dead, double tau, double ramp)
		{
			if (dead < 0.0 || tau <= 1e-5 || ramp < 0.0 || dead > options.maxDeadTimeSeconds ||
				ramp > options.maxRampSeconds) return 1e300;
			double s = 0.0;
			for (size_t i = 0; i < t.size(); ++i)
			{
				const double r = z[i] - StepModel(t[i], dead, tau, ramp);
				s += r * r;
			}
			return s;
		};

		// Instant step: grid over the dead time, golden section over tau, then polish.
		double bestDead = 0.0, bestTau = (std::max)(t63 - t05, 1e-3), bestStep = 1e300;
		const double dt = (std::max)((t.back() - t.front()) / static_cast<double>(t.size()), 1e-4);
		for (double dead = (std::max)(0.0, t05 - 0.5 * (t63 - t05) - 5 * dt); dead <= t05 + dt; dead += dt)
		{
			double lo = 1e-4, hi = (std::max)(4.0 * (t90 + 1e-3), 1e-2);
			const double gr = 0.6180339887498949;
			double a = hi - gr * (hi - lo), b = lo + gr * (hi - lo);
			double fa = sse(dead, a, 0.0), fb = sse(dead, b, 0.0);
			for (int i = 0; i < 50; ++i)
			{
				if (fa < fb) { hi = b; b = a; fb = fa; a = hi - gr * (hi - lo); fa = sse(dead, a, 0.0); }
				else { lo = a; a = b; fa = fb; b = lo + gr * (hi - lo); fb = sse(dead, b, 0.0); }
			}
			const double tau = 0.5 * (lo + hi);
			const double v = sse(dead, tau, 0.0);
			if (v < bestStep) { bestStep = v; bestDead = dead; bestTau = tau; }
		}
		{
			const std::vector<double> p = NelderMead([&](const std::vector<double>& q) { return sse(q[0], q[1], 0.0); },
				{ bestDead, bestTau }, { 2 * dt, 0.1 * bestTau }, 400);
			const double v = sse(p[0], p[1], 0.0);
			if (v < bestStep) { bestStep = v; bestDead = p[0]; bestTau = p[1]; }
		}

		// Internal ramp: several starts, Nelder-Mead in (dead, tau, ramp).
		double rDead = bestDead, rTau = bestTau, rRamp = 0.0, bestRamp = 1e300;
		const double rise = (std::max)(t90 - t05, 4 * dt);
		for (const double fraction : { 0.25, 0.5, 0.8 })
			for (const double tauFraction : { 0.1, 0.4, 1.0 })
			{
				const double ramp = fraction * rise;
				const std::vector<double> start = { (std::max)(0.0, t05 - 0.05 * ramp), (std::max)(tauFraction * bestTau, 2 * dt), ramp };
				const std::vector<double> p = NelderMead([&](const std::vector<double>& q) { return sse(q[0], q[1], q[2]); },
					start, { 2 * dt, 0.2 * start[1], 0.2 * ramp }, 600);
				const double v = sse(p[0], p[1], p[2]);
				if (v < bestRamp) { bestRamp = v; rDead = p[0]; rTau = p[1]; rRamp = p[2]; }
			}

		fit.ok = true;
		if (bestRamp < (1.0 - options.rampSignificance) * bestStep && rRamp > 3.0 * dt)
		{
			fit.rampDetected = true;
			fit.deadTimeSeconds = rDead;
			fit.tauSeconds = rTau;
			fit.rampSeconds = rRamp;
			fit.rmsResidual = std::sqrt(bestRamp / t.size());
		}
		else
		{
			fit.deadTimeSeconds = bestDead;
			fit.tauSeconds = bestTau;
			fit.rmsResidual = std::sqrt(bestStep / t.size());
		}
		return fit;
	}

	LatencySummary SummarizeSteps(const std::vector<StepResponseFit>& fits)
	{
		LatencySummary s;
		std::vector<double> dead, settle;
		int ramps = 0;
		for (const StepResponseFit& f : fits)
		{
			if (!f.ok) continue;
			dead.push_back(f.deadTimeSeconds);
			settle.push_back(f.SettleSeconds());
			if (f.rampDetected) ++ramps;
		}
		if (dead.empty()) return s;
		s.ok = true;
		s.latencyMs = static_cast<int>(std::lround(Median(dead) * 1000.0));
		s.settleMs = static_cast<int>(std::lround(Median(settle) * 1000.0));
		s.rampDetected = ramps * 2 > static_cast<int>(dead.size());
		return s;
	}
}
