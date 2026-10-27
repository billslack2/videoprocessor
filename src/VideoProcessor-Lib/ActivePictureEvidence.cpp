#include "pch.h"

#include "ActivePictureEvidence.h"
#include "SubtitleBarEvidence.h"
#include "SubtitleBoxLookahead.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>


namespace
{
bool CurrentOwnedInkAt(const SubtitleInkSnapshot& ink, int x, int y);

constexpr int kLineSamples = 48;
constexpr int kEdgeDepthSamples = 6;
constexpr int kGlobalGridWidth = 16;
constexpr int kGlobalGridHeight = 16;
constexpr int kGlobalNearBlackP90 = 96;
// Limited Y black is code 64, leaving 32 codes to the legacy cutoff.
// Full-range Y has black at zero; scale that signal interval to 0..1023.
constexpr double kFullRangeNearBlackP90 = 32.0 * 1023.0 / 876.0;
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
	// Presentation-only ownership: never mask picture pixels or side bars.
	const ActivePictureBounds* erasedOverlay = nullptr;
	const ActivePictureBounds* presentation = nullptr;
	int erasedBlackFloor = 0;
	const std::vector<uint8_t>* residualOverlay = nullptr;
	ActivePictureBounds residualBounds;
    const AnalysisEvaluationBudget* budget = nullptr;
    size_t sampleAccesses = 0;
    void Checkpoint()
    {
        // A clock/predicate read per bounded batch, never per sampled pixel.
        if (budget && (sampleAccesses++ & 63u) == 0) budget->Checkpoint();
    }
	bool IsResidualOverlay(int x, int y) const
	{
		return residualOverlay && x >= residualBounds.left && x < residualBounds.right &&
			y >= residualBounds.top && y < residualBounds.bottom &&
			(*residualOverlay)[size_t(y-residualBounds.top) *
				(residualBounds.right-residualBounds.left) + x-residualBounds.left] != 0;
	}
	bool IsErasedOverlay(int x, int y) const
	{
		return erasedOverlay && presentation &&
			x >= erasedOverlay->left && x < erasedOverlay->right &&
			y >= erasedOverlay->top && y < erasedOverlay->bottom &&
			x >= presentation->left && x < presentation->right &&
			(y < presentation->top || y >= presentation->bottom);
	}

	int Luma(int x, int y)
	{
        Checkpoint();
		if (IsErasedOverlay(x, y) || IsResidualOverlay(x, y)) return erasedBlackFloor;
		AnalysisLumaSample sample;
		if (!source.Sample(x, y, sample))
			return 0;
		++lumaSamples;
		return sample.luma;
	}

	void Chroma(int x, int y, int& u, int& v)
	{
        Checkpoint();
		if (IsErasedOverlay(x, y) || IsResidualOverlay(x, y)) { u = v = 512; return; }
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

// Qualify a constant source matte over the entire rough-nominated band,
// using only samples the ordinary edge inspection already collected. Middle
// quarters may contain captions; both outer flanks must agree at every depth.
// A noisy but plausible reference stays unresolved rather than proving picture.
ActivePictureMatteReference InspectRaisedMatteReference(
    const std::vector<int>& luma,
    const std::array<int, kEdgeDepthSamples * kLineSamples>& chromaU,
    const std::array<int, kEdgeDepthSamples * kLineSamples>& chromaV)
{
    ActivePictureMatteReference result;
    result.state = ActivePictureMatteReferenceState::REJECTED;
    struct Cell { int y, u, v, dy, du, dv; };
    std::array<Cell, kEdgeDepthSamples * 2> cells{};
    std::array<int, kEdgeDepthSamples * 2> ys{}, us{}, vs{};
    size_t cellIndex = 0;
    for (int depth = 0; depth < kEdgeDepthSamples; ++depth)
        for (int flank = 0; flank < 2; ++flank)
        {
            std::array<int, kLineSamples / 4> y{}, u{}, v{};
            for (int i = 0; i < kLineSamples / 4; ++i)
            {
                const size_t index = depth * kLineSamples +
                    flank * (3 * kLineSamples / 4) + i;
                y[i] = luma[index]; u[i] = chromaU[index]; v[i] = chromaV[index];
            }
            std::sort(y.begin(), y.end());
            std::sort(u.begin(), u.end());
            std::sort(v.begin(), v.end());
            // Same lower-index percentiles as the existing edge summaries.
            const size_t median = (y.size() - 1) / 2;
            const size_t low = (y.size() - 1) / 10;
            const size_t high = (9 * (y.size() - 1)) / 10;
            cells[cellIndex] = { y[median], u[median], v[median],
                y[high] - y[low], u[high] - u[low], v[high] - v[low] };
            ys[cellIndex] = y[median]; us[cellIndex] = u[median]; vs[cellIndex] = v[median];
            ++cellIndex;
        }
    std::sort(ys.begin(), ys.end());
    std::sort(us.begin(), us.end());
    std::sort(vs.begin(), vs.end());
    result.y = ys[(ys.size() - 1) / 2];
    result.u = us[(us.size() - 1) / 2];
    result.v = vs[(vs.size() - 1) / 2];
    // Reference qualification may use the entire existing black-sample envelope.
    // The ordinary cutoff remains <=104; every spatial/chroma/continuity veto
    // and the independent retention cap remain unchanged.
    if (result.y <= 24 || result.y > 104 ||
        std::abs(result.u - 512) > 32 || std::abs(result.v - 512) > 32)
        return result;
    bool qualified = true;
    for (const auto& cell : cells)
    {
        const int dy = std::abs(cell.y - result.y);
        const int du = std::abs(cell.u - result.u);
        const int dv = std::abs(cell.v - result.v);
        // Preserve uncertainty for bounded +/-6-code block/row noise, even
        // when a whole sampled cell has the same signed noise offset. A
        // depth-correlated picture gradient exceeds this reference envelope.
        if (cell.dy > 24 || cell.du > 24 || cell.dv > 24 ||
            ys.back() - ys.front() > 12 || us.back() - us.front() > 12 ||
            vs.back() - vs.front() > 12)
            return result;
        qualified = qualified && cell.dy <= 16 && cell.du <= 16 && cell.dv <= 16 &&
            dy <= 4 && du <= 4 && dv <= 4;
    }
    result.state = qualified ? ActivePictureMatteReferenceState::QUALIFIED :
        ActivePictureMatteReferenceState::UNRESOLVED;
    return result;
}

struct EdgeScan
{
	bool complete = false;
	bool outerContentSupported = false;
    bool nominalOuterContentSupported = false;
};
bool ScanBlackLine(SampleContext& samples, bool row, int coordinate, int threshold,
	bool& contentSupported, bool* nominalContentSupported = nullptr)
{
	int black = 0;
	int supported[4] = {};
    int nominalSupported[4] = {};
	for (int i = 0; i < kLineSamples; ++i)
	{
		const int position = ((i * 2 + 1) * (row ? samples.source.width : samples.source.height)) /
			(kLineSamples * 2);
		const int luma = row ? samples.Luma(position, coordinate) : samples.Luma(coordinate, position);
		black += luma <= threshold;
		supported[i / 12] += luma > threshold + 24;
        if (nominalContentSupported) nominalSupported[i / 12] += luma > 48;
	}
	// Distributed broad-picture support is separate from the few bright pixels
	// sufficient to stop a black-line search. It also vetoes conflicting symmetry.
	contentSupported = supported[0] >= 6 && supported[1] >= 6 && supported[2] >= 6 && supported[3] >= 6;
    if (nominalContentSupported)
        *nominalContentSupported = nominalSupported[0] >= 6 && nominalSupported[1] >= 6 &&
            nominalSupported[2] >= 6 && nominalSupported[3] >= 6;
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
	SampleContext& samples, int top, int bottom, const SubtitleInkSnapshot* currentInk = nullptr)
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
        samples.Checkpoint();
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
        int known=0,leftKnown=0,rightKnown=0;
		for (int i = 0; i < columns; ++i)
		{
            if(currentInk && CurrentOwnedInkAt(*currentInk,xAt(i),y))continue;
            ++known;leftKnown+=i<columns/8;rightKnown+=i>=columns-columns/8;
			AnalysisLumaSample value;
			if (!sampleAt(xAt(i), y, value)) return result;
			ys.push_back(value.luma); us.push_back(value.chromaU); vs.push_back(value.chromaV);
		}
        if(known<columns/3 || leftKnown<4 || rightKnown<4)return result;
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
		for (int y : rows) {
            int known=0,leftKnown=0,rightKnown=0;
			for (int i = 0; i < columns; ++i)
			{
				AnalysisLumaSample value;
				const int x = xAt(i);
                // Only current detector-owned strokes can be unknown. In
                // particular, do not mask the subtitle's box or inter-line gap.
                if(currentInk && CurrentOwnedInkAt(*currentInk,x,y))continue;
                ++known;leftKnown+=i<columns/8;rightKnown+=i>=columns-columns/8;
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
            if(known<columns/3 || leftKnown<4 || rightKnown<4) {
                result.reason="excluded-band-support-insufficient";return result;
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
    const bool inspectMatte = samples.source.range == AnalysisLumaRange::Full && blackFloor > 0;
    std::array<int, kEdgeDepthSamples * kLineSamples> matteU{}, matteV{};
	double texture = 0.0;
	for (int d = 0; d < kEdgeDepthSamples; ++d)
	{
		const int depth = std::min(barPixels - 1,
			((d * 2 + 1) * barPixels) / (kEdgeDepthSamples * 2));
		const int y = top ? outerOffset + depth : samples.source.height - 1 - outerOffset - depth;
		int lineBlack = 0;
		int previous = -1;
		std::array<int, 4> aboveNearBlack{};
		for (int i = 0; i < kLineSamples; ++i)
		{
			const int x = ((i * 2 + 1) * samples.source.width) /
				(kLineSamples * 2);
			const int value = samples.Luma(x, y);
			luma.push_back(value);
			black += value <= blackThreshold;
			lineBlack += value <= blackThreshold;
			if (value > blackThreshold)
				++aboveNearBlack[i / (kLineSamples / 4)];
			if (previous >= 0)
				texture += std::abs(value - previous);
			previous = value;
			int u = 0, v = 0;
			samples.Chroma(x, y, u, v);
            if (inspectMatte)
            {
                const size_t index = d * kLineSamples + i;
                matteU[index] = u; matteV[index] = v;
            }
			neutral += std::abs(u - 512) <= 32 &&
				std::abs(v - 512) <= 32;
		}
		for (size_t quartile = 0; quartile < aboveNearBlack.size(); ++quartile)
		{
			if (aboveNearBlack[quartile] >= kLineSamples / 8)
				evidence.distributedSupportDepthRowsByQuartile[quartile] |=
					static_cast<uint8_t>(1u << d);
		}
		continuousLines += lineBlack >= 44;
	}
    if (inspectMatte) evidence.matteReference = InspectRaisedMatteReference(luma, matteU, matteV);
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
    const bool inspectMatte = samples.source.range == AnalysisLumaRange::Full && blackFloor > 0;
    std::array<int, kEdgeDepthSamples * kLineSamples> matteU{}, matteV{};
	double texture = 0.0;
	for (int d = 0; d < kEdgeDepthSamples; ++d)
	{
		const int depth = std::min(barPixels - 1,
			((d * 2 + 1) * barPixels) / (kEdgeDepthSamples * 2));
		const int x = left ? outerOffset + depth : samples.source.width - 1 - outerOffset - depth;
		int lineBlack = 0;
		int previous = -1;
		std::array<int, 4> aboveNearBlack{};
		for (int i = 0; i < kLineSamples; ++i)
		{
			const int y = ((i * 2 + 1) * samples.source.height) /
				(kLineSamples * 2);
			const int value = samples.Luma(x, y);
			luma.push_back(value);
			black += value <= blackThreshold;
			lineBlack += value <= blackThreshold;
			if (value > blackThreshold)
				++aboveNearBlack[i / (kLineSamples / 4)];
			if (previous >= 0)
				texture += std::abs(value - previous);
			previous = value;
			int u = 0, v = 0;
			samples.Chroma(x, y, u, v);
            if (inspectMatte)
            {
                const size_t index = d * kLineSamples + i;
                matteU[index] = u; matteV[index] = v;
            }
			neutral += std::abs(u - 512) <= 32 &&
				std::abs(v - 512) <= 32;
		}
		for (size_t quartile = 0; quartile < aboveNearBlack.size(); ++quartile)
		{
			if (aboveNearBlack[quartile] >= kLineSamples / 8)
				evidence.distributedSupportDepthRowsByQuartile[quartile] |=
					static_cast<uint8_t>(1u << d);
		}
		continuousLines += lineBlack >= 44;
	}
    if (inspectMatte) evidence.matteReference = InspectRaisedMatteReference(luma, matteU, matteV);
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
            samples.Checkpoint();
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
	if (samples.IsErasedOverlay(x, y) || samples.IsResidualOverlay(x, y)) return false;
	AnalysisLumaSample sample;
    samples.Checkpoint();
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

// A clean outer bar can be shorter than an over-detected opposite bar when
// encoded dark picture values fall below the ordinary black threshold. This
// secondary route keeps the threshold shared by every format: it recovers only
// a current, symmetric boundary whose excluded pixels match the measured bar
// and whose picture side has a coherent low-code edge. No history is borrowed.
static bool RecoverLowContrastOpposingBoundary(const AnalysisLumaSource& source,
    ActivePictureEvidence& evidence,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
    const auto& raw=evidence.proposedBounds;
    auto& axes=evidence.axisEvidence;
    if(evidence.classification!=ActivePictureClassification::PROVISIONAL ||
        axes.vertical.reason!=ActivePictureAxisReason::BAR_ASYMMETRY ||
        !axes.vertical.scanComplete || !evidence.top.trusted || !evidence.bottom.trusted ||
        !axes.horizontal.scanComplete || axes.horizontal.barCandidate ||
        axes.horizontal.FailedBar() || raw.left!=0 || raw.right!=source.width ||
        source.width<320 || source.height<180)return false;
    const int step=std::max(2,source.height/540);
    const int topBar=raw.top,bottomBar=source.height-raw.bottom;
    const int shorter=std::min(topBar,bottomBar),longer=std::max(topBar,bottomBar);
    // The existing opposing-boundary rule requires stronger contrast for
    // bars shorter than 1/20 of the raster. Low-code recovery must not
    // replace that small-bar requirement.
    if(shorter<=step*2 || shorter<source.height/20 || shorter>=source.height/4 ||
        longer-shorter<=step*2 || longer-shorter>source.height/8)return false;
    const bool anchorTop=topBar<bottomBar;
    const auto& anchor=anchorTop?evidence.top:evidence.bottom;
    if(!std::isfinite(anchor.lumaFloor) || anchor.lumaFloor<0.0 ||
        anchor.lumaFloor>80.0 || anchor.lumaP90>anchor.lumaFloor+4.0 ||
        anchor.lumaDispersion>4.0 || anchor.texture>2.0 ||
        anchor.blackFraction<0.99 || anchor.neutralChromaFraction<0.98 ||
        anchor.continuity<0.99 || anchor.innerBoundaryContrast<24.0 ||
        evidence.top.lumaFloor!=evidence.bottom.lumaFloor)return false;
    const int anchorFloor=static_cast<int>(anchor.lumaFloor);
    const int nominalTop=shorter,nominalBottom=source.height-shorter;
    // Fixed outward uncertainty is source resolution scaled and chroma aligned.
    // The same nomination always produces the same rectangle.
    const int guardedTop=(nominalTop-step)&~1;
    const int guardedBottom=std::min(source.height,(nominalBottom+step+1)&~1);
    if(guardedTop<=0 || guardedBottom>=source.height ||
        guardedBottom-guardedTop<source.height/3 ||
        (anchorTop && nominalBottom<=raw.bottom+step) ||
        (!anchorTop && nominalTop>=raw.top-step))return false;
    SampleContext samples{source};
    samples.budget = budget;
    struct SampleCountGuard {
        ActivePictureEvidence& evidence;
        SampleContext& samples;
        ~SampleCountGuard() {
            evidence.lumaSamples+=samples.lumaSamples;
            evidence.chromaSamples+=samples.chromaSamples;
        }
    } countGuard{evidence,samples};
    auto profile=InspectVerticalCropBarProfile(samples,guardedTop,guardedBottom);
    if(!profile.completed || !profile.clean)return false;
    const int boundary=anchorTop?nominalBottom:nominalTop;
    const int insideDirection=anchorTop?-1:1;
    const int outsideDirection=-insideDirection;
    const int nearRequired=std::max(4,profile.maxLumaDelta+2);
    const int deepRequired=std::max(8,2*profile.maxLumaDelta+4);
    const int chromaRequired=std::max(8,2*profile.maxChromaDelta+2);
    // 48 columns, four zones, four inner depths and two immediately exterior
    // rows. A zero-code bar, isolated bright pixel, or smooth low-slope fade
    // cannot manufacture a full-width picture transition.
    for(int zone=0;zone<4;++zone) {
        int nearSupport=0,deepSupport[3]={},abruptSupport=0;
        int nearSum=0,outsideSum=0;
        for(int index=zone*12;index<(zone+1)*12;++index) {
            const int x=((index*2+1)*source.width)/96;
            AnalysisLumaSample outside[2]{},inside[4]{};
            CheckAnalysisEvaluationBudget(budget);
            for(int d=0;d<2;++d) {
                const int y=boundary+outsideDirection*(2+d*4);
                if(y<0 || y>=source.height || !source.Sample(x,y,outside[d]))return false;
                ++samples.lumaSamples;++samples.chromaSamples;
                if(std::abs(int(outside[d].luma)-profile.referenceY)>4 ||
                    std::abs(int(outside[d].chromaU)-profile.referenceU)>4 ||
                    std::abs(int(outside[d].chromaV)-profile.referenceV)>4)return false;
            }
            for(int d=0;d<4;++d) {
                const int y=boundary+insideDirection*(3+d*4);
                if(y<0 || y>=source.height || !source.Sample(x,y,inside[d]))return false;
                ++samples.lumaSamples;++samples.chromaSamples;
                const int luma=int(inside[d].luma)-profile.referenceY;
                const int chroma=std::max(std::abs(int(inside[d].chromaU)-profile.referenceU),
                    std::abs(int(inside[d].chromaV)-profile.referenceV));
                const bool supported=luma>=(d==0?nearRequired:deepRequired) ||
                    chroma>=chromaRequired;
                if(d==0)nearSupport+=supported;
                else deepSupport[d-1]+=supported;
            }
            nearSum+=inside[0].luma;
            outsideSum+=outside[0].luma;
            int previous=-1,maximumDrop=0;
            for(int offset=-5;offset<=2;++offset) {
                AnalysisLumaSample adjacent{};
                const int y=boundary+outsideDirection*offset;
                if(y<0 || y>=source.height || !source.Sample(x,y,adjacent))return false;
                ++samples.lumaSamples;++samples.chromaSamples;
                if(previous>=0)maximumDrop=std::max(maximumDrop,previous-int(adjacent.luma));
                previous=adjacent.luma;
            }
            abruptSupport+=maximumDrop>=std::max(4,profile.maxLumaDelta+3);
        }
        if(nearSupport<8 || deepSupport[0]<9 || deepSupport[1]<9 ||
            deepSupport[2]<9 || abruptSupport<8 ||
            nearSum-outsideSum<12*nearRequired)return false;
    }
    evidence.verticalBarGuardProfile=profile;
    evidence.top=InspectHorizontalEdge(samples,true,guardedTop,guardedTop,
        anchorFloor,
        std::min(104,anchorFloor+24));
    evidence.bottom=InspectHorizontalEdge(samples,false,source.height-guardedBottom,
        guardedBottom,anchorFloor,
        std::min(104,anchorFloor+24));
    // The guarded edge contrast is intentionally weaker; the entire outer band
    // and the actual nominal transition above supplied its independent proof.
    evidence.top.trusted=evidence.bottom.trusted=true;
    evidence.trustedBounds={0,guardedTop,source.width,guardedBottom,
        source.width,source.height,
        double(source.width)/(guardedBottom-guardedTop),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    axes.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
    axes.vertical.reason=ActivePictureAxisReason::BAR_CONFIRMED;
    evidence.classification=ActivePictureClassification::BAR_CROP_TRUSTED;
    evidence.reason="measured low-code opposing boundary and excluded bands match current bars";
    return true;
}


ActivePictureEvidence ExtractActivePictureEvidence(
	const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
	ActivePictureEvidence result;
	if (!source.IsValid() || source.width < 16 || source.height < 16)
	{
		result.reason = "invalid analysis source dimensions, layout, or data pointer";
		return result;
	}

	SampleContext samples{ source };
    samples.budget = budget;
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
	const bool fullRange = source.range == AnalysisLumaRange::Full;
    // Full-range 25..31 is already inside the raised-matte qualifier's domain.
    // Let it nominate the same strict opposing-reference proof as 32..80,
    // instead of stranding it above nominal24 but below the old floor switch.
    // Limited/Unknown interpretation and the maximum black cutoff are unchanged.
    const int nominalFloorLimit = fullRange ? 24 : 31;
    const int blackFloor = observedLow <= nominalFloorLimit ? 0 :
        fullRange ? std::min(observedLow, 80) : Bounded(observedLow, 48, 80);
    const int blackThreshold = std::min(104, blackFloor + 24);
    auto& reference = result.blackReference;
    reference.evaluated = true;
    reference.observedFloor = reference.floor = blackFloor;
    reference.observedThreshold = reference.threshold = blackThreshold;
    reference.mode = !fullRange ? ActivePictureBlackReferenceMode::LEGACY :
        blackFloor ? ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED :
        ActivePictureBlackReferenceMode::FULL_NOMINAL;

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
		const bool outer = coordinate == 0 || coordinate == (row ? source.height : source.width) - 1;
        const bool black = ScanBlackLine(samples, row, coordinate, blackThreshold, supported,
            fullRange && outer ? &scan.nominalOuterContentSupported : nullptr);
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

    if (fullRange)
    {
        if (topScan.nominalOuterContentSupported && bottomScan.nominalOuterContentSupported)
            reference.nominalFullExtentAxes |= 1;
        if (leftScan.nominalOuterContentSupported && rightScan.nominalOuterContentSupported)
            reference.nominalFullExtentAxes |= 2;
    }
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
    reference.adaptiveCandidate = result.proposedBounds;
    auto completeNominalFull = [&]() {
        reference.floor = 0; reference.threshold = 24;
        reference.nominalReclassified = blackFloor > 0;
        reference.mode = blackFloor > 0 ? ActivePictureBlackReferenceMode::FULL_REJECTED_RAISED :
            ActivePictureBlackReferenceMode::FULL_NOMINAL;
        result.trustedBounds = { 0, 0, source.width, source.height, source.width, source.height,
            static_cast<double>(source.width) / source.height, ActivePictureBounds::BarAxes::NONE };
        result.proposedBounds = result.trustedBounds;
        result.left = {}; result.top = {}; result.right = {}; result.bottom = {};
        result.axisEvidence = {};
        auto fullAxis = [](ActivePictureAxisEvidence& axis) {
            axis.scanComplete = true;
            axis.state = ActivePictureAxisState::FULL_EXTENT_SUPPORTED;
            axis.reason = ActivePictureAxisReason::FULL_EXTENT_SUPPORTED;
        };
        fullAxis(result.axisEvidence.horizontal); fullAxis(result.axisEvidence.vertical);
        result.classification = ActivePictureClassification::FULL_RASTER_TRUSTED;
        result.reason = "nominal Full-range outer lines affirm both full-raster axes";
        result.lumaSamples = samples.lumaSamples;
        result.chromaSamples = samples.chromaSamples;
        return result;
    };
	const int topBar = top;
	const int bottomBar = source.height - bottom;
	const int leftBar = left;
	const int rightBar = source.width - right;
	const bool hasVertical = topBar > yStep * 2 || bottomBar > yStep * 2;
	const bool hasHorizontal = leftBar > xStep * 2 || rightBar > xStep * 2;
	if (!hasVertical && !hasHorizontal)
	{
        if (fullRange)
        {
            if (reference.nominalFullExtentAxes == 3) return completeNominalFull();
            result.classification = ActivePictureClassification::PROVISIONAL;
            result.reason = "Full-range nominal extent lacks distributed support on both axes";
            result.lumaSamples = samples.lumaSamples;
            result.chromaSamples = samples.chromaSamples;
            return result;
        }
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
    bool unresolvedFullReference = false;
    if (fullRange && blackFloor > 0)
    {
        const ActivePictureEdgeEvidence* edges[] = { &result.left, &result.top, &result.right, &result.bottom };
        for (size_t i = 0; i < 4; ++i)
        {
            if (edges[i]->matteReference.state == ActivePictureMatteReferenceState::QUALIFIED)
                reference.qualifiedEdgeMask |= static_cast<uint8_t>(1u << i);
            else if (edges[i]->matteReference.state == ActivePictureMatteReferenceState::UNRESOLVED)
                reference.unresolvedEdgeMask |= static_cast<uint8_t>(1u << i);
        }
        auto matching = [](const ActivePictureMatteReference& a, const ActivePictureMatteReference& b) {
            return a.state == ActivePictureMatteReferenceState::QUALIFIED &&
                b.state == ActivePictureMatteReferenceState::QUALIFIED &&
                std::abs(a.y - b.y) <= 4 && std::abs(a.u - b.u) <= 4 && std::abs(a.v - b.v) <= 4;
        };
        if (matching(result.top.matteReference, result.bottom.matteReference)) reference.qualifiedAxes |= 1;
        if (matching(result.left.matteReference, result.right.matteReference)) reference.qualifiedAxes |= 2;
        const uint8_t pairedEdges = static_cast<uint8_t>(
            ((reference.qualifiedAxes & 1) ? 10 : 0) | ((reference.qualifiedAxes & 2) ? 5 : 0));
        unresolvedFullReference = reference.unresolvedEdgeMask != 0 ||
            (reference.qualifiedEdgeMask & ~pairedEdges) != 0 || reference.qualifiedAxes == 0;
        if (!reference.qualifiedEdgeMask && !reference.unresolvedEdgeMask &&
            reference.nominalFullExtentAxes == 3)
            return completeNominalFull();
        reference.mode = unresolvedFullReference ? ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED :
            ActivePictureBlackReferenceMode::FULL_QUALIFIED_RAISED;
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
			// Filtering can leave visible boundary rows outside the coarse scan's
			// first nonblack line. Retain two whole native scan steps; never relax
			// the black profile or accept mismatches deeper in the excluded bars.
			// Use the same envelope even when the raw profile is already clean so
			// small fringe changes cannot alternate the crop across queued frames.
			const auto& profile = result.verticalBarProfile;
			const int guardRows = 2 * yStep;
			const bool boundaryOnly = profile.completed && profile.mismatchSamples > 0 &&
				(profile.topFirstMismatchY < 0 || profile.topFirstMismatchY >= top - guardRows) &&
				(profile.bottomLastMismatchY < 0 || profile.bottomLastMismatchY < bottom + guardRows);
			if (profile.clean || boundaryOnly)
			{
				// Round within the two-step limit to retain even source coordinates,
				// matching the renderer's chroma-safe crop contract on all formats.
				const int guardedTop = ((top - guardRows + 1) / 2) * 2;
				const int guardedBottom = ((bottom + guardRows) / 2) * 2;
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
    if (unresolvedFullReference)
    {
        result.classification = ActivePictureClassification::PROVISIONAL;
        result.trustedBounds = { 0, 0, source.width, source.height, source.width, source.height,
            static_cast<double>(source.width) / source.height, ActivePictureBounds::BarAxes::NONE };
        auto rejectRaisedBars = [](ActivePictureAxisEvidence& axis) {
            if (axis.state == ActivePictureAxisState::TRUSTED_BARS)
            {
                axis.state = ActivePictureAxisState::UNKNOWN;
                axis.reason = ActivePictureAxisReason::BAR_EDGE_REJECTED;
            }
        };
        // An unresolved matte invalidates its bar authority, not independent
        // positive full-extent evidence needed by current caption hypotheses.
        rejectRaisedBars(result.axisEvidence.horizontal);
        rejectRaisedBars(result.axisEvidence.vertical);
        result.reason = "raised Full-range matte reference is unresolved; no new crop or full-raster authority";
    }
    else RecoverLowContrastOpposingBoundary(source,result,budget);
    CheckAnalysisEvaluationBudget(budget);
	return result;
}

const char* ActivePictureBlackReferenceModeName(ActivePictureBlackReferenceMode mode)
{
    switch (mode)
    {
    case ActivePictureBlackReferenceMode::LEGACY: return "legacy";
    case ActivePictureBlackReferenceMode::FULL_NOMINAL: return "full-nominal";
    case ActivePictureBlackReferenceMode::FULL_QUALIFIED_RAISED: return "full-qualified-raised";
    case ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED: return "full-unresolved-raised";
    case ActivePictureBlackReferenceMode::FULL_REJECTED_RAISED: return "full-rejected-raised";
    default: return "unknown";
    }
}

ActivePictureEvidence EvaluateSymmetricVerticalBarHypothesis(
	const AnalysisLumaSource& source,
	const ActivePictureEvidence& observed,
    bool allowRejectedAlignedEdge,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
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
    samples.budget = budget;
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
    CheckAnalysisEvaluationBudget(budget);
	return result;
}


ActivePictureEvidence EvaluateMeasuredVerticalBarHypothesis(
    const AnalysisLumaSource& source,
    const ActivePictureEvidence& observed,
    const ActivePictureBounds& measuredCandidate)
{
    ActivePictureEvidence result = observed;
    const auto& raw = observed.proposedBounds;
    const auto& axes = observed.axisEvidence;
    if (!source.IsValid() || !observed.available ||
        observed.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        raw.rasterWidth != source.width || raw.rasterHeight != source.height ||
        raw.top < 0 || raw.bottom > source.height || raw.top >= raw.bottom ||
        observed.classification != ActivePictureClassification::PROVISIONAL ||
        axes.vertical.reason != ActivePictureAxisReason::BAR_ASYMMETRY ||
        !axes.vertical.FailedBar() || !axes.vertical.scanComplete ||
        !observed.top.trusted || !observed.bottom.trusted ||
        axes.horizontal.state != ActivePictureAxisState::FULL_EXTENT_SUPPORTED ||
        axes.horizontal.reason != ActivePictureAxisReason::FULL_EXTENT_SUPPORTED ||
        !axes.horizontal.scanComplete || axes.horizontal.barCandidate ||
        raw.left != 0 || raw.right != source.width ||
        measuredCandidate.rasterWidth != source.width ||
        measuredCandidate.rasterHeight != source.height ||
        measuredCandidate.left != 0 ||
        measuredCandidate.right != source.width ||
        measuredCandidate.top <= 0 ||
        measuredCandidate.bottom >= source.height ||
        measuredCandidate.top >= measuredCandidate.bottom ||
        (measuredCandidate.top & 1) != 0 ||
        (measuredCandidate.bottom & 1) != 0)
        return result;

    const int step = std::max(2, source.height / 540);
    const int topBar = measuredCandidate.top;
    const int bottomBar = source.height - measuredCandidate.bottom;
    const int symmetryTolerance = std::max(step * 2, source.height / 360);
    if (topBar <= step * 2 || bottomBar <= step * 2 ||
        topBar >= source.height / 3 || bottomBar >= source.height / 3 ||
        measuredCandidate.bottom - measuredCandidate.top < source.height / 3 ||
        std::abs(topBar - bottomBar) > symmetryTolerance)
        return result;
    const bool topMatchesRaw = std::abs(raw.top - measuredCandidate.top) <= step;
    const bool bottomMatchesRaw = std::abs(raw.bottom - measuredCandidate.bottom) <= step;
    // The known clean edge anchors the nomination. The other must be displaced
    // inward from the raw text-contaminated boundary, never into picture.
    if (topMatchesRaw == bottomMatchesRaw ||
        (topMatchesRaw && measuredCandidate.bottom >= raw.bottom - step) ||
        (bottomMatchesRaw && measuredCandidate.top <= raw.top + step))
        return result;
    const auto& clean = topMatchesRaw ? observed.top : observed.bottom;
    if (!std::isfinite(clean.lumaFloor) || clean.lumaFloor < 0.0 ||
        clean.lumaFloor > 80.0)
        return result;
    const int floor = static_cast<int>(clean.lumaFloor);
    const int blackThreshold = std::min(104, floor + 24);
    SampleContext samples{source};
    auto top = InspectHorizontalEdge(samples, true, topBar,
        measuredCandidate.top, floor, blackThreshold);
    auto bottom = InspectHorizontalEdge(samples, false, bottomBar,
        measuredCandidate.bottom, floor, blackThreshold);
    const bool strictEdges = top.trusted && bottom.trusted;
    const bool connectedPicture = strictEdges &&
        (HasHorizontalPictureContinuation(samples, true,
            measuredCandidate.top, blackThreshold) ||
         HasHorizontalPictureContinuation(samples, false,
            measuredCandidate.bottom, blackThreshold));
    result.lumaSamples += samples.lumaSamples;
    result.chromaSamples += samples.chromaSamples;
    if (!strictEdges || connectedPicture)
        return result;

    result.top = top;
    result.bottom = bottom;
    result.trustedBounds = {0, measuredCandidate.top, source.width,
        measuredCandidate.bottom, source.width, source.height,
        static_cast<double>(source.width) /
            (measuredCandidate.bottom - measuredCandidate.top),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    result.axisEvidence.vertical.state =
        ActivePictureAxisState::TRUSTED_BARS;
    result.axisEvidence.vertical.reason =
        ActivePictureAxisReason::BAR_CONFIRMED;
    result.classification =
        ActivePictureClassification::BAR_CROP_TRUSTED;
    result.reason = "measured flank bars pass strict current picture edges";
    return result;
}


ActivePictureEvidence EvaluateCaptionBackedVerticalBarHypothesis(
    const AnalysisLumaSource& source,
    const ActivePictureEvidence& observed,
    SubtitleScriptHint scriptHint,
    uint64_t sequence)
{
    if (!source.IsValid() || !sequence || !observed.available ||
        observed.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        observed.classification != ActivePictureClassification::PROVISIONAL ||
        observed.axisEvidence.vertical.reason !=
            ActivePictureAxisReason::BAR_ASYMMETRY)
        return observed;
    const auto bars = ExtractSubtitleBarEvidence(source);
    if (!bars.available || bars.top <= 0 || bars.bottom >= source.height)
        return observed;
    const int measuredTop = bars.top & ~1;
    const int measuredBottom = std::min(source.height, (bars.bottom + 1) & ~1);
    if (measuredBottom <= measuredTop)
        return observed;
    const ActivePictureBounds candidate = {
        0, measuredTop, source.width, measuredBottom,
        source.width, source.height,
        static_cast<double>(source.width) / (measuredBottom - measuredTop),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    auto strict = EvaluateMeasuredVerticalBarHypothesis(source, observed, candidate);
    if (strict.classification != ActivePictureClassification::BAR_CROP_TRUSTED)
        return observed;

    SubtitleBoxDetector scanner;
    scanner.SetScriptHint(scriptHint);
    const auto caption = scanner.Analyze(source, measuredTop, measuredBottom,
        sequence, 0);
    const int step = std::max(2, source.height / 540);
    const bool displacedTop =
        measuredTop > observed.proposedBounds.top + step;
    const bool displacedBottom =
        measuredBottom < observed.proposedBounds.bottom - step;
    const bool actualInkCrosses =
        caption.detected && !caption.workLimit && caption.anchor.Valid() &&
        caption.nearBarEligibilityMeasured && caption.nearBarEligible &&
        ((displacedTop && caption.anchor.top < measuredTop &&
            caption.anchor.bottom > measuredTop) ||
         (displacedBottom && caption.anchor.top < measuredBottom &&
            caption.anchor.bottom > measuredBottom));
    if (!actualInkCrosses)
        return observed;
    strict.reason = "measured native bars with current crossing glyph proof";
    return strict;
}

namespace
{
struct OccludedBarInspection
{
    ActivePictureEdgeEvidence edge;
    int flankSupport = 0;
    size_t unknown = 0, samples = 0;
    bool backingExcluded = false, connectedPicture = false;
};

// The subtitle's sampled ownership is a reason to skip a source pixel, never
// an assertion that the pixel was black. Unowned background remains visible to
// the ordinary bar proof. The tiny neighborhood covers antialiased stroke
// fringes without turning the caption's bounding rectangle into a mask.
bool CurrentOwnedInkAt(const SubtitleInkSnapshot& ink, int x, int y)
{
    if (!ink.rawEvidenceComplete || ink.step <= 0 || ink.width <= 0 ||
        ink.height <= 0 || ink.sourceRows.size() != size_t(ink.height) ||
        ink.ownedInk.empty())
        return false;
    const int rowTolerance = std::max(1, ink.step);
    auto row = std::lower_bound(ink.sourceRows.begin(), ink.sourceRows.end(), y);
    int nearest = -1;
    if (row != ink.sourceRows.end() && std::abs(*row - y) <= rowTolerance)
        nearest = int(row - ink.sourceRows.begin());
    if (row != ink.sourceRows.begin() &&
        (nearest < 0 || std::abs(*(row - 1) - y) <
            std::abs(ink.sourceRows[size_t(nearest)] - y)) &&
        std::abs(*(row - 1) - y) <= rowTolerance)
        nearest = int(row - ink.sourceRows.begin()) - 1;
    if (nearest < 0) return false;
    const int column = x / ink.step;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (ink.Get(column + dx, nearest + dy, true)) return true;
    return false;
}

std::array<SubtitleBoxRect, 3> MeasuredBoundaryPanels(
    const SubtitleBoxObservation& current,
    const ActivePictureBounds& candidate)
{
    std::array<SubtitleBoxRect, 3> panels{};
    if (!current.text.capturePanelMeasured ||
        !current.text.anchor.Valid()) return panels;
    // capturePanel is a union for subtitle cleanup. A union may cover real
    // picture between two disjoint line cards, so AR may exclude only each
    // independently measured line interior. Empty legacy line panels grant no
    // backing exclusion; exact sampled owned ink can still explain glyphs.
    for (size_t i = 0; i < panels.size(); ++i)
    {
        const auto& card = current.text.lineCapturePanels[i];
        const auto& line = current.text.lineBounds[i];
        if (!card.Valid() || !line.Valid() ||
            card.left < 0 || card.right > candidate.rasterWidth ||
            card.top < 0 || card.bottom > candidate.rasterHeight ||
            card.right <= line.left || card.left >= line.right ||
            card.bottom <= line.top || card.top >= line.bottom)
            continue;
        // Capture panels are clipped to picture. They may exclude only their
        // own measured picture-side interior at the boundary-contrast row,
        // never inferred card continuation into the encoded bar.
        if ((card.top <= candidate.top + 2 &&
                card.bottom > candidate.top + 2) ||
            (card.top < candidate.bottom - 3 &&
                card.bottom >= candidate.bottom - 3))
            panels[i] = card;
    }
    return panels;
}

OccludedBarInspection InspectSubtitleOccludedBar(
    const AnalysisLumaSource& source, bool top, int boundary, int floor,
    const SubtitleBoxObservation& current,
    const std::array<SubtitleBoxRect, 3>& measuredPanels)
{
    OccludedBarInspection out;
    const int barPixels = top ? boundary : source.height - boundary;
    out.edge.barPixels = barPixels;
    if (barPixels <= 0) return out;
    const int threshold = std::min(104, floor + 24);
    auto unknownAt = [&](int x, int y) {
        for (const auto& card : measuredPanels)
            if (card.Valid() && x >= card.left && x < card.right &&
                y >= card.top && y < card.bottom)
            { out.backingExcluded = true; return true; }
        return current.ink && CurrentOwnedInkAt(*current.ink, x, y);
    };
    std::vector<int> levels;
    levels.reserve(kEdgeDepthSamples * kLineSamples);
    int black = 0, neutral = 0, known = 0, continuous = 0;
    double texture = 0.0;
    int texturePairs = 0;
    for (int d = 0; d < kEdgeDepthSamples; ++d)
    {
        const int depth = std::min(barPixels - 1,
            ((d * 2 + 1) * barPixels) / (kEdgeDepthSamples * 2));
        const int y = top ? depth : source.height - 1 - depth;
        int rowKnown = 0, rowBlack = 0, prior = -1;
        int leftKnown = 0, rightKnown = 0;
        for (int i = 0; i < kLineSamples; ++i)
        {
            const int x = ((i * 2 + 1) * source.width) /
                (kLineSamples * 2);
            if (unknownAt(x, y)) { ++out.unknown; prior = -1; continue; }
            AnalysisLumaSample pixel;
            if (!source.Sample(x, y, pixel)) return out;
            ++out.samples; ++known; ++rowKnown;
            if (i < kLineSamples / 8) ++leftKnown;
            if (i >= kLineSamples - kLineSamples / 8) ++rightKnown;
            levels.push_back(pixel.luma);
            const bool isBlack = pixel.luma <= threshold;
            black += isBlack; rowBlack += isBlack;
            neutral += std::abs(int(pixel.chromaU) - 512) <= 32 &&
                std::abs(int(pixel.chromaV) - 512) <= 32;
            if (prior >= 0) { texture += std::abs(pixel.luma - prior); ++texturePairs; }
            prior = pixel.luma;
        }
        if (leftKnown < 4 || rightKnown < 4 || rowKnown < 16) return out;
        if (rowBlack * 100 >= rowKnown * 95) ++continuous;
        out.flankSupport += leftKnown + rightKnown;
    }
    if (known < kEdgeDepthSamples * 16 ||
        out.unknown * 3 > kEdgeDepthSamples * kLineSamples * 2 ||
        out.flankSupport < kEdgeDepthSamples * 8)
        return out;
    const double outerMean = double(std::accumulate(levels.begin(), levels.end(), 0LL)) / known;
    double innerMean = 0.0;
    int innerKnown = 0;
    const int innerY = Bounded(top ? boundary + 2 : boundary - 3,
        0, source.height - 1);
    for (int i = 0; i < kLineSamples; ++i)
    {
        const int x = ((i * 2 + 1) * source.width) / (kLineSamples * 2);
        if (unknownAt(x, innerY)) { ++out.unknown; continue; }
        AnalysisLumaSample pixel;
        if (!source.Sample(x, innerY, pixel)) return out;
        ++out.samples; innerMean += pixel.luma; ++innerKnown;
    }
    if (innerKnown < 16) return out;
    innerMean /= innerKnown;
    out.edge.blackFraction = double(black) / known;
    out.edge.lumaFloor = floor;
    out.edge.lumaP90 = Percentile(levels, 0.90);
    out.edge.lumaDispersion = out.edge.lumaP90 - Percentile(levels, 0.10);
    out.edge.texture = texture / std::max(1, texturePairs);
    out.edge.neutralChromaFraction = double(neutral) / known;
    out.edge.innerBoundaryContrast = innerMean - outerMean;
    out.edge.continuity = double(continuous) / kEdgeDepthSamples;
    const double requiredContrast =
        barPixels < source.height / 20 ? 18.0 : 10.0;
    out.edge.trusted = out.edge.blackFraction >= 0.95 &&
        out.edge.lumaP90 <= threshold &&
        out.edge.lumaDispersion <= 24.0 && out.edge.texture <= 8.0 &&
        out.edge.neutralChromaFraction >= 0.90 &&
        out.edge.innerBoundaryContrast >= requiredContrast &&
        out.edge.continuity >= 0.99;
    if (!out.edge.trusted) return out;
    // Never allow masking to explain an actual picture continuation. It is
    // checked at the same distributed columns as the normal native proof.
    const int step = std::max(2, source.height / 540);
    const int inside = top ? boundary + step : boundary - 1 - step;
    const int outside = top ? boundary - 1 - step : boundary + step;
    const int farther = top ? boundary - 1 - 2 * step : boundary + 2 * step;
    if (inside < 0 || inside >= source.height || outside < 0 ||
        outside >= source.height || farther < 0 || farther >= source.height)
        return out;
    bool previousConnected = false;
    for (int i = 0; i < kVisibleExtentLineSamples; ++i)
    {
        const int x = ((2 * i + 1) * source.width) /
            (2 * kVisibleExtentLineSamples);
        if (unknownAt(x, inside) || unknownAt(x, outside) ||
            unknownAt(x, farther)) { previousConnected = false; continue; }
        AnalysisLumaSample a, b, c;
        if (!source.Sample(x, inside, a) || !source.Sample(x, outside, b) ||
            !source.Sample(x, farther, c)) return out;
        out.samples += 3;
        auto visible = [&](const AnalysisLumaSample& pixel) {
            return pixel.luma > threshold + 8 ||
                ((std::abs(int(pixel.chromaU) - 512) >= 64 ||
                    std::abs(int(pixel.chromaV) - 512) >= 64) &&
                    pixel.luma >= threshold + 8);
        };
        const bool connected = visible(a) && visible(b) && visible(c);
        if (connected && previousConnected) out.connectedPicture = true;
        previousConnected = connected;
    }
    return out;
}
}

bool SubtitleAssistedVerticalBarProof::IsCurrentFor(
    const AnalysisLumaSource& source,
    const ActivePictureFrameIdentity& currentIdentity,
    uint64_t currentPolicyGeneration, uint64_t currentContinuityGeneration,
    SubtitleScriptHint currentScriptHint) const
{
    return accepted && source.IsValid() &&
        source.generation == currentIdentity.transportGeneration &&
        width == source.width && height == source.height &&
        SameAnalysisFrameIdentity(identity, currentIdentity) &&
        policyGeneration == currentPolicyGeneration &&
        continuityGeneration == currentContinuityGeneration &&
        scriptHint == currentScriptHint;
}

SubtitleAssistedVerticalBarProof EvaluateSubtitleAssistedVerticalBarProof(
    const AnalysisLumaSource& source,
    const ActivePictureEvidence& observedNative,
    const SubtitleBoxObservation& current,
    const ActivePictureFrameIdentity& currentIdentity,
    uint64_t policyGeneration,
    uint64_t continuityGeneration,
    SubtitleScriptHint scriptHint)
{
    SubtitleAssistedVerticalBarProof proof;
    proof.reason = "current-independent-bars-unavailable";
    const auto& horizontal=observedNative.axisEvidence.horizontal;
    const bool fullWidth=horizontal.state==ActivePictureAxisState::FULL_EXTENT_SUPPORTED &&
        horizontal.reason==ActivePictureAxisReason::FULL_EXTENT_SUPPORTED &&
        horizontal.scanComplete && !horizontal.barCandidate &&
        observedNative.proposedBounds.left==0 && observedNative.proposedBounds.right==source.width;
    const bool uncertainSides=horizontal.scanComplete &&
        ((horizontal.FailedBar() && horizontal.reason==ActivePictureAxisReason::BAR_EDGE_REJECTED) ||
         (horizontal.reason==ActivePictureAxisReason::NO_FULL_EXTENT_SUPPORT &&
          !horizontal.barCandidate && observedNative.proposedBounds.left==0 &&
          observedNative.proposedBounds.right==source.width));

    if (!source.IsValid() || !source.generation ||
        source.generation != currentIdentity.transportGeneration ||
        !currentIdentity.acceptedSequence ||
        !observedNative.available ||
        observedNative.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        observedNative.proposedBounds.rasterWidth != source.width ||
        observedNative.proposedBounds.rasterHeight != source.height ||
        observedNative.proposedBounds.left < 0 ||
        observedNative.proposedBounds.right > source.width ||
        observedNative.proposedBounds.left >= observedNative.proposedBounds.right ||
        observedNative.proposedBounds.top < 0 ||
        observedNative.proposedBounds.bottom > source.height ||
        observedNative.proposedBounds.top >= observedNative.proposedBounds.bottom ||
        (!fullWidth && !uncertainSides) ||
        !current.analyzed || current.analysisRefresh || current.pendingRefresh ||
        current.discontinuity || !current.independentBarAnalysis ||
        !current.barAuthority || current.barTrackingAuthority ||
        current.currentAnchorUsesTrackedEdge ||
        current.policyGeneration != policyGeneration ||
        current.continuityGeneration != continuityGeneration ||
        current.scriptHint != scriptHint ||
        current.width != source.width || current.height != source.height ||
        !SameAnalysisFrameIdentity(current.identity, currentIdentity) ||
        !current.text.detected || current.text.held || current.text.workLimit ||
        !current.text.nearBarEligibilityMeasured || !current.text.nearBarEligible ||
        !current.text.anchor.Valid() ||
        !current.ink || !current.ink->rawEvidenceComplete ||
        current.ink->ownedInk.empty() ||
        !current.barEvidence.available ||
        current.barEvidence.captionBackedTop ||
        current.barEvidence.captionBackedBottom ||
        current.barEvidence.topRevalidatedAtReference ||
        current.barEvidence.topRevalidatedNearby ||
        current.barEvidence.bottomRevalidatedAtReference ||
        current.barEvidence.bottomRevalidatedNearby ||
        current.barEvidence.topSupport < 16 ||
        current.barEvidence.bottomSupport < 16 ||
        current.barEvidence.topBoundarySupport < 8 ||
        current.barEvidence.bottomBoundarySupport < 8)
        return proof;

    const int step = std::max(2, source.height / 540);
    // Preserve chroma-safe source bounds without moving a nominated edge
    // into picture. The independent extractor may report an odd source row.
    const int top = current.pictureTop & ~1;
    const int bottom = std::min(source.height, (current.pictureBottom + 1) & ~1);
    const int symmetryTolerance = std::max(step * 2, source.height / 360);
    const int topBar = top, bottomBar = source.height - bottom;
    if (top <= step * 2 || bottom >= source.height - step * 2 ||
        top >= bottom || topBar >= source.height / 3 ||
        bottomBar >= source.height / 3 ||
        bottom - top < source.height / 3 ||
        (top & 1) || (bottom & 1) ||
        std::abs(topBar - bottomBar) > symmetryTolerance ||
        std::abs(current.barEvidence.top - top) > step ||
        std::abs(current.barEvidence.bottom - bottom) > step ||
        current.barEvidence.topFloor < 0 ||
        current.barEvidence.bottomFloor < 0 ||
        current.barEvidence.topFloor > 80 ||
        current.barEvidence.bottomFloor > 80 ||
        std::abs(current.barEvidence.topFloor -
            current.barEvidence.bottomFloor) > 12)
    {
        proof.reason = "independent-bar-geometry-invalid";
        return proof;
    }
    proof.candidate = {0, top, source.width, bottom,
        source.width, source.height,
        double(source.width) / (bottom - top),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    const auto measuredPanels = MeasuredBoundaryPanels(current, proof.candidate);
    const auto topEdge = InspectSubtitleOccludedBar(source, true, top,
        current.barEvidence.topFloor, current, measuredPanels);
    const auto bottomEdge = InspectSubtitleOccludedBar(source, false, bottom,
        current.barEvidence.bottomFloor, current, measuredPanels);
    proof.topFlankSupport = topEdge.flankSupport;
    proof.bottomFlankSupport = bottomEdge.flankSupport;
    proof.unknownSamples = topEdge.unknown + bottomEdge.unknown;
    proof.lumaSamples = topEdge.samples + bottomEdge.samples;
    proof.usedMeasuredBacking = topEdge.backingExcluded || bottomEdge.backingExcluded;
    if (!topEdge.edge.trusted || !bottomEdge.edge.trusted ||
        topEdge.connectedPicture || bottomEdge.connectedPicture)
    {
        proof.reason = topEdge.connectedPicture || bottomEdge.connectedPicture ?
            "connected-picture-crosses-bar" : "unoccluded-edge-unproved";
        return proof;
    }
    proof.evidence = observedNative;
    if(uncertainSides) {
        // Reuse the existing strictly inspected vertical-only route. All
        // columns remain visible; neither a dark side nor a subtitle can
        // manufacture horizontal authority. Retain two scan steps of edge
        // uncertainty and prove every sampled remaining bar pixel to the same
        // four-code tolerance as ordinary dark-scene vertical acquisition.
        const int guardedTop=((top-2*step+1)/2)*2;
        const int guardedBottom=(std::min)(source.height,((bottom+2*step)/2)*2);
        if(guardedTop<=0 || guardedBottom>=source.height) {
            proof.reason="vertical-guard-invalid";return proof;
        }
        SampleContext samples{source};
        auto profile=InspectVerticalCropBarProfile(samples,guardedTop,guardedBottom,current.ink.get());
        proof.lumaSamples+=samples.lumaSamples;
        proof.evidence.verticalBarGuardProfile=profile;
        if(!profile.completed || !profile.clean) {
            proof.reason="subtitle-excluded-band-profile-failed";return proof;
        }
        proof.candidate.top=guardedTop;proof.candidate.bottom=guardedBottom;
        proof.candidate.aspectRatio=double(source.width)/(guardedBottom-guardedTop);
        auto& axes=proof.evidence.axisEvidence;
        axes.sidePictureWidth=source.width;axes.sidePictureHeight=source.height;
        axes.sidePictureTop=top;axes.sidePictureBottom=bottom;
        axes.sidePictureThreshold=(std::min)(128,current.barEvidence.topFloor+48);
        axes.verticalCropProfileEvaluated=true;axes.verticalCropProfileClean=true;
        axes.verticalCropGlyphExclusions=true;
        axes.verticalCropGuardTop=top-guardedTop;
        axes.verticalCropGuardBottom=guardedBottom-bottom;
    }
    proof.evidence.top = topEdge.edge;
    proof.evidence.bottom = bottomEdge.edge;
    proof.evidence.top.barPixels=proof.candidate.top;
    proof.evidence.bottom.barPixels=source.height-proof.candidate.bottom;
    proof.evidence.trustedBounds = proof.candidate;
    proof.evidence.axisEvidence.vertical.state =
        ActivePictureAxisState::TRUSTED_BARS;
    proof.evidence.axisEvidence.vertical.reason =
        ActivePictureAxisReason::BAR_CONFIRMED;
    proof.evidence.axisEvidence.vertical.scanComplete=true;
    proof.evidence.axisEvidence.vertical.barCandidate=true;
    proof.evidence.classification =
        ActivePictureClassification::BAR_CROP_TRUSTED;
    proof.evidence.lumaSamples += proof.lumaSamples;
    proof.evidence.reason = "exact-current subtitle with independently proved native bars";
    proof.identity = currentIdentity;
    proof.policyGeneration = policyGeneration;
    proof.continuityGeneration = continuityGeneration;
    proof.scriptHint = scriptHint;
    proof.width = source.width; proof.height = source.height;
    proof.accepted = true;
    proof.reason = uncertainSides ? "current-subtitle-masked-vertical-profile-proved" :
        "current-native-bars-and-unoccluded-edges-proved";
    return proof;
}

ActivePictureEvidence EvaluateCurrentCaptionVerticalBarReaffirmation(
    const AnalysisLumaSource& source,
    const ActivePictureEvidence& observed,
    const ActivePictureBounds& established,
    const SubtitleBoxObservation& current,
    const ActivePictureFrameIdentity& currentIdentity,
    uint64_t policyGeneration,
    uint64_t continuityGeneration,
    SubtitleScriptHint scriptHint)
{
    if (!source.IsValid() || !currentIdentity.acceptedSequence ||
        !source.generation || source.generation != currentIdentity.transportGeneration ||
        !current.analyzed || current.analysisRefresh || current.pendingRefresh ||
        !current.barAuthority || current.barTrackingAuthority ||
        current.currentAnchorUsesTrackedEdge ||
        current.policyGeneration != policyGeneration ||
        current.continuityGeneration != continuityGeneration ||
        current.scriptHint != scriptHint ||
        current.width != source.width || current.height != source.height ||
        !SameAnalysisFrameIdentity(current.identity, currentIdentity) ||
        !current.sharedPicture.AvailableFor(currentIdentity, source.width, source.height) ||
        current.sharedPicture.bounds.left != established.left ||
        current.sharedPicture.bounds.top != established.top ||
        current.sharedPicture.bounds.right != established.right ||
        current.sharedPicture.bounds.bottom != established.bottom ||
        current.pictureTop != established.top ||
        current.pictureBottom != established.bottom ||
        !current.text.detected || current.text.held || current.text.workLimit ||
        !current.text.nearBarEligibilityMeasured || !current.text.nearBarEligible ||
        !current.text.anchor.Valid())
        return observed;

    const int step = std::max(2, source.height / 540);
    const auto& anchor = current.text.anchor;
    const bool displacedTop = established.top > observed.proposedBounds.top + step;
    const bool displacedBottom = established.bottom < observed.proposedBounds.bottom - step;
    if (!((displacedTop && anchor.top < established.top &&
            anchor.bottom > established.top) ||
          (displacedBottom && anchor.top < established.bottom &&
            anchor.bottom > established.bottom)))
        return observed;

    auto strict = EvaluateMeasuredVerticalBarHypothesis(source, observed, established);
    if (strict.classification != ActivePictureClassification::BAR_CROP_TRUSTED)
    {
        // The earlier aligned-bar bootstrap can have one raw edge untrusted.
        // Reapply its same current-pixel proof, but only to the exact
        // established rectangle; a new geometry needs fresh acquisition.
        auto aligned = EvaluateSymmetricVerticalBarHypothesis(source, observed, true);
        if (aligned.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            aligned.trustedBounds.left == established.left &&
            aligned.trustedBounds.top == established.top &&
            aligned.trustedBounds.right == established.right &&
            aligned.trustedBounds.bottom == established.bottom)
            strict = aligned;
    }
    if (strict.classification == ActivePictureClassification::BAR_CROP_TRUSTED)
        strict.reason = "current worker glyph and strict native bars reaffirm picture";
    return strict;
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
	const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
	ActivePictureGlobalNearBlackEvidence result;
	if (!source.IsValid() || source.width < 16 || source.height < 16)
		return result;

	result.evaluated = true;
	SampleContext samples{ source };
    samples.budget = budget;
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
	result.lumaCutoff = source.range == AnalysisLumaRange::Full ?
		kFullRangeNearBlackP90 : kGlobalNearBlackP90;
	result.nearBlack = result.lumaP90 <= result.lumaCutoff;
	result.lumaSamples = samples.lumaSamples;
    CheckAnalysisEvaluationBudget(budget);
	return result;
}


ActivePictureArDarknessEvidence EvaluateActivePictureArDarkness(
	const AnalysisLumaSource& source, const ActivePictureEvidence& currentNative,
	const ActivePictureGlobalNearBlackEvidence& global,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
	ActivePictureArDarknessEvidence result;
	result.global = global;
	if (!global.evaluated || !global.nearBlack || !source.IsValid() ||
		currentNative.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE ||
		!currentNative.available ||
		currentNative.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
		currentNative.axisEvidence.vertical.state != ActivePictureAxisState::TRUSTED_BARS ||
		!currentNative.top.trusted || !currentNative.bottom.trusted)
		return result;

	const ActivePictureBounds& bounds = currentNative.trustedBounds;
	if (!IsValidBoundsForSource(bounds, source) ||
		(static_cast<unsigned>(bounds.trustedBarAxes) &
		static_cast<unsigned>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0 ||
		bounds.top < std::max(8, source.height / 64) ||
		source.height - bounds.bottom < std::max(8, source.height / 64) ||
		bounds.right - bounds.left < 64 || bounds.bottom - bounds.top < 64)
		return result;

	// Test both encoded bars away from their picture boundary. The raw native
	// detector already certified these edges; this small read only establishes
	// a current source-relative floor for the dark-picture distinction.
	SampleContext samples{ source };
    samples.budget = budget;
	std::vector<int> topBar, bottomBar;
	topBar.reserve(32);
	bottomBar.reserve(32);
	for (int row = 0; row < 2; ++row)
	{
		const int topY = ((row * 2 + 1) * bounds.top) / 4;
		const int bottomY = bounds.bottom +
			((row * 2 + 1) * (source.height - bounds.bottom)) / 4;
		for (int column = 0; column < 16; ++column)
		{
			const int x = static_cast<int>((int64_t(column * 2 + 1) * source.width) / 32);
			topBar.push_back(samples.Luma(x, topY));
			bottomBar.push_back(samples.Luma(x, bottomY));
		}
	}
	result.lumaSamples = samples.lumaSamples;
	const double topFloor = Percentile(topBar, 0.50);
	const double bottomFloor = Percentile(bottomBar, 0.50);
	result.barFloor = std::max(topFloor, bottomFloor);
	if (std::abs(topFloor - bottomFloor) > 8.0 ||
		Percentile(topBar, 0.90) > topFloor + 8.0 ||
		Percentile(bottomBar, 0.90) > bottomFloor + 8.0)
		return result;

	// Exclude picture edges and subtitle zones. A few bright glyphs or stars
	// can increase a global percentile, but cannot create texture across most
	// independent interior tiles or a broad low-frequency scene structure.
	const int pictureWidth = bounds.right - bounds.left;
	const int pictureHeight = bounds.bottom - bounds.top;
	const int innerLeft = bounds.left + pictureWidth / 20;
	const int innerRight = bounds.right - pictureWidth / 20;
	const int innerTop = bounds.top + pictureHeight / 10;
	const int innerBottom = bounds.bottom - pictureHeight / 10;
	const int innerWidth = innerRight - innerLeft;
	const int innerHeight = innerBottom - innerTop;
	if (innerWidth < 32 || innerHeight < 32) return result;

	std::vector<int> picture;
	std::vector<int> tileMedians;
	picture.reserve(4 * 4 * 6 * 6);
	tileMedians.reserve(16);
	bool texturedQuadrant[4] = {};
	for (int tileY = 0; tileY < 4; ++tileY)
	{
		for (int tileX = 0; tileX < 4; ++tileX)
		{
			std::vector<int> tile;
			tile.reserve(36);
			for (int sampleY = 0; sampleY < 6; ++sampleY)
			{
				const int gridY = tileY * 6 + sampleY;
				const int y = innerTop + static_cast<int>(
					(int64_t(gridY * 2 + 1) * innerHeight) / 48);
				for (int sampleX = 0; sampleX < 6; ++sampleX)
				{
					const int gridX = tileX * 6 + sampleX;
					const int x = innerLeft + static_cast<int>(
						(int64_t(gridX * 2 + 1) * innerWidth) / 48);
					const int luma = samples.Luma(x, y);
					tile.push_back(luma);
					picture.push_back(luma);
				}
			}
			tileMedians.push_back(static_cast<int>(Percentile(tile, 0.50)));
			if (Percentile(tile, 0.90) - Percentile(tile, 0.10) >= 12.0)
			{
				++result.texturedTiles;
				texturedQuadrant[(tileY / 2) * 2 + tileX / 2] = true;
			}
		}
	}
	result.lumaSamples = samples.lumaSamples;
	for (bool textured : texturedQuadrant) result.texturedQuadrants += textured ? 1 : 0;
	result.pictureMedian = Percentile(picture, 0.50);
	result.tileMedianSpread = Percentile(tileMedians, 0.90) -
		Percentile(tileMedians, 0.10);
	result.distributedDarkPicture =
		result.pictureMedian >= result.barFloor + 32.0 &&
		result.texturedTiles >= 12 && result.texturedQuadrants == 4 &&
		result.tileMedianSpread >= 16.0;
    CheckAnalysisEvaluationBudget(budget);
	return result;
}

ActivePictureArDarknessEvidence EvaluateActivePictureArDarkness(
	const AnalysisLumaSource& source, const ActivePictureEvidence& currentNative,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
	return EvaluateActivePictureArDarkness(source, currentNative,
		EvaluateActivePictureGlobalNearBlack(source,budget),budget);
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
	const ActivePictureBounds& trustedPresentation,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
    const auto raw = ExtractActivePictureEvidence(source,budget);
    ActivePictureGlobalNearBlackEvidence global;
    ActivePictureArDarknessEvidence arDarkness;
    if (source.IsValid() && source.width >= 16 && source.height >= 16 &&
        IsValidBoundsForSource(trustedPresentation,source))
    {
        global = EvaluateActivePictureGlobalNearBlack(source,budget);
        arDarkness = EvaluateActivePictureArDarkness(source,raw,global,budget);
    }
    return EvaluateActivePicturePresentationRetention(
        source,trustedPresentation,raw,global,arDarkness,budget);
}

ActivePicturePresentationRetentionEvidence EvaluateActivePicturePresentationRetention(
    const AnalysisLumaSource& source, const ActivePictureBounds& trustedPresentation,
    const ActivePictureEvidence& raw,
    const ActivePictureGlobalNearBlackEvidence& global,
    const ActivePictureArDarknessEvidence& arDarkness,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
	ActivePicturePresentationRetentionEvidence result;
	result.activePicture = raw;
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

	result.globalLumaP90 = global.lumaP90;
	result.globalNearBlack = global.nearBlack;
	result.lumaSamples += global.lumaSamples;
	result.distributedDarkPicture = arDarkness.distributedDarkPicture;
	result.lumaSamples += arDarkness.lumaSamples;

	SampleContext samples{ source };
    samples.budget = budget;

    result.blackReference = result.activePicture.blackReference;
    const int blackFloor = result.blackReference.floor;
    const int blackThreshold = result.blackReference.threshold;
	result.presentationBlackFloor = blackFloor;
	result.presentationBlackThreshold = blackThreshold;

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
		result.proposedBoundsAvailable && !result.EffectiveArNearBlack() &&
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
		result.proposedBoundsAvailable && !result.EffectiveArNearBlack() &&
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
		 result.EffectiveArNearBlack() || geometryUnavailable);
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
	else if (result.EffectiveArNearBlack())
		result.reason = "valid global near-black frame is pixel-safe without geometry";
	else if (geometryUnavailable)
		result.reason = "current excluded bands remain pixel-safe despite unavailable geometry";
	else
		result.reason = "non-contained active-picture evidence rejects retention";
    CheckAnalysisEvaluationBudget(budget);
	return result;
}



bool ResampleActivePictureExpansionStrips(
	const AnalysisLumaSource& source,
	const ActivePictureBounds& trustedPresentation,
	const ActivePictureBounds& candidate,
	ActivePicturePresentationRetentionEvidence& currentRetention)
{
	if (!source.IsValid() || source.width < 16 || source.height < 16 ||
		!IsValidBoundsForSource(trustedPresentation, source) ||
		!IsValidBoundsForSource(candidate, source) ||
		trustedPresentation.trustedBarAxes == ActivePictureBounds::BarAxes::NONE ||
		!currentRetention.analysisValid || !currentRetention.presentationValid ||
		!currentRetention.expansionStripsAvailable ||
		!IsValidBoundsForSource(currentRetention.expansionBase, source) ||
		currentRetention.expansionBase.left != trustedPresentation.left ||
		currentRetention.expansionBase.top != trustedPresentation.top ||
		currentRetention.expansionBase.right != trustedPresentation.right ||
		currentRetention.expansionBase.bottom != trustedPresentation.bottom ||
		currentRetention.presentationBlackFloor < 0 ||
		currentRetention.presentationBlackFloor > 80 ||
		currentRetention.presentationBlackThreshold < currentRetention.presentationBlackFloor ||
		currentRetention.presentationBlackThreshold > 104)
		return false;

	SampleContext samples{source};
	const int blackFloor = currentRetention.presentationBlackFloor;
	const int blackThreshold = currentRetention.presentationBlackThreshold;
	const auto top = InspectHorizontalEdge(samples, true,
		std::max(0, trustedPresentation.top - candidate.top),
		trustedPresentation.top, blackFloor, blackThreshold, candidate.top);
	const auto bottom = InspectHorizontalEdge(samples, false,
		std::max(0, candidate.bottom - trustedPresentation.bottom),
		trustedPresentation.bottom, blackFloor, blackThreshold,
		source.height - candidate.bottom);
	const auto left = InspectVerticalEdge(samples, true,
		std::max(0, trustedPresentation.left - candidate.left),
		trustedPresentation.left, blackFloor, blackThreshold, candidate.left);
	const auto right = InspectVerticalEdge(samples, false,
		std::max(0, candidate.right - trustedPresentation.right),
		trustedPresentation.right, blackFloor, blackThreshold,
		source.width - candidate.right);
	currentRetention.expansionStripsAvailable = true;
	currentRetention.expansionBase = trustedPresentation;
	currentRetention.expansionCandidate = candidate;
	currentRetention.expandingTop = top;
	currentRetention.expandingBottom = bottom;
	currentRetention.expandingLeft = left;
	currentRetention.expandingRight = right;
	currentRetention.lumaSamples += samples.lumaSamples;
	currentRetention.chromaSamples += samples.chromaSamples;
	return true;
}
ActivePicturePresentationRetentionEvidence EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(
	const AnalysisLumaSource& source,
	const ActivePictureBounds& trustedPresentation,
	const ActivePicturePresentationRetentionEvidence& raw,
	const ActivePictureBounds& erasedSourceBounds,
	int relocatedGlyphHeight)
{
	// Most frames have complete capture. Pay for native residual inspection
	// only when the inexpensive ordinary bar proof still finds content.
	if (relocatedGlyphHeight > 0)
	{
		const auto strict = EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(
			source, trustedPresentation, raw, erasedSourceBounds, 0);
		if (!strict.relocatedOverlayExclusionEvaluated || strict.excludedBandsPixelSafe)
			return strict;
	}
	auto result = raw;
	result.relocatedOverlayExclusionEvaluated = false;
	result.relocatedOverlayResidualsExcluded = false;
	if (!raw.analysisValid || !raw.presentationValid || !source.IsValid() ||
		!IsValidBoundsForSource(trustedPresentation, source) ||
		!IsValidBoundsForSource(erasedSourceBounds, source) ||
		raw.presentationBlackThreshold <= 0 ||
		raw.excludedTop.barPixels != trustedPresentation.top ||
		raw.excludedBottom.barPixels != source.height - trustedPresentation.bottom ||
		raw.excludedLeft.barPixels != trustedPresentation.left ||
		raw.excludedRight.barPixels != source.width - trustedPresentation.right ||
		erasedSourceBounds.right <= trustedPresentation.left ||
		erasedSourceBounds.left >= trustedPresentation.right)
		return result;
	const bool touchesTop = erasedSourceBounds.top < trustedPresentation.top;
	const bool touchesBottom = erasedSourceBounds.bottom > trustedPresentation.bottom;
	if (!touchesTop && !touchesBottom) return result;
	SampleContext samples{ source };
	samples.erasedOverlay = &erasedSourceBounds;
	samples.presentation = &trustedPresentation;
	samples.erasedBlackFloor = raw.presentationBlackFloor;
	const int blackThreshold = raw.presentationBlackThreshold;
	// Presentation safety may tolerate a few missed terminal glyph pixels after
	// successful relocation. Prove their native-pixel shape and opaque surrounds;
	// never erase them here and never change raw picture/transition evidence.
	std::vector<uint8_t> residual;
	if (relocatedGlyphHeight > 0 && relocatedGlyphHeight <= 256)
	{
		const int g = relocatedGlyphHeight;
		ActivePictureBounds halo = erasedSourceBounds;
		halo.left = std::max(trustedPresentation.left, halo.left - 3*g);
		halo.right = std::min(trustedPresentation.right, halo.right + 3*g);
		halo.top = std::max(0, halo.top - std::max(1,g/4));
		halo.bottom = std::min(source.height, halo.bottom + std::max(1,g/4));
		const int hw = halo.right-halo.left, hh = halo.bottom-halo.top;
		// Keep adversarial/invalid cue unions from creating an unbounded pass.
		if (hw > 0 && hh > 0 && size_t(hw)*hh <= 2*1024*1024)
		{
			residual.assign(size_t(hw)*hh,0);
			bool supported = true;
			size_t inspected = 0, occupied = 0;
			const size_t maxInk = size_t(std::min(2048,std::max(12,g*g/2)));
			for (int y=halo.top; y<halo.bottom && supported; ++y)
				for (int x=halo.left; x<halo.right; ++x)
				{
					if ((y >= trustedPresentation.top && y < trustedPresentation.bottom) ||
						samples.IsErasedOverlay(x,y)) continue;
					AnalysisLumaSample pixel;
					if (!source.Sample(x,y,pixel)) { supported=false; break; }
					++inspected; ++samples.lumaSamples; ++samples.chromaSamples;
					if (std::abs(int(pixel.chromaU)-512)>32 ||
						std::abs(int(pixel.chromaV)-512)>32) { supported=false; break; }
					if (pixel.luma <= blackThreshold) continue;
					// A component touching the proof perimeter or live picture edge
					// is unbounded content, not an isolated missed mark.
					if (x==halo.left || x==halo.right-1 || y==halo.top || y==halo.bottom-1 ||
						y==trustedPresentation.top-1 || y==trustedPresentation.bottom ||
						++occupied>maxInk) { supported=false; break; }
					residual[size_t(y-halo.top)*hw+x-halo.left]=1;
				}
			if (!inspected || !occupied || occupied*100>inspected*3) supported=false;
			std::vector<size_t> flood;
			unsigned components=0;
			for (size_t origin=0; origin<residual.size() && supported; ++origin)
			{
				if (residual[origin]!=1) continue;
				if (++components>8) { supported=false; break; }
				flood.clear(); flood.push_back(origin); residual[origin]=2;
				int minX=int(origin%hw), maxX=minX, minY=int(origin/hw), maxY=minY;
				for (size_t q=0;q<flood.size();++q)
				{
					const int x=int(flood[q]%hw),y=int(flood[q]/hw);
					minX=std::min(minX,x);maxX=std::max(maxX,x);
					minY=std::min(minY,y);maxY=std::max(maxY,y);
					for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
					{
						const int nx=x+dx,ny=y+dy;
						if(nx<0 || nx>=hw || ny<0 || ny>=hh)continue;
						const size_t next=size_t(ny)*hw+nx;
						if(residual[next]==1){residual[next]=2;flood.push_back(next);}
					}
				}
				// Tall narrow quotes/diacritics are valid; broad blocks are not.
				if(maxX-minX+1>std::max(2,g/3+1) ||
					maxY-minY+1>std::max(2,g*2/3+1)) supported=false;
			}
			if (supported)
			{
				samples.residualOverlay=&residual;
				samples.residualBounds=halo;
				result.relocatedOverlayResidualsExcluded=true;
			}
		}
	}
	if (touchesTop)
	{
		result.excludedTop = InspectHorizontalEdge(samples, true,
			trustedPresentation.top, trustedPresentation.top,
			raw.presentationBlackFloor, blackThreshold);
		result.visibleTop = FindHorizontalVisibleExtent(samples, true,
			trustedPresentation.top, blackThreshold);
		result.visibleTop.presentationMargin = std::max(2, source.height / 180);
	}
	if (touchesBottom)
	{
		result.excludedBottom = InspectHorizontalEdge(samples, false,
			source.height - trustedPresentation.bottom, trustedPresentation.bottom,
			raw.presentationBlackFloor, blackThreshold);
		result.visibleBottom = FindHorizontalVisibleExtent(samples, false,
			source.height - trustedPresentation.bottom, blackThreshold);
		result.visibleBottom.presentationMargin = std::max(2, source.height / 180);
	}
	result.outwardVisibleBoundsAvailable = false;
	result.outwardVisibleBounds = {};
	const bool unsafeTop = !ExcludedBandPixelsAreSafe(result.excludedTop) ||
		result.visibleTop.available;
	const bool unsafeBottom = !ExcludedBandPixelsAreSafe(result.excludedBottom) ||
		result.visibleBottom.available;
	const bool unsafeLeft = !ExcludedBandPixelsAreSafe(result.excludedLeft) ||
		result.visibleLeft.available;
	const bool unsafeRight = !ExcludedBandPixelsAreSafe(result.excludedRight) ||
		result.visibleRight.available;
	result.excludedHorizontalBandsPixelSafe = !unsafeLeft && !unsafeRight;
	result.excludedVerticalBandsPixelSafe = !unsafeTop && !unsafeBottom;
	result.excludedBandsPixelSafe =
		!unsafeLeft && !unsafeTop && !unsafeRight && !unsafeBottom;
	if (!result.excludedBandsPixelSafe)
	{
		// Every unsafe edge must be bounded. Otherwise fail open exactly as
		// before; a partial estimate must never hide unmeasured live pixels.
		const bool allUnsafeEdgesBounded = (!unsafeTop || result.visibleTop.available) &&
			(!unsafeBottom || result.visibleBottom.available) &&
			(!unsafeLeft || result.visibleLeft.available) &&
			(!unsafeRight || result.visibleRight.available);
		if (allUnsafeEdgesBounded)
		{
			const int verticalMargin = std::max(2, source.height / 180);
			const int horizontalMargin = std::max(2, source.width / 180);
			result.outwardVisibleBounds = trustedPresentation;
			if (unsafeTop)
				result.outwardVisibleBounds.top = std::max(
					0, result.visibleTop.coordinate - verticalMargin);
			if (unsafeBottom)
				result.outwardVisibleBounds.bottom = std::min(source.height,
					result.visibleBottom.coordinate + verticalMargin);
			if (unsafeLeft)
				result.outwardVisibleBounds.left = std::max(
					0, result.visibleLeft.coordinate - horizontalMargin);
			if (unsafeRight)
				result.outwardVisibleBounds.right = std::min(source.width,
					result.visibleRight.coordinate + horizontalMargin);
			result.outwardVisibleBounds.aspectRatio = static_cast<double>(
				result.outwardVisibleBounds.right - result.outwardVisibleBounds.left) /
				std::max(1, result.outwardVisibleBounds.bottom -
					result.outwardVisibleBounds.top);
			result.outwardVisibleBounds.trustedBarAxes =
				ActivePictureBounds::BarAxes::NONE;
			result.outwardVisibleBoundsAvailable = true;
		}
	}
	const bool geometryUnavailable = !result.activePicture.available &&
		result.activePicture.classification == ActivePictureClassification::UNAVAILABLE;
	result.currentlyPixelSafe = result.excludedBandsPixelSafe &&
		(result.proposedBoundsContained || result.samplingReaffirmed || result.partialSamplingReaffirmed ||
		 result.EffectiveArNearBlack() || geometryUnavailable);
	result.relocatedOverlayExclusionEvaluated = true;
	result.lumaSamples += samples.lumaSamples;
	result.chromaSamples += samples.chromaSamples;
	result.reason = result.excludedBandsPixelSafe
		? "relocated overlay excluded; remaining presentation bands are pixel-safe"
		: "content outside relocated overlay still requires presentation protection";
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
        axes.verticalCropGuardTop < 0 || axes.verticalCropGuardTop > 2 * std::max(2, source.height / 540) ||
        axes.verticalCropGuardBottom < 0 || axes.verticalCropGuardBottom > 2 * std::max(2, source.height / 540) ||
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
		!retention.EffectiveArNearBlack() || !evidence.available ||
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
    const ActivePictureEvidence& raw, const ActivePictureBounds& base,
    const AnalysisEvaluationBudget* budget)
{
    CheckAnalysisEvaluationBudget(budget);
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
        if ((result.samples & 63u) == 0) CheckAnalysisEvaluationBudget(budget);
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
    CheckAnalysisEvaluationBudget(budget);
    return result;
}
