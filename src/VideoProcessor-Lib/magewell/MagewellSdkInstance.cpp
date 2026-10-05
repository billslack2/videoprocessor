/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "MagewellSdkInstance.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>


namespace
{
	struct MagewellSdkManager
	{
		std::mutex mutex;
		std::condition_variable condition;
		std::weak_ptr<MagewellSdkInstance> instance;
		bool lifetimeLive = false;
		std::wstring unavailableReason;
	};

	MagewellSdkManager& GetMagewellSdkManager()
	{
		// The manager contains no vendor module or SDK instance. Keeping this
		// small synchronization state alive until process exit avoids static
		// destruction-order hazards when capture objects release their final
		// token during application shutdown.
		static MagewellSdkManager* manager = new MagewellSdkManager();
		return *manager;
	}

	// Aliasing public shared_ptrs keep this holder alive. It owns the actual
	// SDK token and performs Exit/Free only when the final backend user releases
	// its alias, outside any static library or DLL teardown path.
	struct MagewellSdkLifetime
	{
		std::shared_ptr<MagewellSdkInstance> sdk;
		bool registered = false;

		~MagewellSdkLifetime()
		{
			if (!registered)
				return;

			// Keep the manager marked live until the old SDK has fully exited.
			// A concurrent Acquire sees an expired weak token and waits on the
			// condition variable instead of initializing over the old instance.
			sdk.reset();
			auto& manager = GetMagewellSdkManager();
			{
				std::lock_guard<std::mutex> lock(manager.mutex);
				manager.instance.reset();
				manager.lifetimeLive = false;
			}
			manager.condition.notify_all();
		}
	};

	std::wstring Win32Failure(const wchar_t* operation, DWORD error)
	{
		return std::wstring(operation) + L" (Windows error " +
			std::to_wstring(error) + L")";
	}
}


std::shared_ptr<MagewellSdkInstance> MagewellSdkInstance::Acquire()
{
	RuntimeLoader loader;
	loader.loadModule = &LoadProductionModule;
	loader.freeModule = &FreeProductionModule;
	loader.getProcAddress = &ResolveProductionExport;
	return AcquireCachedWithLoader(loader);
}


std::shared_ptr<MagewellSdkInstance> MagewellSdkInstance::AcquireCachedWithLoader(
	const RuntimeLoader& loader)
{
	auto& manager = GetMagewellSdkManager();
	std::unique_lock<std::mutex> lock(manager.mutex);

	for (;;)
	{
		if (auto existing = manager.instance.lock())
		{
			manager.unavailableReason.clear();
			return existing;
		}

		if (!manager.lifetimeLive)
			break;

		if (loader.onRetiringWait != nullptr)
			loader.onRetiringWait(loader.context);
		manager.condition.wait(lock, [&manager] {
			return !manager.lifetimeLive;
		});
	}

	// Allocate the lifetime owner before loading or initializing the vendor
	// runtime. An allocation failure therefore cannot leave SDK state active.
	auto lifetime = std::make_shared<MagewellSdkLifetime>();
	std::wstring failureReason;
	lifetime->sdk = AcquireWithLoader(loader, failureReason);
	if (!lifetime->sdk)
	{
		manager.unavailableReason = std::move(failureReason);
		return nullptr;
	}

	std::shared_ptr<MagewellSdkInstance> exposed(
		lifetime, lifetime->sdk.get());
	lifetime->registered = true;
	manager.instance = exposed;
	manager.lifetimeLive = true;
	manager.unavailableReason.clear();
	return exposed;
}


std::wstring MagewellSdkInstance::UnavailableReason()
{
	auto& manager = GetMagewellSdkManager();
	std::lock_guard<std::mutex> lock(manager.mutex);
	return manager.unavailableReason;
}


void MagewellSdkInstance::SetDiscoveryUnavailableReason(
	const std::wstring& reason)
{
	auto& manager = GetMagewellSdkManager();
	std::lock_guard<std::mutex> lock(manager.mutex);
	manager.unavailableReason = reason;
}


HMODULE MagewellSdkInstance::LoadRuntimeFromSystem32(
	std::wstring& failureReason)
{
	std::vector<wchar_t> systemDirectory(MAX_PATH + 1, L'\0');
	const UINT directoryLength = ::GetSystemDirectoryW(
		systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
	if (directoryLength == 0 || directoryLength >= systemDirectory.size())
	{
		failureReason = Win32Failure(
			L"Unable to locate the Windows System32 directory", ::GetLastError());
		return nullptr;
	}

	std::wstring runtimePath(systemDirectory.data(), directoryLength);
	runtimePath += L"\\LibMWCapture.dll";

	const DWORD attributes = ::GetFileAttributesW(runtimePath.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES ||
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		const DWORD error = ::GetLastError();
		failureReason = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
			? L"Magewell runtime is not installed in Windows System32"
			: Win32Failure(L"Unable to inspect the Magewell runtime", error);
		return nullptr;
	}

	// Magewell's runtime installer places the process-architecture copy in
	// System32 (or SysWOW64 for a 32-bit process). Use its absolute path and
	// restrict both the primary DLL and its dependencies to that trusted
	// directory. This excludes the current directory, PATH, and app-local DLLs.
	HMODULE module = ::LoadLibraryExW(
		runtimePath.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (module == nullptr)
		failureReason = Win32Failure(
			L"Unable to load Magewell runtime from Windows System32", ::GetLastError());
	return module;
}


HMODULE MagewellSdkInstance::LoadProductionModule(
	void*, std::wstring& failureReason)
{
	return LoadRuntimeFromSystem32(failureReason);
}


void MagewellSdkInstance::FreeProductionModule(void*, HMODULE module)
{
	if (module != nullptr)
		::FreeLibrary(module);
}


FARPROC MagewellSdkInstance::ResolveProductionExport(
	void*, HMODULE module, LPCSTR exportName)
{
	return ::GetProcAddress(module, exportName);
}


std::shared_ptr<MagewellSdkInstance> MagewellSdkInstance::AcquireWithLoader(
	const RuntimeLoader& loader,
	std::wstring& failureReason)
{
	failureReason.clear();
	if (loader.loadModule == nullptr || loader.freeModule == nullptr ||
		loader.getProcAddress == nullptr)
	{
		failureReason = L"Magewell runtime loader is incomplete";
		return nullptr;
	}

	HMODULE module = loader.loadModule(loader.context, failureReason);
	if (module == nullptr)
	{
		if (failureReason.empty())
			failureReason = L"Magewell runtime could not be loaded";
		return nullptr;
	}

	MagewellSdkApi api;
	if (!MagewellSdkApi::BindExports(
		module, api, failureReason, loader.getProcAddress, loader.context))
	{
		loader.freeModule(loader.context, module);
		return nullptr;
	}

	if (!api.MWCaptureInitInstance())
	{
		loader.freeModule(loader.context, module);
		failureReason = L"MWCaptureInitInstance reported initialization failure";
		return nullptr;
	}

	MagewellSdkInstance* raw = nullptr;
	try
	{
		raw = new MagewellSdkInstance(
			api, module, true, loader.freeModule, loader.context);
	}
	catch (...)
	{
		api.MWCaptureExitInstance();
		loader.freeModule(loader.context, module);
		throw;
	}

	// The instance owns both the SDK initialization and the loaded module.
	std::shared_ptr<MagewellSdkInstance> instance(raw);
	return instance;
}


MagewellSdkInstance::~MagewellSdkInstance()
{
	if (m_initialized && m_api.MWCaptureExitInstance != nullptr)
		m_api.MWCaptureExitInstance();
	if (m_module != nullptr && m_freeModule != nullptr)
		m_freeModule(m_loaderContext, m_module);
}


MagewellSdkInstance::MagewellSdkInstance(
	MagewellSdkApi api,
	HMODULE module,
	bool initialized,
	RuntimeLoader::FreeModule freeModule,
	void* loaderContext):
	m_api(std::move(api)),
	m_module(module),
	m_initialized(initialized),
	m_freeModule(freeModule),
	m_loaderContext(loaderContext)
{
}
