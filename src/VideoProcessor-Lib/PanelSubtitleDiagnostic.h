#pragma once

#include <cstddef>
#include <cstdint>

#include <PanelSubtitleDetector.h>

// Renderer-neutral, deliberately visible diagnostic rendering for the panel
// subtitle detector. It operates in-place on an already-converted P010 frame;
// it does not recognize, redraw, erase, or relocate subtitle text.
struct PanelSubtitleDiagnosticSurface
{
	uint16_t* p010Luma = nullptr;
	uint16_t* p010Chroma = nullptr;
	size_t width = 0;
	size_t height = 0;
	size_t lumaStrideBytes = 0;
	size_t chromaStrideBytes = 0;
};

class PanelSubtitleDiagnostic
{
public:
	// Draws a magenta panel outline, yellow glyph bounds, and cyan glyph-mask
	// tint. Returns false without touching the frame for unavailable/malformed
	// detector results or malformed P010 surfaces.
	static bool Apply(const PanelSubtitleResult& result,
		const PanelSubtitleDiagnosticSurface& surface);

private:
	static bool IsValid(const PanelSubtitleRect& rectangle,
		const PanelSubtitleDiagnosticSurface& surface);
	static void PaintPixel(const PanelSubtitleDiagnosticSurface& surface,
		int x, int y, uint16_t luma, uint16_t cb, uint16_t cr);
	static void PaintOutline(const PanelSubtitleDiagnosticSurface& surface,
		const PanelSubtitleRect& rectangle, int thickness, uint16_t luma,
		uint16_t cb, uint16_t cr);
};
