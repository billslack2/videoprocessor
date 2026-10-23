#pragma once

#include <vprenderer/Bt2020SignalWorker.h>
#include <DebugLog.h>
#include <nvapi.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class NvidiaBt2020Backend
{
public:
    struct Frame { NV_INFOFRAME_DATA data{}; NvU32 displayId = 0; };
    ~NvidiaBt2020Backend() { if (initialized) NvAPI_Unload(); }
    bool Read(const std::string& display, Frame& frame)
    {
        if (!initialized)
        {
            const auto status = NvAPI_Initialize();
            if (!Check(display, "initialize", status)) return false;
            initialized = true;
        }
        if (!Check(display, "resolve target",
            NvAPI_DISP_GetDisplayIdByDisplayName(display.c_str(), &frame.displayId))) return false;
        frame.data = {};
        frame.data.version = NV_INFOFRAME_DATA_VER;
        frame.data.size = sizeof(frame.data);
        frame.data.cmd = NV_INFOFRAME_CMD_GET;
        frame.data.type = 2; // AVI
        return Check(display, "read AVI", NvAPI_Disp_InfoFrameControl(frame.displayId, &frame.data));
    }
    bool Write(const std::string& display, Frame& frame)
    {
        NvU32 current = 0;
        if (!Check(display, "resolve before SET",
            NvAPI_DISP_GetDisplayIdByDisplayName(display.c_str(), &current))) return false;
        if (current != frame.displayId)
        {
            DebugLog::Log("NVIDIA BT.2020 report: target changed during attempt on %s; retry pending", display.c_str());
            return false;
        }
        frame.data.cmd = NV_INFOFRAME_CMD_SET;
        return Check(display, "SET AVI", NvAPI_Disp_InfoFrameControl(current, &frame.data));
    }
    static Bt2020Signal::Color GetColor(const Frame& frame)
    { return { frame.data.infoframe.video.colorimetry, frame.data.infoframe.video.extendedColorimetry }; }
    static void SetColor(Frame& frame, Bt2020Signal::Color color)
    {
        frame.data.infoframe.video.colorimetry = color.standard;
        frame.data.infoframe.video.extendedColorimetry = color.extended;
    }
    static void Report(const std::string& display, bool enabled, unsigned attempt, bool accepted, bool verified)
    {
        DebugLog::Log("NVIDIA BT.2020 report: target=%s desired=%s attempt=%u SET=%s readback=%s wire=unverified",
            display.c_str(), enabled ? "BT2020" : "disengaged", attempt,
            accepted ? "accepted" : "failed", verified ? "matched" : "unverified");
    }
private:
    static bool Check(const std::string& display, const char* operation, NvAPI_Status status)
    {
        if (status == NVAPI_OK) return true;
        NvAPI_ShortString message{};
        NvAPI_GetErrorMessage(status, message);
        DebugLog::Log("NVIDIA BT.2020 report: %s failed on %s: %s (%d)", operation, display.c_str(), message, status);
        return false;
    }
    bool initialized = false;
};

// The service owns the backend and every NVAPI call on one worker, including
// final restoration and unload. Render/UI queries read a cached publication.
class NvidiaBt2020Reporter
{
public:
    using Diagnostics = Bt2020Signal::WorkerService<NvidiaBt2020Backend>::Snapshot;
    ~NvidiaBt2020Reporter()
    {
        if (!service.Stop())
            DebugLog::Log("NVIDIA BT.2020 report: final disengagement failed; external state unverified");
    }
    void Request(const std::string& display, bool enabled, bool restart)
    { service.Request(display, enabled, restart); }
    bool IsActive() const { return service.IsActive(); }
    bool IsReadbackVerified() const { return service.IsReadbackVerified(); }
    bool Failed() const { return service.Failed(); }
    std::string Status() const { return service.Status(); }
    Diagnostics Snapshot() const { return service.ReadSnapshot(); }
    bool Restore() { return service.Restore(); }
    void AbandonPendingRestoreForShutdown() { service.AbandonPendingRestoreForShutdown(); }
private:
    Bt2020Signal::WorkerService<NvidiaBt2020Backend> service;
};
