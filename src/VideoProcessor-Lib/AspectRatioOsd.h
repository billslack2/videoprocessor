#pragma once

#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>

enum class PictureAspectUse : uint32_t { UNAVAILABLE, STARTUP_FALLBACK, DETECTED, HELD, PRESENTATION };

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

    struct Rows { std::wstring inUse, detected; };
    // UI-only deadband, anchored to the last displayed measurement so a slow
    // cumulative drift cannot remain hidden. Raw and submitted telemetry stay exact.
    Rows Update(const DetectedPictureAspect& reading)
    {
        if (m_sourceGeneration != reading.sourceGeneration) {
            Reset();
            m_sourceGeneration = reading.sourceGeneration;
        }
        auto displayed = reading;
        if (!reading.available || !std::isfinite(reading.ratio) || reading.ratio <= 0.0) {
            m_displayedDetection = 0.0;
            displayed.available = false;
        } else {
            if (m_displayedDetection <= 0.0 ||
                std::abs(reading.ratio - m_displayedDetection) + 1e-12 >= 0.01 * m_displayedDetection)
                m_displayedDetection = reading.ratio;
            displayed.ratio = m_displayedDetection;
        }
        return Format(displayed);
    }
    void Reset() { m_sourceGeneration = 0; m_displayedDetection = 0.0; }

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
        if (reading.available)
            rows.detected = Named(reading.ratio, reading.provisional ? L"provisional" : nullptr);
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
    uint64_t m_sourceGeneration = 0;
    double m_displayedDetection = 0.0;
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
// updated solely after successful submission; live evidence has no UI dwell.
class PictureAspectTelemetry
{
public:
    void Source(uint64_t generation)
    {
        if (generation != m_value.sourceGeneration) {
            m_value = {};
            m_value.sourceGeneration = generation;
            m_seenDetection = false;
        }
    }
    void Observe(uint64_t generation, uint64_t sequence, double ratio,
        bool available, bool provisional)
    {
        Source(generation);
        m_value.detectedSequence = sequence;
        m_value.available = available && std::isfinite(ratio) && ratio > 0.0;
        m_value.ratio = m_value.available ? ratio : 0.0;
        m_value.provisional = m_value.available && provisional;
    }
    void ClearDetection()
    { m_value.available = m_value.provisional = false; m_value.ratio = 0.0; }
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
    static bool Same(double a, double b) { return std::abs(a-b) <= 0.0005; }
    DetectedPictureAspect m_value;
    bool m_seenDetection = false;
};
