#include "pch.h"

#include "ActivePictureEvidence.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>


namespace
{
constexpr int kLineSamples = 48;
constexpr int kEdgeDepthSamples = 6;
constexpr int kGlobalGridWidth = 16;
constexpr int kGlobalGridHeight = 16;
constexpr int kGlobalNearBlackP90 = 96;
constexpr int kVisibleExtentLineSamples = 256;
constexpr int kVisibleExtentDepthSamples = 64;

template <typename T>
T Bounded(T value, T minimum, T maximum)
{
	return std::max(minimum, std::min(value, maximum));
}

double Percentile(std::vector<int> values, double fraction)
{
	if (values.empty())
		return 0.0;
	const size_t index = std::min(values.size() - 1,
		static_cast<size_t>(fraction * static_cast<double>(values.size() - 1)));
	std::nth_element(values.begin(), values.begin() + index, values.end());
	return static_cast<double>(values[index]);
}

bool IsValidBoundsForSource(const ActivePictureBounds& bounds,
	const AnalysisLumaSource& source)
{
	return bounds.rasterWidth == source.width &&
		bounds.rasterHeight == source.height &&
		bounds.left >= 0 && bounds.top >= 0 &&
		bounds.right > bounds.left && bounds.bottom > bounds.top &&
		bounds.right <= source.width && bounds.bottom <= source.height;
}

bool Contains(const ActivePictureBounds& outside,
	const ActivePictureBounds& inside)
{
	return outside.rasterWidth == inside.rasterWidth &&
		outside.rasterHeight == inside.rasterHeight &&
		inside.left >= outside.left && inside.top >= outside.top &&
		inside.right <= outside.right && inside.bottom <= outside.bottom;
}

struct SampleContext
{
	const AnalysisLumaSource& source;
	size_t lumaSamples = 0;
	size_t chromaSamples = 0;

	int Luma(int x, int y)
	{
		AnalysisLumaSample sample;
		if (!source.Sample(x, y, sample))
			return 0;
		++lumaSamples;
		return sample.luma;
	}

	void Chroma(int x, int y, int& u, int& v)
	{
		AnalysisLumaSample sample;
		if (!source.Sample(x, y, sample))
		{
			u = 0;
			v = 0;
			return;
		}
		u = sample.chromaU;
		v = sample.chromaV;
		++chromaSamples;
	}
};

bool IsBlackRow(SampleContext& samples, int y, int threshold)
{
	int black = 0;
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int x = ((i * 2 + 1) * samples.source.width) /
			(kLineSamples * 2);
		if (samples.Luma(x, y) <= threshold)
			++black;
	}
	return black >= 44;
}

bool IsBlackColumn(SampleContext& samples, int x, int threshold)
{
	int black = 0;
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int y = ((i * 2 + 1) * samples.source.height) /
			(kLineSamples * 2);
		if (samples.Luma(x, y) <= threshold)
			++black;
	}
	return black >= 44;
}

ActivePictureEdgeEvidence InspectHorizontalEdge(SampleContext& samples, bool top,
	int barPixels, int boundary, int blackFloor, int blackThreshold)
{
	ActivePictureEdgeEvidence evidence;
	evidence.barPixels = barPixels;
	if (barPixels <= 0)
		return evidence;

	std::vector<int> luma;
	luma.reserve(kEdgeDepthSamples * kLineSamples);
	int black = 0;
	int neutral = 0;
	int continuousLines = 0;
	double texture = 0.0;
	for (int d = 0; d < kEdgeDepthSamples; ++d)
	{
		const int depth = std::min(barPixels - 1,
			((d * 2 + 1) * barPixels) / (kEdgeDepthSamples * 2));
		const int y = top ? depth : samples.source.height - 1 - depth;
		int lineBlack = 0;
		int previous = -1;
		for (int i = 0; i < kLineSamples; ++i)
		{
			const int x = ((i * 2 + 1) * samples.source.width) /
				(kLineSamples * 2);
			const int value = samples.Luma(x, y);
			luma.push_back(value);
			black += value <= blackThreshold;
			lineBlack += value <= blackThreshold;
			if (previous >= 0)
				texture += std::abs(value - previous);
			previous = value;
			int u = 0, v = 0;
			samples.Chroma(x, y, u, v);
			neutral += std::abs(u - 512) <= 32 &&
				std::abs(v - 512) <= 32;
		}
		continuousLines += lineBlack >= 44;
	}
	const double outerMean = luma.empty() ? 0.0 :
		static_cast<double>(std::accumulate(luma.begin(), luma.end(), 0LL)) /
		luma.size();
	double innerMean = 0.0;
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int x = ((i * 2 + 1) * samples.source.width) /
			(kLineSamples * 2);
		const int y = Bounded(top ? boundary + 2 : boundary - 3,
			0, samples.source.height - 1);
		innerMean += samples.Luma(x, y);
	}
	innerMean /= kLineSamples;
	evidence.blackFraction = static_cast<double>(black) / luma.size();
	evidence.lumaFloor = blackFloor;
	evidence.lumaP90 = Percentile(luma, 0.90);
	evidence.lumaDispersion =
		Percentile(luma, 0.90) - Percentile(luma, 0.10);
	evidence.texture = texture /
		std::max<size_t>(1, luma.size() - kEdgeDepthSamples);
	evidence.neutralChromaFraction =
		static_cast<double>(neutral) / luma.size();
	evidence.innerBoundaryContrast = innerMean - outerMean;
	evidence.continuity =
		static_cast<double>(continuousLines) / kEdgeDepthSamples;
	const bool isSmall = barPixels < samples.source.height / 20;
	const double requiredContrast = isSmall ? 18.0 : 10.0;
	evidence.trusted = evidence.blackFraction >= 0.95 &&
		evidence.lumaP90 <= blackThreshold &&
		evidence.lumaDispersion <= 24.0 &&
		evidence.texture <= 8.0 &&
		evidence.neutralChromaFraction >= 0.90 &&
		evidence.innerBoundaryContrast >= requiredContrast &&
		evidence.continuity >= 0.99;
	evidence.confidence = Bounded(
		0.30 * evidence.blackFraction +
		0.20 * evidence.neutralChromaFraction +
		0.20 * evidence.continuity +
		0.30 * Bounded(evidence.innerBoundaryContrast / 48.0, 0.0, 1.0),
		0.0, 1.0);
	return evidence;
}

ActivePictureEdgeEvidence InspectVerticalEdge(SampleContext& samples, bool left,
	int barPixels, int boundary, int blackFloor, int blackThreshold)
{
	ActivePictureEdgeEvidence evidence;
	evidence.barPixels = barPixels;
	if (barPixels <= 0)
		return evidence;

	std::vector<int> luma;
	luma.reserve(kEdgeDepthSamples * kLineSamples);
	int black = 0;
	int neutral = 0;
	int continuousLines = 0;
	double texture = 0.0;
	for (int d = 0; d < kEdgeDepthSamples; ++d)
	{
		const int depth = std::min(barPixels - 1,
			((d * 2 + 1) * barPixels) / (kEdgeDepthSamples * 2));
		const int x = left ? depth : samples.source.width - 1 - depth;
		int lineBlack = 0;
		int previous = -1;
		for (int i = 0; i < kLineSamples; ++i)
		{
			const int y = ((i * 2 + 1) * samples.source.height) /
				(kLineSamples * 2);
			const int value = samples.Luma(x, y);
			luma.push_back(value);
			black += value <= blackThreshold;
			lineBlack += value <= blackThreshold;
			if (previous >= 0)
				texture += std::abs(value - previous);
			previous = value;
			int u = 0, v = 0;
			samples.Chroma(x, y, u, v);
			neutral += std::abs(u - 512) <= 32 &&
				std::abs(v - 512) <= 32;
		}
		continuousLines += lineBlack >= 44;
	}
	const double outerMean = luma.empty() ? 0.0 :
		static_cast<double>(std::accumulate(luma.begin(), luma.end(), 0LL)) /
		luma.size();
	double innerMean = 0.0;
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int y = ((i * 2 + 1) * samples.source.height) /
			(kLineSamples * 2);
		const int x = Bounded(left ? boundary + 2 : boundary - 3,
			0, samples.source.width - 1);
		innerMean += samples.Luma(x, y);
	}
	innerMean /= kLineSamples;
	evidence.blackFraction = static_cast<double>(black) / luma.size();
	evidence.lumaFloor = blackFloor;
	evidence.lumaP90 = Percentile(luma, 0.90);
	evidence.lumaDispersion =
		Percentile(luma, 0.90) - Percentile(luma, 0.10);
	evidence.texture = texture /
		std::max<size_t>(1, luma.size() - kEdgeDepthSamples);
	evidence.neutralChromaFraction =
		static_cast<double>(neutral) / luma.size();
	evidence.innerBoundaryContrast = innerMean - outerMean;
	evidence.continuity =
		static_cast<double>(continuousLines) / kEdgeDepthSamples;
	const bool isSmall = barPixels < samples.source.width / 20;
	const double requiredContrast = isSmall ? 18.0 : 10.0;
	evidence.trusted = evidence.blackFraction >= 0.95 &&
		evidence.lumaP90 <= blackThreshold &&
		evidence.lumaDispersion <= 24.0 &&
		evidence.texture <= 8.0 &&
		evidence.neutralChromaFraction >= 0.90 &&
		evidence.innerBoundaryContrast >= requiredContrast &&
		evidence.continuity >= 0.99;
	evidence.confidence = Bounded(
		0.30 * evidence.blackFraction +
		0.20 * evidence.neutralChromaFraction +
		0.20 * evidence.continuity +
		0.30 * Bounded(evidence.innerBoundaryContrast / 48.0, 0.0, 1.0),
		0.0, 1.0);
	return evidence;
}


bool ExcludedBandPixelsAreSafe(const ActivePictureEdgeEvidence& evidence)
{
	if (evidence.barPixels <= 0)
		return true;
	// Retention is deliberately a pixel-safety test, not a second acquisition
	// test. Inner-boundary contrast is therefore not part of this predicate.
	return evidence.blackFraction >= 0.95 &&
		evidence.lumaP90 <= evidence.lumaFloor + 24.0 &&
		evidence.lumaP90 <= 104.0 &&
		evidence.lumaDispersion <= 24.0 &&
		evidence.texture <= 8.0 &&
		evidence.neutralChromaFraction >= 0.90 &&
		evidence.continuity >= 0.99;
}

struct ExcludedBandVisibleExtent
{
	bool available = false;
	int coordinate = 0;
};

bool IsCrediblyVisible(SampleContext& samples, int x, int y,
	int blackThreshold)
{
	AnalysisLumaSample sample;
	if (!samples.source.Sample(x, y, sample))
		return false;
	++samples.lumaSamples;
	++samples.chromaSamples;
	// Match the denser renderer-local bar pass: blackThreshold already carries
	// 24 codes above the measured floor, so this is floor + 32. The denser grid
	// and 2x2 support rule retain noise rejection while covering small controls.
	const bool elevatedLuma = sample.luma > blackThreshold + 8;
	const bool colored = (std::abs(static_cast<int>(sample.chromaU) - 512) >= 64 ||
		std::abs(static_cast<int>(sample.chromaV) - 512) >= 64) &&
		sample.luma >= blackThreshold + 8;
	return elevatedLuma || colored;
}

ExcludedBandVisibleExtent FindHorizontalVisibleExtent(SampleContext& samples,
	bool top, int barPixels, int blackThreshold)
{
	ExcludedBandVisibleExtent result;
	if (barPixels <= 0)
		return result;
	const int depthSamples = std::min(kVisibleExtentDepthSamples, barPixels);
	int previousOccupied = -2;
	for (int d = 0; d < depthSamples; ++d)
	{
		const int depth = ((d * 2 + 1) * barPixels) / (depthSamples * 2);
		const int y = top ? depth : samples.source.height - 1 - depth;
		int visible = 0;
		for (int i = 0; i < kVisibleExtentLineSamples; ++i)
		{
			const int x = ((i * 2 + 1) * samples.source.width) /
				(kVisibleExtentLineSamples * 2);
			visible += IsCrediblyVisible(samples, x, y, blackThreshold) ? 1 : 0;
		}
		// Two spatial samples are enough for a narrow glyph, but require the
		// signal on adjacent depth rows below to reject isolated hot pixels.
		if (visible < 2)
			continue;
		if (d != previousOccupied + 1)
		{
			previousOccupied = d;
			continue;
		}
		const int outerDepthIndex = previousOccupied;
		const int extentDepth = ((outerDepthIndex * 2 + 1) * barPixels) /
			(depthSamples * 2);
		const int sampleStep = std::max(1, barPixels / depthSamples);
		result.available = true;
		result.coordinate = top ? std::max(0, extentDepth - sampleStep) :
			std::min(samples.source.height,
				samples.source.height - extentDepth + sampleStep);
		return result;
	}
	return result;
}

ExcludedBandVisibleExtent FindVerticalVisibleExtent(SampleContext& samples,
	bool left, int barPixels, int blackThreshold)
{
	ExcludedBandVisibleExtent result;
	if (barPixels <= 0)
		return result;
	const int depthSamples = std::min(kVisibleExtentDepthSamples, barPixels);
	int previousOccupied = -2;
	for (int d = 0; d < depthSamples; ++d)
	{
		const int depth = ((d * 2 + 1) * barPixels) / (depthSamples * 2);
		const int x = left ? depth : samples.source.width - 1 - depth;
		int visible = 0;
		for (int i = 0; i < kVisibleExtentLineSamples; ++i)
		{
			const int y = ((i * 2 + 1) * samples.source.height) /
				(kVisibleExtentLineSamples * 2);
			visible += IsCrediblyVisible(samples, x, y, blackThreshold) ? 1 : 0;
		}
		if (visible < 2)
			continue;
		if (d != previousOccupied + 1)
		{
			previousOccupied = d;
			continue;
		}
		const int outerDepthIndex = previousOccupied;
		const int extentDepth = ((outerDepthIndex * 2 + 1) * barPixels) /
			(depthSamples * 2);
		const int sampleStep = std::max(1, barPixels / depthSamples);
		result.available = true;
		result.coordinate = left ? std::max(0, extentDepth - sampleStep) :
			std::min(samples.source.width,
				samples.source.width - extentDepth + sampleStep);
		return result;
	}
	return result;
}
}


ActivePictureEvidence ExtractActivePictureEvidence(
	const AnalysisLumaSource& source)
{
	ActivePictureEvidence result;
	if (!source.IsValid() || source.width < 16 || source.height < 16)
	{
		result.reason = "invalid analysis source dimensions, layout, or data pointer";
		return result;
	}

	SampleContext samples{ source };
	std::vector<int> perimeter;
	perimeter.reserve(256);
	for (int i = 0; i < 64; ++i)
	{
		const int x = ((i * 2 + 1) * source.width) / 128;
		const int y = ((i * 2 + 1) * source.height) / 128;
		perimeter.push_back(samples.Luma(x, 0));
		perimeter.push_back(samples.Luma(x, source.height - 1));
		perimeter.push_back(samples.Luma(0, y));
		perimeter.push_back(samples.Luma(source.width - 1, y));
	}
	const int observedLow = static_cast<int>(Percentile(perimeter, 0.10));
	const int blackFloor = observedLow < 32 ? 0 :
		Bounded(observedLow, 48, 80);
	const int blackThreshold = std::min(104, blackFloor + 24);

	const int yStep = std::max(2, source.height / 540);
	const int xStep = std::max(2, source.width / 960);
	// All four directional searches share one hard budget. This keeps the
	// worst-case 4K inspection below 30,000 luma reads even for adversarial
	// all-black or nested-frame input.
	int scanLinesRemaining = 480;
	auto blackRow = [&](int y)
	{
		if (scanLinesRemaining <= 0)
			return false;
		--scanLinesRemaining;
		return IsBlackRow(samples, y, blackThreshold);
	};
	auto blackColumn = [&](int x)
	{
		if (scanLinesRemaining <= 0)
			return false;
		--scanLinesRemaining;
		return IsBlackColumn(samples, x, blackThreshold);
	};
	int top = 0;
	while (top + yStep < source.height / 2 &&
		blackRow(top))
		top += yStep;
	int bottom = source.height;
	while (bottom - yStep > source.height / 2 &&
		blackRow(bottom - 1))
		bottom -= yStep;
	int left = 0;
	while (left + xStep < source.width / 2 &&
		blackColumn(left))
		left += xStep;
	int right = source.width;
	while (right - xStep > source.width / 2 &&
		blackColumn(right - 1))
		right -= xStep;

	const int activeWidth = right - left;
	const int activeHeight = bottom - top;
	if (activeWidth < source.width / 3 || activeHeight < source.height / 3)
	{
		result.reason = "candidate is too small for a credible active picture";
		result.lumaSamples = samples.lumaSamples;
		return result;
	}
	const double proposedAspect =
		static_cast<double>(activeWidth) / activeHeight;
	if (proposedAspect < 1.0 || proposedAspect > 4.0)
	{
		result.reason = "candidate aspect is outside the supported range";
		result.lumaSamples = samples.lumaSamples;
		return result;
	}

	result.available = true;
	result.proposedBounds = { left, top, right, bottom, source.width,
		source.height, proposedAspect, ActivePictureBounds::BarAxes::NONE };
	result.trustedBounds = { 0, 0, source.width, source.height, source.width,
		source.height, static_cast<double>(source.width) / source.height,
		ActivePictureBounds::BarAxes::NONE };
	const int topBar = top;
	const int bottomBar = source.height - bottom;
	const int leftBar = left;
	const int rightBar = source.width - right;
	const bool hasVertical = topBar > yStep * 2 || bottomBar > yStep * 2;
	const bool hasHorizontal = leftBar > xStep * 2 || rightBar > xStep * 2;
	if (!hasVertical && !hasHorizontal)
	{
		result.classification =
			ActivePictureClassification::FULL_RASTER_TRUSTED;
		result.reason = "full raster has immediate crop authority";
		result.lumaSamples = samples.lumaSamples;
		result.chromaSamples = samples.chromaSamples;
		return result;
	}

	bool verticalTrusted = false;
	if (hasVertical)
	{
		result.top = InspectHorizontalEdge(samples, true, topBar, top,
			blackFloor, blackThreshold);
		result.bottom = InspectHorizontalEdge(samples, false, bottomBar, bottom,
			blackFloor, blackThreshold);
		const int symmetryTolerance = std::max(yStep * 2, source.height / 360);
		verticalTrusted = result.top.trusted && result.bottom.trusted &&
			std::abs(topBar - bottomBar) <= symmetryTolerance;
		if (verticalTrusted)
		{
			result.trustedBounds.top = top;
			result.trustedBounds.bottom = bottom;
		}
	}
	bool horizontalTrusted = false;
	if (hasHorizontal)
	{
		result.left = InspectVerticalEdge(samples, true, leftBar, left,
			blackFloor, blackThreshold);
		result.right = InspectVerticalEdge(samples, false, rightBar, right,
			blackFloor, blackThreshold);
		const int symmetryTolerance = std::max(xStep * 2, source.width / 360);
		horizontalTrusted = result.left.trusted && result.right.trusted &&
			std::abs(leftBar - rightBar) <= symmetryTolerance;
		if (horizontalTrusted)
		{
			result.trustedBounds.left = left;
			result.trustedBounds.right = right;
		}
	}
	const int trustedWidth =
		result.trustedBounds.right - result.trustedBounds.left;
	const int trustedHeight =
		result.trustedBounds.bottom - result.trustedBounds.top;
	result.trustedBounds.aspectRatio =
		static_cast<double>(trustedWidth) / trustedHeight;
	// Each axis carries its own crop authority. An untrusted dark feature on
	// the orthogonal axis must not veto an otherwise trusted opposing pair;
	// that axis remains at the full-raster bounds assigned above.
	result.trustedBounds.trustedBarAxes = static_cast<
		ActivePictureBounds::BarAxes>(
		(verticalTrusted ? static_cast<uint8_t>(
			ActivePictureBounds::BarAxes::TOP_BOTTOM) : 0) |
		(horizontalTrusted ? static_cast<uint8_t>(
			ActivePictureBounds::BarAxes::LEFT_RIGHT) : 0));
	result.classification = verticalTrusted || horizontalTrusted ?
		ActivePictureClassification::BAR_CROP_TRUSTED :
		ActivePictureClassification::PROVISIONAL;
	result.reason = result.classification ==
		ActivePictureClassification::BAR_CROP_TRUSTED ?
		"opposing black-bar evidence has crop authority" :
		"candidate lacks coherent opposing black-bar evidence";
	result.lumaSamples = samples.lumaSamples;
	result.chromaSamples = samples.chromaSamples;
	return result;
}

ActivePictureEvidence EvaluateSymmetricVerticalBarHypothesis(
	const AnalysisLumaSource& source,
	const ActivePictureEvidence& observed)
{
	ActivePictureEvidence result = observed;
	if (!source.IsValid() || !observed.available ||
		observed.classification != ActivePictureClassification::PROVISIONAL ||
		observed.proposedBounds.left != 0 ||
		observed.proposedBounds.right != source.width)
	{
		return result;
	}

	const int step = std::max(2, source.height / 540);
	const int observedTopBar = observed.proposedBounds.top;
	const int observedBottomBar = source.height - observed.proposedBounds.bottom;
	const bool cleanTop = observed.top.trusted &&
		(!observed.bottom.trusted || observedTopBar > observedBottomBar + step);
	const bool cleanBottom = observed.bottom.trusted &&
		(!observed.top.trusted || observedBottomBar > observedTopBar + step);
	if (cleanTop == cleanBottom)
		return result;
	const int barPixels = cleanTop ? observedTopBar : observedBottomBar;
	if (barPixels <= step * 2 || barPixels >= source.height / 3)
		return result;

	const int inferredTop = barPixels;
	const int inferredBottom = source.height - barPixels;
	const bool oppositeExpanded = cleanTop
		? observed.proposedBounds.bottom > inferredBottom + step
		: observed.proposedBounds.top < inferredTop - step;
	if (!oppositeExpanded || inferredBottom <= inferredTop)
		return result;

	SampleContext samples{ source };
	const ActivePictureEdgeEvidence& clean = cleanTop ? observed.top : observed.bottom;
	const int blackFloor = static_cast<int>(clean.lumaFloor);
	const int blackThreshold = std::min(104, blackFloor + 24);
	ActivePictureEdgeEvidence opposite = InspectHorizontalEdge(samples, !cleanTop,
		barPixels, cleanTop ? inferredBottom : inferredTop,
		blackFloor, blackThreshold);
	// Sparse glyphs may occupy part of the bar, but most sampled bar pixels and
	// several depth lines must remain coherent black. Broad/deep one-sided picture
	// expansion therefore stays provisional instead of being cropped away.
	const bool overlayCompatible = opposite.blackFraction >= 0.70 &&
		opposite.neutralChromaFraction >= 0.70 &&
		opposite.continuity >= 0.33 &&
		opposite.innerBoundaryContrast >= 10.0;
	if (!overlayCompatible)
		return result;

	opposite.trusted = true;
	if (cleanTop)
		result.bottom = opposite;
	else
		result.top = opposite;
	result.trustedBounds = { 0, inferredTop, source.width, inferredBottom,
		source.width, source.height,
		static_cast<double>(source.width) / (inferredBottom - inferredTop),
		ActivePictureBounds::BarAxes::TOP_BOTTOM };
	result.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
	result.lumaSamples += samples.lumaSamples;
	result.chromaSamples += samples.chromaSamples;
	result.reason =
		"one clean bar plus overlay-compatible opposite bar supports symmetric startup geometry";
	return result;
}

ActivePictureEvidence ExtractP010ActivePictureEvidence(
	const P010PlaneView& view)
{
	AnalysisLumaSource source;
	source.data = view.data;
	source.dataBytes = view.dataBytes;
	source.width = view.width;
	source.height = view.height;
	source.rowBytes = view.lumaPitchBytes;
	source.chromaRowBytes = view.chromaPitchBytes;
	source.format = AnalysisLumaFormat::P010;
	return ExtractActivePictureEvidence(source);
}


ActivePictureGlobalNearBlackEvidence EvaluateActivePictureGlobalNearBlack(
	const AnalysisLumaSource& source)
{
	ActivePictureGlobalNearBlackEvidence result;
	if (!source.IsValid() || source.width < 16 || source.height < 16)
		return result;

	result.evaluated = true;
	SampleContext samples{ source };
	std::vector<int> globalLuma;
	globalLuma.reserve(kGlobalGridWidth * kGlobalGridHeight);
	for (int row = 0; row < kGlobalGridHeight; ++row)
	{
		const int y = ((row * 2 + 1) * source.height) /
			(kGlobalGridHeight * 2);
		for (int column = 0; column < kGlobalGridWidth; ++column)
		{
			const int x = ((column * 2 + 1) * source.width) /
				(kGlobalGridWidth * 2);
			globalLuma.push_back(samples.Luma(x, y));
		}
	}
	result.lumaP90 = Percentile(globalLuma, 0.90);
	result.nearBlack = result.lumaP90 <= kGlobalNearBlackP90;
	result.lumaSamples = samples.lumaSamples;
	return result;
}


ActivePictureGlobalNearBlackEvidence EvaluateP010ActivePictureGlobalNearBlack(
	const P010PlaneView& view)
{
	AnalysisLumaSource source;
	source.data = view.data;
	source.dataBytes = view.dataBytes;
	source.width = view.width;
	source.height = view.height;
	source.rowBytes = view.lumaPitchBytes;
	source.chromaRowBytes = view.chromaPitchBytes;
	source.format = AnalysisLumaFormat::P010;
	return EvaluateActivePictureGlobalNearBlack(source);
}


ActivePicturePresentationRetentionEvidence EvaluateActivePicturePresentationRetention(
	const AnalysisLumaSource& source,
	const ActivePictureBounds& trustedPresentation)
{
	ActivePicturePresentationRetentionEvidence result;
	result.activePicture = ExtractActivePictureEvidence(source);
	result.lumaSamples = result.activePicture.lumaSamples;
	result.chromaSamples = result.activePicture.chromaSamples;
	if (!source.IsValid() || source.width < 16 || source.height < 16)
	{
		result.reason = "invalid analysis source cannot prove presentation safety";
		return result;
	}
	result.analysisValid = true;
	if (!IsValidBoundsForSource(trustedPresentation, source))
	{
		result.reason = "trusted presentation does not match the analysis raster";
		return result;
	}
	result.presentationValid = true;

	const ActivePictureGlobalNearBlackEvidence global =
		EvaluateActivePictureGlobalNearBlack(source);
	result.globalLumaP90 = global.lumaP90;
	result.globalNearBlack = global.nearBlack;
	result.lumaSamples += global.lumaSamples;

	SampleContext samples{ source };

	std::vector<int> perimeter;
	perimeter.reserve(256);
	for (int i = 0; i < 64; ++i)
	{
		const int x = ((i * 2 + 1) * source.width) / 128;
		const int y = ((i * 2 + 1) * source.height) / 128;
		perimeter.push_back(samples.Luma(x, 0));
		perimeter.push_back(samples.Luma(x, source.height - 1));
		perimeter.push_back(samples.Luma(0, y));
		perimeter.push_back(samples.Luma(source.width - 1, y));
	}
	const int observedLow = static_cast<int>(Percentile(perimeter, 0.10));
	const int blackFloor = observedLow < 32 ? 0 :
		Bounded(observedLow, 48, 80);
	const int blackThreshold = std::min(104, blackFloor + 24);

	result.excludedTop = InspectHorizontalEdge(samples, true,
		trustedPresentation.top, trustedPresentation.top,
		blackFloor, blackThreshold);
	result.excludedBottom = InspectHorizontalEdge(samples, false,
		source.height - trustedPresentation.bottom,
		trustedPresentation.bottom, blackFloor, blackThreshold);
	result.excludedLeft = InspectVerticalEdge(samples, true,
		trustedPresentation.left, trustedPresentation.left,
		blackFloor, blackThreshold);
	result.excludedRight = InspectVerticalEdge(samples, false,
		source.width - trustedPresentation.right,
		trustedPresentation.right, blackFloor, blackThreshold);

	const auto topExtent = FindHorizontalVisibleExtent(samples, true,
		trustedPresentation.top, blackThreshold);
	const auto bottomExtent = FindHorizontalVisibleExtent(samples, false,
		source.height - trustedPresentation.bottom, blackThreshold);
	const auto leftExtent = FindVerticalVisibleExtent(samples, true,
		trustedPresentation.left, blackThreshold);
	const auto rightExtent = FindVerticalVisibleExtent(samples, false,
		source.width - trustedPresentation.right, blackThreshold);
	const bool unsafeTop = !ExcludedBandPixelsAreSafe(result.excludedTop) ||
		topExtent.available;
	const bool unsafeBottom = !ExcludedBandPixelsAreSafe(result.excludedBottom) ||
		bottomExtent.available;
	const bool unsafeLeft = !ExcludedBandPixelsAreSafe(result.excludedLeft) ||
		leftExtent.available;
	const bool unsafeRight = !ExcludedBandPixelsAreSafe(result.excludedRight) ||
		rightExtent.available;
	result.excludedBandsPixelSafe =
		!unsafeLeft && !unsafeTop && !unsafeRight && !unsafeBottom;
	if (!result.excludedBandsPixelSafe)
	{
		// Every unsafe edge must be bounded. Otherwise fail open exactly as
		// before; a partial estimate must never hide unmeasured live pixels.
		const bool allUnsafeEdgesBounded = (!unsafeTop || topExtent.available) &&
			(!unsafeBottom || bottomExtent.available) &&
			(!unsafeLeft || leftExtent.available) &&
			(!unsafeRight || rightExtent.available);
		if (allUnsafeEdgesBounded)
		{
			const int verticalMargin = std::max(2, source.height / 180);
			const int horizontalMargin = std::max(2, source.width / 180);
			result.outwardVisibleBounds = trustedPresentation;
			if (unsafeTop)
				result.outwardVisibleBounds.top = std::max(
					0, topExtent.coordinate - verticalMargin);
			if (unsafeBottom)
				result.outwardVisibleBounds.bottom = std::min(source.height,
					bottomExtent.coordinate + verticalMargin);
			if (unsafeLeft)
				result.outwardVisibleBounds.left = std::max(
					0, leftExtent.coordinate - horizontalMargin);
			if (unsafeRight)
				result.outwardVisibleBounds.right = std::min(source.width,
					rightExtent.coordinate + horizontalMargin);
			result.outwardVisibleBounds.aspectRatio = static_cast<double>(
				result.outwardVisibleBounds.right - result.outwardVisibleBounds.left) /
				std::max(1, result.outwardVisibleBounds.bottom -
					result.outwardVisibleBounds.top);
			result.outwardVisibleBounds.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			result.outwardVisibleBoundsAvailable = true;
		}
	}
	result.proposedBoundsAvailable = result.activePicture.available &&
		IsValidBoundsForSource(result.activePicture.proposedBounds, source);
	result.proposedBoundsContained = result.proposedBoundsAvailable &&
		Contains(trustedPresentation, result.activePicture.proposedBounds);
	result.currentlyPixelSafe = result.excludedBandsPixelSafe &&
		(result.proposedBoundsContained || result.globalNearBlack);
	result.lumaSamples += samples.lumaSamples;
	result.chromaSamples += samples.chromaSamples;

	if (result.outwardVisibleBoundsAvailable)
		result.reason = "bounded visible excluded-band content requires outward fit";
	else if (!result.excludedBandsPixelSafe)
		result.reason = "visible, textured, or colored excluded-band pixels reject retention";
	else if (result.proposedBoundsContained)
		result.reason = "current proposal is contained and excluded bands remain pixel-safe";
	else if (result.globalNearBlack)
		result.reason = "valid global near-black frame is pixel-safe without geometry";
	else
		result.reason = "non-contained active-picture evidence rejects retention";
	return result;
}


ActivePicturePresentationRetentionEvidence
	EvaluateP010ActivePicturePresentationRetention(
		const P010PlaneView& view,
		const ActivePictureBounds& trustedPresentation)
{
	AnalysisLumaSource source;
	source.data = view.data;
	source.dataBytes = view.dataBytes;
	source.width = view.width;
	source.height = view.height;
	source.rowBytes = view.lumaPitchBytes;
	source.chromaRowBytes = view.chromaPitchBytes;
	source.format = AnalysisLumaFormat::P010;
	return EvaluateActivePicturePresentationRetention(source,
		trustedPresentation);
}


ActivePictureEvidence ConstrainNearBlackGeometryChange(
	const ActivePicturePresentationRetentionEvidence& retention,
	const ActivePictureBounds& trustedPresentation)
{
	ActivePictureEvidence evidence = retention.activePicture;
	if (!retention.analysisValid || !retention.presentationValid ||
		!retention.globalNearBlack || !evidence.available ||
		evidence.classification == ActivePictureClassification::UNAVAILABLE ||
		evidence.classification == ActivePictureClassification::PROVISIONAL)
	{
		return evidence;
	}

	const ActivePictureBounds& observed = evidence.trustedBounds;
	const bool samePresentation =
		observed.left == trustedPresentation.left &&
		observed.top == trustedPresentation.top &&
		observed.right == trustedPresentation.right &&
		observed.bottom == trustedPresentation.bottom &&
		observed.rasterWidth == trustedPresentation.rasterWidth &&
		observed.rasterHeight == trustedPresentation.rasterHeight;
	if (samePresentation)
		return evidence;

	evidence.classification = ActivePictureClassification::PROVISIONAL;
	evidence.proposedBounds = observed;
	evidence.reason =
		"near-black frame cannot replace retained presentation geometry";
	return evidence;
}


ActivePictureEvidence ConstrainNearBlackCropAcquisition(
	const ActivePictureEvidence& observed,
	bool nearBlackEpisodeActive)
{
	ActivePictureEvidence evidence = observed;
	if (!nearBlackEpisodeActive || !evidence.available ||
		evidence.classification !=
			ActivePictureClassification::BAR_CROP_TRUSTED)
	{
		return evidence;
	}

	evidence.classification = ActivePictureClassification::PROVISIONAL;
	evidence.proposedBounds = evidence.trustedBounds;
	evidence.reason =
		"near-black title episode cannot acquire bar-crop authority";
	return evidence;
}
