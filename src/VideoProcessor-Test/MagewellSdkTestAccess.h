#pragma once

// Test-only access to construct an isolated Magewell SDK token with a fake
// function table. This header is compiled into VideoProcessor-Test only; the
// production loader exposes no runtime path or API-table injection seam.
#include <magewell/MagewellSdkInstance.h>

class MagewellSdkTestAccess
{
public:
	static MagewellSdkInstancePtr Create(
		const MagewellSdkApi& api,
		bool initialized = false)
	{
		return MagewellSdkInstancePtr(
			new MagewellSdkInstance(api, nullptr, initialized, nullptr, nullptr));
	}

	static bool BindExports(
		HMODULE module,
		MagewellSdkApi& api,
		std::wstring& missingExport)
	{
		return MagewellSdkApi::BindExports(module, api, missingExport);
	}

	static MagewellSdkInstancePtr AcquireWithLoader(
		HMODULE (*loadModule)(void*, std::wstring&),
		void (*freeModule)(void*, HMODULE),
		MagewellSdkExportResolver resolveExport,
		void* context,
		std::wstring& failureReason)
	{
		MagewellSdkInstance::RuntimeLoader loader;
		loader.loadModule = loadModule;
		loader.freeModule = freeModule;
		loader.getProcAddress = resolveExport;
		loader.context = context;
		return MagewellSdkInstance::AcquireWithLoader(loader, failureReason);
	}

	static MagewellSdkInstancePtr AcquireCachedWithLoader(
		HMODULE (*loadModule)(void*, std::wstring&),
		void (*freeModule)(void*, HMODULE),
		MagewellSdkExportResolver resolveExport,
		void* context,
		void (*onRetiringWait)(void*) = nullptr)
	{
		MagewellSdkInstance::RuntimeLoader loader;
		loader.loadModule = loadModule;
		loader.freeModule = freeModule;
		loader.getProcAddress = resolveExport;
		loader.onRetiringWait = onRetiringWait;
		loader.context = context;
		return MagewellSdkInstance::AcquireCachedWithLoader(loader);
	}
};
