/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <video_frame_formatter/IVideoFrameFormatter.h>

/**
 * Video frame formatter which reads UYVY (8-bit 4:2:2) and writes to P010 (10-bit 4:2:0 planar)
 * Upscales 8-bit to 10-bit by bit-shifting
 * Downsamples chroma from 4:2:2 to 4:2:0 by averaging vertical pairs
 */
class CUYVYtoP010VideoFrameFormatter : public IVideoFrameFormatter
{
public:
	CUYVYtoP010VideoFrameFormatter() = default;
	virtual ~CUYVYtoP010VideoFrameFormatter() = default;

	// IVideoFrameFormatter
	void OnVideoState(VideoStateComPtr& videoState) override;
	bool FormatVideoFrame(const VideoFrame& inFrame, BYTE* outBuffer) override;
	LONG GetOutFrameSize() const override;

private:
	uint32_t m_height = 0;
	uint32_t m_width = 0;
	uint32_t m_srcStride = 0;  // Source stride from VideoState
};
