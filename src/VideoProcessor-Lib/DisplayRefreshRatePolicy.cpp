#include "pch.h"

#include "DisplayRefreshRatePolicy.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr uint64_t MINIMUM_RAW_INTERVALS = 12;
constexpr double MINIMUM_RATE_HZ = 10.0;
constexpr double MAXIMUM_RATE_HZ = 240.0;
constexpr double NOMINAL_TOLERANCE_RATIO = 0.02;
constexpr double NOMINAL_TOLERANCE_HZ = 0.5;
constexpr double RAW_NOMINAL_TOLERANCE_RATIO = 0.03;
constexpr double RAW_NOMINAL_TOLERANCE_HZ = 0.75;
constexpr double RAW_EXCESS_TOLERANCE_RATIO = 0.02;
constexpr double HARMONIC_TOLERANCE_RATIO = 0.03;
constexpr double MAXIMUM_UNEXPLAINED_COMPENSATION = 1.5;
constexpr double MINIMUM_STARTUP_OBSERVATION_SECONDS = 2.0;
constexpr double MINIMUM_READINESS_OBSERVATION_SECONDS = 10.0;
constexpr double RESTORE_EQUIVALENCE_TOLERANCE_RATIO = 0.0005;
constexpr double RESTORE_EQUIVALENCE_TOLERANCE_HZ = 0.02;
constexpr double MODE_CLOSE_TOLERANCE_RATIO = 0.0005;
constexpr double MODE_CLOSE_TOLERANCE_HZ = 0.02;
constexpr double MODE_FALLBACK_TOLERANCE_RATIO = 0.005;

double AllowedDifference(double rateHz, double ratio, double minimumHz)
{
	return std::max(minimumHz, rateHz * ratio);
}

bool IsClose(double first, double second, double ratio, double minimumHz)
{
	return std::fabs(first - second) <=
		AllowedDifference(second, ratio, minimumHz);
}

bool IsObviousHarmonic(double candidateRateHz, double nominalRateHz)
{
	if (candidateRateHz <= 0.0 || nominalRateHz <= 0.0)
		return false;

	const double ratio = candidateRateHz / nominalRateHz;
	for (const double harmonic : { 0.5, 2.0, 3.0, 4.0 })
	{
		if (std::fabs(ratio - harmonic) <=
			harmonic * HARMONIC_TOLERANCE_RATIO)
		{
			return true;
		}
	}
	return false;
}

DisplayRefreshRateResult Result(
	DisplayRefreshRateDecision decision,
	DisplayRefreshRateReason reason,
	bool shouldRecalculate = false)
{
	DisplayRefreshRateResult result;
	result.decision = decision;
	result.reason = reason;
	result.shouldRecalculate = shouldRecalculate;
	return result;
}
}


double DisplayRefreshRateHz(const DisplayRefreshRational& rate)
{
	return rate.denominator > 0 ?
		static_cast<double>(rate.numerator) / rate.denominator : 0.0;
}


DisplayRefreshModeSelection SelectDisplayRefreshMode(
	const DisplayRefreshRational& requested,
	const std::vector<DisplayRefreshRational>& candidates)
{
	DisplayRefreshModeSelection result;
	result.requestedRateHz = DisplayRefreshRateHz(requested);
	if (!std::isfinite(result.requestedRateHz) ||
		result.requestedRateHz <= 0.0)
	{
		return result;
	}

	const auto selectClosest = [&](double toleranceHz,
		DisplayRefreshModeSelectionPath path) {
		bool found = false;
		for (const DisplayRefreshRational& candidate : candidates)
		{
			const double candidateRateHz = DisplayRefreshRateHz(candidate);
			if (!std::isfinite(candidateRateHz) || candidateRateHz <= 0.0)
				continue;
			const double differenceHz = std::fabs(
				candidateRateHz - result.requestedRateHz);
			if (differenceHz > toleranceHz)
				continue;
			// Stable tie-breaker: retain the higher supported refresh when cadence
			// distance is indistinguishable, preserving presentation headroom.
			if (!found || differenceHz < result.differenceHz - 0.000001 ||
				(std::fabs(differenceHz - result.differenceHz) <= 0.000001 &&
					candidateRateHz > result.selectedRateHz))
			{
				found = true;
				result.path = path;
				result.selected = candidate;
				result.selectedRateHz = candidateRateHz;
				result.differenceHz = differenceHz;
			}
		}
		return found;
	};

	for (const DisplayRefreshRational& candidate : candidates)
	{
		if (DisplayRefreshRatesExactlyEqual(requested, candidate))
		{
			result.path = DisplayRefreshModeSelectionPath::ExactOrClose;
			result.selected = candidate;
			result.selectedRateHz = DisplayRefreshRateHz(candidate);
			result.differenceHz = 0.0;
			return result;
		}
	}
	if (selectClosest(std::max(MODE_CLOSE_TOLERANCE_HZ,
		result.requestedRateHz * MODE_CLOSE_TOLERANCE_RATIO),
		DisplayRefreshModeSelectionPath::ExactOrClose))
	{
		return result;
	}
	selectClosest(result.requestedRateHz * MODE_FALLBACK_TOLERANCE_RATIO,
		DisplayRefreshModeSelectionPath::ClosestInRange);
	return result;
}


bool DisplayRefreshRatesExactlyEqual(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second)
{
	if (first.denominator == 0 || second.denominator == 0)
		return false;
	return static_cast<uint64_t>(first.numerator) *
		static_cast<uint64_t>(second.denominator) ==
		static_cast<uint64_t>(second.numerator) *
		static_cast<uint64_t>(first.denominator);
}


bool DisplayRefreshRatesEquivalentForRestore(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second)
{
	if (DisplayRefreshRatesExactlyEqual(first, second))
		return true;
	if (first.denominator == 0 || second.denominator == 0)
		return false;
	const double firstRate = static_cast<double>(first.numerator) /
		static_cast<double>(first.denominator);
	const double secondRate = static_cast<double>(second.numerator) /
		static_cast<double>(second.denominator);
	return std::isfinite(firstRate) && std::isfinite(secondRate) &&
		IsClose(firstRate, secondRate,
			RESTORE_EQUIVALENCE_TOLERANCE_RATIO,
			RESTORE_EQUIVALENCE_TOLERANCE_HZ);
}

bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow)
{
	return !isChildWindow;
}


bool DisplayRefreshRestoreVerifier::Observe(
	bool querySucceeded, DisplayRefreshRational observed)
{
	if (!querySucceeded ||
		!DisplayRefreshRatesEquivalentForRestore(observed, m_expected))
	{
		m_consecutiveMatches = 0;
		return false;
	}
	if (m_consecutiveMatches < m_requiredConsecutiveMatches)
		++m_consecutiveMatches;
	return m_consecutiveMatches >= m_requiredConsecutiveMatches;
}

DisplayRefreshRateResult EvaluateDisplayRefreshRate(
	const DisplayRefreshRateInput& input)
{
	if (!std::isfinite(input.candidateRateHz))
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::NonFiniteCandidate, true);
	}
	if (input.candidateRateHz <= 0.0)
	{
		return Result(DisplayRefreshRateDecision::Warming,
			DisplayRefreshRateReason::NoSamples);
	}
	if (input.candidateRateHz < MINIMUM_RATE_HZ ||
		input.candidateRateHz > MAXIMUM_RATE_HZ)
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::NominalMismatch, true);
	}
	if (!input.fresh)
	{
		return Result(DisplayRefreshRateDecision::Unavailable,
			DisplayRefreshRateReason::StaleCandidate, true);
	}
	if (input.rawWaitIntervals < MINIMUM_RAW_INTERVALS)
	{
		return Result(DisplayRefreshRateDecision::Warming,
			DisplayRefreshRateReason::InsufficientSamples);
	}
	if (input.compensatedIntervals < input.rawWaitIntervals)
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::InvalidRawCadence, true);
	}
	if (!std::isfinite(input.rawWaitRateHz) ||
		input.rawWaitRateHz <= 0.0)
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::InvalidRawCadence, true);
	}
	if (!std::isfinite(input.minimumWaitIntervalMs) ||
		!std::isfinite(input.maximumWaitIntervalMs) ||
		input.minimumWaitIntervalMs <= 0.0 ||
		input.maximumWaitIntervalMs < input.minimumWaitIntervalMs)
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::InvalidIntervalRange, true);
	}
	if (input.rawWaitRateHz >
		input.candidateRateHz * (1.0 + RAW_EXCESS_TOLERANCE_RATIO))
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::RawCadenceExceedsCandidate, true);
	}

	if (std::isfinite(input.nominalRateHz) &&
		input.nominalRateHz >= MINIMUM_RATE_HZ &&
		input.nominalRateHz <= MAXIMUM_RATE_HZ)
	{
		const bool rawMatchesNominal = IsClose(
			input.rawWaitRateHz, input.nominalRateHz,
			RAW_NOMINAL_TOLERANCE_RATIO, RAW_NOMINAL_TOLERANCE_HZ);
		if (rawMatchesNominal &&
			IsObviousHarmonic(input.candidateRateHz, input.nominalRateHz))
		{
			return Result(DisplayRefreshRateDecision::Quarantined,
				DisplayRefreshRateReason::HarmonicMismatch, true);
		}
		if (!IsClose(input.candidateRateHz, input.nominalRateHz,
			NOMINAL_TOLERANCE_RATIO, NOMINAL_TOLERANCE_HZ))
		{
			return Result(DisplayRefreshRateDecision::Quarantined,
				DisplayRefreshRateReason::NominalMismatch, true);
		}

		const double nominalPeriodMs = 1000.0 / input.nominalRateHz;
		if (input.minimumWaitIntervalMs < nominalPeriodMs * 0.30 ||
			input.maximumWaitIntervalMs > nominalPeriodMs * 10.0)
		{
			return Result(DisplayRefreshRateDecision::Quarantined,
				DisplayRefreshRateReason::InvalidIntervalRange, true);
		}
	}
	else if (input.candidateRateHz / input.rawWaitRateHz >
		MAXIMUM_UNEXPLAINED_COMPENSATION)
	{
		return Result(DisplayRefreshRateDecision::Quarantined,
			DisplayRefreshRateReason::UnexplainedCompensation, true);
	}

	DisplayRefreshRateResult result = Result(
		input.stable ? DisplayRefreshRateDecision::Accepted :
			DisplayRefreshRateDecision::Warming,
		input.stable ? DisplayRefreshRateReason::Accepted :
			DisplayRefreshRateReason::Stabilizing);
	// The checks above validate all cadence and nominal-family constraints.
	// A short two-second observation may start the one reset/prefill sequence;
	// it is not phase confidence and must not be reused as such.
	if (std::isfinite(input.startupObservationSeconds) &&
		input.startupObservationSeconds >=
			MINIMUM_STARTUP_OBSERVATION_SECONDS)
	{
		result.startupRateHz = input.candidateRateHz;
		result.startupValidated = true;
	}
	// Ten seconds gives stronger current-rate evidence while keeping the
	// 30-second phase/scene confidence separate from image startup.
	if (std::isfinite(input.readinessObservationSeconds) &&
		input.readinessObservationSeconds >=
			MINIMUM_READINESS_OBSERVATION_SECONDS)
	{
		result.readinessRateHz = input.candidateRateHz;
		result.readinessValidated = true;
	}
	if (input.stable)
		result.selectedRateHz = input.candidateRateHz;
	return result;
}

const char* ToString(DisplayRefreshRateDecision decision)
{
	switch (decision)
	{
	case DisplayRefreshRateDecision::Accepted: return "accepted";
	case DisplayRefreshRateDecision::Warming: return "warming";
	case DisplayRefreshRateDecision::Quarantined: return "quarantined";
	case DisplayRefreshRateDecision::Unavailable: return "unavailable";
	default: return "unavailable";
	}
}

const char* ToString(DisplayRefreshRateReason reason)
{
	switch (reason)
	{
	case DisplayRefreshRateReason::Accepted: return "candidate validated";
	case DisplayRefreshRateReason::NoSamples: return "no measured samples";
	case DisplayRefreshRateReason::InsufficientSamples:
		return "insufficient raw interval count";
	case DisplayRefreshRateReason::Stabilizing:
		return "measurement stability warm-up";
	case DisplayRefreshRateReason::NonFiniteCandidate:
		return "candidate is not finite";
	case DisplayRefreshRateReason::StaleCandidate:
		return "candidate publication is stale";
	case DisplayRefreshRateReason::InvalidRawCadence:
		return "raw wait cadence is invalid";
	case DisplayRefreshRateReason::InvalidIntervalRange:
		return "raw wait interval range is invalid";
	case DisplayRefreshRateReason::RawCadenceExceedsCandidate:
		return "raw cadence exceeds compensated candidate";
	case DisplayRefreshRateReason::HarmonicMismatch:
		return "candidate is a harmonic of nominal while raw cadence matches nominal";
	case DisplayRefreshRateReason::NominalMismatch:
		return "candidate is implausible for Windows target-path nominal";
	case DisplayRefreshRateReason::UnexplainedCompensation:
		return "compensation factor is unexplained without a nominal cross-check";
	default: return "unknown";
	}
}
