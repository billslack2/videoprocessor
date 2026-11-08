// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "CalibrationRunBase.h"

#include <VideoProcessor-ConfigCore/ConfigEditorCore.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace Calibration
{
	namespace
	{
		std::wstring WideText(const std::string& utf8)
		{
			if (utf8.empty()) return std::wstring();
			int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			const UINT page = n > 0 ? CP_UTF8 : CP_ACP;
			n = MultiByteToWideChar(page, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
			std::wstring out(static_cast<size_t>((std::max)(n, 0)), L'\0');
			if (n > 0) MultiByteToWideChar(page, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
			return out;
		}

		std::string NarrowText(const std::wstring& text)
		{
			if (text.empty()) return std::string();
			const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<size_t>((std::max)(n, 0)), '\0');
			if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
			return out;
		}
	}

	std::string FixedText(double v, int decimals)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
		return buffer;
	}

	std::string JsonNumber(double v, int decimals)
	{
		if (!std::isfinite(v)) return "null";
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.*g", decimals, v);
		return buffer;
	}

	std::string JsonText(const std::string& text)
	{
		std::string out = "\"";
		for (const char c : text)
		{
			if (c == '"') out += "\\\"";
			else if (c == '\\') out += "\\\\";
			else if (c == '\n') out += "\\n";
			else if (c == '\r') out += "\\r";
			else if (c == '\t') out += "\\t";
			else if (static_cast<unsigned char>(c) < 0x20)
			{
				char buffer[8];
				std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
				out += buffer;
			}
			else out.push_back(c);
		}
		return out + "\"";
	}

	RunFolder BuildPhase2RunFolder(const std::string& vpRoot, const std::string& displayName, const RunTimestamp& stamp,
		const std::string& suffix)
	{
		RunFolder f = BuildRunFolder(vpRoot, displayName, stamp);
		f.runDir += "-" + suffix;
		f.relativeRunDir += "-" + suffix;
		f.rawJournal = f.runDir + "\\raw.jsonl";
		f.displayProfile.clear();
		for (int s = 0; s < 3; ++s) { f.cube[s].clear(); f.cubeConfigValue[s].clear(); }
		f.verifyJson = f.runDir + "\\verify.json";
		f.reportHtml = f.runDir + "\\report.html";
		f.beforeDir = f.runDir + "\\before";
		return f;
	}

	CandidateValidator DefaultCandidateValidator()
	{
		return [](const ConfigEditorCore::ConfigDocument& document, std::string& error)
		{
			std::wstring validation;
			if (ConfigEditorCore::ValidateCandidate(document, validation)) return true;
			error = NarrowText(validation);
			return false;
		};
	}

	ConfigDiff PreviewKeyEdits(const std::string& configPath, const std::vector<ConfigEdit>& edits,
		const CandidateValidator& validate, std::string& error)
	{
		ConfigDiff diff;
		ConfigEditorCore::ConfigDocument document;
		std::wstring loadError;
		if (!document.Load(WideText(configPath), loadError))
		{
			error = "could not read the configuration: " + NarrowText(loadError);
			diff.ok = false;
			return diff;
		}
		diff = ApplyKeyOnlyEdits(document, edits);
		if (!diff.ok)
		{
			error = "the keys could not be set";
			return diff;
		}
		std::string validation;
		const CandidateValidator check = validate ? validate : DefaultCandidateValidator();
		if (!check(document, validation))
		{
			error = "VP would reject the edited configuration: " + validation;
			diff.ok = false;
		}
		return diff;
	}

	ApplyOutcome ApplyKeyEdits(const std::string& configPath, const std::vector<ConfigEdit>& edits,
		const RunFolder& folder, const RunTimestamp& stamp, const CandidateValidator& validate)
	{
		ApplyOutcome o;
		ConfigEditorCore::ConfigDocument document;
		std::wstring loadError;
		if (!document.Load(WideText(configPath), loadError))
		{
			o.error = "could not read the configuration: " + NarrowText(loadError);
			return o;
		}
		if (!edits.empty()) o.section = edits.front().section;
		o.diff = ApplyKeyOnlyEdits(document, edits);
		if (!o.diff.ok)
		{
			o.error = "the keys could not be set";
			return o;
		}
		std::string validation;
		const CandidateValidator check = validate ? validate : DefaultCandidateValidator();
		if (!check(document, validation))
		{
			o.error = "VP would reject the edited configuration: " + validation;
			return o;
		}
		o.backupPath = BackupConfigBeside(configPath, stamp, o.error);
		if (o.backupPath.empty()) return o;
		if (!CopyConfigToRun(folder, configPath, o.runCopyPath, o.error)) return o;
		if (!o.diff.Changed())
		{
			o.ok = true;
			return o;
		}
		ConfigEditorCore::SaveResult saved;
		std::wstring saveError;
		if (!ConfigEditorCore::SaveSafely(document, saved, saveError))
		{
			o.error = "the configuration could not be saved: " + NarrowText(saveError);
			return o;
		}
		o.ok = true;
		return o;
	}

	bool ArgumentValue(const std::string& arguments, const std::string& key, std::string& value)
	{
		std::istringstream in(arguments);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const size_t colon = line.find(':');
			if (colon == std::string::npos) continue;
			std::string k = line.substr(0, colon);
			while (!k.empty() && k.back() == ' ') k.pop_back();
			if (k != key) continue;
			std::string v = line.substr(colon + 1);
			while (!v.empty() && v.front() == ' ') v.erase(v.begin());
			while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
			value = v;
			return true;
		}
		return false;
	}

	// ------------------------------------------------------------ MeterRun

	MeterRun::MeterRun(const char* kind, const CalibrationSettings& s, const SessionEnvironment& e,
		std::unique_ptr<IMeter> m, double floor, IPatternSink& p, ISessionClock& c)
		: settings(s), environment(e), meter(std::move(m)), meterFloor(floor > 0.0 ? floor : GenericFloorNits()),
		patterns(p), clock(c), validator(DefaultCandidateValidator())
	{
		status.kind = kind;
		status.meter = meter ? meter->Description() : std::string();
		lastTouch = clock.Now();
	}

	MeterRun::~MeterRun()
	{
		// Derived destructors ran Shutdown(); this is the last line of defence.
		EndPatterns();
		if (meter) meter->Close();
	}

	void MeterRun::Shutdown()
	{
		if (shutDown) return;
		shutDown = true;
		SessionStep step;
		{
			std::lock_guard<std::mutex> g(mutex);
			step = status.step;
		}
		OnFinishing(IsFinal(step) ? step : SessionStep::Cancelled);
		EndPatterns();
		if (meter) meter->Close();
	}

	void MeterRun::Start()
	{
		std::string error;
		{
			std::lock_guard<std::mutex> g(mutex);
			if (status.step != SessionStep::Idle) return;
		}
		if (!OnStart(error))
		{
			std::lock_guard<std::mutex> g(mutex);
			status.step = SessionStep::Failed;
			status.message = error;
			status.instruction = "Stopped. Your configuration was not changed.";
			return;
		}
		std::lock_guard<std::mutex> g(mutex);
		status.step = SessionStep::OpeningMeter;
		status.instruction = "Connecting to the meter...";
		lastTouch = clock.Now();
	}

	void MeterRun::Continue() { std::lock_guard<std::mutex> g(mutex); pending = Command::Continue; lastTouch = clock.Now(); }
	void MeterRun::Skip() { std::lock_guard<std::mutex> g(mutex); pending = Command::Skip; lastTouch = clock.Now(); }
	void MeterRun::Apply() { std::lock_guard<std::mutex> g(mutex); pending = Command::Apply; lastTouch = clock.Now(); }
	void MeterRun::Discard() { std::lock_guard<std::mutex> g(mutex); pending = Command::Discard; lastTouch = clock.Now(); }
	void MeterRun::Touch() { std::lock_guard<std::mutex> g(mutex); lastTouch = clock.Now(); }
	void MeterRun::SetArguments(const std::string& a) { std::lock_guard<std::mutex> g(mutex); arguments = a; }

	void MeterRun::Cancel()
	{
		{
			std::lock_guard<std::mutex> g(mutex);
			pending = Command::Cancel;
			lastTouch = clock.Now();
		}
		if (meter) meter->Abort();
	}

	bool MeterRun::CancelPending()
	{
		std::lock_guard<std::mutex> g(mutex);
		return pending == Command::Cancel;
	}

	double MeterRun::SecondsSinceTouch()
	{
		std::lock_guard<std::mutex> g(mutex);
		return clock.Now() - lastTouch;
	}

	bool MeterRun::Finished() const
	{
		std::lock_guard<std::mutex> g(mutex);
		return IsFinal(status.step);
	}

	SessionStatus MeterRun::Status() const
	{
		std::lock_guard<std::mutex> g(mutex);
		return status;
	}

	MeterRun::Command MeterRun::TakeCommand()
	{
		std::lock_guard<std::mutex> g(mutex);
		const Command c = pending;
		pending = Command::None;
		return c;
	}

	void MeterRun::SetStep(SessionStep step, const std::string& instruction)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.step = step;
		status.instruction = instruction;
	}

	void MeterRun::SetMessage(const std::string& message)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.message = message;
	}

	void MeterRun::SetProgress(int index, int count)
	{
		std::lock_guard<std::mutex> g(mutex);
		status.patchIndex = index;
		status.patchCount = count;
		const double elapsed = clock.Now() - phaseStart;
		status.secondsRemaining = index > 0 && count >= index ? elapsed / index * (count - index) : 0.0;
	}

	void MeterRun::EndPatterns()
	{
		if (!patternsOn) return;
		PatternRequest off;
		off.active = false;
		off.generation = ++generation;
		std::string error;
		patterns.Show(off, error);
		patternsOn = false;
		placementShown = false;
	}

	void MeterRun::Finish(SessionStep step, const std::string& message)
	{
		OnFinishing(step);
		EndPatterns();
		if (meter) meter->Close();
		if (!indexWritten && !folder.runDir.empty() && (step == SessionStep::Cancelled || step == SessionStep::Failed))
			WriteIndex(step == SessionStep::Cancelled ? "cancelled" : "failed");
		std::lock_guard<std::mutex> g(mutex);
		status.step = step;
		if (!message.empty()) status.message = message;
		status.secondsRemaining = 0.0;
		status.instruction = step == SessionStep::Applied ? "Done. The suggested values are in your configuration." :
			step == SessionStep::Discarded ? "Discarded. Your configuration was not changed." :
			step == SessionStep::Cancelled ? "Cancelled. Your configuration was not changed." :
			step == SessionStep::Done ? "Done. Nothing in your configuration was changed." :
			"Stopped. Your configuration was not changed.";
	}

	void MeterRun::Fail(const std::string& message)
	{
		Finish(SessionStep::Failed, message);
	}

	void MeterRun::WriteRaw(const std::string& line)
	{
		if (folder.rawJournal.empty()) return;
		std::string error;
		AppendJournal(folder.rawJournal, line, error);
	}

	void MeterRun::WriteIndex(const char* state)
	{
		IndexEntry e;
		e.display = SanitizeDisplayName(environment.displayName.empty() ? std::string("display") : environment.displayName);
		e.timestamp = stamp.Format() + "-" + FolderSuffix();
		e.run = folder.relativeRunDir;
		e.status = state;
		std::string error;
		if (AppendIndex(folder.indexJson, e, error)) indexWritten = true;
	}

	std::string MeterRun::FormatName() const
	{
		std::string bits = format.codeBits == format.containerBits ?
			std::to_string(format.codeBits) + "-bit" : std::to_string(format.codeBits) + "-in-" + std::to_string(format.containerBits);
		std::string text = bits + (format.limitedRange ? " limited" : " full") +
			(format.transfer == SignalTransfer::Pq ? " PQ" : format.transfer == SignalTransfer::Hlg ? " HLG" : " SDR") +
			(format.bt2020 ? " BT.2020" : "");
		if (format.width > 0)
			text += ", " + std::to_string(format.width) + " x " + std::to_string(format.height);
		if (format.refreshHz > 0.0)
			text += " at " + FixedText(format.refreshHz, 3) + " Hz (measured)";
		text += ", window " + FixedText(EffectiveWindowPercent(settings), 0) + "%";
		return text;
	}

	PatchCodes MeterRun::Signal(double r, double g, double b) const
	{
		return CodesForSignal(format, { r, g, b });
	}

	bool MeterRun::ShowPatch(const PatchCodes& codes, double expectedY, std::string& error, double settleSeconds)
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
		if (!patternsStartedHook)
		{
			patternsStartedHook = true;
			if (!OnPatternsStarted(error)) return false;
		}
		if (settleSeconds < 0.0)
		{
			SettleOptions so;
			so.configuredMs = settings.settleMs;
			so.displayKind = static_cast<int>(settings.displayKind);
			so.extraLatencySeconds = environment.displayLatencySeconds;
			settleSeconds = SettleSeconds(so, previousY, expectedY, whiteY);
		}
		clock.Sleep(settleSeconds);
		return true;
	}

	bool MeterRun::Wait(double seconds)
	{
		const double end = clock.Now() + seconds;
		while (clock.Now() < end)
		{
			if (CancelPending()) return false;
			clock.Sleep((std::min)(0.05, end - clock.Now()));
		}
		return !CancelPending();
	}

	bool MeterRun::HandleMeterAction(const MeterResult& r)
	{
		if (r.status != MeterStatus::NeedsUserAction) return false;
		std::lock_guard<std::mutex> g(mutex);
		resumeStep = status.step;
		status.step = SessionStep::MeterCalibration;
		status.instruction = r.message + "\nThen press Continue to resume the measurement.";
		return true;
	}

	MeterRun::ReadOutcome MeterRun::ReadOnce(Vec3& xyz, double& at, std::string& note)
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			if (CancelPending()) return ReadOutcome::Stop;
			const MeterResult r = meter->Read();
			if (r.status != MeterStatus::Ok && CancelPending()) return ReadOutcome::Stop;
			switch (r.status)
			{
			case MeterStatus::Ok:
			{
				xyz = r.xyz;
				at = clock.Now();
				std::lock_guard<std::mutex> g(mutex);
				status.haveReading = true;
				status.lastXyz = r.xyz;
				return ReadOutcome::Ok;
			}
			case MeterStatus::ReadFailed:
				note = r.message;
				break;
			case MeterStatus::NeedsUserAction:
				HandleMeterAction(r);
				return ReadOutcome::Stop;
			case MeterStatus::Timeout:
			{
				if (reopened) { Fail(r.message); return ReadOutcome::Stop; }
				reopened = true;
				SetMessage("The meter stopped answering; reconnecting...");
				const MeterResult o = meter->Open();
				if (o.status == MeterStatus::Ok) break;
				if (!HandleMeterAction(o)) Fail(o.message);
				return ReadOutcome::Stop;
			}
			default:
				Fail(r.message);
				return ReadOutcome::Stop;
			}
		}
		return ReadOutcome::Unreadable;
	}

	MeterRun::ReadOutcome MeterRun::MeasureCodes(double& elapsed, Vec3& xyz, int& reads, bool& floorLimited, std::string& note)
	{
		ReadingPolicy policy;
		policy.tolerance = settings.repeatTolerancePercent / 100.0;
		policy.floorNits = meterFloor;
		policy.normalMaxReads = 1;
		policy.nearBlackMaxReads = settings.nearBlackMaxReads;
		ReadingAccumulator accumulator(policy, whiteY);
		const double start = clock.Now();
		int failures = 0;
		while (!accumulator.Done())
		{
			Vec3 one;
			double at = 0.0;
			std::string why;
			const ReadOutcome o = ReadOnce(one, at, why);
			if (o == ReadOutcome::Stop) return o;
			if (o == ReadOutcome::Unreadable)
			{
				++failures;
				note = why;
				if (accumulator.Count() > 0) break;
				if (failures > 1) { elapsed = clock.Now() - start; return ReadOutcome::Unreadable; }
				continue;
			}
			accumulator.Add(one);
		}
		xyz = accumulator.Mean();
		reads = accumulator.Count();
		floorLimited = accumulator.FloorLimited();
		elapsed = clock.Now() - start;
		previousY = xyz[1];
		return ReadOutcome::Ok;
	}

	std::string MeterRun::PlacementInstruction() const
	{
		const bool projector = settings.displayKind == DisplayKind::Projector;
		std::string instruction = projector ?
			"Place the meter on a tripod 30-50 cm in front of the screen centre, facing the screen, out of the "
			"projector's beam. The white patch is in the centre." :
			"Lay the meter flat against the centre of the screen (the white patch), with its cable weighted so it "
			"does not tilt.";
		instruction += " The reading below updates live: aim for the highest steady value, then press Continue.";
		return instruction;
	}

	void MeterRun::DoOpenMeter()
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
			folder = BuildPhase2RunFolder(environment.vpRoot, environment.displayName.empty() ? "display" : environment.displayName,
				stamp, FolderSuffix());
			std::lock_guard<std::mutex> g(mutex);
			status.format = FormatName();
			status.runFolder = folder.relativeRunDir;
			status.message = r.message;
			status.step = SessionStep::Placement;
			status.instruction = PlacementInstruction();
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

	void MeterRun::DoPlacement()
	{
		std::string error;
		if (!patternsOn || !placementShown)
		{
			if (!ShowPatch(Signal(1.0, 1.0, 1.0), 0.0, error)) { Fail(error); return; }
			placementShown = true;
		}
		const MeterResult r = meter->Read();
		if (r.status != MeterStatus::Ok && CancelPending()) return;
		if (r.status == MeterStatus::Ok)
		{
			const Chromaticity c = XyzToXy(r.xyz);
			std::lock_guard<std::mutex> g(mutex);
			status.haveReading = true;
			status.lastXyz = r.xyz;
			status.message = "Aim reading: Y " + FixedText(r.xyz[1], 3) + " cd/m2, x " + FixedText(c.x, 4) + ", y " +
				FixedText(c.y, 4);
			whiteY = r.xyz[1];
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

	void MeterRun::DoWarmUp()
	{
		if (warmupStart < 0.0)
		{
			warmupStart = clock.Now();
			std::string error;
			if (!ShowPatch(Signal(0.5, 0.5, 0.5), 0.0, error)) { Fail(error); return; }
		}
		const double remaining = settings.warmupMinutes * 60.0 - (clock.Now() - warmupStart);
		{
			std::lock_guard<std::mutex> g(mutex);
			status.warmupRemaining = (std::max)(remaining, 0.0);
		}
		if (remaining <= 0.0)
		{
			phaseStart = clock.Now();
			BeginWork();
			return;
		}
		clock.Sleep((std::min)(remaining, 1.0));
	}

	void MeterRun::SetReview(const std::string& review, const std::vector<ConfigEdit>& edits, const std::string& applyTarget)
	{
		pendingEdits = edits;
		std::string error;
		ConfigDiff diff;
		if (!edits.empty())
			diff = PreviewKeyEdits(environment.configPath, edits, validator, error);
		std::lock_guard<std::mutex> g(mutex);
		status.step = SessionStep::Review;
		status.review = review;
		status.secondsRemaining = 0.0;
		if (edits.empty())
		{
			status.diff = "Apply is not possible: nothing was measured that can be applied.";
			status.instruction = "Review the result. Discard to finish (the measurements stay in the run folder).";
		}
		else if (!diff.ok)
		{
			status.diff = "Apply is not possible: " + error + "\nSuggested:\n" + diff.Format();
			status.instruction = "Review the result. " + error + ". Discard to finish (the suggestion stays in the run folder).";
		}
		else
		{
			status.diff = diff.Format();
			status.instruction = "Review the result. Apply writes the suggested values to " + applyTarget +
				" (after backing up your configuration); Discard leaves your configuration as it is.";
		}
	}

	void MeterRun::DoApply()
	{
		const ApplyOutcome o = ApplyKeyEdits(environment.configPath, pendingEdits, folder, clock.LocalTime(), validator);
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
		Finish(SessionStep::Applied, "Applied:\n" + o.diff.Format());
	}

	void MeterRun::DoDiscard()
	{
		WriteIndex("discarded");
		Finish(SessionStep::Discarded, "The run was discarded; its measurements stay in " + folder.relativeRunDir + ".");
	}

	bool MeterRun::Step()
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
					if (resumeStep == SessionStep::OpeningMeter) { DoOpenMeter(); break; }
					SetStep(resumeStep, resumeStep == SessionStep::Placement ? PlacementInstruction() :
						"Measuring. Keep the room as it is and the meter still.");
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
				if (WantsWarmUp() && settings.warmupMinutes > 0)
					SetStep(SessionStep::WarmUp, "Warming up the display (" + std::to_string(settings.warmupMinutes) +
						" minutes). Skip when it has already been on for that long.");
				else
					BeginWork();
			}
			else
				DoPlacement();
			break;
		case SessionStep::WarmUp:
			if (command == Command::Skip || command == Command::Continue)
			{
				phaseStart = clock.Now();
				BeginWork();
			}
			else
				DoWarmUp();
			break;
		case SessionStep::Review:
			if (command == Command::Apply) DoApply();
			else if (command == Command::Discard) DoDiscard();
			else clock.Sleep(0.05);
			break;
		default:
			StepWork(step, command);
			break;
		}
		return !Finished();
	}
}
