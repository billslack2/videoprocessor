#pragma once

#include <array>
#include <string>

#include <Windows.h>


namespace OptionalRendererLayout
{
	inline const wchar_t* DirectoryName() { return L"vprenderer"; }
	inline const wchar_t* PluginFileName()
	{
		return L"VideoProcessorVPRenderer.dll";
	}
	inline const std::array<const wchar_t*, 9>& PrivateDependencies()
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
		return dependencies;
	}

	inline std::wstring Join(
		const std::wstring& directory, const wchar_t* name)
	{
		return directory + L"\\" + name;
	}

	inline std::wstring Directory(const std::wstring& executableDirectory)
	{
		return Join(executableDirectory, DirectoryName());
	}

	inline std::wstring PluginPath(const std::wstring& executableDirectory)
	{
		return Join(Directory(executableDirectory), PluginFileName());
	}

	inline bool IsFile(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	inline bool FindMissingRuntimeFile(
		const std::wstring& executableDirectory,
		std::wstring& missingPath)
	{
		missingPath = PluginPath(executableDirectory);
		if (!IsFile(missingPath))
			return true;
		for (const wchar_t* dependency : PrivateDependencies())
		{
			missingPath = Join(Directory(executableDirectory), dependency);
			if (!IsFile(missingPath))
				return true;
		}
		missingPath.clear();
		return false;
	}
}
