/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CV210toP010VideoFrameFormatter.h"
#include <vector>
#include <immintrin.h>
#include <intrin.h> // For __cpuid

// ---------------------------------------------------------------------
// Macros for V210 unpacking
#define V210_READ_PACK_BLOCK(a, b, c) \
    do {                              \
        val  = *src++;                \
        a = val & 0x3FF;              \
        b = (val >> 10) & 0x3FF;      \
        c = (val >> 20) & 0x3FF;      \
    } while (0)

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))

// 720p special-case handling
#define P010_WRITE_VALUE_720(dst, idx, val) ((dst)[(idx)] = (val) << 6)
#define CHROMA_NEUTRAL (512 << 6)

#define FAST_BORDER_COPY_Y(dst, src, width) \
    do { \
        (dst)[0] = 0; (dst)[1] = 0; \
        memcpy((dst) + 2, (src) + 2, ((width) - 4) * sizeof(uint16_t)); \
        (dst)[(width) - 2] = 0; (dst)[(width) - 1] = 0; \
    } while (0)

#define FAST_BORDER_COPY_UV(dst, src, width) \
    do { \
        (dst)[0] = CHROMA_NEUTRAL; (dst)[1] = CHROMA_NEUTRAL; \
        memcpy((dst) + 2, (src) + 2, ((width) - 4) * sizeof(uint16_t)); \
        (dst)[(width) - 2] = CHROMA_NEUTRAL; (dst)[(width) - 1] = CHROMA_NEUTRAL; \
    } while (0)

// =====================================================================
// Constructor / Destructor
// =====================================================================
CV210toP010VideoFrameFormatter::CV210toP010VideoFrameFormatter()
{
    // Thread pool will be initialized lazily on first large frame
}

CV210toP010VideoFrameFormatter::~CV210toP010VideoFrameFormatter()
{
    ShutdownThreadPool();
}

// =====================================================================
// Thread Pool Management - Simple spin-wait for low latency
// =====================================================================
void CV210toP010VideoFrameFormatter::InitializeThreadPool()
{
    if (m_threadsInitialized)
        return;
        
    m_threadContexts = std::make_unique<ThreadContext[]>(MAX_THREADS);
    
    for (uint32_t i = 0; i < MAX_THREADS; i++)
    {
        m_threadContexts[i].state.store(0); // idle
        m_threadContexts[i].thread = std::thread(ThreadWorkerStatic, this, i);
    }
    
    m_threadsInitialized = true;
}

void CV210toP010VideoFrameFormatter::ShutdownThreadPool()
{
    if (!m_threadsInitialized)
        return;
        
    // Signal all threads to exit
    for (uint32_t i = 0; i < MAX_THREADS; i++)
    {
        m_threadContexts[i].state.store(2); // exit
    }
    
    // Wait for all threads to finish
    for (uint32_t i = 0; i < MAX_THREADS; i++)
    {
        if (m_threadContexts[i].thread.joinable())
        {
            m_threadContexts[i].thread.join();
        }
    }
    
    m_threadContexts.reset();
    m_threadsInitialized = false;
}

void CV210toP010VideoFrameFormatter::ThreadWorkerStatic(CV210toP010VideoFrameFormatter* self, uint32_t threadIndex)
{
    ThreadContext& ctx = self->m_threadContexts[threadIndex];
    
    while (true)
    {
        // Spin-wait for work (state == 1) or exit (state == 2)
        int state;
        while ((state = ctx.state.load(std::memory_order_acquire)) == 0)
        {
            // Yield to avoid burning CPU while idle
            std::this_thread::yield();
        }
        
        // Check if we should exit
        if (state == 2)
            break;
            
        // Do the work
        self->ProcessLineSegment(
            ctx.work.srcData, ctx.work.srcStride,
            ctx.work.dstY, ctx.work.dstUV,
            ctx.work.width, ctx.work.startLine, ctx.work.endLine
        );
        
        // Signal completion by going back to idle
        ctx.state.store(0, std::memory_order_release);
    }
}

// =====================================================================
// Process a segment of line pairs (used by threads and main thread)
// This processes lines from startLine to endLine (exclusive)
// startLine MUST be even, endLine MUST be even or equal to height
// =====================================================================
void CV210toP010VideoFrameFormatter::ProcessLineSegment(
    const uint8_t* srcData, uint32_t srcStride,
    uint16_t* dstY, uint16_t* dstUV,
    uint32_t width, uint32_t startLine, uint32_t endLine) noexcept
{
    const uint32_t pixelsPerIter = 12;
    const uint32_t numIters = width / pixelsPerIter;
    const uint32_t remainderPixels = width % pixelsPerIter;

    // Constants
    const __m256i mask_3ff = _mm256_set1_epi32(0x3FF);
    
    // Y permutation indices and shifts
    const __m256i y_idx0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
    const __m256i y_shift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
    const __m256i y_idx1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
    const __m256i y_shift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);
    
    // UV permutation indices and shifts
    const __m256i uv_idx0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
    const __m256i uv_shift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
    const __m256i uv_idx1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
    const __m256i uv_shift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);

    // Process line pairs (even, odd)
    for (uint32_t line = startLine; line < endLine; line += 2)
    {
        const uint32_t* src_even = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line) * srcStride);
        const uint32_t* src_odd = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line + 1) * srcStride);
        
        uint16_t* lineY_even = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* lineY_odd = dstY + static_cast<ptrdiff_t>(line + 1) * width;
        uint16_t* lineUV = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        // Prefetch next lines to hide memory latency
        if (line + 2 < endLine)
        {
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 2) * srcStride), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(srcData + static_cast<ptrdiff_t>(line + 3) * srcStride), _MM_HINT_T0);
        }

        for (uint32_t i = 0; i < numIters; i++)
        {
            // Load 32 bytes from even line (8 ints, 12 pixels)
            __m256i in_even = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_even));
            src_even += 8;

            // Load 32 bytes from odd line (8 ints, 12 pixels)
            __m256i in_odd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_odd));
            src_odd += 8;

            // Process Y from even line (Y0..Y7)
            __m256i y0_even = _mm256_permutevar8x32_epi32(in_even, y_idx0);
            y0_even = _mm256_srlv_epi32(y0_even, y_shift0);
            y0_even = _mm256_and_si256(y0_even, mask_3ff);
            y0_even = _mm256_slli_epi32(y0_even, 6);

            // Process Y from even line (Y8..Y11)
            __m256i y1_even = _mm256_permutevar8x32_epi32(in_even, y_idx1);
            y1_even = _mm256_srlv_epi32(y1_even, y_shift1);
            y1_even = _mm256_and_si256(y1_even, mask_3ff);
            y1_even = _mm256_slli_epi32(y1_even, 6);

            // Process Y from odd line (Y0..Y7)
            __m256i y0_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx0);
            y0_odd = _mm256_srlv_epi32(y0_odd, y_shift0);
            y0_odd = _mm256_and_si256(y0_odd, mask_3ff);
            y0_odd = _mm256_slli_epi32(y0_odd, 6);

            // Process Y from odd line (Y8..Y11)
            __m256i y1_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx1);
            y1_odd = _mm256_srlv_epi32(y1_odd, y_shift1);
            y1_odd = _mm256_and_si256(y1_odd, mask_3ff);
            y1_odd = _mm256_slli_epi32(y1_odd, 6);

            // Store Y from even line (unaligned stores - safe for any pointer)
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_even);
                __m128i y0_hi = _mm256_extracti128_si256(y0_even, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);
                
                __m128i y1_lo = _mm256_castsi256_si128(y1_even);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_even), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_even + 8), packed1);
                lineY_even += 12;
            }

            // Store Y from odd line
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_odd);
                __m128i y0_hi = _mm256_extracti128_si256(y0_odd, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);
                
                __m128i y1_lo = _mm256_castsi256_si128(y1_odd);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_odd), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_odd + 8), packed1);
                lineY_odd += 12;
            }

            // Process UV from even line only (UV0..UV7)
            __m256i uv0 = _mm256_permutevar8x32_epi32(in_even, uv_idx0);
            uv0 = _mm256_srlv_epi32(uv0, uv_shift0);
            uv0 = _mm256_and_si256(uv0, mask_3ff);
            uv0 = _mm256_slli_epi32(uv0, 6);

            // Process UV from even line only (UV8..UV11)
            __m256i uv1 = _mm256_permutevar8x32_epi32(in_even, uv_idx1);
            uv1 = _mm256_srlv_epi32(uv1, uv_shift1);
            uv1 = _mm256_and_si256(uv1, mask_3ff);
            uv1 = _mm256_slli_epi32(uv1, 6);

            // Store UV
            {
                __m128i uv0_lo = _mm256_castsi256_si128(uv0);
                __m128i uv0_hi = _mm256_extracti128_si256(uv0, 1);
                __m128i packed0 = _mm_packus_epi32(uv0_lo, uv0_hi);
                
                __m128i uv1_lo = _mm256_castsi256_si128(uv1);
                __m128i packed1 = _mm_packus_epi32(uv1_lo, uv1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineUV), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineUV + 8), packed1);
                lineUV += 12;
            }
        }

        // Handle remainder (6 pixels) - use regular stores for small amounts
        if (remainderPixels > 0)
        {
            // Process even line remainder
            {
                const uint32_t* src = src_even;
                uint16_t* lineY = lineY_even;
                uint16_t* line_uv = lineUV;
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                *line_uv++ = u << 6; *lineY++ = y1 << 6; *line_uv++ = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *lineY++ = y1 << 6; *line_uv++ = u << 6; *lineY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *line_uv++ = v << 6; *lineY++ = y1 << 6; *line_uv++ = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *lineY++ = y1 << 6; *line_uv++ = v << 6; *lineY++ = y2 << 6;
            }

            // Process odd line remainder (Y only)
            {
                const uint32_t* src = src_odd;
                uint16_t* lineY = lineY_odd;
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                *lineY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *lineY++ = y1 << 6; *lineY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *lineY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *lineY++ = y1 << 6; *lineY++ = y2 << 6;
            }
        }
    }
}

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
    // Initialize thread pool if not already done
    if (!m_threadsInitialized)
    {
        InitializeThreadPool();
    }
    
    // Calculate line pairs per thread (must be even for P010 4:2:0)
    const uint32_t totalLinePairs = height / 2;
    const uint32_t linePairsPerThread = totalLinePairs / (MAX_THREADS + 1); // +1 for main thread
    const uint32_t linesPerThread = linePairsPerThread * 2;
    
    // Distribute work to worker threads
    uint32_t currentLine = 0;
    for (uint32_t i = 0; i < MAX_THREADS; i++)
    {
        ThreadContext& ctx = m_threadContexts[i];
        
        // Set up work item
        ctx.work.srcData = srcData;
        ctx.work.srcStride = srcStride;
        ctx.work.dstY = dstY;
        ctx.work.dstUV = dstUV;
        ctx.work.width = width;
        ctx.work.startLine = currentLine;
        ctx.work.endLine = currentLine + linesPerThread;
        
        currentLine += linesPerThread;
        
        // Signal work available (must be after work item is set up)
        ctx.state.store(1, std::memory_order_release);
    }
    
    // Main thread processes the remaining lines
    ProcessLineSegment(srcData, srcStride, dstY, dstUV, width, currentLine, height);
    
    // Wait for all worker threads to complete (spin-wait)
    for (uint32_t i = 0; i < MAX_THREADS; i++)
    {
        while (m_threadContexts[i].state.load(std::memory_order_acquire) != 0)
        {
            // Brief pause to reduce bus contention
            _mm_pause();
        }
    }
    
    return true;
}

// CPU feature detection
bool CV210toP010VideoFrameFormatter::CheckCPUFeatures() const
{
    if (!m_cpuFeaturesChecked)
    {
        int cpuInfo[4];
        __cpuid(cpuInfo, 0);
        int nIds = cpuInfo[0];

        m_hasAVX2 = false;
        m_hasAVX2MemoryOps = false;

        if (nIds >= 7)
        {
            __cpuidex(cpuInfo, 7, 0);
            m_hasAVX2 = (cpuInfo[1] & (1 << 5)) != 0; // EBX bit 5 is AVX2
            m_hasAVX2MemoryOps = m_hasAVX2; // Assuming if AVX2 is present, we can use it for memory ops
        }
        
        m_cpuFeaturesChecked = true;
    }
    return m_hasAVX2;
}

bool CV210toP010VideoFrameFormatter::HasAVX2MemoryOps() const
{
    CheckCPUFeatures();
    return m_hasAVX2MemoryOps;
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

// ---------------------------------------------------------------------
void CV210toP010VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
    if (!videoState)
        throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    if (m_height % 2 != 0)
        throw std::runtime_error("P010 output needs an even amount of input lines");

    uint32_t origWidth = videoState->displayMode->FrameWidth();
    bool special720 = (origWidth == 1280 && m_height == 720);
    m_special720 = special720;

    if (!special720 && (origWidth % PIXELS_PER_PACK != 0))
        throw std::runtime_error("Can only handle conversions which align with V210 boundary (6 pixels)");

    const uint32_t bytes = videoState->BytesPerFrame();
    uint32_t expectedBytes;
    if (special720)
    {
        const uint32_t aligned_width = ((1280 + 47) / 48) * 48;
        const uint32_t src_stride = aligned_width * 8 / 3;
        expectedBytes = m_height * src_stride;

        const uint32_t extraPixels = origWidth % PIXELS_PER_PACK;
        const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels);
        const uint32_t fullDecodedWidth = origWidth + extraNeeded;

        m_tempY.resize(fullDecodedWidth);
        m_tempUV.resize(fullDecodedWidth);
    }
    else
    {
        expectedBytes = m_height * ((origWidth / PIXELS_PER_PACK) * BYTES_PER_PACK);
        m_tempY.clear();
        m_tempUV.clear();
        m_tempY.shrink_to_fit();
        m_tempUV.shrink_to_fit();
    }

    if (bytes != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");

    m_width = origWidth;
}

// ---------------------------------------------------------------------
// FormatVideoFrame: Simple, fast, reliable conversion
bool CV210toP010VideoFrameFormatter::FormatVideoFrame(
    const VideoFrame& inFrame,
    BYTE* outBuffer)
{
    const auto startTime = GetWallClockTime();

    const uint32_t pixels = m_height * m_width;
    const uint32_t yPlaneSize = pixels * sizeof(uint16_t);
    
    uint16_t* dstY = reinterpret_cast<uint16_t*>(outBuffer);
    uint16_t* dstUV = reinterpret_cast<uint16_t*>(outBuffer + yPlaneSize);

    const uint32_t alignedWidth = ((m_width + 47) / 48) * 48;
    const uint32_t srcStride = alignedWidth * 8 / 3;

    const bool conversionSuccess = ConvertV210ToP010(
        static_cast<const uint8_t*>(inFrame.GetData()),
        srcStride,
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

// ---------------------------------------------------------------------
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010(
    const uint8_t* srcData,
    uint32_t srcStride, 
    uint16_t* dstY,
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    if (m_special720)
    {
        return ConvertV210ToP010_720p(srcData, srcStride, dstY, dstUV, width, height);
    }
    else if (height >= MIN_LINES_FOR_THREADING && CheckCPUFeatures())
    {
        // Use threaded SIMD for large frames (1080p and above)
        return ConvertV210ToP010_Threaded(srcData, srcStride, dstY, dstUV, width, height);
    }
    else
    {
        // Use single-threaded SIMD for smaller frames
        return ConvertV210ToP010_SIMD(srcData, srcStride, dstY, dstUV, width, height);
    }
}

// ---------------------------------------------------------------------
// 720p conversion with border handling
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_720p(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY, 
    uint16_t* dstUV,
    uint32_t width,
    uint32_t height) noexcept
{
    const uint32_t extraPixels = width % PIXELS_PER_PACK;
    const uint32_t extraNeeded = (extraPixels == 0) ? 0 : (PIXELS_PER_PACK - extraPixels);
    const uint32_t fullDecodedWidth = width + extraNeeded;
    const uint32_t fullPacks = fullDecodedWidth / PIXELS_PER_PACK;
    
    uint16_t* const tempYData = m_tempY.data();
    uint16_t* const tempUVData = m_tempUV.data();
    
    for (uint32_t line = 0; line < height; line++)
    {
        const uint32_t* srcLine = reinterpret_cast<const uint32_t*>(
            srcData + static_cast<ptrdiff_t>(line * srcStride));
        const bool isEvenLine = (line & 1) == 0;

        // Fill UV buffer for even lines
        if (isEvenLine)
        {
            for (size_t i = 0; i < m_tempUV.size(); ++i) {
                tempUVData[i] = CHROMA_NEUTRAL;
            }
        }

        const uint32_t* src = srcLine;
        for (uint32_t pack = 0; pack < fullPacks; ++pack) 
        {
            const uint32_t off = pack * PIXELS_PER_PACK;
            uint32_t val;
            uint16_t u, y1, y2, v;

            if (isEvenLine)
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                P010_WRITE_VALUE_720(tempUVData, off + 0, u);
                P010_WRITE_VALUE_720(tempYData, off + 0, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 1, v);

                V210_READ_PACK_BLOCK(y1, u, y2);
                P010_WRITE_VALUE_720(tempYData, off + 1, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 2, u);
                P010_WRITE_VALUE_720(tempYData, off + 2, y2);

                V210_READ_PACK_BLOCK(v, y1, u);
                P010_WRITE_VALUE_720(tempUVData, off + 3, v);
                P010_WRITE_VALUE_720(tempYData, off + 3, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 4, u);

                V210_READ_PACK_BLOCK(y1, v, y2);
                P010_WRITE_VALUE_720(tempYData, off + 4, y1);
                P010_WRITE_VALUE_720(tempUVData, off + 5, v);
                P010_WRITE_VALUE_720(tempYData, off + 5, y2);
            }
            else
            {
                V210_READ_PACK_BLOCK(u, y1, v);
                P010_WRITE_VALUE_720(tempYData, off + 0, y1);

                V210_READ_PACK_BLOCK(y1, u, y2);
                P010_WRITE_VALUE_720(tempYData, off + 1, y1);
                P010_WRITE_VALUE_720(tempYData, off + 2, y2);

                V210_READ_PACK_BLOCK(v, y1, u);
                P010_WRITE_VALUE_720(tempYData, off + 3, y1);

                V210_READ_PACK_BLOCK(y1, v, y2);
                P010_WRITE_VALUE_720(tempYData, off + 4, y1);
                P010_WRITE_VALUE_720(tempYData, off + 5, y2);
            }
        }
        
        uint16_t* lineDstY = dstY + line * width;
        uint16_t* lineDstUV = isEvenLine ? (dstUV + (line / 2) * width) : nullptr;

        FAST_BORDER_COPY_Y(lineDstY, tempYData, width);
        
        if (isEvenLine && lineDstUV)
        {
            FAST_BORDER_COPY_UV(lineDstUV, tempUVData, width);
        }
    }
    
    return true;
}

// ====================================================================
// OPTIMIZED STANDARD CONVERSION - Single-threaded, fast scalar
// ====================================================================
bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Optimized(
    const uint8_t* srcData,
    uint32_t srcStride,
    uint16_t* dstY,
    uint16_t* dstUV, 
    uint32_t width,
    uint32_t height) noexcept
{
    const uint32_t packsPerLine = width / PIXELS_PER_PACK;
    
    // Process 2 lines at a time to improve cache locality and reduce loop overhead
    // Since P010 is 4:2:0, UV plane is shared for 2 lines
    for (uint32_t line = 0; line < height; line += 2)
    {
        const uint8_t* srcLine1 = srcData + static_cast<ptrdiff_t>(line) * srcStride;
        const uint8_t* srcLine2 = srcData + static_cast<ptrdiff_t>(line + 1) * srcStride;
        
        uint16_t* dstY1 = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* dstY2 = dstY + static_cast<ptrdiff_t>(line + 1) * width;
        uint16_t* dstUV_line = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;
        
        const uint32_t* src1 = reinterpret_cast<const uint32_t*>(srcLine1);
        const uint32_t* src2 = reinterpret_cast<const uint32_t*>(srcLine2);
        
        // Process 4 packs (24 pixels) per iteration to unroll loops
        // This reduces branch prediction pressure and allows better pipelining
        uint32_t pack = 0;
        const uint32_t* src = src1; // Initialize src for the macro
        for (; pack + 3 < packsPerLine; pack += 4)
        {
            // Process 4 packs for line 1 (Even line - extracts Y and UV)
            // Pack 0
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                // Block 1
                V210_READ_PACK_BLOCK(u, y1, v);
                dstUV_line[0] = u << 6; dstY1[0] = y1 << 6; dstUV_line[1] = v << 6;
                
                // Block 2
                V210_READ_PACK_BLOCK(y1, u, y2);
                dstY1[1] = y1 << 6; dstUV_line[2] = u << 6; dstY1[2] = y2 << 6;
                
                // Block 3
                V210_READ_PACK_BLOCK(v, y1, u);
                dstUV_line[3] = v << 6; dstY1[3] = y1 << 6; dstUV_line[4] = u << 6;
                
                // Block 4
                V210_READ_PACK_BLOCK(y1, v, y2);
                dstY1[4] = y1 << 6; dstUV_line[5] = v << 6; dstY1[5] = y2 << 6;
            }
            
            // Pack 1
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                dstUV_line[6] = u << 6; dstY1[6] = y1 << 6; dstUV_line[7] = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                dstY1[7] = y1 << 6; dstUV_line[8] = u << 6; dstY1[8] = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                dstUV_line[9] = v << 6; dstY1[9] = y1 << 6; dstUV_line[10] = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                dstY1[10] = y1 << 6; dstUV_line[11] = v << 6; dstY1[11] = y2 << 6;
            }
            
            // Pack 2
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                dstUV_line[12] = u << 6; dstY1[12] = y1 << 6; dstUV_line[13] = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                dstY1[13] = y1 << 6; dstUV_line[14] = u << 6; dstY1[14] = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                dstUV_line[15] = v << 6; dstY1[15] = y1 << 6; dstUV_line[16] = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                dstY1[16] = y1 << 6; dstUV_line[17] = v << 6; dstY1[17] = y2 << 6;
            }
            
            // Pack 3
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                dstUV_line[18] = u << 6; dstY1[18] = y1 << 6; dstUV_line[19] = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                dstY1[19] = y1 << 6; dstUV_line[20] = u << 6; dstY1[20] = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                dstUV_line[21] = v << 6; dstY1[21] = y1 << 6; dstUV_line[22] = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                dstY1[22] = y1 << 6; dstUV_line[23] = v << 6; dstY1[23] = y2 << 6;
            }
            
            // Process 4 packs for line 2 (Odd line - extracts Y only)
            // Use a separate pointer for src to avoid confusion
            src = src2 + (pack * 4); // Re-assign src for the macro
            
            // Pack 0
            {
                uint32_t val;
                uint16_t a, b, c; // Generic names since we discard chroma
                
                V210_READ_PACK_BLOCK(a, b, c); dstY2[0] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[1] = a << 6; dstY2[2] = c << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[3] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[4] = a << 6; dstY2[5] = c << 6;
            }
            
            // Pack 1
            {
                uint32_t val;
                uint16_t a, b, c;
                
                V210_READ_PACK_BLOCK(a, b, c); dstY2[6] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[7] = a << 6; dstY2[8] = c << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[9] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[10] = a << 6; dstY2[11] = c << 6;
            }
            
            // Pack 2
            {
                uint32_t val;
                uint16_t a, b, c;
                
                V210_READ_PACK_BLOCK(a, b, c); dstY2[12] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[13] = a << 6; dstY2[14] = c << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[15] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[16] = a << 6; dstY2[17] = c << 6;
            }
            
            // Pack 3
            {
                uint32_t val;
                uint16_t a, b, c;
                
                V210_READ_PACK_BLOCK(a, b, c); dstY2[18] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[19] = a << 6; dstY2[20] = c << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[21] = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); dstY2[22] = a << 6; dstY2[23] = c << 6;
            }
            
            // Advance pointers
            dstY1 += 24;
            dstY2 += 24;
            dstUV_line += 24;
            src = src1 + ((pack + 4) * 4); // Update src for next iteration of line 1
        }
        
        // Handle remaining packs
        src = src1 + (pack * 4); // Reset src for remaining packs of line 1
        for (; pack < packsPerLine; pack++)
        {
            // Line 1 (Even)
            {
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                *dstUV_line++ = u << 6; *dstY1++ = y1 << 6; *dstUV_line++ = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY1++ = y1 << 6; *dstUV_line++ = u << 6; *dstY1++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *dstUV_line++ = v << 6; *dstY1++ = y1 << 6; *dstUV_line++ = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY1++ = y1 << 6; *dstUV_line++ = v << 6; *dstY1++ = y2 << 6;
            }
            
            // Line 2 (Odd)
            {
                const uint32_t* src_odd = src2 + (pack * 4);
                const uint32_t* src_saved = src; // Save src
                src = src_odd; // Temporarily switch src for macro
                
                uint32_t val;
                uint16_t a, b, c;
                
                V210_READ_PACK_BLOCK(a, b, c); *dstY2++ = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); *dstY2++ = a << 6; *dstY2++ = c << 6;
                V210_READ_PACK_BLOCK(a, b, c); *dstY2++ = b << 6;
                V210_READ_PACK_BLOCK(a, b, c); *dstY2++ = a << 6; *dstY2++ = c << 6;
                
                src = src_saved; // Restore src
            }
        }
    }
    
    return true;
}

bool CV210toP010VideoFrameFormatter::ConvertV210ToP010_Standard(
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
        uint16_t* lineUV = isEvenLine ? (dstUV + static_cast<ptrdiff_t>(line >> 1) * width) : nullptr;
        
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
                // Odd line: Y only
                V210_READ_PACK_BLOCK(u, y1, v);
                *dstY_ptr++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *dstY_ptr++ = y1 << 6; 
                *dstY_ptr++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *dstY_ptr++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *dstY_ptr++ = y1 << 6; 
                *dstY_ptr++ = y2 << 6;
            }
        }
    }
    
    return true;
}

// ---------------------------------------------------------------------
// SIMD implementation - Paired even/odd lines for better parallelism
// ====================================================================
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

    const uint32_t pixelsPerIter = 12;
    const uint32_t numIters = width / pixelsPerIter;
    const uint32_t remainderPixels = width % pixelsPerIter;

    // Constants
    const __m256i mask_3ff = _mm256_set1_epi32(0x3FF);
    
    // Y permutation indices and shifts
    const __m256i y_idx0 = _mm256_setr_epi32(0, 1, 1, 2, 3, 3, 4, 5);
    const __m256i y_shift0 = _mm256_setr_epi32(10, 0, 20, 10, 0, 20, 10, 0);
    const __m256i y_idx1 = _mm256_setr_epi32(5, 6, 7, 7, 0, 0, 0, 0);
    const __m256i y_shift1 = _mm256_setr_epi32(20, 10, 0, 20, 0, 0, 0, 0);
    
    // UV permutation indices and shifts
    const __m256i uv_idx0 = _mm256_setr_epi32(0, 0, 1, 2, 2, 3, 4, 4);
    const __m256i uv_shift0 = _mm256_setr_epi32(0, 20, 10, 0, 20, 10, 0, 20);
    const __m256i uv_idx1 = _mm256_setr_epi32(5, 6, 6, 7, 0, 0, 0, 0);
    const __m256i uv_shift1 = _mm256_setr_epi32(10, 0, 20, 10, 0, 0, 0, 0);

    // Process line pairs (even, odd)
    for (uint32_t line = 0; line < height; line += 2)
    {
        const uint32_t* src_even = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line) * srcStride);
        const uint32_t* src_odd = reinterpret_cast<const uint32_t*>(srcData + static_cast<ptrdiff_t>(line + 1) * srcStride);
        
        uint16_t* lineY_even = dstY + static_cast<ptrdiff_t>(line) * width;
        uint16_t* lineY_odd = dstY + static_cast<ptrdiff_t>(line + 1) * width;
        uint16_t* lineUV = dstUV + static_cast<ptrdiff_t>(line >> 1) * width;

        for (uint32_t i = 0; i < numIters; i++)
        {
            // Load 32 bytes from even line (8 ints, 12 pixels)
            __m256i in_even = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_even));
            src_even += 8;

            // Load 32 bytes from odd line (8 ints, 12 pixels)
            __m256i in_odd = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_odd));
            src_odd += 8;

            // Process Y from even line (Y0..Y7)
            __m256i y0_even = _mm256_permutevar8x32_epi32(in_even, y_idx0);
            y0_even = _mm256_srlv_epi32(y0_even, y_shift0);
            y0_even = _mm256_and_si256(y0_even, mask_3ff);
            y0_even = _mm256_slli_epi32(y0_even, 6);

            // Process Y from even line (Y8..Y11)
            __m256i y1_even = _mm256_permutevar8x32_epi32(in_even, y_idx1);
            y1_even = _mm256_srlv_epi32(y1_even, y_shift1);
            y1_even = _mm256_and_si256(y1_even, mask_3ff);
            y1_even = _mm256_slli_epi32(y1_even, 6);

            // Process Y from odd line (Y0..Y7)
            __m256i y0_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx0);
            y0_odd = _mm256_srlv_epi32(y0_odd, y_shift0);
            y0_odd = _mm256_and_si256(y0_odd, mask_3ff);
            y0_odd = _mm256_slli_epi32(y0_odd, 6);

            // Process Y from odd line (Y8..Y11)
            __m256i y1_odd = _mm256_permutevar8x32_epi32(in_odd, y_idx1);
            y1_odd = _mm256_srlv_epi32(y1_odd, y_shift1);
            y1_odd = _mm256_and_si256(y1_odd, mask_3ff);
            y1_odd = _mm256_slli_epi32(y1_odd, 6);

            // Store Y from even line (unaligned stores - safe for any pointer)
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_even);
                __m128i y0_hi = _mm256_extracti128_si256(y0_even, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);
                
                __m128i y1_lo = _mm256_castsi256_si128(y1_even);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_even), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_even + 8), packed1);
                lineY_even += 12;
            }

            // Store Y from odd line
            {
                __m128i y0_lo = _mm256_castsi256_si128(y0_odd);
                __m128i y0_hi = _mm256_extracti128_si256(y0_odd, 1);
                __m128i packed0 = _mm_packus_epi32(y0_lo, y0_hi);
                
                __m128i y1_lo = _mm256_castsi256_si128(y1_odd);
                __m128i packed1 = _mm_packus_epi32(y1_lo, y1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineY_odd), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineY_odd + 8), packed1);
                lineY_odd += 12;
            }

            // Process UV from even line only (UV0..UV7)
            __m256i uv0 = _mm256_permutevar8x32_epi32(in_even, uv_idx0);
            uv0 = _mm256_srlv_epi32(uv0, uv_shift0);
            uv0 = _mm256_and_si256(uv0, mask_3ff);
            uv0 = _mm256_slli_epi32(uv0, 6);

            // Process UV from even line only (UV8..UV11)
            __m256i uv1 = _mm256_permutevar8x32_epi32(in_even, uv_idx1);
            uv1 = _mm256_srlv_epi32(uv1, uv_shift1);
            uv1 = _mm256_and_si256(uv1, mask_3ff);
            uv1 = _mm256_slli_epi32(uv1, 6);

            // Store UV
            {
                __m128i uv0_lo = _mm256_castsi256_si128(uv0);
                __m128i uv0_hi = _mm256_extracti128_si256(uv0, 1);
                __m128i packed0 = _mm_packus_epi32(uv0_lo, uv0_hi);
                
                __m128i uv1_lo = _mm256_castsi256_si128(uv1);
                __m128i packed1 = _mm_packus_epi32(uv1_lo, uv1_lo);

                _mm_storeu_si128(reinterpret_cast<__m128i*>(lineUV), packed0);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(lineUV + 8), packed1);
                lineUV += 12;
            }
        }

        // Handle remainder (6 pixels) - use regular stores for small amounts
        if (remainderPixels > 0)
        {
            // Process even line remainder
            {
                const uint32_t* src = src_even;
                uint16_t* lineY = lineY_even;
                uint16_t* line_uv = lineUV;
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                *line_uv++ = u << 6; *lineY++ = y1 << 6; *line_uv++ = v << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *lineY++ = y1 << 6; *line_uv++ = u << 6; *lineY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *line_uv++ = v << 6; *lineY++ = y1 << 6; *line_uv++ = u << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *lineY++ = y1 << 6; *line_uv++ = v << 6; *lineY++ = y2 << 6;
            }

            // Process odd line remainder (Y only)
            {
                const uint32_t* src = src_odd;
                uint16_t* lineY = lineY_odd;
                uint32_t val;
                uint16_t u, y1, y2, v;
                
                V210_READ_PACK_BLOCK(u, y1, v);
                *lineY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, u, y2);
                *lineY++ = y1 << 6; *lineY++ = y2 << 6;
                
                V210_READ_PACK_BLOCK(v, y1, u);
                *lineY++ = y1 << 6;
                
                V210_READ_PACK_BLOCK(y1, v, y2);
                *lineY++ = y1 << 6; *lineY++ = y2 << 6;
            }
        }
    }
}

// ---------------------------------------------------------------------
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
