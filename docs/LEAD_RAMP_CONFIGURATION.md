# Lead Ramp Configuration Guide

## Overview

The lead ramp is a timing mechanism that smoothly ramps the lead offset (buffer time) from 0 to a target value over a configurable duration. This allows MadVR and other renderers to establish proper frame buffering without causing initial timing glitches.

## Changes Made (Frame-Based ? Time-Based)

### Previous Implementation (Frame-Based)
- Lead ramp was hardcoded to 500 frames
- Duration depended on frame rate (fast at 60fps, slow at 24fps)
- Not configurable

### New Implementation (Time-Based)
- Lead ramp uses **actual elapsed time** measured in milliseconds
- Default duration: **5000 ms (5 seconds)** - configurable via API
- Works consistently across all frame rates
- Uses wall-clock time instead of frame counting

## Configuration API

### Setting Lead Ramp Duration

```cpp
// Set lead ramp to 10 seconds (10000 milliseconds)
m_videoOutputPin->SetLeadRampDurationMs(10000);

// Set lead ramp to 3 seconds
m_videoOutputPin->SetLeadRampDurationMs(3000);

// Set lead ramp to 1 second
m_videoOutputPin->SetLeadRampDurationMs(1000);

// Retrieve current setting
uint64_t currentDurationMs = m_videoOutputPin->GetLeadRampDurationMs();
```

## Default Behavior

- **Default Duration**: 5000 ms (5 seconds)
- **Lead Time Target**: 20 ms (2,000,000 in 100ns ticks)
- **Ramp Profile**: Linear interpolation from 0% to 100% over the configured duration

## How It Works

### Timeline Example (10-second lead ramp)

```
Frame Timeline:
|--0s--|--1s--|--2s--|--3s--|--4s--|--5s--|--6s--|--7s--|--8s--|--9s--|--10s-|
|  0% |  10% |  20% |  30% |  40% |  50% |  60% |  70% |  80% |  90% | 100% |
^                                                                            ^
Ramp starts                                                          Ramp complete
(lead offset = 0)                                              (lead offset = 20ms)
```

### Calculation

At any given time during the ramp:

```
elapsed_ms = current_time - ramp_start_time
progress_percent = (elapsed_ms / ramp_duration_ms) * 100
lead_offset = target_lead_offset * (progress_percent / 100)

Example after 2.5 seconds (with 5-second ramp):
progress = (2500 / 5000) * 100 = 50%
lead_offset = 20ms * 0.5 = 10ms
```

## When to Adjust the Ramp Duration

### Shorter Duration (1-2 seconds)
- ? Faster convergence to full buffering
- ? More abrupt timing changes that may cause jitter
- Use case: Displays requiring faster startup

### Longer Duration (5-10 seconds)
- ? Smoother, more gradual lead offset application
- ? Less jarring to timing synchronization
- Use case: General playback, real-time capture (default)

### Example Durations by Use Case

```cpp
// Real-time video capture (default - smooth, gradual)
pin->SetLeadRampDurationMs(5000);  // 5 seconds

// Fast startup requirement
pin->SetLeadRampDurationMs(2000);  // 2 seconds

// Ultra-smooth startup (longer buffer time)
pin->SetLeadRampDurationMs(10000); // 10 seconds

// Quick testing/validation
pin->SetLeadRampDurationMs(1000);  // 1 second
```

## Behavior on Reset

When the stream resets (HDMI resync, renderer restart, etc.):
- ? User configuration (duration) is **preserved**
- ? Ramp timing is **reset** (starts over from 0%)

This ensures the next stream uses the same smooth ramp profile as configured.

## Internal State

### Member Variables
```cpp
uint64_t m_leadRampDurationMs;     // User-configurable duration (milliseconds)
uint64_t m_leadRampStartTimeMs;    // Timestamp when current ramp started
bool m_leadRampActive;             // Tracks if ramp has been initialized
```

### Behavior
- `m_leadRampDurationMs` is **only set** by `SetLeadRampDurationMs()` and never reset
- `m_leadRampStartTimeMs` and `m_leadRampActive` are reset on stream resets
- The ramp initializes on first call to `GetRampedLeadTime()`

## Logging and Debugging

The ramp provides periodic logging (every 500ms during ramp):

```
Lead time ramp: 500/5000 ms elapsed -> lead 2.5/20 ms
Lead time ramp: 1000/5000 ms elapsed -> lead 5.0/20 ms
Lead time ramp: 5000/5000 ms elapsed -> lead 20.0/20 ms (complete)
```

At ramp initialization:

```
Initializing lead ramp - duration=5000 ms, target=20.00 ms
```

## Integration with DirectShow Modes

The lead ramp applies to the following timing modes:
- `DS_SSTM_RATIONAL_RATIONAL` - Rational timing
- `DS_SSTM_CLOCK_SMART` - Smart clock-based
- `DS_SSTM_CLOCK_SMART2` - Enhanced smart clock
- `DS_SSTM_CLOCK_THEO` - Clock with theoretical duration
- `DS_SSTM_CLOCK_CLOCK` - Clock-based duration

Other modes (e.g., `DS_SSTM_CLOCK_RATIONAL`) may have their own lead offset handling.

## Migration from Frame-Based (if applicable)

If you have code that was using frame counts:

```cpp
// OLD CODE (frame-based, removed):
// Ramped over hardcoded 500 frames
// Duration: 500 / fps seconds

// NEW CODE (time-based):
// Configure once during initialization
pin->SetLeadRampDurationMs(5000);  // 5 seconds
// Works consistently regardless of frame rate
```

## Technical Details

### Wall-Clock Time Measurement
- Uses `GetWallClockTime()` which returns time in 100ns ticks
- Converted to milliseconds for configuration and calculations
- Immune to frame rate variations

### Precision
- Linear interpolation maintains good precision
- Integer math avoids floating-point drift
- Rounding ensures consistent behavior at ramp boundaries

### Thread Safety
- `m_leadRampDurationMs` can be safely modified anytime
- `m_leadRampStartTimeMs` and `m_leadRampActive` are only modified in frame delivery path
