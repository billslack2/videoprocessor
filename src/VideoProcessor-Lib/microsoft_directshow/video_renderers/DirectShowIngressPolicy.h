#pragma once

#include <VideoConversionOverride.h>
#include <VideoFrameEncoding.h>

enum class DirectShowIngressFamily
{
	MPC,
	GENERIC,
};

inline bool IsDeckLinkPackedRgbP010Encoding(
	VideoFrameEncoding encoding) noexcept
{
	return encoding == VideoFrameEncoding::R210 ||
		encoding == VideoFrameEncoding::R10b ||
		encoding == VideoFrameEncoding::R10l ||
		encoding == VideoFrameEncoding::R12B ||
		encoding == VideoFrameEncoding::R12L;
}

inline bool DirectShowCanConvertToP010(VideoFrameEncoding encoding)
{
	switch (encoding)
	{
	case VideoFrameEncoding::ARGB_8BIT:
	case VideoFrameEncoding::BGRA_8BIT:
	case VideoFrameEncoding::UYVY:
	case VideoFrameEncoding::HDYC:
	case VideoFrameEncoding::V210:
	case VideoFrameEncoding::R210:
	case VideoFrameEncoding::R10b:
	case VideoFrameEncoding::R10l:
	case VideoFrameEncoding::R12B:
	case VideoFrameEncoding::R12L:
		return true;
	default:
		return false;
	}
}

inline bool DirectShowUsesP010Ingress(DirectShowIngressFamily family,
	VideoFrameEncoding encoding, VideoConversionOverride conversionOverride)
{
	if (conversionOverride ==
		VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010)
		return DirectShowCanConvertToP010(encoding);

	if (encoding == VideoFrameEncoding::R10b ||
		encoding == VideoFrameEncoding::R10l ||
		encoding == VideoFrameEncoding::R12L)
		return true;

	// MPC already converted these formats automatically. Generic DirectShow
	// must do the same because it has no direct ARGB/BGRA media-subtype mapping.
	return encoding == VideoFrameEncoding::ARGB_8BIT ||
		encoding == VideoFrameEncoding::BGRA_8BIT;
}
