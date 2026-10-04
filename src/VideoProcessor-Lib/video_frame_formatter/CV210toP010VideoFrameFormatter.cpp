/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>
#include "CV210toP010VideoFrameFormatter.h"
#include <CpuFeatures.h>
#include <iomanip>  // For std::setprecision
#include <ConfigFile.h>

#include "CV210toP010VideoFrameFormatter.Common.h"


// =====================================================================
// Constructor / Destructor
// =====================================================================
CV210toP010VideoFrameFormatter::CV210toP010VideoFrameFormatter()
{
    // Thread pool will be initialized lazily on first large frame
    LoadConfigurationFile();
}

CV210toP010VideoFrameFormatter::~CV210toP010VideoFrameFormatter()
{
    ShutdownThreadPool();
}

// =====================================================================
// Configuration File Loading
// =====================================================================
void CV210toP010VideoFrameFormatter::LoadConfigurationFile()
{
    LoadConfigurationFile(ConfigFile::DEFAULT_FILENAME);
}

bool CV210toP010VideoFrameFormatter::LoadConfigurationFile(const char* filename)
{
    ShutdownThreadPool();
    m_conversionMethod = ConversionMethod::AUTO;
    m_chromaDownsampling = ChromaDownsampling::AVERAGE;
    m_minCoreCount = 1;
    m_maxCoreCount = 1;
    m_actualMaxThreads = 0;
    m_cpuFeaturesChecked = false;
    m_threadPoolUnavailable = false;

    ConfigFile unifiedConfig;
    if (unifiedConfig.Load(filename))
    {
        if (!unifiedConfig.GetWarnings().empty())
        {
            for (const auto& warning : unifiedConfig.GetWarnings())
            {
                DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: Invalid VideoProcessor.cfg syntax: %S"), warning.c_str()));
            }

            DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: VideoProcessor.cfg has syntax errors - using defaults")));
            return false;
        }

        const bool targetConfiguration =
            unifiedConfig.HasSection("directshow.conversion");
        const char* conversionSection = targetConfiguration ?
            "directshow.conversion" : "p010_conversion";
        if (!unifiedConfig.HasSection(conversionSection))
        {
            return true;
        }

        const auto* conversionSettings = unifiedConfig.GetSectionValues(conversionSection);
        if (conversionSettings)
        {
            for (const auto& setting : *conversionSettings)
            {
                try
                {
                    if (setting.first == (targetConfiguration ?
                        "conversion_method" : "conversionmethod"))
                    {
                        const std::string conversionMethod = ConfigFile::NormalizeName(setting.second);
                        if (conversionMethod == "auto")
                            m_conversionMethod = ConversionMethod::AUTO;
                        else if (conversionMethod == "simd")
                            m_conversionMethod = ConversionMethod::SIMD;
                        else if (conversionMethod == "optimized")
                            m_conversionMethod = ConversionMethod::OPTIMIZED;
                        else if (conversionMethod == "standard")
                            m_conversionMethod = ConversionMethod::STANDARD;
                        else
                            DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: Invalid ConversionMethod in VideoProcessor.cfg: %S"), setting.second.c_str()));
                    }
                    else if (targetConfiguration && setting.first == "chroma_downsampling")
                    {
                        const std::string policy = ConfigFile::NormalizeName(setting.second);
                        if (policy == "average")
                            m_chromaDownsampling = ChromaDownsampling::AVERAGE;
                        else if (policy == "legacy")
                            m_chromaDownsampling = ChromaDownsampling::LEGACY;
                        else if (policy == "advanced")
                            m_chromaDownsampling = ChromaDownsampling::ADVANCED;
                        else
                            DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: Invalid chroma_downsampling: %S; using AVERAGE"), setting.second.c_str()));
                    }
                    else if (setting.first == (targetConfiguration ?
                        "min_core_count" : "mincorecount"))
                    {
                        uint32_t minCores = std::stoul(setting.second);
                        m_minCoreCount = std::max(1u, minCores);
                    }
                    else if (setting.first == (targetConfiguration ?
                        "max_core_count" : "maxcorecount"))
                    {
                        uint32_t maxCores = std::stoul(setting.second);
                        m_maxCoreCount = maxCores;
                    }
                    else
                    {
                        DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: Unknown VideoProcessor.cfg [p010_conversion] key: %S"), setting.first.c_str()));
                    }
                }
                catch (const std::exception&)
                {
                    DbgLog((LOG_WARNING, 1, TEXT("CV210toP010VideoFrameFormatter: Invalid VideoProcessor.cfg [p010_conversion] value: %S=%S"),
                        setting.first.c_str(), setting.second.c_str()));
                }
            }
        }

        return true;
    }

    return false;
}

// =====================================================================
// Thread Pool Management - Blocking work and completion notifications
// =====================================================================
bool CV210toP010VideoFrameFormatter::InitializeThreadPool()
{
    if (m_threadsInitialized)
        return true;
    if (m_threadPoolUnavailable)
        return false;

    try
    {
        const uint32_t threadCount = GetActualMaxThreads();
        m_threadContexts = std::make_unique<ThreadContext[]>(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i)
        {
            m_threadContexts[i].thread = std::thread(ThreadWorkerStatic, this, i);
            ++m_startedThreadCount;
        }
        m_threadsInitialized = true;
        return true;
    }
    catch (...)
    {
        // Clean up even if only some helpers were started. A failed pool must
        // not terminate the noexcept conversion path or retry every frame.
        ShutdownThreadPool();
        m_threadPoolUnavailable = true;
        return false;
    }
}

void CV210toP010VideoFrameFormatter::ShutdownThreadPool()
{
    // The caller has finished its last frame before reload/destruction.
    // Count started threads rather than configured threads for partial startup.
    for (uint32_t i = 0; i < m_startedThreadCount; ++i)
    {
        ThreadContext& ctx = m_threadContexts[i];
        {
            std::lock_guard<std::mutex> lock(ctx.mutex);
            ctx.state = 2;
        }
        ctx.workReady.notify_one();
    }
    for (uint32_t i = 0; i < m_startedThreadCount; ++i)
    {
        if (m_threadContexts[i].thread.joinable())
            m_threadContexts[i].thread.join();
    }
    m_threadContexts.reset();
    m_startedThreadCount = 0;
    m_threadsInitialized = false;
}

void CV210toP010VideoFrameFormatter::ThreadWorkerStatic(CV210toP010VideoFrameFormatter* self, uint32_t threadIndex)
{
    ThreadContext& ctx = self->m_threadContexts[threadIndex];
    std::unique_lock<std::mutex> lock(ctx.mutex);
    for (;;)
    {
        // Predicate handles spurious wakes and work published before we wait.
        ctx.workReady.wait(lock, [&ctx] { return ctx.state != 0; });
        if (ctx.state == 2)
            return;

        const ThreadWorkItem work = ctx.work;
        lock.unlock();
        self->ProcessLineSegment(work.srcData, work.srcStride,
            work.dstY, work.dstUV, work.width, work.startLine, work.endLine);
        lock.lock();
        ctx.state = 0;
        // Publishing completion under the mutex makes pixel writes visible
        // before the caller returns the output buffer to the renderer.
        ctx.workDone.notify_one();
    }
}

// =====================================================================
// Process a segment of line pairs (used by threads and main thread)
// =====================================================================
void CV210toP010VideoFrameFormatter::ProcessLineSegment(
    const uint8_t* srcData, uint32_t srcStride,
    uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine) noexcept
{
    if (m_chromaDownsampling == ChromaDownsampling::ADVANCED)
    {
        ProcessAdvancedSegment(srcData, srcStride, dstY, dstUV, width, startLine, endLine, true);
        return;
    }
    if (m_chromaDownsampling == ChromaDownsampling::AVERAGE)
        ProcessLineSegmentImpl<true>(srcData, srcStride, dstY, dstUV, width, startLine, endLine);
    else
        ProcessLineSegmentImpl<false>(srcData, srcStride, dstY, dstUV, width, startLine, endLine);
}

// Template constants eliminate the unused chroma work in Release builds.


// =====================================================================
// Threaded conversion - divides frame into segments for parallel processing
// =====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Threaded(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (!InitializeThreadPool())
        return ConvertV210ToP010_SIMD(srcData, srcStride, dstY, dstUV, width, height);

    const uint32_t threadCount = m_startedThreadCount;

    // Calculate line pairs per thread (must be even for P010 4:2:0)
    const uint32_t totalLinePairs = height / 2;
    const uint32_t linePairsPerThread = totalLinePairs / (threadCount + 1); // +1 for main thread
    const uint32_t linesPerThread = linePairsPerThread * 2;

    // Distribute work to worker threads
    uint32_t currentLine = 0;
    for (uint32_t i = 0; i < threadCount; i++)
    {
        ThreadContext& ctx = m_threadContexts[i];

        {
            std::lock_guard<std::mutex> lock(ctx.mutex);
            ctx.work = { srcData, srcStride, dstY, dstUV, width,
                currentLine, currentLine + linesPerThread };
            ctx.state = 1;
        }
        currentLine += linesPerThread;
        ctx.workReady.notify_one();
    }

    // Main thread processes the remaining lines
    ProcessLineSegment(srcData, srcStride, dstY, dstUV, width, currentLine, height);

    // Usually already complete; otherwise park until the helper publishes
    // its output. No spinning while another thread is descheduled.
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        ThreadContext& ctx = m_threadContexts[i];
        std::unique_lock<std::mutex> lock(ctx.mutex);
        ctx.workDone.wait(lock, [&ctx] { return ctx.state == 0; });
    }

    return true;
}

// CPU feature detection


bool CV210toP010VideoFrameFormatter::HasAVX2MemoryOps() const
{
    CheckCPUFeatures();
    return m_hasAVX2MemoryOps;
}

uint32_t CV210toP010VideoFrameFormatter::GetActualMaxThreads() const
{
    if (m_actualMaxThreads == 0)
    {
        CheckCPUFeatures();
    }
    return m_actualMaxThreads;
}

void CV210toP010VideoFrameFormatter::LogPerformanceStats() const
{
#ifdef _DEBUG
    if (m_totalConversions > 0)
    {
        const double avgTimeUs = static_cast<double>(m_totalConversionTimeUs) / m_totalConversions;
        DbgLog((LOG_TRACE, 1, TEXT("V210->P010 Performance: %llu frames, Avg %.1f us"),
                m_totalConversions, avgTimeUs));
    }
#endif
}

// =====================================================================
void CV210toP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
    if (!videoState)
        throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    if (m_height == 0 || (m_height & 1) != 0)
        throw std::runtime_error("P010 output requires a positive, even input height");

    const uint32_t origWidth = videoState->displayMode->FrameWidth();
    if (origWidth == 0 || (origWidth & 1) != 0)
        throw std::runtime_error("P010 output requires a positive, even width");

    m_stride = videoState->BytesPerRow();
    const uint32_t packedBytes =
        ((origWidth + PIXELS_PER_PACK - 1) / PIXELS_PER_PACK) * BYTES_PER_PACK;
    if (m_stride < packedBytes)
        throw std::runtime_error("v210 input row is smaller than the active frame width");

    m_width = origWidth;
}

// =====================================================================
bool CV210toP010VideoFrameFormatter::FormatVideoFrame(
    const VideoFrame& inFrame,
    BYTE* outBuffer)
{
    const auto startTime = GetWallClockTime();

    const uint32_t pixels = m_height * m_width;
    const uint32_t yPlaneSize = pixels * sizeof(uint16_t);

    uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
    uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + yPlaneSize);

    const bool conversionSuccess = ConvertV210ToP010(
        static_cast<const uint8_t*>(inFrame.GetData()),
        m_stride,
        dstY,
        dstUV,
        m_width,
        m_height
    );

    const auto endTime = GetWallClockTime();
    const uint64_t conversionTime = (endTime - startTime) / 10;
    LogConversionPerformance(conversionTime, conversionSuccess);

    return conversionSuccess;
}

LONG CV210toP010VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;
    return (pixels * sizeof(uint16_t)) +
        (pixels / 2 / 2 * (2 * sizeof(uint16_t)));
}

// =====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    // Keep both existing policy paths intact. ADVANCED owns its filter and uses
    // the same parked helpers only for AUTO/SIMD on AVX2-capable CPUs.
    if (m_chromaDownsampling == ChromaDownsampling::ADVANCED)
    {
        const bool useSimd = (m_conversionMethod == ConversionMethod::AUTO ||
            m_conversionMethod == ConversionMethod::SIMD) && CheckCPUFeatures();
        if (useSimd && height >= MIN_LINES_FOR_THREADING)
            return ConvertV210ToP010_Threaded(srcData, srcStride, dstY, dstUV, width, height);
        ProcessAdvancedSegment(srcData, srcStride, dstY, dstUV, width, 0, height, useSimd);
        return true;
    }

    // Respect the configured conversion method at every resolution.
    ConversionMethod method = m_conversionMethod;

    // AUTO mode: select based on CPU features and frame size
    if (method == ConversionMethod::AUTO)
    {
        // Use threaded SIMD for 720p and above (if AVX2 available)
        if (height >= MIN_LINES_FOR_THREADING && CheckCPUFeatures())
        {
            method = ConversionMethod::SIMD;
        }
        else if (CheckCPUFeatures())
        {
            // Use non-threaded SIMD for smaller frames with AVX2
            method = ConversionMethod::OPTIMIZED;
        }
        else
        {
            // Fall back to standard scalar
            method = ConversionMethod::STANDARD;
        }
    }

    // Execute the selected conversion method
    switch (method)
    {
        case ConversionMethod::SIMD:
            // Threaded SIMD conversion (requires AVX2)
            if (!CheckCPUFeatures())
            {
                // Fall back if AVX2 not available
                return ConvertV210ToP010_Optimized(srcData, srcStride, dstY, dstUV, width, height);
            }
            return ConvertV210ToP010_Threaded(srcData, srcStride, dstY, dstUV, width, height);

        case ConversionMethod::OPTIMIZED:
            // Non-threaded scalar with optimizations
            return ConvertV210ToP010_Optimized(srcData, srcStride, dstY, dstUV, width, height);

        case ConversionMethod::STANDARD:
        default:
            // Standard scalar baseline
            return ConvertV210ToP010_Standard(srcData, srcStride, dstY, dstUV, width, height);
    }
}

// =====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_SIMD(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (!CheckCPUFeatures())
    {
        return ConvertV210ToP010_Optimized(srcData, srcStride, dstY, dstUV, width, height);
    }

    if (m_chromaDownsampling == ChromaDownsampling::ADVANCED)
    {
        ProcessAdvancedSegment(srcData, srcStride, dstY, dstUV, width, 0, height, true);
        return true;
    }
    if (m_chromaDownsampling == ChromaDownsampling::AVERAGE)
        return ConvertV210ToP010_SIMDImpl<true>(srcData, srcStride, dstY, dstUV, width, height);
    else
        return ConvertV210ToP010_SIMDImpl<false>(srcData, srcStride, dstY, dstUV, width, height);
}


// =====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Optimized(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (m_chromaDownsampling == ChromaDownsampling::AVERAGE)
        return ConvertV210ToP010_OptimizedImpl<true>(srcData, srcStride, dstY, dstUV, width, height);
    else
        return ConvertV210ToP010_OptimizedImpl<false>(srcData, srcStride, dstY, dstUV, width, height);
}

template<bool AverageChroma>
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_OptimizedImpl(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    const uint32_t packsPerLine = width / PIXELS_PER_PACK;

    for (uint32_t line = 0; line < height; line++)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
            srcData + line * srcStride);
        const bool isEvenLine = (line & 1) == 0;

        uint16_t* lineY = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* lineUV = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        uint16_t* dstY_ptr = lineY;
        uint16_t* dstUV_ptr = lineUV;

        // Tight loop - one pack per iteration
        for (uint32_t pack = 0; pack < packsPerLine; pack++)
        {
            uint32_t val;
            uint16_t u, y1, y2, v;

            if (isEvenLine)
            {
                // Even line: write both Y and UV
                V210_READ_PACK_BLOCK(u, y1, v);
                *dstUV_ptr++ = u << 6;
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = v << 6;

                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = u << 6;
                *dstY_ptr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                *dstUV_ptr++ = v << 6;
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = u << 6;

                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = v << 6;
                *dstY_ptr++ = y2 << 6;
            }
            else
            {
                // Odd line: write Y and complete the rounded vertical chroma
                // average started by the preceding even line.
                V210_READ_PACK_BLOCK(u, y1, v);
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }

                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }
                *dstY_ptr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }

                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }
                *dstY_ptr++ = y2 << 6;
            }
        }

        const uint32_t tailPixels = width % PIXELS_PER_PACK;
        if (tailPixels > 0)
        {
            const V210Pack tail = ReadV210Pack(src);
            if (isEvenLine)
                WriteV210PackToP010(tail, tailPixels, dstY_ptr, dstUV_ptr);
            else
            {
                uint16_t* noChroma = nullptr;
                WriteV210PackToP010(tail, tailPixels, dstY_ptr, noChroma);
                if (AverageChroma)
                    AverageV210PackChromaIntoP010(tail, tailPixels, dstUV_ptr);
            }
        }
    }

    return true;
}

// =====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Standard(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (m_chromaDownsampling == ChromaDownsampling::AVERAGE)
        return ConvertV210ToP010_StandardImpl<true>(srcData, srcStride, dstY, dstUV, width, height);
    else
        return ConvertV210ToP010_StandardImpl<false>(srcData, srcStride, dstY, dstUV, width, height);
}

template<bool AverageChroma>
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_StandardImpl(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    // Standard implementation matching the reference optimization level
    // Uses the same macros and logic flow as the original reference code
    // Portable scalar baseline without SIMD optimizations.

    const uint32_t packsPerLine = width / PIXELS_PER_PACK;

    for (uint32_t line = 0; line < height; line++)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
            srcData + line * srcStride);
        const bool isEvenLine = (line & 1) == 0;

        // Set destination pointers for this line (matches reference logic)
        uint16_t* dstY_ptr = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* dstUV_ptr = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        // Process each pack using the same macro-based approach as reference
        for (uint32_t pack = 0; pack < packsPerLine; pack++)
        {
            uint32_t val;
            uint16_t u, y1, y2, v;

            if (isEvenLine)
            {
                // Even line: write both Y and UV (matches reference exactly)
                V210_READ_PACK_BLOCK(u, y1, v);
                *dstUV_ptr++ = u << 6;
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = v << 6;

                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = u << 6;
                *dstY_ptr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                *dstUV_ptr++ = v << 6;
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = u << 6;

                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY_ptr++ = y1 << 6;
                *dstUV_ptr++ = v << 6;
                *dstY_ptr++ = y2 << 6;
            }
            else
            {
                // Odd line completes the rounded vertical chroma average.
                V210_READ_PACK_BLOCK(u, y1, v);
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }

                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }
                *dstY_ptr++ = y2 << 6;

                V210_READ_PACK_BLOCK(v, y1, u);
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, u); ++dstUV_ptr; }

                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY_ptr++ = y1 << 6;
                if (AverageChroma) { *dstUV_ptr = AverageP010Chroma(*dstUV_ptr, v); ++dstUV_ptr; }
                *dstY_ptr++ = y2 << 6;
            }
        }

        const uint32_t tailPixels = width % PIXELS_PER_PACK;
        if (tailPixels > 0)
        {
            const V210Pack tail = ReadV210Pack(src);
            if (isEvenLine)
                WriteV210PackToP010(tail, tailPixels, dstY_ptr, dstUV_ptr);
            else
            {
                uint16_t* noChroma = nullptr;
                WriteV210PackToP010(tail, tailPixels, dstY_ptr, noChroma);
                if (AverageChroma)
                    AverageV210PackChromaIntoP010(tail, tailPixels, dstUV_ptr);
            }
        }
    }

    return true;
}

// =====================================================================
void CV210toP010VideoFrameFormatter::LogConversionPerformance(uint64_t conversionTimeUs, bool success) const
{
    m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

#ifdef _DEBUG
    m_totalConversions++;
    m_totalConversionTimeUs += conversionTimeUs;
    m_scalarConversions++;
    m_scalarConversionTimeUs += conversionTimeUs;

    if (m_totalConversions % 100 == 0)
    {
        LogPerformanceStats();
    }
#endif
}


// Lanczos-3 at half-row phase, widened by 2 for the 2:1 vertical reduction.
// w[t] = sinc((t-5.5)/2) * sinc((t-5.5)/6), normalized and rounded to Q14.
// Coefficients sum exactly to 16384; constants remain exact, signed lobes are
// retained until final rounding and 0..1023 saturation (not nominal-range clipping).
// Source halos clamp to frame edges, never to helper partition edges.


void CV210toP010VideoFrameFormatter::ProcessAdvancedSegment(
    const uint8_t* srcData, uint32_t srcStride, uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine, bool useSimd) noexcept
{
    if (useSimd && CheckCPUFeatures())
        ProcessAdvancedSegmentAVX2(srcData, srcStride, dstY, dstUV, width, startLine, endLine);
    else
        ProcessAdvancedSegmentScalar(srcData, srcStride, dstY, dstUV, width, startLine, endLine);
}

void CV210toP010VideoFrameFormatter::ProcessAdvancedSegmentScalar(
    const uint8_t* srcData, uint32_t srcStride, uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine) noexcept
{
    static constexpr int weights[12] = {
        60, 247, -557, -1092, 2220, 7314, 7314, 2220, -1092, -557, 247, 60
    };
    const auto sample = [](const uint32_t* row, uint32_t component) {
        return static_cast<int>((row[component / 3] >> ((component % 3) * 10)) & 1023U);
    };
    for (uint32_t line = startLine; line < endLine; line += 2)
    {
        const uint32_t* rows[12];
        for (int tap = 0; tap < 12; ++tap)
        {
            const int sourceLine = (std::max)(0, (std::min)(
                static_cast<int>(m_height) - 1, static_cast<int>(line) + tap - 5));
            rows[tap] = reinterpret_cast<const uint32_t*>(
                srcData + static_cast<size_t>(sourceLine) * srcStride);
        }
        auto* y0 = dstY + static_cast<size_t>(line) * width;
        auto* y1 = y0 + width;
        auto* uv = dstUV + static_cast<size_t>(line / 2) * width;
        uint32_t x = 0;

        for (; x < width; ++x)
        {
            y0[x] = static_cast<uint16_t>(sample(rows[5], x * 2 + 1) << 6);
            y1[x] = static_cast<uint16_t>(sample(rows[6], x * 2 + 1) << 6);
            int sum = 8192;
            for (int tap = 0; tap < 12; ++tap)
                sum += weights[tap] * sample(rows[tap], x * 2);
            uv[x] = static_cast<uint16_t>((std::min)(1023, (std::max)(0, sum) / 16384) << 6);
        }
    }
}

bool CV210toP010VideoFrameFormatter::CheckCPUFeatures() const
{
    if (!m_cpuFeaturesChecked)
    {
        m_hasAVX2 = CpuFeatures::SupportsAvx2Kernels();
        m_hasAVX2MemoryOps = m_hasAVX2;
        m_actualMaxThreads = GetMaxThreadCount();
        m_cpuFeaturesChecked = true;
    }
    return m_hasAVX2;
}
