#include <pch.h>

#include <PanelSubtitleDiagnostic.h>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
	constexpr uint16_t P010(uint16_t code)
	{
		return static_cast<uint16_t>(std::min<uint16_t>(1023, code) << 6);
	}

	int RectHeight(const PanelSubtitleRect& rectangle)
	{
		return std::max(0, rectangle.bottom - rectangle.top);
	}

	struct MoveOperation
	{
		PanelSubtitleRect source;
		PanelSubtitleRect destination;
		int shiftY = 0;
		uint16_t backingLuma = 64;
		std::vector<int> maskIndices;
		std::vector<uint16_t> luma;
		std::vector<int> chromaIndices;
		std::vector<std::array<uint16_t, 2>> chroma;
	};

	bool IsTopCue(PanelSubtitleLocation location)
	{
		return location == PanelSubtitleLocation::TopBoundary ||
			location == PanelSubtitleLocation::TopBar;
	}

	bool IsBottomCue(PanelSubtitleLocation location)
	{
		return location == PanelSubtitleLocation::BottomBoundary ||
			location == PanelSubtitleLocation::BottomBar;
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
		for (int x = rectangle.left; x < rectangle.right; ++x)
			if (x - rectangle.left < clampedThickness ||
				rectangle.right - 1 - x < clampedThickness ||
				y - rectangle.top < clampedThickness ||
				rectangle.bottom - 1 - y < clampedThickness)
				PaintPixel(surface, x, y, luma, cb, cr);
}

bool PanelSubtitleDiagnostic::Highlight(const PanelSubtitleResult& result,
	const PanelSubtitleDiagnosticSurface& surface)
{
	if (!result.currentMaskVerified || !result.softGlyphMask ||
		result.softGlyphMask->size() < surface.width * surface.height ||
		result.cue.memberCount == 0 || !IsValid(result.cue.captureBounds, surface) ||
		!IsValid(result.cue.glyphBounds, surface))
		return false;
	for (size_t lineIndex = 0; lineIndex < result.cue.memberCount; ++lineIndex)
	{
		const PanelSubtitleGlyphLine& line = result.cue.members[lineIndex];
		if (!IsValid(line.captureBounds, surface) ||
			!IsValid(line.glyphBounds, surface) ||
			line.glyphBounds.left < result.cue.glyphBounds.left ||
			line.glyphBounds.right > result.cue.glyphBounds.right ||
			line.glyphBounds.top < result.cue.glyphBounds.top ||
			line.glyphBounds.bottom > result.cue.glyphBounds.bottom)
			return false;
		bool memberHasMask = false;
		for (int y = line.glyphBounds.top; y < line.glyphBounds.bottom; ++y)
			for (int x = line.glyphBounds.left; x < line.glyphBounds.right; ++x)
				if ((*result.softGlyphMask)[static_cast<size_t>(y) *
					surface.width + x] != 0)
				{
					memberHasMask = true;
					PaintPixel(surface, x, y, 900, 896, 128);
				}
		if (!memberHasMask)
			return false;
	}
	// One box per subtitle. Individual lines are mask anchors, not independent
	// detected panels, so exposing them as boxes was misleading and jittery.
	PaintOutline(surface, result.cue.captureBounds, 3, 900, 896, 896);
	PaintOutline(surface, result.cue.glyphBounds, 2, 900, 128, 896);
	return true;
}

bool PanelSubtitleDiagnostic::Move(const PanelSubtitleResult& result,
	const PanelSubtitleDiagnosticSurface& surface,
	int activePictureTop, int activePictureBottom)
{
	if (!result.currentMaskVerified || !result.softGlyphMask ||
		result.softGlyphMask->size() < surface.width * surface.height ||
		result.cue.memberCount == 0 ||
		activePictureTop < 0 || activePictureBottom <= activePictureTop ||
		static_cast<size_t>(activePictureBottom) > surface.height ||
		!IsValid(result.cue.captureBounds, surface) ||
		!IsValid(result.cue.glyphBounds, surface) ||
		(!IsTopCue(result.cue.location) && !IsBottomCue(result.cue.location)))
		return false;

	const int margin = std::max(12, static_cast<int>(surface.height) * 24 / 1080);
	MoveOperation operation;
	operation.source = result.cue.captureBounds;
	const int boxHeight = RectHeight(operation.source);
	if (boxHeight <= 0 || boxHeight > activePictureBottom - activePictureTop)
		return false;
	int destinationTop = IsTopCue(result.cue.location) ? activePictureTop + margin :
		activePictureBottom - margin - boxHeight;
	destinationTop = std::max(activePictureTop,
		std::min(activePictureBottom - boxHeight, destinationTop));
	operation.shiftY = destinationTop - operation.source.top;
	if ((operation.shiftY & 1) != 0)
	{
		if (destinationTop + boxHeight < activePictureBottom) ++destinationTop;
		else if (destinationTop > activePictureTop) --destinationTop;
		operation.shiftY = destinationTop - operation.source.top;
	}
	operation.destination = { operation.source.left, destinationTop,
		operation.source.right, destinationTop + boxHeight };
	operation.backingLuma = std::min<uint16_t>(result.cue.backingLuma,
		static_cast<uint16_t>(344));
	const size_t glyphArea = static_cast<size_t>(RectHeight(result.cue.glyphBounds)) *
		(result.cue.glyphBounds.right - result.cue.glyphBounds.left);
	operation.maskIndices.reserve(glyphArea / 2);
	operation.luma.reserve(glyphArea / 2);
	operation.chromaIndices.reserve(glyphArea / 8);
	operation.chroma.reserve(glyphArea / 8);
	std::vector<uint8_t> seenPixels(surface.width * surface.height, 0);
	std::vector<uint8_t> seenChroma(surface.width * surface.height / 4, 0);
	for (size_t memberIndex = 0; memberIndex < result.cue.memberCount; ++memberIndex)
	{
		const PanelSubtitleGlyphLine& line = result.cue.members[memberIndex];
		if (!IsValid(line.glyphBounds, surface) ||
			line.glyphBounds.left < result.cue.glyphBounds.left ||
			line.glyphBounds.right > result.cue.glyphBounds.right ||
			line.glyphBounds.top < result.cue.glyphBounds.top ||
			line.glyphBounds.bottom > result.cue.glyphBounds.bottom)
			return false;
		bool memberHasMask = false;
		for (int y = line.glyphBounds.top; y < line.glyphBounds.bottom; ++y)
		{
			uint16_t* yRow = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(surface.p010Luma) + static_cast<size_t>(y) * surface.lumaStrideBytes);
			uint16_t* uvRow = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(surface.p010Chroma) + static_cast<size_t>(y / 2) * surface.chromaStrideBytes);
			for (int x = line.glyphBounds.left; x < line.glyphBounds.right; ++x)
			{
				const int index = y * static_cast<int>(surface.width) + x;
				if ((*result.softGlyphMask)[index] == 0) continue;
				memberHasMask = true;
				if (!seenPixels[index]) { seenPixels[index] = 1; operation.maskIndices.push_back(index); operation.luma.push_back(yRow[x]); }
				const int chromaIndex = (y / 2) * static_cast<int>(surface.width / 2) + x / 2;
				if (!seenChroma[chromaIndex]) { seenChroma[chromaIndex] = 1; operation.chromaIndices.push_back(chromaIndex); operation.chroma.push_back({ uvRow[(x / 2) * 2], uvRow[(x / 2) * 2 + 1] }); }
			}
		}
		if (!memberHasMask) return false;
	}
	if (operation.maskIndices.empty()) return false;

	// Capture completed above. Erase every source mask before drawing any
	// destination so overlapping operations can never sample modified pixels.
	for (const int index : operation.maskIndices)
		{
			const int y = index / static_cast<int>(surface.width);
			const int x = index - y * static_cast<int>(surface.width);
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Luma) +
				static_cast<size_t>(y) * surface.lumaStrideBytes);
			row[x] = P010(operation.backingLuma);
		}
	for (const int chromaIndex : operation.chromaIndices)
		{
			const int y = chromaIndex / static_cast<int>(surface.width / 2);
			const int x = chromaIndex - y * static_cast<int>(surface.width / 2);
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Chroma) +
				static_cast<size_t>(y) * surface.chromaStrideBytes);
			row[x * 2] = P010(512);
			row[x * 2 + 1] = P010(512);
		}
	PaintOutline(surface, operation.source, 2, 700, 300, 850);

		// Fill the complete deterministic destination panel first.
		for (int y = operation.destination.top; y < operation.destination.bottom; ++y)
		{
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Luma) +
				static_cast<size_t>(y) * surface.lumaStrideBytes);
			std::fill(row + operation.destination.left,
				row + operation.destination.right, P010(operation.backingLuma));
		}
		for (int y = operation.destination.top / 2;
			y < (operation.destination.bottom + 1) / 2; ++y)
		{
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Chroma) +
				static_cast<size_t>(y) * surface.chromaStrideBytes);
			for (int x = operation.destination.left / 2;
				x < (operation.destination.right + 1) / 2; ++x)
			{
				row[x * 2] = P010(512);
				row[x * 2 + 1] = P010(512);
			}
		}
		// Draw diagnostic geometry before glyph composition. P010 shares one
		// chroma pair across a 2x2 luma cell; drawing the outline afterwards can
		// otherwise recolor a neighboring glyph edge.
		PaintOutline(surface, operation.destination, 3, 900, 896, 896);
		for (size_t pixel = 0; pixel < operation.maskIndices.size(); ++pixel)
		{
			const int sourceIndex = operation.maskIndices[pixel];
			const int sourceY = sourceIndex / static_cast<int>(surface.width);
			const int x = sourceIndex - sourceY * static_cast<int>(surface.width);
			const int destinationY = sourceY + operation.shiftY;
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Luma) +
				static_cast<size_t>(destinationY) * surface.lumaStrideBytes);
			row[x] = operation.luma[pixel];
		}
		for (size_t cell = 0; cell < operation.chromaIndices.size(); ++cell)
		{
			const int sourceIndex = operation.chromaIndices[cell];
			const int sourceY = sourceIndex / static_cast<int>(surface.width / 2);
			const int x = sourceIndex - sourceY * static_cast<int>(surface.width / 2);
			const int destinationY = sourceY + operation.shiftY / 2;
			uint16_t* row = reinterpret_cast<uint16_t*>(
				reinterpret_cast<uint8_t*>(surface.p010Chroma) +
				static_cast<size_t>(destinationY) * surface.chromaStrideBytes);
			row[x * 2] = operation.chroma[cell][0];
			row[x * 2 + 1] = operation.chroma[cell][1];
		}
	return true;
}

bool PanelSubtitleDiagnostic::Apply(const PanelSubtitleResult& result,
	const PanelSubtitleDiagnosticSurface& surface, PanelSubtitleTestMode mode,
	int activePictureTop, int activePictureBottom)
{
	if (mode == PanelSubtitleTestMode::Off ||
		result.state != PanelSubtitleState::Stable || !surface.p010Luma ||
		!surface.p010Chroma || surface.width < 2 || surface.height < 2 ||
		(surface.width & 1) != 0 || (surface.height & 1) != 0 ||
		surface.lumaStrideBytes < surface.width * sizeof(uint16_t) ||
		surface.chromaStrideBytes < surface.width * sizeof(uint16_t) ||
		result.rasterWidth != surface.width || result.rasterHeight != surface.height)
		return false;
	if (mode == PanelSubtitleTestMode::Move)
		return Move(result, surface, activePictureTop, activePictureBottom);
	return Highlight(result, surface);
}
