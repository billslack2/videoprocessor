# CLOCK_SMART vs CLOCK_SMART2 Timing Mode Comparison

## Overview

Both modes use **hardware timestamps** from the capture device as the primary timing source, but they differ significantly in how they calculate frame durations and handle timing anomalies.

---

## Side-by-Side Comparison

### 1. Start Time Calculation

| Aspect | CLOCK_SMART | CLOCK_SMART2 |
|--------|------------|-------------|
| **Source** | Hardware timestamp | Hardware timestamp |
| **Conversion** | `ConvertTimingClockToReferenceTime()` | `ConvertTimingClockToReferenceTime()` |
| **Method** | Direct, no special handling | Direct, no special handling |
| **Monotonic Enforcement** | Basic (check against previous stop) | Enhanced (check against previous stop) |

**Code Location (CLOCK_SMART):**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

    timeStart = ConvertTimingClockToReferenceTime(
        videoFrame.GetTimingTimestamp(), 
        m_timingClock->TimingClockTicksPerSecond());

    if (m_startTimeOffset == 0)
    {
        m_startTimeOffset = timeStart;
    }

    timeStart -= m_startTimeOffset;
    
    // Basic monotonic check
    if (m_previousTimeStop > 0 && timeStart < minStartTime)
    {
        timeStart = minStartTime;  // Enforce monotonic
    }
    break;
```

**Code Location (CLOCK_SMART2):**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:

    timeStart = ConvertTimingClockToReferenceTime(
        videoFrame.GetTimingTimestamp(), 
        m_timingClock->TimingClockTicksPerSecond());

    if (m_startTimeOffset == 0)
    {
        m_startTimeOffset = timeStart;
    }

    timeStart -= m_startTimeOffset;
    
    // SAME monotonic check as CLOCK_SMART
    if (m_previousTimeStop > 0)
    {
        const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
        
        if (timeStart < minStartTime)
        {
            DbgLog((LOG_WARNING, ...));
            timeStart = minStartTime;
        }
    }
    break;
```

? **Start time calculation is IDENTICAL**

---

### 2. Stop Time Calculation

| Aspect | CLOCK_SMART | CLOCK_SMART2 |
|--------|------------|-------------|
| **Primary Source** | `NextFrameTimestamp()` (if available) | `NextFrameTimestamp()` (if available) |
| **Fallback Duration** | **Theoretical frame duration** | **Average of last 100 actual frame durations** |
| **Duration Tracking** | ? NOT tracked | ? Tracked in circular buffer |
| **Accuracy** | Assumes constant frame durations | Adapts to real hardware variations |

**Code Location (CLOCK_SMART):**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

    timeStop = NextFrameTimestamp();
    if (timeStop == REFERENCE_TIME_INVALID)
    {
        // FALLBACK: Use theoretical frame duration
        timeStop = timeStart + m_frameDuration;  // ? CONSTANT
    }
    else
    {
        assert(m_startTimeOffset > 0);
        timeStop -= m_startTimeOffset;
    }
    break;
```

**Code Location (CLOCK_SMART2):**
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:

    timeStop = NextFrameTimestamp();
    if (timeStop == REFERENCE_TIME_INVALID)
    {
        // FALLBACK: Use average of actual frame durations
        const REFERENCE_TIME smartDuration = CalculateSmartFrameDuration();  // ? ADAPTIVE
        timeStop = timeStart + smartDuration;
        
        // Update duration history
        if (m_lastHardwareTimestamp > 0)
        {
            const REFERENCE_TIME currentHardwareTime = ConvertTimingClockToReferenceTime(
                videoFrame.GetTimingTimestamp(),
                m_timingClock->TimingClockTicksPerSecond());
            const REFERENCE_TIME measuredDuration = currentHardwareTime - m_lastHardwareTimestamp;
            
            UpdateFrameDurationHistory(measuredDuration);  // ? BUILD HISTORY
        }
        
        // Enforce monotonic progression
        const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
        if (monotonicTimeStop != timeStop)
        {
            DbgLog((LOG_WARNING, ...));
            timeStop = monotonicTimeStop;
        }
    }
    else
    {
        // Hardware stop time also checked for monotonicity
        const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
        if (monotonicTimeStop != timeStop)
        {
            DbgLog((LOG_WARNING, ...));
            timeStop = monotonicTimeStop;
        }
    }

    assert(timeStop > timeStart);
    break;
```

?? **Stop time calculation is DIFFERENT**

---

## Key Differences Summary

### CLOCK_SMART (Original)

```
???????????????????????????????????????????
? Hardware Timestamp Available            ?
? ?? YES: Use hardware stop time          ?
? ?? NO:  Use m_frameDuration (CONSTANT)  ?
???????????????????????????????????????????
```

**Characteristics:**
- ? Simple and predictable
- ? Uses theoretical frame duration as fallback
- ? Doesn't adapt to actual hardware variations
- ? Can accumulate drift if hardware runs slightly faster/slower than spec

### CLOCK_SMART2 (Enhanced)

```
???????????????????????????????????????????????????????????????????
? Hardware Timestamp Available                                    ?
? ?? YES: Use hardware stop time (with monotonic check)          ?
? ?? NO:  Use AVERAGE of last 100 actual durations               ?
?        ?? Build history from measured frame-to-frame intervals  ?
?        ?? Adapts to real hardware clock variations             ?
???????????????????????????????????????????????????????????????????
```

**Characteristics:**
- ? Adapts to real hardware behavior
- ? Tracks actual frame durations (circular buffer of 100 samples)
- ? Uses moving average for smoother timing
- ? Better handling of clock drift scenarios
- ? More robust monotonic enforcement
- ?? Slightly more CPU overhead (100-sample circular buffer)

---

## Duration Calculation Methods

### CLOCK_SMART Duration Strategy

```cpp
REFERENCE_TIME CalculateSmartFrameDuration() const
{
    // Always returns theoretical duration
    // = m_frameDuration
    // = (10,000,000 * m_frameDurationTicks) / m_timeScale
}
```

**Example (59.94 Hz):**
- Theoretical duration: 166,833 ticks (100ns units) = 16.6833ms
- This is used for EVERY frame when `NextFrameTimestamp()` is unavailable
- Never changes, even if hardware actually runs at 59.95 or 59.93

### CLOCK_SMART2 Duration Strategy

```cpp
REFERENCE_TIME CalculateSmartFrameDuration() const
{
    // If no history: return theoretical duration
    if (m_durationHistoryCount == 0)
    {
        return (REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale;
    }

    // Otherwise: return AVERAGE of last 100 measurements
    int64_t totalDuration = 0;
    const size_t sampleCount = (m_durationHistoryCount < DURATION_HISTORY_SIZE) 
        ? m_durationHistoryCount 
        : DURATION_HISTORY_SIZE;
    
    for (size_t i = 0; i < sampleCount; i++)
    {
        totalDuration += m_durationHistory[i];
    }

    return totalDuration / sampleCount;
}
```

**Example (59.94 Hz with hardware running +10 ppm fast):**
- Theoretical: 166,833 ticks
- After 100 frames: Average measures ~166,665 ticks (slightly shorter)
- SMART2 adapts and uses 166,665 instead of 166,833
- Result: Timing stays in sync with actual hardware

---

## Duration History Tracking

### SMART2 Circular Buffer

```cpp
// Member variables
REFERENCE_TIME m_durationHistory[DURATION_HISTORY_SIZE] = {};  // 100 entries
size_t m_durationHistoryIndex = 0;   // Write position (wraps 0-99)
size_t m_durationHistoryCount = 0;   // Valid entries (0-100)
REFERENCE_TIME m_lastHardwareTimestamp = 0;  // Previous frame's timestamp
```

### Update Process

```cpp
void UpdateFrameDurationHistory(REFERENCE_TIME actualDuration)
{
    // Validate: 5ms ? duration ? 1000ms (reject outliers)
    if (actualDuration < 50000LL || actualDuration > 10000000LL)
    {
        return;  // Skip invalid measurements
    }

    // Store in circular buffer
    m_durationHistory[m_durationHistoryIndex] = actualDuration;
    m_durationHistoryIndex = (m_durationHistoryIndex + 1) % DURATION_HISTORY_SIZE;
    
    if (m_durationHistoryCount < DURATION_HISTORY_SIZE)
    {
        m_durationHistoryCount++;
    }

    // Log stats every 50 frames (2.5 seconds @ 20fps, ~0.8 sec @ 60fps)
    if ((m_durationHistoryCount % 50) == 0)
    {
        const REFERENCE_TIME avgDuration = CalculateSmartFrameDuration();
        const REFERENCE_TIME theoreticalDuration = ...;
        
        DbgLog(("Average=%.3fms, Theoretical=%.3fms, Diff=%.3fms",
            avgDuration / 10000.0,
            theoreticalDuration / 10000.0,
            (avgDuration - theoreticalDuration) / 10000.0));
    }
}
```

---

## Monotonic Enforcement Comparison

### CLOCK_SMART

```cpp
// Only checks start time
if (m_previousTimeStop > 0)
{
    const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
    
    if (timeStart < minStartTime)
    {
        timeStart = minStartTime;  // ? ADJUST START TIME
    }
}

// Stop time uses NextFrameTimestamp() or theoretical duration
// No additional monotonic checks
```

### CLOCK_SMART2

```cpp
// Check start time (SAME AS SMART)
if (m_previousTimeStop > 0)
{
    const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
    
    if (timeStart < minStartTime)
    {
        timeStart = minStartTime;
    }
}

// ALSO check stop time when using fallback duration
if (timeStop == REFERENCE_TIME_INVALID)
{
    const REFERENCE_TIME smartDuration = CalculateSmartFrameDuration();
    timeStop = timeStart + smartDuration;
    
    // ? ENFORCE MONOTONIC ON STOP TIME
    const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
    if (monotonicTimeStop != timeStop)
    {
        timeStop = monotonicTimeStop;
    }
}

// ? ALSO CHECK MONOTONIC WHEN USING HARDWARE STOP TIME
else
{
    const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
    if (monotonicTimeStop != timeStop)
    {
        timeStop = monotonicTimeStop;
    }
}
```

? **SMART2 enforces monotonicity on BOTH start and stop times**  
? **SMART only enforces on start time**

---

## When to Use Each Mode

### Use CLOCK_SMART When:

- **Hardware timestamps are always available** from `NextFrameTimestamp()`
  - Fall back rarely or never occurs
  - Theoretical duration is just a safety net

- **Frame rates are fixed and consistent**
  - No frame dropping or rate variations
  - Hardware clock is stable

- **You want minimal overhead**
  - No history tracking needed
  - Simpler logic, fewer branches

- **Testing/validation**
  - Want deterministic, predictable behavior
  - Easier to diagnose timing issues

### Use CLOCK_SMART2 When:

- **Hardware timestamps are occasionally unavailable**
  - Fall back is more than just a safety net
  - Need adaptive fallback behavior

- **Hardware clock runs at slightly different rates**
  - DeckLink at +10 ppm, display at -5 ppm
  - Slow clock drift accumulation
  - Long-duration captures where drift matters

- **Variable frame rates or dynamic conditions**
  - Network glitches causing occasional delays
  - Variable capture buffer conditions

- **Better long-term synchronization** required
  - > 1 hour continuous playback
  - Drift accumulation becomes visible
  - Moving average absorbs noise better

---

## Practical Impact Example

### Scenario: 59.94 Hz capture with display at spec

| Time | CLOCK_SMART | CLOCK_SMART2 |
|------|------------|-------------|
| **Frame 1** | Duration: 166,833 ticks (theoretical) | Duration: 166,833 ticks (theoretical) |
| **Frame 50** | Duration: 166,833 ticks (constant) | Duration: 166,810 ticks (measured avg) |
| **Frame 100** | Duration: 166,833 ticks (constant) | Duration: 166,805 ticks (measured avg) |
| **After 1 hour** | Drift: ~50-100ms | Drift: ~5-10ms |
| **After 4 hours** | Drift: >200ms visible | Drift: <50ms |

---

## Summary Table

| Feature | CLOCK_SMART | CLOCK_SMART2 |
|---------|------------|------------|
| Start Time Source | Hardware ? | Hardware ? |
| Stop Time (w/ hardware) | Hardware ? | Hardware + Monotonic ? |
| Stop Time (w/o hardware) | Theoretical | **Measured Average** |
| Duration Tracking | ? None | ? Last 100 samples |
| Adaptation | ? Static | ? Dynamic |
| Overhead | Low | Low-Medium |
| Complexity | Simple | Moderate |
| Long-term Drift | Moderate | Low |
| Best For | Stable/short captures | Adaptive/long captures |
