#include <pch.h>

#include <PanelSubtitleDiagnostic.h>

#include <algorithm>

namespace
{
	constexpr uint16_t P010(uint16_t code)
	{
		return static_cast<uint16_t>(std::min<uint16_t>(1023, code) << 6);
	}
}

bool PanelSubtitleDiagnostic::IsValid(const PanelSubtitleRect& rectangle,
	const PanelSubtitleDiagnosticSurface& surface)
{
	return rectangle.left >= 0 && rectangle.top >= 0 &&
		rectangle.right > rectangle.left && rectangle.bottom > rectangle.top &&
		static_cast<size_t>(rectangle.right) <= surface.width &&
		static_cast<size_t>(rectangle.bottom) <= surface.height;
}

void PanelSubtitleDiagnostic::PaintPixel(
	const PanelSubtitleDiagnosticSurface& surface, int x, int y,
	uint16_t luma, uint16_t cb, uint16_t cr)
{
	if (x < 0 || y < 0 || static_cast<size_t>(x) >= surface.width ||
		static_cast<size_t>(y) >= surface.height)
		return;

	uint16_t* yRow = reinterpret_cast<uint16_t*>(
		reinterpret_cast<uint8_t*>(surface.p010Luma) +
		static_cast<size_t>(y) * surface.lumaStrideBytes);
	yRow[x] = P010(luma);

	uint16_t* uvRow = reinterpret_cast<uint16_t*>(
		reinterpret_cast<uint8_t*>(surface.p010Chroma) +
		static_cast<size_t>(y / 2) * surface.chromaStrideBytes);
	const int chromaX = x / 2;
	uvRow[chromaX * 2] = P010(cb);
	uvRow[chromaX * 2 + 1] = P010(cr);
}

void PanelSubtitleDiagnostic::PaintOutline(
	const PanelSubtitleDiagnosticSurface& surface,
	const PanelSubtitleRect& rectangle, int thickness, uint16_t luma,
	uint16_t cb, uint16_t cr)
{
	if (!IsValid(rectangle, surface))
		return;

	const int clampedThickness = std::max(1, std::min(thickness,
		std::min(rectangle.right - rectangle.left,
			rectangle.bottom - rectangle.top) / 2));
	for (int y = rectangle.top; y < rectangle.bottom; ++y)
	{
		for (int x = rectangle.left; x < rectangle.right; ++x)
		{
			if (x - rectangle.left < clampedThickness ||
				rectangle.right - 1 - x < clampedThickness ||
				y - rectangle.top < clampedThickness ||
				rectangle.bottom - 1 - y < clampedThickness)
				PaintPixel(surface, x, y, luma, cb, cr);
		}
	}
}

bool PanelSubtitleDiagnostic::Apply(const PanelSubtitleResult& result,
	const PanelSubtitleDiagnosticSurface& surface)
{
	if (result.state == PanelSubtitleState::Unavailable || !surface.p010Luma ||
		!surface.p010Chroma || surface.width == 0 || surface.height == 0 ||
		surface.lumaStrideBytes < surface.width * sizeof(uint16_t) ||
		surface.chromaStrideBytes < surface.width * sizeof(uint16_t) ||
		result.rasterWidth != surface.width || result.rasterHeight != surface.height ||
		!IsValid(result.panelBounds, surface) ||
		!IsValid(result.glyphBounds, surface))
		return false;

	// Panel border: magenta. Glyph rectangle: yellow. Glyph pixels: cyan.
	PaintOutline(surface, result.panelBounds, 3, 900, 896, 896);
	if (result.softGlyphMask && result.softGlyphMask->size() >=
		surface.width * surface.height && IsValid(result.maskBounds, surface))
	{
		for (int y = result.maskBounds.top; y < result.maskBounds.bottom; ++y)
		{
			for (int x = result.maskBounds.left; x < result.maskBounds.right; ++x)
			{
				if ((*result.softGlyphMask)[static_cast<size_t>(y) *
					surface.width + x] != 0)
					PaintPixel(surface, x, y, 900, 896, 128);
			}
		}
	}
	PaintOutline(surface, result.glyphBounds, 2, 900, 128, 896);
	return true;
}
