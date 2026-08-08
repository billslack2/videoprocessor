#pragma once

#include <cstdint>

namespace NativeStatsOverlayBitmap
{
	// GDI HALFTONE scaling improves the RGB glyph edges of a 32-bit DIB but
	// clears its otherwise-unused alpha byte. madVR consumes that byte for its
	// masking-aware OSD, so restore alpha independently with nearest-neighbor
	// sampling after GDI has scaled the color channels.
	inline bool RestoreScaledAlphaNearest(const uint8_t* source,
		int sourceWidth, int sourceHeight, int sourceStride,
		uint8_t* target, int targetWidth, int targetHeight, int targetStride)
	{
		if (!source || !target || sourceWidth <= 0 || sourceHeight <= 0 ||
			targetWidth <= 0 || targetHeight <= 0 ||
			sourceStride < sourceWidth * 4 || targetStride < targetWidth * 4)
			return false;

		for (int y = 0; y < targetHeight; ++y)
		{
			const int sourceY = y * sourceHeight / targetHeight;
			for (int x = 0; x < targetWidth; ++x)
			{
				const int sourceX = x * sourceWidth / targetWidth;
				target[y * targetStride + x * 4 + 3] =
					source[sourceY * sourceStride + sourceX * 4 + 3];
			}
		}
		return true;
	}
}
