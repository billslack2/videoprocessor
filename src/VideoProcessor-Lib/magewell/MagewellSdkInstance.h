/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <string>

#include "MagewellSdkApi.h"


/**
 * Ownership token for the Magewell SDK global instance.
 *
 * MWCaptureInitInstance() and MWCaptureExitInstance() bracket all other SDK
 * use. The discoverer and every device it produces hold one of these, so the
 * SDK stays initialised for as long as any of them is alive regardless of the
 * order in which they are released.
 */
class MagewellSdkInstance
{
public:

	// Returns a shared token, initialising the SDK if this is the first
	// holder. Returns nullptr if the SDK could not be initialised, which is
	// the normal result on a machine with no Magewell runtime installed.
	static std::shared_ptr<MagewellSdkInstance> Acquire();

	// Describes why Magewell discovery is unavailable. This may reflect either
	// a runtime loading failure or a successful runtime load with no usable card.
	// An empty string means no unavailable reason is currently recorded.
	static std::wstring UnavailableReason();

	const MagewellSdkApi& Api() const noexcept { return m_api; }

	~MagewellSdkInstance();

	MagewellSdkInstance(const MagewellSdkInstance&) = delete;
	MagewellSdkInstance& operator=(const MagewellSdkInstance&) = delete;

private:
	struct RuntimeLoader
	{
		using LoadModule = HMODULE (*)(void* context, std::wstring& failureReason);
		using FreeModule = void (*)(void* context, HMODULE module);
		using WaitObserver = void (*)(void* context);

		LoadModule loadModule = nullptr;
		FreeModule freeModule = nullptr;
		MagewellSdkExportResolver getProcAddress = nullptr;
		WaitObserver onRetiringWait = nullptr;
		void* context = nullptr;
	};

	MagewellSdkInstance(
		MagewellSdkApi api,
		HMODULE module,
		bool initialized,
		RuntimeLoader::FreeModule freeModule,
		void* loaderContext);

	static HMODULE LoadRuntimeFromSystem32(std::wstring& failureReason);
	static HMODULE LoadProductionModule(
		void* context, std::wstring& failureReason);
	static void FreeProductionModule(void* context, HMODULE module);
	static FARPROC ResolveProductionExport(
		void* context, HMODULE module, LPCSTR exportName);
	static std::shared_ptr<MagewellSdkInstance> AcquireWithLoader(
		const RuntimeLoader& loader,
		std::wstring& failureReason);
	static std::shared_ptr<MagewellSdkInstance> AcquireCachedWithLoader(
		const RuntimeLoader& loader);
	static void SetDiscoveryUnavailableReason(const std::wstring& reason);

	MagewellSdkApi m_api;
	HMODULE m_module = nullptr;
	bool m_initialized = false;
	RuntimeLoader::FreeModule m_freeModule = nullptr;
	void* m_loaderContext = nullptr;

	friend class MagewellCaptureDeviceDiscoverer;
	friend class MagewellSdkTestAccess;
};


typedef std::shared_ptr<MagewellSdkInstance> MagewellSdkInstancePtr;
