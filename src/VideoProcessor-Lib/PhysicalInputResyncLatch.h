#pragma once

#include <ACaptureDevice.h>

// Diagnostic reset nomination only. The UI supplies source/run-qualified card
// notifications in publication order; capture and rendering policy are unchanged.
class PhysicalInputResyncLatch
{
public:
    void Reset() noexcept
    {
        *this = PhysicalInputResyncLatch{};
    }

    // Starting a capture run establishes a new physical observation baseline.
    // It does not nominate a re-sync by itself.
    void BeginRun(const ACaptureDevice* source, CaptureRunToken run) noexcept
    {
        Reset();
        m_source = source;
        m_run = run;
    }

    bool Observe(const ACaptureDevice* source, CaptureRunToken run,
        uint64_t notificationSequence, const CaptureDeviceCardState& state) noexcept
    {
        if (!source || !run || source != m_source || run != m_run ||
            !notificationSequence || notificationSequence <= m_lastSequence)
            return false;
        m_lastSequence = notificationSequence;

        if (state.inputLocked == InputLocked::NO)
        {
            // An initial unlocked input is startup, not a recovered session.
            m_lostAfterLock = m_lostAfterLock || m_seenLocked;
            return false;
        }
        if (state.inputLocked != InputLocked::YES)
            return false;

        NativeContract current;
        const bool complete = current.Read(state);
        const bool recovered = m_seenLocked && m_lostAfterLock;
        const bool changed = !recovered && complete && m_haveContract &&
            !m_contract.Matches(current);
        m_seenLocked = true;
        m_lostAfterLock = false;

        // A relock may precede complete format status. Do not compare its later
        // completed contract with the pre-loss format and nominate a second reset.
        if (recovered)
            m_haveContract = false;
        if (complete)
        {
            // Missing field-order metadata cannot erase a known order for the
            // otherwise unchanged native signal. A new format/relock is separate.
            if (m_haveContract && current.interlaced &&
                current.fieldOrder == DisplayMode::FieldOrder::Unknown &&
                m_contract.Matches(current))
                current.fieldOrder = m_contract.fieldOrder;
            m_contract = current;
            m_haveContract = true;
        }
        return recovered || changed;
    }

private:
    struct NativeContract
    {
        uint32_t width = 0, height = 0, timeScale = 0, frameDuration = 0;
        bool interlaced = false;
        DisplayMode::FieldOrder fieldOrder = DisplayMode::FieldOrder::Unknown;
        ColorFormat encoding = ColorFormat::UNKNOWN;
        BitDepth bitDepth = BitDepth::UNKNOWN;

        bool Read(const CaptureDeviceCardState& state) noexcept
        {
            if (!state.inputDisplayMode || state.inputEncoding == ColorFormat::UNKNOWN ||
                state.inputBitDepth == BitDepth::UNKNOWN)
                return false;
            const auto& mode = *state.inputDisplayMode;
            width = mode.FrameWidth();
            height = mode.FrameHeight();
            timeScale = mode.TimeScale();
            frameDuration = mode.FrameDuration();
            interlaced = mode.IsInterlaced();
            fieldOrder = mode.FirstFieldOrder();
            encoding = state.inputEncoding;
            bitDepth = state.inputBitDepth;
            return width && height && timeScale && frameDuration;
        }

        bool Matches(const NativeContract& other) const noexcept
        {
            const bool fieldOrderKnown =
                fieldOrder != DisplayMode::FieldOrder::Unknown &&
                other.fieldOrder != DisplayMode::FieldOrder::Unknown;
            return width == other.width && height == other.height &&
                static_cast<uint64_t>(timeScale) * other.frameDuration ==
                    static_cast<uint64_t>(other.timeScale) * frameDuration &&
                interlaced == other.interlaced &&
                (!interlaced || !fieldOrderKnown || fieldOrder == other.fieldOrder) &&
                encoding == other.encoding && bitDepth == other.bitDepth;
        }
    };

    const ACaptureDevice* m_source = nullptr; // Identity only; never dereferenced.
    CaptureRunToken m_run = 0;
    uint64_t m_lastSequence = 0;
    bool m_seenLocked = false;
    bool m_lostAfterLock = false;
    bool m_haveContract = false;
    NativeContract m_contract;
};
