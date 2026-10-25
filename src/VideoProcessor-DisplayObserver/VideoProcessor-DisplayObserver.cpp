#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntcons.h>
#include <evntrace.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include "PresentMonTraceConsumer.hpp"
#include "PresentMonTraceSession.hpp"

namespace
{
struct QueryTraceProperties : EVENT_TRACE_PROPERTIES
{
    wchar_t SessionName[MAX_PATH];
};

struct Options
{
    uint32_t targetPid = 0;
    uint64_t seconds = 0;
    HANDLE parentHandle = nullptr;
    HANDLE externalStopHandle = nullptr;
    std::wstring sessionName;
    bool hasParentHandle = false;
    bool hasExternalStopHandle = false;
};

const wchar_t* FindValue(int argc, wchar_t** argv, const wchar_t* name, bool& found)
{
    found = false;
    const wchar_t* result = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (wcscmp(argv[i], name) == 0)
        {
            if (found) return nullptr;
            found = true;
            result = argv[i + 1];
        }
    }
    return result;
}

bool ParseUnsigned(const wchar_t* text, uint64_t& value, int base)
{
    if (text == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    const auto parsed = wcstoull(text, &end, base);
    if (text == end || *end != L'\0') return false;
    value = parsed;
    return true;
}

bool ReadIntegerOption(int argc, wchar_t** argv, const wchar_t* name,
                       uint64_t& value, int base, bool required)
{
    bool found = false;
    const wchar_t* text = FindValue(argc, argv, name, found);
    if (!found) return !required;
    return ParseUnsigned(text, value, base);
}

bool ReadStringOption(int argc, wchar_t** argv, const wchar_t* name,
                      std::wstring& value, bool required)
{
    bool found = false;
    const wchar_t* text = FindValue(argc, argv, name, found);
    if (!found) return !required;
    if (text == nullptr || *text == L'\0') return false;
    value = text;
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options& options)
{
    uint64_t pid = 0;
    uint64_t parentHandleValue = 0;
    uint64_t stopHandleValue = 0;
    uint64_t parentPid = 0;
    if (!ReadIntegerOption(argc, argv, L"--pid", pid, 10, true) ||
        !ReadIntegerOption(argc, argv, L"--seconds", options.seconds, 10, true) ||
        !ReadIntegerOption(argc, argv, L"--parent-handle", parentHandleValue, 0, false) ||
        !ReadIntegerOption(argc, argv, L"--stop-handle", stopHandleValue, 0, false) ||
        !ReadIntegerOption(argc, argv, L"--parent-pid", parentPid, 10, false) ||
        !ReadStringOption(argc, argv, L"--session-name", options.sessionName, false) ||
        pid == 0 || pid > UINT32_MAX || options.seconds > 86400 ||
        (parentPid != 0 && parentHandleValue != 0) ||
        parentHandleValue > static_cast<uint64_t>((std::numeric_limits<uintptr_t>::max)()) ||
        stopHandleValue > static_cast<uint64_t>((std::numeric_limits<uintptr_t>::max)()))
    {
        return false;
    }

    options.targetPid = static_cast<uint32_t>(pid);
    if (parentHandleValue != 0)
    {
        options.parentHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parentHandleValue));
        options.hasParentHandle = true;
    }
    else if (parentPid != 0)
    {
        if (parentPid > UINT32_MAX || parentPid == GetCurrentProcessId()) return false;
        options.parentHandle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parentPid));
        if (options.parentHandle == nullptr) return false;
        options.hasParentHandle = true;
    }

    if (stopHandleValue != 0)
    {
        options.externalStopHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(stopHandleValue));
        options.hasExternalStopHandle = true;
    }

    // Infinite production mode must have both independent shutdown signals.
    if (options.seconds == 0 &&
        (!options.hasParentHandle || !options.hasExternalStopHandle ||
         options.sessionName.empty()))
    {
        return false;
    }
    return true;
}

bool EmitLine(HANDLE localStopEvent, const char* format, ...)
{
    char line[256]{};
    va_list args;
    va_start(args, format);
    const int length = _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(line))
    {
        SetEvent(localStopEvent);
        return false;
    }
    if (fwrite(line, 1, static_cast<size_t>(length), stdout) != static_cast<size_t>(length) ||
        fflush(stdout) != 0)
    {
        SetEvent(localStopEvent);
        return false;
    }
    return true;
}

ULONG QueryLossCounters(PMTraceSession const& session, ULONG& eventsLost, ULONG& buffersLost)
{
    QueryTraceProperties properties{};
    properties.Wnode.BufferSize = static_cast<ULONG>(sizeof(properties));
    properties.LoggerNameOffset = static_cast<ULONG>(offsetof(QueryTraceProperties, SessionName));
    const auto status = ControlTraceW(session.mSessionHandle, nullptr, &properties,
                                      EVENT_TRACE_CONTROL_QUERY);
    if (status == ERROR_SUCCESS)
    {
        eventsLost = properties.EventsLost;
        buffersLost = properties.LogBuffersLost + properties.RealTimeBuffersLost;
    }
    return status;
}

void EmitStatus(HANDLE stopEvent, ULONG winError, int64_t eventsLost,
                int64_t buffersLost, uint64_t completedRingOverflows)
{
    EmitLine(stopEvent, "STATUS %lu %lld %lld %llu\n",
        winError,
        static_cast<long long>(eventsLost),
        static_cast<long long>(buffersLost),
        static_cast<unsigned long long>(completedRingOverflows));
}

void EmitEvents(HANDLE stopEvent, PMTraceConsumer& consumer, uint32_t targetPid,
                std::vector<std::shared_ptr<PresentEvent>>& events)
{
    consumer.DequeuePresentEvents(events);
    for (const auto& event : events)
    {
        if (!event || event->ProcessId != targetPid) continue;
        const auto flags = (event->IsLost ? 1u : 0u) |
                           (event->PresentFailed ? 2u : 0u);
        if (!EmitLine(stopEvent,
            "EVENT %u %u 0x%llX %llu %llu %u %u 0x%02X\n",
            event->ProcessId,
            event->ThreadId,
            static_cast<unsigned long long>(event->SwapChainAddress),
            static_cast<unsigned long long>(event->PresentStartTime),
            static_cast<unsigned long long>(event->ScreenTime),
            static_cast<unsigned>(event->FinalState),
            static_cast<unsigned>(event->PresentMode),
            flags))
        {
            break;
        }
    }
}

bool ValidateWaitableHandle(HANDLE handle, const char* label)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    const auto result = WaitForSingleObject(handle, 0);
    if (result == WAIT_TIMEOUT) return true;
    if (result == WAIT_OBJECT_0) return true;
    std::fprintf(stderr, "%s handle is not waitable: %lu\n", label, GetLastError());
    return false;
}

void WatchInputPipe(HANDLE input, HANDLE stopEvent)
{
    char buffer[128];
    DWORD bytesRead = 0;
    for (;;)
    {
        if (!ReadFile(input, buffer, sizeof(buffer), &bytesRead, nullptr) || bytesRead == 0)
        {
            SetEvent(stopEvent); // Parent closed its control pipe.
            return;
        }
        for (DWORD i = 0; i + 4 <= bytesRead; ++i)
        {
            if (buffer[i] == 'S' && buffer[i + 1] == 'T' &&
                buffer[i + 2] == 'O' && buffer[i + 3] == 'P')
            {
                SetEvent(stopEvent);
                return;
            }
        }
    }
}

void EmitProtocolError(HANDLE stopEvent, ULONG error, const char* detail)
{
    EmitLine(stopEvent, "ERROR %lu\n", error);
    std::fprintf(stderr, "%s: %lu\n", detail, error);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::fprintf(stderr,
            "usage: VideoProcessorDisplayObserver --pid N --seconds N "
            "[--parent-handle H --stop-handle H --session-name NAME]\n"
            "       seconds=0 requires parent, stop, and session-name handles/options\n");
        return 2;
    }

    if ((options.hasParentHandle &&
         !ValidateWaitableHandle(options.parentHandle, "parent")) ||
        (options.hasExternalStopHandle &&
         !ValidateWaitableHandle(options.externalStopHandle, "stop")))
    {
        std::fprintf(stderr, "invalid, signaled, or unavailable shutdown handle\n");
        return 2;
    }

    if (options.hasParentHandle && WaitForSingleObject(options.parentHandle, 0) == WAIT_OBJECT_0)
    {
        return 0;
    }
    if (options.hasExternalStopHandle &&
        WaitForSingleObject(options.externalStopHandle, 0) == WAIT_OBJECT_0)
    {
        return 0;
    }

    if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS))
    {
        std::fprintf(stderr, "could not lower observer process priority: %lu\n", GetLastError());
    }

    HANDLE localStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (localStopEvent == nullptr)
    {
        const auto error = GetLastError();
        EmitProtocolError(nullptr, error, "cannot create local stop event");
        if (options.hasParentHandle && options.parentHandle != nullptr &&
            options.parentHandle != INVALID_HANDLE_VALUE && options.parentHandle != GetCurrentProcess())
        {
            CloseHandle(options.parentHandle);
        }
        return 3;
    }

    PMTraceConsumer consumer;
    consumer.mTrackDisplay = true;
    consumer.mTrackGPU = false;
    consumer.mTrackGPUVideo = false;
    consumer.mTrackInput = false;
    consumer.mTrackFrameType = false;
    consumer.mFilteredProcessIds = true;
    consumer.AddTrackedProcessForFiltering(options.targetPid);

    if (options.sessionName.empty())
    {
        LARGE_INTEGER nonce{};
        QueryPerformanceCounter(&nonce);
        wchar_t generatedName[96]{};
        swprintf_s(generatedName, L"VPDisplay_%u_%llX",
            options.targetPid, static_cast<unsigned long long>(nonce.QuadPart));
        options.sessionName = generatedName;
    }
    if (options.sessionName.size() >= MAX_PATH)
    {
        EmitProtocolError(localStopEvent, ERROR_INVALID_NAME, "session name is too long");
        CloseHandle(localStopEvent);
        return 2;
    }

    PMTraceSession session;
    session.mPMConsumer = &consumer;
    const auto startStatus = session.Start(nullptr, options.sessionName.c_str());
    if (startStatus != ERROR_SUCCESS)
    {
        EmitProtocolError(localStopEvent, startStatus, "PresentMon ETW session start failed");
        CloseHandle(localStopEvent);
        if (options.hasParentHandle && options.parentHandle != nullptr &&
            options.parentHandle != INVALID_HANDLE_VALUE && options.parentHandle != GetCurrentProcess())
        {
            CloseHandle(options.parentHandle);
        }
        return 4;
    }

    if (consumer.mDeferralTimeLimit == 0)
    {
        consumer.mDeferralTimeLimit = session.mTimestampFrequency.QuadPart * 2;
    }

    std::atomic<ULONG> processTraceStatus{ ERROR_SUCCESS };
    TRACEHANDLE workerTraceHandle = session.mTraceHandle;
    std::thread consumerThread([workerTraceHandle, &processTraceStatus, localStopEvent]() mutable
    {
        const auto status = ProcessTrace(&workerTraceHandle, 1, nullptr, nullptr);
        processTraceStatus.store(status, std::memory_order_relaxed);
        if (status != ERROR_SUCCESS && status != ERROR_CANCELLED)
        {
            SetEvent(localStopEvent);
        }
    });

    std::thread inputThread;
    const auto input = GetStdHandle(STD_INPUT_HANDLE);
    if (!options.hasExternalStopHandle &&
        input != nullptr && input != INVALID_HANDLE_VALUE &&
        GetFileType(input) == FILE_TYPE_PIPE)
    {
        inputThread = std::thread(WatchInputPipe, input, localStopEvent);
    }

    EmitLine(localStopEvent, "HELLO 1 %llu\n",
        static_cast<unsigned long long>(session.mTimestampFrequency.QuadPart));

    HANDLE waitHandles[3]{};
    DWORD waitHandleCount = 0;
    waitHandles[waitHandleCount++] = localStopEvent;
    if (options.hasExternalStopHandle)
        waitHandles[waitHandleCount++] = options.externalStopHandle;
    if (options.hasParentHandle)
        waitHandles[waitHandleCount++] = options.parentHandle;

    auto stopAt = std::chrono::steady_clock::time_point::max();
    if (options.seconds != 0)
        stopAt = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
    auto nextStatusAt = std::chrono::steady_clock::now();
    ULONG queryStatus = ERROR_SUCCESS;
    ULONG eventsLost = 0;
    ULONG buffersLost = 0;
    std::vector<std::shared_ptr<PresentEvent>> events;
    DWORD loopError = ERROR_SUCCESS;

    for (;;)
    {
        EmitEvents(localStopEvent, consumer, options.targetPid, events);
        const auto now = std::chrono::steady_clock::now();
        if (now >= stopAt) break;

        if (now >= nextStatusAt)
        {
            queryStatus = QueryLossCounters(session, eventsLost, buffersLost);
            const auto overflow = consumer.mCompletedPresentRingOverflowCount.load(
                std::memory_order_relaxed);
            EmitStatus(localStopEvent, queryStatus,
                queryStatus == ERROR_SUCCESS ? static_cast<int64_t>(eventsLost) : int64_t(-1),
                queryStatus == ERROR_SUCCESS ? static_cast<int64_t>(buffersLost) : int64_t(-1),
                overflow);
            nextStatusAt = now + std::chrono::seconds(1);
        }

        DWORD waitMilliseconds = 20;
        if (stopAt != std::chrono::steady_clock::time_point::max())
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                stopAt - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) break;
            waitMilliseconds = static_cast<DWORD>((std::min<uint64_t>)(
                static_cast<uint64_t>(remaining), waitMilliseconds));
        }
        const auto waitResult = WaitForMultipleObjects(
            waitHandleCount, waitHandles, FALSE, waitMilliseconds);
        if (waitResult >= WAIT_OBJECT_0 &&
            waitResult < WAIT_OBJECT_0 + waitHandleCount)
        {
            break;
        }
        if (waitResult == WAIT_FAILED)
        {
            loopError = GetLastError();
            std::fprintf(stderr, "shutdown wait failed: %lu\n", loopError);
            break;
        }
        if (waitResult != WAIT_TIMEOUT)
        {
            loopError = ERROR_INVALID_FUNCTION;
            std::fprintf(stderr, "unexpected shutdown wait result: %lu\n", waitResult);
            break;
        }
    }

    // Stop only the session handle this helper successfully created.
    session.Stop();
    if (consumerThread.joinable()) consumerThread.join();
    EmitEvents(localStopEvent, consumer, options.targetPid, events);

    const auto finalOverflow = consumer.mCompletedPresentRingOverflowCount.load(
        std::memory_order_relaxed);
    EmitStatus(localStopEvent, session.mLastQueryStatus,
        session.mLastQueryStatus == ERROR_SUCCESS ? static_cast<int64_t>(session.mNumEventsLost) : int64_t(-1),
        session.mLastQueryStatus == ERROR_SUCCESS ? static_cast<int64_t>(session.mNumBuffersLost) : int64_t(-1),
        finalOverflow);

    if (inputThread.joinable())
    {
        CancelSynchronousIo(static_cast<HANDLE>(inputThread.native_handle()));
        inputThread.join();
    }
    CloseHandle(localStopEvent);
    if (options.hasParentHandle && options.parentHandle != nullptr &&
        options.parentHandle != INVALID_HANDLE_VALUE && options.parentHandle != GetCurrentProcess())
    {
        CloseHandle(options.parentHandle);
    }

    const auto traceStatus = processTraceStatus.load(std::memory_order_relaxed);
    if (session.mLastStopStatus != ERROR_SUCCESS)
    {
        std::fprintf(stderr, "ETW session stop failed: %lu\n", session.mLastStopStatus);
        return 6;
    }
    if (loopError != ERROR_SUCCESS) return 7;
    if (traceStatus != ERROR_SUCCESS && traceStatus != ERROR_CANCELLED) return 5;
    return 0;
}
