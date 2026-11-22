#include "pch.h"
#include "CppUnitTest.h"

#include <AnalysisDiagnosticTrace.h>
#include <vprenderer/MatteDecisionJournal.h>
#include <vprenderer/MatteIncidentCapture.h>

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
        TEST_METHOD(ManualDolbySampleKeepsFirstCompleteBody)
		{
			TraceFixture fixture(false, 1000);
			const uint8_t bytes[] = { 0, 5, 255 };
			Assert::IsTrue(fixture.trace->StartCapture(false, true) == AnalysisDiagnosticCaptureResult::Started);
			for (int i = 0; i < 10; ++i)
				fixture.trace->TrySampleDolbyBody(42, bytes, sizeof(bytes));
			fixture.trace->TrySampleDolbyBody(43, bytes, 1);
			fixture.trace->StopCapture();
			Assert::IsTrue(WaitForWriter(*fixture.trace));
			std::ifstream input(CapturePath(*fixture.trace) + "\\capture-summary.json");
			std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			Assert::IsTrue(json.find("\"sourceFrameCounter\":42,\"declaredBytes\":3,\"bodyHex\":\"0005ff\"") != std::string::npos);
		}

		TEST_METHOD(AutomaticRasterCaptureIsOptInAndMetadataOnly)
        {
            TraceFixture fixture(false, 200);
            auto incident=Event("trusted-picture-full-raster-submitted");
            strncpy_s(incident.stage, "auto-raster-incident", _TRUNCATE);
            fixture.trace->Submit(incident);
            Sleep(30);
            Assert::IsFalse(fixture.trace->Status().recording);
            Assert::IsTrue(CapturePath(*fixture.trace).empty());
            fixture.trace->SetAutoRasterCapture(true);
            fixture.trace->Submit(Event("before-auto-incident"));
            fixture.trace->Submit(incident);
            const auto deadline=GetTickCount64()+2000;
            while (CapturePath(*fixture.trace).empty() && GetTickCount64()<deadline) Sleep(5);
            Assert::IsFalse(CapturePath(*fixture.trace).empty());
            fixture.trace->Submit(Event("after-auto-incident"));
            Assert::IsTrue(WaitForWriter(*fixture.trace));
            const auto path=CapturePath(*fixture.trace);
            std::ifstream input(path+"\\analysis.jsonl",std::ios::binary);
            const std::string records((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            Assert::IsTrue(records.find("auto-raster-incident")!=std::string::npos);
            Assert::IsTrue(records.find("before-auto-incident")!=std::string::npos);
            std::ifstream summaryFile(path+"\\capture-summary.json",std::ios::binary);
            const std::string summary((std::istreambuf_iterator<char>(summaryFile)),std::istreambuf_iterator<char>());
            Assert::IsTrue(summary.find("\"includeFrames\":false")!=std::string::npos);
            fixture.trace->Submit(incident);
            Sleep(30);
            Assert::IsFalse(fixture.trace->Status().recording);
            Assert::IsTrue(CapturePath(*fixture.trace)==path);
        }

        TEST_METHOD(MatteJournalEmitsOnPoseGuardAndDistinctFreshEvidence)
        {
            using namespace MatteDecisionJournal;
            Identity first;
            first.generation = 1; first.sequence = 10; first.domain = 3;
            first.viewport = 4; first.policy = 5; first.continuity = 6;
            Identity next = first;
            next.sequence = 11;
            Assert::IsTrue(IsSelectedSnapshot(true, false, first, first));
            Assert::IsFalse(IsSelectedSnapshot(true, true, first, first));
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            Assert::IsFalse(IsSelectedSnapshot(false, false, first, first));
            next = first; ++next.sourceFormat;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.sourceFrame;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.timestamp;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.viewport;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.policy;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.continuity;
            Assert::IsFalse(IsSelectedSnapshot(true, false, first, next));
            next = first; ++next.sequence;
            Assert::IsTrue(ShouldEmit(true, Guard::None, Guard::None, first, first));
            Assert::IsFalse(ShouldEmit(false, Guard::Held, Guard::Held, next, first));
            Assert::IsTrue(ShouldEmit(false, Guard::Held, Guard::Current, next, first));
            Assert::IsTrue(ShouldEmit(false, Guard::Current, Guard::Current, next, first));
            Assert::IsFalse(ShouldEmit(false, Guard::Current, Guard::Current, first, first));
            Assert::IsTrue(ShouldEmit(false, Guard::Unknown, Guard::Current, next, first));
            next = first;
            ++next.domain;
            Assert::IsTrue(ShouldEmit(false, Guard::Current, Guard::Current, next, first));
        }

        TEST_METHOD(MatteIncidentsPreserveSubmittedIdentityAndActualBytes)
        {
            using Result = MatteIncidentCapture::Result;
            const std::string directory = TempDirectory() + "\\vp-matte-test-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const std::wstring wideDirectory(directory.begin(), directory.end());
            const auto first = std::make_shared<std::vector<uint8_t>>(64, 0x31);
            const auto second = std::make_shared<std::vector<uint8_t>>(64, 0x72);
            MatteIncidentCapture::Job job;
            job.frame.width = 8; job.frame.height = 4; job.frame.stride0 = 16; job.frame.stride1 = 16;
            job.frame.transportGeneration = 7; job.frame.acceptedSequence = 11;
            job.frame.data = first->data(); job.frame.dataBytes = first->size(); job.owner = first;
            {
                MatteIncidentCapture disabled;
                Assert::IsFalse(disabled.Enabled());
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(disabled.Observe(job, true, true, false)));
                Assert::IsTrue(GetFileAttributesA(directory.c_str()) == INVALID_FILE_ATTRIBUTES);
            }
            {
                MatteIncidentCapture capture(wideDirectory);
                Assert::IsTrue(capture.Enabled());
                Assert::AreEqual(static_cast<unsigned>(Result::IdentityMismatch),
                    static_cast<unsigned>(capture.Observe(job, false, true, true)));
                const auto dropsAfterFirstMiss = capture.Dropped();
                ++job.frame.acceptedSequence;
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, false, true, true)));
                Assert::AreEqual(dropsAfterFirstMiss, capture.Dropped());
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, false, false)));
                Assert::AreEqual(static_cast<unsigned>(Result::Queued),
                    static_cast<unsigned>(capture.Observe(job, true, true, false)));
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, true, false)));
                Sleep(100);
                job.frame.acceptedSequence = 13;
                job.frame.data = second->data(); job.owner = second;
                job.evidenceGeneration = 7; job.evidenceSequence = 15; job.evidenceDomain = 19;
                job.decision = MatteIncidentCapture::Decision::CurrentMargin;
                job.submitted[1] = 238; job.submitted[2] = 3840; job.submitted[3] = 1920;
                Assert::AreEqual(static_cast<unsigned>(Result::IdentityMismatch),
                    static_cast<unsigned>(capture.Observe(job, false, true, true)));
                ++job.frame.acceptedSequence;
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, true, false)));
                ++job.frame.acceptedSequence;
                Assert::AreEqual(static_cast<unsigned>(Result::Queued),
                    static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::IsTrue(capture.PeakOutstanding() <= 2);
            }
            unsigned frames = 0, sidecars = 0;
            bool checkedFirst = false, checkedSecond = false, checkedDecision = false;
            WIN32_FIND_DATAA entry = {};
            HANDLE search = FindFirstFileA((directory + "\\*").c_str(), &entry);
            Assert::IsTrue(search != INVALID_HANDLE_VALUE);
            do
            {
                const std::string name(entry.cFileName);
                if (name.size() < 4) continue;
                const std::string extension = name.substr(name.size() - 4);
                if (extension == ".bin")
                {
                    std::ifstream input(directory + "\\" + name, std::ios::binary);
                    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
                    if (name.find("-s12-") != std::string::npos) checkedFirst = data == *first;
                    if (name.find("-s15-") != std::string::npos) checkedSecond = data == *second;
                    ++frames;
                }
                if (extension == ".txt")
                {
                    std::ifstream input(directory + "\\" + name);
                    const std::string text((std::istreambuf_iterator<char>(input)), {});
                    if (name.find("-s15-") != std::string::npos)
                        checkedDecision = text.find("evidence_sequence 15") != std::string::npos &&
                            text.find("current detail inside verified raised matte margin") != std::string::npos &&
                            text.find("submitted 0 238 3840 1920") != std::string::npos;
                    ++sidecars;
                }
            } while (FindNextFileA(search, &entry));
            FindClose(search);
            Assert::AreEqual(2u, frames);
            Assert::AreEqual(2u, sidecars);
            Assert::IsTrue(checkedFirst && checkedSecond && checkedDecision);
            RemoveTree(directory);
        }

        TEST_METHOD(MatteIncidentsCaptureDistinctRenewalsAndRespectBudget)
        {
            using Result = MatteIncidentCapture::Result;
            using Decision = MatteIncidentCapture::Decision;
            const std::string directory = TempDirectory() + "\\vp-matte-renewal-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const std::wstring wideDirectory(directory.begin(), directory.end());
            const auto bytes = std::make_shared<std::vector<uint8_t>>(64, 0x55);
            const auto make = [&](uint64_t sequence, int x, Decision decision) {
                MatteIncidentCapture::Job job;
                job.owner = bytes; job.frame.data = bytes->data(); job.frame.dataBytes = bytes->size();
                job.frame.width = 3840; job.frame.height = 2160;
                job.frame.transportGeneration = 2; job.frame.acceptedSequence = sequence;
                job.frame.analysisDomainKey = 41; job.frame.viewportGeneration = 3;
                job.frame.policyGeneration = 4; job.frame.continuityGeneration = 5;
                job.evidenceGeneration = 2; job.evidenceSequence = sequence; job.evidenceDomain = 41;
                job.decision = decision; job.freshWitness = x >= 0; job.bottomX = x; job.bottomY = 1902;
                return job;
            };
            {
                MatteIncidentCapture capture(wideDirectory);
                auto job = make(1600, -1, Decision::None);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, false)));
                Sleep(100);
                job = make(1601, 3675, Decision::AcquiredMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(1721, -1, Decision::None);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, false)));
                job = make(1726, 1804, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(1727, 1808, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                // A new location remains distinct even on the next source frame.
                job = make(1728, 700, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::QueuedRenewal), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(1729, 700, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(1832, 438, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::QueuedRenewal), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(1833, 438, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(1840, 449, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(1850, 1000, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::QueuedRenewal), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(1860, 1300, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::IsTrue(capture.PeakOutstanding() <= 2);
            }
            unsigned sidecars = 0, renewals = 0;
            WIN32_FIND_DATAA entry = {};
            HANDLE search = FindFirstFileA((directory + "\\*.txt").c_str(), &entry);
            Assert::IsTrue(search != INVALID_HANDLE_VALUE);
            do {
                std::ifstream input(directory + "\\" + entry.cFileName);
                const std::string text((std::istreambuf_iterator<char>(input)), {});
                ++sidecars;
                if (text.find("renewal 1") != std::string::npos) {
                    ++renewals;
                    if (text.find("identity_v1 2 1832 ") != std::string::npos)
                        Assert::IsTrue(text.find("witness_bottom 438,1902") != std::string::npos);
                }
            } while (FindNextFileA(search, &entry));
            FindClose(search);
            Assert::AreEqual(6u, sidecars);
            Assert::AreEqual(3u, renewals);
            RemoveTree(directory);
        }

        TEST_METHOD(MatteRenewalBusyAndIdentityMismatchDoNotBorrowFrames)
        {
            using Result = MatteIncidentCapture::Result;
            using Decision = MatteIncidentCapture::Decision;
            struct Gate { HANDLE entered; HANDLE release; };
            const auto beforeWrite = +[](void* context) {
                auto* gate = static_cast<Gate*>(context);
                SetEvent(gate->entered);
                WaitForSingleObject(gate->release, 5000);
            };
            const std::string directory = TempDirectory() + "\\vp-matte-renewal-busy-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const std::wstring wideDirectory(directory.begin(), directory.end());
            const auto bytes = std::make_shared<std::vector<uint8_t>>(64, 0x55);
            const auto make = [&](uint64_t sequence, int x, Decision decision) {
                MatteIncidentCapture::Job job;
                job.owner = bytes; job.frame.data = bytes->data(); job.frame.dataBytes = bytes->size();
                job.frame.width = 3840; job.frame.height = 2160;
                job.frame.transportGeneration = 2; job.frame.acceptedSequence = sequence;
                job.frame.analysisDomainKey = 41; job.evidenceGeneration = 2;
                job.evidenceSequence = sequence; job.evidenceDomain = 41;
                job.decision = decision; job.freshWitness = x >= 0; job.bottomX = x; job.bottomY = 1902;
                return job;
            };
            Gate gate{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            Assert::IsTrue(gate.entered != nullptr); Assert::IsTrue(gate.release != nullptr);
            {
                MatteIncidentCapture capture(wideDirectory, beforeWrite, &gate);
                auto job = make(1, -1, Decision::None);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, false)));
                Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), WaitForSingleObject(gate.entered, 2000));
                job = make(2, 1000, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(4, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Busy), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(uint64_t(1), capture.Dropped());
                job = make(5, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(10, 1800, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::UnknownSelectedState), static_cast<unsigned>(capture.Observe(job, false, true, true)));
                job = make(11, 1800, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::IsTrue(capture.PeakOutstanding() <= 2);
                SetEvent(gate.release);
            }
            CloseHandle(gate.entered); CloseHandle(gate.release);
            RemoveTree(directory);

            const std::string lockDirectory = directory + "-lock";
            const std::wstring wideLockDirectory(lockDirectory.begin(), lockDirectory.end());
            Gate lockGate{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            Assert::IsTrue(lockGate.entered != nullptr); Assert::IsTrue(lockGate.release != nullptr);
            {
                // The injected hook runs only on the worker. The render-side
                // Observe call must not wait or claim a classified loss.
                MatteIncidentCapture capture(wideLockDirectory, nullptr, &lockGate, beforeWrite);
                auto job = make(1, 1000, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), WaitForSingleObject(lockGate.entered, 2000));
                job = make(2, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::RenewalContentionUnknown), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(3, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(uint64_t(0), capture.Dropped());
                SetEvent(lockGate.release);
                Sleep(200);
                job = make(4, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::QueuedRenewal), static_cast<unsigned>(capture.Observe(job, true, true, true)));
            }
            CloseHandle(lockGate.entered); CloseHandle(lockGate.release);
            WIN32_FIND_DATAA found{};
            HANDLE file = FindFirstFileA((lockDirectory + "\\*-s4-*.txt").c_str(), &found);
            Assert::IsTrue(file != INVALID_HANDLE_VALUE);
            std::ifstream in(lockDirectory + "\\" + found.cFileName);
            const std::string sidecar((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            FindClose(file);
            Assert::IsTrue(sidecar.find("prior_unclassified_contention 1") != std::string::npos);
            Assert::IsTrue(sidecar.find("renewal_semantics current_observation") != std::string::npos);
            RemoveTree(lockDirectory);

            const std::string invalidDirectory = directory + "-invalid";
            const std::wstring wideInvalidDirectory(invalidDirectory.begin(), invalidDirectory.end());
            {
                MatteIncidentCapture capture(wideInvalidDirectory);
                auto job = make(1, 1000, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::Queued), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Sleep(100);
                job = make(2, 1500, Decision::CurrentMargin);
                job.owner.reset();
                job.frame.data = nullptr;
                Assert::AreEqual(static_cast<unsigned>(Result::InvalidSource), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                job = make(3, 1500, Decision::CurrentMargin);
                Assert::AreEqual(static_cast<unsigned>(Result::None), static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::AreEqual(uint64_t(1), capture.Dropped());
            }
            RemoveTree(invalidDirectory);
        }

        TEST_METHOD(MatteIncidentsCanArmAfterStartupAndCaptureFirstMargin)
        {
            using Result = MatteIncidentCapture::Result;
            const std::string directory = TempDirectory() + "\\vp-matte-arm-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const std::wstring wideDirectory(directory.begin(), directory.end());
            Assert::IsTrue(CreateDirectoryA(directory.c_str(), nullptr) != 0);
            { std::ofstream disarm(directory + "\\DISARM"); disarm << "paused"; }
            const auto bytes = std::make_shared<std::vector<uint8_t>>(32, 0x42);
            MatteIncidentCapture::Job job;
            job.owner = bytes; job.frame.data = bytes->data(); job.frame.dataBytes = bytes->size();
            job.frame.transportGeneration = 2; job.frame.acceptedSequence = 15;
            {
                MatteIncidentCapture capture(wideDirectory);
                Assert::IsFalse(capture.Enabled());
                Assert::AreEqual(static_cast<unsigned>(Result::None),
                    static_cast<unsigned>(capture.Observe(job, true, false, false)));
                Assert::IsTrue(DeleteFileA((directory + "\\DISARM").c_str()) != 0);
                for (int i = 0; i < 400 && !capture.Enabled(); ++i) Sleep(5);
                Assert::IsTrue(capture.Enabled());
                Assert::AreEqual(static_cast<unsigned>(Result::Queued),
                    static_cast<unsigned>(capture.Observe(job, true, true, true)));
                Assert::IsTrue(capture.PeakOutstanding() <= 2);
            }
            WIN32_FIND_DATAA entry = {};
            HANDLE search = FindFirstFileA((directory + "\\*.bin").c_str(), &entry);
            Assert::IsTrue(search != INVALID_HANDLE_VALUE);
            Assert::IsFalse(FindNextFileA(search, &entry) != 0);
            FindClose(search);
            RemoveTree(directory);
        }

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
