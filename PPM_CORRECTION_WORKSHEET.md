# PPM CORRECTION DIAGNOSIS WORKSHEET
# 
# Use this to systematically find your optimal PPM correction values
# Based on the 4-digit precision OSD you just implemented

## ? CORRECT: Config Key Matching

**Your refresh rate 59.94 Hz truncates to 59 (first two digits before decimal)!**

In `correction.cfg`, you MUST use:
```
59=150    ? CORRECT (59.94 truncates to 59)
60=150    ? WRONG (that's for 60 Hz, not 59.94 Hz)
```

Other common rates (use TRUNCATION, not rounding):
- 59.94 Hz ? key **59** (not 60!)
- 29.97 Hz ? key **29** (not 30!)
- 23.976 Hz ? key **23** (not 24!)

---

## Your Current Situation
- **Current Config**: 59=5 ppm (very conservative)
- **Observed Problem**: "Drops and clock deviation sometimes"
- **Goal**: Find PPM values that eliminate drops over 4+ hour runs

---

## PHASE 1: EXTENDED MONITORING (1-2 hours)

### Setup
1. Use CURRENT correction.cfg (59=5)
2. Start recording PPM Dev from your OSD every 5 minutes
3. Note when/if drops occur

### Recording Table

```
Time (HH:MM) | Measured FPS | PPM Dev | Drops | Queue Max | Notes
=================================================================
00:00        | 59.9412      | +18     | 0     | 5         | Start
00:05        | 59.9385      | -27     | 0     | 6         |
00:10        | 59.9355      | -77     | 0     | 7         |
00:15        | 59.9320      | -136    | 1     | 8         | First drop!
00:20        | 59.9280      | -202    | 3     | 9         |
[... continue every 5 min ...]
```

### After 1 Hour - Analyze the Trend

Copy your data into a spreadsheet and look for:

1. **Starting PPM**: What's the value at 00:00?
2. **Ending PPM**: What's the value at 01:00?
3. **Drift Rate**: (Ending - Starting) / 60 minutes = ppm/minute
4. **When drops start**: At what PPM level do drops begin?

**Example Analysis:**
```
Starting:     +18 ppm
Ending:       -202 ppm  
Change:       -220 ppm over 60 minutes
Drift Rate:   -3.67 ppm/minute
Drops Start:  When PPM reached -136 (at ~16 minutes)

Interpretation:
- Your clock runs SLOW (negative drift)
- Needs POSITIVE correction
- Current 5 ppm is NOWHERE NEAR ENOUGH
- Suggest: Try 59=150-200 next
```

---

## PHASE 2: ADJUSTMENT & RE-TEST (30 min each attempt)

### Test 1: Current (5 ppm) - BASELINE
- Record: 30-minute data
- Expected: See the same drift pattern as Phase 1

### Test 2: Increase Correction by 3-5x
```
Edit correction.cfg:
OLD: 59=5
NEW: 59=50    (or 59=100 if drift was severe)
```
- Restart application
- Record: 30-minute data
- Check: Is PPM deviation now SMALLER over time?

### Test 3: Fine-tune
Based on Test 2 results:
- If still negative drift: Increase to 59=75 or 59=100
- If swung positive: Back to 59=25
- If centered: You found it! ?

---

## PHASE 3: VALIDATION (4+ hour run)

Once you find optimal PPM:
```
correction.cfg:
59=YOURDETERMINEDVALUE   (e.g., 59=75)
```

1. **Run for 4+ hours**
2. **Record every 15 minutes**
3. **Success criteria**:
   - PPM stays ±20 throughout
   - No frame drops
   - Queue size doesn't exceed 10-12

---

## QUICK REFERENCE: PPM VALUE GUIDE

Based on observed drift patterns from Phase 1:

| Drift Pattern | Starting PPM | Ending PPM | Test PPM Value |
|---------------|--------------|-----------|-----------------|
| Drops early, severe drift | -50 | -250 | 59=200 |
| Drops mid-session | -20 | -150 | 59=150 |
| Drops late session | -10 | -100 | 59=100 |
| Slow drift, rare drops | -5 | -50 | 59=50 |
| Slight negative drift | -5 | -20 | 59=20 |
| Already pretty stable | 0 | -10 | 59=5 (current) |

---

## Data Collection Template

Create a CSV file to track results:

```csv
Time,Measured_FPS,PPM_Dev,Drops,Queue_Max,Temperature_if_available
2025-12-30 10:00:00,59.9412,18,0,5,35C
2025-12-30 10:05:00,59.9385,-27,0,6,35C
2025-12-30 10:10:00,59.9355,-77,0,7,36C
2025-12-30 10:15:00,59.9320,-136,1,8,37C
...
```

Then chart it in Excel/Sheets to visualize the drift trajectory.

---

## Key Questions to Answer

After Phase 1, you'll know:

1. ? **How fast does your clock drift?** (ppm/minute)
2. ? **At what PPM level do drops start?**
3. ? **Is drift thermal** (increases as system warms)?
4. ? **What correction magnitude is needed?** (50? 100? 200?)

These answers give you the EXACT PPM value to enter in correction.cfg (using the correct truncated key: 59 for 59.94 Hz)!

