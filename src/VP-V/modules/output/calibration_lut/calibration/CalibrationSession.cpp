// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "CalibrationSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

namespace Calibration
{
	namespace
	{
		class SystemClock final : public ISessionClock
		{
		public:
			double Now() override
			{
				return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
			}
			void Sleep(double seconds) override
			{
				if (seconds > 0.0)
					std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(seconds * 1e6)));
			}
			RunTimestamp LocalTime() override
			{
				SYSTEMTIME t{};
				GetLocalTime(&t);
				RunTimestamp r;
				r.year = t.wYear; r.month = t.wMonth; r.day = t.wDay;
				r.hour = t.wHour; r.minute = t.wMinute; r.second = t.wSecond;
				return r;
			}
		};

		std::string Fixed(double v, int decimals)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
			return buffer;
		}

		std::string Escape(const std::string& text)
		{
			std::string out;
			for (const char c : text)
			{
				if (c == '\\') out += "\\\\";
				else if (c == '\n') out += "\\n";
				else if (c == '\r') continue;
				else out.push_back(c);
			}
			return out;
		}

		std::string Unescape(const std::string& text)
		{
			std::string out;
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '\\' && i + 1 < text.size())
				{
					++i;
					out.push_back(text[i] == 'n' ? '\n' : text[i]);
				}
				else
					out.push_back(text[i]);
			}
			return out;
		}
	}

	std::unique_ptr<ISessionClock> CreateSystemClock()
	{
		return std::unique_ptr<ISessionClock>(new SystemClock());
	}

	const char* ToString(SessionStep step)
	{
		switch (step)
		{
		case SessionStep::Idle: return "idle";
		case SessionStep::OpeningMeter: return "opening_meter";
		case SessionStep::MeterCalibration: return "meter_calibration";
		case SessionStep::Placement: return "placement";
		case SessionStep::WarmUp: return "warm_up";
		case SessionStep::Measuring: return "measuring";
		case SessionStep::Fitting: return "fitting";
		case SessionStep::Verifying: return "verifying";
		case SessionStep::Review: return "review";
		case SessionStep::Applied: return "applied";
		case SessionStep::Discarded: return "discarded";
		case SessionStep::Cancelled: return "cancelled";
		case SessionStep::Failed: return "failed";
		case SessionStep::UserAction: return "user_action";
		case SessionStep::Done: return "done";
		}
		return "unknown";
	}

	bool IsFinal(SessionStep step)
	{
		return step == SessionStep::Applied || step == SessionStep::Discarded || step == SessionStep::Cancelled ||
			step == SessionStep::Failed || step == SessionStep::Done;
	}

	PlanKind ToPlanKind(PlanChoice choice)
	{
		switch (choice)
		{
		case PlanChoice::Quick: return PlanKind::Quick;
		case PlanChoice::Cube5: return PlanKind::Cube5;
		case PlanChoice::Cube13: return PlanKind::Cube13;
		case PlanChoice::Thorough: return PlanKind::Thorough;
		default: return PlanKind::Standard;
		}
	}

	std::vector<Patch> BuildSessionPlan(PlanChoice choice, int driftInterval)
	{
		std::vector<Patch> base;
		const std::vector<Patch> grey = GreyscalePatches();
		const std::vector<Patch> prim = PrimarySecondaryPatches();
		base.insert(base.end(), grey.begin(), grey.end());
		base.insert(base.end(), prim.begin(), prim.end());
		const int n = CubeSizeFor(ToPlanKind(choice));
		if (n >= 2)
		{
			const std::vector<Patch> cube = CubePatches(n);
			base.insert(base.end(), cube.begin(), cube.end());
		}
		return InsertDriftReferences(OrderForMeasurement(base), (std::max)(driftInterval, 0));
	}

	// ------------------------------------------------------------- status

	std::string FormatStatus(const SessionStatus& s)
	{
		std::ostringstream out;
		out << "step: " << ToString(s.step) << "\n"
			<< "kind: " << s.kind << "\n"
			<< "instruction: " << Escape(s.instruction) << "\n"
			<< "message: " << Escape(s.message) << "\n"
			<< "patch: " << s.patchIndex << "/" << s.patchCount << "\n"
			<< "verify: " << s.verifyIndex << "/" << s.verifyCount << "\n"
			<< "seconds_remaining: " << Fixed(s.secondsRemaining, 0) << "\n"
			<< "warmup_remaining: " << Fixed(s.warmupRemaining, 0) << "\n"
			<< "reading: " << (s.haveReading ? Fixed(s.lastXyz[0], 6) + " " + Fixed(s.lastXyz[1], 6) + " " +
				Fixed(s.lastXyz[2], 6) : std::string("none")) << "\n"
			<< "format: " << Escape(s.format) << "\n"
			<< "meter: " << Escape(s.meter) << "\n"
			<< "run: " << Escape(s.runFolder) << "\n"
			<< "review: " << Escape(s.review) << "\n"
			<< "diff: " << Escape(s.diff) << "\n"
			<< "backup: " << Escape(s.backup) << "\n"
			<< "verify_mean: " << Fixed(s.verifyMean, 3) << "\n"
			<< "verify_max: " << Fixed(s.verifyMax, 3) << "\n"
			<< "verify_pass: " << (s.verifyPass ? 1 : 0) << "\n"
			<< "measured: " << s.measured << "\n"
			<< "skipped: " << s.skipped << "\n"
			<< "floor_limited: " << s.floorLimited << "\n"
			<< "drift_percent: " << Fixed(s.driftPercent, 2) << "\n";
		return out.str();
	}

	bool ParseStatus(const std::string& text, SessionStatus& s)
	{
		SessionStatus parsed;
		std::istringstream in(text);
		std::string line;
		bool sawStep = false;
		while (std::getline(in, line))
		{
			const size_t colon = line.find(": ");
			if (colon == std::string::npos) continue;
			const std::string key = line.substr(0, colon);
			const std::string value = line.substr(colon + 2);
			if (key == "step")
			{
				for (int i = 0; i <= static_cast<int>(SessionStep::Done); ++i)
					if (value == ToString(static_cast<SessionStep>(i))) { parsed.step = static_cast<SessionStep>(i); sawStep = true; }
			}
			else if (key == "kind") parsed.kind = value;
			else if (key == "instruction") parsed.instruction = Unescape(value);
			else if (key == "message") parsed.message = Unescape(value);
			else if (key == "patch") sscanf_s(value.c_str(), "%d/%d", &parsed.patchIndex, &parsed.patchCount);
			else if (key == "verify") sscanf_s(value.c_str(), "%d/%d", &parsed.verifyIndex, &parsed.verifyCount);
			else if (key == "seconds_remaining") parsed.secondsRemaining = std::atof(value.c_str());
			else if (key == "warmup_remaining") parsed.warmupRemaining = std::atof(value.c_str());
			else if (key == "reading")
				parsed.haveReading = value != "none" &&
					sscanf_s(value.c_str(), "%lf %lf %lf", &parsed.lastXyz[0], &parsed.lastXyz[1], &parsed.lastXyz[2]) == 3;
			else if (key == "format") parsed.format = Unescape(value);
			else if (key == "meter") parsed.meter = Unescape(value);
			else if (key == "run") parsed.runFolder = Unescape(value);
			else if (key == "review") parsed.review = Unescape(value);
			else if (key == "diff") parsed.diff = Unescape(value);
			else if (key == "backup") parsed.backup = Unescape(value);
			else if (key == "verify_mean") parsed.verifyMean = std::atof(value.c_str());
			else if (key == "verify_max") parsed.verifyMax = std::atof(value.c_str());
			else if (key == "verify_pass") parsed.verifyPass = value == "1";
			else if (key == "measured") parsed.measured = std::atoi(value.c_str());
			else if (key == "skipped") parsed.skipped = std::atoi(value.c_str());
			else if (key == "floor_limited") parsed.floorLimited = std::atoi(value.c_str());
			else if (key == "drift_percent") parsed.driftPercent = std::atof(value.c_str());
		}
		if (!sawStep) return false;
		s = parsed;
		return true;
	}

	// ------------------------------------------------------------ session

	CalibrationSession::CalibrationSession(const CalibrationSettings& s, const SessionEnvironment& e,
		std::unique_ptr<IMeter> m, double floor, IPatternSink& p, ISessionClock& c)
		: settings(s), environment(e), meter(std::move(m)), meterFloor(floor > 0.0 ? floor : GenericFloorNits()),
		patterns(p), clock(c)
	{
		status.meter = meter ? meter->Description() : std::string();
		lastTouch = clock.Now();
	}

	CalibrationSession::~CalibrationSession()
	{
		EndPatterns();
		if (meter) meter->Close();
	}

	void CalibrationSession::Start()
	{
		std::lock_guard<std::mutex> guard(mutex);
		if (status.step != SessionStep::Idle) return;
		plan = BuildSessionPlan(settings.plan, settings.driftInterval);
		status.patchCount = static_cast<int>(plan.size());
		status.step = SessionStep::OpeningMeter;
		status.instruction = "Connecting to the meter...";
		lastTouch = clock.Now();
	}

	void CalibrationSession::Continue() { std::lock_guard<std::mutex> g(mutex); pending = Command::Continue; lastTouch = clock.Now(); }
	void CalibrationSession::Skip() { std::lock_guard<std::mutex> g(mutex); pending = Command::Skip; lastTouch = clock.Now(); }
	void CalibrationSession::Cancel()
	{
		{
			std::lock_guard<std::mutex> g(mutex);
			pending = Command::Cancel;
			lastTouch = clock.Now();
		}
		if (meter) meter->Abort();   // a reading in progress returns at once
	}

	bool CalibrationSession::CancelPending()
	{
		std::lock_guard<std::mutex> g(mutex);
		return pending == Command::Cancel;
	}
	void CalibrationSession::Apply() { std::lock_guard<std::mutex> g(mutex); pending = Command::Apply; lastTouch = clock.Now(); }
	void CalibrationSession::Discard() { std::lock_guard<std::mutex> g(mutex); pending = Command::Discard; lastTouch = clock.Now(); }
	void CalibrationSession::Touch() { std::lock_guard<std::mutex> g(mutex); lastTouch = clock.Now(); }

	double CalibrationSession::SecondsSinceTouch()
	{
		std::lock_guard<std::mutex> g(mutex);
		return clock.Now() - lastTouch;
	}

	bool CalibrationSession::Finished() const
	{
		std::lock_guard<std::mutex> g(mutex);
		return IsFinal(status.step);
	}

	SessionStatus CalibrationSession::Status() const
	{
		std::lock_guard<std::mutex> g(mutex);
		return status;
	}

	CalibrationSession::Command CalibrationSession::TakeCommand()
	{
		std::lock_guard<std::mutex> g(mutex);
		const Command c = pending;
		pending = Command::None;
		return c;
	}

	void CalibrationSession::SetStep(SessionStep step, const std::string& instruction)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.step = step;
		status.instruction = instruction;
	}

	void CalibrationSession::SetMessage(const std::string& message)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.message = message;
	}

	void CalibrationSession::EndPatterns()
	{
		if (!patternsOn) return;
		PatternRequest off;
		off.active = false;
		off.generation = ++generation;
		std::string error;
		patterns.Show(off, error);
		patternsOn = false;
		placementPatchShown = false;
	}

	void CalibrationSession::Finish(SessionStep step, const std::string& message)
	{
		EndPatterns();
		if (meter) meter->Close();
		if (!indexWritten && !folder.runDir.empty() && (step == SessionStep::Cancelled || step == SessionStep::Failed))
			WriteIndex(step == SessionStep::Cancelled ? "cancelled" : "failed");
		std::lock_guard<std::mutex> g(mutex);
		status.step = step;
		status.message = message;
		status.instruction = step == SessionStep::Applied ? "Done. The calibration LUTs are in use." :
			step == SessionStep::Discarded ? "Discarded. Your configuration was not changed." :
			step == SessionStep::Cancelled ? "Cancelled. Your configuration was not changed." :
			"Stopped. Your configuration was not changed.";
	}

	void CalibrationSession::Fail(const std::string& message)
	{
		Finish(SessionStep::Failed, message);
	}

	void CalibrationSession::WriteJournal(const JournalEntry& entry)
	{
		journal.push_back(entry);
		if (folder.rawJournal.empty()) return;
		std::string error;
		AppendJournal(folder.rawJournal, FormatJournalLine(entry), error);
	}

	void CalibrationSession::WriteIndex(const char* state)
	{
		IndexEntry e;
		e.display = SanitizeDisplayName(environment.displayName.empty() ? std::string("display") : environment.displayName);
		e.timestamp = stamp.Format();
		e.run = folder.relativeRunDir;
		e.status = state;
		if (verified) { e.verifyMean = verify.meanDeltaE; e.verifyMax = verify.maxDeltaE; }
		std::string error;
		if (AppendIndex(folder.indexJson, e, error)) indexWritten = true;
	}

	double CalibrationSession::ExpectedY(const Vec3& d) const
	{
		if (!(whiteY > 0.0)) return 0.0;
		const double w[3] = { 0.2126, 0.7152, 0.0722 };
		double y = 0.0;
		for (int k = 0; k < 3; ++k) y += w[k] * std::pow((std::max)(d[k], 0.0), 2.2);
		return whiteY * y;
	}

	std::string CalibrationSession::FormatName() const
	{
		std::string bits = format.codeBits == format.containerBits ?
			std::to_string(format.codeBits) + "-bit" : std::to_string(format.codeBits) + "-in-" + std::to_string(format.containerBits);
		std::string text = bits + (format.limitedRange ? " limited" : " full") +
			(format.transfer == SignalTransfer::Pq ? " PQ" : format.transfer == SignalTransfer::Hlg ? " HLG" : " SDR") +
			(format.bt2020 ? " BT.2020" : "");
		if (format.width > 0)
			text += ", " + std::to_string(format.width) + " x " + std::to_string(format.height);
		if (format.refreshHz > 0.0)
			text += " at " + Fixed(format.refreshHz, 3) + " Hz (measured)";
		text += ", window " + Fixed(EffectiveWindowPercent(settings), 0) + "%";
		return text;
	}

	bool CalibrationSession::ShowPatch(const PatchCodes& codes, double expectedY, std::string& error)
	{
		PatternRequest r;
		r.active = true;
		r.generation = ++generation;
		for (int k = 0; k < 3; ++k) r.patch[k] = codes.container[k];
		const double bg = (std::min)((std::max)(settings.backgroundPercent, 0.0), 100.0) / 100.0;
		const PatchCodes background = CodesForSignal(format, { bg, bg, bg });
		for (int k = 0; k < 3; ++k) r.background[k] = background.container[k];
		r.windowAreaPercent = static_cast<float>(EffectiveWindowPercent(settings));
		if (!patterns.Show(r, error)) return false;
		patternsOn = true;
		// Wait for the renderer to present it (twice: the swapchain's own queue).
		const double deadline = clock.Now() + 5.0;
		for (;;)
		{
			PatternOutputInfo info;
			if (patterns.GetOutput(info) && info.presentedGeneration == r.generation && info.presentsOfGeneration >= 2)
				break;
			if (clock.Now() > deadline)
			{
				error = "VP's renderer is not presenting the patches (is video stopped or the renderer restarting?)";
				return false;
			}
			clock.Sleep(0.01);
		}
		SettleOptions so;
		so.configuredMs = settings.settleMs;
		so.displayKind = static_cast<int>(settings.displayKind);
		so.extraLatencySeconds = environment.displayLatencySeconds;
		clock.Sleep(SettleSeconds(so, previousY, expectedY, whiteY));
		return true;
	}

	bool CalibrationSession::MeasurePatch(const Patch& patch, const PatchCodes& codes, double expectedY,
		JournalEntry& entry)
	{
		ReadingPolicy policy;
		policy.tolerance = settings.repeatTolerancePercent / 100.0;
		policy.floorNits = meterFloor;
		policy.normalMaxReads = 1;
		policy.nearBlackMaxReads = settings.nearBlackMaxReads;
		ReadingAccumulator accumulator(policy, whiteY);
		const double start = clock.Now();
		int failures = 0;
		std::string failure;
		bool acceptPartial = false;
		while (!acceptPartial && !accumulator.Done())
		{
			{
				std::lock_guard<std::mutex> g(mutex);
				if (pending == Command::Cancel) return false;
			}
			const MeterResult r = meter->Read();
			if (r.status != MeterStatus::Ok && CancelPending()) return false;
			switch (r.status)
			{
			case MeterStatus::Ok:
			{
				accumulator.Add(r.xyz);
				std::lock_guard<std::mutex> g(mutex);
				status.haveReading = true;
				status.lastXyz = r.xyz;
				break;
			}
			case MeterStatus::ReadFailed:
				failure = r.message;
				if (++failures > 2)
				{
					if (accumulator.Count() > 0) { acceptPartial = true; break; }
					entry.patch = patch;
					entry.codes = codes;
					entry.status = "unreadable";
					entry.note = r.error == SpotreadErrorKind::TooDark ? "too dark for the meter: " + failure : failure;
					entry.seconds = clock.Now() - start;
					return true;
				}
				break;
			case MeterStatus::NeedsUserAction:
			{
				std::lock_guard<std::mutex> g(mutex);
				resumeStep = status.step;
				status.step = SessionStep::MeterCalibration;
				status.instruction = r.message + "\nThen press Continue to resume the measurement.";
				return false;
			}
			case MeterStatus::Timeout:
				if (!reopened)
				{
					reopened = true;
					SetMessage("The meter stopped answering; reconnecting...");
					const MeterResult o = meter->Open();
					if (o.status == MeterStatus::Ok) break;
					if (o.status == MeterStatus::NeedsUserAction)
					{
						std::lock_guard<std::mutex> g(mutex);
						resumeStep = status.step;
						status.step = SessionStep::MeterCalibration;
						status.instruction = o.message + "\nThen press Continue to resume the measurement.";
						return false;
					}
					Fail(o.message);
					return false;
				}
				Fail(r.message);
				return false;
			default:
				Fail(r.message);
				return false;
			}
		}
		entry.patch = patch;
		entry.codes = codes;
		entry.xyz = accumulator.Mean();
		entry.reads = accumulator.Count();
		entry.spread = accumulator.SpreadY();
		entry.floorLimited = accumulator.FloorLimited();
		entry.seconds = clock.Now() - start;
		entry.status = "ok";
		if (failures > 0) entry.note = std::to_string(failures) + " failed reading(s): " + failure;
		(void)expectedY;
		return true;
	}

	void CalibrationSession::DoOpenMeter()
	{
		if (!meter) { Fail("No meter is configured."); return; }
		const MeterResult r = meter->Open();
		if (r.status != MeterStatus::Ok && CancelPending()) return;
		if (r.status == MeterStatus::Ok)
		{
			PatternOutputInfo info;
			std::string reason;
			// A renderer is attached but may still be starting (cold shader cache): wait a little, visibly, before giving up.
			// Without a renderer there is nothing to wait for: fail at once, as before.
			bool haveOutput = patterns.GetOutput(info) && info.available;
			for (int waited = 0; !haveOutput && waited < 50 && !CancelPending() && patterns.RendererAttached(); ++waited)
			{
				if (waited == 0)
				{
					std::lock_guard<std::mutex> g(mutex);
					status.message = "waiting for VP's renderer";
				}
				clock.Sleep(0.1);
				haveOutput = patterns.GetOutput(info) && info.available;
			}
			if (CancelPending()) return;
			if (!patterns.GetOutput(info) || !PatternFormatSupported(info, reason))
			{
				Fail("VP cannot show calibration patches: " + (reason.empty() ? std::string("the renderer is not running") : reason) +
					". Start VP's renderer with any source, then try again.");
				return;
			}
			format = info;
			stamp = clock.LocalTime();
			folder = BuildRunFolder(environment.vpRoot, environment.displayName.empty() ? "display" : environment.displayName, stamp);
			const bool projector = settings.displayKind == DisplayKind::Projector;
			std::string instruction = projector ?
				"Place the meter on a tripod 30-50 cm in front of the screen centre, facing the screen, out of the "
				"projector's beam. The white patch is in the centre." :
				"Lay the meter flat against the centre of the screen (the white patch), with its cable weighted so it "
				"does not tilt.";
			instruction += " Turn off dynamic contrast, eco / energy saving, auto brightness and similar modes, "
				"and darken the room. The reading below updates live: aim for the highest steady value, then press "
				"Continue.";
			std::lock_guard<std::mutex> g(mutex);
			status.format = FormatName();
			status.runFolder = folder.relativeRunDir;
			status.message = r.message;
			status.step = SessionStep::Placement;
			status.instruction = instruction;
			return;
		}
		if (r.status == MeterStatus::NeedsUserAction)
		{
			std::lock_guard<std::mutex> g(mutex);
			resumeStep = SessionStep::OpeningMeter;
			status.step = SessionStep::MeterCalibration;
			status.instruction = r.message + "\nThen press Continue.";
			return;
		}
		Fail(r.message);
	}

	void CalibrationSession::DoPlacement()
	{
		const PatchCodes white = CodesForSignal(format, { 1.0, 1.0, 1.0 });
		std::string error;
		if (!patternsOn || !placementPatchShown)
		{
			if (!ShowPatch(white, 0.0, error)) { Fail(error); return; }
			placementPatchShown = true;
		}
		const MeterResult r = meter->Read();
		if (r.status != MeterStatus::Ok && CancelPending()) return;
		if (r.status == MeterStatus::Ok)
		{
			const Chromaticity c = XyzToXy(r.xyz);
			std::lock_guard<std::mutex> g(mutex);
			status.haveReading = true;
			status.lastXyz = r.xyz;
			status.message = "Aim reading: Y " + Fixed(r.xyz[1], 3) + " cd/m2, x " + Fixed(c.x, 4) + ", y " + Fixed(c.y, 4);
			return;
		}
		if (r.status == MeterStatus::ReadFailed) { SetMessage("Aim reading failed: " + r.message); return; }
		if (r.status == MeterStatus::NeedsUserAction)
		{
			std::lock_guard<std::mutex> g(mutex);
			resumeStep = SessionStep::Placement;
			status.step = SessionStep::MeterCalibration;
			status.instruction = r.message + "\nThen press Continue.";
			return;
		}
		Fail(r.message);
	}

	void CalibrationSession::DoWarmUp()
	{
		if (warmupStart < 0.0)
		{
			warmupStart = clock.Now();
			// A mid-grey keeps the display at a typical load while it warms.
			std::string error;
			if (!ShowPatch(CodesForSignal(format, { 0.5, 0.5, 0.5 }), 0.0, error)) { Fail(error); return; }
		}
		const double total = settings.warmupMinutes * 60.0;
		const double remaining = total - (clock.Now() - warmupStart);
		{
			std::lock_guard<std::mutex> g(mutex);
			status.warmupRemaining = (std::max)(remaining, 0.0);
		}
		if (remaining <= 0.0)
		{
			phaseStart = clock.Now();
			SetStep(SessionStep::Measuring, "Measuring. Keep the room dark and the meter still.");
			return;
		}
		clock.Sleep((std::min)(remaining, 1.0));
	}

	void CalibrationSession::DoMeasure()
	{
		if (planIndex >= plan.size())
		{
			SetStep(SessionStep::Fitting, "Building the display model and the 3D LUTs...");
			return;
		}
		const Patch& patch = plan[planIndex];
		JournalEntry entry;
		entry.index = static_cast<int>(planIndex);
		entry.phase = "measure";
		if (!PatchRepresentable(format, patch))
		{
			entry.patch = patch;
			entry.codes = CodesForSignal(format, { patch.rgb[0], patch.rgb[1], patch.rgb[2] });
			entry.status = "skipped";
			entry.note = "not representable in this output format (full range has no BTB / WTW codes)";
			WriteJournal(entry);
			++planIndex;
			std::lock_guard<std::mutex> g(mutex);
			++status.skipped;
			status.patchIndex = static_cast<int>(planIndex);
			return;
		}
		const PatchCodes codes = CodesForSignal(format, { patch.rgb[0], patch.rgb[1], patch.rgb[2] });
		const double expected = ExpectedY(codes.device);
		std::string error;
		if (!ShowPatch(codes, expected, error)) { Fail(error); return; }
		if (!MeasurePatch(patch, codes, expected, entry)) return;   // cancel, meter calibration or failure
		WriteJournal(entry);
		++planIndex;
		std::lock_guard<std::mutex> g(mutex);
		if (entry.status == "ok")
		{
			previousY = entry.xyz[1];
			if (patch.kind == PatchKind::DriftReference)
			{
				if (!(whiteY > 0.0)) whiteY = entry.xyz[1];
				driftReferences.push_back(std::make_pair(entry.index, entry.xyz[1]));
				status.driftPercent = whiteY > 0.0 ? (entry.xyz[1] / whiteY - 1.0) * 100.0 : 0.0;
			}
			else if (patch.usedForFit)
			{
				Measurement m;
				m.device = codes.device;
				m.xyz = entry.xyz;
				m.kind = patch.kind;
				measurements.push_back(m);
				measurementPlanIndex.push_back(entry.index);
			}
			++status.measured;
			if (entry.floorLimited) ++status.floorLimited;
		}
		else
			++status.skipped;
		status.patchIndex = static_cast<int>(planIndex);
		const double elapsed = clock.Now() - phaseStart;
		status.secondsRemaining = planIndex > 0 ? elapsed / planIndex * (plan.size() - planIndex) : 0.0;
	}

	std::vector<Measurement> CalibrationSession::DriftCorrected() const
	{
		std::vector<Measurement> out = measurements;
		if (driftReferences.size() < 2 || !(driftReferences.front().second > 0.0)) return out;
		double worst = 0.0;
		for (const auto& r : driftReferences)
			worst = (std::max)(worst, std::fabs(r.second / driftReferences.front().second - 1.0));
		if (worst < 0.002) return out;
		const double reference = driftReferences.front().second;
		for (size_t i = 0; i < out.size(); ++i)
		{
			const int at = measurementPlanIndex[i];
			size_t b = 0;
			while (b + 1 < driftReferences.size() && driftReferences[b + 1].first < at) ++b;
			const size_t a = b;
			const size_t c = (std::min)(b + 1, driftReferences.size() - 1);
			double y = driftReferences[a].second;
			if (c != a && driftReferences[c].first != driftReferences[a].first)
			{
				const double t = static_cast<double>(at - driftReferences[a].first) /
					(driftReferences[c].first - driftReferences[a].first);
				y = driftReferences[a].second + (std::min)((std::max)(t, 0.0), 1.0) * (driftReferences[c].second - driftReferences[a].second);
			}
			if (y > 0.0) out[i].xyz = Scale(out[i].xyz, reference / y);
		}
		return out;
	}

	void CalibrationSession::DoFit()
	{
		EndPatterns();   // video returns while the CPU works
		std::string error;
		ForwardModelOptions options;
		if (!model.Fit(DriftCorrected(), options, &error))
		{
			Fail("The display model could not be fitted: " + error);
			return;
		}
		TargetOptions base;
		base.transfer = settings.targetTransfer == TargetTransferChoice::Gamma ? TargetTransfer::Gamma : TargetTransfer::Bt1886;
		base.gamma = settings.targetGamma;
		base.whiteFraction = (std::min)((std::max)(settings.targetWhitePercent, 25.0), 100.0) / 100.0;
		LutOptions lutOptions;
		lutOptions.size = settings.lutSize;
		std::string notes;
		int written = 0;
		for (int s = 0; s < 3; ++s)
		{
			TargetOptions o = base;
			o.primaries = SlotPrimaries(static_cast<LutSlot>(s));
			std::string slotError;
			if (!targets[s].Build(model, o, &slotError))
			{
				notes += std::string(SlotConfigKey(static_cast<LutSlot>(s))) + ": " + slotError + "\n";
				continue;
			}
			luts[s] = BuildLut(model, targets[s], lutOptions);
			std::vector<std::string> comments = {
				"VideoProcessor automatic calibration " + stamp.Format(),
				"display: " + SanitizeDisplayName(environment.displayName.empty() ? std::string("display") : environment.displayName),
				"output: " + FormatName(),
				"meter: " + status.meter,
			};
			if (!WriteTextFile(folder.cube[s], FormatCube(luts[s], "VP calibration " + std::string(SlotConfigKey(static_cast<LutSlot>(s))),
				comments), error))
			{
				Fail("The LUT could not be written: " + error);
				return;
			}
			slotWritten[s] = true;
			++written;
		}
		if (written == 0)
		{
			Fail("No LUT could be built: " + notes);
			return;
		}
		verifySlot = format.bt2020 && slotWritten[2] ? 2 : slotWritten[0] ? 0 : slotWritten[1] ? 1 : 2;
		const DisplayProfileValues profile = ProfileFromModel(model, targets[verifySlot], static_cast<int>(format.codeBits),
			FormatName());
		WriteTextFile(folder.displayProfile, FormatDisplayProfile(environment.displayName.empty() ? "display" :
			SanitizeDisplayName(environment.displayName), profile), error);
		if (!notes.empty()) SetMessage(notes);
		if (settings.verifyAfter)
		{
			verifyPlan = VerificationPatches(20, 20261017u, CubeSizeFor(ToPlanKind(settings.plan)));
			verifyIndex = 0;
			phaseStart = clock.Now();
			std::lock_guard<std::mutex> g(mutex);
			status.verifyCount = static_cast<int>(verifyPlan.size());
			status.step = SessionStep::Verifying;
			status.instruction = "Verifying: measuring colours through the new LUT (not used for the fit).";
			return;
		}
		DoReview();
	}

	void CalibrationSession::DoVerify()
	{
		if (verifyIndex >= verifyPlan.size())
		{
			std::vector<std::pair<double, Vec3>> greys;
			for (size_t i = 0; i < verifyEntries.size(); ++i)
				if (verifyEntries[i].kind == PatchKind::VerifyGrey)
				{
					for (const Patch& p : verifyPlan)
						if (p.label == verifyEntries[i].label) greys.push_back(std::make_pair(p.rgb[0], verifyEntries[i].measuredXyz));
				}
			verify = BuildVerifyReport(verifyEntries, targets[verifySlot].White(), targets[verifySlot].Black(), greys);
			verified = true;
			std::string error;
			WriteTextFile(folder.verifyJson, FormatVerifyJson(verify), error);
			DoReview();
			return;
		}
		const Patch& patch = verifyPlan[verifyIndex];
		const Vec3 signal = { patch.rgb[0], patch.rgb[1], patch.rgb[2] };
		const Vec3 device = luts[verifySlot].Sample(signal);
		const PatchCodes codes = CodesForSignal(format, device);
		JournalEntry entry;
		entry.index = static_cast<int>(verifyIndex);
		entry.phase = "verify";
		const double expected = ExpectedY(codes.device);
		std::string error;
		if (!ShowPatch(codes, expected, error)) { Fail(error); return; }
		if (!MeasurePatch(patch, codes, expected, entry)) return;
		WriteJournal(entry);
		++verifyIndex;
		if (entry.status == "ok")
		{
			previousY = entry.xyz[1];
			VerifyEntry v;
			v.label = patch.label;
			v.kind = patch.kind;
			// The target the LUT aims for: the slot's colour, or its chroma-clipped
			// reachable colour when the display cannot show it (as the design's
			// verify step and CalibrationCoreTests measure it).
			Vec3 aimed;
			bool clipped = false;
			MapTargetToDevice(model, targets[verifySlot], signal, aimed, &clipped);
			v.targetXyz = clipped ? model.Forward(aimed) : targets[verifySlot].Xyz(signal);
			v.measuredXyz = entry.xyz;
			verifyEntries.push_back(v);
		}
		std::lock_guard<std::mutex> g(mutex);
		status.verifyIndex = static_cast<int>(verifyIndex);
		const double elapsed = clock.Now() - phaseStart;
		status.secondsRemaining = verifyIndex > 0 ? elapsed / verifyIndex * (verifyPlan.size() - verifyIndex) : 0.0;
	}

	void CalibrationSession::DoReview()
	{
		EndPatterns();
		if (meter) meter->Close();   // the meter is not needed any more
		std::string section, error;
		const ConfigDiff diff = PreviewCalibrationEdits(environment.configPath, environment.activeColorSection, folder,
			slotWritten, section, error);
		const Vec3 white = model.NativeWhite();
		const Chromaticity wxy = XyzToXy(white);
		std::ostringstream review;
		review << "Display: white " << Fixed(white[1], 2) << " cd/m2 (x " << Fixed(wxy.x, 4) << ", y " << Fixed(wxy.y, 4)
			<< "), black " << Fixed(model.Black()[1], 4) << " cd/m2";
		if (model.Black()[1] > 0.0) review << ", contrast " << Fixed(white[1] / model.Black()[1], 0) << ":1";
		review << ".\nGreyscale gamma (effective): " << Fixed(EffectiveGamma(model), 2)
			<< "; additivity error " << Fixed(model.AdditivityError() * 100.0, 2) << "%.";
		review << "\nLUTs (" << settings.lutSize << "^3):";
		for (int s = 0; s < 3; ++s)
			if (slotWritten[s])
				review << " " << SlotFileName(static_cast<LutSlot>(s)) << " (" << luts[s].clippedNodes << " gamut-clipped nodes)";
		if (verified)
			review << "\nVerification through the LUT: dE2000 mean " << Fixed(verify.meanDeltaE, 2) << ", max "
				<< Fixed(verify.maxDeltaE, 2) << " (" << verify.worstLabel << ") - "
				<< (verify.pass ? "pass" : "outside the target (mean < 1.5, max < 3)") << ".";
		{
			std::lock_guard<std::mutex> g(mutex);
			review << "\nPatches: " << status.measured << " measured, " << status.skipped << " skipped, "
				<< status.floorLimited << " below the meter's low-light floor.";
			if (std::fabs(status.driftPercent) >= 0.1)
				review << " Display drift during the run: " << Fixed(status.driftPercent, 2) << "% (corrected).";
		}
		std::string report = FormatReportHtml(environment.displayName, model, verified ? &verify : nullptr, journal);
		WriteTextFile(folder.reportHtml, report, error);
		std::lock_guard<std::mutex> g(mutex);
		status.step = SessionStep::Review;
		status.review = review.str();
		status.diff = diff.ok ? diff.Format() : "Apply is not possible: " + error;
		status.verifyMean = verified ? verify.meanDeltaE : -1.0;
		status.verifyMax = verified ? verify.maxDeltaE : -1.0;
		status.verifyPass = verified && verify.pass;
		status.instruction = diff.ok ?
			"Review the result. Apply attaches the new LUTs to [" + section + "] (after backing up your configuration); "
			"Discard leaves your configuration as it is." :
			"Review the result. " + error + ". Discard to finish (the measurements stay in the run folder).";
		status.secondsRemaining = 0.0;
	}

	void CalibrationSession::DoApply()
	{
		const ApplyOutcome o = ApplyCalibrationToConfig(environment.configPath, environment.activeColorSection, folder,
			slotWritten, clock.LocalTime());
		if (!o.ok)
		{
			SetMessage("Apply failed: " + o.error + ". Your configuration was not changed.");
			return;
		}
		WriteIndex("applied");
		{
			std::lock_guard<std::mutex> g(mutex);
			status.backup = "Backup: " + o.backupPath + "\nRun copy: " + o.runCopyPath;
			status.diff = o.diff.Format();
		}
		Finish(SessionStep::Applied, "Applied to [" + o.section + "]:\n" + o.diff.Format());
	}

	void CalibrationSession::DoDiscard()
	{
		WriteIndex("discarded");
		Finish(SessionStep::Discarded, "The run was discarded; its measurements stay in " + folder.relativeRunDir + ".");
	}

	bool CalibrationSession::Step()
	{
		SessionStep step;
		{
			std::lock_guard<std::mutex> g(mutex);
			step = status.step;
		}
		if (IsFinal(step)) return false;
		const Command command = TakeCommand();
		if (command == Command::Cancel)
		{
			Finish(SessionStep::Cancelled, "Cancelled by the user.");
			return false;
		}
		switch (step)
		{
		case SessionStep::Idle:
			clock.Sleep(0.05);
			break;
		case SessionStep::OpeningMeter:
			DoOpenMeter();
			break;
		case SessionStep::MeterCalibration:
			if (command == Command::Continue)
			{
				const MeterResult r = meter->Continue();
				if (r.status != MeterStatus::Ok && CancelPending()) break;
				if (r.status == MeterStatus::Ok)
				{
					SessionStep next = resumeStep;
					if (next == SessionStep::OpeningMeter)
					{
						// The meter is calibrated: go on as after a plain open.
						DoOpenMeter();
						break;
					}
					SetStep(next, next == SessionStep::Placement ? "Place the meter, then press Continue." :
						"Measuring. Keep the room dark and the meter still.");
				}
				else if (r.status == MeterStatus::NeedsUserAction)
					SetStep(SessionStep::MeterCalibration, r.message + "\nThen press Continue.");
				else
					Fail(r.message);
			}
			else
				clock.Sleep(0.05);
			break;
		case SessionStep::Placement:
			if (command == Command::Continue)
			{
				phaseStart = clock.Now();
				if (settings.warmupMinutes > 0)
					SetStep(SessionStep::WarmUp, "Warming up the display (" + std::to_string(settings.warmupMinutes) +
						" minutes). Skip when it has already been on for that long.");
				else
					SetStep(SessionStep::Measuring, "Measuring. Keep the room dark and the meter still.");
			}
			else
				DoPlacement();
			break;
		case SessionStep::WarmUp:
			if (command == Command::Skip || command == Command::Continue)
			{
				phaseStart = clock.Now();
				SetStep(SessionStep::Measuring, "Measuring. Keep the room dark and the meter still.");
			}
			else
				DoWarmUp();
			break;
		case SessionStep::Measuring:
			DoMeasure();
			break;
		case SessionStep::Fitting:
			DoFit();
			break;
		case SessionStep::Verifying:
			DoVerify();
			break;
		case SessionStep::Review:
			if (command == Command::Apply) DoApply();
			else if (command == Command::Discard) DoDiscard();
			else clock.Sleep(0.05);
			break;
		default:
			break;
		}
		return !Finished();
	}
}
