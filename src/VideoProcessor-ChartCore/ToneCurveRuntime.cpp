#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "ToneCurveRuntime.h"
#include <vprenderer/OptionalRendererLayout.h>

namespace ToneCurveRuntime
{
    namespace
    {
        HMODULE library = nullptr;
    }

    bool Load(std::wstring& missing)
    {
        static bool tried = false;
        static std::wstring failure;
        if (!tried)
        {
            tried = true;
            // The Config application runs from config\; VP Renderer's folder is
            // its sibling, where OptionalRendererLayout places it. The charts draw
            // VP Renderer (Beta), so its folder and libplacebo are the ones loaded.
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                static_cast<DWORD>(path.size()));
            path.resize(length);
            const std::wstring configDirectory = path.substr(0, path.find_last_of(L"\\/"));
            const std::wstring root = configDirectory.substr(0, configDirectory.find_last_of(L"\\/"));
            if (length == 0)
                failure = L"the Config application's own path";
            else if (!OptionalRendererLayout::FindMissingRuntimeFile(root, failure, true))
            {
                // A full path loads the companions from the same folder.
                const std::wstring file = OptionalRendererLayout::Join(
                    OptionalRendererLayout::Directory(root, true), L"libplacebo-377.dll");
                library = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (!library) failure = file;
            }
        }
        missing = failure;
        return library != nullptr;
    }

    void* Find(const char* exportName)
    {
        return library ? reinterpret_cast<void*>(GetProcAddress(library, exportName)) : nullptr;
    }
}
