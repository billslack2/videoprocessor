// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "pch.h"
#include "CppUnitTest.h"

#include "CalibrationTestSupport.h"

#include <VP-V/modules/output/calibration_lut/calibration/ArgyllInstall.h>
#include <VP-V/modules/output/calibration_lut/calibration/Meter.h>
#include <VP-V/modules/output/calibration_lut/calibration/MeterCatalog.h>
#include <VP-V/modules/output/calibration_lut/calibration/Spotread.h>
#include <VP-V/modules/output/calibration_lut/calibration/Terminal.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Calibration;
using namespace CalibrationTest;

// Automatic calibration: the meter under the real Windows pseudo-console,
// driven exactly as VP drives spotread, with the fake spotread executable
// (src/VideoProcessor-FakeSpotread) playing the recorded transcripts in
// data/spotread: readings, a sensor-calibration prompt, a failed reading, no
// instrument, a hung instrument (timeout and the process ended), an unexpected
// exit, plus `spotread -?` for discovery. CPU only, no meter.
//
// CalibrationHardwareTests::RealMeterReadsThroughSpotread (VP_TEST_METER=1) is
// the same path on a real meter and ArgyllCMS install.
namespace
{
	std::string g_folder;

	std::string Env(const char* name)
	{
		char* value = nullptr;
		size_t size = 0;
		std::string out;
		if (_dupenv_s(&value, &size, name) == 0 && value) out = value;
		std::free(value);
		return out;
	}

	std::string ReadText(const std::string& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream s;
		s << file.rdbuf();
		return s.str();
	}

	// A scratch folder holding the fake as spotread.exe, under a name with a
	// space (VP must quote the path).
	std::string FakeFolder()
	{
		if (!g_folder.empty()) return g_folder;
		char base[MAX_PATH];
		GetTempPathA(MAX_PATH, base);
		const std::string folder = std::string(base) + "vp spotread fake " + std::to_string(GetCurrentProcessId());
		CreateDirectoryA(folder.c_str(), nullptr);
		CreateDirectoryA((folder + "\\bin").c_str(), nullptr);
		const std::string fake = FakeSpotreadPath();
		Assert::IsTrue(GetFileAttributesA(fake.c_str()) != INVALID_FILE_ATTRIBUTES,
			L"x64\\Release\\test-tools\\fake_spotread.exe is missing: build VideoProcessor-FakeSpotread");
		Assert::IsTrue(CopyFileA(fake.c_str(), (folder + "\\bin\\spotread.exe").c_str(), FALSE) != FALSE);
		g_folder = folder;
		return folder;
	}

	std::wstring Wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }

	struct FakeRun
	{
		std::string log;
		std::unique_ptr<SpotreadMeter> meter;

		FakeRun(const std::string& script, double readTimeout = 10.0, const SpotreadOptions& arguments = SpotreadOptions())
		{
			const std::string folder = FakeFolder();
			log = folder + "\\" + script + ".log";
			DeleteFileA(log.c_str());
			SetEnvironmentVariableA("FAKE_SPOTREAD_SCRIPT", TranscriptPath(script).c_str());
			SetEnvironmentVariableA("FAKE_SPOTREAD_LOG", log.c_str());
			SpotreadMeterOptions o;
			o.spotread = Wide(folder + "\\bin\\spotread.exe");
			o.arguments = arguments;
			o.readTimeoutSeconds = readTimeout;
			o.openTimeoutSeconds = 15.0;
			o.closeTimeoutSeconds = 2.0;
			meter.reset(new SpotreadMeter(o, [] { return CreateConPtyTerminal(); }));
		}
		~FakeRun()
		{
			if (meter) Logger::WriteMessage(("transcript:\n" + meter->Transcript() + "\nlog:\n" + Log() + "\n").c_str());
			meter.reset();
			SetEnvironmentVariableA("FAKE_SPOTREAD_SCRIPT", nullptr);
			SetEnvironmentVariableA("FAKE_SPOTREAD_LOG", nullptr);
		}
		std::string Log() const { return ReadText(log); }
	};

	std::wstring Message(const MeterResult& r)
	{
		return Wide(std::string(ToString(r.status)) + ": " + r.message);
	}
}

namespace VideoProcessorTest
{
	TEST_CLASS(CalibrationMeterTests)
	{
	public:
		TEST_CLASS_INITIALIZE(RequireConPty)
		{
			Assert::IsTrue(ConPtyAvailable(), L"this Windows has no pseudo-console");
		}

		TEST_METHOD(FakeSpotreadReadsThroughThePseudoConsole)
		{
			SpotreadOptions args;
			args.displayType = "l";
			FakeRun run("i1d3_session.txt", 10.0, args);
			MeterResult r = run.meter->Open();
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			r = run.meter->Read();
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			Assert::AreEqual(95.047, r.xyz[0], 1e-6);
			Assert::AreEqual(100.0, r.xyz[1], 1e-6);
			Assert::AreEqual(108.883, r.xyz[2], 1e-6);
			r = run.meter->Read();
			Assert::AreEqual(21.26, r.xyz[1], 1e-6);
			r = run.meter->Read();
			Assert::AreEqual(0.013, r.xyz[1], 1e-9);
			run.meter->Close();
			const std::string log = run.Log();
			Logger::WriteMessage(("fake log:\n" + log).c_str());
			Assert::IsTrue(log.find("args: -e -x -y l") != std::string::npos);
			// Three spaces (readings) then q, through the console's key input.
			size_t keys = 0, pos = 0;
			while ((pos = log.find("key: 32", pos)) != std::string::npos) { ++keys; ++pos; }
			Assert::AreEqual(size_t(3), keys);
			Assert::IsTrue(log.find("key: 113") != std::string::npos);
		}

		TEST_METHOD(FakeSpotreadCalibrationPrompt)
		{
			FakeRun run("i1pro_calibration.txt");
			MeterResult r = run.meter->Open();
			Assert::IsTrue(r.status == MeterStatus::NeedsUserAction, Message(r).c_str());
			Assert::IsTrue(r.message.find("white calibration reference") != std::string::npos);
			r = run.meter->Continue();
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			r = run.meter->Read();
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			Assert::AreEqual(50.0, r.xyz[1], 1e-6);
		}

		TEST_METHOD(FakeSpotreadFailedReading)
		{
			FakeRun run("read_failure.txt");
			Assert::IsTrue(run.meter->Open().status == MeterStatus::Ok);
			MeterResult r = run.meter->Read();
			Assert::IsTrue(r.status == MeterStatus::ReadFailed, Message(r).c_str());
			Assert::IsTrue(r.error == SpotreadErrorKind::TooDark);
			r = run.meter->Read();
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			Assert::AreEqual(0.047, r.xyz[1], 1e-9);
		}

		TEST_METHOD(FakeSpotreadNoInstrument)
		{
			FakeRun run("no_instrument.txt");
			const MeterResult r = run.meter->Open();
			Assert::IsTrue(r.status == MeterStatus::Fatal, Message(r).c_str());
			Assert::IsTrue(r.error == SpotreadErrorKind::NoInstrument, Message(r).c_str());
			Assert::IsFalse(run.meter->IsOpen());
		}

		TEST_METHOD(FakeSpotreadHangTimesOutAndIsEnded)
		{
			FakeRun run("hang.txt", 3.0);
			Assert::IsTrue(run.meter->Open().status == MeterStatus::Ok);
			const auto start = std::chrono::steady_clock::now();
			const MeterResult r = run.meter->Read();
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			Assert::IsTrue(r.status == MeterStatus::Timeout, Message(r).c_str());
			Assert::IsTrue(seconds >= 2.9 && seconds < 12.0, std::to_wstring(seconds).c_str());
			Assert::IsFalse(run.meter->IsOpen());
			// The meter can be opened again (the hung process was ended, not left holding the console).
			FakeRun again("i1d3_session.txt");
			Assert::IsTrue(again.meter->Open().status == MeterStatus::Ok);
			Assert::IsTrue(again.meter->Read().status == MeterStatus::Ok);
		}

		TEST_METHOD(FakeSpotreadUnexpectedExit)
		{
			FakeRun run("crash.txt");
			Assert::IsTrue(run.meter->Open().status == MeterStatus::Ok);
			const MeterResult r = run.meter->Read();
			Assert::IsTrue(r.status == MeterStatus::Fatal, Message(r).c_str());
			Assert::IsTrue(r.message.find("exit code 3") != std::string::npos, Message(r).c_str());
		}

		TEST_METHOD(ProbeRunsUsageAndExplainsAMissingInstall)
		{
			const std::string folder = FakeFolder();
			const ArgyllProbe probe = ProbeArgyll(Wide(folder));
			Logger::WriteMessage((probe.Summary() + "\n").c_str());
			Assert::IsTrue(probe.found);
			Assert::IsTrue(probe.ran, Wide(probe.error).c_str());
			Assert::IsTrue(probe.usage.recognised);
			Assert::AreEqual(std::string("3.5.0"), probe.usage.version);
			Assert::AreEqual(size_t(1), probe.usage.instruments.size());
			Assert::AreEqual(std::string("i1d3"), std::string(MatchInstrument(probe.usage.instruments[0].name)->id));
			Assert::IsTrue(probe.Summary().find("Meter 1: X-Rite i1 DisplayPro") != std::string::npos);
			const ArgyllProbe missing = ProbeArgyll(L"Z:\\Nowhere\\Argyll");
			Assert::IsFalse(missing.found);
			Assert::IsTrue(missing.Summary().find("ArgyllCMS was not found") == 0);
			Assert::IsTrue(missing.Summary().find("Z:\\Nowhere\\Argyll") != std::string::npos);
		}

		TEST_METHOD(RunCapturedEndsAHungProgram)
		{
			const std::string folder = FakeFolder();
			SetEnvironmentVariableA("FAKE_SPOTREAD_SCRIPT", TranscriptPath("hang_now.txt").c_str());
			std::string output, error;
			unsigned long code = 0;
			const std::wstring exe = Wide(folder + "\\bin\\spotread.exe");
			const auto start = std::chrono::steady_clock::now();
			const bool ok = RunCaptured(exe, L"\"" + exe + L"\" -e", 1500, output, code, error);
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			SetEnvironmentVariableA("FAKE_SPOTREAD_SCRIPT", nullptr);
			Assert::IsFalse(ok);
			Assert::IsTrue(error.find("did not finish") != std::string::npos, Wide(error).c_str());
			Assert::IsTrue(seconds >= 1.4 && seconds < 6.0, std::to_wstring(seconds).c_str());
		}
	};

	// Gated: a real meter on a real ArgyllCMS install (VP_TEST_METER=1). Optional:
	// VP_TEST_ARGYLL (the Argyll folder; default: the usual search), VP_TEST_METER_PORT.
	// It needs no display: the meter reads whatever it faces (aim it at a lit
	// screen or a lamp). A spectro asking for its calibration is told to be
	// placed on its tile (VP_TEST_METER_CALIBRATED=1 continues after 10 s).
	// The transcript is saved to %TEMP%\vp-spotread-transcript.txt for
	// data/spotread (recorded_<meter>.txt).
	TEST_CLASS(CalibrationHardwareTests)
	{
	public:
		BEGIN_TEST_METHOD_ATTRIBUTE(RealMeterReadsThroughSpotread)
			TEST_METHOD_ATTRIBUTE(L"Category", L"MeterHardware")
		END_TEST_METHOD_ATTRIBUTE()
		TEST_METHOD(RealMeterReadsThroughSpotread)
		{
			if (Env("VP_TEST_METER") != "1")
			{
				Logger::WriteMessage("SKIP-NO-METER RealMeterReadsThroughSpotread (set VP_TEST_METER=1)\n");
				return;
			}
			const ArgyllProbe probe = ProbeArgyll(Wide(Env("VP_TEST_ARGYLL")));
			Logger::WriteMessage((probe.Summary() + "\n").c_str());
			Assert::IsTrue(probe.found, Wide(probe.Summary()).c_str());
			Assert::IsTrue(probe.ran && probe.usage.recognised, Wide(probe.Summary()).c_str());
			Assert::IsFalse(probe.usage.instruments.empty(), L"no meter connected");
			SpotreadMeterOptions o;
			o.spotread = probe.spotread;
			o.arguments.port = std::atoi(Env("VP_TEST_METER_PORT").c_str());
			o.readTimeoutSeconds = 60.0;
			o.openTimeoutSeconds = 90.0;
			const std::string name = probe.usage.instruments[0].name;
			const MeterModel* model = MatchInstrument(name);
			o.instrumentName = name;
			SpotreadMeter meter(o, [] { return CreateConPtyTerminal(); });
			MeterResult r = meter.Open();
			if (r.status == MeterStatus::NeedsUserAction)
			{
				Logger::WriteMessage(("meter asks: " + r.message + "\n").c_str());
				Assert::IsTrue(Env("VP_TEST_METER_CALIBRATED") == "1",
					L"the meter needs its sensor calibration: place it, then rerun with VP_TEST_METER_CALIBRATED=1");
				Sleep(10000);
				r = meter.Continue();
			}
			Assert::IsTrue(r.status == MeterStatus::Ok, Message(r).c_str());
			for (int i = 0; i < 3; ++i)
			{
				r = meter.Read();
				Logger::WriteMessage(("reading " + std::to_string(i + 1) + ": " + std::string(ToString(r.status)) + " XYZ " +
					std::to_string(r.xyz[0]) + " " + std::to_string(r.xyz[1]) + " " + std::to_string(r.xyz[2]) + " in " +
					std::to_string(r.seconds) + " s " + r.message + "\n").c_str());
				Assert::IsTrue(r.status == MeterStatus::Ok || r.status == MeterStatus::ReadFailed, Message(r).c_str());
				if (r.status == MeterStatus::Ok)
				{
					Assert::IsTrue(std::isfinite(r.xyz[0]) && std::isfinite(r.xyz[1]) && std::isfinite(r.xyz[2]));
					Assert::IsTrue(r.xyz[1] >= -0.01);
				}
			}
			meter.Close();
			char base[MAX_PATH];
			GetTempPathA(MAX_PATH, base);
			std::ofstream(std::string(base) + "vp-spotread-transcript.txt", std::ios::binary) << meter.Transcript();
			Logger::WriteMessage(("model: " + std::string(model ? model->id : "unknown") + "\n").c_str());
		}
	};
}
