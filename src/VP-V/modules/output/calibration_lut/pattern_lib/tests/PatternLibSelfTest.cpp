// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Stand-alone tests for the calibration pattern library (../). No MSTest, no solution, no
// renderer, libplacebo or Windows headers: only the C++ standard library.
// Build + run: ../run_pattern_lib_tests.ps1 (or the one cl.exe line in ../README.md).
// Exit code 0 = all checks passed; each failure prints file:line and the expression.

#include "../CalibrationPattern.h"
#include "../ColorMath.h"
#include "../PatternCodes.h"
#include "../PatternPlan.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void Report(bool ok, const char* expression, const char* file, int line)
	{
		++g_checks;
		if (ok) return;
		++g_failures;
		std::printf("FAIL %s:%d  %s\n", file, line, expression);
	}
}

#define CHECK(expr) Report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) Report((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) Report(std::fabs((a) - (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__)

using namespace Calibration;

namespace
{
	PatternOutputInfo Format(uint32_t container, uint32_t code, bool limited)
	{
		PatternOutputInfo f;
		f.available = true;
		f.containerBits = container;
		f.codeBits = code;
		f.limitedRange = limited;
		return f;
	}

	void FormatSupport()
	{
		std::string why;
		CHECK(PatternFormatSupported(Format(8, 8, true), why));
		CHECK(PatternFormatSupported(Format(10, 8, true), why));
		CHECK(PatternFormatSupported(Format(10, 10, false), why));
		CHECK(!PatternFormatSupported(Format(16, 10, true), why));
		CHECK(why.find("FP16") != std::string::npos);
		CHECK(!PatternFormatSupported(Format(8, 10, true), why));   // deeper than the swapchain
		PatternOutputInfo none;                                      // available = false
		CHECK(!PatternFormatSupported(none, why));
		CHECK(!why.empty());
	}

	void LevelsPerFormat()
	{
		const PatternOutputInfo l8 = Format(8, 8, true), l10 = Format(10, 10, true);
		const PatternOutputInfo f8 = Format(8, 8, false), f10 = Format(10, 10, false);
		CHECK_EQ(BlackCode(l8), 16);   CHECK_EQ(WhiteCode(l8), 235);
		CHECK_EQ(LowestLegalCode(l8), 1);   CHECK_EQ(HighestLegalCode(l8), 254);
		CHECK_EQ(BlackCode(l10), 64);  CHECK_EQ(WhiteCode(l10), 940);
		CHECK_EQ(LowestLegalCode(l10), 4);  CHECK_EQ(HighestLegalCode(l10), 1019);
		CHECK_EQ(BlackCode(f8), 0);    CHECK_EQ(WhiteCode(f8), 255);
		CHECK_EQ(BlackCode(f10), 0);   CHECK_EQ(WhiteCode(f10), 1023);
		CHECK_EQ(LowestLegalCode(f10), 0);  CHECK_EQ(HighestLegalCode(f10), 1023);
	}

	void EightInTen()
	{
		// The sigma-delta's domain: limited = code << 2, full = k * 1023 / 255.
		const PatternOutputInfo lim = Format(10, 8, true), full = Format(10, 8, false);
		CHECK_EQ(ContainerValue(lim, 16), 64);
		CHECK_EQ(ContainerValue(lim, 235), 940);
		CHECK_EQ(ContainerValue(lim, 1), 4);
		CHECK_EQ(ContainerValue(full, 0), 0);
		CHECK_EQ(ContainerValue(full, 255), 1023);
		CHECK_EQ(ContainerValue(full, 128), 514);
		// Out-of-range codes are clamped to the format, never wrapped.
		CHECK_EQ(ContainerValue(full, 9999), 1023);
		CHECK_EQ(ContainerValue(full, -5), 0);
		// 8-bit container: container = code.
		CHECK_EQ(ContainerValue(Format(8, 8, true), 200), 200);
	}

	void SignalsLandOnWholeCodes()
	{
		const PatternOutputInfo f = Format(10, 10, true);
		const PatchCodes black = CodesForSignal(f, { 0.0, 0.0, 0.0 });
		CHECK_EQ(black.code[0], 64);
		CHECK(!black.clipped);
		const PatchCodes white = CodesForSignal(f, { 1.0, 1.0, 1.0 });
		CHECK_EQ(white.code[1], 940);
		CHECK_EQ(white.container[2], 940);
		CHECK_NEAR(white.device[0], 1.0, 1e-12);
		// The recorded device signal is the code's exact signal, not the request.
		const PatchCodes mid = CodesForSignal(f, { 0.5001, 0.5001, 0.5001 });
		CHECK_NEAR(mid.device[0], CodeToSignal(f, mid.code[0]), 1e-12);
		CHECK_EQ(SignalToCode(f, 0.5), 502);
		// BTB / WTW codes stay legal on a limited link.
		const double btb = LimitedCodeToSignal(4);   // 8-bit code 4 -> signal below black
		const PatchCodes b = CodesForSignal(Format(8, 8, true), { btb, btb, btb });
		CHECK_EQ(b.code[0], 4);
		CHECK(!b.clipped);
		// Far outside the legal codes: clipped and flagged.
		const PatchCodes c = CodesForSignal(Format(8, 8, true), { -0.5, 2.0, 0.0 });
		CHECK(c.clipped);
		CHECK_EQ(c.code[0], 1);
		CHECK_EQ(c.code[1], 254);
	}

	void RepresentableOnFullRange()
	{
		Patch btb;
		btb.kind = PatchKind::Btb;
		btb.rgb[0] = btb.rgb[1] = btb.rgb[2] = LimitedCodeToSignal(4);
		CHECK(PatchRepresentable(Format(10, 10, true), btb));
		CHECK(!PatchRepresentable(Format(10, 10, false), btb));   // full range has nothing below black
		Patch target = btb;
		target.space = PatchSpace::TargetSignal;                   // goes through the LUT first
		CHECK(PatchRepresentable(Format(10, 10, false), target));
	}

	void PqRoundTrip()
	{
		CHECK_NEAR(PqEncode(10000.0), 1.0, 1e-9);
		CHECK_NEAR(PqEncode(0.0), 0.0, 1e-5);
		CHECK_NEAR(PqEncode(203.0), 0.58, 0.01);
		for (const double nits : { 0.01, 1.0, 100.0, 1000.0, 4000.0 })
			CHECK_NEAR(PqDecode(PqEncode(nits)) / nits, 1.0, 1e-6);
		CHECK_NEAR(HdrSignalForNits(Format(10, 10, true), 100.0), PqEncode(100.0), 0.0);
	}

	void WindowGeometry()
	{
		const PixelRect full = PatchWindowRect(1920, 1080, 100.0);
		CHECK_EQ(full.left, 0); CHECK_EQ(full.top, 0);
		CHECK_EQ(full.Width(), 1920); CHECK_EQ(full.Height(), 1080);
		const PixelRect q = PatchWindowRect(1920, 1080, 25.0);
		CHECK_EQ(q.Width(), 960); CHECK_EQ(q.Height(), 540);
		CHECK_EQ(q.left, 480); CHECK_EQ(q.top, 270);
		// Any resolution: even offsets (4:2:x pairing), at least 2 x 2, inside the surface.
		for (const int w : { 3, 640, 2560, 3840, 4097 })
			for (const double area : { 0.01, 1.0, 10.0, 50.0, 99.0 })
			{
				const PixelRect r = PatchWindowRect(w, w * 9 / 16 + 2, area);
				CHECK(r.left >= 0 && r.top >= 0 && r.right <= w && r.bottom <= w * 9 / 16 + 2);
				CHECK(r.Width() >= 2 && r.Height() >= 2);
				CHECK_EQ(r.left & 1, 0); CHECK_EQ(r.top & 1, 0);
			}
		const PixelRect none = PatchWindowRect(0, 100, 50.0);
		CHECK_EQ(none.Width(), 0);
	}

	void Plans()
	{
		// Greyscale: black, ramp, near-black steps, BTB, WTW.
		const std::vector<Patch> grey = GreyscalePatches();
		int btb = 0, wtw = 0, black = 0, nearBlack = 0;
		for (const Patch& p : grey)
		{
			btb += p.kind == PatchKind::Btb;
			wtw += p.kind == PatchKind::Wtw;
			black += p.kind == PatchKind::Black;
			nearBlack += p.kind == PatchKind::NearBlack;
			CHECK(p.space == PatchSpace::Device);
			CHECK_EQ(p.rgb[0], p.rgb[1]);
		}
		CHECK_EQ(btb, 4); CHECK_EQ(wtw, 2); CHECK_EQ(black, 1); CHECK(nearBlack >= 5);
		for (const Patch& p : grey)
			if (p.kind == PatchKind::Btb) CHECK(p.rgb[0] < 0.0);
			else if (p.kind == PatchKind::Wtw) CHECK(p.rgb[0] > 1.0);

		CHECK_EQ(PrimarySecondaryPatches().size(), static_cast<size_t>(24));
		const std::vector<Patch> cube = CubePatches(5);
		CHECK_EQ(cube.size(), static_cast<size_t>(125));
		CHECK_NEAR(cube[1].rgb[0], 0.25, 1e-12);   // R fastest
		CHECK_NEAR(cube[5].rgb[1], 0.25, 1e-12);
		CHECK_EQ(CubePatches(1).size(), static_cast<size_t>(0));

		CHECK_EQ(CubeSizeFor(PlanKind::Quick), 0);
		CHECK_EQ(CubeSizeFor(PlanKind::Standard), 9);
		CHECK_EQ(CubeSizeFor(PlanKind::Thorough), 17);

		// A plan starts and ends with a drift reference; same input = same plan.
		const std::vector<Patch> quick = BuildPlan(PlanKind::Quick, 50);
		CHECK(quick.front().kind == PatchKind::DriftReference);
		CHECK(quick.back().kind == PatchKind::DriftReference);
		const std::vector<Patch> again = BuildPlan(PlanKind::Quick, 50);
		CHECK_EQ(quick.size(), again.size());
		for (size_t i = 0; i < quick.size() && i < again.size(); ++i)
			CHECK(quick[i].label == again[i].label);
		for (const Patch& p : quick)
			if (p.kind == PatchKind::DriftReference) CHECK(!p.usedForFit);
		const std::vector<Patch> standard = BuildPlan(PlanKind::Standard, 50);
		CHECK(standard.size() > quick.size() + 729);

		// The verification set: target-signal, never used for the fit, random colours off the lattice.
		const std::vector<Patch> verify = VerificationPatches(20, 20261017u, 9);
		CHECK_EQ(verify.size(), static_cast<size_t>(24 + 8 + 5 + 5 + 20));
		int random = 0;
		for (const Patch& p : verify)
		{
			CHECK(p.space == PatchSpace::TargetSignal);
			CHECK(!p.usedForFit);
			if (p.kind != PatchKind::VerifyRandom) continue;
			++random;
			const double step = 1.0 / 8.0;
			bool allNear = true;
			for (int k = 0; k < 3; ++k)
				if (std::fabs(p.rgb[k] / step - std::floor(p.rgb[k] / step + 0.5)) > 1.0 / 3.0) allNear = false;
			CHECK(!allNear);
		}
		CHECK_EQ(random, 20);
		const std::vector<Patch> verify2 = VerificationPatches(20, 20261017u, 9);
		for (size_t i = 0; i < verify.size(); ++i) CHECK_EQ(verify[i].rgb[0], verify2[i].rgb[0]);
		CHECK_EQ(ColorChecker24().size(), static_cast<size_t>(24));

		std::set<std::string> kinds;
		for (const PatchKind k : { PatchKind::Black, PatchKind::Greyscale, PatchKind::Btb, PatchKind::Cube,
			PatchKind::VerifyRandom })
			kinds.insert(ToString(k));
		CHECK_EQ(kinds.size(), static_cast<size_t>(5));
	}

	void TimeEstimates()
	{
		const MeterTimingModel m;
		CHECK(m.IntegrationSeconds(0.001) > m.IntegrationSeconds(1.0));
		CHECK(m.IntegrationSeconds(1.0) > m.IntegrationSeconds(100.0));
		CHECK(m.IntegrationSeconds(0.0) <= m.maxSeconds + m.overheadSeconds + 1e-12);
		const PlanEstimate e = EstimateTime(BuildPlan(PlanKind::Quick), 1.0);
		CHECK(e.patches > 50);
		CHECK_NEAR(e.settleSeconds, e.patches * 1.0, 1e-9);
		CHECK(e.TotalMinutes() > 1.0);
		CHECK_NEAR(e.TotalMinutes() * 60.0, e.TotalSeconds(), 1e-9);
	}

	void Settling()
	{
		SettleOptions o;
		o.configuredMs = 40;
		CHECK_NEAR(SettleSeconds(o, 100.0, 90.0, 100.0), 0.040, 1e-12);
		CHECK_NEAR(SettleSeconds(o, 100.0, 5.0, 100.0), 0.060, 1e-12);   // a big drop adds half again
		o.extraLatencySeconds = 0.1;
		CHECK_NEAR(SettleSeconds(o, 100.0, 90.0, 100.0), 0.140, 1e-12);
		SettleOptions byKind;
		byKind.displayKind = 0;
		CHECK_NEAR(SettleSeconds(byKind, 0.0, 10.0, 100.0), 0.5, 1e-12);
		byKind.displayKind = 99;                                        // clamped to the last kind
		CHECK_NEAR(SettleSeconds(byKind, 0.0, 10.0, 100.0), 1.0, 1e-12);
	}

	void Readings()
	{
		ReadingPolicy policy;
		policy.floorNits = 0.05;
		policy.nearBlackMaxReads = 4;
		ReadingAccumulator bright(policy, 100.0);
		CHECK(!bright.Done());
		bright.Add({ 50.0, 50.0, 50.0 });
		CHECK(bright.Done());                 // one settled reading above the near-black threshold
		CHECK(!bright.NearBlack());

		ReadingAccumulator dark(policy, 100.0);
		dark.Add({ 0.3, 0.3, 0.3 });
		CHECK(dark.NearBlack());
		CHECK(!dark.Done());
		dark.Add({ 0.301, 0.301, 0.301 });
		CHECK(dark.Done());                   // two agreeing readings above the floor
		CHECK_NEAR(dark.Mean()[1], 0.3005, 1e-12);

		ReadingAccumulator floorLimited(policy, 100.0);
		floorLimited.Add({ 0.01, 0.01, 0.01 });
		floorLimited.Add({ 0.01, 0.01, 0.01 });
		CHECK(floorLimited.FloorLimited());
		CHECK(!floorLimited.Done());          // below the floor: average up to the maximum
		floorLimited.Add({ 0.01, 0.01, 0.01 });
		floorLimited.Add({ 0.01, 0.01, 0.01 });
		CHECK(floorLimited.Done());
		CHECK_EQ(floorLimited.Count(), 4);
		CHECK_NEAR(ReadingAccumulator(policy, 100.0).SpreadY(), 0.0, 0.0);
	}

	void MeasurementOrder()
	{
		const std::vector<Patch> plan = BuildPlan(PlanKind::Quick);
		const std::vector<Patch> ordered = OrderForMeasurement(plan);
		CHECK_EQ(ordered.size(), plan.size());
		CHECK_NEAR(ordered[0].rgb[0], 1.0, 1e-9);   // device white first
		CHECK(ordered[0].kind != PatchKind::Black);
		// After white, the neutral patches step down in light (WTW first, then the ramp, near black, BTB).
		double last = 1e9;
		for (size_t i = 1; i < ordered.size(); ++i)
		{
			const Patch& p = ordered[i];
			const bool neutral = p.space == PatchSpace::Device && (p.kind == PatchKind::Black ||
				p.kind == PatchKind::Greyscale || p.kind == PatchKind::NearBlack || p.kind == PatchKind::Btb ||
				p.kind == PatchKind::Wtw);
			if (!neutral) break;
			const double sum = p.rgb[0] + p.rgb[1] + p.rgb[2];
			CHECK(sum <= last + 1e-9);
			last = sum;
		}
	}

	void ColourMaths()
	{
		const Mat3 rgbToXyz = RgbToXyz(Gamuts::Bt709());
		Mat3 inverse;
		CHECK(rgbToXyz.Inverse(inverse));
		const Mat3 product = rgbToXyz * inverse;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				CHECK_NEAR(product.m[i][j], i == j ? 1.0 : 0.0, 1e-9);
		const Vec3 white = rgbToXyz * Vec3{ 1.0, 1.0, 1.0 };
		CHECK_NEAR(white[1], 1.0, 1e-9);              // Y of white is 1
		CHECK_NEAR(XyzToXy(white).x, 0.3127, 1e-4);   // D65
		// CIEDE2000, Sharma, Wu and Dalal's published test pair 1 (2.0425) and identity.
		CHECK_NEAR(DeltaE2000({ 50.0, 2.6772, -79.7751 }, { 50.0, 0.0, -82.7485 }), 2.0425, 1e-4);
		CHECK_NEAR(DeltaE2000({ 50.0, 10.0, 20.0 }, { 50.0, 10.0, 20.0 }), 0.0, 1e-12);
		Mat3 singular;
		for (int j = 0; j < 3; ++j) singular.m[1][j] = 0.0;
		Mat3 out;
		CHECK(!singular.Inverse(out));
	}
}

int main()
{
	FormatSupport();
	LevelsPerFormat();
	EightInTen();
	SignalsLandOnWholeCodes();
	RepresentableOnFullRange();
	PqRoundTrip();
	WindowGeometry();
	Plans();
	TimeEstimates();
	Settling();
	Readings();
	MeasurementOrder();
	ColourMaths();
	std::printf("pattern_lib self test: %d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
