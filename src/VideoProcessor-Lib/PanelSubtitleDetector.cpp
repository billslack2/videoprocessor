#include "pch.h"
#include "PanelSubtitleDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
	constexpr size_t MaximumLines = 3;
	constexpr size_t MaximumRawCandidates = 48;

	int RectWidth(const PanelSubtitleRect& rectangle)
	{
		return std::max(0, rectangle.right - rectangle.left);
	}

	int RectHeight(const PanelSubtitleRect& rectangle)
	{
		return std::max(0, rectangle.bottom - rectangle.top);
	}

	PanelSubtitleRect ClampRectangle(PanelSubtitleRect rectangle, int width,
		int height)
	{
		rectangle.left = std::max(0, std::min(width, rectangle.left));
		rectangle.right = std::max(0, std::min(width, rectangle.right));
		rectangle.top = std::max(0, std::min(height, rectangle.top));
		rectangle.bottom = std::max(0, std::min(height, rectangle.bottom));
		return rectangle;
	}

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

	bool IsPictureOnly(const PanelSubtitleInput& input,
		const PanelSubtitleRect& rectangle)
	{
		return input.activePictureStable && rectangle.top >= input.activePictureTop &&
			rectangle.bottom <= input.activePictureBottom;
	}

	bool RelatedCaptionMember(const PanelSubtitleRect& cue,
		const PanelSubtitleRect& member, int height)
	{
		const int overlap = std::min(cue.right, member.right) -
			std::max(cue.left, member.left);
		const int requiredOverlap = std::max(1,
			std::min(RectWidth(cue), RectWidth(member)) / 8);
		const int verticalGap = std::max(0, std::max(cue.top - member.bottom,
			member.top - cue.bottom));
		// A three-line cue has two inter-line gaps.  Compare every candidate with
		// the anchor (rather than allowing a transitive chain), but give that
		// anchor enough vertical reach for the whole semantic stack.
		const int maximumGap = std::max(height * 4 / 1080,
			5 * std::max(RectHeight(cue), RectHeight(member)));
		const int horizontalGap = std::max(0, std::max(cue.left - member.right,
			member.left - cue.right));
		const int permittedEdgeGap = std::max(24,
			(static_cast<int>(std::ceil(std::max(RectHeight(cue), RectHeight(member)) * 1.25))));
		return (overlap >= requiredOverlap || horizontalGap <= permittedEdgeGap) &&
			verticalGap <= maximumGap;
	}

	PanelSubtitleRect UnionRectangle(PanelSubtitleRect left,
		const PanelSubtitleRect& right)
	{
		left.left = std::min(left.left, right.left);
		left.top = std::min(left.top, right.top);
		left.right = std::max(left.right, right.right);
		left.bottom = std::max(left.bottom, right.bottom);
		return left;
	}
}

PanelSubtitleDetector::PanelSubtitleDetector(
	const PanelSubtitleDetectorSettings& settings) :
	m_settings(settings)
{
}

bool PanelSubtitleDetector::IsValid(const PanelSubtitleRect& rectangle)
{
	return rectangle.right > rectangle.left && rectangle.bottom > rectangle.top;
}

bool PanelSubtitleDetector::SameRectangleWithin(const PanelSubtitleRect& left,
	const PanelSubtitleRect& right, int tolerance)
{
	return std::abs(left.left - right.left) <= tolerance &&
		std::abs(left.top - right.top) <= tolerance &&
		std::abs(left.right - right.right) <= tolerance &&
		std::abs(left.bottom - right.bottom) <= tolerance;
}

bool PanelSubtitleDetector::SameGeneration(const PanelSubtitleGeneration& left,
	const PanelSubtitleGeneration& right)
{
	return left.pipeline == right.pipeline &&
		left.activePicture == right.activePicture &&
		left.viewport == right.viewport;
}

uint16_t PanelSubtitleDetector::P010Code(const uint16_t* row, int x)
{
	return static_cast<uint16_t>(row[x] >> 6);
}

bool PanelSubtitleDetector::HasNeutralChroma(const PanelSubtitleInput& input,
	int x, int y) const
{
	if (!input.p010Chroma || input.chromaStrideBytes < input.width * sizeof(uint16_t))
		return true;
	const auto* row = reinterpret_cast<const uint16_t*>(
		reinterpret_cast<const uint8_t*>(input.p010Chroma) +
		static_cast<size_t>(y / 2) * input.chromaStrideBytes);
	const int chromaX = (x / 2) * 2;
	const int cb = P010Code(row, chromaX);
	const int cr = P010Code(row, chromaX + 1);
	return std::abs(cb - 512) <= 208 && std::abs(cr - 512) <= 208;
}

bool PanelSubtitleDetector::HasLocalDarkSupport(const PanelSubtitleInput& input,
	int x, int y, uint16_t glyphLuma) const
{
	// A low-PQ code is only interesting when it is locally contrasted against
	// the trusted dark bar. Sample along eight rays so narrow/anti-aliased
	// strokes remain eligible while broad scene areas never become seeds.
	constexpr std::array<std::array<int, 2>, 8> Directions = {{
		{{ -1, 0 }}, {{ 1, 0 }}, {{ 0, -1 }}, {{ 0, 1 }},
		{{ -1, -1 }}, {{ 1, -1 }}, {{ -1, 1 }}, {{ 1, 1 }},
	}};
	const int width = static_cast<int>(input.width);
	const int height = static_cast<int>(input.height);
	int darkDirections = 0;
	for (const auto& direction : Directions)
	{
		bool foundDark = false;
		for (int distance = 1; distance <= 12; ++distance)
		{
			const int sampleX = x + direction[0] * distance;
			const int sampleY = y + direction[1] * distance;
			if (sampleX < 0 || sampleX >= width || sampleY < 0 || sampleY >= height)
				break;
			const auto* row = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(input.p010Luma) +
				static_cast<size_t>(sampleY) * input.strideBytes);
			const uint16_t darkLuma = P010Code(row, sampleX);
			if (darkLuma <= m_settings.maximumBackingLuma &&
				glyphLuma >= darkLuma + m_settings.minimumGlyphContrast)
			{
				foundDark = true;
				break;
			}
		}
		if (foundDark && ++darkDirections >= 2)
			return true;
	}
	return false;
}

bool PanelSubtitleDetector::IsGlyphSeed(const PanelSubtitleInput& input,
	int x, int y) const
{
	const auto* row = reinterpret_cast<const uint16_t*>(
		reinterpret_cast<const uint8_t*>(input.p010Luma) +
		static_cast<size_t>(y) * input.strideBytes);
	// Only trusted letterbox/pillarbox bars get the HDR-safe coarse floor.
	// Keeping the old bright-only floor in the active picture prevents normal
	// PQ scene highlights from becoming subtitle proposals.
	const bool trustedBar = IsTrustedActivePicture(input) &&
		(y < input.activePictureTop || y >= input.activePictureBottom);
	const uint16_t code = P010Code(row, x);
	if (!HasNeutralChroma(input, x, y))
		return false;
	if (code >= std::max<uint16_t>(620, m_settings.minimumGlyphLuma))
		return true;
	return trustedBar && code >= m_settings.minimumGlyphLuma &&
		HasLocalDarkSupport(input, x, y, code);
}

bool PanelSubtitleDetector::IsContrastGlyph(const PanelSubtitleInput& input,
	int x, int y, uint16_t backingLuma) const
{
	const auto* row = reinterpret_cast<const uint16_t*>(
		reinterpret_cast<const uint8_t*>(input.p010Luma) +
		static_cast<size_t>(y) * input.strideBytes);
	const uint16_t threshold = static_cast<uint16_t>(std::min<int>(1023,
		static_cast<int>(backingLuma) + m_settings.minimumGlyphContrast));
	return P010Code(row, x) >= threshold && HasNeutralChroma(input, x, y);
}

void PanelSubtitleDetector::Reset()
{
	m_candidate = {};
	m_stable = {};
	m_acquisitionGeneration = {};
	m_nextAcquisitionSequence = 0;
	m_hasAcquisitionGeneration = false;
	m_stableSoftMisses = 0;
}

bool PanelSubtitleDetector::IsTrustedActivePicture(
	const PanelSubtitleInput& input) const
{
	return input.activePictureStable &&
		input.trustedActivePictureGeneration != 0 &&
		input.trustedActivePictureGeneration == input.generation.activePicture &&
		input.activePictureTop >= 0 &&
		input.activePictureBottom > input.activePictureTop &&
		input.activePictureBottom <= static_cast<int>(input.height) &&
		(input.activePictureTop > 0 ||
			input.activePictureBottom < static_cast<int>(input.height));
}

bool PanelSubtitleDetector::IsInSearchDomain(const PanelSubtitleInput& input,
	int y) const
{
	if (!IsTrustedActivePicture(input))
		return y >= std::max(0, input.searchTop) &&
			y < std::min(static_cast<int>(input.height), input.searchBottom);

	const int boundary = std::max(32,
		(static_cast<int>(input.height) * m_settings.boundaryBandPercent + 99) / 100);
	return y < std::min(static_cast<int>(input.height), input.activePictureTop + boundary) ||
		y >= std::max(0, input.activePictureBottom - boundary);
}

bool PanelSubtitleDetector::QualifyCandidate(const PanelSubtitleInput& input,
	const PanelSubtitleRect& glyphBounds, Candidate& candidate,
	const Candidate* cueAnchor, bool allowExtentGaps)
{
	const int width = static_cast<int>(input.width);
	const int height = static_cast<int>(input.height);
	const int glyphHeight = RectHeight(glyphBounds);
	const int supportX = std::max(8, glyphHeight / 3);
	const int supportY = std::max(6, glyphHeight / 4);
	const PanelSubtitleRect support = ClampRectangle({
		glyphBounds.left - supportX, glyphBounds.top - supportY,
		glyphBounds.right + supportX, glyphBounds.bottom + supportY }, width, height);
	if (!IsValid(support))
		return false;
	// A straddling line has picture pixels by design. Learn its backing only
	// from the encoded-bar side, rather than allowing the scene behind the
	// picture-side glyphs to poison the dark-panel estimate.
	const bool trustedBounds = IsTrustedActivePicture(input);
	const bool useTopBarBacking = trustedBounds &&
		glyphBounds.top < input.activePictureTop;
	const bool useBottomBarBacking = trustedBounds && !useTopBarBacking &&
		glyphBounds.bottom > input.activePictureBottom;
	auto isBackingSample = [&](int y) {
		return !trustedBounds ||
			(useTopBarBacking && y < input.activePictureTop) ||
			(useBottomBarBacking && y >= input.activePictureBottom) ||
			(!useTopBarBacking && !useBottomBarBacking);
	};

	uint16_t backingLuma = cueAnchor ? cueAnchor->backingLuma : 1023;
	if (!cueAnchor)
	{
		std::array<uint32_t, 1024> histogram{};
		uint32_t nonSeeds = 0;
		for (int y = support.top; y < support.bottom; ++y)
		{
			if (!isBackingSample(y)) continue;
			const auto* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(input.p010Luma) + static_cast<size_t>(y) * input.strideBytes);
			for (int x = support.left; x < support.right; ++x)
				if (!IsGlyphSeed(input, x, y)) { ++histogram[P010Code(row, x)]; ++nonSeeds; }
		}
		if (nonSeeds == 0) return false;
		const uint32_t rank = (nonSeeds * 72 + 99) / 100;
		uint32_t cumulative = 0;
		for (size_t code = 0; code < histogram.size(); ++code)
			if ((cumulative += histogram[code]) >= rank) { backingLuma = static_cast<uint16_t>(code); break; }
		const uint16_t backingLimit = static_cast<uint16_t>(std::min<int>(m_settings.maximumBackingLuma, backingLuma + 48));
		uint32_t dark = 0;
		for (int y = support.top; y < support.bottom; ++y)
		{
			if (!isBackingSample(y)) continue;
			const auto* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(input.p010Luma) + static_cast<size_t>(y) * input.strideBytes);
			for (int x = support.left; x < support.right; ++x)
				if (!IsGlyphSeed(input, x, y) && P010Code(row, x) <= backingLimit) ++dark;
		}
		if (dark * 100 < nonSeeds * m_settings.minimumBackingCoveragePercent) return false;
	}

	PanelSubtitleLocation location = cueAnchor ? cueAnchor->line.location : PanelSubtitleLocation::None;
	uint32_t beforeBoundary = 0;
	uint32_t afterBoundary = 0;
	if (IsTrustedActivePicture(input) && !cueAnchor)
	{
		const int top = input.activePictureTop;
		const int bottom = input.activePictureBottom;
		const bool crossesTop = glyphBounds.top < top && glyphBounds.bottom > top;
		const bool crossesBottom = glyphBounds.top < bottom && glyphBounds.bottom > bottom;
		const bool topBar = glyphBounds.bottom <= top;
		const bool bottomBar = glyphBounds.top >= bottom;
		if (static_cast<int>(crossesTop) + static_cast<int>(crossesBottom) +
			static_cast<int>(topBar) + static_cast<int>(bottomBar) != 1)
			return false;
		location = crossesTop ? PanelSubtitleLocation::TopBoundary :
			crossesBottom ? PanelSubtitleLocation::BottomBoundary :
			topBar ? PanelSubtitleLocation::TopBar : PanelSubtitleLocation::BottomBar;
	}

	uint32_t seedPixels = 0;
	int seedLeft = glyphBounds.right;
	int seedTop = glyphBounds.bottom;
	auto isMemberSeed = [&](int x, int y) {
		if (!cueAnchor)
			return IsGlyphSeed(input, x, y);
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
				static_cast<size_t>(y) * input.strideBytes);
		// The parent bar anchor has already proved the backing.  Permit the
		// same low-PQ, neutral, contrast-qualified glyph code on the picture
		// side without reopening general scene acquisition.
		return HasNeutralChroma(input, x, y) &&
			P010Code(row, x) >= m_settings.minimumGlyphLuma &&
			IsContrastGlyph(input, x, y, backingLuma);
	};
	for (int y = glyphBounds.top; y < glyphBounds.bottom; ++y)
	{
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
			static_cast<size_t>(y) * input.strideBytes);
		for (int x = glyphBounds.left; x < glyphBounds.right; ++x)
		{
			if (!isMemberSeed(x, y) ||
				!IsContrastGlyph(input, x, y, backingLuma))
				continue;
			++seedPixels;
			seedLeft = std::min(seedLeft, x);
			seedTop = std::min(seedTop, y);
			if (location == PanelSubtitleLocation::TopBoundary && y < input.activePictureTop ||
				location == PanelSubtitleLocation::BottomBoundary && y < input.activePictureBottom)
				++beforeBoundary;
			if (location == PanelSubtitleLocation::TopBoundary && y >= input.activePictureTop ||
				location == PanelSubtitleLocation::BottomBoundary && y >= input.activePictureBottom)
				++afterBoundary;
		}
	}
	if (seedPixels < 24 ||
		(!cueAnchor && (location == PanelSubtitleLocation::TopBoundary ||
			location == PanelSubtitleLocation::BottomBoundary) &&
			(beforeBoundary < 12 || afterBoundary < 12)))
		return false;
	uint64_t fingerprint = 1469598103934665603ULL;
	for (int y = glyphBounds.top; y < glyphBounds.bottom; ++y)
	{
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
			static_cast<size_t>(y) * input.strideBytes);
		for (int x = glyphBounds.left; x < glyphBounds.right; ++x)
			if (isMemberSeed(x, y) &&
				IsContrastGlyph(input, x, y, backingLuma))
			{
				// Normalize to the seed origin: a one-pixel source jitter does not
				// invalidate an otherwise identical long-running cue.
				fingerprint ^= static_cast<uint64_t>((y - seedTop) * 257 + (x - seedLeft));
				fingerprint *= 1099511628211ULL;
			}
	}

	const int maximumGap = std::max((height * 48 + 1079) / 1080,
		static_cast<int>(std::ceil(glyphHeight * 2.7)));
	int currentGap = 0;
	int longestGap = 0;
	for (int x = glyphBounds.left; x < glyphBounds.right; ++x)
	{
		bool occupied = false;
		for (int y = glyphBounds.top; y < glyphBounds.bottom && !occupied; ++y)
		{
			const auto* row = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(input.p010Luma) +
				static_cast<size_t>(y) * input.strideBytes);
			occupied = isMemberSeed(x, y) &&
				IsContrastGlyph(input, x, y, backingLuma);
		}
		if (occupied)
		{
			longestGap = std::max(longestGap, currentGap);
			currentGap = 0;
		}
		else
			++currentGap;
	}
	if (!allowExtentGaps && std::max(longestGap, currentGap) > maximumGap)
		return false;

	const int captureInset = std::max(22,
		static_cast<int>(std::ceil(glyphHeight * 1.6)));
	candidate.line.glyphBounds = glyphBounds;
	candidate.line.captureBounds = ClampRectangle({
		glyphBounds.left - std::max(32, captureInset), glyphBounds.top - captureInset,
		glyphBounds.right + std::max(32, captureInset), glyphBounds.bottom + captureInset },
		width, height);
	candidate.line.location = location;
	candidate.line.seedPixels = seedPixels;
	candidate.line.fingerprint = fingerprint;
	candidate.line.backingLuma = backingLuma;
	candidate.backingLuma = backingLuma;
	candidate.anchoredPictureCompanion = cueAnchor != nullptr;
	return true;
}

void PanelSubtitleDetector::BuildMask(const PanelSubtitleInput& input,
	const std::array<Candidate, 48>& candidates, size_t candidateCount)
{
	const size_t pixels = input.width * input.height;
	if (!m_workMask)
		m_workMask = std::make_shared<std::vector<uint8_t>>();
	std::vector<uint8_t>& mask = *m_workMask;
	if (mask.size() != pixels)
	{
		mask.assign(pixels, 0);
		m_workMaskDirty.clear();
	}
	else
	{
		for (const size_t dirty : m_workMaskDirty)
			mask[dirty] = 0;
		m_workMaskDirty.clear();
	}
	for (size_t index = 0; index < candidateCount; ++index)
	{
		const PanelSubtitleRect nearby = ClampRectangle({
			candidates[index].line.glyphBounds.left - 4,
			candidates[index].line.glyphBounds.top - 4,
			candidates[index].line.glyphBounds.right + 4,
			candidates[index].line.glyphBounds.bottom + 4 },
			static_cast<int>(input.width), static_cast<int>(input.height));
		const uint16_t threshold = static_cast<uint16_t>(std::min<int>(1023,
			static_cast<int>(candidates[index].backingLuma) +
			m_settings.minimumGlyphContrast));
		for (int y = nearby.top; y < nearby.bottom; ++y)
		{
			const auto* row = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(input.p010Luma) +
				static_cast<size_t>(y) * input.strideBytes);
			for (int x = nearby.left; x < nearby.right; ++x)
			{
				const uint16_t code = P010Code(row, x);
				const bool memberSeed = candidates[index].anchoredPictureCompanion ?
					(HasNeutralChroma(input, x, y) &&
						code >= m_settings.minimumGlyphLuma) : IsGlyphSeed(input, x, y);
				if (code >= threshold && memberSeed)
				{
					const size_t maskIndex =
						static_cast<size_t>(y) * input.width + x;
					if (mask[maskIndex] == 0)
						m_workMaskDirty.push_back(maskIndex);
					mask[maskIndex] = static_cast<uint8_t>(std::min(255,
						(static_cast<int>(code) - threshold + 1) * 4));
				}
			}
		}
	}
}

bool PanelSubtitleDetector::BuildCandidate(const PanelSubtitleInput& input,
	PanelSubtitleResult& result)
{
	const int width = static_cast<int>(input.width);
	const int height = static_cast<int>(input.height);
	if (width <= 0 || height <= 0)
		return false;

	// Seed runs are joined only when they share a baseline and a bounded word
	// gap. This prevents a bright UI highlight elsewhere in the same row from
	// being folded into the subtitle proposal (the old widest-row-extrema bug).
	struct Proposal
	{
		PanelSubtitleRect bounds;
		int lastY = 0;
	};
	std::array<Proposal, 48> proposals{};
	size_t proposalCount = 0;
	const int rowGapLimit = std::max(2, height / 360);
	const int characterGap = std::max(3, height / 180);
	const int baselineMergeGap = std::max(12, height * 18 / 1080);
	// Acquisition is the expensive path. At UHD resolutions, sample a maximum
	// of roughly 1920x1080 seed locations, then qualify and build the glyph mask
	// at full source resolution after a proposal is found.
	const int scanStepX = std::max(1, width / 1920);
	const int scanStepY = std::max(1, height / 1080);
	auto addRun = [&](int left, int right, int y) {
		if (right - left < 2)
			return;
		size_t match = proposalCount;
		int bestDistance = std::numeric_limits<int>::max();
		for (size_t index = 0; index < proposalCount; ++index)
		{
			Proposal& proposal = proposals[index];
			if (y - proposal.lastY > std::max(rowGapLimit, scanStepY))
				continue;
			const int horizontalDistance = std::max(0, std::max(
				proposal.bounds.left - right, left - proposal.bounds.right));
			if (horizontalDistance <= baselineMergeGap && horizontalDistance < bestDistance)
			{
				match = index;
				bestDistance = horizontalDistance;
			}
		}
		if (match == proposalCount)
		{
			if (proposalCount >= proposals.size())
				return;
			proposals[proposalCount++] = { { left, y, right,
				std::min(height, y + scanStepY) }, y };
			return;
		}
		Proposal& proposal = proposals[match];
		proposal.bounds.left = std::min(proposal.bounds.left, left);
		proposal.bounds.top = std::min(proposal.bounds.top, y);
		proposal.bounds.right = std::max(proposal.bounds.right, right);
		proposal.bounds.bottom = std::max(proposal.bounds.bottom,
			std::min(height, y + scanStepY));
		proposal.lastY = y;
	};
	for (int y = 0; y < height; y += scanStepY)
	{
		if (!IsInSearchDomain(input, y))
			continue;
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
			static_cast<size_t>(y) * input.strideBytes);
		int left = -1;
		int lastSeed = -1;
		for (int x = 0; x < width; x += scanStepX)
		{
			if (IsGlyphSeed(input, x, y))
			{
				if (left < 0)
					left = x;
				lastSeed = x;
			}
			else if (left >= 0 && x - lastSeed > characterGap)
			{
				addRun(left, std::min(width, lastSeed + scanStepX), y);
				left = -1;
				lastSeed = -1;
			}
		}
		if (left >= 0)
			addRun(left, std::min(width, lastSeed + scanStepX), y);
	}

	std::sort(proposals.begin(), proposals.begin() + proposalCount,
		[](const Proposal& left, const Proposal& right) {
			return left.bounds.top == right.bounds.top ?
				left.bounds.left < right.bounds.left : left.bounds.top < right.bounds.top;
		});
	std::array<PanelSubtitleRect, 48> lines{};
	size_t lineCount = 0;
	for (size_t index = 0; index < proposalCount; ++index)
	{
		const PanelSubtitleRect& proposal = proposals[index].bounds;
		size_t match = lineCount;
		for (size_t line = 0; line < lineCount; ++line)
		{
			const PanelSubtitleRect& existing = lines[line];
			const int overlap = std::min(existing.bottom, proposal.bottom) -
				std::max(existing.top, proposal.top);
			const int requiredOverlap = std::min(RectHeight(existing),
				RectHeight(proposal)) / 2;
			const int horizontalGap = std::max(0, proposal.left - existing.right);
			const int mergeGap = std::max(height * 24 / 1080,
				2 * std::max(RectHeight(existing), RectHeight(proposal)));
			if (overlap >= requiredOverlap && horizontalGap <= mergeGap)
			{
				match = line;
				break;
			}
		}
		if (match == lineCount)
		{
			if (lineCount < lines.size())
				lines[lineCount++] = proposal;
		}
		else
		{
			lines[match].left = std::min(lines[match].left, proposal.left);
			lines[match].top = std::min(lines[match].top, proposal.top);
			lines[match].right = std::max(lines[match].right, proposal.right);
			lines[match].bottom = std::max(lines[match].bottom, proposal.bottom);
		}
	}

	// Keep raw qualification bounded, but do not let its storage limit decide
	// how many semantic subtitle lines a cue may contain.  A long rendered line
	// can legitimately arrive as several raw runs.
	std::array<Candidate, MaximumRawCandidates> candidates{};
	size_t candidateCount = 0;
	for (size_t index = 0; index < lineCount && candidateCount < candidates.size(); ++index)
	{
		const PanelSubtitleRect& bounds = lines[index];
		const int glyphWidth = RectWidth(bounds);
		const int glyphHeight = RectHeight(bounds);
		if (glyphWidth < std::max(m_settings.minimumGlyphWidth, width / 100) ||
			glyphWidth > width * 72 / 100 ||
			glyphHeight < std::max(m_settings.minimumGlyphHeight, height / 154) ||
			glyphHeight > height * m_settings.maximumGlyphHeightPercent / 100)
			continue;
		Candidate candidate;
		if (QualifyCandidate(input, bounds, candidate))
			candidates[candidateCount++] = candidate;
	}
	if (candidateCount == 0)
		return false;
	// Separate bright menu items along one baseline are not a multiline
	// subtitle. A real phrase would have merged above under the bounded word
	// gap, while this guard rejects wide UI spacing. Vertically distinct lines
	// with horizontal overlap do not enter this guard and are unioned below.
	const int menuGap = std::max((height * 48 + 1079) / 1080, 24);
	for (size_t left = 0; left < candidateCount; ++left)
		for (size_t right = left + 1; right < candidateCount; ++right)
	{
		const PanelSubtitleRect& a = candidates[left].line.glyphBounds;
		const PanelSubtitleRect& b = candidates[right].line.glyphBounds;
		const int verticalOverlap = std::min(a.bottom, b.bottom) -
			std::max(a.top, b.top);
		const int horizontalGap = std::max(0, std::max(a.left - b.right,
			b.left - a.right));
		if (verticalOverlap > 0 && horizontalGap > menuGap)
		{
			// A long subtitle line may be split into several adjacent raw runs.
			// Reject separated menu items only when there is no same-baseline chain
			// of bounded word gaps between them.
			bool bridged = false;
			for (size_t middle = 0; middle < candidateCount; ++middle)
			{
				if (middle == left || middle == right) continue;
				const PanelSubtitleRect& c = candidates[middle].line.glyphBounds;
				const int cOverlap = std::min(a.bottom, c.bottom) - std::max(a.top, c.top);
				const int center = c.left + RectWidth(c) / 2;
				const int aCenter = a.left + RectWidth(a) / 2;
				const int bCenter = b.left + RectWidth(b) / 2;
				if (cOverlap > 0 && center > std::min(aCenter, bCenter) &&
					center < std::max(aCenter, bCenter))
				{ bridged = true; break; }
			}
			if (!bridged)
				return false;
		}
	}
	// Pick one deterministic bar/boundary anchor, then build one connected cue
	// from members on the same active-picture side.  Never union every detected
	// line: independent top/bottom overlays are separate candidates, not one
	// caption.
	size_t anchorIndex = 0;
	for (size_t index = 1; index < candidateCount; ++index)
		if (candidates[index].line.seedPixels > candidates[anchorIndex].line.seedPixels ||
			(candidates[index].line.seedPixels == candidates[anchorIndex].line.seedPixels &&
				candidates[index].line.glyphBounds.top < candidates[anchorIndex].line.glyphBounds.top))
			anchorIndex = index;
	std::array<Candidate, MaximumRawCandidates> cueMembers{};
	size_t cueMemberCount = 0;
	cueMembers[cueMemberCount++] = candidates[anchorIndex];
	// Coalesce only raw fragments on the anchor's own baseline into its one
	// semantic line.  This makes the public three-line limit semantic rather
	// than an accidental cap on words/fragments.  The fixed root comparison
	// prevents a chain of unrelated UI fragments from bridging in.
	std::array<bool, MaximumRawCandidates> mergedFragments{};
	mergedFragments[anchorIndex] = true;
	bool mergedAnother = true;
	while (mergedAnother)
	{
		mergedAnother = false;
		for (size_t index = 0; index < candidateCount; ++index)
		{
			if (mergedFragments[index] ||
				IsTopCue(candidates[index].line.location) != IsTopCue(cueMembers[0].line.location))
				continue;
			const PanelSubtitleRect& rootFragment = cueMembers[0].line.glyphBounds;
			const PanelSubtitleRect& fragment = candidates[index].line.glyphBounds;
			const int verticalOverlap = std::min(rootFragment.bottom, fragment.bottom) -
				std::max(rootFragment.top, fragment.top);
			const int neededOverlap = std::max(1, std::min(RectHeight(rootFragment),
				RectHeight(fragment)) / 2);
			const int horizontalGap = std::max(0, std::max(rootFragment.left - fragment.right,
				fragment.left - rootFragment.right));
			const int maximumFragmentGap = std::max(48,
				3 * std::max(RectHeight(rootFragment), RectHeight(fragment)));
			const PanelSubtitleRect expanded = UnionRectangle(rootFragment, fragment);
			if (verticalOverlap < neededOverlap || horizontalGap > maximumFragmentGap ||
				RectWidth(expanded) > width - std::max(32, width / 100))
				continue;
			mergedFragments[index] = true;
			cueMembers[0].line.glyphBounds = expanded;
			cueMembers[0].line.seedPixels += candidates[index].line.seedPixels;
			cueMembers[0].line.fingerprint ^= candidates[index].line.fingerprint +
				0x9e3779b97f4a7c15ULL + (cueMembers[0].line.fingerprint << 6) +
				(cueMembers[0].line.fingerprint >> 2);
			mergedAnother = true;
		}
	}
	{
		const int glyphHeight = RectHeight(cueMembers[0].line.glyphBounds);
		const int captureInset = std::max(22,
			static_cast<int>(std::ceil(glyphHeight * 1.6)));
		cueMembers[0].line.captureBounds = ClampRectangle({
			cueMembers[0].line.glyphBounds.left - std::max(32, captureInset),
			cueMembers[0].line.glyphBounds.top - captureInset,
			cueMembers[0].line.glyphBounds.right + std::max(32, captureInset),
			cueMembers[0].line.glyphBounds.bottom + captureInset }, width, height);
	}
	PanelSubtitleRect combinedGlyph = candidates[anchorIndex].line.glyphBounds;
	combinedGlyph = cueMembers[0].line.glyphBounds;
	// The first qualified bar/boundary line is the sole anchor.  Do not spend
	// semantic cue slots on additional raw bar fragments before looking inward
	// for the caption's picture-side lines.
	// Picture-side text is admitted only as a contrast-qualified member using
	// the anchor's known bar backing.  It therefore receives both a mask and a
	// stable-frame validation anchor; raw proposals never enter public geometry.
	if (IsTrustedActivePicture(input))
	{
		// The coarse HDR seed is intentionally disabled in the active picture
		// during free acquisition. Once the bar anchor is known, make one bounded
		// inward pass using that anchor's contrast threshold so low-PQ companion
		// glyphs can become proposals without admitting scene highlights globally.
		std::array<Proposal, 48> pictureProposals{};
		size_t pictureProposalCount = 0;
		const bool bottomCue = IsBottomCue(cueMembers[0].line.location);
		const int pictureBoundary = std::max(32,
			(height * m_settings.boundaryBandPercent + 99) / 100);
		const int pictureStart = bottomCue ?
			std::max(input.activePictureTop, input.activePictureBottom - pictureBoundary) :
			input.activePictureTop;
		const int pictureEnd = bottomCue ? input.activePictureBottom :
			std::min(input.activePictureBottom, input.activePictureTop + pictureBoundary);
		auto addPictureRun = [&](int left, int right, int y) {
			if (right - left < 2) return;
			size_t match = pictureProposalCount;
			for (size_t proposalIndex = 0; proposalIndex < pictureProposalCount; ++proposalIndex)
			{
				Proposal& proposal = pictureProposals[proposalIndex];
				const int distance = std::max(0, std::max(proposal.bounds.left - right,
					left - proposal.bounds.right));
				if (y - proposal.lastY <= std::max(rowGapLimit, scanStepY) &&
					distance <= baselineMergeGap) { match = proposalIndex; break; }
			}
			if (match == pictureProposalCount)
			{
				if (pictureProposalCount < pictureProposals.size())
					pictureProposals[pictureProposalCount++] = { { left, y, right,
						std::min(height, y + scanStepY) }, y };
				return;
			}
			Proposal& proposal = pictureProposals[match];
			proposal.bounds = UnionRectangle(proposal.bounds, { left, y, right,
				std::min(height, y + scanStepY) });
			proposal.lastY = y;
		};
		const uint16_t anchorThreshold = static_cast<uint16_t>(std::min<int>(1023,
			static_cast<int>(cueMembers[0].backingLuma) + m_settings.minimumGlyphContrast));
		for (int y = pictureStart; y < pictureEnd; y += scanStepY)
		{
			const auto* row = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(input.p010Luma) + static_cast<size_t>(y) * input.strideBytes);
			int left = -1;
			int lastSeed = -1;
			for (int x = 0; x < width; x += scanStepX)
			{
				const uint16_t code = P010Code(row, x);
				const bool seed = HasNeutralChroma(input, x, y) &&
					code >= m_settings.minimumGlyphLuma && code >= anchorThreshold &&
					(code >= std::max<uint16_t>(620, m_settings.minimumGlyphLuma) ||
						// Keep diffuse strokes below normal picture background; they still
						// need local dark support rather than becoming a broad scene seed.
						(code < std::max<uint16_t>(600,
							static_cast<uint16_t>(m_settings.minimumGlyphLuma + 64)) &&
							HasLocalDarkSupport(input, x, y, code)));
				if (seed) { if (left < 0) left = x; lastSeed = x; }
				else if (left >= 0 && x - lastSeed > characterGap)
				{ addPictureRun(left, std::min(width, lastSeed + scanStepX), y); left = lastSeed = -1; }
			}
			if (left >= 0) addPictureRun(left, std::min(width, lastSeed + scanStepX), y);
		}
		std::array<PanelSubtitleRect, 48> companionBounds{};
		size_t companionBoundCount = 0;
		for (size_t proposalIndex = 0; proposalIndex < pictureProposalCount; ++proposalIndex)
		{
			const PanelSubtitleRect& proposal = pictureProposals[proposalIndex].bounds;
			size_t match = companionBoundCount;
			for (size_t boundIndex = 0; boundIndex < companionBoundCount; ++boundIndex)
			{
				const PanelSubtitleRect& existing = companionBounds[boundIndex];
				const int overlap = std::min(existing.bottom, proposal.bottom) - std::max(existing.top, proposal.top);
				const int gap = std::max(0, proposal.left - existing.right);
				if (overlap >= std::min(RectHeight(existing), RectHeight(proposal)) / 2 &&
					gap <= std::max(height * 24 / 1080, 2 * std::max(RectHeight(existing), RectHeight(proposal))))
				{ match = boundIndex; break; }
			}
			if (match == companionBoundCount) { if (companionBoundCount < companionBounds.size()) companionBounds[companionBoundCount++] = proposal; }
			else companionBounds[match] = UnionRectangle(companionBounds[match], proposal);
		}
		std::array<Candidate, MaximumLines - 1> companions{};
		size_t companionCount = 0;
		for (size_t index = 0; index < companionBoundCount && companionCount < companions.size(); ++index)
		{
			const PanelSubtitleRect& bounds = companionBounds[index];
			if (!IsPictureOnly(input, bounds) ||
				!RelatedCaptionMember(cueMembers[0].line.glyphBounds, bounds, height))
				continue;
			if ((bottomCue && bounds.bottom > cueMembers[0].line.glyphBounds.top) ||
				(!bottomCue && bounds.top < cueMembers[0].line.glyphBounds.bottom))
				continue;
			const int glyphWidth = RectWidth(bounds);
			const int glyphHeight = RectHeight(bounds);
			if (glyphWidth < std::max(m_settings.minimumGlyphWidth, width / 100) ||
				glyphHeight < std::max(m_settings.minimumGlyphHeight, height / 154) ||
				glyphHeight > height * m_settings.maximumGlyphHeightPercent / 100)
				continue;
			Candidate companion;
			if (!QualifyCandidate(input, bounds, companion, &cueMembers[0]))
				continue;
			companions[companionCount++] = companion;
		}
		// Anchor-rooted inward stack: bottom cues add the nearest lines above;
		// top cues add the nearest lines below.  Every companion is related to
		// the immutable anchor, never to a previously admitted companion, so UI
		// cannot bridge itself into a cue transitively.
		std::sort(companions.begin(), companions.begin() + companionCount,
			[&](const Candidate& left, const Candidate& right) {
				return IsBottomCue(cueMembers[0].line.location) ?
					left.line.glyphBounds.bottom > right.line.glyphBounds.bottom :
					left.line.glyphBounds.top < right.line.glyphBounds.top;
			});
		for (size_t index = 0; index < companionCount; ++index)
		{
			cueMembers[cueMemberCount++] = companions[index];
			combinedGlyph = UnionRectangle(combinedGlyph,
				companions[index].line.glyphBounds);
		}
	}
	else
	{
		for (size_t index = 0; index < candidateCount && cueMemberCount < MaximumLines; ++index)
		{
			if (index == anchorIndex || !RelatedCaptionMember(
				cueMembers[0].line.glyphBounds, candidates[index].line.glyphBounds, height))
				continue;
			cueMembers[cueMemberCount++] = candidates[index];
			combinedGlyph = UnionRectangle(combinedGlyph, candidates[index].line.glyphBounds);
		}
	}
	// Derive public geometry from the final semantic member list in one pass.
	// Incremental proposal geometry must never survive companion ordering or
	// fragment coalescing and leave an admitted member outside the cue box.
	combinedGlyph = cueMembers[0].line.glyphBounds;
	for (size_t index = 1; index < cueMemberCount; ++index)
		combinedGlyph = UnionRectangle(combinedGlyph,
			cueMembers[index].line.glyphBounds);
	BuildMask(input, cueMembers, cueMemberCount);
	int largestMemberHeight = 0;
	for (size_t index = 0; index < cueMemberCount; ++index)
		largestMemberHeight = std::max(largestMemberHeight,
			RectHeight(cueMembers[index].line.glyphBounds));
	const int captureInset = std::max(22, largestMemberHeight);
	PanelSubtitleRect combinedCapture = {
		combinedGlyph.left - std::max(32, captureInset), combinedGlyph.top - captureInset,
		combinedGlyph.right + std::max(32, captureInset), combinedGlyph.bottom + captureInset };
	if (IsTrustedActivePicture(input))
	{
		// Bar-only cues stay in the bar; boundary cues get at most one member
		// height of picture context rather than a 1.6x *combined* cue explosion.
		if (IsBottomCue(cueMembers[0].line.location))
		{
			if (combinedGlyph.top >= input.activePictureBottom)
				combinedCapture.top = std::max(combinedCapture.top, input.activePictureBottom);
			combinedCapture.bottom = std::min(combinedCapture.bottom, height);
		}
		else
		{
			if (combinedGlyph.bottom <= input.activePictureTop)
				combinedCapture.bottom = std::min(combinedCapture.bottom, input.activePictureTop);
			combinedCapture.top = std::max(combinedCapture.top, 0);
		}
	}
	else
	{
		// Keep legacy constrained-domain diagnostics unchanged.
		const int legacyInset = std::max(22,
			static_cast<int>(std::ceil(RectHeight(combinedGlyph) * 1.6)));
		combinedCapture.top = combinedGlyph.top - legacyInset;
		combinedCapture.bottom = combinedGlyph.bottom + legacyInset;
		combinedCapture.left = combinedGlyph.left - std::max(32, legacyInset);
		combinedCapture.right = combinedGlyph.right + std::max(32, legacyInset);
	}
	combinedCapture = ClampRectangle(combinedCapture, width, height);

	result = {};
	result.state = PanelSubtitleState::Candidate;
	result.sourceSequence = input.sourceSequence;
	result.generation = input.generation;
	result.rasterWidth = input.width;
	result.rasterHeight = input.height;
	result.cue.captureBounds = combinedCapture;
	result.cue.glyphBounds = combinedGlyph;
	result.cue.maskBounds = combinedGlyph;
	result.cue.location = cueMembers[0].line.location;
	result.cue.backingLuma = cueMembers[0].backingLuma;
	result.cue.memberCount = cueMemberCount;
	result.lineCount = cueMemberCount;
	for (size_t index = 0; index < cueMemberCount; ++index)
	{
		result.cue.members[index] = cueMembers[index].line;
		result.lines[index] = cueMembers[index].line; // legacy/log projection
	}
	// Public geometry is one immutable caption capture box. Individual line
	// records remain private mask/validation anchors, never separate captions.
	result.panelBounds = combinedCapture;
	result.glyphBounds = combinedGlyph;
	result.maskBounds = combinedGlyph;
	result.panelLuma = cueMembers[0].backingLuma;
	result.fingerprint = cueMembers[0].line.fingerprint;
	for (size_t index = 1; index < cueMemberCount; ++index)
		result.fingerprint ^= cueMembers[index].line.fingerprint +
			0x9e3779b97f4a7c15ULL + (result.fingerprint << 6) +
			(result.fingerprint >> 2);
	result.stabilityObservations = 1;
	return true;
}

bool PanelSubtitleDetector::HasStableSeedOverlap(const PanelSubtitleInput& input,
	const PanelSubtitleGlyphLine& line) const
{
	if (!m_stable.softGlyphMask ||
		m_stable.softGlyphMask->size() != input.width * input.height)
		return false;
	uint32_t currentSeeds = 0;
	uint32_t previousSeeds = 0;
	uint32_t overlap = 0;
	const bool anchoredPictureMember = IsPictureOnly(input, line.glyphBounds) &&
		line.location != PanelSubtitleLocation::None;
	for (int y = line.glyphBounds.top; y < line.glyphBounds.bottom; ++y)
		for (int x = line.glyphBounds.left; x < line.glyphBounds.right; ++x)
		{
			const size_t index = static_cast<size_t>(y) * input.width + x;
			const auto* row = reinterpret_cast<const uint16_t*>(
				reinterpret_cast<const uint8_t*>(input.p010Luma) +
				static_cast<size_t>(y) * input.strideBytes);
			const bool currentSeed = anchoredPictureMember ?
				(HasNeutralChroma(input, x, y) &&
					P010Code(row, x) >= m_settings.minimumGlyphLuma) :
				IsGlyphSeed(input, x, y);
			const bool current = currentSeed &&
				IsContrastGlyph(input, x, y, line.backingLuma);
			const bool previous = (*m_stable.softGlyphMask)[index] != 0;
			currentSeeds += current;
			previousSeeds += previous;
			overlap += current && previous;
		}
	const uint32_t reference = std::max(currentSeeds, previousSeeds);
	return reference != 0 && overlap * 100 >= reference * 70;
}

bool PanelSubtitleDetector::ValidateStable(const PanelSubtitleInput& input,
	PanelSubtitleResult& result)
{
	if (m_stable.state != PanelSubtitleState::Stable ||
		m_stable.cue.memberCount == 0 ||
		!SameGeneration(m_stable.generation, input.generation))
		return false;

	// Long-running subtitle cues are the normal case. Revalidate only each
	// frozen glyph/capture ROI, rebuild the local mask, and keep the confirmed
	// capture geometry. A failed validation immediately falls back to full
	// acquisition below.
	std::array<Candidate, MaximumRawCandidates> candidates{};
	for (size_t index = 0; index < m_stable.cue.memberCount; ++index)
	{
		const Candidate* anchor = index != 0 && IsPictureOnly(input,
			m_stable.cue.members[index].glyphBounds) ? &candidates[0] : nullptr;
		if (!QualifyCandidate(input, m_stable.cue.members[index].glyphBounds,
			candidates[index], anchor, index == 0) ||
			!HasStableSeedOverlap(input, m_stable.cue.members[index]) ||
			candidates[index].line.location != m_stable.cue.members[index].location ||
			std::abs(static_cast<int>(candidates[index].line.seedPixels) -
				static_cast<int>(m_stable.cue.members[index].seedPixels)) >
				std::max(8, static_cast<int>(m_stable.cue.members[index].seedPixels) / 6) ||
			std::abs(static_cast<int>(candidates[index].line.backingLuma) -
				static_cast<int>(m_stable.cue.members[index].backingLuma)) > 32)
			return false;
	}
	BuildMask(input, candidates, m_stable.cue.memberCount);
	result = WithCurrentFrame(m_stable, input);
	++result.stabilityObservations;
	return true;
}

bool PanelSubtitleDetector::Matches(const PanelSubtitleResult& left,
	const PanelSubtitleResult& right) const
{
	if (left.state == PanelSubtitleState::Unavailable ||
		right.state == PanelSubtitleState::Unavailable ||
		left.rasterWidth != right.rasterWidth ||
		left.rasterHeight != right.rasterHeight ||
		!SameGeneration(left.generation, right.generation) ||
		left.cue.memberCount == 0 || left.cue.memberCount != right.cue.memberCount ||
		left.cue.location != right.cue.location)
		return false;
	const int tolerance = std::max(2, static_cast<int>(left.rasterHeight) / 270);
	for (size_t index = 0; index < left.cue.memberCount; ++index)
	{
		if (left.cue.members[index].location != right.cue.members[index].location ||
			!SameRectangleWithin(left.cue.members[index].glyphBounds,
				right.cue.members[index].glyphBounds, tolerance))
			return false;
	}
	return true;
}

void PanelSubtitleDetector::AttachMask(PanelSubtitleResult& result) const
{
	result.softGlyphMask = m_workMask;
}

PanelSubtitleResult PanelSubtitleDetector::WithCurrentFrame(
	const PanelSubtitleResult& result, const PanelSubtitleInput& input,
	bool maskVerified) const
{
	PanelSubtitleResult current = result;
	current.sourceSequence = input.sourceSequence;
	current.generation = input.generation;
	if (maskVerified)
		AttachMask(current);
	current.currentMaskVerified = maskVerified;
	return current;
}

PanelSubtitleResult PanelSubtitleDetector::Analyze(const PanelSubtitleInput& input)
{
	PanelSubtitleResult unavailable;
	unavailable.sourceSequence = input.sourceSequence;
	unavailable.generation = input.generation;
	unavailable.rasterWidth = input.width;
	unavailable.rasterHeight = input.height;
	if (!input.enabled || !input.p010Luma || input.width == 0 ||
		input.height == 0 || input.strideBytes < input.width * sizeof(uint16_t) ||
		(input.activePictureStable && !IsTrustedActivePicture(input)))
	{
		Reset();
		return unavailable;
	}
	if (!m_hasAcquisitionGeneration ||
		!SameGeneration(m_acquisitionGeneration, input.generation))
	{
		m_candidate = {};
		m_stable = {};
		m_acquisitionGeneration = input.generation;
		m_nextAcquisitionSequence = input.sourceSequence;
		m_hasAcquisitionGeneration = true;
	}

	PanelSubtitleResult validated;
	if (ValidateStable(input, validated))
	{
		m_stableSoftMisses = 0;
		return validated;
	}
	const bool stableValidationFailed =
		m_stable.state == PanelSubtitleState::Stable;
	if (stableValidationFailed)
		++m_stableSoftMisses;
	const bool candidatePending =
		m_candidate.state == PanelSubtitleState::Candidate &&
		SameGeneration(m_candidate.generation, input.generation);
	if (!stableValidationFailed && !candidatePending &&
		input.sourceSequence < m_nextAcquisitionSequence)
		return unavailable;

	++m_acquisitionScanCount;
	PanelSubtitleResult current;
	if (!BuildCandidate(input, current))
	{
		m_candidate = {};
		if (stableValidationFailed && m_stableSoftMisses <= static_cast<uint32_t>(
			std::max(0, m_settings.releaseGraceFrames)))
			return WithCurrentFrame(m_stable, input, false);
		m_stable = {};
		m_stableSoftMisses = 0;
		m_nextAcquisitionSequence = input.sourceSequence +
			static_cast<uint64_t>(std::max(1,
				m_settings.acquisitionIntervalFrames));
		return unavailable;
	}
	if (candidatePending && Matches(m_candidate, current))
	{
		const uint32_t observations = m_candidate.stabilityObservations + 1;
		if (observations < static_cast<uint32_t>(
			std::max(2, m_settings.confirmationFrames)))
		{
			m_candidate.stabilityObservations = observations;
			return WithCurrentFrame(m_candidate, input);
		}
		m_stable = m_candidate; // Keep first-observation geometry frozen.
		m_stable.state = PanelSubtitleState::Stable;
		m_stable.stabilityObservations = observations;
		m_candidate = {};
		m_stableSoftMisses = 0;
		return WithCurrentFrame(m_stable, input);
	}

	m_stable = {};
	m_stableSoftMisses = 0;
	AttachMask(current);
	current.currentMaskVerified = true;
	m_candidate = current;
	// Always inspect the next frame so a newly seen cue can lock immediately;
	// the idle cadence applies only while no candidate exists.
	m_nextAcquisitionSequence = input.sourceSequence + 1;
	return current;
}
