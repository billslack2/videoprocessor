# PPM Trim Corrections Applied to HARDWARE_RATIONAL Mode

## Summary

Applied PPM (Parts Per Million) trim corrections to **HARDWARE_RATIONAL** timing mode to ensure consistent timing accuracy across both rational timing modes.

## Changes Made

### 1. **Initialize() Method** (Lines 85-105)
**Added:** PPM corrections loading for HARDWARE_RATIONAL mode
- Moved PPM loading to execute for both `DS_SSTM_RATIONAL_RATIONAL` and `DS_SSTM_HARDWARE_RATIONAL`
- Calculates refresh rate and calls `LoadPPMCorrections()` to load `correction.cfg`
- Ensures frame rate adjustments are available when needed

**Before:**
```cpp
if (timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
{
    double refreshRate = (double)timeScale / (double)frameDurationTicks;
    LoadPPMCorrections(refreshRate);
}
```

**After:**
```cpp
if (timestamp == DirectShowStartStopTimeMethod::DS_SSTM_RATIONAL_RATIONAL)
{
    double refreshRate = (double)timeScale / (double)frameDurationTicks;
    LoadPPMCorrections(refreshRate);
}
else if (timestamp == DirectShowStartStopTimeMethod::DS_SSTM_HARDWARE_RATIONAL)
{
    double refreshRate = (double)timeScale / (double)frameDurationTicks;
    LoadPPMCorrections(refreshRate);
}
```

### 2. **RenderVideoFrameIntoSample() - HARDWARE_RATIONAL Case** (Lines 340-365)
**Added:** PPM trim correction when calculating m_rationalFrameDuration on first frame
- Applies `GetRationalTrimNumerator()` using U64_MulDiv (precise 128-bit math)
- Uses identical correction approach as RATIONAL_RATIONAL mode
- Updated debug logging to indicate PPM trim is applied

**Before:**
```cpp
m_rationalFrameDuration = (REFERENCE_TIME)((referenceTimePerSecond * m_frameDurationTicks) / m_timeScale);
```

**After:**
```cpp
uint64_t trimmedDurationTicks = U64_MulDiv(
    (uint64_t)m_frameDurationTicks,
    GetRationalTrimNumerator(),
    RATIONAL_TRIM_DENOMINATOR);

m_rationalFrameDuration = (REFERENCE_TIME)((referenceTimePerSecond * trimmedDurationTicks) / m_timeScale);
```

### 3. **Reset() Method - HARDWARE_RATIONAL Logging** (Lines 426-455)
**Added:** Detailed PPM correction logging for HARDWARE_RATIONAL mode
- Shows PPM adjustment value from correction.cfg
- Displays effect (faster/slower) with trim percentage
- Identifies whether corrections came from file or default
- Clarifies hybrid approach: "Hardware timestamps for start, rational duration with PPM trim for frame intervals"

**Example Output:**
```
Reset(): HARDWARE_RATIONAL mode active - using correction.cfg PPM:
  PPM adjustment: 25 (from correction.cfg)
  Effect: Stream runs 25 PPM FASTER (trim 99.997500% = slight slowdown to compensate)
  Hardware timestamps for start, rational duration with PPM trim for frame intervals
```

## Technical Impact

### Timing Accuracy
- **Before:** Frame duration based only on theoretical rational math (no PPM correction)
- **After:** Frame duration adjusted by correction.cfg PPM offset, matching RATIONAL_RATIONAL mode

### Example: 23.976 Hz Stream with +25 PPM Correction

| Parameter | Value |
|-----------|-------|
| Nominal Frame Duration | 41708.33 ticks |
| Trim Numerator | 999,975 (out of 1,000,000) |
| **Corrected Duration** | **41,706.87 ticks** |
| **Net Effect** | 1.46 ticks shorter per frame = compensates for stream running 25 PPM fast |

## Consistency Improvements

### Both modes now align on:
1. ? PPM correction loading during initialization
2. ? Same U64_MulDiv precision for trim calculation
3. ? Identical trim numerator (`GetRationalTrimNumerator()`)
4. ? Same RATIONAL_TRIM_DENOMINATOR (1,000,000)
5. ? Consistent debug logging and visibility

## Testing & Validation

- ? Code compiles without errors
- ? Full solution build successful
- ? No breaking changes (internal implementation only)
- ? Backward compatible (falls back to default if no correction.cfg)

## Benefit

HARDWARE_RATIONAL mode now provides **frame-rate accuracy matching RATIONAL_RATIONAL**, while still using hardware timestamps for start time. This eliminates a timing accuracy gap and ensures users get consistent performance regardless of which hybrid timing mode they choose.

