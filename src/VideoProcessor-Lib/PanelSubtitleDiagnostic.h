#pragma once

#include <cstddef>
#include <cstdint>

#include <PanelSubtitleDetector.h>
#include <PanelSubtitleTestMode.h>

// Renderer-neutral, deliberately visible diagnostic rendering for the panel
// subtitle detector. It operates in-place on an already-converted P010 frame;
// it never recognizes text. In explicit Move mode it can erase the detected
// glyph mask and composite captured glyph pixels inside the active picture.
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
	// Validation-only renderer. Highlight draws one locked cue box plus all of
	// its current member masks. A stale soft-release result never mutates.
	// Move captures the current glyph pixels, erases only their source mask,
	// and composites them into deterministic boxes wholly inside the active
	// picture. Candidate/unavailable/stale geometry never mutates the surface.
	static bool Apply(const PanelSubtitleResult& result,
		const PanelSubtitleDiagnosticSurface& surface,
		PanelSubtitleTestMode mode = PanelSubtitleTestMode::Off,
		int activePictureTop = 0, int activePictureBottom = 0);

private:
	static bool IsValid(const PanelSubtitleRect& rectangle,
		const PanelSubtitleDiagnosticSurface& surface);
	static void PaintPixel(const PanelSubtitleDiagnosticSurface& surface,
		int x, int y, uint16_t luma, uint16_t cb, uint16_t cr);
	static void PaintOutline(const PanelSubtitleDiagnosticSurface& surface,
		const PanelSubtitleRect& rectangle, int thickness, uint16_t luma,
		uint16_t cb, uint16_t cr);
	static bool Highlight(const PanelSubtitleResult& result,
		const PanelSubtitleDiagnosticSurface& surface);
	static bool Move(const PanelSubtitleResult& result,
		const PanelSubtitleDiagnosticSurface& surface,
		int activePictureTop, int activePictureBottom);
};
