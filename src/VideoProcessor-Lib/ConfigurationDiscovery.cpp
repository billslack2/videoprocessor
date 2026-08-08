#include "pch.h"

#include "ConfigurationDiscovery.h"

#include "RendererId.h"
#include "microsoft_directshow/video_renderers/DirectShowVideoRenderers.h"

#include <DeckLinkAPI_h.h>

#include <algorithm>

namespace
{
	struct MonitorCandidate
	{
		std::wstring source;
		std::wstring friendly;
	};

	BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
	{
		auto* candidates = reinterpret_cast<std::vector<MonitorCandidate>*>(parameter);
		MONITORINFOEXW info = {};
		info.cbSize = sizeof(info);
		if (GetMonitorInfoW(monitor, &info))
			candidates->push_back({ info.szDevice, {} });
		return TRUE;
	}

	void PopulateFriendlyMonitorNames(std::vector<MonitorCandidate>& candidates)
	{
		UINT32 pathCount = 0;
		UINT32 modeCount = 0;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
			&pathCount, &modeCount) != ERROR_SUCCESS)
			return;
		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount,
			paths.data(), &modeCount, modes.data(), nullptr);
		if (result != ERROR_SUCCESS) return;
		paths.resize(pathCount);
		for (const DISPLAYCONFIG_PATH_INFO& path : paths)
		{
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
			source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			source.header.size = sizeof(source);
			source.header.adapterId = path.sourceInfo.adapterId;
			source.header.id = path.sourceInfo.id;
			if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
			DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
			target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
			target.header.size = sizeof(target);
			target.header.adapterId = path.targetInfo.adapterId;
			target.header.id = path.targetInfo.id;
			if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
			for (MonitorCandidate& candidate : candidates)
				if (_wcsicmp(candidate.source.c_str(), source.viewGdiDeviceName) == 0)
				{
					candidate.friendly = target.monitorFriendlyDeviceName;
					break;
				}
		}
	}
}

std::vector<std::wstring> ConfigurationDiscovery::RendererNames(bool hideLegacyRenderers)
{
	std::vector<RendererId> ids;
	try
	{
		DirectShowVideoRendererIds(ids, hideLegacyRenderers);
	}
	catch (...)
	{
		// Discovery is advisory in the configuration editor. A configured
		// renderer must remain editable even when COM discovery is unavailable.
	}
#if defined(_WIN64)
	// Alpha is a first-class x64 VP renderer choice. The editor may run from a
	// development output directory that does not contain the plugin even though
	// the deployed VP installation does, so discovery must not hide the choice
	// merely because this helper process cannot load it from its own directory.
	ids.push_back(RendererId::Libplacebo());
#endif
	std::vector<std::wstring> names;
	for (const RendererId& renderer : RendererId::OrderForDisplay(ids))
		names.emplace_back(renderer.name.GetString());
	return names;
}

std::vector<std::wstring> ConfigurationDiscovery::CaptureDeviceNames()
{
	std::vector<std::wstring> names;
	CComPtr<IDeckLinkIterator> iterator;
	if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
		IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator)) != S_OK)
		return names;
	for (;;)
	{
		CComPtr<IDeckLink> device;
		if (iterator->Next(&device) != S_OK || !device) break;
		BSTR displayName = nullptr;
		if (device->GetDisplayName(&displayName) == S_OK && displayName)
		{
			names.emplace_back(displayName);
			SysFreeString(displayName);
		}
	}
	return names;
}

std::vector<std::wstring> ConfigurationDiscovery::CaptureConnectionNames(
	const std::wstring& captureDeviceName)
{
	std::vector<std::wstring> names;
	CComPtr<IDeckLinkIterator> iterator;
	if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
		IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator)) != S_OK)
		return names;
	for (;;)
	{
		CComPtr<IDeckLink> device;
		if (iterator->Next(&device) != S_OK || !device) break;
		BSTR displayName = nullptr;
		const bool matches = device->GetDisplayName(&displayName) == S_OK &&
			displayName && _wcsicmp(displayName, captureDeviceName.c_str()) == 0;
		if (displayName) SysFreeString(displayName);
		if (!matches) continue;

		CComQIPtr<IDeckLinkProfileAttributes> attributes(device);
		LONGLONG available = 0;
		if (!attributes || attributes->GetInt(
			BMDDeckLinkVideoInputConnections, &available) != S_OK)
			return names;
		const std::pair<BMDVideoConnection, const wchar_t*> known[] = {
			{ bmdVideoConnectionSDI, L"SDI" },
			{ bmdVideoConnectionHDMI, L"HDMI" },
			{ bmdVideoConnectionOpticalSDI, L"Optical SDI" },
			{ bmdVideoConnectionComponent, L"Component" },
			{ bmdVideoConnectionComposite, L"Composite" },
			{ bmdVideoConnectionSVideo, L"S-Video" }
		};
		for (const auto& connection : known)
			if ((static_cast<LONGLONG>(connection.first) & available) != 0)
				names.emplace_back(connection.second);
		return names;
	}
	return names;
}

std::vector<std::wstring> ConfigurationDiscovery::ActiveMonitorNames()
{
	std::vector<MonitorCandidate> candidates;
	EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
		reinterpret_cast<LPARAM>(&candidates));
	PopulateFriendlyMonitorNames(candidates);
	std::vector<std::wstring> names;
	for (const MonitorCandidate& candidate : candidates)
	{
		const std::wstring& value = candidate.friendly.empty() ?
			candidate.source : candidate.friendly;
		if (!value.empty() && std::find(names.begin(), names.end(), value) == names.end())
			names.push_back(value);
	}
	return names;
}
