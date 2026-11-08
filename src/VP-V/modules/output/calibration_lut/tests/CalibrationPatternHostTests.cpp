// Copyright(C) 2026 VP-V contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "pch.h"
#include "CppUnitTest.h"

#include <VP-V/modules/output/calibration_lut/calibration/CalibrationSession.h>
#include <VP-V/modules/output/calibration_lut/calibration/CalibrationSimulation.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPatternPolicy.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPipe.h>
#include <VP-V/modules/output/calibration_lut/host/CalibrationPipeSink.h>
#include <vprenderer/LibplaceboDisplayLut.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Calibration;

// Automatic calibration in VideoProcessor: the renderer pattern host's pure policy, the calibration
// pipe (wire format, request handling, a live local pipe, the abandoned-patterns timer) and the
// CLI driver's path end to end without a GPU: CalibrationSession -> PipePatternSink -> pipe ->
// server -> a simulated screen, a simulated meter, and the .cube files on disk. CPU only.
namespace
{
	std::wstring UniquePipeName(const wchar_t* tag)
	{
		return std::wstring(L"\\\\.\\pipe\\VideoProcessor-Calibration-test-") + tag + L"-" +
			std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
	}

	PatternOutputInfo TenBitLimited()
	{
		PatternOutputInfo info;
		info.available = true;
		info.containerBits = 10;
		info.codeBits = 10;
		info.limitedRange = true;
		info.width = 1920;
		info.height = 1080;
		info.refreshHz = 60.0;
		return info;
	}

	std::string TempFolder(const char* tag)
	{
		char base[MAX_PATH] = {};
		GetTempPathA(MAX_PATH, base);
		const std::string folder = std::string(base) + "vp-autocal-" + tag + "-" + std::to_string(GetCurrentProcessId()) +
			"-" + std::to_string(GetTickCount64());
		CreateDirectoryA(folder.c_str(), nullptr);
		return folder;
	}
}

TEST_CLASS(CalibrationPatternHostPolicyTests)
{
public:
	TEST_METHOD(ContainerAndCodeBits)
	{
		using namespace CalibrationPatternHost;
		Assert::AreEqual(10u, ContainerBits(DXGI_FORMAT_R10G10B10A2_UNORM));
		Assert::AreEqual(8u, ContainerBits(DXGI_FORMAT_B8G8R8A8_UNORM));
		Assert::AreEqual(8u, ContainerBits(DXGI_FORMAT_R8G8B8A8_UNORM));
		Assert::AreEqual(16u, ContainerBits(DXGI_FORMAT_R16G16B16A16_FLOAT));
		Assert::AreEqual(0u, ContainerBits(DXGI_FORMAT_UNKNOWN));
		Assert::AreEqual(8u, CodeBits(10, "8"), L"8-in-10");
		Assert::AreEqual(10u, CodeBits(10, "10"));
		Assert::AreEqual(10u, CodeBits(10, "auto"));
		Assert::AreEqual(10u, CodeBits(10, "output"));
		Assert::AreEqual(8u, CodeBits(8, "10"), L"a link never exceeds its container");
		Assert::AreEqual(16u, CodeBits(16, "8"), L"float is reported as is (and refused by the session)");
	}

	TEST_METHOD(PatchCodesNeedAnIntegerSwapchain)
	{
		using namespace CalibrationPatternHost;
		Assert::AreEqual(1023.0, PatchMaxCode(DXGI_FORMAT_R10G10B10A2_UNORM));
		Assert::AreEqual(255.0, PatchMaxCode(DXGI_FORMAT_B8G8R8A8_UNORM));
		Assert::AreEqual(0.0, PatchMaxCode(DXGI_FORMAT_R16G16B16A16_FLOAT));
	}

	TEST_METHOD(OutputQueryDoesNotSpinWhilePatchesShow)
	{
		using namespace CalibrationPatternHost;
		Assert::AreEqual(1, FreshOutputAttempts(true, 10));
		Assert::AreEqual(100, FreshOutputAttempts(true, 0), L"nothing cached yet: wait for the render thread");
		Assert::AreEqual(100, FreshOutputAttempts(false, 10));
	}
};

TEST_CLASS(CalibrationPipeTests)
{
public:
	TEST_METHOD(SetPatternRoundTrip)
	{
		PatternRequest request;
		request.active = true;
		request.generation = 42;
		request.patch[0] = 940; request.patch[1] = 64; request.patch[2] = 4;
		request.background[1] = 64;
		request.windowAreaPercent = 10.0f;
		request.flags = PatternReadback;
		const std::vector<uint8_t> bytes = CalibrationPipe::EncodeSetPatternRequest(request);
		CalibrationPipe::Message message;
		Assert::IsTrue(CalibrationPipe::Decode(bytes.data(), bytes.size(), message));
		PatternRequest decoded;
		Assert::IsTrue(CalibrationPipe::DecodeSetPatternRequest(message, decoded));
		Assert::IsTrue(decoded.active);
		Assert::AreEqual<uint64_t>(42, decoded.generation);
		Assert::AreEqual<uint16_t>(940, decoded.patch[0]);
		Assert::AreEqual<uint16_t>(4, decoded.patch[2]);
		Assert::AreEqual<uint16_t>(64, decoded.background[1]);
		Assert::AreEqual(10.0f, decoded.windowAreaPercent);
		Assert::AreEqual(PatternReadback, decoded.flags);
	}

	TEST_METHOD(OutputAndErrorReplies)
	{
		PatternOutputInfo info = TenBitLimited();
		info.presentedGeneration = 7;
		info.presentsOfGeneration = 2;
		strncpy_s(info.reason, sizeof(info.reason), "fine", _TRUNCATE);
		std::vector<uint8_t> bytes = CalibrationPipe::EncodeOutputReply(true, info);
		CalibrationPipe::Message message;
		Assert::IsTrue(CalibrationPipe::Decode(bytes.data(), bytes.size(), message));
		PatternOutputInfo decoded;
		bool ok = false;
		std::string error;
		Assert::IsTrue(CalibrationPipe::DecodeOutputReply(message, ok, decoded, error));
		Assert::IsTrue(ok);
		Assert::AreEqual(10u, decoded.containerBits);
		Assert::AreEqual<uint64_t>(7, decoded.presentedGeneration);
		Assert::AreEqual(std::string("fine"), std::string(decoded.reason));

		bytes = CalibrationPipe::EncodeError(static_cast<uint32_t>(CalibrationPipe::Op::GetOutput), "refused here");
		Assert::IsTrue(CalibrationPipe::Decode(bytes.data(), bytes.size(), message));
		Assert::IsFalse(CalibrationPipe::DecodeOutputReply(message, ok, decoded, error));
		Assert::AreEqual(std::string("refused here"), error);
	}

	TEST_METHOD(MalformedMessagesAreRefused)
	{
		std::vector<uint8_t> bytes = CalibrationPipe::EncodeGetOutputRequest();
		CalibrationPipe::Message message;
		Assert::IsTrue(CalibrationPipe::Decode(bytes.data(), bytes.size(), message));
		std::vector<uint8_t> bad = bytes;
		bad[0] ^= 0xFF;
		Assert::IsFalse(CalibrationPipe::Decode(bad.data(), bad.size(), message), L"magic");
		bad = bytes;
		bad.push_back(0);
		Assert::IsFalse(CalibrationPipe::Decode(bad.data(), bad.size(), message), L"payload size");
		Assert::IsFalse(CalibrationPipe::Decode(bytes.data(), 8, message), L"short header");

		// A request whose struct size differs (a tool from another build) is refused by the handler.
		CalibrationPipe::Message request;
		request.op = static_cast<uint32_t>(CalibrationPipe::Op::SetPattern);
		request.payload = { 4, 0, 0, 0, 1, 2, 3, 4 };
		const std::vector<uint8_t> wrongSize = CalibrationPipe::Encode(request);
		bool active = false;
		bool called = false;
		CalibrationPipe::Handlers handlers;
		handlers.setPattern = [&called](const PatternRequest&, std::string&) { called = true; return true; };
		const std::vector<uint8_t> reply = CalibrationPipe::HandleRequest(wrongSize.data(), wrongSize.size(), handlers, active);
		Assert::IsTrue(CalibrationPipe::Decode(reply.data(), reply.size(), message));
		Assert::IsTrue((message.op & CalibrationPipe::ErrorFlag) != 0);
		Assert::IsFalse(called);
		Assert::IsFalse(active);
	}

	TEST_METHOD(HandlerTracksWhetherPatternsAreOn)
	{
		CalibrationPipe::Handlers handlers;
		handlers.setPattern = [](const PatternRequest&, std::string&) { return true; };
		bool active = false;
		PatternRequest on;
		on.active = true;
		std::vector<uint8_t> bytes = CalibrationPipe::EncodeSetPatternRequest(on);
		CalibrationPipe::HandleRequest(bytes.data(), bytes.size(), handlers, active);
		Assert::IsTrue(active);
		PatternRequest off;
		bytes = CalibrationPipe::EncodeSetPatternRequest(off);
		CalibrationPipe::HandleRequest(bytes.data(), bytes.size(), handlers, active);
		Assert::IsFalse(active);
	}

	TEST_METHOD(LivePipeServesBothCalls)
	{
		std::atomic<int> gets{ 0 };
		PatternRequest lastShown;
		std::mutex shownMutex;
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [&gets](PatternOutputInfo& info) { ++gets; info = TenBitLimited(); return true; };
		handlers.setPattern = [&](const PatternRequest& request, std::string& reason)
		{
			std::lock_guard<std::mutex> guard(shownMutex);
			lastShown = request;
			reason = "shown";
			return true;
		};
		const std::wstring name = UniquePipeName(L"live");
		CalibrationPipe::Server server(name, handlers);
		std::string error;
		Assert::IsTrue(server.Start(error), std::wstring(error.begin(), error.end()).c_str());

		CalibrationPipe::Server second(name, handlers);
		Assert::IsFalse(second.Start(error), L"one server per pipe name");

		CalibrationPipe::Client client(name);
		PatternOutputInfo info;
		bool ok = false;
		Assert::IsTrue(client.GetOutput(info, ok, error), std::wstring(error.begin(), error.end()).c_str());
		Assert::IsTrue(ok);
		Assert::AreEqual(10u, info.codeBits);
		PatternRequest request;
		request.active = true;
		request.generation = 3;
		request.patch[0] = 502;
		std::string reason;
		Assert::IsTrue(client.SetPattern(request, ok, reason, error));
		Assert::IsTrue(ok);
		Assert::AreEqual(std::string("shown"), reason);
		for (int i = 0; i < 20; ++i)
			Assert::IsTrue(client.GetOutput(info, ok, error), L"repeated calls on one server instance");
		Assert::AreEqual(21, gets.load());
		{
			std::lock_guard<std::mutex> guard(shownMutex);
			Assert::AreEqual<uint16_t>(502, lastShown.patch[0]);
		}
		server.Stop();
		Assert::IsFalse(client.GetOutput(info, ok, error), L"no pipe after Stop");
		Assert::IsTrue(error.find("VP_CALIBRATION_PIPE") != std::string::npos, L"the error says how to enable the pipe");
	}

	TEST_METHOD(AbandonedPatternsEndWithoutTheClient)
	{
		std::atomic<int> ends{ 0 };
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [](PatternOutputInfo& info) { info = TenBitLimited(); return true; };
		handlers.setPattern = [&ends](const PatternRequest& request, std::string&) { if (!request.active) ++ends; return true; };
		const std::wstring name = UniquePipeName(L"idle");
		CalibrationPipe::Server server(name, handlers, 0.5);
		std::string error;
		Assert::IsTrue(server.Start(error));
		CalibrationPipe::Client client(name);
		PatternRequest request;
		request.active = true;
		bool ok = false;
		std::string reason;
		Assert::IsTrue(client.SetPattern(request, ok, reason, error));
		// No further request: the server ends the patches itself (it wakes once a second).
		for (int i = 0; i < 40 && ends.load() == 0; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Assert::AreEqual(1, ends.load());
		server.Stop();
		Assert::AreEqual(1, ends.load(), L"Stop does not call the handler again");
	}
};

TEST_CLASS(CalibrationCliPathTests)
{
public:
	// The CLI driver's path without a GPU: the session's patches cross the real pipe to a server
	// backed by a simulated screen; the simulated meter reads what the pipe sink last showed.
	TEST_METHOD(SimulatedRunThroughThePipeWritesCubeFiles)
	{
		SimulatedScreen screen(TenBitLimited());
		std::mutex screenMutex;
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [&](PatternOutputInfo& info)
		{
			std::lock_guard<std::mutex> guard(screenMutex);
			const bool ok = screen.GetOutput(info);
			info.presentedGeneration = screen.LastRequest().generation;   // shown at once
			info.presentsOfGeneration = 2;
			return ok;
		};
		handlers.setPattern = [&](const PatternRequest& request, std::string& reason)
		{
			std::lock_guard<std::mutex> guard(screenMutex);
			return screen.Show(request, reason);
		};
		const std::wstring name = UniquePipeName(L"cli");
		CalibrationPipe::Server server(name, handlers);
		std::string error;
		Assert::IsTrue(server.Start(error));

		CalibrationPipe::PipePatternSink sink{ CalibrationPipe::Client(name) };
		ManualClock clock;
		SimulatedMeterParams meterParams;
		meterParams.matrixError = SimulatedMeter::MatrixError(0.01, 7u);
		std::unique_ptr<IMeter> meter(new SimulatedScreenMeter([&sink](Vec3& device) { return sink.Showing(device); },
			SimulatedDisplayParams(), meterParams, &clock));

		CalibrationSettings settings;
		settings.meterModel = "simulated";
		settings.plan = PlanChoice::Quick;
		settings.lutSize = 17;
		settings.warmupMinutes = 0;
		settings.verifyAfter = false;
		settings.settleMs = 40;
		SessionEnvironment environment;
		environment.vpRoot = TempFolder("cli");
		environment.configPath = environment.vpRoot + "\\VideoProcessor.cfg";
		environment.displayName = "simulated";
		CalibrationSession session(settings, environment, std::move(meter), 0.002, sink, clock);
		session.Start();
		for (int i = 0; i < 20000 && !session.Finished(); ++i)
		{
			session.Touch();
			session.Step();
			const SessionStatus s = session.Status();
			if (s.step == SessionStep::MeterCalibration || s.step == SessionStep::Placement || s.step == SessionStep::UserAction)
				session.Continue();
			else if (s.step == SessionStep::WarmUp)
				session.Skip();
			else if (s.step == SessionStep::Review)
				session.Discard();
		}
		const SessionStatus s = session.Status();
		server.Stop();
		Assert::IsTrue(s.step == SessionStep::Discarded, std::wstring(s.message.begin(), s.message.end()).c_str());
		Assert::IsTrue(s.measured > 20, L"patches were measured through the pipe");
		Assert::IsFalse(screen.PatternActive(), L"the session returned VP to video");

		int cubes = 0;
		for (int slot = 0; slot < 3; ++slot)
		{
			const std::string& path = session.Folder().cube[slot];
			if (path.empty()) continue;
			std::ifstream file(path);
			if (!file) continue;
			std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			Assert::IsTrue(content.find("LUT_3D_SIZE 17") != std::string::npos, L"a 17-point .cube");
			// VP's own display-LUT parser (the renderer's LoadDisplayLut) accepts the file.
			LibplaceboDisplayLut::LoadResult loaded = LibplaceboDisplayLut::Load(nullptr, path);
			Assert::IsTrue(loaded.status == LibplaceboDisplayLut::Status::ACTIVE, L"LibplaceboDisplayLut accepts the .cube");
			pl_lut_free(&loaded.lut);
			++cubes;
		}
		Assert::IsTrue(cubes >= 1, L"at least one .cube written");
	}
};
