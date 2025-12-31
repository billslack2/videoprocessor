# P010 Conversion Method Selection and Core Control

## Overview
Updated `CV210toP010VideoFrameFormatter` to support:
1. **Method Selection** - Choose conversion algorithm (AUTO, SIMD, OPTIMIZED, STANDARD)
2. **Core Count Control** - Set min/max CPU cores for threading

---

## Conversion Methods

### AUTO (Default)
Automatically selects the best method based on:
- CPU features (AVX2 detection)
- Frame size (720p+ uses threading)
- Falls back gracefully if AVX2 unavailable

### SIMD
- **Threaded AVX2 SIMD conversion**
- Best performance for 720p and higher
- Uses configured min/max core counts
- Requires: AVX2 CPU support

### OPTIMIZED
- **Non-threaded optimized scalar**
- Good performance for smaller frames
- No threading overhead
- Works on all CPUs

### STANDARD
- **Baseline scalar implementation**
- Reference quality
- Predictable, simple path
- Useful for validation/testing

---

## Configuration API

### Setting Conversion Method
```cpp
formatter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::SIMD);
// or
formatter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::AUTO);
```

### Setting Core Count
```cpp
// Minimum cores (default: 2)
formatter.SetMinCoreCount(4);   // Force at least 4 cores

// Maximum cores (default: 0 = auto, which leaves 2 for OS)
formatter.SetMaxCoreCount(6);   // Use maximum 6 cores
```

### Example: Force specific core count
```cpp
CV210toP010VideoFrameFormatter converter;
converter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::SIMD);
converter.SetMinCoreCount(2);
converter.SetMaxCoreCount(4);   // Force 2-4 cores regardless of CPU
```

---

## Core Count Behavior

### Default (maxCoreCount = 0, AUTO mode)
```
Available Cores: 8
Result: 8 - 2 = 6 cores used for conversion
(leaves 2 cores for OS/UI)
```

### With Configured Min/Max
```
Available Cores: 8
MinCores: 2, MaxCores: 4
Result: 4 cores used for conversion
(respects configured maximum)
```

### Edge Cases Handled
- ? minCores > maxCores (auto-corrected)
- ? maxCores > available cores (capped at hardware cores)
- ? minCores = 1 (valid, no minimum enforced)
- ? Zero cores requested (silently corrected to 1)
- ? Threading disabled for sub-720p frames

---

## Implementation Details

### Changes to Header (CV210toP010VideoFrameFormatter.h)

```cpp
enum class ConversionMethod
{
    AUTO,           // Auto-select best method
    SIMD,           // Threaded AVX2 SIMD
    OPTIMIZED,      // Non-threaded scalar optimized
    STANDARD,       // Standard scalar baseline
};

// Configuration members
ConversionMethod m_conversionMethod = ConversionMethod::AUTO;
uint32_t m_minCoreCount = 2;    // Minimum cores (default: 2)
uint32_t m_maxCoreCount = 0;    // Maximum cores (0=auto)

// Public setters
void SetConversionMethod(ConversionMethod method);
void SetMinCoreCount(uint32_t minCores);
void SetMaxCoreCount(uint32_t maxCores);
```

### GetMaxThreadCount() Logic

```
1. Query hardware_concurrency()
2. Apply m_minCoreCount floor
3. If m_maxCoreCount == 0 (auto):
   - Use (cores - 2) to preserve OS threads
4. Else:
   - Clamp to m_maxCoreCount
5. Final cap at 8 threads
```

---

## Backward Compatibility

? **Fully backward compatible**
- Default behavior unchanged (AUTO mode)
- Existing code requires no modifications
- New configuration is optional

---

## Performance Tuning Guide

### For Maximum Performance
```cpp
converter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::SIMD);
converter.SetMaxCoreCount(0);  // Auto-detect, leave 2 for OS
```

### For Consistent Predictable Performance
```cpp
converter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::OPTIMIZED);
// Single-threaded, consistent latency
```

### For Testing/Validation
```cpp
converter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::STANDARD);
// Baseline reference implementation
```

### For Limited Resource Environments
```cpp
converter.SetConversionMethod(CV210toP010VideoFrameFormatter::ConversionMethod::SIMD);
converter.SetMaxCoreCount(2);  // Cap at 2 threads even on 8-core system
```

---

## Thread Safety

?? **Configuration must be set BEFORE calling FormatVideoFrame()** 

Thread pool is initialized on first frame, so configuration changes after that won't affect the running pool.

```cpp
// Correct order:
formatter.SetConversionMethod(...);  // Configure first
formatter.SetMaxCoreCount(...);
// Then process frames:
formatter.FormatVideoFrame(frame, buffer);

// Incorrect (won't change active pool):
formatter.FormatVideoFrame(frame, buffer);
formatter.SetMaxCoreCount(2);  // Too late, pool already initialized
```

---

## Testing the Configuration

Use the stats overlay to verify:
- Monitor "Conv Time" to measure performance
- Check "10s Avg/Max" for consistency
- Verify "Method" selection in output (if added to stats)
- Compare timing between different ConversionMethod settings
