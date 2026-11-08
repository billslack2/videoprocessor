// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration phase 2: the HDR (PQ) verify run. With VP's output in
// HDR (PQ), VP shows PQ patches - exact codes, with the HDR metadata VP sends
// left as it is - and the meter measures how the display tracks the PQ EOTF
// against the target:
//
//   target = what VP itself would send for that source light: VP's own tone
//            mapping when the display peak VP maps to is below the source's
//            (the renderer reports libplacebo's tone curve for the playing
//            source; hdr_verify_tone_curve), else the source light itself;
//            a display then shows the PQ code's absolute light.
//
// Each patch is reported with its luminance tracking error and dE ITP
// (ITU-R BT.2124: 720 x |(dI, 0.5 dCt, dCp)| in ICtCp, absolute light). A
// patch whose target is above the display's measured peak is marked and left
// out of the verdict (the display cannot reach it; it shows the clip).
//
// Verify only: nothing in the configuration changes (the run ends in Done).

#include "CalibrationRunBase.h"

namespace Calibration
{
	// ICtCp (BT.2100) of absolute XYZ (cd/m2, D65).
	Vec3 XyzToIctcp(const Vec3& xyzNits);
	// ITU-R BT.2124 dE ITP.
	double DeltaEItp(const Vec3& xyzReference, const Vec3& xyzTest);

	// The tone curve a target is computed with.
	struct ToneCurve
	{
		enum class Kind { Identity, Samples, Bt2390 } kind = Kind::Identity;
		std::vector<double> sourceNits, outputNits;     // Samples: rising
		double sourcePeak = 1000.0, sourceBlack = 0.0;  // Bt2390
		double targetPeak = 1000.0, targetBlack = 0.0;
		std::string description;
		double Map(double nits) const;
	};

	// ITU-R BT.2390 EETF (PQ domain, with the black lift), nits in and out.
	double Bt2390Eetf(double nits, double sourceBlack, double sourcePeak, double targetBlack, double targetPeak);

	// From the renderer's report and the settings (vp = VP's own curve when the
	// renderer reported one, else BT.2390; bt2390; none). measuredPeak: the
	// display's measured peak, the BT.2390 target when VP's is unknown.
	ToneCurve ChooseToneCurve(const PatternOutputInfo& output, const CalibrationSettings& settings, double measuredPeak);

	struct HdrPatch
	{
		std::string label;
		Vec3 sourceRgbNits = { 0, 0, 0 };   // linear BT.2020, absolute
		bool peakProbe = false;             // the display's peak (PQ 10000)
	};

	// Peak probe first, then greys from 0.05 nits to the source peak, a
	// reference white (203 cd/m2) and P3 primaries / secondaries at two levels.
	std::vector<HdrPatch> BuildHdrPlan(double sourcePeak);

	struct HdrEntry
	{
		std::string label;
		double sourceNits = 0.0;        // the source's Y
		Vec3 targetXyz = { 0, 0, 0 };
		Vec3 measuredXyz = { 0, 0, 0 };
		PatchCodes codes;
		double deltaEItp = 0.0;
		double trackingPercent = 0.0;   // (measured Y / target Y - 1) x 100
		bool aboveDisplayPeak = false;
		bool grey = false;
		bool informative = false;       // the peak probe and the black patch: reported, not in the verdict
	};

	struct HdrReport
	{
		std::vector<HdrEntry> entries;
		double measuredPeak = 0.0;
		double meanItp = 0.0, maxItp = 0.0, p95Itp = 0.0;
		std::string worst;
		int counted = 0;
		bool pass = false;              // mean <= 2 and max <= 4 over the patches the display can reach
		std::string curve;
		std::string metadata;
	};

	// Target XYZ for a source patch through the curve, on the exact PQ codes.
	void HdrTarget(const HdrPatch& patch, const ToneCurve& curve, const PatternOutputInfo& format, PatchCodes& codes,
		Vec3& targetXyz);
	void SummarizeHdr(HdrReport& report);
	std::string FormatHdrJson(const HdrReport& report);
	std::string FormatHdrReview(const HdrReport& report);
	std::string FormatHdrHtml(const HdrReport& report);

	class HdrVerifyRun final : public MeterRun
	{
	public:
		HdrVerifyRun(const CalibrationSettings& settings, const SessionEnvironment& environment, std::unique_ptr<IMeter> meter,
			double meterFloorNits, IPatternSink& patterns, ISessionClock& clock);
		~HdrVerifyRun() override;
		const HdrReport& Report() const { return report; }
		const ToneCurve& Curve() const { return curve; }

	protected:
		const char* FolderSuffix() const override { return "hdr"; }
		bool OnPatternsStarted(std::string& error) override;
		void BeginWork() override;
		void StepWork(SessionStep step, Command command) override;

	private:
		std::vector<HdrPatch> plan;
		size_t index = 0;
		ToneCurve curve;
		HdrReport report;
	};
}
