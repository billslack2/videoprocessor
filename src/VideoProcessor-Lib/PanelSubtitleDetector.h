#pragma once

#include <array>
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

enum class PanelSubtitleLocation : uint8_t
{
	None,
	TopBoundary,
	BottomBoundary,
	TopBar,
	BottomBar,
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
	// Conservative P010 10-bit acquisition floor. This is deliberately below
	// SDR reference white: PQ subtitles can be visually white after tone
	// mapping while their source code is only around 500.
	uint16_t minimumGlyphLuma = 320;
	// A candidate must still be this much brighter than its learned dark-bar
	// backing before it is accepted or included in the glyph mask.
	uint16_t minimumGlyphContrast = 160;
	int minimumGlyphWidth = 12;
	int minimumGlyphHeight = 7;
	int maximumGlyphHeightPercent = 8;
	int boundaryBandPercent = 11;
	uint16_t maximumBackingLuma = 344;
	uint8_t minimumBackingCoveragePercent = 58;
	int confirmationFrames = 2;
	// With no cue present, run bounded acquisition at this cadence. A pending
	// candidate and a failed stable-cue validation always bypass the cadence so
	// cue confirmation and release still happen on adjacent frames.
	int acquisitionIntervalFrames = 3;
	// Keep a confirmed cue identity through a very short decoding/dropout miss,
	// but never reuse its old mask for a rendering mutation.
	int releaseGraceFrames = 2;
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
	// When supplied, these are the trusted, stable active-picture bounds.
	// The detector scans only the bars and a narrow band straddling either
	// boundary. Legacy callers without this information keep using searchTop /
	// searchBottom as a constrained fallback domain.
	int activePictureTop = 0;
	int activePictureBottom = 0;
	uint64_t trustedActivePictureGeneration = 0;
	bool activePictureStable = false;
	// Optional P010 4:2:0 interleaved CbCr plane. When available, bright seeds
	// must be near-neutral to reject colored UI/highlight elements. Appended to
	// preserve the aggregate-initialization ABI of existing callers.
	const uint16_t* p010Chroma = nullptr;
	size_t chromaStrideBytes = 0;
};

struct PanelSubtitleGlyphLine
{
	PanelSubtitleRect glyphBounds;
	PanelSubtitleRect captureBounds;
	PanelSubtitleLocation location = PanelSubtitleLocation::None;
	uint32_t seedPixels = 0;
	uint64_t fingerprint = 0;
	uint16_t backingLuma = 0;
};

// A single caption operation.  Members are not independent captions: every
// member is a current, contrast-qualified mask/validation anchor and the
// capture rectangle is the one geometry consumed by Highlight and Move.
struct PanelSubtitleCueSet
{
	PanelSubtitleRect captureBounds;
	PanelSubtitleRect glyphBounds;
	PanelSubtitleRect maskBounds;
	PanelSubtitleLocation location = PanelSubtitleLocation::None;
	uint16_t backingLuma = 0;
	std::array<PanelSubtitleGlyphLine, 3> members{};
	size_t memberCount = 0;
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
	PanelSubtitleCueSet cue;
	// Private validation/mask anchors for a caption. Public panelBounds and
	// glyphBounds are the single union capture geometry for all its members.
	std::array<PanelSubtitleGlyphLine, 3> lines{};
	size_t lineCount = 0;
	// One byte per source pixel, with zero outside maskBounds. Values are the
	// contrast-derived foreground opacity against the learned panel color.
	std::shared_ptr<const std::vector<uint8_t>> softGlyphMask;
	// True only when softGlyphMask was rebuilt from this source frame. A soft
	// release-grace result intentionally retains identity but cannot mutate.
	bool currentMaskVerified = false;
};

class PanelSubtitleDetector
{
public:
	explicit PanelSubtitleDetector(
		const PanelSubtitleDetectorSettings& settings = {});

	PanelSubtitleResult Analyze(const PanelSubtitleInput& input);
	void Reset();
	uint64_t AcquisitionScanCount() const { return m_acquisitionScanCount; }

private:
	struct Candidate
	{
		PanelSubtitleGlyphLine line;
		uint16_t backingLuma = 0;
		// A picture-side member is eligible only through an already-qualified
		// bar/boundary anchor.  It may use the HDR-safe cue threshold, but never
		// participates in free acquisition.
		bool anchoredPictureCompanion = false;
	};

	static bool IsValid(const PanelSubtitleRect& rectangle);
	static bool SameRectangleWithin(const PanelSubtitleRect& left,
		const PanelSubtitleRect& right, int tolerance);
	static bool SameGeneration(const PanelSubtitleGeneration& left,
		const PanelSubtitleGeneration& right);
	static uint16_t P010Code(const uint16_t* row, int x);
	bool HasNeutralChroma(const PanelSubtitleInput& input, int x, int y) const;
	bool HasLocalDarkSupport(const PanelSubtitleInput& input, int x, int y,
		uint16_t glyphLuma) const;
	bool IsGlyphSeed(const PanelSubtitleInput& input, int x, int y) const;
	bool IsContrastGlyph(const PanelSubtitleInput& input, int x, int y,
		uint16_t backingLuma) const;

	bool IsTrustedActivePicture(const PanelSubtitleInput& input) const;
	bool IsInSearchDomain(const PanelSubtitleInput& input, int y) const;
	bool QualifyCandidate(const PanelSubtitleInput& input,
		const PanelSubtitleRect& glyphBounds, Candidate& candidate,
		const Candidate* cueAnchor = nullptr, bool allowExtentGaps = false);
	void BuildMask(const PanelSubtitleInput& input,
		const std::array<Candidate, 48>& candidates, size_t candidateCount);
	bool BuildCandidate(const PanelSubtitleInput& input,
		PanelSubtitleResult& result);
	bool ValidateStable(const PanelSubtitleInput& input,
		PanelSubtitleResult& result);
	bool HasStableSeedOverlap(const PanelSubtitleInput& input,
		const PanelSubtitleGlyphLine& line) const;
	bool Matches(const PanelSubtitleResult& left,
		const PanelSubtitleResult& right) const;
	void AttachMask(PanelSubtitleResult& result) const;
	PanelSubtitleResult WithCurrentFrame(const PanelSubtitleResult& result,
		const PanelSubtitleInput& input, bool maskVerified = true) const;

	PanelSubtitleDetectorSettings m_settings;
	std::shared_ptr<std::vector<uint8_t>> m_workMask =
		std::make_shared<std::vector<uint8_t>>();
	std::vector<size_t> m_workMaskDirty;
	PanelSubtitleResult m_candidate;
	PanelSubtitleResult m_stable;
	PanelSubtitleGeneration m_acquisitionGeneration;
	uint64_t m_nextAcquisitionSequence = 0;
	uint64_t m_acquisitionScanCount = 0;
	bool m_hasAcquisitionGeneration = false;
	uint32_t m_stableSoftMisses = 0;
};
