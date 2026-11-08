// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration phase 2: the ambient-light check. The screen's black
// and white are measured with the room lights off and on, with the room's lux
// from VP's light sensor ([ambient] mode sensor) or entered by the user. The
// rise in screen black per lux is the screen's reflectance as the intent
// interpreter's ambient adaptation uses it (key ambient_screen_reflect, cd/m2
// per lux, a renderer profile key: [vprenderer]; merged with the intent
// work in integration round 4):
//
//   ambient_screen_reflect = (black_lit - black_dark) / (lux_lit - lux_dark)
//
// White rises by the same veil (reported as a cross-check). The result is a
// suggested value; Apply writes it to [vprenderer] with phase 1's rules
// (diff first, backups, key only, validated). A VP build without the intent
// ambient adaptation does not know the key: the review then says so and the
// value stays in ambient.cfg in the run folder.

#include "CalibrationRunBase.h"

#include <functional>

namespace Calibration
{
	constexpr const char* AMBIENT_REFLECT_SECTION = "vprenderer";
	constexpr const char* AMBIENT_REFLECT_KEY = "ambient_screen_reflect";

	struct AmbientMeasurement
	{
		bool have = false;
		double lux = 0.0;
		double luxSpreadPercent = 0.0;   // change of the sensor's lux during the measurement
		bool luxFromSensor = false;
		Vec3 black = { 0, 0, 0 };
		Vec3 white = { 0, 0, 0 };
	};

	struct AmbientResult
	{
		bool ok = false;
		std::string error;
		double reflect = 0.0;          // cd/m2 per lux, from black (clamped to the key's 0..1)
		double reflectRaw = 0.0;       // before clamping
		double reflectFromWhite = 0.0; // the cross-check
		double agreementPercent = 0.0; // |white - black| / black estimate
		double veilNits = 0.0;         // the black rise
		double contrastDark = 0.0, contrastLit = 0.0;
		std::vector<std::string> notes;
	};

	// Least lux difference that gives a usable estimate.
	constexpr double AMBIENT_MIN_LUX_DELTA = 5.0;

	AmbientResult ComputeScreenReflect(const AmbientMeasurement& dark, const AmbientMeasurement& lit);
	std::string FormatReflectValue(double reflect);
	std::vector<ConfigEdit> AmbientEdits(const AmbientResult& result);
	std::string FormatAmbientReview(const AmbientMeasurement& dark, const AmbientMeasurement& lit, const AmbientResult& r);
	std::string FormatAmbientCfg(const AmbientResult& r);

	class AmbientCheckRun final : public MeterRun
	{
	public:
		// roomLux: the newest reading of VP's light sensor (false when none);
		// the user's value (Continue with "lux: <n>") takes precedence.
		using LuxSource = std::function<bool(double& lux)>;
		AmbientCheckRun(const CalibrationSettings& settings, const SessionEnvironment& environment,
			std::unique_ptr<IMeter> meter, double meterFloorNits, IPatternSink& patterns, ISessionClock& clock,
			LuxSource roomLux);
		~AmbientCheckRun() override;

		const AmbientMeasurement& Dark() const { return dark; }
		const AmbientMeasurement& Lit() const { return lit; }
		const AmbientResult& Result() const { return result; }
		void SetValidatorForTests(CandidateValidator v) { validator = std::move(v); }

	protected:
		const char* FolderSuffix() const override { return "ambient"; }
		bool OnStart(std::string& error) override;
		void BeginWork() override;
		void StepWork(SessionStep step, Command command) override;

	private:
		bool TakeLux(bool lit, double& lux, bool& fromSensor, std::string& error);
		bool MeasureOne(bool black, Vec3& xyz);

		LuxSource roomLux;
		bool litStage = false;
		int unit = 0;   // 0 black, 1 white
		double luxBefore = 0.0;
		AmbientMeasurement dark, lit;
		AmbientResult result;
	};
}
