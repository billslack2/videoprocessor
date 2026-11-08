// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// VideoProcessor-Calibrate: a console driver for one automatic calibration run.
//
//   1. Start VideoProcessor.exe with VP_CALIBRATION_PIPE=1 in its environment and a source rendering
//      with the VP Renderer (the renderer then serves the local calibration pipe).
//   2. Run this tool. It runs CalibrationSession (the module's own run state machine): patches go
//      to the renderer over the pipe (exact codes in the backbuffer), readings come from ArgyllCMS
//      spotread (or a simulated meter), and the fitted 3D LUTs are written as .cube files that VP's
//      existing calibration_lut_<slot> keys load.
//
// --simulate runs the same session with no VideoProcessor at all (simulated screen and meter).
#include <pch.h>

#include <VP-V/modules/output/calibration_lut/calibration/ArgyllInstall.h>
#include <VP-V/modules/output/calibration_lut/calibration/CalibrationConfig.h>
#include <VP-V/modules/output/calibration_lut/calibration/CalibrationSession.h>
#include <VP-V/modules/output/calibration_lut/calibration/CalibrationSimulation.h>
#include <VP-V/modules/output/calibration_lut/calibration/Meter.h>
#include <VP-V/modules/output/calibration_lut/calibration/MeterCatalog.h>
#include <VP-V/modules/output/calibration_lut/calibration/Spotread.h>
#include <VP-V/modules/output/calibration_lut/calibration/Terminal.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPipe.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPipeSink.h>

#include <ConfigFile.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace Calibration;

namespace
{
	std::atomic<bool> g_cancel{ false };

	BOOL WINAPI OnConsoleControl(DWORD type)
	{
		if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
		{
			g_cancel.store(true);
			return TRUE;   // the main loop cancels the session, which returns VP to video
		}
		return FALSE;
	}

	struct Options
	{
		std::string configPath = "VideoProcessor.cfg";
		std::string settingsPath;            // "" = calibration.cfg next to the configuration
		std::string colorSection;            // for --apply, e.g. vprenderer.color.Rec709
		std::string meter;                   // overrides [calibration] meter_model
		std::string plan;                    // overrides plan
		int lutSize = 0;                     // overrides lut_size
		bool simulate = false;
		bool apply = false;
		bool yes = false;                    // answer Continue to every prompt
		bool skipWarmup = false;
		bool help = false;
	};

	void Usage()
	{
		std::cout <<
			"VideoProcessor-Calibrate: one automatic display calibration run.\n"
			"\n"
			"  VideoProcessor-Calibrate [options]\n"
			"\n"
			"  --config <file>         VideoProcessor.cfg (run folder root; Apply edits it). Default: .\\VideoProcessor.cfg\n"
			"  --settings <file>       a file with a [calibration] section. Default: calibration.cfg next to --config\n"
			"                          (missing = defaults)\n"
			"  --meter <id>            meter_model: auto, simulated, i1d3, i1pro, ... (overrides the settings)\n"
			"  --plan <name>           quick, cube5, standard, cube13, thorough\n"
			"  --lut-size <n>          LUT points per axis (e.g. 17, 33)\n"
			"  --simulate              no VideoProcessor: simulated screen and meter (CPU dry run)\n"
			"  --apply --color-section <section>\n"
			"                          at Review, apply: set calibration_lut_enabled and calibration_lut_<slot> in\n"
			"                          that profile (e.g. vprenderer.color.Rec709), after a timestamped backup.\n"
			"                          Without --apply the run is kept (not applied) and the .cube paths are printed.\n"
			"  --yes                   answer Continue to every prompt (meter calibration, placement)\n"
			"  --skip-warmup           skip the display warm-up wait\n"
			"\n"
			"VideoProcessor must run with VP_CALIBRATION_PIPE=1 in its environment and a source rendering with the\n"
			"VP Renderer (not needed with --simulate). Ctrl+C cancels the run and returns VP to video.\n";
	}

	bool ParseArguments(int argc, wchar_t** argv, Options& o)
	{
		const auto narrow = [](const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			std::string out(size > 0 ? size - 1 : 0, '\0');
			if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], size, nullptr, nullptr);
			return out;
		};
		for (int i = 1; i < argc; ++i)
		{
			const std::string a = narrow(argv[i]);
			const auto value = [&](std::string& target) -> bool
			{
				if (i + 1 >= argc) { std::cerr << a << " needs a value\n"; return false; }
				target = narrow(argv[++i]);
				return true;
			};
			std::string number;
			if (a == "--config") { if (!value(o.configPath)) return false; }
			else if (a == "--settings") { if (!value(o.settingsPath)) return false; }
			else if (a == "--color-section") { if (!value(o.colorSection)) return false; }
			else if (a == "--meter") { if (!value(o.meter)) return false; }
			else if (a == "--plan") { if (!value(o.plan)) return false; }
			else if (a == "--lut-size") { if (!value(number)) return false; o.lutSize = std::atoi(number.c_str()); }
			else if (a == "--simulate") o.simulate = true;
			else if (a == "--apply") o.apply = true;
			else if (a == "--yes") o.yes = true;
			else if (a == "--skip-warmup") o.skipWarmup = true;
			else if (a == "--help" || a == "-h" || a == "/?") o.help = true;
			else { std::cerr << "unknown argument " << a << "\n"; return false; }
		}
		return true;
	}

	std::string FullPath(const std::string& path)
	{
		char buffer[MAX_PATH * 2] = {};
		const DWORD n = GetFullPathNameA(path.c_str(), static_cast<DWORD>(sizeof(buffer)), buffer, nullptr);
		return n > 0 && n < sizeof(buffer) ? std::string(buffer) : path;
	}

	std::string FolderOf(const std::string& path)
	{
		const std::string::size_type slash = path.find_last_of("\\/");
		return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
	}

	bool FileExists(const std::string& path)
	{
		const DWORD attributes = GetFileAttributesA(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::wstring Wide(const std::string& text)
	{
		const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
		std::wstring out(size > 0 ? size - 1 : 0, L'\0');
		if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
		return out;
	}

	bool ParsePlan(const std::string& name, PlanChoice& plan)
	{
		const PlanChoice all[] = { PlanChoice::Quick, PlanChoice::Cube5, PlanChoice::Standard, PlanChoice::Cube13,
			PlanChoice::Thorough };
		for (PlanChoice p : all)
			if (name == ToString(p)) { plan = p; return true; }
		return false;
	}

	// The simulated clock of --simulate: Sleep advances time at once; local time is real (the run
	// folder's name).
	class FastClock final : public ISessionClock
	{
	public:
		double Now() override { return now; }
		void Sleep(double seconds) override { if (seconds > 0.0) now += seconds; }
		RunTimestamp LocalTime() override { return system->LocalTime(); }

	private:
		std::unique_ptr<ISessionClock> system = CreateSystemClock();
		double now = 1000.0;
	};

	// The spotread meter, as the module's own controller creates it (no instrument probe here: the
	// model comes from meter_model, or spotread's first instrument with "auto").
	std::unique_ptr<IMeter> CreateSpotreadMeter(const CalibrationSettings& settings, double& floorNits, std::string& error)
	{
		std::string searched;
		const std::wstring spotread = FindSpotread(SystemSearchEnvironment(Wide(settings.argyllPath)), searched);
		if (spotread.empty())
		{
			error = ArgyllInstallGuidance(searched);
			return nullptr;
		}
		if (!ConPtyAvailable())
		{
			error = "This Windows has no pseudo-console (Windows 10 version 1809 or newer is needed for spotread).";
			return nullptr;
		}
		const MeterModel* model = ResolveMeterModel(settings.meterModel, std::string());
		floorNits = model ? model->floorNits : GenericFloorNits();
		SpotreadMeterOptions options;
		options.spotread = spotread;
		options.arguments.port = settings.meterPort;
		options.arguments.displayType = settings.displayType;
		options.arguments.spectralCorrection = settings.spectralCorrection;
		options.arguments.refreshMode = settings.refreshMode == RefreshMode::Refresh ? 1 :
			settings.refreshMode == RefreshMode::NonRefresh ? 2 : 0;
		options.readTimeoutSeconds = settings.readTimeoutSeconds;
		options.openTimeoutSeconds = (std::max)(60.0, static_cast<double>(settings.readTimeoutSeconds));
		options.instrumentName = model ? model->label : "ArgyllCMS meter";
		return std::unique_ptr<IMeter>(new SpotreadMeter(options, [] { return CreateConPtyTerminal(); }));
	}

	std::string ReadLine()
	{
		std::string line;
		if (!std::getline(std::cin, line)) return "c";
		return line;
	}

	void PrintStatus(const SessionStatus& s, std::string& lastLine)
	{
		char progress[128] = {};
		if (s.step == SessionStep::Measuring || s.step == SessionStep::Verifying)
			sprintf_s(progress, " patch %d/%d verify %d/%d", s.patchIndex, s.patchCount, s.verifyIndex, s.verifyCount);
		std::string line = std::string("[") + ToString(s.step) + "]" + progress;
		if (s.haveReading)
		{
			char xyz[96] = {};
			sprintf_s(xyz, " XYZ %.4f %.4f %.4f", s.lastXyz[0], s.lastXyz[1], s.lastXyz[2]);
			line += xyz;
		}
		if (!s.message.empty()) line += "  " + s.message;
		if (line != lastLine)
		{
			std::cout << line << "\n";
			lastLine = line;
		}
	}
}

int wmain(int argc, wchar_t** argv)
{
	Options options;
	if (!ParseArguments(argc, argv, options)) { Usage(); return 2; }
	if (options.help) { Usage(); return 0; }
	if (options.apply && options.colorSection.empty())
	{
		std::cerr << "--apply needs --color-section <section> (the Color profile whose calibration_lut keys change)\n";
		return 2;
	}
	SetConsoleCtrlHandler(OnConsoleControl, TRUE);

	// Settings: [calibration] from a separate file (VideoProcessor.cfg's own schema does not own the
	// section), then the command-line overrides.
	const std::string configPath = FullPath(options.configPath);
	const std::string settingsPath = FullPath(options.settingsPath.empty() ?
		FolderOf(configPath) + "\\calibration.cfg" : options.settingsPath);
	ConfigFile settingsFile;
	CalibrationSettings settings;
	if (FileExists(settingsPath))
	{
		if (!settingsFile.Load(settingsPath, ConfigFile::ReadPolicy::Fresh))
		{
			std::cerr << "cannot read " << settingsPath << "\n";
			return 2;
		}
		std::string validation;
		if (!ValidateConfig(settingsFile, validation))
		{
			std::cerr << "the [calibration] settings in " << settingsPath << " are invalid: " << validation << "\n";
			return 2;
		}
		settings = ReadSettings(settingsFile);
		std::cout << "settings: " << settingsPath << "\n";
	}
	else
		std::cout << "settings: defaults (" << settingsPath << " not found)\n";
	if (!options.meter.empty()) settings.meterModel = ConfigFile::NormalizeName(options.meter);
	if (!options.plan.empty() && !ParsePlan(ConfigFile::NormalizeName(options.plan), settings.plan))
	{
		std::cerr << "unknown plan " << options.plan << "\n";
		return 2;
	}
	if (options.lutSize > 0) settings.lutSize = options.lutSize;
	if (options.simulate) settings.meterModel = "simulated";

	SessionEnvironment environment;
	environment.configPath = configPath;
	environment.vpRoot = FolderOf(configPath);
	environment.activeColorSection = options.colorSection;
	environment.displayName = !settings.displayName.empty() ? settings.displayName :
		options.simulate ? std::string("simulated") : std::string("display");

	// Patterns: the renderer over the pipe, or a simulated screen.
	std::unique_ptr<IPatternSink> sink;
	CalibrationPipe::PipePatternSink* pipeSink = nullptr;
	SimulatedScreen* screen = nullptr;
	if (options.simulate)
	{
		PatternOutputInfo format;
		format.available = true;
		format.containerBits = 10;
		format.codeBits = 10;
		format.limitedRange = true;
		format.width = 3840;
		format.height = 2160;
		format.refreshHz = 60.0;
		screen = new SimulatedScreen(format);
		sink.reset(screen);
	}
	else
	{
		pipeSink = new CalibrationPipe::PipePatternSink(CalibrationPipe::Client());
		sink.reset(pipeSink);
		PatternOutputInfo info;
		if (!sink->GetOutput(info) && !sink->RendererAttached())
		{
			std::cerr << info.reason << "\n";
			return 3;
		}
		std::cout << "VP output: " << info.width << "x" << info.height << " container " << info.containerBits
			<< "-bit, codes " << info.codeBits << "-bit, " << (info.limitedRange ? "limited" : "full") << " range"
			<< (info.available ? "" : std::string(" (not ready: ") + info.reason + ")") << "\n";
	}

	// Meter.
	std::unique_ptr<ISessionClock> clock(options.simulate ? static_cast<ISessionClock*>(new FastClock()) :
		CreateSystemClock().release());
	double floorNits = 0.0;
	std::unique_ptr<IMeter> meter;
	std::string error;
	if (settings.meterModel == "simulated")
	{
		floorNits = 0.002;
		SimulatedMeterParams meterParams;
		meterParams.matrixError = SimulatedMeter::MatrixError(0.01, 7u);
		SimulatedScreenMeter::ShownFn shown = screen ?
			SimulatedScreenMeter::ShownFn([screen](Vec3& device) { return screen->Showing(device); }) :
			SimulatedScreenMeter::ShownFn([pipeSink](Vec3& device) { return pipeSink->Showing(device); });
		meter.reset(new SimulatedScreenMeter(shown, SimulatedDisplayParams(), meterParams, clock.get()));
	}
	else
		meter = CreateSpotreadMeter(settings, floorNits, error);
	if (!meter)
	{
		std::cerr << "meter: " << error << "\n";
		return 3;
	}
	std::cout << "meter: " << meter->Description() << "; plan " << ToString(settings.plan) << "; LUT "
		<< settings.lutSize << " points\n";

	CalibrationSession session(settings, environment, std::move(meter), floorNits, *sink, *clock);

	// The heartbeat keeps the renderer's abandoned-patterns timer quiet during long meter readings.
	std::atomic<bool> stopHeartbeat{ false };
	std::thread heartbeat;
	if (pipeSink)
		heartbeat = std::thread([pipeSink, &stopHeartbeat]()
		{
			while (!stopHeartbeat.load())
			{
				for (int i = 0; i < 50 && !stopHeartbeat.load(); ++i)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (!stopHeartbeat.load()) pipeSink->Heartbeat();
			}
		});

	session.Start();
	std::string lastLine;
	bool cancelSent = false;
	bool warmupAnnounced = false;
	while (!session.Finished())
	{
		if (g_cancel.load() && !cancelSent)
		{
			std::cout << "cancelling (patterns end, video returns)...\n";
			session.Cancel();
			cancelSent = true;
		}
		session.Touch();
		session.Step();
		const SessionStatus s = session.Status();
		PrintStatus(s, lastLine);
		if (cancelSent) continue;
		if (s.step == SessionStep::MeterCalibration || s.step == SessionStep::Placement || s.step == SessionStep::UserAction)
		{
			std::cout << s.instruction << "\n";
			if (options.yes) { session.Continue(); continue; }
			std::cout << "Enter = continue, s = skip, c = cancel: " << std::flush;
			const std::string answer = ReadLine();
			if (answer == "c") { session.Cancel(); cancelSent = true; }
			else if (answer == "s") session.Skip();
			else session.Continue();
		}
		else if (s.step == SessionStep::WarmUp)
		{
			if (options.skipWarmup) session.Skip();
			else if (!warmupAnnounced)
			{
				std::cout << s.instruction << " (" << static_cast<int>(s.warmupRemaining) << " s; --skip-warmup skips it)\n";
				warmupAnnounced = true;
			}
		}
		else if (s.step == SessionStep::Review)
		{
			std::cout << "\n" << s.review << "\n";
			if (!s.diff.empty()) std::cout << "Apply would change:\n" << s.diff << "\n";
			if (options.apply) session.Apply();
			else session.Discard();   // the run (its .cube files and journal) stays on disk
		}
	}
	stopHeartbeat.store(true);
	if (heartbeat.joinable()) heartbeat.join();

	const SessionStatus s = session.Status();
	PrintStatus(s, lastLine);
	const RunFolder& folder = session.Folder();
	if (!folder.runDir.empty())
	{
		std::cout << "run folder: " << folder.runDir << "\n";
		for (int slot = 0; slot < 3; ++slot)
			if (!folder.cube[slot].empty() && FileExists(folder.cube[slot]))
				std::cout << "  " << folder.cube[slot] << "  (calibration_lut value: " << folder.cubeConfigValue[slot] << ")\n";
	}
	if (s.step == SessionStep::Discarded)
		std::cout << "Not applied. To use a LUT, set calibration_lut_enabled: true and calibration_lut_<slot> in your Color "
			"profile to the value above, then Apply in Config.\n";
	if (s.step == SessionStep::Applied)
		std::cout << s.backup << "\nPress Apply in Config (or restart VP) so the renderer loads the new LUT.\n";
	return s.step == SessionStep::Applied || s.step == SessionStep::Discarded || s.step == SessionStep::Done ? 0 : 1;
}
