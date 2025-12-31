# Stats Overlay Update: Conversion Time Percentage Display

## What Changed

The stats overlay now displays **conversion time as a percentage of frame time** with a warning indicator when conversion is consuming too much of the rendering budget.

## Before vs After

### Before (Old Display)
```
Conv Time:        1.5 ms
10s Avg/Max:      1.6 / 3.2 ms
```

**Problem:** No context about whether 1.5ms is good or bad.

### After (New Display)

#### Safe Scenario (60Hz, 1.5ms conversion)
```
Conv Time:        1.5 ms (9.0%)      ? Under 10%, safe
10s Avg/Max:      1.6 / 3.2 ms
```

#### Warning Scenario (60Hz, 2.5ms conversion)
```
Conv Time:        2.5 ms (15.0%) ?   ? Over 10%, warning shown
10s Avg/Max:      2.3 / 4.1 ms
```

#### Critical Scenario (60Hz, 4.0ms conversion)
```
Conv Time:        4.0 ms (24.0%) ?   ? Critical, likely dropping frames
10s Avg/Max:      3.8 / 6.2 ms
```

## Full Stats Overlay Example

### Stable 60Hz Operation (After P010 Optimization)

```
??????????????????????????????????????????????
?  Resolution:       3840x2160               ?
?  Refresh:          59.940000 Hz            ?
?  Capture Rate:     59.941234 Hz            ?
?  Deviation:        +2 ppm                  ?
?  EOTF:             PQ                      ?
?  Colorspace:       BT.2020                 ?
?  Pixel Format:     V210                    ?
?  Video Conv:       V210 > P010             ?
?  Conv Time:        1.5 ms (9.0%)           ?  ? NEW: Shows percentage
?  10s Avg/Max:      1.6 / 3.2 ms            ?
?                                            ?
?  Method:           Clock-Rational          ?
?  PPM Correction:   +5                      ?
?  Offset:           N/A                     ?
?                                            ?
?  VP Lat:           2.3 ms                  ?
?  DS Lat:           8.1 ms                  ?
?                                            ?
?  Queue:            4/8                     ?
?  VFrames:          12345                   ?
?  Dropped:          0/2                     ?
??????????????????????????????????????????????
```

### Problematic Scenario (Before Optimization)

```
??????????????????????????????????????????????
?  Resolution:       3840x2160               ?
?  Refresh:          59.940000 Hz            ?
?  Capture Rate:     59.938721 Hz            ?
?  Deviation:        -2 ppm                  ?
?  EOTF:             PQ                      ?
?  Colorspace:       BT.2020                 ?
?  Pixel Format:     V210                    ?
?  Video Conv:       V210 > P010             ?
?  Conv Time:        4.0 ms (24.0%) ?        ?  ? WARNING: Too high!
?  10s Avg/Max:      3.8 / 6.2 ms            ?
?                                            ?
?  Method:           Clock-Rational          ?
?  PPM Correction:   +5                      ?
?  Offset:           N/A                     ?
?                                            ?
?  VP Lat:           2.3 ms                  ?
?  DS Lat:           8.1 ms                  ?
?                                            ?
?  Queue:            7/8 [FULL]              ?  ? Queue backing up!
?  VFrames:          12345                   ?
?  Dropped:          42/138                  ?  ? Dropping frames!
??????????????????????????????????????????????
```

## Interpretation Guide

### Conversion Time Percentage Thresholds

| % of Frame Time | 24Hz (41.67ms) | 60Hz (16.67ms) | 120Hz (8.33ms) | Status |
|-----------------|----------------|----------------|----------------|---------|
| **0-5%**        | < 2.1ms        | < 0.8ms        | < 0.4ms        | ? Excellent |
| **5-10%**       | 2.1-4.2ms      | 0.8-1.7ms      | 0.4-0.8ms      | ? Good |
| **10-15%**      | 4.2-6.3ms      | 1.7-2.5ms      | 0.8-1.2ms      | ? Marginal |
| **15-25%**      | 6.3-10.4ms     | 2.5-4.2ms      | 1.2-2.1ms      | ? Poor |
| **>25%**        | > 10.4ms       | > 4.2ms        | > 2.1ms        | ? Critical |

### Warning Indicator (?)

Appears when conversion time exceeds **10% of frame time**:

```cpp
if (conversionPct > 10.0)
{
    line.Format(TEXT("Conv Time:        %.2f ms (%.1f%%) ?"), 
        currentConvMs, conversionPct);
}
```

**Meaning:** The conversion is taking a significant portion of the frame budget, leaving less time for MadVR to render. This can lead to:
- Increased latency
- Frame drops
- Stuttering
- Queue backup (watch "Queue" stat)

## Diagnostic Workflow

### 1. Check Conversion Percentage First

If you see:
```
Conv Time:        2.5 ms (15.0%) ?
```

**This means:**
- At 60Hz, each frame has 16.67ms total
- Conversion is using 2.5ms (15%)
- **MadVR only has 14.17ms left** to render
- This is likely causing instability

### 2. Compare to Queue Status

**Healthy:**
```
Conv Time:        1.5 ms (9.0%)
Queue:            4/8
Dropped:          0/2
```
**Conversion is fast enough, queue is not backing up.**

**Unhealthy:**
```
Conv Time:        4.0 ms (24.0%) ?
Queue:            8/8 [FULL]          ? Queue saturated
Dropped:          42/138              ? Dropping frames
```
**Conversion is too slow, queue fills up, frames dropped.**

### 3. Check Spike Behavior

```
Conv Time:        1.5 ms (9.0%)
10s Avg/Max:      1.6 / 12.5 ms      ? Max is 8x average!
```

**Sporadic spikes detected.** Causes:
- CPU frequency scaling
- Thermal throttling
- Background processes
- Power saving mode

**Solutions:**
- Set Windows power plan to "High Performance"
- Check CPU temperatures
- Close unnecessary background apps
- Disable CPU frequency scaling

### 4. Look for Patterns

**Consistent performance (good):**
```
Conv Time:        1.5 ms (9.0%)
10s Avg/Max:      1.6 / 2.1 ms       ? Max is close to average
```

**Variable performance (problem):**
```
Conv Time:        3.2 ms (19.2%) ?
10s Avg/Max:      2.8 / 9.7 ms       ? Highly variable
```

## Real-World Examples

### Example 1: Optimized P010 Conversion at 60Hz
```
Video Conv:       V210 > P010
Conv Time:        1.5 ms (9.0%)
10s Avg/Max:      1.6 / 2.3 ms
Queue:            4/8
Dropped:          0/0
```
**Assessment:** ? Excellent. Conversion is fast, consistent, no drops.

### Example 2: Slow Conversion at 60Hz
```
Video Conv:       V210 > P010
Conv Time:        4.2 ms (25.2%) ?
10s Avg/Max:      4.0 / 5.8 ms
Queue:            7/8 [FULL]
Dropped:          23/87
```
**Assessment:** ? Critical. Conversion is too slow, queue backing up, dropping frames.
**Action:** Use optimized P010 conversion method (see `P010_CONVERSION_METHOD_SELECTION.md`).

### Example 3: 120Hz Operation
```
Refresh:          119.880000 Hz
Video Conv:       V210 > P010
Conv Time:        1.5 ms (18.0%) ?      ? Same 1.5ms, but now 18%!
10s Avg/Max:      1.6 / 2.3 ms
Queue:            6/8
Dropped:          15/42
```
**Assessment:** ? Problematic. Same conversion time, but at 120Hz (8.33ms frame time), it's now consuming 18% of the budget.
**Action:** Need async conversion or GPU acceleration for stable 120Hz.

### Example 4: No Conversion (Native Format)
```
Video Conv:       None
                                        ? No conversion stats shown
Queue:            2/8
Dropped:          0/0
```
**Assessment:** ? Perfect. No conversion overhead, MadVR gets 100% of frame time.

## Technical Details

### Calculation

```cpp
// In StatsOverlayWindow.cpp::DrawStats()
double frameTimeMs = 1000.0 / m_stats.refreshRate;         // e.g., 16.67ms at 60Hz
double currentConvMs = m_stats.currentConversionTimeUs / 1000.0;  // Convert ?s to ms
double conversionPct = (currentConvMs / frameTimeMs) * 100.0;     // Calculate percentage

if (conversionPct > 10.0)
{
    line.Format(TEXT("Conv Time:        %.2f ms (%.1f%%) ?"), 
        currentConvMs, conversionPct);
}
else
{
    line.Format(TEXT("Conv Time:        %.2f ms (%.1f%%)"), 
        currentConvMs, conversionPct);
}
```

### Warning Threshold

**Why 10%?**

At 60Hz (16.67ms per frame):
- **10% = 1.67ms** conversion time
- Leaves **15.0ms** for MadVR (90% of frame time)
- MadVR needs ~10-12ms for complex HDR processing
- **2-3ms margin** for jitter/overhead
- This is the practical limit for stable operation

At higher percentages:
- **15% = 2.5ms** ? Only 14.17ms left (tight but possible)
- **20% = 3.3ms** ? Only 13.37ms left (unstable)
- **25% = 4.2ms** ? Only 12.47ms left (frequent drops)

## Benefits of This Display

### Before
**User sees:** "Conv Time: 4.0 ms"
**User thinks:** "Is that good or bad? I don't know."

### After
**User sees:** "Conv Time: 4.0 ms (24.0%) ?"
**User knows immediately:**
- Conversion is taking 24% of the frame budget (BAD)
- Warning indicator confirms it's a problem
- MadVR only has 76% of frame time left
- This explains why they're seeing drops

### Actionable Feedback

The percentage gives **instant context**:
- **9%** ? "This is fine, conversion isn't my problem"
- **15% ?** ? "Aha! Conversion is eating too much time, need to optimize"
- **24% ?** ? "This is definitely why I'm dropping frames at 60Hz"

Combined with queue stats:
```
Conv Time:        4.0 ms (24.0%) ?
Queue:            8/8 [FULL]
Dropped:          42/138
```

**Clear story:** Slow conversion ? Queue fills ? Frames dropped.

## Related Documentation

- **CONVERSION_TIME_IMPACT.md** - Deep dive into why conversion time matters
- **P010_CONVERSION_METHOD_SELECTION.md** - How to optimize P010 conversion
- **docs/CLOCK_RATIONAL_VS_SMART_COMPARISON.md** - Timing modes comparison

## Summary

The updated stats overlay now provides **immediate visual feedback** about whether video conversion is impacting rendering performance. The percentage calculation and warning indicator (?) make it obvious when conversion time is consuming too much of the frame budget, helping users diagnose stability issues quickly.
