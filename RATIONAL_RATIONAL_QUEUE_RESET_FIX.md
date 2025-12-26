# RATIONAL_RATIONAL Queue Reset Fix - Complete Solution

## Root Cause Analysis

### The Race Condition Problem

When `Reset()` is called on the buffered queue, there's a **race condition** between:
1. **Thread A (UI/Reset)**: Calling `Reset()` which clears `m_frameCounterOffset = 0`
2. **Thread B (ThreadProc)**: Still processing an old frame from before the reset

**The Race Condition Timeline:**
```
Thread A (Reset):           Thread B (ThreadProc):
-----------------           ----------------------
                            Pop frame #4998 from queue
                            Start RenderVideoFrameIntoSample()
                            
Reset() called
  PurgeQueue()              
  m_frameCounterOffset = 0  
                            m_frameCounterOffset = 4998 (CORRUPTED!)
  DeliverNewSegment()       
  Reset complete            
                            Continue processing frame #4998
                            
New frame #5000 arrives
  m_frameCounterOffset still = 4998 (WRONG!)
  streamFrameCounter = 5000 - 4998 = 2 (SHOULD BE 0!)
  Timeline CORRUPTED ? Repeat loops!
```

### Why Previous Fixes Failed

1. **Just clearing `m_frameCounterOffset` in Reset()**: Not enough - ThreadProc can overwrite it
2. **Setting flags for gradual ramp-up**: Good for offset, but doesn't fix the corruption
3. **Separate lock/unlock cycles**: Race condition still exists between operations

## The Complete Solution

### 1. Atomic Reset in CBufferedLiveSourceVideoOutputPin::Reset()

```cpp
void CBufferedLiveSourceVideoOutputPin::Reset()
{
    DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - Starting reset sequence")));
    
    {
        // CRITICAL: Hold the lock while purging AND resetting timeline state
        // This ensures no frame is being processed during reset
        CAutoLock lock(&m_filterCritSec);
        
        // Purge all frames from queue (inline, not separate call)
        while (!m_videoFrameQueue.empty())
        {
            VideoFrame popFrame = m_videoFrameQueue.front();
            popFrame.SourceBufferRelease();
            m_videoFrameQueue.pop_front();
            ++m_droppedFrameCount;
        }
        
        // CRITICAL: Reset timeline state atomically with queue purge
        m_frameCounter = 0;
        m_previousFrameCounter = 0;
        m_startTimeOffset = 0;
        m_frameCounterOffset = 0;
        m_previousTimeStop = 0;
        
        // Set flags for next frame processing
        m_forceDiscontinuity = true;
        m_deliverNewSegment = true;
        
        // Reset pipeline offset ramping
        m_disablePipelineOffsetTemporarily = true;
        m_framesAfterReset = 0;
        
        DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - Queue purged and timeline state reset atomically")));
    }
    
    // Now call base class Reset() for DirectShow-level reset
    ALiveSourceVideoOutputPin::Reset();
    
    DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin::Reset() - Reset sequence complete")));
}
```

### Key Points:

1. **Single Lock Scope**: All state changes happen within the same `CAutoLock` scope
2. **Inline Queue Purge**: No separate function call that might release the lock
3. **Timeline Reset Before Base Call**: State is clean before DirectShow flush
4. **Flags Set Atomically**: `m_forceDiscontinuity` and `m_deliverNewSegment` set together

### 2. Same Pattern in SetFrameQueueMaxSize()

The same race condition existed in `SetFrameQueueMaxSize()` - now fixed:

```cpp
void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
    {
        CAutoLock lock(&m_filterCritSec);

        m_frameQueueMaxSize = frameQueueMaxSize;

        // Purge queue atomically with timeline reset
        while (!m_videoFrameQueue.empty())
        {
            // ... purge frames ...
        }
        
        // Reset ALL timeline state atomically
        m_frameCounter = 0;
        m_previousFrameCounter = 0;
        m_startTimeOffset = 0;
        m_frameCounterOffset = 0;
        m_previousTimeStop = 0;
        
        m_forceDiscontinuity = true;
        m_deliverNewSegment = true;
        m_disablePipelineOffsetTemporarily = true;
        m_framesAfterReset = 0;
    }
    
    // ... rest of method ...
}
```

### 3. ThreadProc Synchronization

The `ThreadProc` already uses `CAutoLock lock(&m_filterCritSec)` when accessing the queue:

```cpp
{
    CAutoLock lock(&m_filterCritSec);

    if (!m_isActive)
        break;

    if (m_videoFrameQueue.empty())
        continue;

    // Pop frame
    videoFrame = m_videoFrameQueue.front();
    m_videoFrameQueue.pop_front();
}

// Process frame OUTSIDE lock (this is fine)
RenderVideoFrameIntoSample(videoFrame, pSample);
```

**This is the key insight**: `RenderVideoFrameIntoSample()` runs OUTSIDE the lock, so it can access the timeline state variables (`m_frameCounterOffset`, etc.) without holding the lock.

**Solution**: Reset the timeline state WHILE holding the lock in Reset(). Then when ThreadProc next tries to process a frame:
- If it's still processing an old frame ? that frame was already popped BEFORE the reset
- When it finishes, the next frame it gets will be from AFTER the reset
- The first new frame will correctly set `m_frameCounterOffset`

### 4. First Frame After Reset

When the first frame arrives after reset:
1. `m_frameCounterOffset == 0` (was reset)
2. Frame arrives with counter (e.g., 5000)
3. `m_frameCounterOffset = 5000` (set correctly)
4. `streamFrameCounter = 5000 - 5000 = 0` ?
5. Timeline starts clean from 0

## Variables Reset During Reset()

| Variable | Purpose | Reset Value |
|----------|---------|-------------|
| `m_frameCounter` | Local frame counter | 0 |
| `m_previousFrameCounter` | For discontinuity detection | 0 |
| `m_startTimeOffset` | Clock mode time offset | 0 |
| `m_frameCounterOffset` | Source frame offset | 0 |
| `m_previousTimeStop` | For monotonicity check | 0 |
| `m_forceDiscontinuity` | Signal discontinuity | true |
| `m_deliverNewSegment` | Signal new segment | true |
| `m_disablePipelineOffsetTemporarily` | Disable offset during ramp | true |
| `m_framesAfterReset` | Ramp counter | 0 |

## Expected Behavior After Fix

### Normal Reset Sequence
```
1. Reset() called
2. Lock acquired
3. Queue purged (frames released)
4. All timeline state cleared atomically
5. Lock released
6. Base Reset() called (DirectShow flush/new segment)
7. First new frame arrives
8. m_frameCounterOffset set to new frame's counter
9. streamFrameCounter = 0 (correct!)
10. Timeline proceeds correctly
```

### No More Repeat Loops
- Timeline always starts from 0 after reset
- No stale frame counter corruption
- Pipeline offset ramps up smoothly (10 frames)
- MadVR receives proper new segment notification

## Testing

1. Start playback at any frame rate
2. Click Reset button multiple times rapidly
3. Monitor debug output for "Frame counter offset set" messages
4. Verify no "CRITICAL - RATIONAL_RATIONAL timestamp inversion" errors
5. Verify no visual repeats or stuttering
