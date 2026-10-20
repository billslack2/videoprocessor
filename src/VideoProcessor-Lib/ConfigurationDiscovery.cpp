#include "pch.h"

#include "ConfigurationDiscovery.h"
#include "DisplayMonitorInventory.h"

#include "RendererId.h"
#include "microsoft_directshow/video_renderers/DirectShowVideoRenderers.h"

#include <DeckLinkAPI_h.h>

#include "magewell/MagewellBackend.h"

#include <algorithm>

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
	ids.push_back(RendererId::Libplacebo(true));
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
	// A machine may have one vendor's card, the other, or both. A missing
	// vendor runtime is a normal condition here, not an error, so each
	// backend is enumerated independently and contributes what it finds.
	if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
		IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator)) == S_OK)
	{
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
	}
	MagewellBackend::AppendDeviceNames(names);
	return names;
}

std::vector<std::wstring> ConfigurationDiscovery::CaptureConnectionNames(
	const std::wstring& captureDeviceName)
{
	std::vector<std::wstring> names;
	CComPtr<IDeckLinkIterator> iterator;
	if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
		IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator)) != S_OK)
		return MagewellBackend::ConnectionNames(captureDeviceName);
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
	return MagewellBackend::ConnectionNames(captureDeviceName);
}

std::vector<std::wstring> ConfigurationDiscovery::ActiveMonitorNames()
{
    std::vector<std::wstring> names;
    for (const auto& monitor : DisplayMonitorInventory::Enumerate()) names.push_back(monitor.label);
    return names;
}
