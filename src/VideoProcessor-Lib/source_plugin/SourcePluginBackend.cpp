/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include <dxgi1_6.h>
#include <atlbase.h>

#include <DebugLog.h>

#include "SourcePluginBackend.h"
#include "SourcePluginCaptureDevice.h"


namespace
{
	// A plugin DLL whose function table passed every check. The module is
	// loaded; whoever holds this frees it or hands it to a device.
	struct LoadedPlugin
	{
		HMODULE module = nullptr;
		const vp_source_plugin_api* api = nullptr;
		std::wstring path;
		std::wstring name;
		std::string dirUtf8;
	};

	std::wstring Utf8ToWide(const char* utf8)
	{
		const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
		if (n <= 1)
			return std::wstring();
		std::wstring result(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], n);
		result.resize(n - 1);
		return result;
	}

	std::string WideToUtf8(const std::wstring& wide)
	{
		const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1)
			return std::string();
		std::string result(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], n, nullptr, nullptr);
		result.resize(n - 1);
		return result;
	}

	std::wstring ParentDirectory(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
	}

	bool IsDirectory(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	// plugins\sources beside VideoProcessor.exe. This code also runs inside
	// config\VideoProcessorConfigDiscovery.dll, one folder down, so look
	// beside the module holding this code first, then in its parent.
	std::wstring PluginsDirectory()
	{
		HMODULE self = nullptr;
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&PluginsDirectory), &self))
			return std::wstring();

		wchar_t modulePath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(self, modulePath, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return std::wstring();

		const std::wstring moduleDirectory = ParentDirectory(modulePath);
		for (const std::wstring& root : { moduleDirectory, ParentDirectory(moduleDirectory) })
		{
			if (root.empty())
				continue;
			const std::wstring candidate = root + L"\\plugins\\sources";
			if (IsDirectory(candidate))
				return candidate;
		}

		return std::wstring();
	}

	const char* RefusalReason(const vp_source_plugin_api* api)
	{
		if (!api)
			return "get_api returned no table";
		if (api->abi_version != VP_SOURCE_PLUGIN_ABI_VERSION)
			return "ABI version differs";
		if (api->struct_size < sizeof(vp_source_plugin_api))
			return "function table too small";
		if (!api->name || !api->name[0])
			return "no name";
		if (!api->create || !api->destroy || !api->start || !api->stop ||
			!api->get_state || !api->wait_frame || !api->release_frame ||
			!api->clock_now || !api->clock_ticks_per_second ||
			!api->frames_captured || !api->frames_missed || !api->send_command)
			return "missing function";
		return nullptr;
	}

	// Loads every plugin that passes the checks, in file-name order. Refused
	// DLLs are logged and freed. Names must be unique: VP and the editor
	// select a capture device by name.
	std::vector<LoadedPlugin> LoadPlugins()
	{
		std::vector<LoadedPlugin> plugins;

		const std::wstring directory = PluginsDirectory();
		if (directory.empty())
			return plugins;

		std::vector<std::wstring> files;
		WIN32_FIND_DATAW findData = {};
		const HANDLE find = FindFirstFileW((directory + L"\\*.dll").c_str(), &findData);
		if (find == INVALID_HANDLE_VALUE)
			return plugins;
		do
		{
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			// The wildcard also matches longer extensions such as .dll~.
			const std::wstring file = findData.cFileName;
			if (file.size() < 4 || _wcsicmp(file.c_str() + file.size() - 4, L".dll") != 0)
				continue;
			files.push_back(file);
		} while (FindNextFileW(find, &findData));
		FindClose(find);

		std::sort(files.begin(), files.end());

		for (const std::wstring& file : files)
		{
			LoadedPlugin plugin;
			plugin.path = directory + L"\\" + file;
			const std::string pathUtf8 = WideToUtf8(plugin.path);

			plugin.module = LoadLibraryExW(plugin.path.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (!plugin.module)
			{
				DebugLog::Log("Source plugin refused: %s: LoadLibrary failed, error=%lu",
					pathUtf8.c_str(), GetLastError());
				continue;
			}

			const auto getApi = reinterpret_cast<vp_source_plugin_get_api_fn>(
				GetProcAddress(plugin.module, VP_SOURCE_PLUGIN_ENTRY_NAME));
			plugin.api = getApi ? getApi(VP_SOURCE_PLUGIN_ABI_VERSION) : nullptr;

			const char* refusal = getApi ? RefusalReason(plugin.api) :
				"no " VP_SOURCE_PLUGIN_ENTRY_NAME " export";
			if (!refusal)
			{
				plugin.name = Utf8ToWide(plugin.api->name);
				for (const LoadedPlugin& other : plugins)
					if (_wcsicmp(other.name.c_str(), plugin.name.c_str()) == 0)
						refusal = "name already used by another plugin";
			}

			if (refusal)
			{
				DebugLog::Log("Source plugin refused: %s: %s", pathUtf8.c_str(), refusal);
				FreeLibrary(plugin.module);
				continue;
			}

			plugin.dirUtf8 = WideToUtf8(directory);
			plugins.push_back(std::move(plugin));
		}

		return plugins;
	}

	void FreePlugins(std::vector<LoadedPlugin>& plugins)
	{
		for (LoadedPlugin& plugin : plugins)
			FreeLibrary(plugin.module);
		plugins.clear();
	}


	class SourcePluginCaptureDeviceDiscoverer:
		public ACaptureDeviceDiscoverer
	{
	public:

		SourcePluginCaptureDeviceDiscoverer(ICaptureDeviceDiscovererCallback& callback):
			ACaptureDeviceDiscoverer(callback)
		{
		}

		// ACaptureDeviceDiscoverer
		void Start() override
		{
			if (m_started)
				throw std::runtime_error("Discoverer already started");
			m_started = true;

			for (LoadedPlugin& plugin : LoadPlugins())
			{
				ACaptureDeviceComPtr captureDevice;
				try
				{
					captureDevice.Attach(new SourcePluginCaptureDevice(
						plugin.module, plugin.api, plugin.dirUtf8));
				}
				catch (const std::exception& error)
				{
					DebugLog::Log("Source plugin refused: %s: %s",
						WideToUtf8(plugin.path).c_str(), error.what());
					FreeLibrary(plugin.module);
					continue;
				}

				// The callback takes over the reference it is given.
				m_captureDevices.push_back(captureDevice);
				m_callback.OnCaptureDeviceFound(captureDevice);
			}
		}

		void Stop() override
		{
			if (!m_started)
				throw std::runtime_error("Discoverer not running");
			m_started = false;

			// The callback takes over each reference, as with DeckLink.
			for (ACaptureDeviceComPtr& captureDevice : m_captureDevices)
				m_callback.OnCaptureDeviceLost(captureDevice);

			m_captureDevices.clear();
		}

		// IUnknown
		HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override
		{
			if (!ppv)
				return E_INVALIDARG;

			*ppv = nullptr;
			if (iid == IID_IUnknown)
			{
				*ppv = this;
				AddRef();
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		ULONG AddRef() override
		{
			return ++m_refCount;
		}

		ULONG Release() override
		{
			const ULONG newRefValue = --m_refCount;
			if (newRefValue == 0)
				delete this;

			return newRefValue;
		}

	private:
		bool m_started = false;
		std::vector<ACaptureDeviceComPtr> m_captureDevices;

		// The owner assigns this to a CComPtr, which takes the first reference.
		std::atomic<ULONG> m_refCount{0};
	};

	// GUI thread only.
	std::string& PlayerKeysLine()
	{
		static std::string line;
		return line;
	}

	std::atomic<HMONITOR>& FullscreenMonitorHandle()
	{
		static std::atomic<HMONITOR> monitor{ nullptr };
		return monitor;
	}

	// The active path's refresh rate as a rational (e.g. 24000/1001), which
	// EnumDisplaySettings' integer dmDisplayFrequency cannot give.
	bool TargetRefreshRate(const wchar_t* gdiDeviceName, UINT32& numerator, UINT32& denominator)
	{
		UINT32 pathCount = 0;
		UINT32 modeCount = 0;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
			pathCount == 0)
			return false;
		std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
		std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
			&modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
			return false;
		for (UINT32 i = 0; i < pathCount; ++i)
		{
			DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
			source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			source.header.size = sizeof(source);
			source.header.adapterId = paths[i].sourceInfo.adapterId;
			source.header.id = paths[i].sourceInfo.id;
			if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
				wcscmp(source.viewGdiDeviceName, gdiDeviceName) != 0)
				continue;
			numerator = paths[i].targetInfo.refreshRate.Numerator;
			denominator = paths[i].targetInfo.refreshRate.Denominator;
			return numerator > 0 && denominator > 0;
		}
		return false;
	}

	// Windows HDR is on for the display when DXGI reports its output in PQ.
	bool OutputIsPq(HMONITOR monitor, bool& pq)
	{
		CComPtr<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
			return false;
		for (UINT a = 0; a < 64; ++a)
		{
			CComPtr<IDXGIAdapter1> adapter;
			if (FAILED(factory->EnumAdapters1(a, &adapter)))
				break;
			for (UINT o = 0; o < 64; ++o)
			{
				CComPtr<IDXGIOutput> output;
				if (FAILED(adapter->EnumOutputs(o, &output)))
					break;
				DXGI_OUTPUT_DESC desc = {};
				if (FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor)
					continue;
				CComQIPtr<IDXGIOutput6> output6(output);
				DXGI_OUTPUT_DESC1 desc1 = {};
				if (!output6 || FAILED(output6->GetDesc1(&desc1)))
					return false;
				pq = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
				return true;
			}
		}
		return false;
	}

	// The chord as VP's shortcut parser spells it (Ctrl, Alt, Shift, key);
	// empty for a cleared key. Covers the keys that parser accepts.
	std::string ChordText(const ACCEL& accelerator)
	{
		const WORD key = accelerator.key;
		std::string keyText;
		if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
			keyText = std::string(1, static_cast<char>(key));
		else if (key >= VK_F1 && key <= VK_F24)
			keyText = "F" + std::to_string(key - VK_F1 + 1);
		else if (key == VK_ESCAPE)
			keyText = "Esc";
		else if (key == VK_RETURN)
			keyText = "Enter";
		else
			return {};

		std::string chord;
		if (accelerator.fVirt & FCONTROL) chord += "Ctrl+";
		if (accelerator.fVirt & FALT) chord += "Alt+";
		if (accelerator.fVirt & FSHIFT) chord += "Shift+";
		return chord + keyText;
	}
}


namespace SourcePluginBackend
{
	CComPtr<ACaptureDeviceDiscoverer> CreateDiscoverer(ICaptureDeviceDiscovererCallback& callback)
	{
		return CComPtr<ACaptureDeviceDiscoverer>(new SourcePluginCaptureDeviceDiscoverer(callback));
	}

	bool IsSourcePluginDevice(ACaptureDevice* device)
	{
		return dynamic_cast<SourcePluginCaptureDevice*>(device) != nullptr;
	}

	bool SendSourcePluginCommand(ACaptureDevice* device, const char* commandUtf8)
	{
		SourcePluginCaptureDevice* plugin = dynamic_cast<SourcePluginCaptureDevice*>(device);
		if (!plugin)
			return false;

		plugin->SendCommand(commandUtf8);
		return true;
	}

	void SetPlayerKeys(const std::vector<PlayerKey>& keys, ACaptureDevice* activeDevice)
	{
		std::string line = "keys ";
		for (size_t i = 0; i < keys.size(); ++i)
		{
			if (i)
				line += ';';
			line += keys[i].name + '=' + ChordText(keys[i].accelerator);
		}
		PlayerKeysLine() = line;
		SendSourcePluginCommand(activeDevice, line.c_str());
	}

	const std::string& PlayerKeysCommand()
	{
		return PlayerKeysLine();
	}

	void SetFullscreenMonitor(HMONITOR monitor)
	{
		FullscreenMonitorHandle().store(monitor);
	}

	std::string DesktopModeFormat(std::string& why)
	{
		const HMONITOR monitor = FullscreenMonitorHandle().load();
		MONITORINFOEXW info = {};
		info.cbSize = sizeof(info);
		if (!monitor || !GetMonitorInfoW(monitor, &info))
		{
			why = "no fullscreen display";
			return std::string();
		}
		DEVMODEW mode = {};
		mode.dmSize = sizeof(mode);
		if (!EnumDisplaySettingsExW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0) ||
			!mode.dmPelsWidth || !mode.dmPelsHeight)
		{
			why = "desktop mode unavailable";
			return std::string();
		}
		UINT32 numerator = 0;
		UINT32 denominator = 0;
		if (!TargetRefreshRate(info.szDevice, numerator, denominator))
		{
			why = "refresh rate unavailable";
			return std::string();
		}
		bool pq = false;
		if (!OutputIsPq(monitor, pq))
		{
			why = "DXGI colour space unavailable";
			return std::string();
		}
		char format[64];
		sprintf_s(format, "%lux%lu@%u/%u %s", mode.dmPelsWidth, mode.dmPelsHeight,
			numerator, denominator, pq ? "pq" : "sdr");
		return format;
	}

	void AppendDeviceNames(std::vector<std::wstring>& names)
	{
		std::vector<LoadedPlugin> plugins = LoadPlugins();
		for (const LoadedPlugin& plugin : plugins)
			names.push_back(plugin.name);
		FreePlugins(plugins);
	}

	bool IsDeviceName(const std::wstring& deviceName)
	{
		std::vector<LoadedPlugin> plugins = LoadPlugins();
		bool found = false;
		for (const LoadedPlugin& plugin : plugins)
			if (_wcsicmp(plugin.name.c_str(), deviceName.c_str()) == 0)
				found = true;
		FreePlugins(plugins);
		return found;
	}

	std::vector<std::wstring> ConnectionNames(const std::wstring& deviceName)
	{
		if (!IsDeviceName(deviceName))
			return {};

		return { SOURCE_PLUGIN_INPUT_NAME };
	}
}
