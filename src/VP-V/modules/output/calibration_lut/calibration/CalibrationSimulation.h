// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Automatic calibration: simulated pieces for the CPU tests and for a dry
// run ([calibration] meter_model: simulated - the real renderer shows the
// patches, the readings come from a simulated display and meter):
//  - SimulatedScreen: an IPatternSink that remembers the codes shown (no GPU);
//  - SimulatedScreenMeter: an IMeter that reads what a pattern sink shows
//    through SimulatedDisplay + SimulatedMeter;
//  - ManualClock: an ISessionClock whose Sleep advances time at once.

#include "CalibrationSession.h"
#include "SimulatedDisplay.h"
#include "SimulatedMeter.h"

#include <functional>
#include <mutex>

namespace Calibration
{
	class SimulatedScreen final : public IPatternSink
	{
	public:
		explicit SimulatedScreen(const PatternOutputInfo& format);
		bool GetOutput(PatternOutputInfo& info) override;
		bool Show(const PatternRequest& request, std::string& error) override;

		// The device signal currently shown (the patch), or the video (nullptr).
		bool Showing(Vec3& device) const;
		const PatternRequest& LastRequest() const { return last; }
		int ShowCount() const { return shows; }
		bool PatternActive() const { return last.active; }
		void SetFormat(const PatternOutputInfo& format) { info = format; }
		// Code -> device signal for the shown container codes.
		Vec3 DeviceForContainer(const uint16_t container[3]) const;

	private:
		PatternOutputInfo info;
		PatternRequest last;
		int shows = 0;
	};

	// Reads `screen` (any IPatternSink exposing the shown device signal through
	// `shown`) via a simulated display and meter.
	class SimulatedScreenMeter final : public IMeter
	{
	public:
		using ShownFn = std::function<bool(Vec3& device)>;
		SimulatedScreenMeter(ShownFn shown, const SimulatedDisplayParams& display, const SimulatedMeterParams& meter,
			ISessionClock* clock = nullptr);
		MeterResult Open() override;
		MeterResult Continue() override;
		MeterResult Read() override;
		void Close() override { open = false; }
		bool IsOpen() const override { return open; }
		std::string Description() const override { return "Simulated meter (dry run)"; }

		// Test hooks: the next Open asks for a sensor calibration; Read fails n times.
		void RequireCalibrationOnOpen(bool value) { calibrationOnOpen = value; }
		void FailNextReads(int count, SpotreadErrorKind kind) { failReads = count; failKind = kind; }
		void TimeoutNextRead() { timeoutNext = true; }
		int Reads() const { return reads; }
		SimulatedDisplay& Display() { return display; }

	private:
		ShownFn shown;
		SimulatedDisplay display;
		SimulatedMeter meter;
		ISessionClock* clock;
		bool open = false;
		bool calibrationOnOpen = false;
		bool calibrating = false;
		int failReads = 0;
		SpotreadErrorKind failKind = SpotreadErrorKind::Misread;
		bool timeoutNext = false;
		int reads = 0;
	};

	class ManualClock final : public ISessionClock
	{
	public:
		double Now() override { std::lock_guard<std::mutex> g(mutex); return now; }
		void Sleep(double seconds) override { std::lock_guard<std::mutex> g(mutex); if (seconds > 0.0) now += seconds; }
		RunTimestamp LocalTime() override
		{
			RunTimestamp t;
			t.year = 2026; t.month = 10; t.day = 17; t.hour = 21; t.minute = 0;
			std::lock_guard<std::mutex> g(mutex);
			t.second = static_cast<int>(now) % 60;
			return t;
		}
		void Advance(double seconds) { Sleep(seconds); }

	private:
		std::mutex mutex;
		double now = 1000.0;
	};
}
