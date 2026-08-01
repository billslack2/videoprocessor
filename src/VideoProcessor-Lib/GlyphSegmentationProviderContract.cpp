#include "pch.h"
#include "GlyphSegmentationProviderContract.h"

#include <cmath>
#include <limits>

namespace
{
	bool IsNormalized(float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	}

	bool SameGeneration(const GlyphSegmentationGeneration& left,
		const GlyphSegmentationGeneration& right) noexcept
	{
		return left.pipeline == right.pipeline &&
			left.activePicture == right.activePicture &&
			left.viewport == right.viewport;
	}

	bool CheckedPixels(size_t width, size_t height) noexcept
	{
		return width != 0 && height <= std::numeric_limits<size_t>::max() / width;
	}
}

bool GlyphSegmentationNormalizedRect::IsValid() const noexcept
{
	return IsNormalized(left) && IsNormalized(top) && IsNormalized(right) &&
		IsNormalized(bottom) && right > left && bottom > top;
}

bool GlyphSegmentationP010Source::IsValidFor(size_t width, size_t height) const noexcept
{
	if (width == 0 || height == 0 || width > std::numeric_limits<size_t>::max() / sizeof(uint16_t))
		return false;
	const size_t minimumStride = width * sizeof(uint16_t);
	if (kind == GlyphSegmentationP010SourceKind::CpuPlanes)
		return luma != nullptr && lumaStrideBytes >= minimumStride &&
			(!chroma || chromaStrideBytes >= minimumStride);
	return kind == GlyphSegmentationP010SourceKind::OpaqueGpuResource &&
		opaqueResourceToken != 0;
}

bool GlyphSegmentationRequest::IsValid() const noexcept
{
	if (requestToken == 0 || sourceSequence == 0 || rasterWidth == 0 ||
		rasterHeight == 0 || !activePicture.IsValid() || roiCount == 0 ||
		roiCount > rois.size() || !source.IsValidFor(rasterWidth, rasterHeight))
		return false;
	for (size_t index = 0; index < roiCount; ++index)
		if (!rois[index].bounds.IsValid() || !rois[index].trustedBar ||
			rois[index].backingP50 > 1023 || rois[index].backingP72 > 1023 ||
			rois[index].backingMad > 1023)
			return false;
	return true;
}

bool GlyphSegmentationSoftMask::IsValid() const noexcept
{
	if (!bounds.IsValid() || !interior || !edge || !CheckedPixels(width, height))
		return false;
	const size_t pixels = width * height;
	return interior->size() == pixels && edge->size() == pixels;
}

bool GlyphSegmentationResult::Matches(const GlyphSegmentationRequest& request) const noexcept
{
	return requestToken == request.requestToken &&
		sourceSequence == request.sourceSequence &&
		rasterWidth == request.rasterWidth && rasterHeight == request.rasterHeight &&
		SameGeneration(generation, request.generation);
}

bool GlyphSegmentationResult::IsUsable() const noexcept
{
	if (status != GlyphSegmentationResultStatus::Completed || requestToken == 0 ||
		memberCount == 0 || memberCount > members.size())
		return false;
	for (size_t index = 0; index < memberCount; ++index)
		if (!members[index].glyphBounds.IsValid() ||
			!members[index].captureBounds.IsValid() ||
			!members[index].mask.IsValid() ||
			!std::isfinite(members[index].confidence) ||
			members[index].confidence < 0.0f || members[index].confidence > 1.0f)
			return false;
	return true;
}

bool UnavailableGlyphSegmentationProvider::IsAvailable() const noexcept
{
	return false;
}

bool UnavailableGlyphSegmentationProvider::Submit(const GlyphSegmentationRequest& request)
{
	(void)request;
	return false;
}

bool UnavailableGlyphSegmentationProvider::TryTakeLatest(GlyphSegmentationResult& result)
{
	result = {};
	result.status = GlyphSegmentationResultStatus::Unavailable;
	return false;
}

void UnavailableGlyphSegmentationProvider::Cancel() noexcept
{
}

uint64_t GlyphSegmentationLatestOnlyGate::IssueToken() noexcept
{
	uint64_t current = m_latestToken.load(std::memory_order_relaxed);
	for (;;)
	{
		const uint64_t next = current == std::numeric_limits<uint64_t>::max() ?
			1 : current + 1;
		if (m_latestToken.compare_exchange_weak(current, next,
			std::memory_order_release, std::memory_order_relaxed))
			return next;
	}
}

void GlyphSegmentationLatestOnlyGate::Invalidate() noexcept
{
	(void)IssueToken();
}

bool GlyphSegmentationLatestOnlyGate::Accepts(
	const GlyphSegmentationResult& result) const noexcept
{
	return result.requestToken != 0 &&
		result.requestToken == m_latestToken.load(std::memory_order_acquire);
}

uint64_t GlyphSegmentationLatestOnlyGate::LatestToken() const noexcept
{
	return m_latestToken.load(std::memory_order_acquire);
}
