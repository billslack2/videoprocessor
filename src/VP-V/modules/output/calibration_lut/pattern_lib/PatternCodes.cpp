// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "PatternCodes.h"

#include <algorithm>
#include <cmath>

namespace Calibration
{
	bool PatternFormatSupported(const PatternOutputInfo& f, std::string& reason)
	{
		if (!f.available)
		{
			reason = f.reason[0] ? f.reason : "the VP renderer is not running";
			return false;
		}
		if (f.containerBits != 8 && f.containerBits != 10)
		{
			reason = f.containerBits == 16 ?
				"the output is a floating-point (FP16) swapchain: exact patch codes need an 8- or 10-bit output" :
				"the output's bit depth is not known";
			return false;
		}
		if (f.codeBits != 8 && f.codeBits != 10)
		{
			reason = "the output's colour depth is not known";
			return false;
		}
		if (f.codeBits > f.containerBits)
		{
			reason = "the colour depth is deeper than the swapchain";
			return false;
		}
		reason.clear();
		return true;
	}

	namespace
	{
		int Shift(const PatternOutputInfo& f) { return static_cast<int>(f.codeBits) - 8; }
		int MaxCode(const PatternOutputInfo& f) { return (1 << f.codeBits) - 1; }
	}

	int BlackCode(const PatternOutputInfo& f) { return f.limitedRange ? 16 << Shift(f) : 0; }
	int WhiteCode(const PatternOutputInfo& f) { return f.limitedRange ? 235 << Shift(f) : MaxCode(f); }
	int LowestLegalCode(const PatternOutputInfo& f) { return f.limitedRange ? 1 << Shift(f) : 0; }
	int HighestLegalCode(const PatternOutputInfo& f)
	{
		return f.limitedRange ? (254 << Shift(f)) + ((1 << Shift(f)) - 1) : MaxCode(f);
	}

	uint16_t ContainerValue(const PatternOutputInfo& f, int code)
	{
		code = (std::min)((std::max)(code, 0), MaxCode(f));
		if (f.containerBits == f.codeBits) return static_cast<uint16_t>(code);
		// 8-in-10: the code in the top 8 bits, as the sigma-delta writes it.
		if (f.limitedRange) return static_cast<uint16_t>(code << (f.containerBits - f.codeBits));
		const double scale = static_cast<double>((1 << f.containerBits) - 1) / MaxCode(f);
		return static_cast<uint16_t>(std::lround(code * scale));
	}

	double CodeToSignal(const PatternOutputInfo& f, int code)
	{
		const double black = BlackCode(f), white = WhiteCode(f);
		return (code - black) / (white - black);
	}

	int SignalToCode(const PatternOutputInfo& f, double signal)
	{
		const double black = BlackCode(f), white = WhiteCode(f);
		return static_cast<int>(std::lround(black + signal * (white - black)));
	}

	PatchCodes CodesForSignal(const PatternOutputInfo& f, const Vec3& signal)
	{
		PatchCodes p;
		const int lo = LowestLegalCode(f), hi = HighestLegalCode(f);
		for (int c = 0; c < 3; ++c)
		{
			int code = SignalToCode(f, signal[c]);
			if (code < lo || code > hi)
			{
				p.clipped = true;
				code = (std::min)((std::max)(code, lo), hi);
			}
			p.code[c] = code;
			p.container[c] = ContainerValue(f, code);
			p.device[c] = CodeToSignal(f, code);
		}
		return p;
	}

	bool PatchRepresentable(const PatternOutputInfo& f, const Patch& patch)
	{
		if (patch.space != PatchSpace::Device) return true;   // target-signal patches go through the LUT first
		return !CodesForSignal(f, { patch.rgb[0], patch.rgb[1], patch.rgb[2] }).clipped;
	}

	double PqEncode(double nits)
	{
		const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
		const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
		const double y = (std::min)((std::max)(nits / 10000.0, 0.0), 1.0);
		const double p = std::pow(y, m1);
		return std::pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
	}

	double PqDecode(double signal)
	{
		const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
		const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
		const double e = std::pow((std::min)((std::max)(signal, 0.0), 1.0), 1.0 / m2);
		const double num = (std::max)(e - c1, 0.0);
		const double den = c2 - c3 * e;
		return den > 0.0 ? 10000.0 * std::pow(num / den, 1.0 / m1) : 10000.0;
	}

	double HdrSignalForNits(const PatternOutputInfo&, double nits)
	{
		// The PQ signal spans black..white codes in either range.
		return PqEncode(nits);
	}

	// ------------------------------------------------------- measuring

	double SettleSeconds(const SettleOptions& o, double previousY, double nextY, double whiteY)
	{
		// DisplayKind: 0 projector, 1 LCD, 2 OLED, 3 other.
		static const double baseSeconds[4] = { 0.5, 1.0, 0.8, 1.0 };
		static const double dropSeconds[4] = { 0.5, 1.0, 0.5, 1.0 };
		const int kind = (std::min)((std::max)(o.displayKind, 0), 3);
		double seconds = o.configuredMs > 0 ? o.configuredMs / 1000.0 : baseSeconds[kind];
		if (whiteY > 0.0 && previousY > 0.0 && nextY < 0.1 * previousY && previousY > 0.01 * whiteY)
			seconds += o.configuredMs > 0 ? 0.5 * o.configuredMs / 1000.0 : dropSeconds[kind];
		return seconds + (std::max)(o.extraLatencySeconds, 0.0);
	}

	ReadingAccumulator::ReadingAccumulator(const ReadingPolicy& p, double white) : policy(p), whiteY(white) {}

	void ReadingAccumulator::Add(const Vec3& xyz) { readings.push_back(xyz); }

	Vec3 ReadingAccumulator::Mean() const
	{
		Vec3 sum = { 0, 0, 0 };
		for (const Vec3& r : readings) sum = Calibration::Add(sum, r);
		return readings.empty() ? sum : Scale(sum, 1.0 / readings.size());
	}

	bool ReadingAccumulator::NearBlack() const
	{
		if (readings.empty()) return false;
		const double y = Mean()[1];
		return (whiteY > 0.0 && y < policy.nearBlackFraction * whiteY) || y < 2.0 * policy.floorNits;
	}

	bool ReadingAccumulator::FloorLimited() const
	{
		return !readings.empty() && Mean()[1] < policy.floorNits;
	}

	double ReadingAccumulator::SpreadY() const
	{
		if (readings.size() < 2) return 0.0;
		double lo = readings[0][1], hi = lo;
		for (const Vec3& r : readings) { lo = (std::min)(lo, r[1]); hi = (std::max)(hi, r[1]); }
		const double mean = Mean()[1];
		return mean > 0.0 ? (hi - lo) / mean : (hi > lo ? 1.0 : 0.0);
	}

	bool ReadingAccumulator::Done() const
	{
		const int n = Count();
		if (n == 0) return false;
		if (!NearBlack()) return n >= (std::max)(policy.normalMaxReads, 1);
		const int maxReads = (std::max)(policy.nearBlackMaxReads, 1);
		if (n >= maxReads) return true;
		// Early out: two or more readings that agree, above the meter's floor.
		if (n >= 2 && !FloorLimited())
		{
			const double a = readings[n - 1][1], b = readings[n - 2][1];
			const double scale = (std::max)(std::fabs(a), std::fabs(b));
			return std::fabs(a - b) <= policy.tolerance * scale + 0.1 * policy.floorNits;
		}
		return false;
	}

	std::vector<Patch> OrderForMeasurement(const std::vector<Patch>& plan)
	{
		std::vector<Patch> grey, rest;
		for (const Patch& p : plan)
		{
			const bool neutral = p.space == PatchSpace::Device &&
				(p.kind == PatchKind::Black || p.kind == PatchKind::Greyscale || p.kind == PatchKind::NearBlack ||
					p.kind == PatchKind::Btb || p.kind == PatchKind::Wtw);
			(neutral ? grey : rest).push_back(p);
		}
		std::stable_sort(grey.begin(), grey.end(), [](const Patch& a, const Patch& b)
		{
			return a.rgb[0] + a.rgb[1] + a.rgb[2] > b.rgb[0] + b.rgb[1] + b.rgb[2];
		});
		// White (signal 1) first: it is the reference for everything after it.
		std::stable_partition(grey.begin(), grey.end(), [](const Patch& p)
		{
			return std::fabs(p.rgb[0] - 1.0) < 1e-9 && std::fabs(p.rgb[1] - 1.0) < 1e-9 && std::fabs(p.rgb[2] - 1.0) < 1e-9;
		});
		std::vector<Patch> out;
		out.reserve(plan.size());
		out.insert(out.end(), grey.begin(), grey.end());
		out.insert(out.end(), rest.begin(), rest.end());
		return out;
	}
}
