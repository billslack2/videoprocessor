/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <DeckLinkAPI_h.h>

#include <ColorFormat.h>
#include <BitDepth.h>
#include <VideoFrameEncoding.h>
#include <EOTF.h>
#include <ColorSpace.h>
#include <DisplayMode.h>
#include <ConfigFile.h>

#include <functional>
#include <string>


struct DeckLinkCaptureFormatPreferences
{
	BMDPixelFormat rgb8 = bmdFormat8BitARGB;
	BMDPixelFormat rgb10 = bmdFormat10BitRGB;
	BMDPixelFormat rgb12 = bmdFormat12BitRGB;
};

bool ReadDeckLinkCaptureFormatPreferences(const ConfigFile& config,
	DeckLinkCaptureFormatPreferences& preferences, std::string& error);

BMDPixelFormat CanonicalDeckLinkCapturePixelFormat(
	BMDDetectedVideoInputFormatFlags detectedSignalFlags);

BMDPixelFormat PreferredDeckLinkCapturePixelFormat(
	BMDDetectedVideoInputFormatFlags detectedSignalFlags,
	const DeckLinkCaptureFormatPreferences& preferences);

BMDPixelFormat ResolveDeckLinkCapturePixelFormat(
	BMDDetectedVideoInputFormatFlags detectedSignalFlags,
	const DeckLinkCaptureFormatPreferences& preferences,
	const std::function<bool(BMDPixelFormat)>& isSupported,
	bool& usedFallback);

const char* DeckLinkPixelFormatName(BMDPixelFormat pixelFormat);


ColorFormat TranslateColorFormat(BMDDetectedVideoInputFormatFlags detectedVideoInputFormatFlagsValue);

BitDepth TranslateBithDepth(BMDDetectedVideoInputFormatFlags detectedVideoInputFormatFlagsValue);

VideoFrameEncoding Translate(BMDPixelFormat, ColorSpace);

EOTF TranslateEOTF(LONGLONG electroOpticalTransferFuncValue);

ColorSpace Translate(BMDColorspace, uint32_t verticalLines);

DisplayModeSharedPtr Translate(BMDDisplayMode);

double FPS(BMDDisplayMode displayMode);
