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

struct EdgeScan
{
	bool complete = false;
	bool outerContentSupported = false;
};
bool ScanBlackLine(SampleContext& samples, bool row, int coordinate, int threshold,
	bool& contentSupported)
{
	int black = 0;
	int supported[4] = {};
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int position = ((i * 2 + 1) * (row ? samples.source.width : samples.source.height)) /
			(kLineSamples * 2);
		const int luma = row ? samples.Luma(position, coordinate) : samples.Luma(coordinate, position);
		black += luma <= threshold;
		supported[i / 12] += luma > threshold + 24;
	}
	// Diagnostic certificate only: distributed support in both opposing outer
	// lines, not the few bright pixels sufficient to stop a black-line search.
	contentSupported = supported[0] >= 6 && supported[1] >= 6 && supported[2] >= 6 && supported[3] >= 6;
	return black >= 44;
}

ActivePictureEdgeEvidence InspectHorizontalEdge(SampleContext& samples, bool top,
	int barPixels, int boundary, int blackFloor, int blackThreshold, int outerOffset = 0)
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
		const int y = top ? outerOffset + depth : samples.source.height - 1 - outerOffset - depth;
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
	int barPixels, int boundary, int blackFloor, int blackThreshold, int outerOffset = 0)
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
		const int x = left ? outerOffset + depth : samples.source.width - 1 - outerOffset - depth;
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

// A provisional vertical edge may stop one coarse scan step early. This can
// retain only an existing opposing-bar crop; it cannot establish new geometry,
// change width, or excuse a material format change.
bool IsVerticalSamplingProposal(const ActivePictureBounds& base,
	const ActivePictureBounds& proposed)
{
	const auto axes=static_cast<unsigned>(base.trustedBarAxes);
	if ((axes & static_cast<unsigned>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0 ||
		base.top <= 0 || base.bottom >= base.rasterHeight ||
		proposed.left != base.left || proposed.right != base.right)
		return false;
	const int step=std::max(2,base.rasterHeight / 540); // Same vertical grid as acquisition.
	return std::abs(proposed.top-base.top) <= step &&
		std::abs(proposed.bottom-base.bottom) <= step &&
		std::abs((proposed.bottom-proposed.top)-(base.bottom-base.top)) <= step;
}

bool SamplingExpansionPixelsAreSafe(SampleContext& samples,
	const ActivePictureBounds& base, const ActivePictureBounds& proposed,
	int blackThreshold, int& peakY, int& peakChromaDelta)
{
	// Whole-bar sampling can miss a thin bright or colored strip. Inspect each
	// disputed row at the existing visible-extent horizontal density, using the
	// same credible-luma cutoff and retained-bar chroma tolerance. At 4K this
	// costs at most 1,024 samples (the aggregate height delta is one scan step).
	auto safeRow=[&](int y) {
		bool rowSafe = true;
		for (int i=0;i<kVisibleExtentLineSamples;++i)
		{
			const int x=((i*2+1)*samples.source.width)/(kVisibleExtentLineSamples*2);
			AnalysisLumaSample pixel;
			if (!samples.source.Sample(x,y,pixel)) return false;
			++samples.lumaSamples; ++samples.chromaSamples;
			const int chromaDelta = std::max(std::abs(int(pixel.chromaU)-512),
				std::abs(int(pixel.chromaV)-512));
			peakY = std::max(peakY,int(pixel.luma));
			peakChromaDelta = std::max(peakChromaDelta,chromaDelta);
			if (pixel.luma > blackThreshold+8 || chromaDelta > 32) rowSafe = false;
		}
		return rowSafe;
	};
	bool safe = true;
	for (int y=proposed.top;y<base.top;++y) safe = safeRow(y) && safe;
	for (int y=base.bottom;y<proposed.bottom;++y) safe = safeRow(y) && safe;
	return safe;
}

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


bool CanRetainProvisionalSamplingCrop(const ActivePictureBounds& trusted,
	const ActivePictureBounds& observed, ActivePictureClassification classification,
	bool currentPresentationRetainable)
{
	// This consumes current retention eligibility, including explicit edge tolerance.
	// It never promotes provisional geometry to new format authority. Callers
	// must also bind that proof to the exact retained base and source sample.
	AnalysisLumaSource raster;
	raster.width = trusted.rasterWidth;
	raster.height = trusted.rasterHeight;
	return currentPresentationRetainable &&
		classification == ActivePictureClassification::PROVISIONAL &&
		IsValidBoundsForSource(trusted,raster) &&
		IsValidBoundsForSource(observed,raster) &&
		IsVerticalSamplingProposal(trusted,observed);
}

const char* ActivePictureAxisStateName(ActivePictureAxisState state)
{
	switch (state) {
	case ActivePictureAxisState::TRUSTED_BARS: return "trusted-bars";
	case ActivePictureAxisState::FULL_EXTENT_SUPPORTED: return "full-extent-supported";
	default: return "unknown";
	}
}
const char* ActivePictureAxisReasonName(ActivePictureAxisReason reason)
{
	switch (reason) {
	case ActivePictureAxisReason::SCAN_INCOMPLETE: return "scan-incomplete";
	case ActivePictureAxisReason::BAR_EDGE_REJECTED: return "bar-edge-rejected";
	case ActivePictureAxisReason::BAR_ASYMMETRY: return "bar-asymmetry";
	case ActivePictureAxisReason::BAR_CONFIRMED: return "bar-confirmed";
	case ActivePictureAxisReason::FULL_EXTENT_SUPPORTED: return "distributed-edge-content";
	case ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT: return "no-full-extent-support";
	default: return "not-evaluated";
	}
}
ActivePictureObservation MakeActivePictureObservation(const ActivePictureEvidence& evidence,
	uint64_t frameNumber, double framesPerSecond)
{
	ActivePictureObservation observation;
	observation.frameNumber = frameNumber;
	observation.framesPerSecond = framesPerSecond;
	observation.available = evidence.available;
	observation.classification = evidence.classification;
	observation.bounds = evidence.classification == ActivePictureClassification::PROVISIONAL
		? evidence.proposedBounds : evidence.trustedBounds;
	observation.axisEvidence = evidence.axisEvidence;
	return observation;
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
	EdgeScan topScan, bottomScan, leftScan, rightScan;
	auto blackLine = [&](bool row, int coordinate, EdgeScan& scan)
	{
		if (scanLinesRemaining <= 0) return false;
		--scanLinesRemaining;
		bool supported = false;
		const bool black = ScanBlackLine(samples, row, coordinate, blackThreshold, supported);
		if (coordinate == 0 || coordinate == (row ? source.height : source.width) - 1)
			scan.outerContentSupported = supported;
		if (!black) scan.complete = true;
		return black;
	};
	int top = 0;
	while (top + yStep < source.height / 2 &&
		blackLine(true, top, topScan))
		top += yStep;
	int bottom = source.height;
	while (bottom - yStep > source.height / 2 &&
		blackLine(true, bottom - 1, bottomScan))
		bottom -= yStep;
	int left = 0;
	while (left + xStep < source.width / 2 &&
		blackLine(false, left, leftScan))
		left += xStep;
	int right = source.width;
	while (right - xStep > source.width / 2 &&
		blackLine(false, right - 1, rightScan))
		right -= xStep;

	auto measuredAxis = [](const EdgeScan& first, const EdgeScan& last, int before, int after, int step) {
		ActivePictureAxisEvidence axis;
		axis.scanComplete = first.complete && last.complete;
		axis.barCandidate = before > step * 2 || after > step * 2;
		axis.reason = !axis.scanComplete ? ActivePictureAxisReason::SCAN_INCOMPLETE :
			axis.barCandidate ? ActivePictureAxisReason::BAR_EDGE_REJECTED : ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT;
		if (axis.scanComplete && before == 0 && after == 0 &&
			first.outerContentSupported && last.outerContentSupported)
		{
			axis.state = ActivePictureAxisState::FULL_EXTENT_SUPPORTED;
			axis.reason = ActivePictureAxisReason::FULL_EXTENT_SUPPORTED;
		}
		return axis;
	};
	result.axisEvidence.horizontal = measuredAxis(leftScan, rightScan, left, source.width-right, xStep);
	result.axisEvidence.vertical = measuredAxis(topScan, bottomScan, top, source.height-bottom, yStep);

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
	auto finishAxis = [](ActivePictureAxisEvidence& axis, bool trusted,
		bool bothEdgesTrusted) {
		if (!axis.scanComplete || !axis.barCandidate) return;
		if (trusted) {
			axis.state = ActivePictureAxisState::TRUSTED_BARS;
			axis.reason = ActivePictureAxisReason::BAR_CONFIRMED;
		} else if (bothEdgesTrusted) axis.reason = ActivePictureAxisReason::BAR_ASYMMETRY;
	};
	finishAxis(result.axisEvidence.vertical, verticalTrusted, result.top.trusted && result.bottom.trusted);
	finishAxis(result.axisEvidence.horizontal, horizontalTrusted, result.left.trusted && result.right.trusted);
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

	const auto& candidate = result.activePicture.classification == ActivePictureClassification::PROVISIONAL
		? result.activePicture.proposedBounds : result.activePicture.trustedBounds;
	if (result.activePicture.available && IsValidBoundsForSource(candidate, source))
	{
		result.expansionStripsAvailable = true;
		result.expansionBase = trustedPresentation;
		result.expansionCandidate = candidate;
		result.expandingTop = InspectHorizontalEdge(samples, true,
			trustedPresentation.top - candidate.top, trustedPresentation.top,
			blackFloor, blackThreshold, candidate.top);
		result.expandingBottom = InspectHorizontalEdge(samples, false,
			candidate.bottom - trustedPresentation.bottom, trustedPresentation.bottom,
			blackFloor, blackThreshold, source.height - candidate.bottom);
		result.expandingLeft = InspectVerticalEdge(samples, true,
			trustedPresentation.left - candidate.left, trustedPresentation.left,
			blackFloor, blackThreshold, candidate.left);
		result.expandingRight = InspectVerticalEdge(samples, false,
			candidate.right - trustedPresentation.right, trustedPresentation.right,
			blackFloor, blackThreshold, source.width - candidate.right);
	}

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
	result.excludedHorizontalBandsPixelSafe = !unsafeLeft && !unsafeRight;
	result.excludedVerticalBandsPixelSafe = !unsafeTop && !unsafeBottom;
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
	// Provisional one-step vertical jitter is presentation evidence, not new
	// aspect authority. Broad current bands must remain safe. Disputed-row
	// pixels are reported separately: edge tolerance is not proof of blackness.
	if (result.excludedBandsPixelSafe && !result.proposedBoundsContained &&
		result.proposedBoundsAvailable && !result.globalNearBlack &&
		result.activePicture.classification == ActivePictureClassification::PROVISIONAL &&
		IsVerticalSamplingProposal(trustedPresentation,result.activePicture.proposedBounds))
	{
		result.samplingEquivalent = true;
		result.samplingReaffirmed=SamplingExpansionPixelsAreSafe(samples,
			trustedPresentation,result.activePicture.proposedBounds,blackThreshold,
			result.samplingStripPeakY,result.samplingStripPeakChromaDelta);
		result.samplingStripConflict=!result.samplingReaffirmed;
	}
	// Acquisition may also be unavailable on a logo/title or exhaust its scan
	// budget. Retain only the existing rectangle when current bands are safe.
	// Other available conflicting proposals still veto retention, except for
	// the established global-near-black rule.
	const bool geometryUnavailable = !result.activePicture.available &&
		result.activePicture.classification == ActivePictureClassification::UNAVAILABLE;
	result.currentlyPixelSafe = result.excludedBandsPixelSafe &&
		(result.proposedBoundsContained || result.samplingReaffirmed ||
		 result.globalNearBlack || geometryUnavailable);
	result.lumaSamples += samples.lumaSamples;
	result.chromaSamples += samples.chromaSamples;

	if (result.outwardVisibleBoundsAvailable)
		result.reason = "bounded visible excluded-band content requires outward fit";
	else if (!result.excludedBandsPixelSafe)
		result.reason = "visible, textured, or colored excluded-band pixels reject retention";
	else if (result.proposedBoundsContained)
		result.reason = "current proposal is contained and excluded bands remain pixel-safe";
	else if (result.samplingReaffirmed)
		result.reason = "one-scan-step provisional edge retained after current strip pixel proof";
	else if (result.samplingStripConflict)
		result.reason = "one-scan-step border content tolerated within established framing";
	else if (result.globalNearBlack)
		result.reason = "valid global near-black frame is pixel-safe without geometry";
	else if (geometryUnavailable)
		result.reason = "current excluded bands remain pixel-safe despite unavailable geometry";
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

ActivePictureRetentionHandoff ResolveActivePictureRetentionHandoff(
	const AnalysisLumaSource& source, const ActivePictureBounds& measuredBounds,
	const ActivePicturePresentationRetentionEvidence& measured,
	const ActivePictureBounds& presentationBounds)
{
	const bool sameBounds = measuredBounds.left == presentationBounds.left &&
		measuredBounds.top == presentationBounds.top && measuredBounds.right == presentationBounds.right &&
		measuredBounds.bottom == presentationBounds.bottom && measuredBounds.rasterWidth == presentationBounds.rasterWidth &&
		measuredBounds.rasterHeight == presentationBounds.rasterHeight;
	if (source.IsValid() && measured.analysisValid && measured.presentationValid && sameBounds)
		return {measured, measuredBounds, false};
	// A transition changes the rectangle, not the evidence already measured.
	// Inspect the new base against these same source bytes before publishing it
	// as the presentation certificate. Invalid sources remain non-authoritative.
	return {EvaluateActivePicturePresentationRetention(source, presentationBounds), presentationBounds, true};
}

ActivePictureDiagnosticGrid SampleActivePictureDiagnosticGrid(
	const AnalysisLumaSource& source)
{
	ActivePictureDiagnosticGrid result;
	if (!source.IsValid() || source.width < 2 || source.height < 2)
		return result;
	result.columns = std::min(source.width, 128);
	result.rows = std::min(source.height, 270);
	result.samples.reserve(static_cast<size_t>(result.columns) * result.rows);
	for (int row = 0; row < result.rows; ++row)
	{
		const int y = static_cast<int>(static_cast<int64_t>(row) *
			(source.height - 1) / (result.rows - 1));
		for (int column = 0; column < result.columns; ++column)
		{
			const int x = static_cast<int>(static_cast<int64_t>(column) *
				(source.width - 1) / (result.columns - 1));
			AnalysisLumaSample sample;
			if (!source.Sample(x, y, sample))
				return {};
			result.samples.push_back(sample);
		}
	}
	return result;
}


FullRasterColorEvidence EvaluateFullRasterColorEvidence(
    const AnalysisLumaSource& source)
{
    FullRasterColorEvidence result;
    if (!source.IsValid() || source.width < 128 || source.height < 128)
    {
        result.reason = "invalid or undersized color corroboration source";
        return result;
    }
    // An 8-bit input expanded into a 10-bit plane has not acquired precision.
    // Unknown provenance also abstains: a plane format alone proves nothing
    // about the precision of the original capture.
    switch (source.encoding)
    {
    case VideoFrameEncoding::V210:
    case VideoFrameEncoding::R210:
    case VideoFrameEncoding::R10b:
    case VideoFrameEncoding::R10l:
    case VideoFrameEncoding::R12B:
    case VideoFrameEncoding::R12L:
        result.precisionSupported = true;
        break;
    default:
        result.reason = "source precision insufficient or unknown for subtle color evidence";
        return result;
    }
    constexpr int depths = 6;
    constexpr int positions = 96;
    constexpr int cells = 4;
    bool allEdges = true;
    for (int edge = 0; edge < 4; ++edge)
    {
        const bool vertical = edge == 0 || edge == 2;
        const bool reverse = edge >= 2;
        const int across = vertical ? source.height : source.width;
        const int extent = vertical ? source.width : source.height;
        std::vector<int> outer[3], inner[3];
        std::vector<int> outerCell[cells][3], innerCell[cells][3];
        for (int channel = 0; channel < 3; ++channel)
        {
            outer[channel].reserve(depths * positions);
            inner[channel].reserve(depths * positions);
            for (int cell = 0; cell < cells; ++cell)
            {
                outerCell[cell][channel].reserve(depths * positions / cells);
                innerCell[cell][channel].reserve(depths * positions / cells);
            }
        }
        for (int d = 0; d < depths; ++d)
        {
            // Include the actual outermost line and a shallow band. Paired
            // interior samples are well beyond typical thin scope bars.
            const int depth = d * std::max(1, extent / 32) / (depths - 1);
            for (int i = 0; i < positions; ++i)
            {
                const int position = (2 * i + 1) * across / (2 * positions);
                for (int inside = 0; inside < 2; ++inside)
                {
                    const int fromEdge = depth + (inside ? extent / 8 : 0);
                    const int coordinate = reverse ? extent - 1 - fromEdge : fromEdge;
                    AnalysisLumaSample pixel;
                    if (!source.Sample(vertical ? coordinate : position,
                        vertical ? position : coordinate, pixel))
                    {
                        result.reason = "color corroboration sample unavailable";
                        return result;
                    }
                    ++result.sampleCount;
                    const int values[] = { pixel.luma, pixel.chromaU, pixel.chromaV };
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        (inside ? inner[channel] : outer[channel]).push_back(values[channel]);
                        (inside ? innerCell[i / 24][channel] : outerCell[i / 24][channel]).push_back(values[channel]);
                    }
                }
            }
        }
        auto& evidence = result.edges[edge];
        evidence.medianY = Percentile(outer[0], 0.5);
        evidence.medianU = Percentile(outer[1], 0.5);
        evidence.medianV = Percentile(outer[2], 0.5);
        evidence.dispersionY = Percentile(outer[0], 0.90) - Percentile(outer[0], 0.10);
        evidence.dispersionU = Percentile(outer[1], 0.90) - Percentile(outer[1], 0.10);
        evidence.dispersionV = Percentile(outer[2], 0.90) - Percentile(outer[2], 0.10);
        evidence.interiorMedianY = Percentile(inner[0], 0.5);
        evidence.interiorMedianU = Percentile(inner[1], 0.5);
        evidence.interiorMedianV = Percentile(inner[2], 0.5);
        for (int cell = 0; cell < cells; ++cell)
        {
            const double spread = Percentile(outerCell[cell][0], 0.90) -
                Percentile(outerCell[cell][0], 0.10);
            evidence.supportedCells += spread >= 3.0;
            for (int channel = 0; channel < 3; ++channel)
            {
                const double delta = std::abs(Percentile(outerCell[cell][channel], 0.5) -
                    Percentile(innerCell[cell][channel], 0.5));
                auto& maximum = channel == 0 ? evidence.maxBackgroundDeltaY : evidence.maxBackgroundDeltaUV;
                maximum = std::max(maximum, delta);
            }
        }
        const bool tinted = std::max(std::abs(evidence.medianU - 512.0),
            std::abs(evidence.medianV - 512.0)) >= 2.0;
        // This weak pattern is deliberately diagnostic only. Tinted noise
        // with no distinguishable boundary remains ambiguous; a positive
        // candidate MUST NOT authorize either retention or acquisition.
        evidence.candidateSupported = tinted && evidence.dispersionY >= 4.0 &&
            evidence.supportedCells >= 3 && evidence.maxBackgroundDeltaY <= 8.0 &&
            evidence.maxBackgroundDeltaUV <= 3.0;
        allEdges = allEdges && evidence.candidateSupported;
    }
    result.evaluated = true;
    result.candidateSupported = allEdges;
    result.reason = allEdges ? "weak full-raster color pattern; indistinguishable noisy bars remain possible" :
        "insufficient color detail or continuity for diagnostic full-raster pattern";
    return result;
}
