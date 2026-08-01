#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Detector-only contract for captions rendered on an opaque, visually uniform
// dark panel. It never recognizes text. All rectangles are half-open,
// source-raster coordinates.
enum class PanelSubtitleState
{
	Unavailable,
	Candidate,
	Stable,
};

struct PanelSubtitleRect
{
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
};

struct PanelSubtitleGeneration
{
	uint64_t pipeline = 0;
	uint64_t activePicture = 0;
	uint64_t viewport = 0;
};

struct PanelSubtitleDetectorSettings
{
	int minimumPanelWidth = 48;
	int minimumPanelHeight = 10;
	int minimumHorizontalInset = 4;
	uint16_t maximumPanelLuma = 320;
	uint16_t maximumPanelVariation = 20;
	// A wide subtitle line can cover most of a panel row. The independent
	// uniformity check below prevents this tolerance from accepting dark
	// picture detail as a panel.
	uint8_t minimumPanelDarkCoveragePercent = 35;
	uint8_t minimumPanelUniformityPercent = 55;
	uint16_t minimumGlyphContrast = 32;
	uint16_t glyphSoftness = 48;
	int minimumGlyphPixels = 20;
	int minimumGlyphRows = 2;
	int minimumGlyphColumns = 4;
};

struct PanelSubtitleInput
{
	const uint16_t* p010Luma = nullptr;
	size_t width = 0;
	size_t height = 0;
	size_t strideBytes = 0;
	int searchTop = 0;
	int searchBottom = 0;
	uint64_t sourceSequence = 0;
	PanelSubtitleGeneration generation;
	bool enabled = false;
};

struct PanelSubtitleResult
{
	PanelSubtitleState state = PanelSubtitleState::Unavailable;
	uint64_t sourceSequence = 0;
	PanelSubtitleGeneration generation;
	size_t rasterWidth = 0;
	size_t rasterHeight = 0;
	PanelSubtitleRect panelBounds;
	PanelSubtitleRect glyphBounds;
	PanelSubtitleRect maskBounds;
	uint16_t panelLuma = 0;
	uint64_t fingerprint = 0;
	uint32_t stabilityObservations = 0;
	// One byte per source pixel, with zero outside maskBounds. Values are the
	// contrast-derived foreground opacity against the learned panel color.
	std::shared_ptr<const std::vector<uint8_t>> softGlyphMask;
};

class PanelSubtitleDetector
{
public:
	explicit PanelSubtitleDetector(
		const PanelSubtitleDetectorSettings& settings = {});

	PanelSubtitleResult Analyze(const PanelSubtitleInput& input);
	void Reset();

private:
	struct RowRun
	{
		int left = 0;
		int right = 0;
	};

	static bool IsValid(const PanelSubtitleRect& rectangle);
	static bool SameRectangle(const PanelSubtitleRect& left,
		const PanelSubtitleRect& right);
	static bool SameGeneration(const PanelSubtitleGeneration& left,
		const PanelSubtitleGeneration& right);
	static uint16_t P010Code(const uint16_t* row, int x);

	RowRun LongestDarkRun(const PanelSubtitleInput& input, int y) const;
	bool IsDarkEnough(const PanelSubtitleInput& input, int y, int left,
		int right) const;
	bool BuildCandidate(const PanelSubtitleInput& input,
		PanelSubtitleResult& result);
	bool Matches(const PanelSubtitleResult& left,
		const PanelSubtitleResult& right) const;
	void AttachMask(PanelSubtitleResult& result) const;
	PanelSubtitleResult WithCurrentFrame(const PanelSubtitleResult& result,
		const PanelSubtitleInput& input) const;

	PanelSubtitleDetectorSettings m_settings;
	std::vector<uint8_t> m_workMask;
	std::vector<uint32_t> m_rowCounts;
	std::vector<uint32_t> m_columnCounts;
	PanelSubtitleResult m_candidate;
	PanelSubtitleResult m_stable;
};
