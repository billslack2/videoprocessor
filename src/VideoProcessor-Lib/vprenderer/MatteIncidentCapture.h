#pragma once

#include <AnalysisDiagnosticTrace.h>
#include <DebugLog.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
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
    enum class Result : uint32_t { None, Queued, QueuedRenewal, IdentityMismatch, Busy, InvalidSource, UnknownSelectedState, RenewalContentionUnknown };
    struct Job
    {
        AnalysisDiagnosticFramePacketV1 frame{};
        std::shared_ptr<const void> owner;
        int32_t submitted[4]{};
        uint64_t evidenceGeneration = 0;
        uint64_t evidenceSequence = 0;
        uint64_t evidenceDomain = 0;
        Decision decision = Decision::None;
        // Witness coordinates are supplied only by the exact selected journal
        // snapshot. They never supply crop authority.
        bool freshWitness = false;
        int topX = -1, topY = -1, bottomX = -1, bottomY = -1;
        bool acquisition = false;
        bool marginOnset = false;
        bool renewal = false;
        bool priorUnclassifiedContention = false;
    };

    using WriterHook = void (*)(void*);
    // WriterHook is an optional worker-only test seam; production leaves it null.
    explicit MatteIncidentCapture(const std::wstring& directory = {},
        WriterHook beforeWrite = nullptr, void* hookContext = nullptr,
        WriterHook whileLocked = nullptr) noexcept
        : m_beforeWrite(beforeWrite), m_hookContext(hookContext), m_whileLocked(whileLocked)
    {
        try
        {
            if (!IsAbsolutePath(directory))
                return;
            m_directory = directory;
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            m_session = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
            bool disarmed = false;
            const bool armStateKnown = ReadDisarmState(disarmed);
            m_armed.store(armStateKnown && !disarmed, std::memory_order_release);
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
            const bool possibleRenewal = currentIdentity && scope && margin && input.freshWitness &&
                input.decision == Decision::CurrentMargin;
            if (!currentIdentity)
                m_renewalQuarantined.store(true, std::memory_order_relaxed);
            // Losing an onset is reported; never relabel a later frame as it.
            m_wasScope.store(currentIdentity ? scope : true, std::memory_order_relaxed);
            m_wasMargin.store(currentIdentity ? (scope && margin) : true, std::memory_order_relaxed);
            if (!currentIdentity)
                return m_unknownSelected.exchange(true, std::memory_order_relaxed)
                    ? Result::None : Result::UnknownSelectedState;
            if (possibleOnset)
            {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return Result::Busy;
            }
            if (possibleRenewal && !m_renewalContention.exchange(true, std::memory_order_relaxed))
            {
                m_everRenewalContention.store(true, std::memory_order_relaxed);
                return Result::RenewalContentionUnknown;
            }
            return Result::None;
        }
        m_renewalContention.store(false, std::memory_order_relaxed);
        const bool wasMargin = m_wasMargin.load(std::memory_order_relaxed);
        const bool acquisition = scope && !m_wasScope.load(std::memory_order_relaxed) && m_scopeCount < 4;
        const bool marginOnset = scope && margin && !wasMargin && m_marginCount < 4;
        if (!currentIdentity)
        {
            // A decision was submitted but its source owner is not exact.
            // Report the missed onset once, and do not substitute a later frame.
            // Selected output is unknown: quarantine both transition labels
            // until a later current non-scope or non-margin frame clears them.
            m_wasScope.store(true, std::memory_order_relaxed);
            m_wasMargin.store(true, std::memory_order_relaxed);
            m_renewalQuarantined.store(true, std::memory_order_relaxed);
            const bool firstUnknown = !m_unknownSelected.exchange(true, std::memory_order_relaxed);
            if (acquisition || marginOnset)
            {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return Result::IdentityMismatch;
            }
            return firstUnknown ? Result::UnknownSelectedState : Result::None;
        }
        m_unknownSelected.store(false, std::memory_order_relaxed);
        if (!scope || !margin)
            m_renewalQuarantined.store(false, std::memory_order_relaxed);
        const bool exactWitness = scope && margin && input.freshWitness &&
            (input.decision == Decision::CurrentMargin || input.decision == Decision::AcquiredMargin) &&
            input.frame.acceptedSequence != 0 &&
            input.evidenceGeneration == input.frame.transportGeneration &&
            input.evidenceSequence == input.frame.acceptedSequence &&
            input.evidenceDomain == input.frame.analysisDomainKey && input.evidenceDomain != 0 &&
            (ValidWitness(input.topX, input.topY, input.frame) ||
             ValidWitness(input.bottomX, input.bottomY, input.frame));
        const bool renewal = !acquisition && !marginOnset && wasMargin && exactWitness &&
            input.decision == Decision::CurrentMargin && m_witnessCount < m_witnesses.size() &&
            !m_renewalQuarantined.load(std::memory_order_relaxed) &&
            m_renewalCount < 3 && HasDistinctWitness(input);
        m_wasScope.store(scope, std::memory_order_relaxed);
        m_wasMargin.store(scope && margin, std::memory_order_relaxed);
        if (!acquisition && !marginOnset && !renewal)
            return Result::None;
        if (!input.owner || !input.frame.data || input.frame.format != 0 ||
            !input.frame.dataBytes || input.frame.dataBytes > 128ull * 1024 * 1024)
        {
            if (exactWitness)
                RememberWitnesses(input);
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return Result::InvalidSource;
        }
        if (m_outstanding >= m_queue.size())
        {
            // The exact source is lost. Remember this witness to avoid
            // relabeling a later nearby frame as the missed trigger.
            if (exactWitness)
                RememberWitnesses(input);
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return Result::Busy;
        }
        Job job = input;
        job.acquisition = acquisition;
        job.marginOnset = marginOnset;
        job.renewal = renewal;
        job.priorUnclassifiedContention = m_everRenewalContention.load(std::memory_order_relaxed);
        m_queue[(m_head + m_count) % m_queue.size()] = std::move(job);
        ++m_count;
        ++m_outstanding;
        if (m_outstanding > m_peakOutstanding.load(std::memory_order_relaxed))
            m_peakOutstanding.store(m_outstanding, std::memory_order_relaxed);
        if (acquisition)
            ++m_scopeCount;
        if (marginOnset)
            ++m_marginCount;
        if (renewal)
            ++m_renewalCount;
        if (exactWitness)
            RememberWitnesses(input);
        lock.unlock();
        m_changed.notify_one();
        return renewal ? Result::QueuedRenewal : Result::Queued;
    }
    uint64_t Dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }
    size_t PeakOutstanding() const noexcept { return m_peakOutstanding.load(std::memory_order_relaxed); }
    static const char* Reason(Decision decision) noexcept { return DecisionName(decision); }
    static const char* ResultName(Result result) noexcept
    {
        switch (result)
        {
        case Result::Queued: return "queued";
        case Result::QueuedRenewal: return "queued-renewal-observation";
        case Result::IdentityMismatch: return "identity-mismatch";
        case Result::Busy: return "queue-or-lock-busy";
        case Result::InvalidSource: return "invalid-source";
        case Result::UnknownSelectedState: return "unknown-selected-state";
        case Result::RenewalContentionUnknown: return "renewal-contended-unclassified";
        default: return "none";
        }
    }

private:
    struct Witness
    {
        int x = -1, y = -1;
        bool top = false;
        uint64_t generation = 0, domain = 0, viewport = 0, policy = 0, continuity = 0;
    };
    static bool ValidWitness(int x, int y, const AnalysisDiagnosticFramePacketV1& frame) noexcept
    {
        return x >= 0 && y >= 0 && static_cast<uint32_t>(x) < frame.width && static_cast<uint32_t>(y) < frame.height;
    }
    bool IsNearbyWitness(int x, int y, bool top, const AnalysisDiagnosticFramePacketV1& frame) const noexcept
    {
        for (size_t i = 0; i < m_witnessCount; ++i)
        {
            const auto& prior = m_witnesses[i];
            if (prior.top == top && prior.generation == frame.transportGeneration &&
                prior.domain == frame.analysisDomainKey && prior.viewport == frame.viewportGeneration &&
                prior.policy == frame.policyGeneration && prior.continuity == frame.continuityGeneration &&
                std::abs(prior.x - x) <= 16 && std::abs(prior.y - y) <= 16)
                return true;
        }
        return false;
    }
    bool HasDistinctWitness(const Job& job) const noexcept
    {
        return (ValidWitness(job.topX, job.topY, job.frame) &&
                !IsNearbyWitness(job.topX, job.topY, true, job.frame)) ||
            (ValidWitness(job.bottomX, job.bottomY, job.frame) &&
                !IsNearbyWitness(job.bottomX, job.bottomY, false, job.frame));
    }
    void RememberWitnesses(const Job& job) noexcept
    {
        for (int side = 0; side < 2; ++side)
        {
            const bool top = side == 0;
            const int x = top ? job.topX : job.bottomX;
            const int y = top ? job.topY : job.bottomY;
            if (!ValidWitness(x, y, job.frame) || IsNearbyWitness(x, y, top, job.frame) ||
                m_witnessCount >= m_witnesses.size())
                continue;
            auto& witness = m_witnesses[m_witnessCount++];
            witness.x = x; witness.y = y; witness.top = top;
            witness.generation = job.frame.transportGeneration;
            witness.domain = job.frame.analysisDomainKey;
            witness.viewport = job.frame.viewportGeneration;
            witness.policy = job.frame.policyGeneration;
            witness.continuity = job.frame.continuityGeneration;
        }
    }
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
    static bool IsAbsolutePath(const std::wstring& path) noexcept
    {
        const bool drive = path.size() >= 3 && path[1] == L':' &&
            (path[2] == L'\\' || path[2] == L'/');
        const bool unc = path.size() >= 5 && path[0] == L'\\' && path[1] == L'\\';
        return drive || unc;
    }
    static bool EnsureDirectory(const std::wstring& path) noexcept
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (CreateDirectoryW(path.c_str(), nullptr))
            return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        const DWORD existing = GetFileAttributesW(path.c_str());
        return existing != INVALID_FILE_ATTRIBUTES && (existing & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    static bool EnsureDirectoryTree(const std::wstring& path)
    {
        if (!IsAbsolutePath(path))
            return false;
        size_t rootEnd = 3;
        if (path[0] == L'\\' && path[1] == L'\\')
        {
            const size_t serverEnd = path.find_first_of(L"\\/", 2);
            if (serverEnd == std::wstring::npos)
                return false;
            const size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
            if (shareEnd == std::wstring::npos)
                return EnsureDirectory(path);
            rootEnd = shareEnd + 1;
        }
        for (size_t i = rootEnd; i < path.size(); ++i)
        {
            if ((path[i] == L'\\' || path[i] == L'/') && !EnsureDirectory(path.substr(0, i)))
                return false;
        }
        return EnsureDirectory(path);
    }
    bool ReadDisarmState(bool& disarmed) const
    {
        const std::wstring path = m_directory + L"\\DISARM";
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            disarmed = true;
            return true;
        }
        const DWORD error = GetLastError();
        disarmed = false;
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    bool WriteJob(const Job& job)
    {
        if (!EnsureDirectoryTree(m_directory))
            return false;
        const auto& f = job.frame;
        const auto stem = std::wstring(L"matte-") + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(m_session) + L"-" + std::to_wstring(reinterpret_cast<uintptr_t>(this)) +
            L"-g" + std::to_wstring(f.transportGeneration) + L"-s" +
            std::to_wstring(f.acceptedSequence) + L"-" + std::to_wstring(++m_written);
        const auto root = m_directory + L"\\" + stem;
        struct RemoveUnpublished
        {
            std::wstring path;
            bool owns = false;
            bool published = false;
            ~RemoveUnpublished() { if (owns && !published) DeleteFileW(path.c_str()); }
        };
        RemoveUnpublished binaryGuard{root + L".bin"};
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
            << " decision=\"" << DecisionName(job.decision) << "\" dropped " << Dropped()
            << " renewal " << job.renewal << " witness_top " << job.topX << ',' << job.topY
            << " witness_bottom " << job.bottomX << ',' << job.bottomY
            << " prior_unclassified_contention " << job.priorUnclassifiedContention
            << " renewal_semantics current_observation" << '\n';
        const auto contents = meta.str();
        const auto pending = root + L".pending";
        const auto sidecar = root + L".txt";
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
                        bool disarmed = false;
                        const bool armStateKnown = ReadDisarmState(disarmed);
                        lock.lock();
                        const bool wasArmed = m_armed.exchange(armStateKnown && !disarmed, std::memory_order_acq_rel);
                        if (armStateKnown && !disarmed && !wasArmed)
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
                    if (m_whileLocked)
                        m_whileLocked(m_hookContext);
                    job = std::move(m_queue[m_head]);
                    m_queue[m_head] = {};
                    m_head = (m_head + 1) % m_queue.size();
                    --m_count;
                }
                bool written = false;
                try
                {
                    if (m_beforeWrite)
                        m_beforeWrite(m_hookContext);
                    written = WriteJob(job);
                }
                catch (...) { written = false; }
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
    WriterHook m_beforeWrite = nullptr;
    void* m_hookContext = nullptr;
    WriterHook m_whileLocked = nullptr;
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
    unsigned m_renewalCount = 0;
    std::array<Witness, 22> m_witnesses{};
    size_t m_witnessCount = 0;
    std::atomic<bool> m_renewalQuarantined{false};
    std::atomic<bool> m_renewalContention{false};
    std::atomic<bool> m_everRenewalContention{false};
    std::atomic<bool> m_wasScope{false};
    std::atomic<bool> m_wasMargin{false};
    std::atomic<bool> m_unknownSelected{false};
    uint64_t m_session = 0;
    uint64_t m_written = 0;
    std::atomic<uint64_t> m_dropped{0};
    std::atomic<size_t> m_peakOutstanding{0};
};
