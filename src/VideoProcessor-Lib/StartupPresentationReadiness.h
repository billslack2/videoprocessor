#pragma once

#include <cstdint>
#include <type_traits>

// Optional side-channel only: no IVideoRenderer vtable or existing plugin ABI
// layout changes. All cross-DLL values have a fixed width and own no storage.
namespace StartupPresentation
{
    constexpr uint32_t SnapshotVersion = 1;
    constexpr uint64_t MaximumGeometryWaitMs = 3000;
    constexpr uint64_t StableGeometryMs = 250;
    constexpr uint32_t StableGeometrySources = 3;

    enum class QueryStatus : uint32_t { Unsupported = 0, Available = 1, Busy = 2 };
    enum Flag : uint32_t { Required = 1, ActualSubmission = 2, Ready = 4 };
    enum class Reason : uint32_t
    {
        AwaitingSubmission, AwaitingGeometry, AwaitingOwner,
        ReadyNative, ReadyFullRaster, NotRequired
    };
    struct Rect
    {
        int32_t left = 0, top = 0, right = 0, bottom = 0;
    };
    struct SnapshotV1
    {
        uint32_t structureSize = sizeof(SnapshotV1);
        uint32_t version = SnapshotVersion;
        uint32_t flags = Required;
        Reason reason = Reason::AwaitingSubmission;
        uint32_t rendererGeneration = 0;
        int32_t rasterWidth = 0, rasterHeight = 0;
        uint64_t sourceGeneration = 0, sourceFormatKey = 0;
        uint64_t viewportEpoch = 0, outputContract = 0;
        uint64_t selectedSequence = 0, submissionSerial = 0;
        uint64_t firstSubmittedTick = 0, submittedTick = 0;
        Rect nativeBounds, displayedBounds;
    };
    static_assert(std::is_standard_layout<SnapshotV1>::value &&
        std::is_trivially_copyable<SnapshotV1>::value, "Readiness export requires a fixed-layout copy");

    struct Context
    {
        uint32_t rendererGeneration = 0;
        int32_t rasterWidth = 0, rasterHeight = 0;
        uint64_t sourceGeneration = 0, sourceFormatKey = 0;
        uint64_t viewportEpoch = 0, outputContract = 0;
    };
    inline bool Supported(const SnapshotV1& snapshot)
    {
        return snapshot.version == SnapshotVersion && snapshot.structureSize >= sizeof(SnapshotV1);
    }
    inline bool SameContext(const SnapshotV1& snapshot, const Context& context)
    {
        return snapshot.rendererGeneration == context.rendererGeneration &&
            snapshot.rasterWidth == context.rasterWidth && snapshot.rasterHeight == context.rasterHeight &&
            snapshot.sourceGeneration == context.sourceGeneration && snapshot.sourceFormatKey == context.sourceFormatKey &&
            snapshot.viewportEpoch == context.viewportEpoch && snapshot.outputContract == context.outputContract;
    }
    inline bool Valid(const Rect& bounds, int width, int height)
    {
        return width > 0 && height > 0 && bounds.left >= 0 && bounds.top >= 0 &&
            bounds.right > bounds.left && bounds.bottom > bounds.top &&
            bounds.right <= width && bounds.bottom <= height;
    }
    inline SnapshotV1 Reset(const Context& context, bool required = true,
        const SnapshotV1& previous = SnapshotV1{})
    {
        SnapshotV1 result;
        result.flags = required ? Required : 0;
        result.reason = required ? Reason::AwaitingSubmission : Reason::NotRequired;
        result.rendererGeneration = context.rendererGeneration;
        result.rasterWidth = context.rasterWidth; result.rasterHeight = context.rasterHeight;
        result.sourceGeneration = context.sourceGeneration; result.sourceFormatKey = context.sourceFormatKey;
        result.viewportEpoch = context.viewportEpoch; result.outputContract = context.outputContract;
        if (previous.rendererGeneration == context.rendererGeneration) {
            result.firstSubmittedTick = previous.firstSubmittedTick;
            result.submissionSerial = previous.submissionSerial;
        }
        return result;
    }
    struct SubmissionInput
    {
        Context context;
        uint64_t selectedSequence = 0, tick = 0;
        Rect nativeBounds, displayedBounds;
        bool required = true, rendered = false, submitted = false;
        bool contextCurrent = false, nativeAuthorityConfirmed = false;
        bool fullRasterAuthorityConfirmed = false, poseQualified = false;
    };
    inline SnapshotV1 AfterSubmission(const SnapshotV1& previous, const SubmissionInput& input)
    {
        auto result = Reset(input.context, input.required, previous);
        if (!input.contextCurrent || !input.rendered || !input.submitted ||
            !input.context.rendererGeneration || !input.context.sourceGeneration || !input.selectedSequence ||
            !Valid(input.displayedBounds, input.context.rasterWidth, input.context.rasterHeight))
            return SameContext(previous, input.context) && input.contextCurrent ? previous : result;
        result.flags |= ActualSubmission;
        result.selectedSequence = input.selectedSequence;
        result.submissionSerial = previous.rendererGeneration == input.context.rendererGeneration
            ? previous.submissionSerial + 1 : 1;
        result.submittedTick = input.tick;
        if (!result.firstSubmittedTick) result.firstSubmittedTick = input.tick;
        result.nativeBounds = input.nativeBounds; result.displayedBounds = input.displayedBounds;
        if (!input.required) {
            result.flags |= Ready; result.reason = Reason::NotRequired;
        } else if (!input.poseQualified) {
            result.reason = Reason::AwaitingOwner;
        } else if (input.nativeAuthorityConfirmed &&
            Valid(input.nativeBounds, input.context.rasterWidth, input.context.rasterHeight)) {
            result.flags |= Ready; result.reason = Reason::ReadyNative;
        } else if (input.fullRasterAuthorityConfirmed && input.nativeBounds.left == 0 &&
            input.nativeBounds.top == 0 && input.nativeBounds.right == input.context.rasterWidth &&
            input.nativeBounds.bottom == input.context.rasterHeight) {
            result.flags |= Ready; result.reason = Reason::ReadyFullRaster;
        } else result.reason = Reason::AwaitingGeometry;
        return result;
    }

    enum class RevealReason : uint32_t
    {
        ExistingPrerequisites, StaleRenderer, QueryBusy, AwaitingSubmission,
        AwaitingGeometry, AwaitingOwner, Ready, NotRequired, LegacyRenderer,
        GeometryTimeout, AlreadyReleased
    };
    struct RevealInput
    {
        uint32_t rendererGeneration = 0;
        uint64_t now = 0;
        bool existingFrameReady = false;
        QueryStatus queryStatus = QueryStatus::Unsupported;
        SnapshotV1 snapshot;
    };
    struct RevealDecision
    {
        bool release = false, timedOut = false;
        RevealReason reason = RevealReason::ExistingPrerequisites;
        uint64_t elapsedMs = 0;
    };
    class RevealGate
    {
    public:
        void BeginSession() { *this = RevealGate{}; }
        void Arm(uint32_t generation)
        {
            // Renderer/profile/fullscreen retries belong to the same startup
            // session. They cannot restart its deadline or rearm after reveal.
            if (generation == m_generation && (m_armed || m_released)) return;
            m_generation = generation;
            m_stableSamples = 0;
            if (!m_released) m_armed = true;
        }
        void MarkRevealed() { m_armed = false; m_released = true; }
        RevealDecision Evaluate(const RevealInput& input)
        {
            RevealDecision result;
            if (!input.existingFrameReady) return result;
            if (!m_armed) {
                result.release = true; result.reason = RevealReason::AlreadyReleased; return result;
            }
            if (input.rendererGeneration != m_generation) {
                result.reason = RevealReason::StaleRenderer; return result;
            }
            if (input.queryStatus == QueryStatus::Unsupported) {
                result.release = true; result.reason = RevealReason::LegacyRenderer; return result;
            }
            if (input.queryStatus == QueryStatus::Busy) {
                result.reason = RevealReason::QueryBusy; return result;
            }
            const auto& snapshot = input.snapshot;
            if (!Supported(snapshot) || snapshot.rendererGeneration != m_generation) {
                result.reason = RevealReason::StaleRenderer; return result;
            }
            if (!(snapshot.flags & ActualSubmission) || !snapshot.sourceGeneration ||
                !snapshot.selectedSequence || !snapshot.submissionSerial ||
                !Valid(snapshot.displayedBounds, snapshot.rasterWidth, snapshot.rasterHeight)) {
                result.reason = RevealReason::AwaitingSubmission; return result;
            }
            if (!m_firstSubmission) {
                const auto first = snapshot.firstSubmittedTick;
                m_firstSubmission = first && first <= input.now ? first : input.now;
            }
            result.elapsedMs = input.now >= m_firstSubmission ? input.now - m_firstSubmission : 0;
            if (!(snapshot.flags & Required)) {
                result.release = true; result.reason = RevealReason::NotRequired;
            } else if ((snapshot.flags & Ready) &&
                (snapshot.reason == Reason::ReadyNative || snapshot.reason == Reason::ReadyFullRaster) &&
                Valid(snapshot.nativeBounds, snapshot.rasterWidth, snapshot.rasterHeight)) {
                if (ObserveStableGeometry(snapshot)) {
                    result.release = true; result.reason = RevealReason::Ready;
                } else result.reason = RevealReason::AwaitingGeometry;
            } else {
                m_stableSamples = 0;
                result.reason = snapshot.reason == Reason::AwaitingOwner
                    ? RevealReason::AwaitingOwner : RevealReason::AwaitingGeometry;
            }
            if (!result.release && result.elapsedMs >= MaximumGeometryWaitMs) {
                result.release = true; result.timedOut = true; result.reason = RevealReason::GeometryTimeout;
            }
            return result;
        }
    private:
        static bool SameRect(const Rect& a, const Rect& b)
        {
            return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        }
        bool ObserveStableGeometry(const SnapshotV1& snapshot)
        {
            const Context context{snapshot.rendererGeneration,snapshot.rasterWidth,snapshot.rasterHeight,
                snapshot.sourceGeneration,snapshot.sourceFormatKey,snapshot.viewportEpoch,snapshot.outputContract};
            const bool same = m_stableSamples && SameContext(m_stableSnapshot,context) &&
                m_stableSnapshot.reason==snapshot.reason &&
                SameRect(m_stableSnapshot.nativeBounds,snapshot.nativeBounds) &&
                SameRect(m_stableSnapshot.displayedBounds,snapshot.displayedBounds) &&
                snapshot.selectedSequence>=m_stableSnapshot.selectedSequence &&
                snapshot.submittedTick>=m_stableSnapshot.submittedTick;
            if (!same) {
                m_stableSamples=1;
                m_stableFirstTick=snapshot.submittedTick;
            } else if (snapshot.selectedSequence>m_stableSnapshot.selectedSequence) {
                if (m_stableSamples<StableGeometrySources) ++m_stableSamples;
            }
            // Querying the same snapshot or resubmitting a cadence repeat cannot
            // advance the source-time dwell. Only distinct selected sources count.
            if (!same || snapshot.selectedSequence>m_stableSnapshot.selectedSequence)
                m_stableSnapshot=snapshot;
            return m_stableSamples>=StableGeometrySources &&
                m_stableSnapshot.submittedTick-m_stableFirstTick>=StableGeometryMs;
        }
        SnapshotV1 m_stableSnapshot;
        uint64_t m_stableFirstTick=0;
        uint32_t m_stableSamples=0;
        uint32_t m_generation = 0;
        uint64_t m_firstSubmission = 0;
        bool m_armed = false, m_released = false;
    };
}
