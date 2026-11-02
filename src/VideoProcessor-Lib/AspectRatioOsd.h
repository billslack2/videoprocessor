#pragma once

#include <ActivePictureTransitionModel.h>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>
#include <limits>

enum class PhysicalAspectConfidence : uint32_t { UNKNOWN, STRONG_ABRUPT_EDGE, SOURCE_RASTER_EXTENT, SOURCE_CANVAS_ESTIMATE };

enum class PictureAspectUse : uint32_t { UNAVAILABLE, STARTUP_FALLBACK, DETECTED, HELD, PRESENTATION };

// Presentation-independent detection history. This is an anchored measured
// estimate, never current raw evidence or permission to crop/render a frame.
struct StabilizedPictureAspect
{
    bool available = false, held = false;
    double ratio = 0.0;
    uint64_t sourceGeneration = 0, acceptedSequence = 0;
    bool physicalBoundsAvailable = false;
    int physicalBounds[4] = {};
    PhysicalAspectConfidence confidence = PhysicalAspectConfidence::UNKNOWN;
    ActivePictureDetectorKind detectorKind = ActivePictureDetectorKind::Legacy;
};

struct DetectedPictureAspect
{
	uint64_t sourceGeneration = 0;
	uint64_t publicationGeneration = 0;
	double ratio = 0.0;
	bool available = false;
    bool provisional = false;
    double inUseRatio = 0.0;
    bool inUseAvailable = false;
    PictureAspectUse inUseState = PictureAspectUse::UNAVAILABLE;
    uint64_t detectedSequence = 0, presentedSequence = 0;
    bool physicalEvaluated = false;
    PhysicalAspectConfidence physicalConfidence = PhysicalAspectConfidence::UNKNOWN;
    int physicalBounds[4] = {};
    bool physicalBoundsAvailable = false;
    ActivePictureDetectorKind detectorKind = ActivePictureDetectorKind::Legacy;
    StabilizedPictureAspect stableDetection;
};

// Presentation-only labels for the VP Renderer Ctrl+I OSD.
class AspectRatioOsd
{
public:
	struct Label
	{
		std::wstring ratio = L"---";
		std::wstring name = L"Unknown";
		int target = -1;
	};

    struct Rows { std::wstring inUse, detected, retained = L"Unavailable"; };
    // Raw observations remain exact; the separate detection estimate has its own anchored history.
    Rows Update(const DetectedPictureAspect& reading) { return Format(reading); }
    void Reset() {}

    static std::wstring Named(double ratio, const wchar_t* status = nullptr)
    {
        if (!std::isfinite(ratio) || ratio <= 0.0) return L"Unavailable";
        const auto label = Classify(ratio);
        return label.ratio + L" (" + label.name +
            (status ? std::wstring(L"; ") + status : std::wstring{}) + L")";
    }
    static Rows Format(const DetectedPictureAspect& reading)
    {
        Rows rows{L"Unavailable", L"Unavailable"};
        if (reading.inUseAvailable) {
            const wchar_t* status = nullptr;
            if (reading.inUseState == PictureAspectUse::HELD) status = L"held";
            else if (reading.inUseState == PictureAspectUse::PRESENTATION) status = L"presentation";
            rows.inUse = Named(reading.inUseRatio, status);
        }
        const bool spatial = reading.detectorKind != ActivePictureDetectorKind::Legacy;
        if (reading.physicalEvaluated &&
            reading.physicalConfidence == PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE) {
            rows.detected = L"Canvas estimate (blank margins unresolved; source spatial experimental)";
        } else if (reading.available && !reading.provisional) {
            wchar_t numeric[64] = {};
            swprintf_s(numeric, L"%.2f:1", reading.ratio);
            rows.detected = numeric;
            if (reading.physicalBoundsAvailable) {
                wchar_t geometry[128] = {};
                swprintf_s(geometry, L" [%d,%d..%d,%d; %ls]",
                    reading.physicalBounds[0], reading.physicalBounds[1],
                    reading.physicalBounds[2], reading.physicalBounds[3],
                    reading.physicalConfidence == PhysicalAspectConfidence::SOURCE_RASTER_EXTENT ? L"source extent" : L"strong edge");
                rows.detected += geometry;
            }
            if (spatial) rows.detected += L" (source spatial experimental)";
        } else if (reading.physicalEvaluated)
            rows.detected = L"Unknown (boundaries unresolved)";
        else if (reading.provisional)
            rows.detected = L"Unknown (awaiting physical measurement)";
        if (spatial && !reading.available &&
            reading.physicalConfidence != PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE)
            rows.detected = reading.physicalEvaluated ?
                L"Unknown (boundaries unresolved; source spatial experimental)" :
                L"Pending (source spatial experimental)";
        // The producing mode belongs to the current observation, not to its
        // retained predecessor. Missing current evidence stays visible.
        if (!spatial && (reading.physicalEvaluated || reading.provisional))
            rows.detected += L" (legacy)";
        const auto& stable = reading.stableDetection;
        if (stable.available && std::isfinite(stable.ratio) && stable.ratio > 0.0) {
            wchar_t numeric[64] = {};
            swprintf_s(numeric, L"%.2f:1", stable.ratio);
            rows.retained = numeric;
            if (stable.physicalBoundsAvailable) {
                wchar_t geometry[128] = {};
                swprintf_s(geometry, L" [%d,%d..%d,%d; stable]", stable.physicalBounds[0],
                    stable.physicalBounds[1], stable.physicalBounds[2], stable.physicalBounds[3]);
                rows.retained += geometry;
            } else rows.retained += L" [stable]";
            if (stable.detectorKind != ActivePictureDetectorKind::Legacy)
                rows.retained += L" (source spatial experimental)";
            // Observe() also supports legacy non-physical consumers.
            if (!spatial && !reading.physicalEvaluated && !reading.provisional)
                rows.detected += L" (legacy)";
            rows.detected = L"Current: " + rows.detected + L"; retained: " + rows.retained;
        }
        return rows;
    }

	static Label Classify(double ratio)
	{
		if (!std::isfinite(ratio) || ratio <= 0.0)
			return {};
		int best = -1;
		double bestDifference = 1.0;
		const auto* targets = Targets();
		for (int i = 0; i < 14; ++i)
		{
			const double difference =
				std::abs(ratio - targets[i].value) / targets[i].value;
			// Strict comparison breaks exact ties toward the lower target.
			if (difference <= 0.01 + 1e-12 &&
				difference < bestDifference - 1e-12)
			{
				best = i;
				bestDifference = difference;
			}
		}
		if (best >= 0)
			return { targets[best].display, targets[best].name, best };
		wchar_t numeric[32] = {};
		swprintf_s(numeric, L"%.2f", ratio);
		std::wstring formatted(numeric);
		return { formatted + L":1", L"nonstandard", -1 };
	}

private:
	struct Target
	{
		double value;
		const wchar_t* display;
		const wchar_t* name;
	};
	static const Target* Targets()
	{
		static const Target targets[] = {
			{ 4.0 / 3.0, L"4:3", L"TV" },
			{ 1.37, L"1.37:1", L"Academy" },
			{ 1.43, L"1.43:1", L"IMAX 70mm" },
			{ 1.66, L"1.66:1", L"European widescreen" },
			{ 16.0 / 9.0, L"16:9", L"HDTV" },
			{ 1.85, L"1.85:1", L"Flat" },
			{ 1.90, L"1.90:1", L"Digital IMAX" },
			{ 2.00, L"2.00:1", L"Univisium" },
			{ 2.20, L"2.20:1", L"70mm / Todd-AO" },
			{ 21.0 / 9.0, L"21:9", L"Consumer ultrawide" },
			{ 2.35, L"2.35:1", L"CinemaScope" },
			{ 2.39, L"2.39:1", L"Scope" },
			{ 2.40, L"2.40:1", L"Scope" },
			{ 2.76, L"2.76:1", L"Ultra Panavision 70" }
		};
		return targets;
	}

};

// Telemetry only: this never selects or holds a renderer crop. Used geometry is
// updated solely after successful submission. Stable detection never feeds geometry.
class PictureAspectTelemetry
{
public:
    void Source(uint64_t generation)
    {
        if (generation != m_value.sourceGeneration) {
            m_value = {};
            m_value.sourceGeneration = generation;
            m_seenDetection = false;
            m_stableLastObservedSequence = 0;
            m_stableLastQualifiedSequence = 0;
        }
    }
    // Legacy qualified observation. Provisional nominations are not physical measurements.
    void Observe(uint64_t generation, uint64_t sequence, double ratio,
        bool available, bool provisional)
    {
        Source(generation);
        m_value.detectedSequence = sequence;
        m_value.available = available && !provisional && std::isfinite(ratio) && ratio > 0.0;
        m_value.ratio = m_value.available ? ratio : 0.0;
        m_value.provisional = provisional;
        m_value.physicalEvaluated = false;
        m_value.physicalConfidence = PhysicalAspectConfidence::UNKNOWN;
        m_value.physicalBoundsAvailable = false;
        m_value.detectorKind = ActivePictureDetectorKind::Legacy;
        for (auto& coordinate : m_value.physicalBounds) coordinate = 0;
        ObserveStableDetection(sequence);
    }
    void ObservePhysical(uint64_t generation, uint64_t sequence,
        bool evaluated, bool estimateAvailable, PhysicalAspectConfidence confidence,
        int left, int top, int right, int bottom,
        ActivePictureDetectorKind detectorKind = ActivePictureDetectorKind::Legacy)
    {
        Source(generation);
        m_value.detectedSequence = sequence;
        m_value.physicalEvaluated = evaluated;
        m_value.detectorKind = detectorKind;
        m_value.physicalConfidence = confidence;
        m_value.available = evaluated && estimateAvailable &&
            (confidence == PhysicalAspectConfidence::STRONG_ABRUPT_EDGE ||
             confidence == PhysicalAspectConfidence::SOURCE_RASTER_EXTENT) &&
            left >= 0 && top >= 0 && right > left && bottom > top;
        m_value.provisional = false;
        m_value.ratio = m_value.available ? double(right-left) / double(bottom-top) : 0.0;
        m_value.physicalBoundsAvailable = m_value.available;
        m_value.physicalBounds[0] = m_value.available ? left : 0;
        m_value.physicalBounds[1] = m_value.available ? top : 0;
        m_value.physicalBounds[2] = m_value.available ? right : 0;
        m_value.physicalBounds[3] = m_value.available ? bottom : 0;
        if (!m_value.available && !(evaluated && confidence == PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE))
            m_value.physicalConfidence = PhysicalAspectConfidence::UNKNOWN;
        ObserveStableDetection(sequence);
    }
    void ClearDetection()
    {
        Observe(m_value.sourceGeneration, m_value.detectedSequence, 0.0, false, false);
        m_value.stableDetection={};
        m_stableLastObservedSequence=0;
        m_stableLastQualifiedSequence=0;
    }
    void Submitted(uint64_t generation, uint64_t sequence, double ratio,
        double nativeRatio, bool acceptedPicture, bool succeeded)
    {
        Source(generation);
        if (!succeeded || !std::isfinite(ratio) || ratio <= 0.0) return;
        const bool sameAsLast = m_value.inUseAvailable && Same(ratio, m_value.inUseRatio);
        const bool sameAsLive = m_value.available && !m_value.provisional && Same(ratio,m_value.ratio);
        m_seenDetection = m_seenDetection || acceptedPicture || sameAsLive;
        m_value.inUseState = !m_seenDetection && Same(ratio,nativeRatio)
            ? PictureAspectUse::STARTUP_FALLBACK
            : sameAsLive ? PictureAspectUse::DETECTED
            : sameAsLast ? PictureAspectUse::HELD : PictureAspectUse::PRESENTATION;
        m_value.inUseAvailable = true;
        m_value.inUseRatio = ratio;
        m_value.presentedSequence = sequence;
        ++m_value.publicationGeneration;
    }
    void ClearPresentation() { m_value.inUseAvailable = false; m_value.inUseState = PictureAspectUse::UNAVAILABLE; }
    const DetectedPictureAspect& Read() const { return m_value; }
private:
    void ObserveStableDetection(uint64_t sequence)
    {
        auto& stable=m_value.stableDetection;
        if(stable.available)stable.held=true;
        const bool newest=sequence && sequence>=m_stableLastObservedSequence;
        if(sequence>m_stableLastObservedSequence)m_stableLastObservedSequence=sequence;
        // The renderer can publish UNKNOWN before completing this same source's
        // physical measurement. Permit its first qualifying completion, while
        // rejecting older sources and every second qualified interpretation.
        // Unknown duplicates never reopen a previously consumed qualification.
        if(!newest || sequence==m_stableLastQualifiedSequence || !m_value.sourceGeneration ||
            !m_value.available || m_value.provisional || !std::isfinite(m_value.ratio) || m_value.ratio<=0.0)return;
        m_stableLastQualifiedSequence=sequence; // Consume even an in-band hold.
        constexpr double deadband=0.03;
        const double tolerance=8*std::numeric_limits<double>::epsilon();
        if(stable.available && std::abs(m_value.ratio-stable.ratio)/stable.ratio<=deadband+tolerance)
            return; // Always compare to the accepted anchor; never walk/average.
        stable={};stable.available=true;stable.ratio=m_value.ratio;
        stable.sourceGeneration=m_value.sourceGeneration;stable.acceptedSequence=sequence;
        stable.physicalBoundsAvailable=m_value.physicalBoundsAvailable;
        for(unsigned i=0;i<4;++i)stable.physicalBounds[i]=m_value.physicalBounds[i];
        stable.confidence=m_value.physicalConfidence;stable.detectorKind=m_value.detectorKind;
    }
    static bool Same(double a, double b) { return std::abs(a-b) <= 0.0005; }
    DetectedPictureAspect m_value;
    bool m_seenDetection = false;
    uint64_t m_stableLastObservedSequence = 0;
    uint64_t m_stableLastQualifiedSequence = 0;
};
