// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration (the module README, "LUT maths"):
// the display's forward model, the calibration target, the Gauss-Newton
// inverse, the 3D LUT and its .cube file, and the dE2000 verify report.
//
// Pure and double precision. Device values are the normalised display input
// signal (0 = code 16, 1 = code 235 on a limited link; outside 0..1 are the
// BTB / WTW codes). Target signals are the calibration LUT's input: full-
// domain RGB in the slot's gamut and transfer.
//
// Forward model (fit from measured XYZ):
//   lin(d) = K + M f(d)            K: measured black, M: 3x3, f: per-channel
//                                  monotone splines (greyscale through M^-1,
//                                  including the BTB / WTW codes)
//   F(d)   = lin(d) + (Y_lin(d) + eps) R(d)
//                                  R: a regularised residual field on a
//                                  device grid (trilinear, second-difference
//                                  smoothness), relative to the luminance so
//                                  it carries crosstalk / additivity errors
//                                  in shadows and highlights alike.
// The LUT's end slopes follow ExtendedRange::LutExtrapolate (the edge cell's
// slope, summed over clamped axes): the cells next to the black and white
// corners are set so that extrapolating the neutral axis reproduces the
// measured BTB / WTW response where the display responds there, and are left
// as the inverse made them where it does not (BTB / WTW codes still pass).

#include "../pattern_lib/ColorMath.h"
#include "../pattern_lib/PatternPlan.h"

#include <string>
#include <vector>

namespace Calibration
{
	struct Measurement
	{
		Vec3 device = { 0.0, 0.0, 0.0 };
		Vec3 xyz = { 0.0, 0.0, 0.0 };   // absolute (cd/m2 for Y)
		PatchKind kind = PatchKind::Cube;
	};

	// Monotone (non-decreasing) cubic Hermite spline, Fritsch-Carlson
	// tangents after an isotonic (pool-adjacent-violators) fit of the data.
	// Linear beyond the end knots with the end tangent.
	class MonotoneSpline
	{
	public:
		bool Fit(const std::vector<double>& x, const std::vector<double>& y,
			const std::vector<double>* weights = nullptr);
		double Eval(double x) const;
		double Slope(double x) const;
		// The smallest x in [lo, hi] with Eval(x) >= y (bisection).
		double Inverse(double y, double lo, double hi) const;
		double MinX() const { return xs.empty() ? 0.0 : xs.front(); }
		double MaxX() const { return xs.empty() ? 1.0 : xs.back(); }
		bool Empty() const { return xs.empty(); }
		const std::vector<double>& Knots() const { return xs; }
		const std::vector<double>& Values() const { return ys; }

	private:
		std::vector<double> xs, ys, ms;
	};

	struct ForwardModelOptions
	{
		int residualGrid = -1;          // -1 auto (from the cube data), 0 none, else nodes per axis
		double smoothness = 0.02;       // second-difference weight (relative to the data)
		double ridge = 1e-4;            // pull towards zero where there is no data
		double darkEpsilon = 1e-3;      // eps as a fraction of the white luminance
		int alternations = 4;           // curve / matrix refinements
	};

	class DisplayModel
	{
	public:
		bool Fit(const std::vector<Measurement>& measurements,
			const ForwardModelOptions& options = ForwardModelOptions(), std::string* error = nullptr);

		Vec3 Forward(const Vec3& device) const;
		// Without the residual field.
		Vec3 Linear(const Vec3& device) const;

		// Unconstrained Gauss-Newton (Levenberg-Marquardt damped) in linear
		// light. start: a warm start (else the linear model's inverse).
		// True when converged (relative residual < tolerance).
		bool Inverse(const Vec3& xyz, Vec3& device, const Vec3* start = nullptr, double tolerance = 1e-7) const;
		// From the start only (no restarts): the gamut-clip search, which
		// moves in small steps from a reachable colour.
		bool InverseFrom(const Vec3& xyz, Vec3& device, const Vec3& start, double tolerance = 1e-7) const
		{
			Vec3 d = start;
			const bool ok = valid && Refine(xyz, d, tolerance) <= tolerance;
			device = d;
			return ok;
		}

		const Vec3& Black() const { return black; }
		Vec3 NativeWhite() const { return Forward({ 1.0, 1.0, 1.0 }); }
		const Mat3& Matrix() const { return matrix; }
		// The measured full red / green / blue patches above black.
		const Vec3& MeasuredPrimary(int channel) const { return primaryXyz[channel]; }
		const MonotoneSpline& Curve(int channel) const { return curves[channel]; }
		// The measured device range (BTB / WTW codes included).
		double MinDevice() const { return minDevice; }
		double MaxDevice() const { return maxDevice; }
		// True when channel c's light changes measurably between two device values.
		bool ChannelResponds(int channel, double from, double to) const;
		// |W - (R + G + B - 2K)| / W in Y (the measured patches).
		double AdditivityError() const { return additivity; }
		int ResidualGrid() const { return grid; }
		bool Valid() const { return valid; }

	private:
		Vec3 Residual(const Vec3& device) const;
		// Damped Gauss-Newton from d; returns the final relative residual.
		double Refine(const Vec3& xyz, Vec3& d, double tolerance) const;
		void FitResidual(const std::vector<Measurement>& m, const ForwardModelOptions& options);

		bool valid = false;
		Vec3 black = { 0.0, 0.0, 0.0 };
		Vec3 primaryXyz[3];
		double responsiveFrom[3] = { 0.0, 0.0, 0.0 };   // below this a channel's curve is flat
		Mat3 matrix;
		Mat3 matrixInverse;
		MonotoneSpline curves[3];
		double minDevice = 0.0, maxDevice = 1.0;
		double additivity = 0.0;
		double whiteY = 1.0;
		double epsilon = 1e-3;
		int grid = 0;
		std::vector<Vec3> field;   // grid^3, R fastest
	};

	enum class TargetTransfer
	{
		Gamma,      // pure power, black-point compensated (keeps shadow detail)
		Bt1886,     // with the measured black and white
	};

	struct TargetOptions
	{
		Primaries primaries = Gamuts::Bt709();
		TargetTransfer transfer = TargetTransfer::Bt1886;
		double gamma = 2.4;            // TargetTransfer::Gamma (and BT.1886's exponent)
		double whiteFraction = 1.0;    // of the largest reachable white at the target white point
	};

	class CalibrationTarget
	{
	public:
		bool Build(const DisplayModel& model, const TargetOptions& options, std::string* error = nullptr);
		// Relative linear light: 0 at signal 0, 1 at signal 1; odd / analytic outside.
		double RelativeLinear(double signal) const;
		// Target XYZ of a signal triplet (unclipped; outside the display's gamut too).
		Vec3 Xyz(const Vec3& signal) const;
		const Vec3& White() const { return white; }
		const Vec3& Black() const { return black; }
		const TargetOptions& Options() const { return options; }

	private:
		TargetOptions options;
		Vec3 white = { 0, 0, 0 }, black = { 0, 0, 0 };
		Mat3 matrix;                  // relative linear RGB -> XYZ above black
		double bt1886A = 1.0, bt1886B = 0.0, lb = 0.0, lw = 1.0;
	};

	// Target signal -> device, with LCh chroma clipping at constant hue and
	// lightness (Lab against the target white) when the colour is out of
	// reach. The neutral axis is never clipped. Device is clamped to 0..1.
	bool MapTargetToDevice(const DisplayModel& model, const CalibrationTarget& target, const Vec3& signal,
		Vec3& device, bool* clipped = nullptr, const Vec3* warmStart = nullptr, int clipIterations = 18);

	struct EndSlopeFit
	{
		bool fittedHigh = false, fittedLow = false;
		Vec3 slopeHigh = { 0, 0, 0 }, slopeLow = { 0, 0, 0 };     // d(device)/d(signal) along the neutral axis
		Vec3 naturalHigh = { 0, 0, 0 }, naturalLow = { 0, 0, 0 }; // what the inverse alone gave
	};

	struct LutOptions
	{
		int size = 33;                 // 2..65 (LibplaceboDisplayLut::MAX_3D_SIZE)
		bool monotoneNeutral = true;
		bool fitEndSlopes = true;
		int clipIterations = 18;
	};

	struct Lut3D
	{
		int size = 0;
		std::vector<float> data;       // RGB triplets, R innermost, B outermost (pl_custom_lut's layout)
		EndSlopeFit endSlopes;
		int clippedNodes = 0;
		int failedNodes = 0;
		bool neutralAdjusted = false;

		size_t Index(int r, int g, int b) const { return 3u * ((static_cast<size_t>(b) * size + g) * size + r); }
		Vec3 At(int r, int g, int b) const
		{
			const size_t i = Index(r, g, b);
			return { data[i], data[i + 1], data[i + 2] };
		}
		void Set(int r, int g, int b, const Vec3& v)
		{
			const size_t i = Index(r, g, b);
			for (int k = 0; k < 3; ++k) data[i + k] = static_cast<float>(v[k]);
		}
		// Trilinear, input clamped to 0..1 (as libplacebo samples).
		Vec3 Sample(const Vec3& signal) const;
	};

	Lut3D BuildLut(const DisplayModel& model, const CalibrationTarget& target, const LutOptions& options = LutOptions());

	// .cube text: TITLE, comment lines, LUT_3D_SIZE and the data. No DOMAIN
	// lines (the default 0..1 domain; LibplaceboDisplayLut rejects others).
	std::string FormatCube(const Lut3D& lut, const std::string& title, const std::vector<std::string>& comments = {});
	bool WriteCube(const Lut3D& lut, const std::string& path, const std::string& title,
		const std::vector<std::string>& comments = {}, std::string* error = nullptr);

	// Verify report (design "Verify"): dE2000 against the target, pass at
	// mean < 1.5 and max < 3.
	struct VerifyEntry
	{
		std::string label;
		PatchKind kind = PatchKind::VerifyRandom;
		Vec3 targetXyz = { 0, 0, 0 };
		Vec3 measuredXyz = { 0, 0, 0 };
		double deltaE = 0.0;
	};

	struct GreyStep
	{
		double signal = 0.0;
		double luminance = 0.0;
		double gamma = 0.0;   // log(Y_rel) / log(signal); 0 at the ends
		double cct = 0.0;
	};

	struct VerifyReport
	{
		std::vector<VerifyEntry> entries;
		std::vector<GreyStep> greys;
		double meanDeltaE = 0.0;
		double maxDeltaE = 0.0;
		double p95DeltaE = 0.0;
		std::string worstLabel;
		double passMean = 1.5;
		double passMax = 3.0;
		bool pass = false;
	};

	// targetWhite: the Lab reference (the target's white). greys: (signal,
	// measured XYZ) pairs of the greyscale for gamma and CCT per step.
	VerifyReport BuildVerifyReport(const std::vector<VerifyEntry>& entries, const Vec3& targetWhite,
		const Vec3& targetBlack, const std::vector<std::pair<double, Vec3>>& greys = {});
	std::string FormatVerifyJson(const VerifyReport& report);
}
