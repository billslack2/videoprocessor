// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "AmbientCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace Calibration
{
	AmbientResult ComputeScreenReflect(const AmbientMeasurement& d, const AmbientMeasurement& l)
	{
		AmbientResult r;
		if (!d.have || !l.have) { r.error = "both the dark and the lit measurement are needed"; return r; }
		const double dl = l.lux - d.lux;
		if (!(dl >= AMBIENT_MIN_LUX_DELTA))
		{
			char text[200];
			std::snprintf(text, sizeof(text), "the room light changed by %.1f lux between the two measurements; at least "
				"%.0f lux more with the lights on is needed", dl, AMBIENT_MIN_LUX_DELTA);
			r.error = text;
			return r;
		}
		r.veilNits = l.black[1] - d.black[1];
		r.reflectRaw = r.veilNits / dl;
		r.reflectFromWhite = (l.white[1] - d.white[1]) / dl;
		r.reflect = (std::min)((std::max)(r.reflectRaw, 0.0), 1.0);
		if (r.reflectRaw < 0.0)
			r.notes.push_back("Black was lower with the lights on (meter noise or a moved meter); the suggestion is 0.");
		if (r.reflectRaw > 1.0)
			r.notes.push_back("The black rise is above 1 cd/m2 per lux (the key's limit); check the lux values.");
		if (std::fabs(r.reflectRaw) > 1e-9)
			r.agreementPercent = std::fabs(r.reflectFromWhite - r.reflectRaw) / std::fabs(r.reflectRaw) * 100.0;
		if (r.agreementPercent > 50.0)
			r.notes.push_back("White rose by a different amount than black (" + FixedText(r.agreementPercent, 0) +
				"% apart): the display may have changed its own light with the room, or the room light was not steady.");
		if (d.black[1] > 0.0) r.contrastDark = d.white[1] / d.black[1];
		if (l.black[1] > 0.0) r.contrastLit = l.white[1] / l.black[1];
		if (d.luxSpreadPercent > 20.0 || l.luxSpreadPercent > 20.0)
			r.notes.push_back("The room light changed by more than 20% while a measurement ran; repeat with steady light.");
		r.ok = true;
		return r;
	}

	std::string FormatReflectValue(double reflect)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%.5f", reflect);
		std::string s = text;
		while (s.size() > 3 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
		return s;
	}

	std::vector<ConfigEdit> AmbientEdits(const AmbientResult& r)
	{
		std::vector<ConfigEdit> edits;
		if (r.ok) edits.push_back({ AMBIENT_REFLECT_SECTION, AMBIENT_REFLECT_KEY, FormatReflectValue(r.reflect) });
		return edits;
	}

	std::string FormatAmbientReview(const AmbientMeasurement& d, const AmbientMeasurement& l, const AmbientResult& r)
	{
		std::ostringstream out;
		auto line = [&](const char* name, const AmbientMeasurement& m)
		{
			out << name << ": " << FixedText(m.lux, 1) << " lux (" << (m.luxFromSensor ? "light sensor" : "entered") << "), black "
				<< FixedText(m.black[1], 4) << " cd/m2, white " << FixedText(m.white[1], 2) << " cd/m2";
			if (m.black[1] > 0.0) out << ", contrast " << FixedText(m.white[1] / m.black[1], 0) << ":1";
			out << ".\n";
		};
		out << "Ambient-light check.\n";
		line("Lights off", d);
		line("Lights on", l);
		if (r.ok)
		{
			out << "Screen black rose by " << FixedText(r.veilNits, 4) << " cd/m2: ambient_screen_reflect "
				<< FormatReflectValue(r.reflect) << " cd/m2 per lux (white gives " << FormatReflectValue(r.reflectFromWhite) << ").";
		}
		else
			out << "No suggestion: " << r.error << ".";
		for (const std::string& n : r.notes) out << "\n" << n;
		return out.str();
	}

	std::string FormatAmbientCfg(const AmbientResult& r)
	{
		std::ostringstream out;
		out << "# Ambient-light check (VP automatic calibration, phase 2): the suggested value.\n"
			<< "# A renderer profile key of the intent ambient adaptation (cd/m2 per lux).\n";
		if (r.ok) out << "[" << AMBIENT_REFLECT_SECTION << "]\n" << AMBIENT_REFLECT_KEY << ": " << FormatReflectValue(r.reflect) << "\n";
		else out << "# not measured: " << r.error << "\n";
		return out.str();
	}

	AmbientCheckRun::AmbientCheckRun(const CalibrationSettings& s, const SessionEnvironment& e, std::unique_ptr<IMeter> m,
		double floor, IPatternSink& p, ISessionClock& c, LuxSource lux)
		: MeterRun("ambient", s, e, std::move(m), floor, p, c), roomLux(std::move(lux))
	{
		// Integration round 4: the intent ambient adaptation is merged, so VP's own
		// validation knows ambient_screen_reflect (renderer key, 0..1 cd/m2 per lux)
		// and Apply writes it. A value VP rejects (out of range) is refused with the
		// reason; the suggestion stays in ambient.cfg in the run folder.
		CandidateValidator base = DefaultCandidateValidator();
		validator = [base](const ConfigEditorCore::ConfigDocument& document, std::string& error)
		{
			if (base(document, error)) return true;
			if (error.find(AMBIENT_REFLECT_KEY) != std::string::npos)
				error = "VP refused the suggested ambient_screen_reflect; the value is kept in ambient.cfg in the run "
					"folder (" + error + ")";
			return false;
		};
	}

	AmbientCheckRun::~AmbientCheckRun()
	{
		Shutdown();
	}

	bool AmbientCheckRun::OnStart(std::string&)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.patchCount = 4;
		return true;
	}

	void AmbientCheckRun::BeginWork()
	{
		litStage = false;
		unit = 0;
		SetProgress(0, 4);
		SetStep(SessionStep::UserAction, "Switch the room lights OFF, as when you watch, then press Continue. "
			"(Without VP's light sensor, the dark room counts as 0 lux unless you enter a value.)");
	}

	bool AmbientCheckRun::TakeLux(bool litNow, double& lux, bool& fromSensor, std::string& error)
	{
		std::string entered;
		{
			std::lock_guard<std::mutex> g(mutex);
			ArgumentValue(arguments, "lux", entered);
			arguments.clear();
		}
		if (!entered.empty())
		{
			char* end = nullptr;
			const double v = std::strtod(entered.c_str(), &end);
			if (end && *end == '\0' && std::isfinite(v) && v >= 0.0 && v <= 200000.0)
			{
				lux = v;
				fromSensor = false;
				return true;
			}
			error = "the room light value '" + entered + "' is not a number of lux";
			return false;
		}
		double sensed = 0.0;
		if (roomLux && roomLux(sensed))
		{
			lux = sensed;
			fromSensor = true;
			return true;
		}
		if (!litNow)
		{
			lux = 0.0;
			fromSensor = false;
			return true;
		}
		error = "no light sensor reading: enter the room's lux with the lights on (a phone light meter app is close "
			"enough), then press Continue";
		return false;
	}

	bool AmbientCheckRun::MeasureOne(bool black, Vec3& xyz)
	{
		std::string error;
		const double s = black ? 0.0 : 1.0;
		if (!ShowPatch(Signal(s, s, s), black ? 0.0 : whiteY, error)) { Fail(error); return false; }
		double elapsed = 0.0;
		int reads = 0;
		bool floorLimited = false;
		std::string note;
		const ReadOutcome o = MeasureCodes(elapsed, xyz, reads, floorLimited, note);
		if (o == ReadOutcome::Stop) return false;
		if (o == ReadOutcome::Unreadable) { Fail("The meter could not read the " + std::string(black ? "black" : "white") +
			" patch: " + note); return false; }
		WriteRaw(std::string("{\"phase\":\"") + (litStage ? "lit" : "dark") + "\",\"patch\":\"" + (black ? "black" : "white") +
			"\",\"xyz\":[" + JsonNumber(xyz[0], 8) + "," + JsonNumber(xyz[1], 8) + "," + JsonNumber(xyz[2], 8) + "],\"reads\":" +
			std::to_string(reads) + ",\"floor_limited\":" + (floorLimited ? "true" : "false") + "}");
		return true;
	}

	void AmbientCheckRun::StepWork(SessionStep step, Command command)
	{
		if (step == SessionStep::UserAction)
		{
			if (command != Command::Continue) { clock.Sleep(0.05); return; }
			AmbientMeasurement& m = litStage ? lit : dark;
			std::string error;
			if (!TakeLux(litStage, m.lux, m.luxFromSensor, error)) { SetMessage(error); return; }
			luxBefore = m.lux;
			unit = 0;
			SetMessage(std::string("Room light ") + FixedText(m.lux, 1) + " lux; measuring black and white...");
			SetStep(SessionStep::Measuring, litStage ? "Measuring with the lights on. Keep the room as it is." :
				"Measuring with the lights off. Keep the room dark.");
			return;
		}
		if (step == SessionStep::Measuring)
		{
			AmbientMeasurement& m = litStage ? lit : dark;
			if (!MeasureOne(unit == 0, unit == 0 ? m.black : m.white)) return;
			if (unit == 0) { ++unit; SetProgress((litStage ? 2 : 0) + 1, 4); return; }
			// The sensor's lux again: a change during the measurement is reported.
			double after = 0.0;
			if (m.luxFromSensor && roomLux && roomLux(after) && luxBefore > 0.0)
			{
				m.luxSpreadPercent = std::fabs(after - luxBefore) / luxBefore * 100.0;
				m.lux = 0.5 * (luxBefore + after);
			}
			m.have = true;
			SetProgress(litStage ? 4 : 2, 4);
			if (!litStage)
			{
				litStage = true;
				SetStep(SessionStep::UserAction, "Now switch the room lights ON as you normally have them when lit, then "
					"press Continue. Without VP's light sensor, enter the room's lux first.");
				return;
			}
			SetStep(SessionStep::Fitting, "Working out the screen's reflectance...");
			return;
		}
		if (step == SessionStep::Fitting)
		{
			EndPatterns();
			result = ComputeScreenReflect(dark, lit);
			std::string error;
			WriteTextFile(folder.runDir + "\\ambient.cfg", FormatAmbientCfg(result), error);
			const std::string review = FormatAmbientReview(dark, lit, result);
			WriteRaw("{\"result\":{\"ok\":" + std::string(result.ok ? "true" : "false") + ",\"reflect\":" +
				JsonNumber(result.reflect, 6) + ",\"reflect_white\":" + JsonNumber(result.reflectFromWhite, 6) +
				",\"dark_lux\":" + JsonNumber(dark.lux, 6) + ",\"lit_lux\":" + JsonNumber(lit.lux, 6) + "}}");
			WriteTextFile(folder.reportHtml, "<!doctype html><html><head><meta charset=\"utf-8\"><title>VP ambient-light "
				"check</title></head><body style=\"font-family:Segoe UI,Arial,sans-serif\"><pre>" + review +
				"</pre></body></html>\n", error);
			SetReview(review, AmbientEdits(result), "[vprenderer] ambient_screen_reflect");
			return;
		}
		clock.Sleep(0.05);
	}
}
