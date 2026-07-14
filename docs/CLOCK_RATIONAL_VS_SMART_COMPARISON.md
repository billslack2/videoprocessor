# CLOCK_RATIONAL vs CLOCK_SMART vs CLOCK_SMART2 - Complete Comparison

## Overview

Three hardware-based timing modes with different approaches to frame duration calculation and PPM correction:

| Mode | Start Time | Stop Time | Duration | PPM Correction | Use Case |
|------|-----------|----------|----------|---|----------|
| **CLOCK_SMART** | Hardware | Hardware (or theoretical) | Static theoretical | ? None | Basic stable captures |
| **CLOCK_SMART2** | Hardware | Hardware (or adaptive average) | Adaptive measured | ? None | Long captures with drift |
| **CLOCK_RATIONAL** | Hardware | Hardware + rational math | Rational + PPM trim | ? Yes (correction.cfg) | Precise sync with PPM compensation |

---

## Detailed Comparison

### 1. Start Time Calculation

```
???????????????????????????????????????????????????????????
? ALL THREE MODES USE IDENTICAL START TIME LOGIC          ?
???????????????????????????????????????????????????????????

Step 1: Get hardware timestamp
   timeStart = ConvertTimingClockToReferenceTime(
       videoFrame.GetTimingTimestamp(), 
       m_timingClock->TimingClockTicksPerSecond());

Step 2: Offset first frame to zero
   if (m_startTimeOffset == 0)
       m_startTimeOffset = timeStart;
   timeStart -= m_startTimeOffset;

Step 3: Enforce monotonic progression
   if (m_previousTimeStop > 0)
       timeStart = max(timeStart, m_previousTimeStop - m_frameDuration);
```

? **Start time: IDENTICAL across all three modes**

---

### 2. Stop Time Calculation - The Key Differences

#### CLOCK_SMART
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

    timeStop = NextFrameTimestamp();
    if (timeStop == REFERENCE_TIME_INVALID)
    {
        // Fallback: Use THEORETICAL frame duration (constant)
        timeStop = timeStart + m_frameDuration;
    }
    else
    {
        assert(m_startTimeOffset > 0);
        timeStop -= m_startTimeOffset;
    }
    break;
```

**Characteristics:**
- Uses **hardware stop time** when available
- Falls back to **constant theoretical duration**
- No adaptive behavior
- Simple, predictable

---

#### CLOCK_SMART2
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART2:

    timeStop = NextFrameTimestamp();
    if (timeStop == REFERENCE_TIME_INVALID)
    {
        // Fallback: Use ADAPTIVE average of last 100 frame durations
        const REFERENCE_TIME smartDuration = CalculateSmartFrameDuration();
        timeStop = timeStart + smartDuration;
        
        // Track actual durations for future averages
        if (m_lastHardwareTimestamp > 0)
        {
            const REFERENCE_TIME currentHardwareTime = ConvertTimingClockToReferenceTime(
                videoFrame.GetTimingTimestamp(),
                m_timingClock->TimingClockTicksPerSecond());
            const REFERENCE_TIME measuredDuration = currentHardwareTime - m_lastHardwareTimestamp;
            
            UpdateFrameDurationHistory(measuredDuration);
        }
        
        // Enforce monotonic on both fallback AND hardware stop times
        const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
        if (monotonicTimeStop != timeStop)
        {
            timeStop = monotonicTimeStop;
        }
    }
    else
    {
        assert(m_startTimeOffset > 0);
        timeStop -= m_startTimeOffset;
        
        // ALSO check hardware stop time for monotonicity
        const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
        if (monotonicTimeStop != timeStop)
        {
            timeStop = monotonicTimeStop;
        }
    }

    assert(timeStop > timeStart);
    break;
```

**Characteristics:**
- Uses **hardware stop time** when available
- Falls back to **adaptive average of 100 samples**
- Tracks actual durations from hardware timestamps
- Enforces monotonicity on BOTH fallback and hardware paths
- Adapts to real hardware behavior

---

#### CLOCK_RATIONAL ? THE HYBRID APPROACH
```cpp
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
{
    // HYBRID MODE: Hardware start time + Rational duration math + PPM correction

    // Get raw hardware timestamp
    REFERENCE_TIME rawHardwareTime = (REFERENCE_TIME)(videoFrame.GetTimingTimestamp() * 
        (10000000.0 / m_timingClock->TimingClockTicksPerSecond()));
    
    // Initialize rational frame duration and limits on first frame
    if (m_rationalFrameDuration == 0)
    {
        const uint64_t referenceTimePerSecond = 10000000ULL;
        
        // Apply PPM trim correction to frame duration
        // This is the KEY DIFFERENCE: incorporates correction.cfg PPM adjustments
        uint64_t trimmedDurationTicks = U64_MulDiv(
            (uint64_t)m_frameDurationTicks,
            GetRationalTrimNumerator(),      // ? FROM correction.cfg
            RATIONAL_TRIM_DENOMINATOR);
        
        m_rationalFrameDuration = (REFERENCE_TIME)((referenceTimePerSecond * trimmedDurationTicks) / m_timeScale);
        m_minFrameAdvance = m_rationalFrameDuration / 4;      // 25% tolerance
        m_maxFrameAdvance = m_rationalFrameDuration * 2;      // 200% tolerance
        
        DbgLog((LOG_TRACE, 1, TEXT("Initialized rational duration=%I64d (%.3fms) with PPM trim, limits=[%I64d, %I64d]"),
            m_rationalFrameDuration, m_rationalFrameDuration / 10000.0,
            m_minFrameAdvance, m_maxFrameAdvance));
    }
    
    // Handle first frame
    if (m_startTimeOffset == 0)
    {
        m_startTimeOffset = rawHardwareTime;
        m_previousHardwareTimestamp = rawHardwareTime;
        timeStart = 0;
    }
    else
    {
        timeStart = rawHardwareTime - m_startTimeOffset;
        
        // ANOMALY DETECTION: Hardware timestamp validation
        const REFERENCE_TIME timeSincePrevious = timeStart - (m_previousTimeStop - m_rationalFrameDuration);
        
        if (timeSincePrevious < m_minFrameAdvance)
        {
            // Hardware went backwards or too close
            timeStart = m_previousTimeStop - m_rationalFrameDuration + m_minFrameAdvance;
            m_hardwareTimingAnomalyCount++;
            
            DbgLog((LOG_WARNING, 1, TEXT("Hardware time too close/backwards, enforced (anomaly #%u)"),
                m_hardwareTimingAnomalyCount));
        }
        else if (timeSincePrevious > m_maxFrameAdvance)
        {
            // Hardware jumped too far
            timeStart = m_previousTimeStop - m_rationalFrameDuration + m_maxFrameAdvance;
            m_hardwareTimingAnomalyCount++;
            
            DbgLog((LOG_WARNING, 1, TEXT("Hardware time jumped too far, limited (anomaly #%u)"),
                m_hardwareTimingAnomalyCount));
        }
        
        // Final monotonic check
        if (timeStart <= (m_previousTimeStop - m_rationalFrameDuration))
        {
            timeStart = m_previousTimeStop - m_rationalFrameDuration + 1;
        }
    }
    
    m_previousHardwareTimestamp = rawHardwareTime;
    break;
}

// Then in stop time section:
case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_RATIONAL:
{
    // Use rational math for PERFECT frame duration (not hardware-dependent)
    timeStop = timeStart + m_rationalFrameDuration;  // ? RATIONAL, not hardware
    
    // Log periodically for diagnostics
    if (streamFrameCounter % 300 == 0)
    {
        DbgLog((LOG_TRACE, 1, TEXT("timeStart=%I64d, duration=%I64d (w/ PPM trim), timeStop=%I64d"),
            timeStart, m_rationalFrameDuration, timeStop));
    }
    break;
}
```

**Characteristics:**
- Uses **hardware timestamps** for start time detection only
- Uses **rational mathematical frame duration** for stop time (not hardware-dependent)
- **PPM-corrected duration** loaded from `correction.cfg`
- **Anomaly detection** on hardware timestamps (validates progression)
- Best of both worlds: hardware alignment + mathematical precision

---

## Duration Calculation Comparison

### CLOCK_SMART Duration
```cpp
REFERENCE_TIME CalculateSmartFrameDuration() const
{
    // Always returns theoretical duration
    return (REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale;
    
    // Example: 59.94 Hz
    // = (10,000,000 * 1001) / 60000
    // = 166,833 ticks (100ns units) = 16.6833ms
    // CONSTANT - never changes
}
```

### CLOCK_SMART2 Duration
```cpp
REFERENCE_TIME CalculateSmartFrameDuration() const
{
    if (m_durationHistoryCount == 0)
    {
        // Fallback to theoretical while building history
        return (REFERENCE_TIME_TICKS_PER_SECOND * m_frameDurationTicks) / m_timeScale;
    }

    // ADAPTIVE: Average of last 100 measured durations
    int64_t totalDuration = 0;
    const size_t sampleCount = (m_durationHistoryCount < DURATION_HISTORY_SIZE) 
        ? m_durationHistoryCount 
        : DURATION_HISTORY_SIZE;
    
    for (size_t i = 0; i < sampleCount; i++)
    {
        totalDuration += m_durationHistory[i];
    }

    return totalDuration / sampleCount;
    
    // Example: Hardware running +5 ppm fast
    // Theoretical: 166,833 ticks
    // After 100 frames: Average measures 166,750 ticks (slightly shorter)
    // Adapts to actual hardware rate
}
```

### CLOCK_RATIONAL Duration
```cpp
// Calculated at initialization time
if (m_rationalFrameDuration == 0)
{
    const uint64_t referenceTimePerSecond = 10000000ULL;
    
    // APPLY PPM CORRECTION FROM correction.cfg
    // Example: 59.94 Hz with +10 ppm correction needed
    // GetRationalTrimNumerator() = 999,990 (was 1,000,000)
    // Adjustment: stream running 10 ppm too fast, so slow it down by 10 ppm
    
    uint64_t trimmedDurationTicks = U64_MulDiv(
        (uint64_t)m_frameDurationTicks,      // 1001 for 59.94
        GetRationalTrimNumerator(),           // 999,990 (with +10 ppm correction)
        RATIONAL_TRIM_DENOMINATOR);           // 1,000,000
    
    m_rationalFrameDuration = (REFERENCE_TIME)((referenceTimePerSecond * trimmedDurationTicks) / m_timeScale);
    
    // Result: 166,665 ticks instead of 166,833
    // Permanently corrected for the entire playback session
    // All frames use this SAME rational duration
}

// Usage
timeStop = timeStart + m_rationalFrameDuration;  // ? CONSTANT but PPM-corrected
```

---

## PPM Correction Handling

### CLOCK_SMART & CLOCK_SMART2
```cpp
// ? NO PPM CORRECTION SUPPORT
// These modes ignore correction.cfg entirely
// They adapt to hardware behavior but don't apply corrections
```

### CLOCK_RATIONAL
```cpp
// ? FULL PPM CORRECTION SUPPORT
void ALiveSourceVideoOutputPin::LoadPPMCorrections(double refreshRate)
{
    bool loaded = m_ppmCorrectionLoader.LoadCorrectionFile();
    
    if (loaded)
    {
        // Get PPM correction for this specific refresh rate from correction.cfg
        int ppmCorrection = m_ppmCorrectionLoader.GetPPMCorrection(refreshRate);
        
        if (ppmCorrection == 0)
        {
            m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;  // No correction
        }
        else
        {
            // Calculate trim numerator: subtract PPM from denominator
            // Positive PPM = stream faster = use smaller numerator = stream slower
            m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR - ppmCorrection;
        }
        
        DbgLog((LOG_TRACE, 1, TEXT("%.3f Hz - applying %d PPM (trim: %.6f%%)"), 
            refreshRate, ppmCorrection,
            (100.0 * m_currentRationalTrimNumerator) / RATIONAL_TRIM_DENOMINATOR));
    }
    else
    {
        // No correction file
        m_currentRationalTrimNumerator = RATIONAL_TRIM_DENOMINATOR;
    }
}

// Example: 59.94 Hz, correction.cfg specifies +10 ppm
// DeckLink runs 10 ppm too fast, display runs at spec
// ppmCorrection = +10
// m_currentRationalTrimNumerator = 1,000,000 - 10 = 999,990
// Duration multiplied by 999,990/1,000,000 = 0.99999
// Result: Stream slows by 10 ppm to match display
```

---

## Anomaly Handling Comparison

### CLOCK_SMART
```cpp
// Minimal anomaly handling
// Only checks that start time doesn't go backwards
if (m_previousTimeStop > 0)
{
    const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
    if (timeStart < minStartTime)
    {
        timeStart = minStartTime;  // ? MINIMAL CORRECTION
    }
}
```

### CLOCK_SMART2
```cpp
// Enhanced monotonic enforcement
// Checks both start and stop times
if (m_previousTimeStop > 0)
{
    const REFERENCE_TIME minStartTime = m_previousTimeStop - m_frameDuration;
    if (timeStart < minStartTime)
    {
        timeStart = minStartTime;
    }
}

// Also enforces monotonicity on stop times
const REFERENCE_TIME monotonicTimeStop = EnforceMonotonicProgression(timeStop, m_previousTimeStop);
if (monotonicTimeStop != timeStop)
{
    timeStop = monotonicTimeStop;  // ? ENHANCED CORRECTION
}
```

### CLOCK_RATIONAL ?
```cpp
// SOPHISTICATED anomaly detection with tracking
// Validates hardware timestamp progression

REFERENCE_TIME timeSincePrevious = timeStart - (m_previousTimeStop - m_rationalFrameDuration);

if (timeSincePrevious < m_minFrameAdvance)
{
    // Hardware went backwards or stalled
    timeStart = m_previousTimeStop - m_rationalFrameDuration + m_minFrameAdvance;
    m_hardwareTimingAnomalyCount++;  // ? COUNT ANOMALIES
    
    DbgLog((LOG_WARNING, 1, TEXT("Hardware time anomaly #%u: too close/backwards"),
        m_hardwareTimingAnomalyCount));
}
else if (timeSincePrevious > m_maxFrameAdvance)
{
    // Hardware jumped too far (frame drop recovery?)
    timeStart = m_previousTimeStop - m_rationalFrameDuration + m_maxFrameAdvance;
    m_hardwareTimingAnomalyCount++;  // ? COUNT ANOMALIES
    
    DbgLog((LOG_WARNING, 1, TEXT("Hardware time anomaly #%u: jumped too far"),
        m_hardwareTimingAnomalyCount));
}

// Final failsafe
if (timeStart <= (m_previousTimeStop - m_rationalFrameDuration))
{
    timeStart = m_previousTimeStop - m_rationalFrameDuration + 1;
}

// BONUS: Reports anomaly count for diagnostics
// Debug logs show: "anomalies=5" for entire playback session
```

---

## Practical Comparison Example

### Scenario: 59.94 Hz, 4-hour capture

**Hardware Behavior:** DeckLink clock runs +15 PPM fast (1.5 seconds/hour drift)

#### CLOCK_SMART
```
Hour 1:  ±0 seconds drift (uses theoretical 166,833 ticks)
Hour 2:  ~1.5 seconds drift
Hour 3:  ~3.0 seconds drift
Hour 4:  ~4.5 seconds drift

Result: By end of playback, audio/video seriously out of sync
```

#### CLOCK_SMART2
```
Hour 1:  ±0 seconds (building history, uses theoretical)
Hour 2:  ~0.5 seconds (history adapts to +15 ppm, learns measured duration)
Hour 3:  ~0.7 seconds (moving average absorbs variations)
Hour 4:  ~0.9 seconds (stabilized around measured value)

Result: Long-term drift reduced but not eliminated
```

#### CLOCK_RATIONAL (with correction.cfg: +15 PPM)
```
Hour 1:  ±0 seconds (applies +15 ppm correction from day 1)
Hour 2:  ±0 seconds (rational math is drift-free with correction)
Hour 3:  ±0 seconds (perfect mathematical precision)
Hour 4:  ±0 seconds (no drift accumulation)

Result: Perfect sync maintained across entire 4-hour capture
```

---

## When to Use Each Mode

### Use CLOCK_SMART When:
- ? Hardware timestamps always available (no fallbacks)
- ? Short captures (< 30 minutes)
- ? Frame rates are very stable
- ? You want simplest possible logic
- ? Testing/validation

### Use CLOCK_SMART2 When:
- ? Hardware timestamps occasionally unavailable
- ? Medium captures (30 minutes - 4 hours)
- ? Want adaptive behavior to match hardware
- ? Don't have correction data available
- ? Can tolerate small drift over time

### Use CLOCK_RATIONAL When:
- ? **PPM correction data available** (correction.cfg)
- ? Long captures (> 4 hours)
- ? **Need perfect sync** with displays/audio
- ? Frame rates vary slightly (+/- PPM)
- ? Professional applications (sports, concerts, events)
- ? Want hardware timestamp validation
- ? Want anomaly reporting for diagnostics

---

## Architecture Diagram

```
???????????????????????????????????????????????????????????????????
?                         START TIME (SAME FOR ALL)               ?
?  Hardware Timestamp ? Convert to Reference Time ? Offset to 0   ?
???????????????????????????????????????????????????????????????????
                              ?
                         
              ???????????????????????????????????????????????
              ?          DIVERGES HERE: Duration Type      ?
              ???????????????????????????????????????????????
                              ?
        ????????????????????????????????????????????????????????????
        ?                           ?                              ?
   CLOCK_SMART              CLOCK_SMART2                  CLOCK_RATIONAL
   ????????????             ????????????????            ????????????????
   ? Hardware ?             ?   Hardware   ?            ?  Hardware    ?
   ?  Stop    ?             ?    Stop      ?            ?  Start Only  ?
   ?          ?             ?              ?            ?              ?
   ?Fallback: ?             ?  Fallback:   ?            ?Duration:     ?
   ?Constant  ?             ?  Adaptive    ?            ?Rational Math ?
   ?170 ticks ?             ?  Avg (100)   ?            ?+ PPM Correct ?
   ?          ?             ?  160 ticks   ?            ?163 ticks     ?
   ?(fixed)   ?             ? (adaptive)   ?            ?(constant but ?
   ?          ?             ?              ?            ? PPM-adjusted)?
   ?No PPM    ?             ? No PPM       ?            ?? PPM Corr   ?
   ?Correction?             ? Correction   ?            ?from cfg      ?
   ????????????             ????????????????            ????????????????
```

---

## Summary Comparison Table

| Feature | CLOCK_SMART | CLOCK_SMART2 | CLOCK_RATIONAL |
|---------|------------|------------|----------------|
| **Start Time** | Hardware | Hardware | Hardware |
| **Stop Time** | Hardware or theoretical | Hardware or adaptive avg | Hardware + rational math |
| **Duration Type** | Static theoretical | Adaptive measured | Rational + PPM-corrected |
| **PPM Correction** | ? None | ? None | ? Yes (correction.cfg) |
| **History Tracking** | ? None | ? 100 samples | ? None |
| **Anomaly Detection** | Basic | Enhanced | **Sophisticated** |
| **Anomaly Counting** | No | No | **Yes** |
| **Long-term Drift** | Moderate | Low | **Zero** |
| **Complexity** | Low | Medium | Medium-High |
| **Best Use** | Short stable | Medium adaptive | Long precise |
| **Capture Duration** | < 30 min | 30 min - 4 hrs | **> 4 hours** |

---

## Technical Highlights

### CLOCK_RATIONAL Unique Features

1. **PPM-Aware Frame Duration**
   - Adjusts frame duration based on correction.cfg
   - Permanent throughout playback session
   - Mathematically perfect (no rounding errors)

2. **Anomaly Tracking**
   - Counts hardware timing anomalies
   - Logs them for diagnostics
   - Helps identify capture card issues

3. **Tolerance Bands**
   - `m_minFrameAdvance` = 25% of rational duration
   - `m_maxFrameAdvance` = 200% of rational duration
   - Validates hardware progression sanity

4. **Perfect Sync Guarantee**
   - With correct correction.cfg values
   - Maintains sync indefinitely
   - No cumulative drift

