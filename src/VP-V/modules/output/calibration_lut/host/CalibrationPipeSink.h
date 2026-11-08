// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "CalibrationPipe.h"
#include "../calibration/CalibrationSession.h"
#include "../calibration/CalibrationSimulation.h"

#include <cstring>
#include <mutex>
#include <string>

// The calibration session's pattern sink in a separate process: every GetOutput / Show goes to the
// running VP renderer over the calibration pipe (CalibrationPipe.h). Header-only; used by the CLI
// driver (VideoProcessor-Calibrate) and its CPU tests.
namespace CalibrationPipe
{
	class PipePatternSink final : public Calibration::IPatternSink
	{
	public:
		explicit PipePatternSink(Client pipeClient) : client(std::move(pipeClient)) {}

		bool GetOutput(Calibration::PatternOutputInfo& info) override
		{
			std::lock_guard<std::mutex> guard(mutex);
			info = Calibration::PatternOutputInfo();
			bool ok = false;
			std::string error;
			if (!client.GetOutput(info, ok, error))
			{
				attached = false;
				info = Calibration::PatternOutputInfo();
				strncpy_s(info.reason, sizeof(info.reason), error.c_str(), _TRUNCATE);
				return false;
			}
			attached = true;
			format = info;
			return ok;
		}

		// True once the pipe answered (the renderer may still be starting; the session then waits).
		bool RendererAttached() override
		{
			std::lock_guard<std::mutex> guard(mutex);
			return attached;
		}

		bool Show(const Calibration::PatternRequest& request, std::string& error) override
		{
			std::lock_guard<std::mutex> guard(mutex);
			bool ok = false;
			std::string reason;
			if (!client.SetPattern(request, ok, reason, error))
			{
				attached = false;
				return !request.active;   // nothing to end when VP is gone
			}
			if (!ok)
			{
				error = reason.empty() ? std::string("the VP Renderer refused the patch") : reason;
				return false;
			}
			last = request;
			return true;
		}

		// The device signal currently shown, from the codes last sent and the output format (the
		// simulated meter of a dry run through the real renderer reads this).
		bool Showing(Calibration::Vec3& device)
		{
			std::lock_guard<std::mutex> guard(mutex);
			if (!last.active || !(format.codeBits == 8 || format.codeBits == 10)) return false;
			Calibration::SimulatedScreen screen(format);
			device = screen.DeviceForContainer(last.patch);
			return true;
		}

		// A heartbeat: keeps the renderer's abandoned-patterns timer from ending a run that waits on
		// a long meter reading.
		void Heartbeat()
		{
			Calibration::PatternOutputInfo info;
			bool ok = false;
			std::string error;
			client.GetOutput(info, ok, error);
		}

	private:
		std::mutex mutex;
		Client client;
		Calibration::PatternRequest last;
		Calibration::PatternOutputInfo format;
		bool attached = false;
	};
}
