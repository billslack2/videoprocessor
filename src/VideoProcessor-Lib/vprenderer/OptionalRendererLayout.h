#pragma once

#include <array>
#include <string>

#include <Windows.h>


namespace OptionalRendererLayout
{
	inline const wchar_t* DirectoryName(bool beta = false) { return beta ? L"vprenderer-beta" : L"vprenderer"; }
	inline const wchar_t* PluginFileName(bool beta = false)
	{
		return beta ? L"VideoProcessorVPRendererBeta.dll" : L"VideoProcessorVPRenderer.dll";
	}
	inline const std::array<const wchar_t*, 9>& PrivateDependencies(bool beta = false)
	{
		static const std::array<const wchar_t*, 9> dependencies = {
			L"libdovi.dll",
			L"libgcc_s_seh-1.dll",
			L"liblcms2-2.dll",
			L"libplacebo-360.dll",
			L"libshaderc_shared.dll",
			L"libspirv-cross-c-shared.dll",
			L"libstdc++-6.dll",
			L"libwinpthread-1.dll",
			L"vulkan-1.dll"
		};
		static const std::array<const wchar_t*, 9> betaDependencies = {
			L"libdovi.dll", L"libgcc_s_seh-1.dll", L"liblcms2-2.dll",
			L"libplacebo-388.dll", L"libshaderc_shared.dll",
			L"libspirv-cross-c-shared.dll", L"libstdc++-6.dll",
			L"libwinpthread-1.dll", L"vulkan-1.dll"
		};
		return beta ? betaDependencies : dependencies;
	}

	inline std::wstring Join(
		const std::wstring& directory, const wchar_t* name)
	{
		return directory + L"\\" + name;
	}

	inline std::wstring Directory(const std::wstring& executableDirectory, bool beta = false)
	{
		return Join(executableDirectory, DirectoryName(beta));
	}

	inline std::wstring PluginPath(const std::wstring& executableDirectory, bool beta = false)
	{
		return Join(Directory(executableDirectory, beta), PluginFileName(beta));
	}

	inline bool IsFile(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	inline bool FindMissingRuntimeFile(
		const std::wstring& executableDirectory,
		std::wstring& missingPath, bool beta = false)
	{
		missingPath = PluginPath(executableDirectory, beta);
		if (!IsFile(missingPath))
			return true;
		for (const wchar_t* dependency : PrivateDependencies(beta))
		{
			missingPath = Join(Directory(executableDirectory, beta), dependency);
			if (!IsFile(missingPath))
				return true;
		}
		missingPath.clear();
		return false;
	}
}
