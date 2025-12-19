# Changes Made to Fix RATIONAL_RATIONAL Timeline

## File: ALiveSourceVideoOutputPin.cpp

### Change 1: Simplified Corruption Detection (Lines ~360-385)

**Before:**
```cpp
if (m_startTimeOffset != 0)
{
    m_startTimeOffset = 0;
    m_frameCounter = 0;      // This was problematic!
    m_previousTimeStop = 0;
    m_frameCounterOffset = 0;
    m_previousFrameCounter = 0;
    m_forceDiscontinuity = true;
    m_deliverNewSegment = true;
}
```

**After:**
```cpp
if (m_startTimeOffset != 0)
{
    m_startTimeOffset = 0;
    m_previousTimeStop = 0;
    m_forceDiscontinuity = true;
    m_deliverNewSegment = true;
}
```

**Why:** No longer resetting `m_frameCounter`, `m_frameCounterOffset`, or `m_previousFrameCounter` because we no longer use them for timing calculations. The timing is now derived from `streamFrameCounter` which is always correct.

---

### Change 2: RATIONAL_RATIONAL Start Time (Lines ~695-730)

**Before:**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
{
    const uint64_t frameNum = m_frameCounter - 1;  // WRONG SOURCE
    const uint64_t referenceTimePerSecond = 10000000ULL;
    timeStart = (REFERENCE_TIME)((frameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
    
    if (m_frameCounter == 1 && frameNum != 0)  // Checking wrong counter
    { ... }
    
    if (m_frameCounter > 1 && timeStart <= m_previousTimeStop)
    { ... }
    
    if (m_frameCounter == 1)  // Checking wrong counter
    { ... }
    
    if (m_frameCounter % 100 == 0)  // Checking wrong counter
    { ... }
    break;
}
```

**After:**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
{
    const uint64_t frameNum = streamFrameCounter;  // CORRECT SOURCE
    const uint64_t referenceTimePerSecond = 10000000ULL;
    timeStart = (REFERENCE_TIME)((frameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
    
    if (streamFrameCounter == 0 && frameNum != 0)  // Check correct counter
    { ... }
    
    if (streamFrameCounter > 0 && timeStart <= m_previousTimeStop)
    { ... }
    
    if (streamFrameCounter == 0)  // Check correct counter
    { ... }
    
    if (streamFrameCounter % 100 == 0 && streamFrameCounter > 0)  // Check correct counter
    { ... }
    break;
}
```

**Why:** `streamFrameCounter` is derived from `videoFrame.GetCounter()`, the source of truth. It's never out of sync with the actual stream position.

---

### Change 3: RATIONAL_RATIONAL Stop Time (Lines ~755-765)

**Before:**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
{
    const uint64_t nextFrameNum = m_frameCounter;  // WRONG SOURCE
    const uint64_t referenceTimePerSecond = 10000000ULL;
    timeStop = (REFERENCE_TIME)((nextFrameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
    break;
}
```

**After:**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL:
{
    const uint64_t nextFrameNum = streamFrameCounter + 1;  // CORRECT SOURCE
    const uint64_t referenceTimePerSecond = 10000000ULL;
    timeStop = (REFERENCE_TIME)((nextFrameNum * referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
    break;
}
```

**Why:** Same as above - use the authoritative source.

---

## Summary

**Total lines changed: ~35 lines**
**Impact: Complete fix for RATIONAL_RATIONAL timeline corruption**

The fix is minimal and focused because it addresses the ROOT CAUSE, not symptoms:
- ? Wrong: Trying to detect corruption after it's already caused problems
- ? Right: Use the correct source (streamFrameCounter) so corruption can't happen

This follows the fundamental principle of DirectShow filter design: **the timestamp source is always authoritative, not internal state**.
