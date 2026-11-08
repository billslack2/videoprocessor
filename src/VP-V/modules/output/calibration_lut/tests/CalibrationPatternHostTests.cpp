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
#include <aclapi.h>
#include <sddl.h>

#include <cmath>
#include <cstddef>
#include <functional>

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

	TEST_METHOD(PatchCodesMustFitTheContainer)
	{
		using namespace CalibrationPatternHost;
		const uint16_t white8[3] = { 235, 235, 235 }, white10[3] = { 940, 940, 940 }, wtw10[3] = { 1023, 1023, 1023 };
		const uint16_t black[3] = { 16, 16, 16 }, over8[3] = { 256, 0, 0 };
		Assert::IsTrue(PatchFitsContainer(white8, black, 8));
		Assert::IsTrue(PatchFitsContainer(white10, black, 10));
		Assert::IsTrue(PatchFitsContainer(wtw10, black, 10), L"above-white codes are legal patches");
		Assert::IsFalse(PatchFitsContainer(white10, black, 8), L"10-bit codes on an 8-bit swapchain");
		Assert::IsFalse(PatchFitsContainer(white8, over8, 8), L"the background is checked too");
		Assert::IsFalse(PatchFitsContainer(white8, black, 16), L"float swapchain");
		Assert::IsFalse(PatchFitsContainer(white8, black, 0), L"output not known yet");
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
		std::string why;
		Assert::IsTrue(CalibrationPipe::DecodeSetPatternRequest(message, decoded, why));
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

	TEST_METHOD(PatternRequestFieldsAreValidated)
	{
		// Builds a valid request, lets `corrupt` change the struct and then its raw bytes, validates.
		const auto refused = [](const std::function<void(PatternRequest&, uint8_t*)>& corrupt) -> bool
		{
			PatternRequest request;
			request.active = true;
			request.patch[0] = 940;
			uint8_t raw[sizeof(PatternRequest)];
			corrupt(request, nullptr);
			std::memcpy(raw, &request, sizeof(raw));
			corrupt(request, raw);
			PatternRequest out;
			std::string why;
			return !CalibrationPipe::ValidatePatternRequestBytes(raw, sizeof(raw), out, why) && !why.empty();
		};
		Assert::IsFalse(refused([](PatternRequest&, uint8_t*) {}), L"a valid request passes");
		Assert::IsTrue(refused([](PatternRequest&, uint8_t* raw) { if (raw) raw[offsetof(PatternRequest, active)] = 2; }), L"bool byte 2");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.patch[1] = 1024; }), L"patch code above 10 bits");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.background[2] = 0xFFFF; }), L"background code");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.windowAreaPercent = 0.0f; }), L"window 0");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.windowAreaPercent = 100.5f; }), L"window over 100");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.windowAreaPercent = std::nanf(""); }), L"window NaN");
		Assert::IsTrue(refused([](PatternRequest& r, uint8_t*) { r.flags = 0x80; }), L"unknown flag");
		PatternRequest out;
		std::string why;
		uint8_t tooShort[8] = {};
		Assert::IsFalse(CalibrationPipe::ValidatePatternRequestBytes(tooShort, sizeof(tooShort), out, why), L"short struct");
	}

	TEST_METHOD(OutputInfoRepliesAreValidated)
	{
		PatternOutputInfo info = TenBitLimited();
		uint8_t raw[sizeof(PatternOutputInfo)];
		std::memcpy(raw, &info, sizeof(raw));
		PatternOutputInfo out;
		std::string why;
		Assert::IsTrue(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why));
		std::memset(raw + offsetof(PatternOutputInfo, reason), 'x', sizeof(info.reason));   // unterminated
		Assert::IsTrue(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why));
		Assert::AreEqual(sizeof(info.reason) - 1, std::strlen(out.reason), L"the reason is terminated");
		raw[offsetof(PatternOutputInfo, available)] = 7;
		Assert::IsFalse(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why), L"bool byte");
		PatternOutputInfo bad = TenBitLimited();
		bad.codeBits = 12;
		std::memcpy(raw, &bad, sizeof(raw));
		Assert::IsFalse(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why), L"code bits above the container");
		bad = TenBitLimited();
		bad.toneCurveCount = 65;
		std::memcpy(raw, &bad, sizeof(raw));
		Assert::IsFalse(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why), L"tone curve count");
		bad = TenBitLimited();
		std::memcpy(raw, &bad, sizeof(raw));
		const int32_t transfer = 9;
		std::memcpy(raw + offsetof(PatternOutputInfo, transfer), &transfer, sizeof(transfer));
		Assert::IsFalse(CalibrationPipe::ValidateOutputInfoBytes(raw, sizeof(raw), out, why), L"transfer enum");
	}

	TEST_METHOD(GarbageAndOversizedInputIsRefused)
	{
		bool called = false;
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [&called](PatternOutputInfo&) { called = true; return true; };
		handlers.setPattern = [&called](const PatternRequest&, std::string&) { called = true; return true; };
		bool active = false;
		CalibrationPipe::Message message;
		uint32_t seed = 12345;
		for (int round = 0; round < 500; ++round)
		{
			std::vector<uint8_t> garbage(1 + (seed % 600));
			for (uint8_t& b : garbage) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(seed >> 24); }
			const std::vector<uint8_t> reply = CalibrationPipe::HandleRequest(garbage.data(), garbage.size(), handlers, active);
			Assert::IsTrue(CalibrationPipe::Decode(reply.data(), reply.size(), message), L"every answer is a well-formed message");
			Assert::IsTrue((message.op & CalibrationPipe::ErrorFlag) != 0, L"garbage is an error reply");
		}
		// A valid header announcing a payload over the cap.
		CalibrationPipe::Message big;
		big.op = static_cast<uint32_t>(CalibrationPipe::Op::SetPattern);
		big.payload.assign(CalibrationPipe::MaximumMessageBytes, 0);
		const std::vector<uint8_t> oversized = CalibrationPipe::Encode(big);
		Assert::IsFalse(CalibrationPipe::Decode(oversized.data(), oversized.size(), message), L"over the message cap");
		CalibrationPipe::HandleRequest(oversized.data(), oversized.size(), handlers, active);
		// A valid magic and version with a payload length that lies.
		std::vector<uint8_t> liar = CalibrationPipe::EncodeGetOutputRequest();
		const uint32_t huge = 0x7FFFFFFFu;
		std::memcpy(liar.data() + 12, &huge, 4);
		Assert::IsFalse(CalibrationPipe::Decode(liar.data(), liar.size(), message), L"payload length mismatch");
		Assert::IsFalse(called, L"no handler ran for invalid input");
		Assert::IsFalse(active);
	}

	TEST_METHOD(PipeNameIsPerSessionAndTheDaclIsUserAndSystemOnly)
	{
		DWORD session = 0;
		Assert::IsTrue(ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE);
		Assert::IsTrue(CalibrationPipe::PipeNameForSession(session) == CalibrationPipe::DefaultPipeName());
		Assert::IsTrue(CalibrationPipe::DefaultPipeName().find(L"VideoProcessor-Calibration-" + std::to_wstring(session)) !=
			std::wstring::npos);
		std::wstring sddl;
		std::string error;
		Assert::IsTrue(CalibrationPipe::PipeSecuritySddl(sddl, error));
		Assert::AreEqual(0u, static_cast<unsigned>(sddl.find(L"D:P(A;;GA;;;S-1-")), L"protected DACL, the user's SID first");
		Assert::IsTrue(sddl.find(L"(A;;GA;;;SY)") != std::wstring::npos, L"SYSTEM");
		for (const wchar_t* broad : { L";WD)", L";AU)", L";BU)", L";BA)", L";IU)", L";AN)" })
			Assert::IsTrue(sddl.find(broad) == std::wstring::npos, broad);
		// The high-integrity no-write-up label exactly when this process is elevated.
		bool elevated = false;
		{
			HANDLE token = nullptr;
			Assert::IsTrue(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE);
			TOKEN_ELEVATION elevation{};
			DWORD size = 0;
			elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
				elevation.TokenIsElevated != 0;
			CloseHandle(token);
		}
		Assert::AreEqual(elevated, sddl.find(L"S:(ML;;NW;;;HI)") != std::wstring::npos, L"label iff elevated");

		// The live pipe carries exactly that DACL: two allow entries, the token user and SYSTEM.
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [](PatternOutputInfo& info) { info = TenBitLimited(); return true; };
		const std::wstring name = UniquePipeName(L"dacl");
		CalibrationPipe::Server server(name, handlers);
		Assert::IsTrue(server.Start(error));
		Assert::IsTrue(WaitNamedPipeW(name.c_str(), 2000) != FALSE);
		HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		Assert::IsTrue(client != INVALID_HANDLE_VALUE);
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		const DWORD status = GetSecurityInfo(client, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
			nullptr, nullptr, &dacl, nullptr, &descriptor);
		Assert::IsTrue(status == ERROR_SUCCESS);
		Assert::IsNotNull(dacl);
		Assert::IsTrue(dacl->AceCount == 2, L"exactly two entries");
		LPWSTR text = nullptr;
		Assert::IsTrue(ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION,
			&text, nullptr) != FALSE);
		const std::wstring actual(text);
		LocalFree(text);
		LocalFree(descriptor);
		CloseHandle(client);
		server.Stop();
		Assert::IsTrue(actual.find(L"SY)") != std::wstring::npos, actual.c_str());
		// "D:P(A;;GA;;;" is 12 characters; the user's SID follows up to the first ')'.
		const std::wstring userSid = sddl.substr(12, sddl.find(L")") - 12);
		Assert::IsTrue(actual.find(L";;;" + userSid + L")") != std::wstring::npos, actual.c_str());
	}

	TEST_METHOD(ConnectFailuresAreExplicit)
	{
		// An elevated VideoProcessor (high-integrity pipe) seen from a non-elevated tool: never a silent lockout.
		const std::string elevatedVp = CalibrationPipe::ConnectErrorText(ERROR_ACCESS_DENIED, false);
		Assert::IsTrue(elevatedVp == "VideoProcessor is running as administrator: run this tool as administrator too",
			std::wstring(elevatedVp.begin(), elevatedVp.end()).c_str());
		const std::string otherUser = CalibrationPipe::ConnectErrorText(ERROR_ACCESS_DENIED, true);
		Assert::IsTrue(otherUser.find("another user") != std::string::npos);
		const std::string missing = CalibrationPipe::ConnectErrorText(ERROR_FILE_NOT_FOUND, false);
		Assert::IsTrue(missing.find("VP_CALIBRATION_PIPE=1") != std::string::npos, L"says how to enable the pipe");
		Assert::IsTrue(CalibrationPipe::ConnectErrorText(ERROR_PIPE_BUSY, false).find("busy") != std::string::npos);
		Assert::IsTrue(CalibrationPipe::ConnectErrorText(1234, false).find("1234") != std::string::npos);
	}

	TEST_METHOD(OnlyOneClientPerSession)
	{
		const std::wstring name = L"Local\\VideoProcessor-Calibrate-test-" + std::to_wstring(GetCurrentProcessId()) +
			L"-" + std::to_wstring(GetTickCount64());
		std::string why;
		{
			CalibrationPipe::SingleClientGuard first;
			Assert::IsTrue(first.Acquire(name, why));
			CalibrationPipe::SingleClientGuard second;
			Assert::IsFalse(second.Acquire(name, why), L"a second client in the same session is refused");
			Assert::IsTrue(why.find("already running") != std::string::npos);
		}
		CalibrationPipe::SingleClientGuard after;
		Assert::IsTrue(after.Acquire(name, why), L"free again once the first client exits");
		Assert::IsTrue(CalibrationPipe::SingleClientGuard::DefaultName().find(L"Local\\") == 0, L"per session");
	}

	TEST_METHOD(ClientVerifiesTheServerRunsAsTheSameUser)
	{
		std::string why;
		Assert::IsTrue(CalibrationPipe::SameUserProcess(GetCurrentProcessId(), why), std::wstring(why.begin(), why.end()).c_str());
		why.clear();
		Assert::IsFalse(CalibrationPipe::SameUserProcess(0, why), L"the idle process is not this user");
		Assert::IsFalse(why.empty(), L"a reason is given");
		why.clear();
		Assert::IsFalse(CalibrationPipe::SameUserProcess(4, why), L"the System process is not this user");
		Assert::IsFalse(why.empty());
		// The client's full path (identification-only connect, server check, transaction) against a
		// server in this process, which runs as this user.
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [](PatternOutputInfo& info) { info = TenBitLimited(); return true; };
		const std::wstring name = UniquePipeName(L"verify");
		CalibrationPipe::Server server(name, handlers);
		std::string error;
		Assert::IsTrue(server.Start(error));
		CalibrationPipe::Client client(name);
		PatternOutputInfo info;
		bool ok = false;
		Assert::IsTrue(client.GetOutput(info, ok, error), std::wstring(error.begin(), error.end()).c_str());
		Assert::IsTrue(ok);
		server.Stop();
	}

	TEST_METHOD(LivePipeRefusesOversizedMessagesAndKeepsServing)
	{
		CalibrationPipe::Handlers handlers;
		handlers.getOutput = [](PatternOutputInfo& info) { info = TenBitLimited(); return true; };
		const std::wstring name = UniquePipeName(L"big");
		CalibrationPipe::Server server(name, handlers);
		std::string error;
		Assert::IsTrue(server.Start(error));
		std::vector<uint8_t> big(CalibrationPipe::MaximumMessageBytes * 4, 0xAB);
		std::vector<uint8_t> reply(CalibrationPipe::MaximumMessageBytes);
		DWORD read = 0;
		if (CallNamedPipeW(name.c_str(), big.data(), static_cast<DWORD>(big.size()), reply.data(),
			static_cast<DWORD>(reply.size()), &read, 3000))
		{
			CalibrationPipe::Message message;
			Assert::IsTrue(CalibrationPipe::Decode(reply.data(), read, message));
			Assert::IsTrue((message.op & CalibrationPipe::ErrorFlag) != 0, L"an oversized message gets an error reply");
		}
		CalibrationPipe::Client client(name);
		PatternOutputInfo info;
		bool ok = false;
		Assert::IsTrue(client.GetOutput(info, ok, error), L"the server still answers after an oversized message");
		Assert::IsTrue(ok);
		server.Stop();
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
