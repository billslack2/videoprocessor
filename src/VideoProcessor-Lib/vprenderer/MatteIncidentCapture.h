#pragma once

#include <AnalysisDiagnosticTrace.h>
#include <DebugLog.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <windows.h>

// Opt-in diagnostics. The render path only tries a mutex and retains an
// immutable owner. All directory creation, pixel and sidecar I/O run on worker.
class MatteIncidentCapture
{
public:
    enum class Decision : uint32_t { None, AcquiredMargin, CurrentMargin, HeldMargin };
    enum class Result : uint32_t { None, Queued, IdentityMismatch, Busy, InvalidSource, UnknownSelectedState };
    struct Job
    {
        AnalysisDiagnosticFramePacketV1 frame{};
        std::shared_ptr<const void> owner;
        int32_t submitted[4]{};
        uint64_t evidenceGeneration = 0;
        uint64_t evidenceSequence = 0;
        uint64_t evidenceDomain = 0;
        Decision decision = Decision::None;
        bool acquisition = false;
        bool marginOnset = false;
    };

    explicit MatteIncidentCapture(const std::wstring& directory = {}) noexcept
    {
        try
        {
            if (directory.empty() || !std::filesystem::path(directory).is_absolute())
                return;
            m_directory = directory;
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            m_session = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
            std::error_code error;
            const bool disarmed = std::filesystem::exists(
                std::filesystem::path(m_directory) / L"DISARM", error);
            m_armed.store(!disarmed && !error, std::memory_order_release);
            m_worker = std::thread(&MatteIncidentCapture::Run, this);
        }
        catch (...)
        {
            m_armed.store(false, std::memory_order_release);
            m_directory.clear();
        }
    }
    ~MatteIncidentCapture()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_changed.notify_one();
        if (m_worker.joinable())
            m_worker.join();
    }
    MatteIncidentCapture(const MatteIncidentCapture&) = delete;
    MatteIncidentCapture& operator=(const MatteIncidentCapture&) = delete;

    static std::wstring EnvironmentPath() noexcept
    {
        try
        {
            wchar_t* value = nullptr;
            size_t size = 0;
            _wdupenv_s(&value, &size, L"VP_MATTE_CAPTURE_DIR");
            std::wstring result = value ? value : L"";
            free(value);
            return result;
        }
        catch (...) { return {}; }
    }
    bool Configured() const noexcept { return !m_directory.empty(); }
    bool Enabled() const noexcept
    {
        return m_armed.load(std::memory_order_acquire);
    }

    // Caller supplies successfully submitted geometry. The identity check must
    // cover the selected packet, current source and evidence, not a cached cue.
    Result Observe(const Job& input, bool currentIdentity, bool scope, bool margin) noexcept
    {
        if (!Enabled())
        {
            // While disarmed, track submitted state without retaining pixels.
            // Arming inside an existing margin must not relabel it as an onset.
            if (Configured() && currentIdentity)
            {
                m_wasScope.store(scope, std::memory_order_relaxed);
                m_wasMargin.store(scope && margin, std::memory_order_relaxed);
            }
            return Result::None;
        }
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            const bool possibleOnset = (scope && !m_wasScope.load(std::memory_order_relaxed)) ||
                (scope && margin && !m_wasMargin.load(std::memory_order_relaxed));
            // Losing an onset is reported; never relabel a later frame as it.
            m_wasScope.store(currentIdentity ? scope : true, std::memory_order_relaxed);
            m_wasMargin.store(currentIdentity ? (scope && margin) : true, std::memory_order_relaxed);
            if (!currentIdentity)
                return m_unknownSelected.exchange(true, std::memory_order_relaxed)
                    ? Result::None : Result::UnknownSelectedState;
            if (possibleOnset)
                m_dropped.fetch_add(1, std::memory_order_relaxed);
            return possibleOnset ? Result::Busy : Result::None;
        }
        const bool acquisition = scope && !m_wasScope.load(std::memory_order_relaxed) && m_scopeCount < 4;
        const bool marginOnset = scope && margin && !m_wasMargin.load(std::memory_order_relaxed) && m_marginCount < 4;
        if (!currentIdentity)
        {
            // A decision was submitted but its source owner is not exact.
            // Report the missed onset once, and do not substitute a later frame.
            // Selected output is unknown: quarantine both transition labels
            // until a later current non-scope or non-margin frame clears them.
            m_wasScope.store(true, std::memory_order_relaxed);
            m_wasMargin.store(true, std::memory_order_relaxed);
            const bool firstUnknown = !m_unknownSelected.exchange(true, std::memory_order_relaxed);
            if (acquisition || marginOnset)
            {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return Result::IdentityMismatch;
            }
            return firstUnknown ? Result::UnknownSelectedState : Result::None;
        }
        m_unknownSelected.store(false, std::memory_order_relaxed);
        m_wasScope.store(scope, std::memory_order_relaxed);
        m_wasMargin.store(scope && margin, std::memory_order_relaxed);
        if (!acquisition && !marginOnset)
            return Result::None;
        if (!input.owner || !input.frame.data || input.frame.format != 0 ||
            !input.frame.dataBytes || input.frame.dataBytes > 128ull * 1024 * 1024)
        {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return Result::InvalidSource;
        }
        if (m_outstanding >= m_queue.size())
        {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return Result::Busy;
        }
        Job job = input;
        job.acquisition = acquisition;
        job.marginOnset = marginOnset;
        m_queue[(m_head + m_count) % m_queue.size()] = std::move(job);
        ++m_count;
        ++m_outstanding;
        if (m_outstanding > m_peakOutstanding.load(std::memory_order_relaxed))
            m_peakOutstanding.store(m_outstanding, std::memory_order_relaxed);
        if (acquisition)
            ++m_scopeCount;
        if (marginOnset)
            ++m_marginCount;
        lock.unlock();
        m_changed.notify_one();
        return Result::Queued;
    }
    uint64_t Dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }
    size_t PeakOutstanding() const noexcept { return m_peakOutstanding.load(std::memory_order_relaxed); }
    static const char* Reason(Decision decision) noexcept { return DecisionName(decision); }
    static const char* ResultName(Result result) noexcept
    {
        switch (result)
        {
        case Result::Queued: return "queued";
        case Result::IdentityMismatch: return "identity-mismatch";
        case Result::Busy: return "queue-or-lock-busy";
        case Result::InvalidSource: return "invalid-source";
        case Result::UnknownSelectedState: return "unknown-selected-state";
        default: return "none";
        }
    }

private:
    static const char* DecisionName(Decision decision) noexcept
    {
        switch (decision)
        {
        case Decision::AcquiredMargin: return "acquired scope with verified raised matte margin";
        case Decision::CurrentMargin: return "current detail inside verified raised matte margin";
        case Decision::HeldMargin: return "retaining raised matte margin around current crop";
        default: return "native scope acquisition";
        }
    }
    static bool WriteNewFile(const std::wstring& path, const void* data, DWORD bytes) noexcept
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        const bool ok = WriteFile(file, data, bytes, &written, nullptr) && written == bytes;
        CloseHandle(file);
        if (!ok)
            DeleteFileW(path.c_str()); // Only this worker's CREATE_NEW file.
        return ok;
    }
    bool WriteJob(const Job& job)
    {
        std::error_code error;
        std::filesystem::create_directories(m_directory, error);
        if (error)
            return false;
        const auto& f = job.frame;
        const auto stem = std::wstring(L"matte-") + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(m_session) + L"-" + std::to_wstring(reinterpret_cast<uintptr_t>(this)) +
            L"-g" + std::to_wstring(f.transportGeneration) + L"-s" +
            std::to_wstring(f.acceptedSequence) + L"-" + std::to_wstring(++m_written);
        const auto root = std::filesystem::path(m_directory) / stem;
        struct RemoveUnpublished
        {
            std::wstring path;
            bool owns = false;
            bool published = false;
            ~RemoveUnpublished() { if (owns && !published) DeleteFileW(path.c_str()); }
        };
        RemoveUnpublished binaryGuard{root.wstring() + L".bin"};
        if (!WriteNewFile(binaryGuard.path, f.data, static_cast<DWORD>(f.dataBytes)))
            return false;
        binaryGuard.owns = true;
        std::ostringstream meta;
        meta << f.width << ' ' << f.height << ' ' << f.stride0 << ' ' << f.stride1 << ' '
            << f.format << ' ' << f.encoding << ' ' << f.colorspace << ' ' << f.top << ' '
            << f.bottom << ' ' << f.scriptHint << ' ' << f.analysisSampleRange << '\n';
        meta << "identity_v1 " << f.transportGeneration << ' ' << f.acceptedSequence << ' '
            << f.sourceFrameNumber << ' ' << f.captureTimestamp << ' ' << f.sourceFormatGeneration << ' '
            << f.viewportGeneration << ' ' << f.rendererGeneration << ' ' << f.analysisDomainKey << ' '
            << f.policyGeneration << ' ' << f.continuityGeneration << ' ' << f.nearBarDistance << ' '
            << f.optimizationMode << ' ' << f.frameDuration100ns << ' ' << f.discontinuity << ' '
            << f.cadenceRepeat << ' ' << f.rgbBitAlignment << '\n';
        meta << "submitted " << job.submitted[0] << ' ' << job.submitted[1] << ' '
            << job.submitted[2] << ' ' << job.submitted[3] << " acquisition " << job.acquisition
            << " margin_onset " << job.marginOnset << " evidence_generation " << job.evidenceGeneration
            << " evidence_sequence " << job.evidenceSequence << " evidence_domain " << job.evidenceDomain
            << " decision=\"" << DecisionName(job.decision) << "\" dropped " << Dropped() << '\n';
        const auto contents = meta.str();
        const auto pending = root.wstring() + L".pending";
        const auto sidecar = root.wstring() + L".txt";
        RemoveUnpublished pendingGuard{pending};
        if (!WriteNewFile(pendingGuard.path, contents.data(), static_cast<DWORD>(contents.size())))
            return false;
        pendingGuard.owns = true;
        // The final sidecar is the publication marker. CREATE_NEW names and
        // MoveFileW guarantee no completed capture is ever overwritten.
        if (!MoveFileW(pending.c_str(), sidecar.c_str()))
            return false;
        pendingGuard.published = true;
        binaryGuard.published = true;
        return true;
    }
    void Run() noexcept
    {
        try
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_changed.wait_for(lock, std::chrono::milliseconds(250),
                        [this] { return m_stop || m_count; });
                    {
                        lock.unlock();
                        std::error_code error;
                        const bool disarmed = std::filesystem::exists(
                            std::filesystem::path(m_directory) / L"DISARM", error);
                        lock.lock();
                        const bool wasArmed = m_armed.exchange(!disarmed && !error, std::memory_order_acq_rel);
                        if (!disarmed && !error && !wasArmed)
                        {
                            m_scopeCount = 0;
                            m_marginCount = 0;
                        }
                    }
                    if (!m_count)
                    {
                        if (m_stop)
                            break;
                        continue;
                    }
                    job = std::move(m_queue[m_head]);
                    m_queue[m_head] = {};
                    m_head = (m_head + 1) % m_queue.size();
                    --m_count;
                }
                bool written = false;
                try { written = WriteJob(job); } catch (...) { written = false; }
                if (!written)
                    m_dropped.fetch_add(1, std::memory_order_relaxed);
                DebugLog::Log("Matte incident writer: generation=%llu sequence=%llu result=%s dropped=%llu",
                    job.frame.transportGeneration, job.frame.acceptedSequence,
                    written ? "written" : "write-failed", Dropped());
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    --m_outstanding;
                }
            }
        }
        catch (...)
        {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            m_armed.store(false, std::memory_order_release);
        }
    }
    std::wstring m_directory;
    std::atomic<bool> m_armed{false};
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::thread m_worker;
    bool m_stop = false;
    std::array<Job, 2> m_queue{};
    size_t m_head = 0;
    size_t m_count = 0;
    size_t m_outstanding = 0;
    unsigned m_scopeCount = 0;
    unsigned m_marginCount = 0;
    std::atomic<bool> m_wasScope{false};
    std::atomic<bool> m_wasMargin{false};
    std::atomic<bool> m_unknownSelected{false};
    uint64_t m_session = 0;
    uint64_t m_written = 0;
    std::atomic<uint64_t> m_dropped{0};
    std::atomic<size_t> m_peakOutstanding{0};
};
