#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Renderer-neutral contract for a future GPU/NN glyph segmenter. The provider
// produces geometry and soft masks only; it never recognizes, redraws, or
// relocates text. Coordinates are normalized to the source P010 raster.

struct GlyphSegmentationNormalizedRect
{
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;

	bool IsValid() const noexcept;
};

struct GlyphSegmentationGeneration
{
	uint64_t pipeline = 0;
	uint64_t activePicture = 0;
	uint64_t viewport = 0;
};

enum class GlyphSegmentationTransfer : uint8_t
{
	Unknown,
	Sdr,
	Pq,
	Hlg,
};

enum class GlyphSegmentationP010Range : uint8_t
{
	Unknown,
	Limited,
	Full,
};

// No graphics-API type is exposed here. A renderer may provide CPU planes or
// an implementation-owned GPU resource token valid until its request is
// canceled/completed.
enum class GlyphSegmentationP010SourceKind : uint8_t
{
	None,
	CpuPlanes,
	OpaqueGpuResource,
};

struct GlyphSegmentationP010Source
{
	GlyphSegmentationP010SourceKind kind = GlyphSegmentationP010SourceKind::None;
	const uint16_t* luma = nullptr;
	const uint16_t* chroma = nullptr;
	size_t lumaStrideBytes = 0;
	size_t chromaStrideBytes = 0;
	uint64_t opaqueResourceToken = 0;
	uint64_t completionToken = 0;

	bool IsValidFor(size_t width, size_t height) const noexcept;
};

struct GlyphSegmentationP010Context
{
	GlyphSegmentationTransfer transfer = GlyphSegmentationTransfer::Unknown;
	GlyphSegmentationP010Range range = GlyphSegmentationP010Range::Unknown;
	uint16_t blackCode = 64;
	uint16_t neutralChromaCode = 512;
	bool chromaIsAvailable = false;
};

enum class GlyphSegmentationRoiContext : uint8_t
{
	TopBar,
	BottomBar,
	TopBoundary,
	BottomBoundary,
};

struct GlyphSegmentationRoi
{
	GlyphSegmentationNormalizedRect bounds;
	GlyphSegmentationRoiContext context = GlyphSegmentationRoiContext::BottomBar;
	// Robust P010 background statistics supplied by the compute/classical
	// proposal pass. The model may use them, but they are not model outputs.
	uint16_t backingP50 = 0;
	uint16_t backingP72 = 0;
	uint16_t backingMad = 0;
	bool trustedBar = false;
};

struct GlyphSegmentationRequest
{
	// Globally monotonic, latest-only token from GlyphSegmentationLatestOnlyGate.
	uint64_t requestToken = 0;
	uint64_t sourceSequence = 0;
	GlyphSegmentationGeneration generation;
	size_t rasterWidth = 0;
	size_t rasterHeight = 0;
	GlyphSegmentationP010Source source;
	GlyphSegmentationP010Context p010;
	GlyphSegmentationNormalizedRect activePicture;
	std::array<GlyphSegmentationRoi, 4> rois{};
	size_t roiCount = 0;

	bool IsValid() const noexcept;
};

// A source-member mask is local to bounds, one byte per pixel. Both channels
// are soft opacity values: interior is confident glyph fill/stroke, edge is
// the anti-aliased/outline transition. Consumers decide how to combine them.
struct GlyphSegmentationSoftMask
{
	GlyphSegmentationNormalizedRect bounds;
	size_t width = 0;
	size_t height = 0;
	std::shared_ptr<const std::vector<uint8_t>> interior;
	std::shared_ptr<const std::vector<uint8_t>> edge;

	bool IsValid() const noexcept;
};

struct GlyphSegmentationMember
{
	GlyphSegmentationNormalizedRect glyphBounds;
	GlyphSegmentationNormalizedRect captureBounds;
	GlyphSegmentationSoftMask mask;
	uint8_t roiIndex = 0;
	float confidence = 0.0f;
};

enum class GlyphSegmentationResultStatus : uint8_t
{
	Unavailable,
	NoGlyphs,
	Completed,
	Canceled,
	Faulted,
};

struct GlyphSegmentationTelemetry
{
	uint64_t queuedMicroseconds = 0;
	uint64_t computeMicroseconds = 0;
	uint32_t proposalsSubmitted = 0;
	uint32_t proposalsEvaluated = 0;
	uint32_t proposalsDroppedLatestOnly = 0;
	uint32_t membersProduced = 0;
	uint32_t interiorPixels = 0;
	uint32_t edgePixels = 0;
	bool gpuResident = false;
};

struct GlyphSegmentationResult
{
	uint64_t requestToken = 0;
	uint64_t sourceSequence = 0;
	GlyphSegmentationGeneration generation;
	size_t rasterWidth = 0;
	size_t rasterHeight = 0;
	GlyphSegmentationResultStatus status = GlyphSegmentationResultStatus::Unavailable;
	std::array<GlyphSegmentationMember, 3> members{};
	size_t memberCount = 0;
	GlyphSegmentationTelemetry telemetry;

	bool Matches(const GlyphSegmentationRequest& request) const noexcept;
	bool IsUsable() const noexcept;
};

// Submit never blocks the video-delivery path. A provider retains at most one
// pending request and one completed result; submitting a newer request may
// drop older pending work. TryTakeLatest returns only the newest completion.
class IGlyphSegmentationProvider
{
public:
	virtual ~IGlyphSegmentationProvider() = default;
	virtual bool IsAvailable() const noexcept = 0;
	virtual bool Submit(const GlyphSegmentationRequest& request) = 0;
	virtual bool TryTakeLatest(GlyphSegmentationResult& result) = 0;
	virtual void Cancel() noexcept = 0;
};

// Safe default until a platform-specific GPU/NN implementation is installed.
class UnavailableGlyphSegmentationProvider final : public IGlyphSegmentationProvider
{
public:
	bool IsAvailable() const noexcept override;
	bool Submit(const GlyphSegmentationRequest& request) override;
	bool TryTakeLatest(GlyphSegmentationResult& result) override;
	void Cancel() noexcept override;
};

// A small, thread-safe latest-only gate for renderer integration. IssueToken()
// must be called for every submitted request; only results carrying the most
// recent issued token may mutate a current frame.
class GlyphSegmentationLatestOnlyGate
{
public:
	uint64_t IssueToken() noexcept;
	void Invalidate() noexcept;
	bool Accepts(const GlyphSegmentationResult& result) const noexcept;
	uint64_t LatestToken() const noexcept;

private:
	std::atomic<uint64_t> m_latestToken{ 0 };
};
