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
	enum class ReadStatus
	{
		NEVER,
		AVAILABLE,
		UNAVAILABLE,
		INVALID,
		BUSY
	};

	enum class LabelStatus
	{
		UNKNOWN,
		CURRENT,
		HELD_BUSY,
		HELD_UNAVAILABLE,
		HELD_AMBIGUOUS
	};

	struct Label
	{
		std::wstring ratio = L"---";
		std::wstring name = L"Unknown";
		int target = -1;
	};

	struct DiagnosticState
	{
		ReadStatus readStatus = ReadStatus::NEVER;
		DetectedPictureAspect lastReading;
		bool hasReading = false;
		bool rawRatioValid = false;
		Label displayedLabel;
		LabelStatus labelStatus = LabelStatus::UNKNOWN;
		uint64_t lastReadAgeMs = 0;
		uint64_t labelAgeMs = 0;
		uint64_t holdAgeMs = 0;
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
		m_busySinceMs = 0;
		m_labelSinceMs = 0;
		m_lastReadTickMs = 0;
		m_hasReading = false;
		m_readStatus = ReadStatus::NEVER;
		m_lastReading = {};
		m_hasLoggedDiagnostic = false;
		m_lastLoggedDiagnostic = {};
	}

	const Label& Update(const DetectedPictureAspect& reading, uint64_t nowMs)
	{
		const Label previousLabel = m_label;
		const bool sourceReset = !m_hasSource ||
			(reading.sourceGeneration != 0 &&
			 m_sourceGeneration != reading.sourceGeneration);
		if (sourceReset)
		{
			Reset();
			m_hasSource = true;
			m_sourceGeneration = reading.sourceGeneration;
		}
		m_lastReading = reading;
		m_hasReading = true;
		m_lastReadTickMs = nowMs;
		m_busySinceMs = 0;
		const bool rawRatioValid = reading.available &&
			std::isfinite(reading.ratio) && reading.ratio > 0.0;
		m_readStatus = !reading.available ? ReadStatus::UNAVAILABLE :
			(rawRatioValid ? ReadStatus::AVAILABLE : ReadStatus::INVALID);
		auto finish = [&]() -> const Label& {
			if (sourceReset || !SameLabel(previousLabel, m_label))
				m_labelSinceMs = nowMs;
			return m_label;
		};
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
				return finish();
			m_label = {};
			return finish();
		}
		m_unavailableSinceMs = 0;
		const Label incoming = Classify(reading.ratio);
		if (m_label.ratio == L"---")
		{
			m_label = incoming;
			return finish();
		}
		if (incoming.ratio == m_label.ratio &&
			incoming.name == m_label.name)
		{
			m_candidateSinceMs = 0;
			return finish();
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
			return finish();
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
		return finish();
	}

	// A failed nonblocking renderer read leaves the presentation label intact.
	// This updates diagnostics only and does not advance or reset label policy.
	void MarkReadBusy(uint64_t nowMs)
	{
		if (m_readStatus != ReadStatus::BUSY)
			m_busySinceMs = nowMs;
		m_readStatus = ReadStatus::BUSY;
	}

	DiagnosticState GetDiagnosticState(uint64_t nowMs) const
	{
		DiagnosticState state;
		state.readStatus = m_readStatus;
		state.lastReading = m_lastReading;
		state.hasReading = m_hasReading;
		state.rawRatioValid = m_hasReading && m_lastReading.available &&
			std::isfinite(m_lastReading.ratio) && m_lastReading.ratio > 0.0;
		state.displayedLabel = m_label;
		if (m_label.ratio == L"---")
			state.labelStatus = LabelStatus::UNKNOWN;
		else if (m_readStatus == ReadStatus::BUSY)
			state.labelStatus = LabelStatus::HELD_BUSY;
		else if ((m_readStatus == ReadStatus::UNAVAILABLE ||
			m_readStatus == ReadStatus::INVALID) && m_unavailableSinceMs != 0)
			state.labelStatus = LabelStatus::HELD_UNAVAILABLE;
		else if (m_readStatus == ReadStatus::AVAILABLE &&
			m_candidateSinceMs != 0)
			state.labelStatus = LabelStatus::HELD_AMBIGUOUS;
		else
			state.labelStatus = LabelStatus::CURRENT;

		if (m_hasReading && nowMs >= m_lastReadTickMs)
			state.lastReadAgeMs = nowMs - m_lastReadTickMs;
		if (m_label.ratio != L"---" && m_labelSinceMs != 0 &&
			nowMs >= m_labelSinceMs)
			state.labelAgeMs = nowMs - m_labelSinceMs;
		switch (state.labelStatus)
		{
		case LabelStatus::HELD_BUSY:
			if (m_busySinceMs != 0 && nowMs >= m_busySinceMs)
				state.holdAgeMs = nowMs - m_busySinceMs;
			break;
		case LabelStatus::HELD_UNAVAILABLE:
			if (m_unavailableSinceMs != 0 && nowMs >= m_unavailableSinceMs)
				state.holdAgeMs = nowMs - m_unavailableSinceMs;
			break;
		case LabelStatus::HELD_AMBIGUOUS:
			if (m_candidateSinceMs != 0 && nowMs >= m_candidateSinceMs)
				state.holdAgeMs = nowMs - m_candidateSinceMs;
			break;
		default:
			break;
		}
		return state;
	}

	// Ages are snapshots and are intentionally excluded: callers can poll often
	// without producing a log line until one of the diagnostic states changes.
	bool ShouldLogDiagnostic(const DiagnosticState& state)
	{
		if (m_hasLoggedDiagnostic && SameDiagnostic(m_lastLoggedDiagnostic, state))
			return false;
		m_lastLoggedDiagnostic = state;
		m_hasLoggedDiagnostic = true;
		return true;
	}

	static const char* ReadStatusName(ReadStatus status)
	{
		switch (status)
		{
		case ReadStatus::AVAILABLE: return "available";
		case ReadStatus::UNAVAILABLE: return "unavailable";
		case ReadStatus::INVALID: return "invalid";
		case ReadStatus::BUSY: return "busy";
		default: return "never";
		}
	}

	static const char* LabelStatusName(LabelStatus status)
	{
		switch (status)
		{
		case LabelStatus::CURRENT: return "current";
		case LabelStatus::HELD_BUSY: return "held-busy";
		case LabelStatus::HELD_UNAVAILABLE: return "held-unavailable";
		case LabelStatus::HELD_AMBIGUOUS: return "held-ambiguous";
		default: return "unknown";
		}
	}

	static const char* HoldReasonName(LabelStatus status)
	{
		switch (status)
		{
		case LabelStatus::HELD_BUSY: return "busy";
		case LabelStatus::HELD_UNAVAILABLE: return "unavailable";
		case LabelStatus::HELD_AMBIGUOUS: return "ambiguous-label-change";
		default: return "none";
		}
	}

	static Label Classify(double ratio)
	{
		if (!std::isfinite(ratio) || ratio <= 0.0)
			return {};
		int best = -1;
		double bestDifference = 1.0;
		const auto* targets = Targets();
		for (int i = 0; i < 13; ++i)
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
	static bool SameLabel(const Label& a, const Label& b)
	{
		return a.ratio == b.ratio && a.name == b.name && a.target == b.target;
	}

	static bool SameDiagnostic(const DiagnosticState& a,
		const DiagnosticState& b)
	{
		const bool sameRatio = a.rawRatioValid != b.rawRatioValid ? false :
			(std::isfinite(a.lastReading.ratio) &&
			 std::isfinite(b.lastReading.ratio))
				? a.lastReading.ratio == b.lastReading.ratio
				: !std::isfinite(a.lastReading.ratio) &&
					!std::isfinite(b.lastReading.ratio);
		return a.readStatus == b.readStatus &&
			a.hasReading == b.hasReading &&
			a.lastReading.available == b.lastReading.available &&
			sameRatio &&
			a.lastReading.sourceGeneration == b.lastReading.sourceGeneration &&
			a.lastReading.publicationGeneration == b.lastReading.publicationGeneration &&
			SameLabel(a.displayedLabel, b.displayedLabel) &&
			a.labelStatus == b.labelStatus;
	}

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
	uint64_t m_busySinceMs = 0;
	uint64_t m_labelSinceMs = 0;
	uint64_t m_lastReadTickMs = 0;
	bool m_hasReading = false;
	ReadStatus m_readStatus = ReadStatus::NEVER;
	DetectedPictureAspect m_lastReading;
	bool m_hasLoggedDiagnostic = false;
	DiagnosticState m_lastLoggedDiagnostic;
};
