/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


extern "C"
{
	#include <libswscale/swscale.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/frame.h>
	#include <libavcodec/codec.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/hwcontext.h>
}

#include <video_frame_formatter/IVideoFrameFormatter.h>


/**
 * This formatter can convert using an ffmpeg decoder and scaler for a target pixel format
 * Supports optional hardware acceleration (DXVA2/D3D11VA) with safe fallback to software decoding
 */
class CFFMpegDecoderVideoFrameFormatter:
	public IVideoFrameFormatter
{
public:

	CFFMpegDecoderVideoFrameFormatter(
		AVCodecID inputCodecId,
		AVPixelFormat targetPixelFormat,
		bool useHardwareDecoding = true);  // Optional: enable hardware decoding (default: true)
	virtual ~CFFMpegDecoderVideoFrameFormatter();

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;

	// Hardware decoding info
	bool IsUsingHardwareDecoding() const { return m_usingHardwareDecoding; }
	const char* GetDecoderType() const { return m_decoderType; }

	// Public method to get conversion performance metrics
	void GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const
	{
		currentUs = m_performanceWindow.lastTimeUs;
		avg10s = m_performanceWindow.GetAverage();
		max10s = m_performanceWindow.GetMax();
	}

private:

	const AVPixelFormat mTargetPixelFormat;
	const AVCodec* mAVCodecDecoder;
	AVCodecContext* mAVCodecContext;

	int mInputBytesPerVideoFrame = 0;
	int mHeight = 0;
	int mWidth = 0;
	LONG mOutFrameSize = 0;

	struct SwsContext* mSws = nullptr;
	AVFrame* mInputFrame = nullptr;
	AVFrame* mOutputFrame = nullptr;
	AVFrame* m_swFrameForHWDecode = nullptr;  // Frame for transferring hw decoded data to RAM

	AVPacket* mPkt;
	
	// Hardware decoding support
	bool m_enableHardwareDecoding = true;
	bool m_usingHardwareDecoding = false;
	const char* m_decoderType = "Software";  // "Hardware" or "Software"
	AVBufferRef* m_hwDeviceCtx = nullptr;

	// Rolling window performance tracking (for stats overlay)
	struct RollingPerformanceWindow
	{
		static const size_t WINDOW_SIZE = 600;  // 10 seconds @ 60fps
		double times[WINDOW_SIZE];
		size_t currentIndex = 0;
		size_t samplesCollected = 0;
		double lastTimeUs = 0.0;
		
		void AddSample(double timeUs)
		{
			lastTimeUs = timeUs;
			times[currentIndex] = timeUs;
			currentIndex = (currentIndex + 1) % WINDOW_SIZE;
			if (samplesCollected < WINDOW_SIZE)
				samplesCollected++;
		}
		
		double GetAverage() const
		{
			if (samplesCollected == 0) return 0.0;
			double sum = 0.0;
			for (size_t i = 0; i < samplesCollected; i++)
				sum += times[i];
			return sum / samplesCollected;
		}
		
		double GetMax() const
		{
			if (samplesCollected == 0) return 0.0;
			double maxVal = times[0];
			for (size_t i = 1; i < samplesCollected; i++)
				if (times[i] > maxVal)
					maxVal = times[i];
			return maxVal;
		}
	};
	
	mutable RollingPerformanceWindow m_performanceWindow;

	void Cleanup();
	
	// Hardware decoding methods
	bool TryInitializeHardwareDecoding(AVCodecID inputCodecId);
	AVPixelFormat GetHardwarePixelFormat(AVCodecContext* ctx, const AVPixelFormat* fmt);
	bool TransferHardwareFrameToCPU(AVFrame* hwFrame, AVFrame* swFrame);
};
