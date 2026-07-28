#include "pch.h"

#include <DebugLogRetention.h>
#include "CppUnitTest.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
class TemporaryLogDirectory
{
public:
	TemporaryLogDirectory()
	{
		char temporaryRoot[MAX_PATH] = {};
		GetTempPathA(ARRAYSIZE(temporaryRoot), temporaryRoot);
		m_path = std::string(temporaryRoot) +
			"VideoProcessor-log-retention-" +
			std::to_string(GetCurrentProcessId()) + "-" +
			std::to_string(GetTickCount64());
		CreateDirectoryA(m_path.c_str(), nullptr);
	}

	~TemporaryLogDirectory()
	{
		WIN32_FIND_DATAA data = {};
		HANDLE find = FindFirstFileA((m_path + "\\*").c_str(), &data);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
			{
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
					DeleteFileA((m_path + "\\" + data.cFileName).c_str());
			}
			while (FindNextFileA(find, &data));
			FindClose(find);
		}
		RemoveDirectoryA(m_path.c_str());
	}

	std::string Path(const std::string& filename) const
	{
		return m_path + "\\" + filename;
	}

private:
	std::string m_path;
};

void WriteFile(const std::string& path, const std::string& contents = "log")
{
	std::ofstream file(path, std::ios::out | std::ios::trunc);
	file << contents;
}

size_t CountMatchingLogs(const TemporaryLogDirectory& directory)
{
	size_t count = 0;
	WIN32_FIND_DATAA data = {};
	HANDLE find = FindFirstFileA(directory.Path("*").c_str(), &data);
	if (find == INVALID_HANDLE_VALUE)
		return count;
	do
	{
		const std::string name = data.cFileName;
		if (name == "vp_debug.log" ||
			DebugLogRetention::IsArchiveFileName("vp_debug.log", name))
			++count;
	}
	while (FindNextFileA(find, &data));
	FindClose(find);
	return count;
}

std::string ArchiveName(size_t index)
{
	std::ostringstream name;
	name << "vp_debug.20260727-12" <<
		std::setw(2) << std::setfill('0') << (index / 60) <<
		std::setw(2) << std::setfill('0') << (index % 60) << ".log";
	return name.str();
}
}

namespace VideoProcessorTest
{
	TEST_CLASS(DebugLogRetentionTests)
	{
	public:
		TEST_METHOD(OmittedSettingUsesTen)
		{
			const auto setting =
				DebugLogRetention::ResolveSetting(nullptr, false, false);
			Assert::AreEqual(
				DebugLogRetention::DEFAULT_COUNT, setting.count);
			Assert::IsTrue(
				setting.diagnostic.find("setting omitted") !=
				std::string::npos);
		}

		TEST_METHOD(ValidMinimumNormalAndMaximumValuesAreAccepted)
		{
			for (const auto& sample :
				std::vector<std::pair<std::string, size_t>>{
					{ "1", 1 }, { "25", 25 }, { "100", 100 } })
			{
				const auto setting = DebugLogRetention::ResolveSetting(
					&sample.first, false, false);
				Assert::AreEqual(sample.second, setting.count);
			}
		}

		TEST_METHOD(InvalidZeroNegativeOverflowAndTextUseTen)
		{
			for (const std::string& raw :
				{ "0", "-1", "101", "999999999999999999999", "ten", "" })
			{
				const auto setting =
					DebugLogRetention::ResolveSetting(&raw, false, false);
				Assert::AreEqual(
					DebugLogRetention::DEFAULT_COUNT, setting.count);
				Assert::IsTrue(
					setting.diagnostic.find("invalid value") !=
					std::string::npos);
			}
		}

		TEST_METHOD(DuplicateAndUnreadableConfigurationUseTen)
		{
			const std::string raw = "25";
			Assert::AreEqual(
				DebugLogRetention::DEFAULT_COUNT,
				DebugLogRetention::ResolveSetting(&raw, true, false).count);
			Assert::AreEqual(
				DebugLogRetention::DEFAULT_COUNT,
				DebugLogRetention::ResolveSetting(nullptr, false, true).count);
		}

		TEST_METHOD(ArchiveMatcherRejectsUnrelatedAndResemblingFiles)
		{
			Assert::IsTrue(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727-153045.log"));
			Assert::IsTrue(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727-153045.2.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "other.20260727-153045.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug-old.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727-153045.txt"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727_153045.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20261327-153045.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727-253045.log"));
			Assert::IsFalse(DebugLogRetention::IsArchiveFileName(
				"vp_debug.log", "vp_debug.20260727-153045.0.log"));
		}

		TEST_METHOD(FirstRunCreatesStableActiveLog)
		{
			TemporaryLogDirectory directory;
			const std::string active = directory.Path("vp_debug.log");
			const auto result = DebugLogRetention::Rotate(active, 10);
			Assert::IsTrue(result.activeReady);
			Assert::AreEqual(static_cast<size_t>(1), result.retainedCount);
			Assert::AreNotEqual(
				INVALID_FILE_ATTRIBUTES, GetFileAttributesA(active.c_str()));
		}

		TEST_METHOD(MissingLogsDirectoryIsNotCreated)
		{
			TemporaryLogDirectory directory;
			const std::string missingDirectory =
				directory.Path("logs");
			const auto result = DebugLogRetention::Rotate(
				missingDirectory + "\\vp_debug.log", 10);
			Assert::IsFalse(result.activeReady);
			Assert::AreEqual(
				INVALID_FILE_ATTRIBUTES,
				GetFileAttributesA(missingDirectory.c_str()));
		}

		TEST_METHOD(NonEmptyActiveLogIsArchivedWithCollisionSafeName)
		{
			TemporaryLogDirectory directory;
			const std::string active = directory.Path("vp_debug.log");
			WriteFile(active, "prior session");
			const std::time_t timestamp = 1785171045;

			const auto first =
				DebugLogRetention::Rotate(active, 10, timestamp);
			Assert::IsTrue(first.activeReady);
			WriteFile(active, "second session");
			const auto second =
				DebugLogRetention::Rotate(active, 10, timestamp);
			Assert::IsTrue(second.activeReady);
			Assert::AreEqual(static_cast<size_t>(3), second.retainedCount);
		}

		TEST_METHOD(ArchiveRenameFailureContinuesInExistingActiveLog)
		{
			TemporaryLogDirectory directory;
			const std::string active = directory.Path("vp_debug.log");
			WriteFile(active, "prior session");
			HANDLE lock = CreateFileA(
				active.c_str(),
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			Assert::AreNotEqual(INVALID_HANDLE_VALUE, lock);

			const auto result = DebugLogRetention::Rotate(active, 10);
			CloseHandle(lock);
			Assert::IsTrue(result.activeReady);
			Assert::AreEqual(static_cast<size_t>(1), result.retainedCount);
			Assert::IsTrue(std::any_of(
				result.diagnostics.begin(), result.diagnostics.end(),
				[](const std::string& diagnostic)
				{
					return diagnostic.find("could not archive") !=
						std::string::npos;
				}));
		}

		TEST_METHOD(PruneFailureIsNonFatalAndDiagnosed)
		{
			TemporaryLogDirectory directory;
			const std::string active = directory.Path("vp_debug.log");
			const std::string archive = directory.Path(ArchiveName(0));
			WriteFile(active, "");
			WriteFile(archive);
			Assert::IsTrue(SetFileAttributesA(
				archive.c_str(), FILE_ATTRIBUTE_READONLY));

			const auto result = DebugLogRetention::Rotate(active, 1);
			Assert::IsTrue(SetFileAttributesA(
				archive.c_str(), FILE_ATTRIBUTE_NORMAL));
			Assert::IsTrue(result.activeReady);
			Assert::AreEqual(static_cast<size_t>(2), result.retainedCount);
			Assert::IsTrue(std::any_of(
				result.diagnostics.begin(), result.diagnostics.end(),
				[](const std::string& diagnostic)
				{
					return diagnostic.find("could not prune") !=
						std::string::npos;
				}));
		}

		TEST_METHOD(EverySupportedCountPrunesOnlyMatchingLogs)
		{
			for (size_t retention = DebugLogRetention::MIN_COUNT;
				retention <= DebugLogRetention::MAX_COUNT;
				++retention)
			{
				TemporaryLogDirectory directory;
				const std::string active = directory.Path("vp_debug.log");
				WriteFile(active, "");
				WriteFile(directory.Path("unrelated.log"), "keep");
				WriteFile(
					directory.Path("vp_debug.20260727-120000.txt"), "keep");
				for (size_t index = 0; index <= retention; ++index)
					WriteFile(directory.Path(ArchiveName(index)));

				const auto result =
					DebugLogRetention::Rotate(active, retention);
				Assert::IsTrue(result.activeReady);
				Assert::AreEqual(retention, CountMatchingLogs(directory));
				Assert::AreEqual(
					INVALID_FILE_ATTRIBUTES,
					GetFileAttributesA(
						directory.Path(ArchiveName(0)).c_str()));
				if (retention > 1)
					Assert::AreNotEqual(
						INVALID_FILE_ATTRIBUTES,
						GetFileAttributesA(
							directory.Path(
								ArchiveName(retention)).c_str()));
				Assert::AreNotEqual(
					INVALID_FILE_ATTRIBUTES,
					GetFileAttributesA(
						directory.Path("unrelated.log").c_str()));
				Assert::AreNotEqual(
					INVALID_FILE_ATTRIBUTES,
					GetFileAttributesA(
						directory.Path(
							"vp_debug.20260727-120000.txt").c_str()));
			}
		}
	};
}
