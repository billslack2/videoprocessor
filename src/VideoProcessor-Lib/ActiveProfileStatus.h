#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace ActiveProfileStatus
{
    constexpr uint32_t Version = 2;
    constexpr wchar_t MappingName[] = L"Local\\VideoProcessor.ActiveProfileStatus.v2";

    struct Snapshot
    {
        uint32_t version = Version;
        uint32_t processId = 0;
        uint64_t generation = 0;
        char queue[96]{};
        char renderer[96]{};
        char viewport[96]{};
        char shader[96]{};
    };

    inline void Copy(char* destination, size_t count, const std::string& value)
    {
        strncpy_s(destination, count, value.c_str(), _TRUNCATE);
    }

    inline std::string SectionFor(const char* root, const std::string& profile)
    {
        if (profile.empty()) return {};
        return profile == "base" ? root : std::string(root) + "." + profile;
    }

    inline void Publish(uint32_t processId, uint64_t generation,
        const std::map<std::string, std::string>& selections,
        const std::string& shader)
    {
        static HANDLE mapping = nullptr;
        static Snapshot* shared = nullptr;
        if (!shared)
        {
            mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                PAGE_READWRITE, 0, sizeof(Snapshot), MappingName);
            if (!mapping) return;
            shared = static_cast<Snapshot*>(MapViewOfFile(mapping,
                FILE_MAP_WRITE, 0, 0, sizeof(Snapshot)));
            if (!shared) { CloseHandle(mapping); mapping = nullptr; return; }
        }

        Snapshot next;
        next.processId = processId;
        next.generation = generation;
        const auto renderer = selections.find("display");
        const auto queue = selections.find("queue");
        const auto viewport = selections.find("viewport");
        // Queue profiles use the first (root) profile whenever no conditional
        // selection overrides it. Publish that resolved default explicitly so
        // the editor can mark it active just like an explicitly selected one.
        Copy(next.queue, sizeof(next.queue), queue == selections.end() ?
            std::string("queue") : SectionFor("queue", queue->second));
        Copy(next.renderer, sizeof(next.renderer), renderer == selections.end() ?
            std::string() : SectionFor("vprenderer", renderer->second));
        Copy(next.viewport, sizeof(next.viewport), viewport == selections.end() ?
            std::string() : SectionFor("vprenderer.viewport", viewport->second));
        Copy(next.shader, sizeof(next.shader), shader);
        *shared = next;
        MemoryBarrier();
    }

    inline bool Read(uint32_t expectedProcessId, Snapshot& result)
    {
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
        if (!mapping) return false;
        const auto* shared = static_cast<const Snapshot*>(MapViewOfFile(mapping,
            FILE_MAP_READ, 0, 0, sizeof(Snapshot)));
        if (!shared) { CloseHandle(mapping); return false; }
        MemoryBarrier();
        result = *shared;
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        // Config may be launched directly, without a VP-owned HWND. In that
        // case accept the only live publisher, but still require that its
        // process is alive. An editor launched by VP continues to require an
        // exact PID match.
        if (result.version != Version || result.processId == 0 ||
            (expectedProcessId != 0 && result.processId != expectedProcessId))
            return false;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
            result.processId);
        if (!process) return false;
        DWORD exitCode = 0;
        const bool running = GetExitCodeProcess(process, &exitCode) &&
            exitCode == STILL_ACTIVE;
        CloseHandle(process);
        return running;
    }
}
