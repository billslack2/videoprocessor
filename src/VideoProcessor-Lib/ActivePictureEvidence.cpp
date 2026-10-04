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
	// Distributed broad-picture support is separate from the few bright pixels
	// sufficient to stop a black-line search. It also vetoes conflicting symmetry.
	contentSupported = supported[0] >= 6 && supported[1] >= 6 && supported[2] >= 6 && supported[3] >= 6;
	return black >= 44;
}

// An edge is positive picture evidence only when support is distributed over
// the verified picture height and continues into the image. This rejects a
// corner logo, sparse stars/credits, or a thin bright border at the raster edge.
int InspectSidePicture(SampleContext& samples, bool left, int top, int bottom, int threshold,
	ActivePictureSideProbe& probe)
{
	probe = {};
	probe.evaluated = true;
	int minimum = 12;
	for (int depth = 0; depth < 3; ++depth)
	{
		const int inset = depth * std::max(1, samples.source.width / 128);
		const int x = left ? inset : samples.source.width - 1 - inset;
		for (int zone = 0; zone < 4; ++zone)
		{
			int bright = 0;
			auto& cell = probe.cells[depth * 4 + zone];
			int sum = 0;
			for (int i = 0; i < 12; ++i)
			{
				const int y = top + ((2 * (zone * 12 + i) + 1) * (bottom - top)) / 96;
				const int luma = samples.Luma(x, y);
				bright += luma > threshold;
				cell.nonBlack += luma > threshold - 24;
				cell.peakLuma = std::max(cell.peakLuma, luma);
				sum += luma;
			}
			cell.strong = bright;
			cell.meanLuma = sum / 12;
			minimum = std::min(minimum, bright);
		}
	}
	return minimum;
}

void SummarizeSidePicture(const ActivePictureSideProbe& probe,
	uint8_t& strongZoneMask, int& nonBlackMinimum)
{
	strongZoneMask = 0;
	nonBlackMinimum = -1;
	if (!probe.evaluated) return;
	strongZoneMask = 0xf;
	nonBlackMinimum = 12;
	for (int depth = 0; depth < 3; ++depth)
		for (int zone = 0; zone < 4; ++zone)
		{
			const auto& cell = probe.cells[depth * 4 + zone];
			if (cell.strong < 6)
				strongZoneMask &= static_cast<uint8_t>(~(1u << zone));
			nonBlackMinimum = std::min(nonBlackMinimum, cell.nonBlack);
		}
}

ActivePictureVerticalBarProfile InspectVerticalCropBarProfile(
	SampleContext& samples, int top, int bottom)
{
	ActivePictureVerticalBarProfile result;
	result.evaluated = true;
	result.apertureTop = top;
	result.apertureBottom = bottom;
	result.reason = "reference-sampling-failed";
	const auto& source = samples.source;
	constexpr int columns = 96;
	constexpr int adjacentRows = 8;
	constexpr int tolerance = 4;
	const auto xAt = [&](int i) { return i * (source.width - 1) / (columns - 1); };
	const auto sampleAt = [&](int x, int y, AnalysisLumaSample& value) {
		if (!source.Sample(x, y, value)) return false;
		++samples.lumaSamples;
		++samples.chromaSamples;
		++result.samples;
		return true;
	};
	// The independently verified top bar defines the current reference. Prefer
	// its outer portion, away from the picture; the full bars are checked below.
	std::vector<int> ys, us, vs;
	ys.reserve(8 * columns); us.reserve(8 * columns); vs.reserve(8 * columns);
	std::vector<int> referenceRows;
	for (int d = 0; d < 8; ++d)
	{
		const int y = (2 * d + 1) * std::max(1, top - adjacentRows) / 16;
		referenceRows.push_back(y);
		for (int i = 0; i < columns; ++i)
		{
			AnalysisLumaSample value;
			if (!sampleAt(xAt(i), y, value)) return result;
			ys.push_back(value.luma); us.push_back(value.chromaU); vs.push_back(value.chromaV);
		}
	}
	const int yMedian = static_cast<int>(Percentile(ys, .5));
	const int uMedian = static_cast<int>(Percentile(us, .5));
	const int vMedian = static_cast<int>(Percentile(vs, .5));
	result.referenceY = yMedian; result.referenceU = uMedian; result.referenceV = vMedian;
	result.reason = "reference-not-clean";
	if (yMedian > 80 || std::abs(uMedian - 512) > 8 || std::abs(vMedian - 512) > 8 ||
		Percentile(ys, .9) - Percentile(ys, .1) > tolerance ||
		Percentile(us, .9) - Percentile(us, .1) > tolerance ||
		Percentile(vs, .9) - Percentile(vs, .1) > tolerance)
		return result;
	// Inspect both complete prospective bars, including all eight immediately
	// excluded rows and raster-edge rows. The deeper cadence matches the dense
	// detector. This is bounded sampling, not proof about every source pixel.
	result.reason = "excluded-band-sampling-failed";
	for (bool upper : { true, false })
	{
		const int first = upper ? 0 : bottom;
		const int last = upper ? top : source.height;
		std::vector<int> rows;
		if (upper) rows = referenceRows;
		for (int d = 0; d < std::min(adjacentRows, last - first); ++d)
		{
			rows.push_back(first + d);
			rows.push_back(last - 1 - d);
		}
		for (int y = first; y < last; y += std::max(1, source.height / 540))
			rows.push_back(y);
		std::sort(rows.begin(), rows.end());
		rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
		for (int y : rows)
			for (int i = 0; i < columns; ++i)
			{
				AnalysisLumaSample value;
				const int x = xAt(i);
				if (!sampleAt(x, y, value)) return result;
				const int dy = std::abs(static_cast<int>(value.luma) - yMedian);
				const int duv = std::max(std::abs(static_cast<int>(value.chromaU) - uMedian),
					std::abs(static_cast<int>(value.chromaV) - vMedian));
				result.maxLumaDelta = std::max(result.maxLumaDelta, dy);
				result.maxChromaDelta = std::max(result.maxChromaDelta, duv);
				if (dy > tolerance || duv > tolerance)
				{
					++result.mismatchSamples;
					const int boundaryRows = std::max(2, source.height / 540);
					const bool adjacent = upper ? y >= top - boundaryRows : y < bottom + boundaryRows;
					if (adjacent) ++result.boundaryMismatchSamples;
					else ++result.deepMismatchSamples;
					auto& firstY = upper ? result.topFirstMismatchY : result.bottomFirstMismatchY;
					auto& lastY = upper ? result.topLastMismatchY : result.bottomLastMismatchY;
					if (firstY < 0) firstY = y;
					lastY = y;
					if (result.firstMismatchY < 0)
					{
						result.firstMismatchX = x;
						result.firstMismatchY = y;
					}
				}
			}
	}
	result.completed = true;
	result.clean = result.firstMismatchY < 0;
	result.reason = result.clean ? "excluded-bands-clean" : "excluded-band-profile-mismatch";
	return result;
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

using ExcludedBandVisibleExtent = ActivePictureVisibleExtentDiagnostic;

// Each provisional vertical edge may stop one coarse scan step early. This can
// retain only an existing opposing-bar crop; it cannot establish new geometry,
// change width, or excuse a material format change. Contained side proposals
// describe pixels already inside the retained crop, not an outward conflict.
bool IsVerticalSamplingProposal(const ActivePictureBounds& base,
	const ActivePictureBounds& proposed)
{
	const auto axes=static_cast<unsigned>(base.trustedBarAxes);
	if ((axes & static_cast<unsigned>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0 ||
		base.top <= 0 || base.bottom >= base.rasterHeight ||
		proposed.left < base.left || proposed.right > base.right)
		return false;
	const int step=std::max(2,base.rasterHeight / 540); // Same vertical grid as acquisition.
	return std::abs(proposed.top-base.top) <= step &&
		std::abs(proposed.bottom-base.bottom) <= step;
}

// Inward uncertainty is inside the retained picture. Only the newly excluded
// rows need additional proof; this shape alone never grants retention.
bool IsPartialVerticalSamplingProposal(const ActivePictureBounds& base,
	const ActivePictureBounds& proposed)
{
	const auto axes = static_cast<unsigned>(base.trustedBarAxes);
	if ((axes & static_cast<unsigned>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0 ||
		base.top <= 0 || base.bottom >= base.rasterHeight ||
		proposed.left < base.left || proposed.right > base.right ||
		IsVerticalSamplingProposal(base, proposed))
		return false;
	const int step = std::max(2, base.rasterHeight / 540);
	return proposed.top >= base.top - step &&
		proposed.bottom <= base.bottom + step &&
		(proposed.top < base.top || proposed.bottom > base.bottom);
}

bool SamplingExpansionPixelsAreSafe(SampleContext& samples,
	const ActivePictureBounds& base, const ActivePictureBounds& proposed,
	int blackThreshold, int& peakY, int& peakChromaDelta)
{
	// Whole-bar sampling can miss a thin bright or colored strip. Inspect each
	// disputed row at the existing visible-extent horizontal density, using the
	// same credible-luma cutoff and retained-bar chroma tolerance. At 4K this
	// costs at most 2,048 samples (one scan step at each vertical edge).
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
	int blackThreshold, AnalysisLumaSample* diagnosticSample = nullptr)
{
	AnalysisLumaSample sample;
	if (!samples.source.Sample(x, y, sample))
		return false;
	++samples.lumaSamples;
	++samples.chromaSamples;
	if (diagnosticSample) *diagnosticSample = sample;
	// Match the denser renderer-local bar pass: blackThreshold already carries
	// 24 codes above the measured floor, so this is floor + 32. The denser grid
	// and 2x2 support rule retain noise rejection while covering small controls.
	const bool elevatedLuma = sample.luma > blackThreshold + 8;
	const bool colored = (std::abs(static_cast<int>(sample.chromaU) - 512) >= 64 ||
		std::abs(static_cast<int>(sample.chromaV) - 512) >= 64) &&
		sample.luma >= blackThreshold + 8;
	return elevatedLuma || colored;
}

// Require connected picture at the same neighboring sample columns across the
// candidate edge. Aggregate black coverage can miss a narrow object; a black
// gap around a caption/logo must not count as picture continuation.
bool HasHorizontalPictureContinuation(SampleContext& samples, bool top,
    int boundary, int blackThreshold)
{
    const int step = std::max(2, samples.source.height / 540);
    const int inside = top ? boundary + step : boundary - 1 - step;
    const int outside = top ? boundary - 1 - step : boundary + step;
    const int farther = top ? boundary - 1 - 2 * step : boundary + 2 * step;
    if (inside < 0 || inside >= samples.source.height ||
        outside < 0 || outside >= samples.source.height ||
        farther < 0 || farther >= samples.source.height)
        return false;
    const int columns = std::min(kVisibleExtentLineSamples, samples.source.width);
    bool previous = false;
    for (int column = 0; column < columns; ++column)
    {
        const int x = ((2 * column + 1) * samples.source.width) /
            (2 * columns);
        const bool connected = IsCrediblyVisible(samples, x, outside, blackThreshold) &&
            IsCrediblyVisible(samples, x, farther, blackThreshold) &&
            IsCrediblyVisible(samples, x, inside, blackThreshold);
        if (connected && previous) return true;
        previous = connected;
    }
    return false;
}

ExcludedBandVisibleExtent FindHorizontalVisibleExtent(SampleContext& samples,
	bool top, int barPixels, int blackThreshold)
{
	ExcludedBandVisibleExtent result;
	if (barPixels <= 0)
		return result;
	const int depthSamples = std::min(kVisibleExtentDepthSamples, barPixels);
	result.lumaCutoff = blackThreshold + 8;
	result.depthSamples = depthSamples;
	int previousOccupied = -2;
	ActivePictureVisibleExtentDiagnostic previousLine;
	for (int d = 0; d < depthSamples; ++d)
	{
		const int depth = ((d * 2 + 1) * barPixels) / (depthSamples * 2);
		const int y = top ? depth : samples.source.height - 1 - depth;
		int visible = 0;
		ActivePictureVisibleExtentDiagnostic line;
		for (int i = 0; i < kVisibleExtentLineSamples; ++i)
		{
			const int x = ((i * 2 + 1) * samples.source.width) /
				(kVisibleExtentLineSamples * 2);
			AnalysisLumaSample pixel;
			if (IsCrediblyVisible(samples, x, y, blackThreshold, &pixel))
			{
				const int chromaDelta = std::max(std::abs(int(pixel.chromaU)-512), std::abs(int(pixel.chromaV)-512));
				if (visible == 0)
				{
					line.firstX=x; line.firstY=y; line.firstLuma=pixel.luma;
					line.firstU=pixel.chromaU; line.firstV=pixel.chromaV;
					line.firstReason=(pixel.luma > blackThreshold+8 ? 1 : 0) |
						(chromaDelta >= 64 && pixel.luma >= blackThreshold+8 ? 2 : 0);
				}
				++visible;
				line.peakLuma=std::max(line.peakLuma,int(pixel.luma));
				line.peakChromaDelta=std::max(line.peakChromaDelta,chromaDelta);
			}
		}
		// Two spatial samples are enough for a narrow glyph, but require the
		// signal on adjacent depth rows below to reject isolated hot pixels.
		if (visible < 2)
			continue;
		if (d != previousOccupied + 1)
		{
			previousLine = line;
			previousLine.firstLine = y;
			previousLine.firstLineSupport = visible;
			previousOccupied = d;
			continue;
		}
		const int outerDepthIndex = previousOccupied;
		const int extentDepth = ((outerDepthIndex * 2 + 1) * barPixels) /
			(depthSamples * 2);
		const int sampleStep = std::max(1, barPixels / depthSamples);
		result = previousLine;
		result.secondLine = y;
		result.secondLineSupport = visible;
		result.peakLuma = std::max(result.peakLuma,line.peakLuma);
		result.peakChromaDelta = std::max(result.peakChromaDelta,line.peakChromaDelta);
		result.sampleStep = sampleStep;
		result.depthSamples = depthSamples;
		result.lumaCutoff = blackThreshold + 8;
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
	result.lumaCutoff = blackThreshold + 8;
	result.depthSamples = depthSamples;
	int previousOccupied = -2;
	ActivePictureVisibleExtentDiagnostic previousLine;
	for (int d = 0; d < depthSamples; ++d)
	{
		const int depth = ((d * 2 + 1) * barPixels) / (depthSamples * 2);
		const int x = left ? depth : samples.source.width - 1 - depth;
		int visible = 0;
		ActivePictureVisibleExtentDiagnostic line;
		for (int i = 0; i < kVisibleExtentLineSamples; ++i)
		{
			const int y = ((i * 2 + 1) * samples.source.height) /
				(kVisibleExtentLineSamples * 2);
			AnalysisLumaSample pixel;
			if (IsCrediblyVisible(samples, x, y, blackThreshold, &pixel))
			{
				const int chromaDelta = std::max(std::abs(int(pixel.chromaU)-512), std::abs(int(pixel.chromaV)-512));
				if (visible == 0)
				{
					line.firstX=x; line.firstY=y; line.firstLuma=pixel.luma;
					line.firstU=pixel.chromaU; line.firstV=pixel.chromaV;
					line.firstReason=(pixel.luma > blackThreshold+8 ? 1 : 0) |
						(chromaDelta >= 64 && pixel.luma >= blackThreshold+8 ? 2 : 0);
				}
				++visible;
				line.peakLuma=std::max(line.peakLuma,int(pixel.luma));
				line.peakChromaDelta=std::max(line.peakChromaDelta,chromaDelta);
			}
		}
		if (visible < 2)
			continue;
		if (d != previousOccupied + 1)
		{
			previousLine = line;
			previousLine.firstLine = x;
			previousLine.firstLineSupport = visible;
			previousOccupied = d;
			continue;
		}
		const int outerDepthIndex = previousOccupied;
		const int extentDepth = ((outerDepthIndex * 2 + 1) * barPixels) /
			(depthSamples * 2);
		const int sampleStep = std::max(1, barPixels / depthSamples);
		result = previousLine;
		result.secondLine = x;
		result.secondLineSupport = visible;
		result.peakLuma = std::max(result.peakLuma,line.peakLuma);
		result.peakChromaDelta = std::max(result.peakChromaDelta,line.peakChromaDelta);
		result.sampleStep = sampleStep;
		result.depthSamples = depthSamples;
		result.lumaCutoff = blackThreshold + 8;
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
	bool currentPresentationRetainable, bool partialSamplingReaffirmed)
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
		(IsVerticalSamplingProposal(trusted,observed) ||
		 (partialSamplingReaffirmed && IsPartialVerticalSamplingProposal(trusted,observed)));
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
	case ActivePictureAxisReason::BAR_PICTURE_CONTINUATION: return "bar-picture-continuation";
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
	observation.authorityOrigin = evidence.authorityOrigin;
	observation.sparseTransitionProof = evidence.sparseTransitionProof;
	observation.rememberedEdgeReturnProof = evidence.rememberedEdgeReturnProof;
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
	// All four directional searches share one hard budget. The rough scan stays
	// below 30,000 luma reads even for adversarial all-black or nested-frame
	// input. Eligible vertical crops may require additional strict band profiles.
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

	// A coarse stop is the first sampled picture line, not necessarily the
	// first real picture line. Refine only its already bracketed interval,
	// using the identical line predicate and a separate bounded reserve.
	// Otherwise a native crop can exclude one to three visible rows that the
	// denser retention pass correctly refuses on the very same source frame.
	// Keep the entire existing coarse budget available to all four directions.
	// Partial-axis evidence remains usable even when another axis used it up.
	// 64 extra lines keep the worst rough-scan cost below 30,000 luma reads.
	int refinementLinesRemaining = 64;
	auto refineOutward = [&](bool row, bool leading, int& boundary,
		int step, EdgeScan& scan) {
		const int extent = row ? source.height : source.width;
		if (!scan.complete || boundary <= 0 || boundary >= extent)
			return true;
		const int first = leading ? boundary - step + 1 : boundary + step - 2;
		for (int offset = 0; offset < step - 1; ++offset)
		{
			const int coordinate = leading ? first + offset : first - offset;
			if (coordinate < 0 || coordinate >= extent) continue;
			// An uninspected bracket cannot keep the inward coarse boundary as
			// crop authority. Explicitly reject this observation below.
			if (refinementLinesRemaining <= 0) return false;
			--refinementLinesRemaining;
			bool supported = false;
			if (!ScanBlackLine(samples, row, coordinate, blackThreshold, supported))
			{
				boundary = leading ? coordinate : coordinate + 1;
				break;
			}
		}
		// Preserve the whole chroma pair at odd picture boundaries. Rounding
		// is always outward; it may retain a black pixel, never discard picture.
		boundary = leading ? boundary & ~1 : std::min(extent, (boundary + 1) & ~1);
		return true;
	};
	if (!refineOutward(true, true, top, yStep, topScan) ||
		!refineOutward(true, false, bottom, yStep, bottomScan) ||
		!refineOutward(false, true, left, xStep, leftScan) ||
		!refineOutward(false, false, right, xStep, rightScan))
	{
		result.reason = "native edge refinement exhausted bounded reserve";
		result.lumaSamples = samples.lumaSamples;
		result.chromaSamples = samples.chromaSamples;
		return result;
	}

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
        const bool topContinues = result.top.trusted &&
            HasHorizontalPictureContinuation(samples, true, top, blackThreshold);
        const bool bottomContinues = result.bottom.trusted &&
            HasHorizontalPictureContinuation(samples, false, bottom, blackThreshold);
        if (topContinues || bottomContinues)
        {
            // Preserve measured bounds and independent horizontal authority.
            // This rejects a candidate; it does not prove full-raster content.
            if (topContinues) result.top.trusted = false;
            if (bottomContinues) result.bottom.trusted = false;
            result.axisEvidence.vertical.reason = ActivePictureAxisReason::BAR_PICTURE_CONTINUATION;
        }
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
	// A failed side proposal remains failed and every source column stays visible.
	// Preserve the existing broad-side path; otherwise inspect only the bands
	// a vertical crop would remove. Side darkness cannot veto those verified bars.
	if (verticalTrusted && result.axisEvidence.horizontal.FailedBar() &&
		result.axisEvidence.horizontal.scanComplete && source.width >= 320 && source.height >= 180)
	{
		auto& axes = result.axisEvidence;
		axes.sidePictureWidth = source.width;
		axes.sidePictureHeight = source.height;
		axes.sidePictureTop = top;
		axes.sidePictureBottom = bottom;
		axes.sidePictureThreshold = blackThreshold + 24;
		if (left == 0)
			axes.leftPictureMinimum = InspectSidePicture(samples, true, top, bottom, axes.sidePictureThreshold, result.leftSideProbe);
		if (right == source.width)
			axes.rightPictureMinimum = InspectSidePicture(samples, false, top, bottom, axes.sidePictureThreshold, result.rightSideProbe);
		SummarizeSidePicture(result.leftSideProbe, axes.leftPictureStrongZoneMask, axes.leftPictureNonBlackMinimum);
		SummarizeSidePicture(result.rightSideProbe, axes.rightPictureStrongZoneMask, axes.rightPictureNonBlackMinimum);
		// Keep the original four-zone route unchanged. Independent vertical crop
		// permission instead requires current strict removed-band inspection, with
		// no side-brightness quorum and no authority borrowed from remembered bounds.
		if (axes.horizontal.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED &&
			axes.vertical.state == ActivePictureAxisState::TRUSTED_BARS && axes.vertical.scanComplete &&
			axes.leftPictureMinimum < 6 && axes.rightPictureMinimum < 6)
		{
			result.verticalBarProfile = InspectVerticalCropBarProfile(samples, top, bottom);
			axes.verticalCropProfileEvaluated = result.verticalBarProfile.evaluated;
			// A coarse scan coordinate can exclude a few actual picture rows. Keep
			// one whole native scan step instead of relaxing the black profile.
			// Always use this envelope on the new route so tiny profile changes do
			// not alternate between padded and unpadded targets across the queue.
			const auto& profile = result.verticalBarProfile;
			const bool boundaryOnly = profile.completed && profile.mismatchSamples > 0 &&
				profile.deepMismatchSamples == 0 && profile.boundaryMismatchSamples == profile.mismatchSamples;
			if (profile.clean || boundaryOnly)
			{
				// Round within the one-step limit to retain even source coordinates,
				// matching the renderer's chroma-safe crop contract on all formats.
				const int guardedTop = ((top - yStep + 1) / 2) * 2;
				const int guardedBottom = ((bottom + yStep) / 2) * 2;
				if (guardedTop > 0 && guardedTop <= top && guardedBottom >= bottom &&
					guardedBottom < source.height && (guardedTop < top || guardedBottom > bottom))
				{
					result.verticalBarGuardProfile = InspectVerticalCropBarProfile(samples, guardedTop, guardedBottom);
					if (result.verticalBarGuardProfile.clean)
					{
						// Keep uncertainty rows visible and bind the strict profile to
						// this exact crop and its raw aperture. Retained rows and side
						// pixels require no additional picture-brightness evidence.
						axes.verticalCropGuardTop = top - guardedTop;
						axes.verticalCropGuardBottom = guardedBottom - bottom;
						axes.verticalCropProfileClean = true;
						result.trustedBounds.top = guardedTop;
						result.trustedBounds.bottom = guardedBottom;
					}
				}
			}
		}
	}
	const int trustedWidth =
		result.trustedBounds.right - result.trustedBounds.left;
	const int trustedHeight =
		result.trustedBounds.bottom - result.trustedBounds.top;
	result.trustedBounds.aspectRatio =
		static_cast<double>(trustedWidth) / trustedHeight;
	// Record the independently verified axes and keep full-raster coordinates
	// on an untrusted axis. This is measurement authority, not permission to
	// replace an established program format: transition admission separately
	// checks incomplete axes and possible all-sided inset compositions.
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
	const ActivePictureEvidence& observed,
    bool allowRejectedAlignedEdge)
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
    const bool rejectedAlignedEdge = allowRejectedAlignedEdge && (cleanTop
        ? (!observed.bottom.trusted && std::abs(observed.proposedBounds.bottom-inferredBottom) <= step)
        : (!observed.top.trusted && std::abs(observed.proposedBounds.top-inferredTop) <= step));
    // A subtitle can spoil strict edge statistics without moving the proposed
    // boundary. The diagnostic opt-in still needs all pixel checks below.
	if ((!oppositeExpanded && !rejectedAlignedEdge) || inferredBottom <= inferredTop)
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

	// A mostly black band can still contain real picture connected to the
	// inferred edge. Aggregate black coverage cannot authorize mirroring over
	// that picture. Reuse the native distributed-content witness on both sides
	// of the boundary, leaving one scan step for sampling/filter fringe.
	const int inferredBoundary = cleanTop ? inferredBottom : inferredTop;
	const int insideRow = cleanTop ? inferredBoundary - 1 - step : inferredBoundary + step;
	const int outsideRow = cleanTop ? inferredBoundary + step : inferredBoundary - 1 - step;
	const int furtherOutsideRow = cleanTop ? inferredBoundary + 2 * step : inferredBoundary - 1 - 2 * step;
	auto broadPictureAt = [&](int y) {
		if (y < 0 || y >= source.height) return false;
		bool supported = false;
		ScanBlackLine(samples, true, y, blackThreshold, supported);
		return supported;
	};
	if (HasHorizontalPictureContinuation(samples, !cleanTop, inferredBoundary, blackThreshold) ||
        (broadPictureAt(insideRow) && broadPictureAt(outsideRow) &&
         broadPictureAt(furtherOutsideRow)))
	{
		result.lumaSamples += samples.lumaSamples;
		result.chromaSamples += samples.chromaSamples;
		result.axisEvidence.vertical.reason = ActivePictureAxisReason::BAR_PICTURE_CONTINUATION;
		result.reason = "symmetric hypothesis contradicts connected picture";
		return result;
	}

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


bool ActivePicturePresentationRetentionEvidence::IsWeakBoundedFringe(
	const ActivePictureBounds& presentation) const
{
	if (!analysisValid || !presentationValid || !outwardVisibleBoundsAvailable ||
		presentation.rasterWidth <= 0 || presentation.rasterHeight <= 0 ||
		presentation.left < 0 || presentation.top < 0 ||
		presentation.left >= presentation.right || presentation.top >= presentation.bottom ||
		presentation.right > presentation.rasterWidth ||
		presentation.bottom > presentation.rasterHeight ||
		outwardVisibleBounds.rasterWidth != presentation.rasterWidth ||
		outwardVisibleBounds.rasterHeight != presentation.rasterHeight ||
		outwardVisibleBounds.left < 0 || outwardVisibleBounds.top < 0 ||
		outwardVisibleBounds.right > presentation.rasterWidth ||
		outwardVisibleBounds.bottom > presentation.rasterHeight ||
		outwardVisibleBounds.left != presentation.left ||
		outwardVisibleBounds.top > presentation.top ||
		outwardVisibleBounds.right != presentation.right ||
		outwardVisibleBounds.bottom < presentation.bottom ||
		visibleLeft.available || visibleRight.available ||
		visibleTop.available == visibleBottom.available)
		return false;
	const bool top = visibleTop.available;
	const auto& witness = top ? visibleTop : visibleBottom;
	// These are measured source rows, before the outward presentation margin.
	// Do not mistake a low average across a broad occupied bar for weak fringe.
	if (witness.sampleStep != 1 || witness.firstLine < 0 || witness.secondLine < 0 ||
		std::abs(witness.firstLine - witness.secondLine) != 1 ||
		witness.firstLineSupport < 2 || witness.secondLineSupport < 2 ||
		witness.firstLineSupport > kVisibleExtentLineSamples / 16 ||
		witness.secondLineSupport > kVisibleExtentLineSamples / 16 ||
		witness.peakLuma > witness.lumaCutoff + 8 || witness.peakChromaDelta >= 64)
		return false;
	const int outer = top
		? std::min(witness.firstLine, witness.secondLine)
		: std::max(witness.firstLine, witness.secondLine);
	const int inner = top
		? std::max(witness.firstLine, witness.secondLine)
		: std::min(witness.firstLine, witness.secondLine);
	if (top)
		return outwardVisibleBounds.top < presentation.top &&
			outwardVisibleBounds.bottom == presentation.bottom &&
			inner < presentation.top && presentation.top - outer <= 2;
	return outwardVisibleBounds.bottom > presentation.bottom &&
		outwardVisibleBounds.top == presentation.top &&
		inner >= presentation.bottom && outer < presentation.rasterHeight &&
		outer - (presentation.bottom - 1) <= 2;
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
	result.visibleTop = topExtent; result.visibleBottom = bottomExtent;
	result.visibleLeft = leftExtent; result.visibleRight = rightExtent;
	result.visibleTop.presentationMargin = result.visibleBottom.presentationMargin = std::max(2, source.height / 180);
	result.visibleLeft.presentationMargin = result.visibleRight.presentationMargin = std::max(2, source.width / 180);
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
	// A partial dark-picture measurement may move far inward on one edge while
	// missing the opposite edge by one scan step. Keep the original contract
	// only with a separate current strip certificate, not geometry tolerance.
	else if (result.excludedBandsPixelSafe && !result.proposedBoundsContained &&
		result.proposedBoundsAvailable && !result.globalNearBlack &&
		result.activePicture.classification == ActivePictureClassification::PROVISIONAL &&
		IsPartialVerticalSamplingProposal(trustedPresentation, result.activePicture.proposedBounds))
	{
		result.partialSamplingEvaluated = true;
		result.partialSamplingReaffirmed = SamplingExpansionPixelsAreSafe(samples,
			trustedPresentation, result.activePicture.proposedBounds, blackThreshold,
			result.samplingStripPeakY, result.samplingStripPeakChromaDelta);
	}
	// Acquisition may also be unavailable on a logo/title or exhaust its scan
	// budget. Retain only the existing rectangle when current bands are safe.
	// Other available conflicting proposals still veto retention, except for
	// the established global-near-black rule.
	const bool geometryUnavailable = !result.activePicture.available &&
		result.activePicture.classification == ActivePictureClassification::UNAVAILABLE;
	result.currentlyPixelSafe = result.excludedBandsPixelSafe &&
		(result.proposedBoundsContained || result.samplingReaffirmed || result.partialSamplingReaffirmed ||
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
	else if (result.partialSamplingEvaluated)
		result.reason = result.partialSamplingReaffirmed
			? "partial inward proposal retained after current outward-strip pixel proof"
			: "partial inward proposal has conflicting outward-strip pixels";
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


ActivePictureSideDiagnostics MeasureActivePictureSideDiagnostics(
    const AnalysisLumaSource& source, const ActivePictureEvidence& evidence)
{
    ActivePictureSideDiagnostics result;
    // DIAGNOSTIC SIDE PROBE RED SEAM
    const auto& bounds = evidence.trustedBounds;
    const auto& axes = evidence.axisEvidence;
    if (!source.IsValid() || source.width < 320 || source.height < 180 || !evidence.available ||
        evidence.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        evidence.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        !IsValidBoundsForSource(bounds, source) || bounds.left != 0 || bounds.right != source.width ||
        bounds.top <= 0 || bounds.bottom >= source.height ||
        bounds.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        axes.vertical.state != ActivePictureAxisState::TRUSTED_BARS || !axes.vertical.scanComplete ||
        !axes.horizontal.FailedBar() || !axes.horizontal.scanComplete ||
        axes.sidePictureWidth != source.width || axes.sidePictureHeight != source.height ||
        axes.verticalCropGuardTop < 0 || axes.verticalCropGuardTop > std::max(2, source.height / 540) ||
        axes.verticalCropGuardBottom < 0 || axes.verticalCropGuardBottom > std::max(2, source.height / 540) ||
        axes.sidePictureTop < bounds.top || axes.sidePictureTop >= axes.sidePictureBottom ||
        axes.sidePictureBottom > bounds.bottom ||
        axes.sidePictureTop - bounds.top != axes.verticalCropGuardTop ||
        bounds.bottom - axes.sidePictureBottom != axes.verticalCropGuardBottom ||
        axes.sidePictureThreshold < 24 || axes.sidePictureThreshold > 128)
        return result;
    SampleContext samples{source};
    result.aperture = bounds;
    result.aperture.top = axes.sidePictureTop;
    result.aperture.bottom = axes.sidePictureBottom;
    result.aperture.aspectRatio = static_cast<double>(source.width) /
        (result.aperture.bottom - result.aperture.top);
    result.threshold = axes.sidePictureThreshold;
    // Match the authoritative raw witness aperture, including when a boundary
    // guard retains additional rows outside it. These reads do not grant authority.
    result.leftMinimum = InspectSidePicture(samples, true, result.aperture.top, result.aperture.bottom,
        result.threshold, result.left);
    result.rightMinimum = InspectSidePicture(samples, false, result.aperture.top, result.aperture.bottom,
        result.threshold, result.right);
    result.lumaSamples = samples.lumaSamples;
    if (result.lumaSamples != 288) return {};
    result.evaluated = true;
    return result;
}


ActivePictureOpposingBoundaryDiagnostics MeasureActivePictureOpposingBoundaryDiagnostics(
    const AnalysisLumaSource& source, const ActivePictureEvidence& evidence)
{
    ActivePictureOpposingBoundaryDiagnostics result;
    // OPPOSING BOUNDARY DIAGNOSTIC RED SEAM
    const auto& raw = evidence.proposedBounds;
    const auto& axes = evidence.axisEvidence;
    if (!source.IsValid() || source.width < 320 || source.height < 180 ||
        !evidence.available || evidence.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        evidence.classification != ActivePictureClassification::PROVISIONAL ||
        !IsValidBoundsForSource(raw, source) || raw.left != 0 || raw.right != source.width ||
        !axes.horizontal.scanComplete || axes.horizontal.barCandidate || axes.horizontal.FailedBar() ||
        axes.horizontal.state == ActivePictureAxisState::TRUSTED_BARS ||
        !axes.vertical.scanComplete || axes.vertical.reason != ActivePictureAxisReason::BAR_ASYMMETRY ||
        !axes.vertical.FailedBar() || !evidence.top.trusted || !evidence.bottom.trusted ||
        evidence.top.barPixels != raw.top || evidence.bottom.barPixels != source.height - raw.bottom ||
        !std::isfinite(evidence.top.lumaFloor) || evidence.top.lumaFloor < 0.0 ||
        evidence.top.lumaFloor > 80.0 || evidence.top.lumaFloor != evidence.bottom.lumaFloor)
        return result;
    const int inset = std::min(raw.top, source.height - raw.bottom);
    if (inset < 3 || inset * 2 >= source.height || raw.top == source.height - raw.bottom)
        return result;
    const int floor = static_cast<int>(evidence.top.lumaFloor);
    const int cutoff = std::min(104, floor + 24);
    result.candidate = {0, inset, source.width, source.height - inset,
        source.width, source.height, static_cast<double>(source.width) / (source.height - 2 * inset),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    SampleContext samples{source};
    result.top = InspectHorizontalEdge(samples, true, inset, inset, floor, cutoff);
    result.bottom = InspectHorizontalEdge(samples, false, inset, source.height - inset, floor, cutoff);
    const auto adjacent = [&](bool top) {
        ActivePictureAdjacentBoundaryProbe probe;
        std::vector<int> outside;
        outside.reserve(kLineSamples);
        const int boundary = top ? inset : source.height - inset;
        const int insideY = top ? boundary + 2 : boundary - 3;
        const int outsideY = top ? boundary - 3 : boundary + 2;
        const int requiredContrast = inset < source.height / 20 ? 18 : 10;
        for (int i = 0; i < kLineSamples; ++i)
        {
            const int x = ((i * 2 + 1) * source.width) / (kLineSamples * 2);
            const int inner = samples.Luma(x, insideY), outer = samples.Luma(x, outsideY);
            probe.insideMean += inner; probe.outsideMean += outer;
            probe.supported += inner - outer >= requiredContrast;
            outside.push_back(outer);
        }
        probe.insideMean /= kLineSamples; probe.outsideMean /= kLineSamples;
        probe.contrast = probe.insideMean - probe.outsideMean;
        probe.outsideP90 = Percentile(outside, 0.90);
        probe.outsideDispersion = probe.outsideP90 - Percentile(outside, 0.10);
        return probe;
    };
    result.topAdjacent = adjacent(true);
    result.bottomAdjacent = adjacent(false);
    result.lumaSamples = samples.lumaSamples;
    result.chromaSamples = samples.chromaSamples;
    if (result.lumaSamples != 864 || result.chromaSamples != 576) return {};
    result.evaluated = true;
    return result;
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

RelativeBarContrastEvidence InspectRelativeBarContrast(const AnalysisLumaSource& source,
    const ActivePictureEvidence& raw, const ActivePictureBounds& base)
{
    constexpr int columns = 96;
    constexpr int stripDepths = 6;
    constexpr int boundaryRows = 8;
    constexpr int profileTolerance = 4;
    constexpr int minimumContrast = 8;
    constexpr int minimumSupportedColumns = columns / 2;
    constexpr int minimumSupportedPerQuartile = 6;
    constexpr size_t sampleBudget = 16384;

    RelativeBarContrastEvidence result;
    result.evaluated = true;
    result.base = base;
    result.target = raw.trustedBounds;
    result.sourceGeneration = source.generation;
    result.reason = "geometry-not-eligible";
    const auto& target = result.target;
    const auto validVerticalBounds = [&](const ActivePictureBounds& bounds) {
        return bounds.rasterWidth == source.width && bounds.rasterHeight == source.height &&
            bounds.left == 0 && bounds.right == source.width && bounds.top > 0 &&
            bounds.bottom < source.height && bounds.bottom > bounds.top &&
            bounds.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM &&
            std::abs(bounds.top - (source.height - bounds.bottom)) <=
                std::max(2, source.height / 270);
    };
    const auto& horizontal = raw.axisEvidence.horizontal;
    const bool sideAllowed = horizontal.scanComplete &&
        ((horizontal.state == ActivePictureAxisState::FULL_EXTENT_SUPPORTED && !horizontal.barCandidate) ||
         (horizontal.FailedBar() && horizontal.reason == ActivePictureAxisReason::BAR_EDGE_REJECTED));
    if (!source.IsValid() || source.generation == 0 || source.width < 320 || source.height < 180 ||
        !raw.available || raw.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        raw.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        !raw.top.trusted || !raw.bottom.trusted || !sideAllowed ||
        !raw.axisEvidence.vertical.scanComplete ||
        raw.axisEvidence.vertical.state != ActivePictureAxisState::TRUSTED_BARS ||
        !validVerticalBounds(base) || !validVerticalBounds(target) ||
        target.top >= base.top || target.bottom <= base.bottom ||
        (target.bottom - target.top) <= (base.bottom - base.top) * 1.05)
        return result;

    const auto readSample = [&](int x, int y, AnalysisLumaSample& sample) {
        if (result.samples >= sampleBudget)
        {
            result.reason = "sample-budget";
            return false;
        }
        ++result.samples;
        if (!source.Sample(x, y, sample))
        {
            result.reason = "sample-invalid";
            return false;
        }
        return true;
    };
    bool strong[2] = {false, false};
    for (int side = 0; side < 2; ++side)
    {
        const int barHeight = side ? source.height - target.bottom : target.top;
        const int rowStep = std::max(1, source.height / 540);
        std::vector<int> rows;
        for (int depth = 0; depth < barHeight; depth += rowStep)
            rows.push_back(depth);
        // Inspect every row adjacent to the actual discard boundary: do not let
        // a caption line hide between the coarse bar-depth sampling rows.
        for (int depth = std::max(0, barHeight - boundaryRows); depth < barHeight; ++depth)
            rows.push_back(depth);
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

        std::vector<AnalysisLumaSample> exterior;
        std::vector<int> luma, chromaU, chromaV;
        for (int depth : rows)
        {
            for (int column = 0; column < columns; ++column)
            {
                AnalysisLumaSample sample;
                const int x = (2 * column + 1) * source.width / (2 * columns);
                const int y = side ? source.height - 1 - depth : depth;
                if (!readSample(x, y, sample))
                    return result;
                exterior.push_back(sample);
                luma.push_back(sample.luma);
                chromaU.push_back(sample.chromaU);
                chromaV.push_back(sample.chromaV);
            }
        }
        const double referenceY = Percentile(luma, .5);
        const double referenceU = Percentile(chromaU, .5);
        const double referenceV = Percentile(chromaV, .5);
        result.reason = "bar-reference-not-black";
        if (referenceY > 80 || std::abs(referenceU - 512) > 8 || std::abs(referenceV - 512) > 8)
            return result;
        result.reason = "bar-profile-contaminated";
        for (const auto& pixel : exterior)
        {
            if (std::abs(pixel.luma - referenceY) > profileTolerance ||
                std::abs(pixel.chromaU - referenceU) > profileTolerance ||
                std::abs(pixel.chromaV - referenceV) > profileTolerance)
                return result;
        }

        const int stripBegin = side ? base.bottom : target.top;
        const int stripEnd = side ? target.bottom : base.top;
        std::vector<int> picture;
        int blackSamples = 0;
        int continuousRows = 0;
        double texture = 0;
        for (int depth = 0; depth < stripDepths; ++depth)
        {
            const int y = stripBegin + (2 * depth + 1) * (stripEnd - stripBegin) / (2 * stripDepths);
            int supported = 0;
            int quartiles[4] = {};
            int rowBlackSamples = 0;
            int previous = -1;
            for (int column = 0; column < columns; ++column)
            {
                AnalysisLumaSample sample;
                if (!readSample((2 * column + 1) * source.width / (2 * columns), y, sample))
                    return result;
                const int value = sample.luma;
                picture.push_back(value);
                if (value >= referenceY + minimumContrast)
                {
                    ++supported;
                    ++quartiles[column / (columns / 4)];
                }
                // Use the maximum existing bar cutoff (104), rather than its
                // adaptive value. This makes the strong-strip corroboration
                // no more permissive than BroadPictureLike's occupancy gate.
                if (value <= 104)
                {
                    ++blackSamples;
                    ++rowBlackSamples;
                }
                if (previous >= 0)
                    texture += std::abs(value - previous);
                previous = value;
            }
            result.reason = "strip-contrast-not-distributed";
            if (supported < minimumSupportedColumns ||
                *std::min_element(quartiles, quartiles + 4) < minimumSupportedPerQuartile)
                return result;
            continuousRows += rowBlackSamples >= 88;
        }
        // Keep BroadPictureLike's brightness/texture and occupancy limits on
        // at least one newly exposed strip, measured from current pixels.
        strong[side] = blackSamples <= picture.size() * .80 && continuousRows <= stripDepths * .85 &&
            (Percentile(picture, .9) >= 112 || texture / (stripDepths * (columns - 1)) >= 12);
    }
    result.reason = "no-strong-picture-strip";
    if (!strong[0] && !strong[1])
        return result;
    result.valid = true;
    result.reason = "relative-bars-and-picture-confirmed";
    return result;
}
