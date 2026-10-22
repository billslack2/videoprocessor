#pragma once
#include <DisplayRasterInfo.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

// Strings published by the render thread, with no driver calls or waits behind
// Present on the OSD thread. Raster annotations are deliberately never cached.
class OutputOsdTelemetryCache
{
public:
    uint64_t OutputGeneration() const
    { std::lock_guard<std::mutex> lock(mutex_); return outputGeneration_; }
    bool HasOutput() const
    { std::lock_guard<std::mutex> lock(mutex_); return !output_.empty(); }
    void InvalidateOutput()
    { std::lock_guard<std::mutex> lock(mutex_); ++outputGeneration_; output_.clear(); }
    void InvalidateLut()
    { std::lock_guard<std::mutex> lock(mutex_); lut_.clear(); }
    void Retire()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retired_ = true; ++outputGeneration_; output_.clear(); lut_.clear();
    }
    bool PublishOutput(uint64_t generation, std::string value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (retired_ || generation != outputGeneration_) return false;
        output_ = std::move(value); return true;
    }
    void PublishLut(std::string value)
    { std::lock_guard<std::mutex> lock(mutex_); if (!retired_) lut_ = std::move(value); }
    std::string ReadLut() const
    { std::lock_guard<std::mutex> lock(mutex_); return lut_; }

    // This is the renderer getter's production assembly path. A missing current
    // value is an authoritative empty snapshot, never a request to reuse UI text.
    // Formatter runs only after a nonblocking acquisition of renderMutex, and
    // returns empty while initialization or deferred mode changes are pending.
    template<class Formatter, class RasterReader>
    std::string ReadOutput(std::mutex& renderMutex, Formatter&& formatter,
        RasterReader&& readRaster)
    {
        {
            std::unique_lock<std::mutex> renderLock(renderMutex, std::try_to_lock);
            if (renderLock.owns_lock())
            {
                const auto generation = OutputGeneration();
                auto value = formatter();
                if (!value.empty()) PublishOutput(generation, std::move(value));
            }
        }
        // Read both small caches together. The raster reader does not take the
        // render lock, and no raster writer acquires this cache's mutex.
        std::lock_guard<std::mutex> lock(mutex_);
        if (output_.empty()) return {};
        return output_ + DisplayRasterInfo::FormatDetails(readRaster());
    }
private:
    mutable std::mutex mutex_;
    uint64_t outputGeneration_ = 0;
    bool retired_ = false;
    std::string output_, lut_;
};
