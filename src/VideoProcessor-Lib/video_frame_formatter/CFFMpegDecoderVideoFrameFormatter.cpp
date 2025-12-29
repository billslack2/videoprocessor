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
	AVPixelFormat targetPixelFormat,
	bool useHardwareDecoding):
	mTargetPixelFormat(targetPixelFormat),
	m_enableHardwareDecoding(useHardwareDecoding)
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

	// Attempt hardware decoding initialization if enabled
	if (m_enableHardwareDecoding)
	{
		if (TryInitializeHardwareDecoding(inputCodecId))
		{
			m_usingHardwareDecoding = true;
			m_decoderType = "Hardware (D3D11VA)";
		}
		else
		{
			// Hardware decoding failed, fall back to software
			m_usingHardwareDecoding = false;
			m_decoderType = "Software (fallback from hardware attempt)";
		}
	}
	else
	{
		m_usingHardwareDecoding = false;
		m_decoderType = "Software (hardware disabled)";
	}

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

	// Allocate frame for hardware to CPU transfer if using hardware decoding
	if (m_usingHardwareDecoding)
	{
		m_swFrameForHWDecode = av_frame_alloc();
		if (!m_swFrameForHWDecode)
			throw std::runtime_error("Failed to alloc frame for hardware decode transfer");
	}

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

	if (m_swFrameForHWDecode)
		av_frame_free(&m_swFrameForHWDecode);

	if (mPkt)
		av_packet_free(&mPkt);

	// Clean up hardware device context
	if (m_hwDeviceCtx)
	{
		av_buffer_unref(&m_hwDeviceCtx);
		m_hwDeviceCtx = nullptr;
	}
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

	const auto startTime = GetWallClockTime();

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

	// Handle hardware decoded frame transfer
	AVFrame* decodedFrame = mInputFrame;
	if (m_usingHardwareDecoding && mInputFrame->format == AV_PIX_FMT_D3D11)
	{
		// Transfer hardware frame to CPU memory for further processing
		if (!TransferHardwareFrameToCPU(mInputFrame, m_swFrameForHWDecode))
		{
			DbgLog((LOG_TRACE, 1, TEXT("Warning: Hardware frame transfer failed, using original frame")));
			// Continue with original frame, may have partial data
		}
		else
		{
			decodedFrame = m_swFrameForHWDecode;
		}
	}

	// Convert to intermediate buffer
	int scaled_lines = sws_scale(
		mSws,
		decodedFrame->data, decodedFrame->linesize,
		0, mHeight,
		mOutputFrame->data, mOutputFrame->linesize);
	if (scaled_lines != mHeight)
		throw std::runtime_error("Failed to sws_scale all lines");

	// Copy to output buffer
	int ret2 = av_image_copy_to_buffer(
		outBuffer, mOutFrameSize,
		(const uint8_t* const*)mOutputFrame->data, mOutputFrame->linesize,
		mTargetPixelFormat,
		mWidth, mHeight,
		OUTPUT_LINESIZE_ALIGNMENT);
	if (ret2 < 0)
		throw std::runtime_error("Failed to copy image to buffer");

	const auto endTime = GetWallClockTime();
	const uint64_t conversionTimeUs = (endTime - startTime) / 10;
	m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

	return true;
}

// Hardware decoding initialization
bool CFFMpegDecoderVideoFrameFormatter::TryInitializeHardwareDecoding(AVCodecID inputCodecId)
{
	// Attempt to find hardware decoder for D3D11VA (Windows)
	const char* hwDecoderName = nullptr;
	AVHWDeviceType deviceType = AV_HWDEVICE_TYPE_NONE;

	// Map codec to hardware decoder name
	switch (inputCodecId)
	{
	case AV_CODEC_ID_H264:
		hwDecoderName = "h264_d3d11va";
		deviceType = AV_HWDEVICE_TYPE_D3D11VA;
		break;
	case AV_CODEC_ID_HEVC:
		hwDecoderName = "hevc_d3d11va";
		deviceType = AV_HWDEVICE_TYPE_D3D11VA;
		break;
	case AV_CODEC_ID_VP9:
		hwDecoderName = "vp9_d3d11va";
		deviceType = AV_HWDEVICE_TYPE_D3D11VA;
		break;
	case AV_CODEC_ID_AV1:
		hwDecoderName = "av1_d3d11va";
		deviceType = AV_HWDEVICE_TYPE_D3D11VA;
		break;
	default:
		// No hardware decoder available for this codec
		DbgLog((LOG_TRACE, 1, TEXT("Hardware decoding not available for codec ID %d"), inputCodecId));
		return false;
	}

	if (!hwDecoderName || deviceType == AV_HWDEVICE_TYPE_NONE)
		return false;

	// Try to find the hardware decoder
	const AVCodec* hwCodec = avcodec_find_decoder_by_name(hwDecoderName);
	if (!hwCodec)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Hardware decoder '%s' not found on this system"), 
			hwDecoderName));
		return false;
	}

	// Create hardware device context
	int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, deviceType, nullptr, nullptr, 0);
	if (ret < 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Failed to create hardware device context for D3D11VA (error %d)"), ret));
		return false;
	}

	// Update codec context to use hardware device
	mAVCodecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
	if (!mAVCodecContext->hw_device_ctx)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Failed to reference hardware device context")));
		av_buffer_unref(&m_hwDeviceCtx);
		m_hwDeviceCtx = nullptr;
		return false;
	}

	DbgLog((LOG_TRACE, 1, TEXT("Hardware decoding (D3D11VA) initialized for codec '%s'"), hwDecoderName));
	return true;
}

// Frame transfer from hardware to CPU memory
bool CFFMpegDecoderVideoFrameFormatter::TransferHardwareFrameToCPU(AVFrame* hwFrame, AVFrame* swFrame)
{
	if (!hwFrame || !swFrame)
		return false;

	// Check if frame is on hardware
	if (hwFrame->format != AV_PIX_FMT_D3D11)
	{
		// Frame is already on CPU, no transfer needed
		return true;
	}

	// Transfer data from GPU to CPU
	int ret = av_hwframe_transfer_data(swFrame, hwFrame, 0);
	if (ret < 0)
	{
		DbgLog((LOG_TRACE, 1, TEXT("Failed to transfer hardware frame to CPU memory (error %d)"), ret));
		return false;
	}

	return true;
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
