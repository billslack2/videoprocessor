#include "pch.h"
#include "CppUnitTest.h"

#include <AnalysisDiagnosticTrace.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	struct OwnerCounters
	{
		std::atomic<int> retained{ 0 };
		std::atomic<int> released{ 0 };
	};

	struct OwnerSource
	{
		std::shared_ptr<const std::vector<uint8_t>> bytes;
		OwnerCounters* counters = nullptr;
	};

	struct OwnerToken
	{
		std::shared_ptr<const void> owner;
		OwnerCounters* counters = nullptr;
	};

	std::string TempDirectory();
	void RemoveTree(const std::string& directory);

	void* __cdecl RetainOwner(const void* context)
	{
		auto* source = const_cast<OwnerSource*>(
			static_cast<const OwnerSource*>(context));
		OwnerToken* token = new (std::nothrow) OwnerToken();
		if (token != nullptr)
		{
			token->owner = source->bytes;
			token->counters = source->counters;
			source->counters->retained.fetch_add(1, std::memory_order_relaxed);
		}
		return token;
	}

	void __cdecl ReleaseOwner(void* token)
	{
		auto* retained = static_cast<OwnerToken*>(token);
		retained->counters->released.fetch_add(1, std::memory_order_relaxed);
		delete retained;
	}

	struct TraceFixture
	{
		AnalysisDiagnosticTraceOptions options;
		std::string root;
		std::unique_ptr<AnalysisDiagnosticTrace> trace;

		explicit TraceFixture(
			bool frameCaptureSupported = false, uint32_t captureMs = 40)
		{
			static std::atomic<uint64_t> sequence{ 0 };
			root = TempDirectory() + "\\vp-analysis-trace-test-" +
				std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(GetTickCount64()) + "-" +
				std::to_string(sequence.fetch_add(1));
			options.outputRoot = root;
			options.preRollMs = 1000;
			options.captureMs = captureMs;
			options.maxRecordHistory = 32;
			options.maxCaptureRecords = 32;
			options.maxFrameBytes = 8;
			options.maxCaptureFrameBytes = 32;
			options.maxOutstandingFrames = 2;
			trace.reset(new AnalysisDiagnosticTrace(options));
			trace->SetProducerAvailable(true, frameCaptureSupported);
		}

		~TraceFixture()
		{
			trace.reset();
			RemoveTree(root);
		}
	};

	std::string TempDirectory()
	{
		char path[MAX_PATH] = {};
		const DWORD length = GetTempPathA(MAX_PATH, path);
		if (length == 0 || length >= MAX_PATH)
			return ".";
		std::string result(path, length);
		while (!result.empty() &&
			(result.back() == '\\' || result.back() == '/'))
			result.pop_back();
		return result;
	}

	void RemoveTree(const std::string& directory)
	{
		const std::string pattern = directory + "\\*";
		WIN32_FIND_DATAA entry = {};
		HANDLE search = FindFirstFileA(pattern.c_str(), &entry);
		if (search != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (strcmp(entry.cFileName, ".") == 0 ||
					strcmp(entry.cFileName, "..") == 0)
					continue;
				const std::string child = directory + "\\" + entry.cFileName;
				if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					RemoveTree(child);
				else
					DeleteFileA(child.c_str());
			} while (FindNextFileA(search, &entry));
			FindClose(search);
		}
		RemoveDirectoryA(directory.c_str());
	}

	AnalysisDiagnosticRecordV1 Event(const char* reason)
	{
		AnalysisDiagnosticRecordV1 record;
		record.kind = AnalysisDiagnosticRecordKindV1::Event;
		strncpy_s(record.stage, "test", _TRUNCATE);
		strncpy_s(record.reason, reason, _TRUNCATE);
		record.frameIdentity = 17;
		record.analysisDomainKey = 23;
		return record;
	}

	bool WaitForWriter(AnalysisDiagnosticTrace& trace, uint32_t timeoutMs = 2000)
	{
		const uint64_t deadline = GetTickCount64() + timeoutMs;
		while (GetTickCount64() < deadline)
		{
			const AnalysisDiagnosticTraceStatus status = trace.Status();
			if (!status.recording && !status.captureWriting)
				return true;
			Sleep(5);
		}
		return false;
	}

	std::string CapturePath(const AnalysisDiagnosticTrace& trace)
	{
		const auto status = trace.Status();
		return status.outputPath;
	}
}

namespace Tests
{
	TEST_CLASS(AnalysisDiagnosticTraceTests)
	{
	public:
		TEST_METHOD(CaptureIncludesTenSecondStyleRollingHistoryAndPostTriggerRecords)
		{
			TraceFixture fixture(false, 1000);
			fixture.trace->Submit(Event("preroll-record"));
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(false)));
			fixture.trace->Submit(Event("post-trigger-record"));
			fixture.trace->StopCapture();

			Assert::IsTrue(WaitForWriter(*fixture.trace));
			const std::string records =
				CapturePath(*fixture.trace) + "\\analysis.jsonl";
			std::ifstream input(records.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(input.is_open());
			const std::string contents(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(contents.find("preroll-record") != std::string::npos);
			Assert::IsTrue(contents.find("post-trigger-record") != std::string::npos);

			const std::string summaryPath =
				CapturePath(*fixture.trace) + "\\capture-summary.json";
			std::ifstream summaryInput(
				summaryPath.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(summaryInput.is_open());
			const std::string summary(
				(std::istreambuf_iterator<char>(summaryInput)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(summary.find(
				"\"session\":{\"acceptedRecords\":2,\"recordsWritten\":2") !=
				std::string::npos);
			Assert::IsTrue(summary.find(
				"\"lifetime\":{\"droppedRecords\":") !=
				std::string::npos);
			Assert::IsTrue(summary.find(
				"\"limits\":{\"preRollMs\":1000,\"captureMs\":1000,"
				"\"recordHistoryCapacity\":32,\"maxCaptureRecords\":32") !=
				std::string::npos);
			Assert::IsTrue(summary.find(
				"\"lifetimeCounterMeaning\":") != std::string::npos);
		}

		TEST_METHOD(CaptureExpiresWithoutAnySubmittedFrames)
		{
			TraceFixture fixture;
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(false)));

			// No Submit or SubmitFrame calls: the writer must observe the deadline
			// itself and finish the empty metadata capture.
			Assert::IsTrue(WaitForWriter(*fixture.trace, 2000));
			const AnalysisDiagnosticTraceStatus status = fixture.trace->Status();
			Assert::IsFalse(status.recording);
			Assert::IsFalse(status.captureWriting);
			Assert::AreEqual<uint64_t>(0, status.recordsWritten);
			Assert::AreEqual<uint64_t>(0, status.framesWritten);

			const std::string records =
				CapturePath(*fixture.trace) + "\\analysis.jsonl";
			std::ifstream input(records.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(input.is_open());
			const std::string contents(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(contents.empty());
		}

		TEST_METHOD(CaptureRecordLimitIsHardClampedInSummary)
		{
			AnalysisDiagnosticTraceOptions options;
			options.captureMs = 40;
			options.maxCaptureRecords =
				AnalysisDiagnosticTrace::MAX_CAPTURE_RECORDS + 1;
			options.outputRoot = TempDirectory() +
				"\\vp-analysis-trace-record-cap-" +
				std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(GetTickCount64());
			std::unique_ptr<AnalysisDiagnosticTrace> trace(
				new AnalysisDiagnosticTrace(options));
			trace->SetProducerAvailable(true, false);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(trace->StartCapture(false)));
			trace->StopCapture();
			Assert::IsTrue(WaitForWriter(*trace));

			const std::string summaryPath =
				std::string(trace->Status().outputPath) + "\\capture-summary.json";
			std::ifstream input(summaryPath.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(input.is_open());
			const std::string summary(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(summary.find("\"maxCaptureRecords\":65536") !=
				std::string::npos);
			trace.reset();
			RemoveTree(options.outputRoot);
		}

		TEST_METHOD(SubmitFillsMissingRecordTimestamp)
		{
			TraceFixture fixture;
			AnalysisDiagnosticRecordV1 record = Event("timestamp-defaulted");
			record.timestampNs = 0;
			fixture.trace->Submit(record);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(false)));
			fixture.trace->StopCapture();

			Assert::IsTrue(WaitForWriter(*fixture.trace));
			const std::string records =
				CapturePath(*fixture.trace) + "\\analysis.jsonl";
			std::ifstream input(records.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(input.is_open());
			const std::string contents(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			const std::string timestampKey = "\"timestampNs\":";
			const size_t timestampPosition = contents.find(timestampKey);
			Assert::IsTrue(timestampPosition != std::string::npos);
			const size_t valuePosition = timestampPosition + timestampKey.size();
			char* end = nullptr;
			const unsigned long long timestamp = std::strtoull(
				contents.c_str() + valuePosition, &end, 10);
			Assert::IsTrue(end != contents.c_str() + valuePosition);
			Assert::IsTrue(timestamp != 0);
		}

		TEST_METHOD(MetadataSubmitThroughputIsLoggedWithoutTimingThreshold)
		{
			TraceFixture fixture;
			const AnalysisDiagnosticRecordV1 record = Event("metadata-benchmark");
			const uint32_t submissionCount = 20000;
			const auto start = std::chrono::steady_clock::now();
			for (uint32_t index = 0; index < submissionCount; ++index)
				fixture.trace->Submit(record);
			const auto finish = std::chrono::steady_clock::now();
			const double elapsedUs = std::chrono::duration<double,
				std::micro>(finish - start).count();
			const double microsecondsPerRecord = elapsedUs / submissionCount;
			const AnalysisDiagnosticTraceStatus status = fixture.trace->Status();

			std::ostringstream message;
			message << "AnalysisDiagnosticTrace metadata Submit: "
				<< submissionCount << " calls, " << elapsedUs << " us total, "
				<< microsecondsPerRecord << " us/record, "
				<< status.droppedRecords << " dropped (lock contention is allowed).";
			Logger::WriteMessage(message.str().c_str());
			Assert::IsTrue(elapsedUs > 0.0);
		}

		TEST_METHOD(CaptureReportsProducerAndFrameCapabilityOutcomes)
		{
			AnalysisDiagnosticTraceOptions options;
			options.captureMs = 40;
			options.outputRoot = TempDirectory() +
				"\\vp-analysis-trace-capability-" +
				std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(GetTickCount64());
			std::unique_ptr<AnalysisDiagnosticTrace> trace(
				new AnalysisDiagnosticTrace(options));
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::NoProducer),
				static_cast<uint32_t>(trace->StartCapture(false)));

			trace->SetProducerAvailable(true, false);
			Assert::AreEqual(
				static_cast<uint32_t>(
					AnalysisDiagnosticCaptureResult::FrameCaptureUnavailable),
				static_cast<uint32_t>(trace->StartCapture(true)));
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(trace->StartCapture(false)));
			Assert::AreEqual(
				static_cast<uint32_t>(
					AnalysisDiagnosticCaptureResult::AlreadyRecording),
				static_cast<uint32_t>(trace->StartCapture(false)));
			trace->StopCapture();
			Assert::IsTrue(WaitForWriter(*trace));
			trace.reset();
			RemoveTree(options.outputRoot);
		}

		TEST_METHOD(FrameCaptureRetainsOwnerAndWritesIdentitySidecar)
		{
			TraceFixture fixture(true);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(true)));

			OwnerCounters counters;
			OwnerSource owner;
			owner.bytes = std::make_shared<const std::vector<uint8_t>>(
				std::vector<uint8_t>{ 1, 2, 3, 4 });
			owner.counters = &counters;
			AnalysisDiagnosticFramePacketV1 frame;
			frame.width = 2;
			frame.height = 2;
			frame.stride0 = 2;
			frame.stride1 = 2;
			frame.format = 4;
			frame.encoding = 6;
			frame.colorspace = 8;
			frame.top = 3;
			frame.bottom = 18;
			frame.scriptHint = 1;
			frame.transportGeneration = 101;
			frame.acceptedSequence = 202;
			frame.sourceFrameNumber = 303;
			frame.captureTimestamp = 404;
			frame.sourceFormatGeneration = 505;
			frame.viewportGeneration = 606;
			frame.rendererGeneration = 707;
			frame.analysisDomainKey = 808;
			frame.policyGeneration = 909;
			frame.continuityGeneration = 1001;
			frame.nearBarDistance = 11;
			frame.optimizationMode = 12;
			frame.frameDuration100ns = 417083;
			frame.discontinuity = 1;
			frame.cadenceRepeat = 0;
			frame.rgbBitAlignment = 2;
			frame.data = owner.bytes->data();
			frame.dataBytes = owner.bytes->size();
			frame.ownerContext = &owner;
			frame.retainOwner = RetainOwner;
			frame.releaseOwner = ReleaseOwner;
			fixture.trace->SubmitFrame(frame);
			owner.bytes.reset(); // The queued token must keep the pixels alive.
			fixture.trace->StopCapture();

			Assert::IsTrue(WaitForWriter(*fixture.trace));
			const AnalysisDiagnosticTraceStatus status = fixture.trace->Status();
			Assert::AreEqual<uint64_t>(1, status.framesWritten);
			Assert::AreEqual<uint64_t>(4, status.frameBytesWritten);
			Assert::AreEqual(1, counters.retained.load());
			Assert::AreEqual(1, counters.released.load());

			const std::string sidecar =
				CapturePath(*fixture.trace) + "\\frame-000001.txt";
			std::ifstream input(sidecar.c_str(), std::ios::in | std::ios::binary);
			Assert::IsTrue(input.is_open());
			const std::string contents(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			Assert::IsTrue(contents.find("2 2 2 2 4 6 8 3 18 1") == 0);
			Assert::IsTrue(contents.find(
				"identity_v1 101 202 303 404 505 606 707 808 909 1001 11 12 "
				"417083 1 0 2") != std::string::npos);
		}

		TEST_METHOD(FrameSizeCapRejectsBeforeRetainingOwner)
		{
			TraceFixture fixture(true);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(true)));
			OwnerCounters counters;
			OwnerSource owner;
			owner.bytes = std::make_shared<const std::vector<uint8_t>>(
				std::vector<uint8_t>{ 7 });
			owner.counters = &counters;
			const uint8_t pixel = 7;
			AnalysisDiagnosticFramePacketV1 frame;
			frame.data = &pixel;
			frame.dataBytes = 9; // Fixture cap is eight bytes.
			frame.ownerContext = &owner;
			frame.retainOwner = RetainOwner;
			frame.releaseOwner = ReleaseOwner;
			fixture.trace->SubmitFrame(frame);
			fixture.trace->StopCapture();

			Assert::IsTrue(WaitForWriter(*fixture.trace));
			Assert::AreEqual<uint64_t>(1, fixture.trace->Status().droppedFrames);
			Assert::AreEqual(0, counters.retained.load());
			Assert::AreEqual(0, counters.released.load());
		}

		TEST_METHOD(RecordCapacityAndFileFailuresAreReported)
		{
			TraceFixture fixture;
			fixture.options.maxCaptureRecords = 1;
			fixture.trace.reset(new AnalysisDiagnosticTrace(fixture.options));
			fixture.trace->SetProducerAvailable(true, false);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(fixture.trace->StartCapture(false)));
			fixture.trace->Submit(Event("first"));
			fixture.trace->Submit(Event("capacity-drop"));
			Assert::IsTrue(WaitForWriter(*fixture.trace));
			Assert::IsTrue(fixture.trace->Status().droppedRecords >= 1);

			const std::string failureRoot = fixture.root + "-file";
			{
				std::ofstream blocker(failureRoot, std::ios::out | std::ios::binary);
				blocker << "not a directory";
			}
			AnalysisDiagnosticTraceOptions failureOptions;
			failureOptions.captureMs = 40;
			failureOptions.outputRoot = failureRoot;
			std::unique_ptr<AnalysisDiagnosticTrace> failing(
				new AnalysisDiagnosticTrace(failureOptions));
			failing->SetProducerAvailable(true, false);
			Assert::AreEqual(
				static_cast<uint32_t>(AnalysisDiagnosticCaptureResult::Started),
				static_cast<uint32_t>(failing->StartCapture(false)));
			const uint64_t deadline = GetTickCount64() + 2000;
			while (GetTickCount64() < deadline &&
				failing->Status().writeFailures == 0)
				Sleep(5);
			Assert::IsTrue(failing->Status().writeFailures > 0);
			failing.reset();
			DeleteFileA(failureRoot.c_str());
		}
	};
}
