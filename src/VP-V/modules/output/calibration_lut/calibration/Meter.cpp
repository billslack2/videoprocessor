// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "Meter.h"

#include <algorithm>
#include <chrono>

namespace Calibration
{
	const char* ToString(MeterStatus status)
	{
		switch (status)
		{
		case MeterStatus::Ok: return "ok";
		case MeterStatus::ReadFailed: return "read_failed";
		case MeterStatus::NeedsUserAction: return "needs_user_action";
		case MeterStatus::Timeout: return "timeout";
		case MeterStatus::Fatal: return "fatal";
		}
		return "unknown";
	}

	namespace
	{
		std::wstring FolderOf(const std::wstring& path)
		{
			const size_t slash = path.find_last_of(L"\\/");
			return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
		}

		std::string LastLines(const std::string& transcript, int count)
		{
			size_t pos = transcript.size();
			int lines = 0;
			while (pos > 0 && lines <= count)
			{
				--pos;
				if (transcript[pos] == '\n') ++lines;
			}
			return TrimText(transcript.substr(pos));
		}

		std::string Advice(SpotreadErrorKind kind)
		{
			switch (kind)
			{
			case SpotreadErrorKind::NoInstrument:
				return "No meter was found. Plug it in, close other software that may hold it (i1Profiler, "
					"Calman, DisplayCAL), then try again.";
			case SpotreadErrorKind::InitFailed:
				return "The meter did not start. Unplug it and plug it in again. i1Pro and ColorMunki Photo need "
					"ArgyllCMS's USB driver.";
			case SpotreadErrorKind::Communication:
				return "USB communication with the meter failed. Use a direct USB port (no hub), then try again.";
			case SpotreadErrorKind::Usage:
				return "spotread did not accept VP's options: check the display type and the spectral correction "
					"file, or install the current ArgyllCMS.";
			case SpotreadErrorKind::UserAbort:
				return "The measurement was stopped at the meter.";
			default:
				return std::string();
			}
		}
	}

	SpotreadMeter::SpotreadMeter(const SpotreadMeterOptions& o, TerminalFactory f, Clock c)
		: options(o), factory(std::move(f)), clock(std::move(c))
	{
	}

	SpotreadMeter::~SpotreadMeter()
	{
		Close();
	}

	double SpotreadMeter::Now() const
	{
		if (clock) return clock();
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::string SpotreadMeter::Description() const
	{
		return options.instrumentName.empty() ? std::string("ArgyllCMS spotread") : options.instrumentName;
	}

	bool SpotreadMeter::Send(const std::string& key)
	{
		if (!terminal) return false;
		keys += key;
		return terminal->Write(key);
	}

	SpotreadEvent SpotreadMeter::WaitEvent(double timeoutSeconds, bool acceptReading)
	{
		const double deadline = Now() + (std::max)(timeoutSeconds, 0.0);
		for (;;)
		{
			if (aborted.load())
			{
				SpotreadEvent e;
				e.kind = SpotreadEventKind::FatalError;
				e.error = SpotreadErrorKind::UserAbort;
				e.text = "The measurement was stopped.";
				return e;
			}
			while (!queued.empty())
			{
				const SpotreadEvent e = queued.front();
				queued.erase(queued.begin());
				if (e.kind == SpotreadEventKind::Reading && !acceptReading)
					continue;   // a redraw of an earlier result, or a key VP did not send
				return e;
			}
			if (!terminal)
			{
				SpotreadEvent e;
				e.kind = SpotreadEventKind::Exited;
				e.text = "the meter is closed";
				return e;
			}
			const double remaining = deadline - Now();
			if (remaining <= 0.0)
			{
				SpotreadEvent e;
				e.kind = SpotreadEventKind::Timeout;
				return e;
			}
			std::string chunk;
			const int slice = static_cast<int>((std::min)(remaining, 0.25) * 1000.0) + 1;
			if (terminal->Read(chunk, slice))
			{
				for (const SpotreadEvent& e : parser.Feed(chunk))
					queued.push_back(e);
				continue;
			}
			unsigned long code = 0;
			if (terminal->Exited(code))
			{
				// Collect what is still buffered first.
				std::string tail;
				if (terminal->Read(tail, 50))
				{
					for (const SpotreadEvent& e : parser.Feed(tail))
						queued.push_back(e);
					for (const SpotreadEvent& e : parser.Feed("\n"))
						queued.push_back(e);
					if (!queued.empty()) continue;
				}
				SpotreadEvent e;
				e.kind = SpotreadEventKind::Exited;
				e.text = "spotread ended (exit code " + std::to_string(code) + ")";
				return e;
			}
		}
	}

	MeterResult SpotreadMeter::Fail(MeterStatus status, const SpotreadEvent& event)
	{
		MeterResult r;
		r.status = status;
		r.error = event.error;
		std::string message = event.text;
		if (event.kind == SpotreadEventKind::Timeout)
			message = "The meter did not answer in time; spotread was closed.";
		const std::string advice = Advice(event.error);
		if (!advice.empty()) message += (message.empty() ? "" : "\n") + advice;
		if (event.kind == SpotreadEventKind::Exited || event.kind == SpotreadEventKind::FatalError)
		{
			const std::string tail = LastLines(parser.Transcript(), 4);
			if (!tail.empty() && tail != event.text) message += "\nspotread said:\n" + tail;
		}
		r.message = message;
		Close();
		return r;
	}

	MeterResult SpotreadMeter::Open()
	{
		MeterResult r;
		aborted.store(false);
		if (terminal && state == State::Ready) { r.status = MeterStatus::Ok; return r; }
		Close();
		if (options.spotread.empty())
		{
			r.status = MeterStatus::Fatal;
			r.message = ArgyllInstallGuidance(std::string());
			return r;
		}
		terminal = factory ? factory() : nullptr;
		if (!terminal)
		{
			r.status = MeterStatus::Fatal;
			r.message = "No pseudo-console is available (Windows 10 version 1809 or newer is needed).";
			return r;
		}
		parser.Reset();
		queued.clear();
		std::string error;
		const std::wstring commandLine = BuildCommandLine(options.spotread, SpotreadArguments(options.arguments));
		if (!terminal->Start(options.spotread, commandLine, FolderOf(options.spotread), error))
		{
			terminal.reset();
			r.status = MeterStatus::Fatal;
			r.message = "spotread could not be started: " + error;
			return r;
		}
		state = State::WaitingPrompt;
		const double deadline = Now() + options.openTimeoutSeconds;
		std::string note;
		for (;;)
		{
			const SpotreadEvent e = WaitEvent(deadline - Now(), false);
			switch (e.kind)
			{
			case SpotreadEventKind::ReadyPrompt:
				state = State::Ready;
				r.status = MeterStatus::Ok;
				r.message = note;
				return r;
			case SpotreadEventKind::CalibrationPrompt:
				state = State::Calibration;
				lastCalibrationText = e.text;
				r.status = MeterStatus::NeedsUserAction;
				r.message = (note.empty() ? std::string() : note + "\n") +
					(e.text.empty() ? std::string("Put the meter in its calibration position (cap on, or on its "
						"calibration tile), then press Continue.") : e.text);
				return r;
			case SpotreadEventKind::ReadFailed:
				note = e.text;   // e.g. a failed calibration; spotread asks again
				break;
			case SpotreadEventKind::NeedsCalibration:
			case SpotreadEventKind::CalibrationComplete:
				break;
			case SpotreadEventKind::Timeout:
				return Fail(MeterStatus::Timeout, e);
			default:
				return Fail(MeterStatus::Fatal, e);
			}
		}
	}

	MeterResult SpotreadMeter::Continue()
	{
		MeterResult r;
		if (!terminal) { r.status = MeterStatus::Fatal; r.message = "The meter is not open."; return r; }
		if (state == State::Ready) { r.status = MeterStatus::Ok; return r; }
		if (state == State::Calibration)
		{
			if (!Send(" "))
			{
				SpotreadEvent e;
				e.kind = SpotreadEventKind::FatalError;
				e.text = "Could not send a key to spotread.";
				return Fail(MeterStatus::Fatal, e);
			}
			state = State::WaitingPrompt;
		}
		const double deadline = Now() + options.openTimeoutSeconds;
		std::string note;
		for (;;)
		{
			const SpotreadEvent e = WaitEvent(deadline - Now(), false);
			switch (e.kind)
			{
			case SpotreadEventKind::ReadyPrompt:
				state = State::Ready;
				r.status = MeterStatus::Ok;
				r.message = note;
				return r;
			case SpotreadEventKind::CalibrationPrompt:
				state = State::Calibration;
				r.status = MeterStatus::NeedsUserAction;
				r.message = (note.empty() ? std::string() : note + "\n") + e.text;
				return r;
			case SpotreadEventKind::ReadFailed:
				note = e.text;
				break;
			case SpotreadEventKind::NeedsCalibration:
			case SpotreadEventKind::CalibrationComplete:
				break;
			case SpotreadEventKind::Timeout:
				return Fail(MeterStatus::Timeout, e);
			default:
				return Fail(MeterStatus::Fatal, e);
			}
		}
	}

	MeterResult SpotreadMeter::Read()
	{
		MeterResult r;
		if (!terminal) { r.status = MeterStatus::Fatal; r.message = "The meter is not open."; return r; }
		if (state == State::WaitingPrompt)
		{
			// The prompt after the previous reading; normally already queued.
			const SpotreadEvent e = WaitEvent(options.readTimeoutSeconds, false);
			switch (e.kind)
			{
			case SpotreadEventKind::ReadyPrompt:
				state = State::Ready;
				break;
			case SpotreadEventKind::CalibrationPrompt:
				state = State::Calibration;
				r.status = MeterStatus::NeedsUserAction;
				r.message = e.text;
				return r;
			case SpotreadEventKind::ReadFailed:
				state = State::WaitingPrompt;
				r.status = MeterStatus::ReadFailed;
				r.error = e.error;
				r.message = e.text;
				return r;
			case SpotreadEventKind::NeedsCalibration:
			case SpotreadEventKind::CalibrationComplete:
				return Read();
			case SpotreadEventKind::Timeout:
				return Fail(MeterStatus::Timeout, e);
			default:
				return Fail(MeterStatus::Fatal, e);
			}
		}
		if (state == State::Calibration)
		{
			r.status = MeterStatus::NeedsUserAction;
			r.message = lastCalibrationText;
			return r;
		}
		const double start = Now();
		if (!Send(" "))
		{
			SpotreadEvent e;
			e.kind = SpotreadEventKind::FatalError;
			e.text = "Could not send a key to spotread.";
			return Fail(MeterStatus::Fatal, e);
		}
		state = State::WaitingPrompt;
		const double deadline = start + options.readTimeoutSeconds;
		for (;;)
		{
			const SpotreadEvent e = WaitEvent(deadline - Now(), true);
			switch (e.kind)
			{
			case SpotreadEventKind::Reading:
				r.status = MeterStatus::Ok;
				r.xyz = e.xyz;
				r.seconds = Now() - start;
				return r;
			case SpotreadEventKind::ReadyPrompt:
				state = State::Ready;
				r.status = MeterStatus::ReadFailed;
				r.error = SpotreadErrorKind::Misread;
				r.message = "spotread asked again without a reading";
				return r;
			case SpotreadEventKind::ReadFailed:
				r.status = MeterStatus::ReadFailed;
				r.error = e.error;
				r.message = e.text;
				return r;
			case SpotreadEventKind::CalibrationPrompt:
				state = State::Calibration;
				lastCalibrationText = e.text;
				r.status = MeterStatus::NeedsUserAction;
				r.message = e.text;
				return r;
			case SpotreadEventKind::NeedsCalibration:
			case SpotreadEventKind::CalibrationComplete:
				break;
			case SpotreadEventKind::Timeout:
				return Fail(MeterStatus::Timeout, e);
			default:
				return Fail(MeterStatus::Fatal, e);
			}
		}
	}

	void SpotreadMeter::Close()
	{
		if (!terminal) { state = State::Closed; return; }
		unsigned long code = 0;
		if (!terminal->Exited(code))
		{
			// 'q' quits at the prompt; a second 'q' / Esc answers a confirmation.
			Send("q");
			const double half = Now() + options.closeTimeoutSeconds / 2.0;
			std::string sink;
			while (!terminal->Exited(code) && Now() < half)
				terminal->Read(sink, 50);
			if (!terminal->Exited(code))
			{
				Send("q");
				const double end = Now() + options.closeTimeoutSeconds / 2.0;
				while (!terminal->Exited(code) && Now() < end)
					terminal->Read(sink, 50);
			}
			if (!terminal->Exited(code))
				terminal->Terminate();   // the spotread this meter started
			if (!sink.empty()) parser.Feed(sink);
		}
		terminal.reset();
		queued.clear();
		state = State::Closed;
	}
}
