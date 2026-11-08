// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: which integer codes a patch is, in the active output
// format, and how long / how often it is measured.
//
// Output formats (PatternOutputInfo, read from the renderer):
//   8-bit       containerBits 8,  codeBits 8:  container = code
//   8-in-10     containerBits 10, codeBits 8:  the 8-bit code in the top 8 bits,
//               exactly as the sigma-delta emits it (limited: 4k / 1023,
//               full: k / 255 - Temporal sigma-delta.glsl, "Value domain")
//   10-bit      containerBits 10, codeBits 10: container = code
// Limited range: black 16 / 64, white 235 / 940; legal codes 1..254 (8-bit)
// and 4..1019 (10-bit), so the BTB / WTW codes of the plan are kept. Full
// range has no codes outside black..white (those patches are skipped).
//
// Device signal (the core's model input, PatternPlan.h): 0 = black code,
// 1 = white code, linear in codes. With the sigma-delta off for the patch a
// requested signal lands on a whole code; the measurement is recorded at the
// code's exact signal (QuantisedSignal), never the requested one.
//
// HDR (PQ) output: a patch is a code like any other; HdrSignalForNits gives
// the PQ signal (0..1 of the full code range) for an absolute luminance.

#include "CalibrationPattern.h"
#include "ColorMath.h"
#include "PatternPlan.h"

#include <string>
#include <vector>

namespace Calibration
{
	struct PatchCodes
	{
		int code[3] = { 0, 0, 0 };        // at codeBits
		uint16_t container[3] = { 0, 0, 0 };
		Vec3 device = { 0, 0, 0 };        // the exact signal of the codes
		bool clipped = false;             // the request was outside the legal codes
	};

	// False (with reason) for a format VP cannot show exact codes in
	// (float swapchain, unknown depth).
	bool PatternFormatSupported(const PatternOutputInfo& format, std::string& reason);

	int BlackCode(const PatternOutputInfo& format);
	int WhiteCode(const PatternOutputInfo& format);
	int LowestLegalCode(const PatternOutputInfo& format);
	int HighestLegalCode(const PatternOutputInfo& format);
	// A code at codeBits -> the container value.
	uint16_t ContainerValue(const PatternOutputInfo& format, int code);
	double CodeToSignal(const PatternOutputInfo& format, int code);
	int SignalToCode(const PatternOutputInfo& format, double signal);   // rounded, unclamped
	PatchCodes CodesForSignal(const PatternOutputInfo& format, const Vec3& signal);
	// True when the format can show the patch without clipping (BTB / WTW
	// patches on a full-range output cannot).
	bool PatchRepresentable(const PatternOutputInfo& format, const Patch& patch);

	// SMPTE ST 2084.
	double PqEncode(double nits);
	double PqDecode(double signal);
	// The full-range PQ signal (0 = code 0 of the full range) for a luminance;
	// convert with the output's range by SignalForFullRangeFraction.
	double HdrSignalForNits(const PatternOutputInfo& format, double nits);

	// ------------------------------------------------------- measuring

	// How long the display needs after a pattern change before the meter
	// starts (display physics, never a frame count): the configured settle,
	// else the display kind's default, plus extra after a large drop in light
	// (LCD / projector-lamp / OLED panels decay slowest into black).
	struct SettleOptions
	{
		int configuredMs = 0;           // [calibration] settle_ms; 0 = from the display kind
		int displayKind = 0;            // DisplayKind as int
		double extraLatencySeconds = 0; // measured display latency, when known
	};

	double SettleSeconds(const SettleOptions& options, double previousY, double nextY, double whiteY);

	// Repeated readings of one patch until two agree (within tolerance, or
	// the meter's absolute noise floor), up to maxReads; near black every
	// reading up to near_black_max_reads is averaged (a colorimeter's count
	// is noisy there) and the patch is marked floor-limited below the meter's
	// floor.
	struct ReadingPolicy
	{
		double tolerance = 0.01;         // relative, repeat_tolerance_percent / 100
		double floorNits = 0.05;         // the meter's usable floor (MeterCatalog)
		int normalMaxReads = 1;          // above the near-black threshold one settled reading
		int nearBlackMaxReads = 4;
		double nearBlackFraction = 0.01; // of white: below it the near-black strategy applies
	};

	class ReadingAccumulator
	{
	public:
		ReadingAccumulator(const ReadingPolicy& policy, double whiteY);
		void Add(const Vec3& xyz);
		// True when no more readings are needed.
		bool Done() const;
		Vec3 Mean() const;
		int Count() const { return static_cast<int>(readings.size()); }
		bool NearBlack() const;
		bool FloorLimited() const;
		// Relative spread of Y over the readings (0 with one).
		double SpreadY() const;

	private:
		ReadingPolicy policy;
		double whiteY;
		std::vector<Vec3> readings;
	};

	// The near-black part of a plan, reordered so the meter steps down from
	// brighter patches (a colorimeter's dark integration adapts best when
	// the light falls gradually) and the black patch is measured last of the
	// greyscale ramp, then again at the end of the run for drift.
	std::vector<Patch> OrderForMeasurement(const std::vector<Patch>& plan);
}
