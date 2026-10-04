#include "pch.h"

#include "ConfigurationDiscovery.h"

#include "RendererId.h"
#include "microsoft_directshow/video_renderers/DirectShowVideoRenderers.h"

#include <DeckLinkAPI_h.h>

#include <LibMWCapture/MWCapture.h>

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

	// The Magewell SDK reports names as ANSI. Product names are plain ASCII,
	// so a direct widening avoids any dependency on the active code page.
	std::wstring WidenAscii(const CHAR* value, size_t capacity)
	{
		std::wstring widened;
		for (size_t index = 0; index < capacity && value[index] != '\0'; ++index)
			widened.push_back(static_cast<wchar_t>(
				static_cast<unsigned char>(value[index])));
		return widened;
	}

	// Mirrors the DeckLink convention: a single device is named plainly and
	// multiple devices carry a one-based suffix so each entry stays unique.
	std::wstring MagewellDisplayName(
		const MWCAP_CHANNEL_INFO& info, int index, int channelCount)
	{
		std::wstring name = WidenAscii(
			info.szProductName, sizeof(info.szProductName));
		if (name.empty())
			name = L"Magewell Capture";
		if (channelCount > 1)
			name += L" (" + std::to_wstring(index + 1) + L")";
		return name;
	}

	// Magewell devices are enumerated through the MWCapture SDK rather than
	// through COM. The SDK instance is opened and closed inside this call
	// because configuration discovery is a short-lived query, not a session.
	void AppendMagewellCaptureDeviceNames(std::vector<std::wstring>& names)
	{
		if (!MWCaptureInitInstance())
			return;

		if (MWRefreshDevice() != MW_SUCCEEDED)
		{
			MWCaptureExitInstance();
			return;
		}

		const int channelCount = MWGetChannelCount();
		for (int index = 0; index < channelCount; ++index)
		{
			MWCAP_CHANNEL_INFO info = {};
			if (MWGetChannelInfoByIndex(index, &info) != MW_SUCCEEDED)
				continue;

			names.emplace_back(
				MagewellDisplayName(info, index, channelCount));
		}

		MWCaptureExitInstance();
	}

	// Reports the physical connectors of a Magewell channel using the same
	// connection labels the DeckLink path produces, so the configuration
	// editor presents one consistent vocabulary regardless of vendor.
	std::vector<std::wstring> MagewellCaptureConnectionNames(
		const std::wstring& captureDeviceName)
	{
		std::vector<std::wstring> names;
		if (!MWCaptureInitInstance())
			return names;

		if (MWRefreshDevice() != MW_SUCCEEDED)
		{
			MWCaptureExitInstance();
			return names;
		}

		const int channelCount = MWGetChannelCount();
		for (int index = 0; index < channelCount; ++index)
		{
			MWCAP_CHANNEL_INFO info = {};
			if (MWGetChannelInfoByIndex(index, &info) != MW_SUCCEEDED)
				continue;
			if (_wcsicmp(MagewellDisplayName(info, index, channelCount).c_str(),
				captureDeviceName.c_str()) != 0)
				continue;

			WCHAR path[128] = {};
			if (MWGetDevicePath(index, path) != MW_SUCCEEDED)
				break;

			const HCHANNEL channel = MWOpenChannelByPath(path);
			if (channel == nullptr)
				break;

			DWORD sources[16] = {};
			DWORD sourceCount = ARRAYSIZE(sources);
			if (MWGetVideoInputSourceArray(channel, sources, &sourceCount)
				== MW_SUCCEEDED)
			{
				DWORD seen = 0;
				for (DWORD source = 0; source < sourceCount; ++source)
				{
					const DWORD type = INPUT_TYPE(sources[source]);
					if ((seen & type) != 0)
						continue;
					seen |= type;

					switch (type)
					{
					case MWCAP_VIDEO_INPUT_TYPE_HDMI:
						names.emplace_back(L"HDMI"); break;
					case MWCAP_VIDEO_INPUT_TYPE_SDI:
						names.emplace_back(L"SDI"); break;
					case MWCAP_VIDEO_INPUT_TYPE_COMPONENT:
						names.emplace_back(L"Component"); break;
					case MWCAP_VIDEO_INPUT_TYPE_CVBS:
						names.emplace_back(L"Composite"); break;
					case MWCAP_VIDEO_INPUT_TYPE_YC:
						names.emplace_back(L"S-Video"); break;
					default:
						break;
					}
				}
			}

			MWCloseChannel(channel);
			break;
		}

		MWCaptureExitInstance();
		return names;
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
	AppendMagewellCaptureDeviceNames(names);
	return names;
}

std::vector<std::wstring> ConfigurationDiscovery::CaptureConnectionNames(
	const std::wstring& captureDeviceName)
{
	std::vector<std::wstring> names;
	CComPtr<IDeckLinkIterator> iterator;
	if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
		IID_IDeckLinkIterator, reinterpret_cast<void**>(&iterator)) != S_OK)
		return MagewellCaptureConnectionNames(captureDeviceName);
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
	return MagewellCaptureConnectionNames(captureDeviceName);
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
