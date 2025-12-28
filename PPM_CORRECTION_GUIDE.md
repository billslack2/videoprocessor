# PPM Correction Configuration File

This document explains how to use the PPM (Parts Per Million) correction feature to fine-tune timing for different refresh rates.

## Overview

The PPM correction feature allows you to apply timing adjustments per refresh rate to compensate for slight frame rate mismatches that can cause frame repeats or drops over time. This is particularly useful when using RATIONAL_RATIONAL timing mode.

## Configuration File Format

Create a file named `correction.cfg` in the same directory as the VideoProcessor executable with the following format:

```
60=5
59=5
50=5
30=5
24=0
23=0
```

### Format Rules

- **refresh_rate=ppm_value**: Left side is refresh rate (integer Hz), right side is PPM adjustment
- **Multiple entries**: Can be on same line separated by spaces, or on separate lines (recommended)
- **Comments**: Lines starting with `#` are ignored
- **Empty lines**: Ignored

### PPM Values

- **Positive values**: Make the stream faster (reduces frame repeats)
- **Negative values**: Make the stream slower (may help with dropped frames)
- **Zero values**: No correction applied
- **Range**: -1,000,000 to +1,000,000 PPM

### Refresh Rate Matching

- **Exact matching**: 60.00 Hz matches "60" 
- **Rounding**: 59.94 Hz matches "60" (closest integer)
- **Tolerance**: ±0.5 Hz tolerance for matching
- **Best match**: If multiple rates could match, closest one is used

## Examples

### Basic Configuration
```
# Standard corrections for common rates
60=5    # 60 Hz needs 5 PPM faster
50=5    # 50 Hz needs 5 PPM faster  
24=0    # 24 Hz cinema - no correction
```

### Advanced Configuration
```
# Fine-tuned corrections
60=12   # 60 Hz needs 12 PPM faster (severe repeat issue)
59=8    # 59.94 Hz needs 8 PPM faster
50=3    # 50 Hz needs only 3 PPM faster
30=-2   # 30 Hz actually needs to be 2 PPM slower
25=0    # 25 Hz no correction needed
24=0    # 24 Hz cinema standard
```

### Multiple Lines Format (Recommended)
```
# One entry per line for clarity
60=5
59=5
50=5
30=5
24=0
23=0
```

## How It Works

1. **File Loading**: When RATIONAL_RATIONAL mode is selected, the system looks for `correction.cfg`
2. **Rate Detection**: Current refresh rate is calculated from timing parameters
3. **PPM Lookup**: System finds best matching rate and applies corresponding PPM
4. **Timing Adjustment**: PPM value adjusts the rational timing calculations
5. **Consistency**: Same correction applied to both start and stop timestamps

## Usage

1. **Create correction.cfg**: Place file in VideoProcessor directory
2. **Select RATIONAL_RATIONAL**: Use this timing mode for PPM corrections
3. **Test**: Monitor for frame repeats/drops over 20-30 minutes
4. **Adjust**: Fine-tune PPM values as needed

## Stats Overlay Display

The current PPM correction is displayed in the stats overlay (toggle with Ctrl+I):

- **PPM Correction: +5**: Shows active correction of +5 PPM (faster)
- **PPM Correction: 0 (off)**: Shows no correction is being applied
- **Only shown for RATIONAL_RATIONAL mode**

The stats overlay shows:
- Current PPM value being applied
- Whether the correction comes from correction.cfg or is using default (0)
- Only displayed when using RATIONAL_RATIONAL timing mode

## Default Behavior

- **No file**: If `correction.cfg` doesn't exist, no correction is applied (0 PPM)
- **No matching rate**: If refresh rate isn't in config, no correction is applied
- **Invalid values**: Invalid lines are logged and ignored

## Debugging

Debug logs show PPM loading and application:

```
PPMCorrectionLoader: Loading correction.cfg
PPMCorrectionLoader: 60 Hz = 5 PPM
LoadPPMCorrections: 59.940 Hz - applying 5 PPM correction (matched 60 Hz)
Reset(): RATIONAL_RATIONAL mode active - using correction.cfg PPM:
  PPM adjustment: 5 (from correction.cfg)
  Trim ratio: 999995/1000000 = 99.999500%
```

## Recommended Starting Values

For most setups experiencing frame repeats:

```
60=5
59=5
50=5
30=5
24=0
23=0
```

Start with these values and adjust based on long-term testing results.