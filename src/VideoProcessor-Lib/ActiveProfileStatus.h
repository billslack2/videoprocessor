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
        Copy(next.queue, sizeof(next.queue), queue == selections.end() ?
            std::string() : SectionFor("queue", queue->second));
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
        if (expectedProcessId == 0) return false;
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
        if (!mapping) return false;
        const auto* shared = static_cast<const Snapshot*>(MapViewOfFile(mapping,
            FILE_MAP_READ, 0, 0, sizeof(Snapshot)));
        if (!shared) { CloseHandle(mapping); return false; }
        MemoryBarrier();
        result = *shared;
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        return result.version == Version && result.processId == expectedProcessId;
    }
}
