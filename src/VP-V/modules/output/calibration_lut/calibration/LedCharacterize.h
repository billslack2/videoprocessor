// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration, LED projector
// measurements: fits for the LED values that are guesses today.
//  - flux_table: LPW1 -> relative flux, measured in both directions so the
//    up/down hysteresis is visible (the table is their mean).
//  - CNTR curve: light against CNTR for a few codes; dmd_clip_cntr is where
//    the white code (235) stops gaining (the DMD's maximum), cntr_max_usable
//    the highest CNTR that still adds light for any measured code.
//  - Step response: dead time plus first-order lag, with a projector-internal
//    linear ramp detected when it explains the data significantly better.
//
// Input is time-stamped samples; nothing here talks to a meter, sensor or
// projector. No rates are assumed: the sample times carry them.

#include <string>
#include <utility>
#include <vector>

namespace Calibration
{
	struct TimedSample
	{
		double seconds = 0.0;
		double value = 0.0;
	};

	enum class StepDirection
	{
		Up,
		Down,
	};

	// One dwell at an LPW1 level, entered from below (Up) or above (Down).
	struct FluxStep
	{
		int lpw = 0;
		StepDirection direction = StepDirection::Up;
		std::vector<TimedSample> samples;
	};

	struct FluxFitOptions
	{
		double settledFraction = 0.5;   // use the last part of each dwell
		int referenceLpw = -1;          // flux 1.0 here; -1 = the highest level measured
	};

	struct FluxPoint
	{
		int lpw = 0;
		bool haveUp = false, haveDown = false;
		double up = 0.0, down = 0.0;    // relative flux per direction
		double flux = 0.0;              // the table value (mean, monotone)
		double hysteresis = 0.0;        // (down - up) / flux
	};

	struct FluxFit
	{
		bool ok = false;
		std::vector<FluxPoint> points;
		double maxHysteresis = 0.0;     // largest |hysteresis|
		std::vector<std::pair<double, double>> table;   // strictly rising in lpw and flux
		std::string error;
		// "60:0.551234, 65:0.590000, ..." (LedDrive::ParseFluxTable's format)
		std::string ConfigValue() const;
	};

	FluxFit FitFluxTable(const std::vector<FluxStep>& steps, const FluxFitOptions& options = FluxFitOptions());

	struct CntrSample
	{
		int cntr = 0;
		int code = 0;           // 8-bit limited code shown
		double luminance = 0.0;
	};

	struct CntrFit
	{
		bool ok = false;
		bool clipReached = false;       // the white code reached the plateau in range
		double clipExact = 0.0;         // the knee (fractional CNTR)
		int dmdClipCntr = -1;           // -1 = not reached (config: = cntr_max_usable)
		double usableExact = 0.0;
		int cntrMaxUsable = 0;
		double plateau = 0.0;           // the DMD's maximum light
		std::string error;
	};

	CntrFit FitCntrCurve(const std::vector<CntrSample>& samples, int whiteCode = 235);

	struct StepResponseOptions
	{
		double maxDeadTimeSeconds = 1.0;
		double maxRampSeconds = 2.0;
		// The ramp model must cut the step model's residual by this fraction.
		double rampSignificance = 0.3;
		double settledTailFraction = 0.15;  // the final level from the last part of the record
	};

	struct StepResponseFit
	{
		bool ok = false;
		double from = 0.0, to = 0.0;
		double deadTimeSeconds = 0.0;
		double tauSeconds = 0.0;
		double rampSeconds = 0.0;       // 0 = an instant step inside the projector
		bool rampDetected = false;
		double rmsResidual = 0.0;       // in units of |to - from|
		std::string error;
		// 63% of the way after the dead time (lag plus half the ramp).
		double SettleSeconds() const { return tauSeconds + 0.5 * rampSeconds; }
	};

	// samples: light against time around one commanded step at commandSeconds.
	StepResponseFit FitStepResponse(const std::vector<TimedSample>& samples, double commandSeconds,
		const StepResponseOptions& options = StepResponseOptions());

	// The model value (normalised 0 -> 1) at time t after the command.
	double StepModel(double sinceCommand, double deadTime, double tau, double ramp);

	struct LatencySummary
	{
		bool ok = false;
		int latencyMs = 0;   // led_latency_ms / cntr_latency_ms
		int settleMs = 0;    // led_settle_ms / cntr_settle_ms
		bool rampDetected = false;
	};

	// The median over several fitted steps (up and down).
	LatencySummary SummarizeSteps(const std::vector<StepResponseFit>& fits);

	// A picture step against an LPW1 step: how much later the LED lands.
	inline double LedAfterPictureSeconds(const StepResponseFit& picture, const StepResponseFit& led)
	{
		return led.deadTimeSeconds - picture.deadTimeSeconds;
	}
}
