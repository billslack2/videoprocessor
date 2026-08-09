#pragma once

#include <VideoConversionOverride.h>
#include <VideoFrameEncoding.h>
#include "DirectShowIngressPolicy.h"

// madVR receives a small set of capture formats through VP-owned P010
// conversion. Keep this decision independent of COM/media-type negotiation so
// format additions cannot silently fall back to an incompatible subtype.
inline bool MadVRUsesP010Ingress(VideoFrameEncoding encoding,
	VideoConversionOverride conversionOverride) noexcept
{
	return conversionOverride ==
			VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010 ||
		encoding == VideoFrameEncoding::ARGB_8BIT ||
		encoding == VideoFrameEncoding::BGRA_8BIT ||
		encoding == VideoFrameEncoding::R10b ||
		encoding == VideoFrameEncoding::R10l ||
		encoding == VideoFrameEncoding::R12B ||
		encoding == VideoFrameEncoding::R12L;
}
