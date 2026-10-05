#include <pch.h>

#include "MagewellSdkApi.h"

#define MAGEWELL_WIDEN_IMPL(value) L##value
#define MAGEWELL_WIDEN(value) MAGEWELL_WIDEN_IMPL(value)

bool MagewellSdkApi::IsComplete(std::wstring* missingExport) const
{
#define MAGEWELL_CHECK_SDK_FUNCTION(name) \
	if (name == nullptr) \
	{ \
		if (missingExport != nullptr) \
			*missingExport = L"Missing required Magewell SDK export: " MAGEWELL_WIDEN(#name); \
		return false; \
	}
	MAGEWELL_SDK_API_FUNCTIONS(MAGEWELL_CHECK_SDK_FUNCTION)
#undef MAGEWELL_CHECK_SDK_FUNCTION
	if (missingExport != nullptr)
		missingExport->clear();
	return true;
}

bool MagewellSdkApi::BindExports(
	HMODULE module,
	MagewellSdkApi& api,
	std::wstring& missingExport,
	MagewellSdkExportResolver resolver,
	void* resolverContext)
{
	if (module == nullptr)
	{
		missingExport = L"Magewell SDK module handle is null";
		return false;
	}

#define MAGEWELL_BIND_SDK_FUNCTION(name) \
	api.name = reinterpret_cast<decltype(api.name)>( \
		resolver != nullptr \
			? resolver(resolverContext, module, #name) \
			: ::GetProcAddress(module, #name)); \
	if (api.name == nullptr) \
	{ \
		missingExport = L"Missing required Magewell SDK export: " MAGEWELL_WIDEN(#name); \
		return false; \
	}
	MAGEWELL_SDK_API_FUNCTIONS(MAGEWELL_BIND_SDK_FUNCTION)
#undef MAGEWELL_BIND_SDK_FUNCTION

	missingExport.clear();
	return api.IsComplete(&missingExport);
}

#undef MAGEWELL_WIDEN
#undef MAGEWELL_WIDEN_IMPL
