# RATIONAL_RATIONAL Reset - Simplified Fix

## The Issue
Queue resets were causing massive repeats because the timeline state wasn't being properly cleared.

## The Solution  
**Simple and direct**: On reset, zero out everything and start fresh.

### What Gets Zeroed on Reset

```cpp
m_frameCounter = 0;              // Local frame counter
m_previousFrameCounter = 0;      // Previous frame tracking
m_frameCounterOffset = 0;        // Frame number offset (CRITICAL)
m_previousTimeStop = 0;          // Previous timestamp
m_startTimeOffset = 0;           // Time offset for CLOCK modes
m_droppedFrameCount = 0;         // Reset dropped frame counter
```

### Reset Sequence

1. **Lock the queue** (if buffered mode)
2. **Purge all frames** from queue
3. **Zero all timeline state**
4. **Release lock**
5. **Call base Reset()** for DirectShow signaling (flush + new segment)
6. **Done** - timeline is clean, next frame starts from frame 0

### Key Points

- **No ramping**: Offset is applied immediately on every frame
- **No flags**: No special `m_disablePipelineOffsetTemporarily` logic
- **No gradual re-enable**: Just apply the offset every time
- **Atomic operation**: All state zeroed while holding lock (prevents race conditions)

### Timeline After Reset

```
Reset() called:
  - Queue purged
  - m_frameCounterOffset = 0
  - m_previousTimeStop = 0
  - All state zeroed

First frame arrives (e.g., frame #5000):
  - m_frameCounterOffset == 0 ? set to 5000
  - streamFrameCounter = 5000 - 5000 = 0 ?
  - timeStart = 0 + offset ?
  - timeStop = frameDuration + offset ?

Second frame (frame #5001):
  - streamFrameCounter = 5001 - 5000 = 1 ?
  - timeStart = 16.67ms + offset ?
  - Monotonic progression continues ?
```

## Why This Works

1. **Clean Slate**: All counters and offsets zeroed
2. **First Frame Sets Offset**: Next frame naturally sets `m_frameCounterOffset` to its frame number
3. **No Race Condition**: Atomic operation under lock prevents ThreadProc corruption
4. **Consistent Timestamps**: Pipeline offset applied every frame, no exceptions
5. **DirectShow Happy**: New segment notification tells renderer to resync

## Testing

After reset, you should see:
- ? No repeats
- ? Timestamps start from ~offset value (not huge jumps)
- ? Monotonic progression (each frame's timestamp > previous)
- ? Smooth playback after reset
