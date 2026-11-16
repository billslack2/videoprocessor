/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <unknwn.h>


/**
 * One subtitle or message image a source delivered with a frame, never drawn
 * into the frame's picture (see source_plugin/vp_source_plugin.h, Cues).
 * Premultiplied BGRA, sRGB, white at 1.0. Immutable once made; frames that
 * show the same image share one.
 */
struct SourceCue
{
	uint64_t id = 0;
	int32_t x = 0;                  // bounds in the source frame's pixels
	int32_t y = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> pixels;    // width * 4 bytes a row, no padding
};


/**
 * The cues on one frame. Either may be empty. whiteNits is the level a cue's
 * white is drawn at on screen.
 */
struct SourceCues
{
	std::shared_ptr<const SourceCue> subtitle;
	std::shared_ptr<const SourceCue> message;
	float whiteNits = 50.0f;
};


/**
 * Implemented by a frame's source buffer (VideoFrame::GetSourceBuffer) when
 * its source delivers cues. Query it with QueryInterface.
 */
struct __declspec(uuid("5819138b-d685-4326-b19f-ed9bce267fe8"))
ISourceCueCarrier: public IUnknown
{
	virtual SourceCues GetSourceCues() const = 0;
};


// The cues a frame's source buffer carries, or none. Read by the renderer for
// every formatter, so a frame keeps its cues whichever formatter converts it.
inline SourceCues ReadSourceCues(IUnknown* sourceBuffer)
{
	SourceCues cues;
	ISourceCueCarrier* carrier = nullptr;
	if (sourceBuffer && SUCCEEDED(sourceBuffer->QueryInterface(
		__uuidof(ISourceCueCarrier), reinterpret_cast<void**>(&carrier))))
	{
		cues = carrier->GetSourceCues();
		carrier->Release();
	}
	return cues;
}
