// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: the meter (design "Meter and licence").
//
// IMeter is what the session measures with. SpotreadMeter drives ArgyllCMS's
// spotread under a pseudo-console (Terminal.h): one long-lived process, a
// space per reading, the "Result is XYZ:" line parsed (Spotread.h). Sensor
// calibration prompts are passed to the user; errors are classified; a
// reading that does not arrive within the timeout ends the process (a hung
// instrument is reopened, never waited on forever).
//
// A vendor SDK meter would be another IMeter (a slot left for later; vendor
// SDKs are licensed to OEMs only).

#include "../pattern_lib/ColorMath.h"
#include "Spotread.h"
#include "Terminal.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace Calibration
{
	enum class MeterStatus
	{
		Ok,
		ReadFailed,       // this reading failed; the meter is still usable (retry)
		NeedsUserAction,  // follow `message` (sensor calibration), then Continue()
		Timeout,          // no answer in time; the meter was closed
		Fatal,            // the meter cannot be used (message says why)
	};

	const char* ToString(MeterStatus status);

	struct MeterResult
	{
		MeterStatus status = MeterStatus::Fatal;
		Vec3 xyz = { 0, 0, 0 };
		double seconds = 0.0;
		SpotreadErrorKind error = SpotreadErrorKind::None;
		std::string message;
	};

	class IMeter
	{
	public:
		virtual ~IMeter() = default;
		virtual MeterResult Open() = 0;
		virtual MeterResult Continue() = 0;
		virtual MeterResult Read() = 0;
		virtual void Close() = 0;
		virtual bool IsOpen() const = 0;
		virtual std::string Description() const = 0;
		// Any thread: ends a wait in progress (cancel, VP closing). The call in
		// progress returns Fatal; the next Open() clears it.
		virtual void Abort() {}
	};

	struct SpotreadMeterOptions
	{
		std::wstring spotread;              // full path of spotread.exe
		SpotreadOptions arguments;
		double openTimeoutSeconds = 60.0;   // instrument start-up (USB enumeration, warm-up)
		double readTimeoutSeconds = 60.0;   // one reading, dark patches included
		double closeTimeoutSeconds = 3.0;
		std::string instrumentName;         // for Description()
	};

	class SpotreadMeter final : public IMeter
	{
	public:
		using TerminalFactory = std::function<std::unique_ptr<ITerminal>()>;
		using Clock = std::function<double()>;   // seconds, monotonic

		SpotreadMeter(const SpotreadMeterOptions& options, TerminalFactory factory, Clock clock = Clock());
		~SpotreadMeter() override;

		MeterResult Open() override;
		MeterResult Continue() override;
		MeterResult Read() override;
		void Close() override;
		bool IsOpen() const override { return terminal != nullptr; }
		std::string Description() const override;
		void Abort() override { aborted.store(true); }

		// The plain-text transcript (bounded), for the journal and the log.
		const std::string& Transcript() const { return parser.Transcript(); }
		// The keys sent (tests).
		const std::string& KeysSent() const { return keys; }

	private:
		enum class State { Closed, WaitingPrompt, Ready, Calibration };

		// Waits until an event of interest or the deadline; returns it (Timeout /
		// Exited synthesised).
		SpotreadEvent WaitEvent(double timeoutSeconds, bool acceptReading);
		bool Send(const std::string& key);
		MeterResult Fail(MeterStatus status, const SpotreadEvent& event);
		double Now() const;

		SpotreadMeterOptions options;
		TerminalFactory factory;
		Clock clock;
		std::unique_ptr<ITerminal> terminal;
		SpotreadParser parser;
		State state = State::Closed;
		std::vector<SpotreadEvent> queued;
		std::string keys;
		std::string lastCalibrationText;
		std::atomic_bool aborted{ false };
	};
}
