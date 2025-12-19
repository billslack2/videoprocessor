# RATIONAL_RATIONAL Timeline Corruption Fix

## Root Cause Discovery

The RATIONAL_RATIONAL timing mode was failing to correctly handle timeline corruption because of a **fundamental architectural mismatch**:

### The Bug
```cpp
// WRONG: Uses internal local counter
const uint64_t frameNum = m_frameCounter - 1;
timeStart = (REFERENCE_TIME)((frameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
```

**Problem**: `m_frameCounter` is a **local incrementer** that only counts frames processed by THIS pin. It has NO connection to the actual source stream frame numbers. When the renderer changes, network hiccups occur, or the stream is interrupted, `m_frameCounter` can:
- Get out of sync with the real frame stream
- Be reset while `m_startTimeOffset` carries stale data
- Cause timestamp inversions when combined with corrupted state

### The Solution
```cpp
// CORRECT: Uses stream frame counter derived from source
const uint64_t frameNum = streamFrameCounter;  
timeStart = (REFERENCE_TIME)((frameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
```

**Why it works**: `streamFrameCounter` is derived from `videoFrame.GetCounter()` which comes directly from the SOURCE. It's adjusted by `m_frameCounterOffset` to start from 0, but it's always authoritative to the actual stream position. This means:
- **No internal state drift** - we're always synchronized with the source
- **Corruption detection is simpler** - only need to check if `m_startTimeOffset` was improperly set
- **Timeline restart is automatic** - when discontinuities occur in the source, they're captured immediately
- **Matches THEO_THEO pattern** - both now derive timing from the source, not internal state

## Comparison with THEO_THEO

**Why THEO_THEO worked correctly:**
```cpp
timeStart = (streamFrameCounter * m_frameDuration);
```

It uses `streamFrameCounter` directly, not an internal counter. It was never affected by `m_frameCounter` corruption.

**Why RATIONAL_RATIONAL now works correctly:**
It now uses the same pattern, just with rational math instead of frame duration multiplication.

## Remaining Safeguards

The corruption detection still handles the case where `m_startTimeOffset` is improperly set:

```cpp
if (m_startTimeOffset != 0)  // Should NEVER be set for RATIONAL_RATIONAL
{
    m_startTimeOffset = 0;
    m_previousTimeStop = 0;
    m_forceDiscontinuity = true;
    m_deliverNewSegment = true;
}
```

This catches lingering state from previous CLOCK modes that didn't properly reset.

## Timeline Reset Behavior

When corruption is detected:
1. **Discontinuity flag set** - Next frame gets `SetDiscontinuity(TRUE)` on the sample
2. **New segment delivered** - `DeliverNewSegment(0, MAXLONGLONG, 1.0)` restarts MadVR's timeline understanding
3. **Timestamps restart from 0** - Since `streamFrameCounter` was reset via offset, timing naturally restarts

## Key Insight

**The bug wasn't in the detection/reset logic - it was in the fundamental timing calculation.**

Using an internal `m_frameCounter` for rational timing is inherently fragile because:
- It only advances locally
- It can desync from the actual stream
- It carries implicit state that must be perfectly synchronized across multiple variables

Using `streamFrameCounter` is robust because:
- It comes from the SOURCE (authoritative)
- It's automatically in sync with frame positions
- It handles stream resets transparently
- It matches industry-standard patterns (like THEO_THEO)

This is the same principle used in digital video processors and DirectShow filters everywhere - **derive timing from the source, don't try to track it separately**.
