#pragma once

#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>

struct DetectedPictureAspect
{
	uint64_t sourceGeneration = 0;
	uint64_t publicationGeneration = 0;
	double ratio = 0.0;
	bool available = false;
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

	void Reset()
	{
		m_sourceGeneration = 0;
		m_hasSource = false;
		m_label = {};
		m_candidate = {};
		m_candidateSinceMs = 0;
		m_candidatePublication = 0;
		m_unavailableSinceMs = 0;
	}

	const Label& Update(const DetectedPictureAspect& reading, uint64_t nowMs)
	{
		if (!m_hasSource ||
			(reading.sourceGeneration != 0 &&
			 m_sourceGeneration != reading.sourceGeneration))
		{
			Reset();
			m_hasSource = true;
			m_sourceGeneration = reading.sourceGeneration;
		}
		if (!reading.available || !std::isfinite(reading.ratio) ||
			reading.ratio <= 0.0)
		{
			m_candidateSinceMs = 0;
			// Fullscreen/windowed handoff can temporarily withdraw the detector
			// publication. Keep the last trusted display for a bounded interval
			// while the same source is reacquired.
			if (m_unavailableSinceMs == 0)
				m_unavailableSinceMs = nowMs;
			if (m_label.ratio != L"---" &&
				nowMs - m_unavailableSinceMs < 10000)
				return m_label;
			m_label = {};
			return m_label;
		}
		m_unavailableSinceMs = 0;
		const Label incoming = Classify(reading.ratio);
		if (m_label.ratio == L"---")
		{
			m_label = incoming;
			return m_label;
		}
		if (incoming.ratio == m_label.ratio &&
			incoming.name == m_label.name)
		{
			m_candidateSinceMs = 0;
			return m_label;
		}

		// The detector has already committed a stable rectangle. Publish clear
		// changes at once; only nearby competing labels need extra dwell.
		const bool ambiguous = m_label.target >= 0 &&
			std::abs(reading.ratio - Targets()[m_label.target].value) /
				Targets()[m_label.target].value <= 0.03 + 1e-12;
		if (!ambiguous)
		{
			m_label = incoming;
			m_candidateSinceMs = 0;
			return m_label;
		}
		if (incoming.ratio != m_candidate.ratio ||
			incoming.name != m_candidate.name ||
			reading.publicationGeneration != m_candidatePublication)
		{
			m_candidate = incoming;
			m_candidatePublication = reading.publicationGeneration;
			m_candidateSinceMs = nowMs;
		}
		if (nowMs - m_candidateSinceMs >= 5000)
		{
			m_label = incoming;
			m_candidateSinceMs = 0;
		}
		return m_label;
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
		swprintf_s(numeric, L"%.3f", ratio);
		std::wstring formatted(numeric);
		while (formatted.size() > 4 && formatted.back() == L'0')
			formatted.pop_back();
		return { formatted + L":1", L"Unknown", -1 };
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

	uint64_t m_sourceGeneration = 0;
	bool m_hasSource = false;
	Label m_label;
	Label m_candidate;
	uint64_t m_candidatePublication = 0;
	uint64_t m_candidateSinceMs = 0;
	uint64_t m_unavailableSinceMs = 0;
};
