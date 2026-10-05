#include "pch.h"
#include "CppUnitTest.h"

#include "MagewellSdkTestAccess.h"

#include <cstring>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	struct FakeRuntime
	{
		MagewellSdkApi api;
		bool loadSucceeds = true;
		std::string missingExport;
		std::atomic<int> loadCount{ 0 };
		std::atomic<int> freeCount{ 0 };
		std::atomic<int> resolveCount{ 0 };
		std::atomic<int> initializeCount{ 0 };
		std::atomic<int> exitCount{ 0 };
		BOOL initializeResult = TRUE;
		std::mutex lifecycleMutex;
		std::condition_variable lifecycleCondition;
		bool blockExit = false;
		bool exitEntered = false;
		bool allowExit = false;
		bool retiringWaitEntered = false;
	};

	FakeRuntime* g_activeRuntime = nullptr;
	std::mutex g_testMutex;

	BOOL FakeInitialize()
	{
		++g_activeRuntime->initializeCount;
		return g_activeRuntime->initializeResult;
	}

	void FakeExit()
	{
		auto& runtime = *g_activeRuntime;
		++runtime.exitCount;
		std::unique_lock<std::mutex> lock(runtime.lifecycleMutex);
		runtime.exitEntered = true;
		runtime.lifecycleCondition.notify_all();
		runtime.lifecycleCondition.wait(lock, [&runtime] {
			return !runtime.blockExit || runtime.allowExit;
		});
	}

	void FakeNoop()
	{
	}

	HMODULE FakeLoad(void* context, std::wstring& failureReason)
	{
		auto& runtime = *static_cast<FakeRuntime*>(context);
		++runtime.loadCount;
		if (!runtime.loadSucceeds)
		{
			failureReason = L"synthetic missing Magewell runtime";
			return nullptr;
		}
		return reinterpret_cast<HMODULE>(&runtime);
	}

	void FakeFree(void* context, HMODULE module)
	{
		auto& runtime = *static_cast<FakeRuntime*>(context);
		Assert::IsTrue(module == reinterpret_cast<HMODULE>(&runtime));
		++runtime.freeCount;
	}

	FARPROC FakeResolve(void* context, HMODULE module, LPCSTR exportName)
	{
		auto& runtime = *static_cast<FakeRuntime*>(context);
		Assert::IsTrue(module == reinterpret_cast<HMODULE>(&runtime));
		++runtime.resolveCount;
		if (runtime.missingExport == exportName)
			return nullptr;

#define MAGEWELL_RETURN_FAKE_EXPORT(name) \
		if (std::strcmp(exportName, #name) == 0) \
			return reinterpret_cast<FARPROC>(runtime.api.name);
		MAGEWELL_SDK_API_FUNCTIONS(MAGEWELL_RETURN_FAKE_EXPORT)
#undef MAGEWELL_RETURN_FAKE_EXPORT
		return nullptr;
	}

	void ObserveRetiringWait(void* context)
	{
		auto& runtime = *static_cast<FakeRuntime*>(context);
		std::lock_guard<std::mutex> lock(runtime.lifecycleMutex);
		runtime.retiringWaitEntered = true;
		runtime.lifecycleCondition.notify_all();
	}

	void PrepareFakeRuntime(FakeRuntime& runtime)
	{
		g_activeRuntime = &runtime;
#define MAGEWELL_SET_FAKE_EXPORT(name) \
		runtime.api.name = reinterpret_cast<decltype(runtime.api.name)>(&FakeNoop);
		MAGEWELL_SDK_API_FUNCTIONS(MAGEWELL_SET_FAKE_EXPORT)
#undef MAGEWELL_SET_FAKE_EXPORT
		runtime.api.MWCaptureInitInstance = &FakeInitialize;
		runtime.api.MWCaptureExitInstance = &FakeExit;
	}

	MagewellSdkInstancePtr Acquire(FakeRuntime& runtime, std::wstring& reason)
	{
		return MagewellSdkTestAccess::AcquireWithLoader(
			&FakeLoad, &FakeFree, &FakeResolve, &runtime, reason);
	}

	MagewellSdkInstancePtr AcquireCached(FakeRuntime& runtime)
	{
		return MagewellSdkTestAccess::AcquireCachedWithLoader(
			&FakeLoad, &FakeFree, &FakeResolve, &runtime, &ObserveRetiringWait);
	}
}

namespace Tests
{
	TEST_CLASS(MagewellSdkInstanceTests)
	{
	public:
		TEST_METHOD(LoadFailureReturnsUnavailableAndDoesNotResolveOrFree)
		{
			const std::lock_guard<std::mutex> testLock(g_testMutex);
			FakeRuntime runtime;
			PrepareFakeRuntime(runtime);
			runtime.loadSucceeds = false;
			std::wstring reason;

			const auto instance = Acquire(runtime, reason);

			Assert::IsNull(instance.get());
			Assert::AreEqual(1, static_cast<int>(runtime.loadCount));
			Assert::AreEqual(0, static_cast<int>(runtime.resolveCount));
			Assert::AreEqual(0, static_cast<int>(runtime.initializeCount));
			Assert::AreEqual(0, static_cast<int>(runtime.freeCount));
			Assert::AreEqual(std::wstring(L"synthetic missing Magewell runtime"), reason);
			g_activeRuntime = nullptr;
		}

		TEST_METHOD(MissingScanExportUnloadsRuntimeBeforeInitialization)
		{
			const std::lock_guard<std::mutex> testLock(g_testMutex);
			FakeRuntime runtime;
			PrepareFakeRuntime(runtime);
			runtime.missingExport = "MWSetInputSourceScan";
			std::wstring reason;

			const auto instance = Acquire(runtime, reason);

			Assert::IsNull(instance.get());
			Assert::AreEqual(1, static_cast<int>(runtime.loadCount));
			Assert::AreEqual(0, static_cast<int>(runtime.initializeCount));
			Assert::AreEqual(1, static_cast<int>(runtime.freeCount));
			Assert::IsTrue(reason.find(L"MWSetInputSourceScan") != std::wstring::npos);
			g_activeRuntime = nullptr;
		}

		TEST_METHOD(InitializationFailureUnloadsWithoutCallingExit)
		{
			const std::lock_guard<std::mutex> testLock(g_testMutex);
			FakeRuntime runtime;
			PrepareFakeRuntime(runtime);
			runtime.initializeResult = FALSE;
			std::wstring reason;

			const auto instance = Acquire(runtime, reason);

			Assert::IsNull(instance.get());
			Assert::AreEqual(1, static_cast<int>(runtime.initializeCount));
			Assert::AreEqual(0, static_cast<int>(runtime.exitCount));
			Assert::AreEqual(1, static_cast<int>(runtime.freeCount));
			Assert::IsTrue(reason.find(L"initialization failure") != std::wstring::npos);
			g_activeRuntime = nullptr;
		}

		TEST_METHOD(SuccessKeepsModuleLoadedUntilExitInstanceCompletes)
		{
			const std::lock_guard<std::mutex> testLock(g_testMutex);
			FakeRuntime runtime;
			PrepareFakeRuntime(runtime);
			std::wstring reason;

			auto instance = Acquire(runtime, reason);

			Assert::IsNotNull(instance.get());
			Assert::IsTrue(reason.empty());
			Assert::AreEqual(1, static_cast<int>(runtime.initializeCount));
			Assert::AreEqual(0, static_cast<int>(runtime.exitCount));
			Assert::AreEqual(0, static_cast<int>(runtime.freeCount));
			Assert::IsTrue(instance->Api().IsComplete());

			instance.reset();

			Assert::AreEqual(1, static_cast<int>(runtime.exitCount));
			Assert::AreEqual(1, static_cast<int>(runtime.freeCount));
			g_activeRuntime = nullptr;
		}

		TEST_METHOD(ReacquireWaitsForPreviousExitAndModuleRelease)
		{
			const std::lock_guard<std::mutex> testLock(g_testMutex);
			FakeRuntime runtime;
			PrepareFakeRuntime(runtime);
			runtime.blockExit = true;
			auto first = AcquireCached(runtime);
			Assert::IsNotNull(first.get());

			std::thread releaseThread([instance = std::move(first)]() mutable {
				instance.reset();
			});
			bool exitReached = false;
			{
				std::unique_lock<std::mutex> lock(runtime.lifecycleMutex);
				exitReached = runtime.lifecycleCondition.wait_for(
					lock, std::chrono::seconds(3), [&runtime] {
						return runtime.exitEntered;
					});
			}

			MagewellSdkInstancePtr second;
			std::atomic<bool> reacquireReturned{ false };
			std::thread acquireThread([&] {
				second = AcquireCached(runtime);
				reacquireReturned.store(true, std::memory_order_release);
			});
			bool reacquireWaitObserved = false;
			{
				std::unique_lock<std::mutex> lock(runtime.lifecycleMutex);
				reacquireWaitObserved = runtime.lifecycleCondition.wait_for(
					lock, std::chrono::seconds(3), [&runtime] {
						return runtime.retiringWaitEntered;
					});
			}
			const bool stayedUnloadedDuringExit =
				runtime.loadCount.load() == 1 &&
				!reacquireReturned.load(std::memory_order_acquire);

			// Always release the gate before joining, even if an earlier timed
			// observation failed, so a regression cannot strand the test host.
			{
				std::lock_guard<std::mutex> lock(runtime.lifecycleMutex);
				runtime.allowExit = true;
			}
			runtime.lifecycleCondition.notify_all();
			releaseThread.join();
			acquireThread.join();

			Assert::IsTrue(exitReached);
			Assert::IsTrue(reacquireWaitObserved);
			Assert::IsTrue(stayedUnloadedDuringExit);
			Assert::IsNotNull(second.get());
			Assert::AreEqual(2, static_cast<int>(runtime.loadCount));
			Assert::AreEqual(1, static_cast<int>(runtime.freeCount));

			second.reset();
			Assert::AreEqual(2, static_cast<int>(runtime.exitCount));
			Assert::AreEqual(2, static_cast<int>(runtime.freeCount));
			g_activeRuntime = nullptr;
		}
	};
}
