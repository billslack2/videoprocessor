# CLOCK_DRIFT_DIAGNOSIS_AND_FIX

# Clock Drift Diagnosis and PPM Correction Guide

## Your Situation

You mentioned:
> "But I DO get drops and clock deviation sometimes that is what I am trying to fix!!"

This is a **real problem** that needs systematic diagnosis and correction. The 4-digit precision OSD you just implemented is the perfect tool for this.

---

## CRITICAL FIX: PPM Config Matching

? **CORRECT**: Your refresh rate key in `correction.cfg` uses **TRUNCATED** integer value (first digits before decimal), not rounded!

### Examples

| Actual Hz | Truncates To | Config Key | Example |
|-----------|--------------|-----------|---------|
| 59.94 | 59 | **59=** | `59=150` |
| 29.97 | 29 | **29=** | `29=75` |
| 23.976 | 23 | **23=** | `23=50` |
| 50.0 | 50 | **50=** | `50=100` |
| 60.0 | 60 | **60=** | `60=50` |

### Your Current correction.cfg (NOW CORRECT!)

```ini
59=5    ? This will match 59.94 Hz correctly (truncate to 59)
29=5    ? This will match 29.97 Hz correctly (truncate to 29)
50=5    ? This will match 50 Hz correctly
23=0    ? This will match 23.976 Hz correctly (truncate to 23)
```

---

## Step 1: Diagnose Your Clock Drift Problem

### Enable Long-Duration Monitoring

Use your OSD to record data over extended periods:

1. **Run for 1 hour** with the stats overlay visible
2. **Record every 5 minutes**:
   - Theoretical Refresh Rate (from display mode)
   - Measured FPS
   - PPM Deviation
   - Frame drops count
   - Queue health

### Example Monitoring Schedule

```
Time      | Theoretical | Measured | PPM Dev | Drops | Notes
========================================================
00:00     | 59.9401     | 59.9412  | +18     | 0     | Baseline
00:05     | 59.9401     | 59.9385  | -27     | 0     | Slight drift
00:10     | 59.9401     | 59.9355  | -77     | 0     | Increasing drift
00:15     | 59.9401     | 59.9320  | -136    | 1     | Drift + drops!
00:20     | 59.9401     | 59.9280  | -202    | 3     | Progressive drift
...
```

### What to Look For

**Pattern 1: Progressive Negative Drift** (most common)
```
-18, -50, -100, -150, -200 ppm over time ? Clock running SLOW
Solution: Apply POSITIVE PPM correction
Example: 59=150 (means +150 ppm for 59.94 Hz)
```

**Pattern 2: Progressive Positive Drift**
```
+18, +50, +100, +150, +200 ppm over time ? Clock running FAST
Solution: Apply NEGATIVE PPM correction
Example: 59=-150 (means -150 ppm for 59.94 Hz)
```

**Pattern 3: Oscillating Drift** (PLL hunting)
```
-50, +30, -40, +35, -45 ppm ? Timing jitter
Solution: May need frame offset adjustment instead
```

---

## Step 2: Create Your `correction.cfg` File

Place this file in the **same directory as the executable**:

### File Location
```
C:\Users\bslac\vp\videoprocessor - VS2026\x64\Release\correction.cfg
```

### File Format

```ini
# PPM Correction Configuration
# Key rule: Use the TRUNCATED refresh rate (first digits before decimal)
# 59.94 ? 59, 29.97 ? 29, 23.976 ? 23
# Positive PPM = faster timing (reduces drift if clock runs slow)
# Negative PPM = slower timing (reduces drift if clock runs fast)

59=0    # For 59.94 Hz (truncate to 59)
29=0    # For 29.97 Hz (truncate to 29)
50=0    # For 50 Hz
24=0    # For 24 Hz
23=0    # For 23.976 Hz (truncate to 23)
```

### For Your Case (59.94 Hz with Observed Drift)

If you observe **consistent negative drift** (e.g., -100 to -200 ppm):

```ini
# Clock drifts slow - apply positive correction
59=150    # For 59.94 Hz
```

If you observe **consistent positive drift** (e.g., +100 to +200 ppm):

```ini
# Clock drifts fast - apply negative correction  
59=-150   # For 59.94 Hz
```

---

## Step 3: Test and Iterate

### Testing Procedure

1. **Enable Rational-Rational timing mode** (if not already)
2. **Create correction.cfg** with initial guess
3. **Run for 10 minutes** and observe:
   - Does PPM deviation **decrease**?
   - Do **frame drops cease**?
   - Is queue health **stable**?

### Interpreting Results

| Result | Action |
|--------|--------|
| PPM drift still negative/positive | Increase correction magnitude |
| PPM overcorrects opposite direction | Decrease correction magnitude |
| Oscillates around zero | Correct value found! ? |
| Still dropping frames | May be queue size issue instead |

### Example Iteration

```
Test 1: correction.cfg has "59=100"
Result: -100 ppm baseline ? -10 ppm after 1 hour
??  Still drifting negative, increase correction

Test 2: correction.cfg has "59=200"  
Result: -100 ppm baseline ? +50 ppm after 1 hour
??  Overcorrected, went positive, decrease slightly

Test 3: correction.cfg has "59=150"
Result: -100 ppm baseline ? 0 ± 10 ppm after 1 hour
? OPTIMAL! Stays centered
```

---

## Step 4: Verify with Extended Run

Once you find optimal PPM:

```
correction.cfg:
59=YOURDETERMINEDVALUE   (e.g., 59=75)
```

1. **Run for 4+ hours**
2. **Record measurements every 15 minutes**
3. **Expected result**:
   - PPM deviation stays ±20 ppm
   - No frame drops
   - Queue size stable

---

## Root Causes of Clock Drift

### Hardware Issues
- **DeckLink card clock** drifting relative to system clock
- **Display monitor** refresh rate off-spec
- **Thermal drift** (system warms up, clock changes)

### Software Issues  
- **DirectShow timing method** not ideal for your hardware
- **Frame offset** needs adjustment
- **Queue size** insufficient for your pipeline

---

## Advanced: Finding Your Specific PPM Value

If you know your **actual clock frequencies**, calculate it:

```
PPM_Error = ((Measured_Hz - Theoretical_Hz) / Theoretical_Hz) × 1,000,000

Example:
Measured:     59.9300 Hz
Theoretical:  59.9401 Hz
Difference:  -0.0101 Hz

PPM = (-0.0101 / 59.9401) × 1,000,000 = -168.5 ppm
? Apply +169 ppm correction to neutralize
```

---

## Monitoring Over Time

Create a simple log file format to track:

```
# Time, Theoretical, Measured, PPM_Dev, Drops, Queue_Max
2025-12-30 10:00:00, 59.9401, 59.9412, +18, 0, 5
2025-12-30 10:05:00, 59.9401, 59.9385, -27, 0, 6
2025-12-30 10:10:00, 59.9401, 59.9355, -77, 0, 7
```

Your OSD now provides the first 3 values! Add drop counts from the main UI.

---

## Configuration Examples

### For 59.94 Hz (NTSC Timing) - Most Common Video Frequency

```ini
# NTSC frequencies with typical DeckLink drift
# Remember: 59.94 truncates to 59 (first two digits)
59=150
29=75
```

### For Multiple Frame Rates

```ini
# Different frame rates may need different corrections
23=50      # 23.976 Hz (truncate to 23)
29=75      # 29.97 Hz (truncate to 29)
50=100     # 50 Hz
59=150     # 59.94 Hz (truncate to 59)
60=150     # 60 Hz
```

---

## Next Steps

1. ? **You have**: 4-digit precision OSD monitoring
2. ? **You now know**: Config uses truncated refresh rate (59 for 59.94)
3. ?? **Do now**: Run extended test and collect data
4. ?? **Create**: `correction.cfg` with initial guess (using correct truncated key!)
5. ?? **Iterate**: Adjust PPM value based on observed drift
6. ? **Verify**: Confirm stability over 4+ hour run

---

## Timing Mode Recommendations

For best stability with Rational-Rational:

```
Method:              DS_SSTM_RATIONAL_RATIONAL
Frame Offset:        Auto (let system calculate)
Queue Size:          16-32 frames
```

This mode is **designed for exactly this problem** - it uses pure integer math to eliminate cumulative rounding errors.

---

## Questions to Answer from Your Data

Once you've monitored for 1 hour, answer:

1. Does PPM consistently drift negative or positive?
2. At what rate does it drift (ppm per hour)?
3. When do frame drops occur - only at the end or throughout?
4. Is the drift related to system temperature (longer = warmer = more drift)?

These answers will tell you the exact PPM correction needed!

