#include "pch.h"
#include "PanelSubtitleDetector.h"

#include <algorithm>
#include <array>
#include <cstdlib>

PanelSubtitleDetector::PanelSubtitleDetector(
	const PanelSubtitleDetectorSettings& settings) :
	m_settings(settings)
{
}

bool PanelSubtitleDetector::IsValid(const PanelSubtitleRect& rectangle)
{
	return rectangle.right > rectangle.left && rectangle.bottom > rectangle.top;
}

bool PanelSubtitleDetector::SameRectangle(const PanelSubtitleRect& left,
	const PanelSubtitleRect& right)
{
	return left.left == right.left && left.top == right.top &&
		left.right == right.right && left.bottom == right.bottom;
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

void PanelSubtitleDetector::Reset()
{
	m_candidate = {};
	m_stable = {};
}

PanelSubtitleDetector::RowRun PanelSubtitleDetector::LongestDarkRun(
	const PanelSubtitleInput& input, int y) const
{
	const auto* row = reinterpret_cast<const uint16_t*>(
		reinterpret_cast<const uint8_t*>(input.p010Luma) +
		static_cast<size_t>(y) * input.strideBytes);
	RowRun best;
	int runLeft = 0;
	bool inRun = false;
	for (int x = 0; x < static_cast<int>(input.width); ++x)
	{
		if (P010Code(row, x) <= m_settings.maximumPanelLuma)
		{
			if (!inRun)
			{
				runLeft = x;
				inRun = true;
			}
		}
		else if (inRun)
		{
			if (x - runLeft > best.right - best.left)
				best = { runLeft, x };
			inRun = false;
		}
	}
	if (inRun && static_cast<int>(input.width) - runLeft > best.right - best.left)
		best = { runLeft, static_cast<int>(input.width) };
	return best;
}

bool PanelSubtitleDetector::IsDarkEnough(const PanelSubtitleInput& input,
	int y, int left, int right) const
{
	const auto* row = reinterpret_cast<const uint16_t*>(
		reinterpret_cast<const uint8_t*>(input.p010Luma) +
		static_cast<size_t>(y) * input.strideBytes);
	int dark = 0;
	for (int x = left; x < right; ++x)
		dark += P010Code(row, x) <= m_settings.maximumPanelLuma;
	return dark * 100 >= (right - left) *
		m_settings.minimumPanelDarkCoveragePercent;
}

bool PanelSubtitleDetector::BuildCandidate(const PanelSubtitleInput& input,
	PanelSubtitleResult& result)
{
	const int width = static_cast<int>(input.width);
	const int height = static_cast<int>(input.height);
	const int top = std::max(0, input.searchTop);
	const int bottom = std::min(height, input.searchBottom);
	if (top >= bottom || width < m_settings.minimumPanelWidth ||
		bottom - top < m_settings.minimumPanelHeight)
		return false;

	PanelSubtitleRect bestPanel;
	int64_t bestArea = 0;
	for (int y = top; y < bottom; ++y)
	{
		const RowRun seed = LongestDarkRun(input, y);
		if (seed.right - seed.left < m_settings.minimumPanelWidth ||
			seed.left < m_settings.minimumHorizontalInset ||
			seed.right > width - m_settings.minimumHorizontalInset)
			continue;

		int panelTop = y;
		while (panelTop > top && IsDarkEnough(input, panelTop - 1,
			seed.left, seed.right))
			--panelTop;
		int panelBottom = y + 1;
		while (panelBottom < bottom && IsDarkEnough(input, panelBottom,
			seed.left, seed.right))
			++panelBottom;
		const int64_t area = static_cast<int64_t>(seed.right - seed.left) *
			(panelBottom - panelTop);
		if (panelBottom - panelTop >= m_settings.minimumPanelHeight &&
			area > bestArea)
		{
			bestArea = area;
			bestPanel = { seed.left, panelTop, seed.right, panelBottom };
		}
	}
	if (!IsValid(bestPanel))
		return false;

	std::array<uint32_t, 1024> histogram{};
	uint64_t sampleCount = 0;
	for (int y = bestPanel.top; y < bestPanel.bottom; ++y)
	{
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
			static_cast<size_t>(y) * input.strideBytes);
		for (int x = bestPanel.left; x < bestPanel.right; ++x)
		{
			const uint16_t code = P010Code(row, x);
			if (code <= m_settings.maximumPanelLuma)
			{
				++histogram[code];
				++sampleCount;
			}
		}
	}
	if (sampleCount == 0)
		return false;
	uint64_t running = 0;
	uint16_t panelLuma = 0;
	for (size_t code = 0; code < histogram.size(); ++code)
	{
		running += histogram[code];
		if (running * 2 >= sampleCount)
		{
			panelLuma = static_cast<uint16_t>(code);
			break;
		}
	}

	uint64_t uniformPixels = 0;
	const size_t maskPixels = input.width * input.height;
	if (m_workMask.size() != maskPixels)
		m_workMask.assign(maskPixels, 0);
	else
		std::fill(m_workMask.begin(), m_workMask.end(), 0);
	if (m_rowCounts.size() != input.height)
		m_rowCounts.assign(input.height, 0);
	else
		std::fill(m_rowCounts.begin(), m_rowCounts.end(), 0);
	if (m_columnCounts.size() != input.width)
		m_columnCounts.assign(input.width, 0);
	else
		std::fill(m_columnCounts.begin(), m_columnCounts.end(), 0);

	uint64_t glyphPixels = 0;
	uint64_t fingerprint = 1469598103934665603ULL;
	for (int y = bestPanel.top; y < bestPanel.bottom; ++y)
	{
		const auto* row = reinterpret_cast<const uint16_t*>(
			reinterpret_cast<const uint8_t*>(input.p010Luma) +
			static_cast<size_t>(y) * input.strideBytes);
		for (int x = bestPanel.left; x < bestPanel.right; ++x)
		{
			const uint16_t code = P010Code(row, x);
			const int contrast = std::abs(static_cast<int>(code) -
				static_cast<int>(panelLuma));
			if (contrast <= m_settings.maximumPanelVariation)
				++uniformPixels;
			uint8_t alpha = 0;
			if (contrast > m_settings.minimumGlyphContrast)
			{
				const int denominator = std::max(1,
					static_cast<int>(m_settings.glyphSoftness));
				alpha = static_cast<uint8_t>(std::min(255,
					(contrast - m_settings.minimumGlyphContrast) * 255 /
					denominator));
			}
			const size_t index = static_cast<size_t>(y) * input.width + x;
			m_workMask[index] = alpha;
			if (alpha != 0)
			{
				++glyphPixels;
				++m_rowCounts[y];
				++m_columnCounts[x];
				fingerprint ^= static_cast<uint64_t>((y - bestPanel.top) * 257 +
					(x - bestPanel.left) + alpha);
				fingerprint *= 1099511628211ULL;
			}
		}
	}
	const uint64_t panelPixels = static_cast<uint64_t>(bestPanel.right - bestPanel.left) *
		(bestPanel.bottom - bestPanel.top);
	if (uniformPixels * 100 < panelPixels *
		m_settings.minimumPanelUniformityPercent ||
		glyphPixels < static_cast<uint64_t>(m_settings.minimumGlyphPixels))
		return false;

	int glyphTop = bestPanel.bottom;
	int glyphBottom = bestPanel.top;
	for (int y = bestPanel.top; y < bestPanel.bottom; ++y)
	{
		if (m_rowCounts[y] >= static_cast<uint32_t>(m_settings.minimumGlyphRows))
		{
			glyphTop = std::min(glyphTop, y);
			glyphBottom = std::max(glyphBottom, y + 1);
		}
	}
	int glyphLeft = bestPanel.right;
	int glyphRight = bestPanel.left;
	for (int x = bestPanel.left; x < bestPanel.right; ++x)
	{
		if (m_columnCounts[x] >= static_cast<uint32_t>(m_settings.minimumGlyphColumns))
		{
			glyphLeft = std::min(glyphLeft, x);
			glyphRight = std::max(glyphRight, x + 1);
		}
	}
	const PanelSubtitleRect glyphBounds = {
		glyphLeft, glyphTop, glyphRight, glyphBottom };
	if (!IsValid(glyphBounds))
		return false;

	result = {};
	result.state = PanelSubtitleState::Candidate;
	result.sourceSequence = input.sourceSequence;
	result.generation = input.generation;
	result.rasterWidth = input.width;
	result.rasterHeight = input.height;
	result.panelBounds = bestPanel;
	result.glyphBounds = glyphBounds;
	result.maskBounds = glyphBounds;
	result.panelLuma = panelLuma;
	result.fingerprint = fingerprint ^
		(static_cast<uint64_t>(panelLuma) << 48) ^
		(static_cast<uint64_t>(bestPanel.left) << 32) ^
		static_cast<uint64_t>(bestPanel.top);
	result.stabilityObservations = 1;
	return true;
}

bool PanelSubtitleDetector::Matches(const PanelSubtitleResult& left,
	const PanelSubtitleResult& right) const
{
	return left.state != PanelSubtitleState::Unavailable &&
		right.state != PanelSubtitleState::Unavailable &&
		left.rasterWidth == right.rasterWidth &&
		left.rasterHeight == right.rasterHeight &&
		SameGeneration(left.generation, right.generation) &&
		SameRectangle(left.panelBounds, right.panelBounds) &&
		SameRectangle(left.glyphBounds, right.glyphBounds) &&
		left.panelLuma == right.panelLuma &&
		left.fingerprint == right.fingerprint;
}

void PanelSubtitleDetector::AttachMask(PanelSubtitleResult& result) const
{
	result.softGlyphMask = std::make_shared<const std::vector<uint8_t>>(m_workMask);
}

PanelSubtitleResult PanelSubtitleDetector::WithCurrentFrame(
	const PanelSubtitleResult& result, const PanelSubtitleInput& input) const
{
	PanelSubtitleResult current = result;
	current.sourceSequence = input.sourceSequence;
	current.generation = input.generation;
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
		input.height == 0 || input.strideBytes < input.width * sizeof(uint16_t))
	{
		Reset();
		return unavailable;
	}

	PanelSubtitleResult current;
	if (!BuildCandidate(input, current))
	{
		Reset();
		return unavailable;
	}
	if (m_stable.state == PanelSubtitleState::Stable && Matches(m_stable, current))
		return WithCurrentFrame(m_stable, input);
	if (m_candidate.state == PanelSubtitleState::Candidate && Matches(m_candidate, current))
	{
		m_stable = m_candidate;
		m_stable.state = PanelSubtitleState::Stable;
		m_stable.stabilityObservations = 2;
		m_candidate = {};
		return WithCurrentFrame(m_stable, input);
	}

	m_stable = {};
	AttachMask(current);
	m_candidate = current;
	return current;
}
