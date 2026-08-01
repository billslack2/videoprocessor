#pragma once

#include <cstdint>

// VP-0070 validation-only behavior. This is deliberately independent of the
// legacy SubtitleRepositionMode/OCR path.
enum class PanelSubtitleTestMode : uint8_t
{
	Off,
	Highlight,
	Move,
};

inline const char* PanelSubtitleTestModeName(PanelSubtitleTestMode mode)
{
	switch (mode)
	{
	case PanelSubtitleTestMode::Highlight:
		return "highlight";
	case PanelSubtitleTestMode::Move:
		return "move";
	default:
		return "off";
	}
}
