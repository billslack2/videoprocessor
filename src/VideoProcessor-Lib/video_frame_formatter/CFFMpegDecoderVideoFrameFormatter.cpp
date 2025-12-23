/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <immintrin.h>  // AVX2 intrinsics
#include <intrin.h>     // CPU feature detection

#include <libavutil/error.h>

#include "CFFMpegDecoderVideoFrameFormatter.h"


static const int OUTPUT_LINESIZE_ALIGNMENT = 1;


CFFMpegDecoderVideoFrameFormatter::CFFMpegDecoderVideoFrameFormatter(
	AVCodecID inputCodecId,
	AVPixelFormat targetPixelFormat):
	mTargetPixelFormat(targetPixelFormat)
{
	// Check params

	if (inputCodecId == AV_CODEC_ID_NONE)
		throw std::runtime_error("Need valid AV_CODEC_ID_*");

	if (targetPixelFormat == AV_PIX_FMT_NONE)
		throw std::runtime_error("Need valid AV_PIX_FMT_*");

	if(!sws_isSupportedOutput(mTargetPixelFormat))
		throw std::runtime_error("Target pixel format not supported by swscale");

	// Build decoder and context

	const AVCodec* avCodecDecoder = avcodec_find_decoder(inputCodecId);
	if (!avCodecDecoder)
		throw std::runtime_error("Codec not found");

	mAVCodecContext = avcodec_alloc_context3(avCodecDecoder);
	if (!mAVCodecContext)
		throw std::runtime_error("Could not allocate video codec context");

	// This is a non-standard ffmpeg extension signalling no use of other threads
	mAVCodecContext->thread_count = -1;

	if (avcodec_open2(mAVCodecContext, avCodecDecoder, nullptr) < 0)
		throw std::runtime_error("Could not open codec");

	if(!sws_isSupportedInput(mAVCodecContext->pix_fmt))
		throw std::runtime_error("Source decoder pixel format not supported");

	// Alloc used buffers

	mInputFrame = av_frame_alloc();
	if (!mInputFrame)
		throw std::runtime_error("Failed to alloc input frame");

	mOutputFrame = av_frame_alloc();
	if (!mOutputFrame)
		throw std::runtime_error("Failed to alloc output frame");

	mPkt = av_packet_alloc();
	if (!mPkt)
		throw std::runtime_error("Failed to alloc mPkt");
}


CFFMpegDecoderVideoFrameFormatter::~CFFMpegDecoderVideoFrameFormatter()
{
	Cleanup();

	if (mAVCodecContext)
	{
		avcodec_close(mAVCodecContext);
		avcodec_free_context(&mAVCodecContext);
	}

	if(mInputFrame)
		av_frame_free(&mInputFrame);

	if (mOutputFrame)
		av_frame_free(&mOutputFrame);

	if (mPkt)
		av_packet_free(&mPkt);
}


void CFFMpegDecoderVideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

	Cleanup();

	mInputBytesPerVideoFrame = videoState->BytesPerFrame();
	assert(mInputBytesPerVideoFrame > 0);

	mHeight = videoState->displayMode->FrameHeight();
	assert(mHeight > 0);

	mWidth = videoState->displayMode->FrameWidth();
	assert(mWidth > 0);

	// Update codec context with width and height
	mAVCodecContext->height = mHeight;
	mAVCodecContext->width = mWidth;

	// Build context
	mSws = sws_getContext(
		mWidth, mHeight,
		mAVCodecContext->pix_fmt,
		mWidth, mHeight,
		mTargetPixelFormat,
		SWS_FAST_BILINEAR,
		nullptr,
		nullptr,
		nullptr);

	if (!mSws)
		throw std::runtime_error("Failed to get context");

	if(!av_image_alloc(
		mOutputFrame->data, mOutputFrame->linesize,
		mWidth, mHeight,
		mTargetPixelFormat,
		OUTPUT_LINESIZE_ALIGNMENT))
		throw std::runtime_error("Failed to allocate mOutputFrame image");

	mOutFrameSize = av_image_get_buffer_size(
		mTargetPixelFormat,
		mWidth, mHeight,
		OUTPUT_LINESIZE_ALIGNMENT);

	if(mOutFrameSize <= 0)
		throw std::runtime_error("Failed to get output frame size");
}


bool CFFMpegDecoderVideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	assert(mOutFrameSize > 0);  // Means it's set up

	if (mWidth == 0 || mHeight == 0 || mInputBytesPerVideoFrame == 0)
		throw std::runtime_error("Width, height or bytes per frame not known, call OnVideoState() first");

	mPkt->data = (uint8_t*)inFrame.GetData();
	mPkt->size = mInputBytesPerVideoFrame;

	// Decode
	int ret = avcodec_send_packet(mAVCodecContext, mPkt);
	if (ret < 0)
		throw std::runtime_error("Failed to send packet for decoding");

	ret = avcodec_receive_frame(mAVCodecContext, mInputFrame);
	if (ret == AVERROR(EAGAIN))
		return false;
	if (ret == AVERROR_EOF)
		throw std::runtime_error("Unexpected return for avcodec_receive_frame");
	if (ret < 0)
		throw std::runtime_error("avcodec_receive_frame errored");

	// ?? OPTIMIZATION 1: Direct scaling to output buffer when possible
	// Check if we can scale directly to the output buffer to eliminate the extra copy
	const bool canScaleDirectly = (mTargetPixelFormat == AV_PIX_FMT_P010LE || 
	                               mTargetPixelFormat == AV_PIX_FMT_YUV420P10LE) &&
	                              (mOutputFrame->linesize[0] == mWidth * 2); // 16-bit format check

	if (canScaleDirectly)
	{
		// ?? ZERO-COPY: Scale directly into the output buffer
		// Set up temporary data pointers to output buffer
		uint8_t* tempData[4] = { nullptr, nullptr, nullptr, nullptr };
		int tempLinesize[4] = { 0, 0, 0, 0 };
		
		// Calculate proper linesize and data pointers for P010/YUV420P10LE
		const int ySize = mWidth * mHeight * 2;  // 16-bit Y plane
		const int uvSize = (mWidth/2) * (mHeight/2) * 2;  // 16-bit UV planes
		
		if (mTargetPixelFormat == AV_PIX_FMT_P010LE)
		{
			tempData[0] = outBuffer;              // Y plane
			tempData[1] = outBuffer + ySize;      // Interleaved UV plane
			tempLinesize[0] = mWidth * 2;
			tempLinesize[1] = mWidth * 2;
		}
		else // AV_PIX_FMT_YUV420P10LE
		{
			tempData[0] = outBuffer;                    // Y plane
			tempData[1] = outBuffer + ySize;            // U plane  
			tempData[2] = outBuffer + ySize + uvSize;   // V plane
			tempLinesize[0] = mWidth * 2;
			tempLinesize[1] = mWidth;
			tempLinesize[2] = mWidth;
		}

		// Scale directly to output buffer - ELIMINATES COPY!
		int scaled_lines = sws_scale(
			mSws,
			mInputFrame->data, mInputFrame->linesize,
			0, mHeight,
			tempData, tempLinesize);
			
		if (scaled_lines != mHeight)
			throw std::runtime_error("Failed to sws_scale all lines");
			
		// No copy needed! Data is already in the output buffer
		return true;
	}
	else
	{
		// ?? OPTIMIZATION 2: Use optimized memory copy for fallback case
		// Convert to intermediate buffer first (existing path)
		int scaled_lines = sws_scale(
			mSws,
			mInputFrame->data, mInputFrame->linesize,
			0, mHeight,
			mOutputFrame->data, mOutputFrame->linesize);
		if (scaled_lines != mHeight)
			throw std::runtime_error("Failed to sws_scale all lines");

		// ?? OPTIMIZED COPY: Use platform-specific optimized copy instead of av_image_copy_to_buffer
		OptimizedFrameCopy(outBuffer, mOutputFrame->data, mOutputFrame->linesize, 
		                  mTargetPixelFormat, mWidth, mHeight);
		return true;
	}
}

// ?? NEW: Platform-optimized copy function
void CFFMpegDecoderVideoFrameFormatter::OptimizedFrameCopy(
	uint8_t* dst, uint8_t* const src[4], const int srcLinesize[4],
	AVPixelFormat pixFmt, int width, int height)
{
	// Use SIMD-optimized memory copy operations
	switch (pixFmt)
	{
	case AV_PIX_FMT_P010LE:
	{
		// P010: Y plane + interleaved UV plane
		const int yPlaneSize = width * height * 2;
		const int uvPlaneSize = width * (height / 2) * 2;
		
		// Copy Y plane with optimized copy
		OptimizedMemcpy(dst, src[0], yPlaneSize);
		
		// Copy UV plane
		OptimizedMemcpy(dst + yPlaneSize, src[1], uvPlaneSize);
		break;
	}
	case AV_PIX_FMT_YUV420P10LE:
	{
		// YUV420P10LE: Separate Y, U, V planes
		const int yPlaneSize = width * height * 2;
		const int uvPlaneSize = (width / 2) * (height / 2) * 2;
		
		OptimizedMemcpy(dst, src[0], yPlaneSize);
		OptimizedMemcpy(dst + yPlaneSize, src[1], uvPlaneSize);
		OptimizedMemcpy(dst + yPlaneSize + uvPlaneSize, src[2], uvPlaneSize);
		break;
	}
	default:
		// Fallback to original method for other formats
		av_image_copy_to_buffer(dst, mOutFrameSize, src, srcLinesize,
		                       pixFmt, width, height, OUTPUT_LINESIZE_ALIGNMENT);
		break;
	}
}

// ?? NEW: SIMD-optimized memory copy
void CFFMpegDecoderVideoFrameFormatter::OptimizedMemcpy(void* dst, const void* src, size_t size)
{
	// Use safe AVX2 approach - same as other formatters
    if (size >= 128 && HasAVX2_Safe())
    {
        const size_t avx2Chunks = size / 32;
        const size_t remainder = size % 32;
        
        const __m256i* srcVec = (const __m256i*)src;
        __m256i* dstVec = (__m256i*)dst;
        
        // Process in blocks of 4 AVX2 registers (128 bytes) with prefetching
        const size_t blockSize = 4;
        const size_t blocks = avx2Chunks / blockSize;
        
        for (size_t block = 0; block < blocks; ++block)
        {
            const size_t i = block * blockSize;
            
            // Prefetch next block
            if (block + 2 < blocks)
            {
                _mm_prefetch((const char*)(srcVec + (block + 2) * blockSize), _MM_HINT_T0);
            }
            
            // Copy 4 x 32-byte chunks (128 bytes total)
            _mm256_storeu_si256(dstVec + i + 0, _mm256_loadu_si256(srcVec + i + 0));
            _mm256_storeu_si256(dstVec + i + 1, _mm256_loadu_si256(srcVec + i + 1));
            _mm256_storeu_si256(dstVec + i + 2, _mm256_loadu_si256(srcVec + i + 2));
            _mm256_storeu_si256(dstVec + i + 3, _mm256_loadu_si256(srcVec + i + 3));
        }
        
        // Handle remaining full AVX2 chunks
        for (size_t i = blocks * blockSize; i < avx2Chunks; ++i)
        {
            _mm256_storeu_si256(dstVec + i, _mm256_loadu_si256(srcVec + i));
        }
        
        // Handle remainder
        if (remainder > 0)
        {
            memcpy((uint8_t*)dst + avx2Chunks * 32, 
                   (const uint8_t*)src + avx2Chunks * 32, remainder);
        }
    }
    else
    {
        // Use standard memcpy for smaller sizes or when AVX2 unavailable
        memcpy(dst, src, size);
    }
}

// Safe CPU feature detection for FFmpeg formatter
bool CFFMpegDecoderVideoFrameFormatter::HasAVX2_Safe() const
{
    static int checked = -1;
    
    if (checked == -1)
    {
        int cpuInfo[4];
        __cpuid(cpuInfo, 0);
        if (cpuInfo[0] >= 7)
        {
            __cpuid(cpuInfo, 7);
            bool hasAVX2 = (cpuInfo[1] & (1 << 5)) != 0;
            __cpuid(cpuInfo, 1);
            bool hasAVX = (cpuInfo[2] & (1 << 28)) != 0;
            bool osSupportsAVX = (cpuInfo[2] & (1 << 27)) != 0;
            checked = (hasAVX2 && hasAVX && osSupportsAVX) ? 1 : 0;
        }
        else
        {
            checked = 0;
        }
        
        #ifdef _DEBUG
        static bool logged = false;
        if (!logged) {
            DbgLog((LOG_TRACE, 1, TEXT("CFFMpegDecoderVideoFrameFormatter: Safe AVX2 Memory Operations - %s"), 
                    checked == 1 ? TEXT("ENABLED") : TEXT("DISABLED")));
            logged = true;
        }
        #endif
    }
    
    return checked == 1;
}

LONG CFFMpegDecoderVideoFrameFormatter::GetOutFrameSize() const
{
	assert(mOutFrameSize > 0);
	return mOutFrameSize;
}

void CFFMpegDecoderVideoFrameFormatter::Cleanup()
{
	if (mOutFrameSize > 0)
	{
		sws_freeContext(mSws);

		av_freep(&mOutputFrame->data[0]);
	}
}
