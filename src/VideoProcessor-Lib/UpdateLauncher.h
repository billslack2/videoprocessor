#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Shared native launcher: all networking and installation happens out of process.
namespace UpdateLauncher
{
    constexpr UINT MenuCommand = 0x1ee0;
    constexpr UINT_PTR StartupTimer = 0x7a31;
    inline std::wstring InstallationRoot()
    {
        wchar_t executable[32768] = {};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) return {};
        std::wstring root(executable, length);
        root.resize(root.find_last_of(L"\\/"));
        const auto last = root.find_last_of(L"\\/");
        if (last != std::wstring::npos && _wcsicmp(root.substr(last + 1).c_str(), L"config") == 0)
            root.resize(last);
        return root;
    }
    inline bool InstallationPending()
    {
        const auto file = InstallationRoot() + L"\\.vp-update.lock";
        HANDLE probe = CreateFileW(file.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (probe != INVALID_HANDLE_VALUE) { CloseHandle(probe); return false; }
        return GetLastError() == ERROR_SHARING_VIOLATION;
    }
    inline bool Launch(bool background, HWND owner = nullptr)
    {
        const std::wstring root = InstallationRoot();
        if (root.empty()) return false;
        const std::wstring helper = root + L"\\VideoProcessorUpdate.exe";
        if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            if (!background) MessageBoxW(owner, L"This copy does not include the updater. Install the current VideoProcessor setup to enable updates.", L"VideoProcessor updates", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        std::wstring command = L"\"" + helper + L"\" --root \"" + root + L"\"";
        if (background) command += L" --background";
        std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(0);
        STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        const bool started = CreateProcessW(helper.c_str(), writable.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process) != FALSE;
        if (started) { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
        else if (!background) MessageBoxW(owner, L"The update helper could not start. Rerun setup to repair this installation.", L"VideoProcessor updates", MB_OK | MB_ICONERROR);
        return started;
    }
}
