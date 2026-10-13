#pragma once

// Per-frame source metadata. Values are nits, independently of VP's output target.
struct DolbyVisionLuminanceStatus
{
    bool tunnelActive = false;
    bool available = false;
    bool sourceRangeFromMetadata = false;
    bool sceneAvailable = false;
    float sourceMin = 0;
    float sourceMax = 0;
    float sceneMax = 0;
    float sceneAvg = 0;
};
