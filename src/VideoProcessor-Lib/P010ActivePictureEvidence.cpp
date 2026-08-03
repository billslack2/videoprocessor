#include "pch.h"

#include "P010ActivePictureEvidence.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>


namespace
{
constexpr int kLineSamples = 48;
constexpr int kEdgeDepthSamples = 6;

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

P010EdgeEvidence InspectHorizontalEdge(SampleContext& samples, bool top,
	int barPixels, int boundary, int blackFloor, int blackThreshold)
{
	P010EdgeEvidence evidence;
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

P010EdgeEvidence InspectVerticalEdge(SampleContext& samples, bool left,
	int barPixels, int boundary, int blackFloor, int blackThreshold)
{
	P010EdgeEvidence evidence;
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
}


P010ActivePictureEvidence ExtractActivePictureEvidence(
	const AnalysisLumaSource& source)
{
	P010ActivePictureEvidence result;
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
		source.height, proposedAspect, false };
	result.trustedBounds = { 0, 0, source.width, source.height, source.width,
		source.height, static_cast<double>(source.width) / source.height, true };
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
	result.trustedBounds.symmetricBars =
		verticalTrusted || horizontalTrusted;
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

P010ActivePictureEvidence ExtractP010ActivePictureEvidence(
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
