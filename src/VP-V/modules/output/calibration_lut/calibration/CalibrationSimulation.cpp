// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <pch.h>

#include "CalibrationSimulation.h"

namespace Calibration
{
	SimulatedScreen::SimulatedScreen(const PatternOutputInfo& format) : info(format) {}

	bool SimulatedScreen::GetOutput(PatternOutputInfo& out)
	{
		out = info;
		return info.available;
	}

	bool SimulatedScreen::Show(const PatternRequest& request, std::string& error)
	{
		if (!info.available) { error = "no renderer"; return false; }
		last = request;
		++shows;
		info.presentedGeneration = request.generation;
		info.presentsOfGeneration = 2;   // presented at once
		return true;
	}

	Vec3 SimulatedScreen::DeviceForContainer(const uint16_t container[3]) const
	{
		Vec3 d;
		for (int k = 0; k < 3; ++k)
		{
			int code = container[k];
			if (info.containerBits > info.codeBits)
			{
				if (info.limitedRange) code >>= (info.containerBits - info.codeBits);
				else code = static_cast<int>(std::lround(code * ((1 << info.codeBits) - 1.0) / ((1 << info.containerBits) - 1.0)));
			}
			d[k] = CodeToSignal(info, code);
		}
		return d;
	}

	bool SimulatedScreen::Showing(Vec3& device) const
	{
		if (!last.active) return false;
		device = DeviceForContainer(last.patch);
		return true;
	}

	SimulatedScreenMeter::SimulatedScreenMeter(ShownFn s, const SimulatedDisplayParams& d, const SimulatedMeterParams& m,
		ISessionClock* c)
		: shown(std::move(s)), display(d), meter(m), clock(c)
	{
	}

	MeterResult SimulatedScreenMeter::Open()
	{
		MeterResult r;
		open = true;
		if (calibrationOnOpen)
		{
			calibrationOnOpen = false;
			calibrating = true;
			r.status = MeterStatus::NeedsUserAction;
			r.message = "Set instrument sensor to calibration position (simulated)";
			return r;
		}
		r.status = MeterStatus::Ok;
		return r;
	}

	MeterResult SimulatedScreenMeter::Continue()
	{
		MeterResult r;
		calibrating = false;
		r.status = open ? MeterStatus::Ok : MeterStatus::Fatal;
		if (!open) r.message = "The meter is not open.";
		return r;
	}

	MeterResult SimulatedScreenMeter::Read()
	{
		MeterResult r;
		if (!open) { r.status = MeterStatus::Fatal; r.message = "The meter is not open."; return r; }
		if (calibrating) { r.status = MeterStatus::NeedsUserAction; r.message = "calibration"; return r; }
		if (timeoutNext)
		{
			timeoutNext = false;
			open = false;
			r.status = MeterStatus::Timeout;
			r.message = "The meter did not answer in time; spotread was closed.";
			return r;
		}
		if (failReads > 0)
		{
			--failReads;
			r.status = MeterStatus::ReadFailed;
			r.error = failKind;
			r.message = failKind == SpotreadErrorKind::TooDark ? "Spot read failed: too dark" : "Spot read failed: misread";
			return r;
		}
		Vec3 device = { 0, 0, 0 };
		const bool patch = shown && shown(device);
		const Vec3 truth = patch ? display.Emit(device) : display.Emit({ 0.5, 0.5, 0.5 });
		const MeterReading reading = meter.Read(truth);
		if (clock) clock->Sleep(reading.seconds);
		++reads;
		r.status = MeterStatus::Ok;
		r.xyz = reading.xyz;
		r.seconds = reading.seconds;
		return r;
	}
}
