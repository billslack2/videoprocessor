#pragma once

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace ActiveProfileStatus
{
    constexpr uint32_t Version = 4;
    constexpr wchar_t MappingName[] = L"Local\\VideoProcessor.ActiveProfileStatus.v4";
    constexpr size_t MaximumActiveShaders = 16;

    struct Snapshot
    {
        uint32_t version = Version;
        uint32_t processId = 0;
        uint64_t generation = 0;
        char queue[96]{};
        char renderer[96]{};
        char viewport[96]{};
        uint64_t rendererGeneration = 0;
        uint64_t shaderGeneration = 0;
        uint32_t shaderAvailable = 0;
        uint32_t shaderCount = 0;
        char shaders[MaximumActiveShaders][96]{};
        char sourceEotf[32]{};
        char sourceColorSpace[32]{};
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

    inline bool ShaderSetIsCurrent(const Snapshot& snapshot)
    {
        return snapshot.shaderAvailable != 0 &&
            snapshot.rendererGeneration != 0 &&
            snapshot.shaderGeneration == snapshot.rendererGeneration &&
            snapshot.shaderCount <= MaximumActiveShaders;
    }

    inline void Publish(uint32_t processId, uint64_t generation,
        const std::map<std::string, std::string>& selections,
        uint64_t rendererGeneration, bool shaderAvailable,
        const std::vector<std::string>& shaders,
        const std::string& sourceEotf = {},
        const std::string& sourceColorSpace = {})
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
        next.rendererGeneration = rendererGeneration;
        next.shaderAvailable = shaderAvailable ? 1u : 0u;
        next.shaderGeneration = shaderAvailable ? rendererGeneration : 0;
        next.shaderCount = shaderAvailable ? static_cast<uint32_t>(
            (std::min)(shaders.size(), MaximumActiveShaders)) : 0;
        for (uint32_t index = 0; index < next.shaderCount; ++index)
            Copy(next.shaders[index], sizeof(next.shaders[index]), shaders[index]);
        Copy(next.sourceEotf, sizeof(next.sourceEotf), sourceEotf);
        Copy(next.sourceColorSpace, sizeof(next.sourceColorSpace), sourceColorSpace);
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
