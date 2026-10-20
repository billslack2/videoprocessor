#include "DisplayRefreshRatePolicy.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <cctype>
#include <sstream>
#include <string>
#include <limits>

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


std::vector<DisplayRefreshModeSelection> RankDisplayRefreshModesForInput(
	const DisplayRefreshRational& input, bool interlaced,
	const std::vector<DisplayRefreshRational>& candidates)
{
	std::vector<DisplayRefreshModeSelection> ranked;
	if (input.numerator == 0 || input.denominator == 0) return ranked;
	std::vector<DisplayRefreshRational> remaining = candidates;
	auto appendGroup = [&](DisplayRefreshRational requested, bool doubled) {
		while (!remaining.empty())
		{
			auto selected = SelectDisplayRefreshMode(requested, remaining);
			if (selected.path == DisplayRefreshModeSelectionPath::None) break;
			selected.doubledRate = doubled;
			ranked.push_back(selected);
			remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
				[&](const DisplayRefreshRational& candidate) {
					return DisplayRefreshRatesExactlyEqual(candidate, selected.selected);
				}), remaining.end());
		}
	};
	if (!interlaced) appendGroup(input, false);
	const double inputHz = DisplayRefreshRateHz(input);
	if ((interlaced || (inputHz > 24.1 && inputHz < 31.0)) &&
		input.numerator <= (std::numeric_limits<uint32_t>::max)() / 2)
	{
		appendGroup({ input.numerator * 2, input.denominator }, true);
	}
	return ranked;
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

namespace
{
std::string TrimModeWhitespace(const std::string& text)
{
	size_t first = 0;
	while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
		++first;
	size_t last = text.size();
	while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])))
		--last;
	return text.substr(first, last - first);
}

bool ParseUnsignedModeComponent(const std::string& text, uint32_t& value)
{
	if (text.empty() || !std::all_of(text.begin(), text.end(),
		[](unsigned char c) { return std::isdigit(c) != 0; }))
		return false;
	try
	{
		size_t consumed = 0;
		const unsigned long long parsed = std::stoull(text, &consumed);
		if (consumed != text.size() || parsed > UINT32_MAX) return false;
		value = static_cast<uint32_t>(parsed);
		return true;
	}
	catch (const std::exception&) { return false; }
}

uint32_t WidthForShortDisplayMode(uint32_t height)
{
	switch (height)
	{
	case 240: return 426;
	case 360: return 640;
	case 480: return 854;
	case 576: return 1024;
	case 720: return 1280;
	case 1080: return 1920;
	case 1440: return 2560;
	case 2160: return 3840;
	case 4320: return 7680;
	default: return 0;
	}
}

bool ParseModeRate(const std::string& text, DisplayRefreshRational& rate)
{
	const size_t slash = text.find('/');
	if (slash != std::string::npos)
	{
		if (text.find('/', slash + 1) != std::string::npos ||
			!ParseUnsignedModeComponent(text.substr(0, slash), rate.numerator) ||
			!ParseUnsignedModeComponent(text.substr(slash + 1), rate.denominator) ||
			rate.denominator == 0)
			return false;
	}
	else if (text == "23.976") rate = { 24000, 1001 };
	else if (text == "29.97") rate = { 30000, 1001 };
	else if (text == "59.94") rate = { 60000, 1001 };
	else if (text.find('.') != std::string::npos)
	{
		try
		{
			size_t consumed = 0;
			const double parsed = std::stod(text, &consumed);
			if (consumed != text.size() || !std::isfinite(parsed) || parsed < 20.0 || parsed > 240.0)
				return false;
			const std::string fraction = text.substr(text.find('.') + 1);
			if (fraction.empty() || fraction.size() > 3 ||
				!std::all_of(fraction.begin(), fraction.end(),
					[](unsigned char c) { return std::isdigit(c) != 0; }))
				return false;
			uint32_t denominator = fraction.size() == 1 ? 10 :
				(fraction.size() == 2 ? 100 : 1000);
			uint32_t numerator = static_cast<uint32_t>(std::llround(parsed * denominator));
			uint32_t divisor = numerator;
			uint32_t remainder = denominator;
			while (remainder != 0)
			{
				const uint32_t next = divisor % remainder;
				divisor = remainder;
				remainder = next;
			}
			rate = { numerator / divisor, denominator / divisor };
		}
		catch (const std::exception&) { return false; }
	}
	else
	{
		uint32_t integer = 0;
		if (!ParseUnsignedModeComponent(text, integer)) return false;
		if (integer == 23) rate = { 24000, 1001 };
		else if (integer == 29) rate = { 30000, 1001 };
		else if (integer == 59) rate = { 60000, 1001 };
		else rate = { integer, 1 };
	}
	const double hz = DisplayRefreshRateHz(rate);
	return std::isfinite(hz) && hz >= 20.0 && hz <= 240.0;
}

bool IsDisplayModeAvailable(const DisplayOutputMode& requested,
	const std::vector<DisplayOutputMode>& available)
{
	return std::any_of(available.begin(), available.end(),
		[&requested](const DisplayOutputMode& candidate)
			{ return DisplayOutputModesExactlyEqual(requested, candidate); });
}
}

bool ParseDisplayOutputMode(const std::string& text,
	DisplayOutputMode& mode, std::string& error)
{
	error.clear();
	mode = {};
	const std::string token = TrimModeWhitespace(text);
	const size_t progressive = token.find_first_of("pP");
	if (progressive == std::string::npos || progressive == 0 ||
		progressive + 1 >= token.size())
	{
		error = "mode must use progressive WIDTHxHEIGHTpRATE or HEIGHTpRATE form";
		return false;
	}
	const size_t raster = token.find_first_of("xX");
	if (raster == std::string::npos || raster > progressive)
	{
		if (!ParseUnsignedModeComponent(token.substr(0, progressive), mode.height))
		{
			error = "short-form mode height must be a whole number";
			return false;
		}
		mode.width = WidthForShortDisplayMode(mode.height);
		if (mode.width == 0)
		{
			error = "short-form height has no documented 16:9 width; use WIDTHxHEIGHTpRATE";
			return false;
		}
	}
	else
	{
		if (!ParseUnsignedModeComponent(token.substr(0, raster), mode.width) ||
			!ParseUnsignedModeComponent(token.substr(raster + 1,
				progressive - raster - 1), mode.height))
		{
			error = "mode width and height must be whole numbers";
			return false;
		}
	}
	if (mode.width < 100 || mode.width > 10000 ||
		mode.height < 100 || mode.height > 10000)
	{
		error = "mode width and height must be from 100 through 10000 pixels";
		return false;
	}
	if (!ParseModeRate(token.substr(progressive + 1), mode.refreshRate))
	{
		error = "mode rate must be a supported decimal, integer, or exact rational from 20 through 240 Hz";
		return false;
	}
	return true;
}

bool ParseDisplayOutputModeList(const std::string& text,
	std::vector<DisplayOutputMode>& modes, std::string& error)
{
	modes.clear();
	error.clear();
	std::string normalized = text;
	std::replace(normalized.begin(), normalized.end(), ',', ' ');
	std::istringstream input(normalized);
	std::string token;
	while (input >> token)
	{
		DisplayOutputMode mode;
		std::string detail;
		if (!ParseDisplayOutputMode(token, mode, detail))
		{
			error = "invalid display mode '" + token + "': " + detail;
			modes.clear();
			return false;
		}
		if (std::any_of(modes.begin(), modes.end(),
			[&mode](const DisplayOutputMode& existing)
				{ return DisplayOutputModesExactlyEqual(existing, mode); }))
		{
			error = "duplicate display mode '" + FormatDisplayOutputMode(mode) + "'";
			modes.clear();
			return false;
		}
		modes.push_back(mode);
	}
	return true;
}

std::string FormatDisplayOutputMode(const DisplayOutputMode& mode)
{
	std::string rate;
	if (DisplayRefreshRatesExactlyEqual(mode.refreshRate, { 24000, 1001 }))
		rate = "23.976";
	else if (DisplayRefreshRatesExactlyEqual(mode.refreshRate, { 30000, 1001 }))
		rate = "29.97";
	else if (DisplayRefreshRatesExactlyEqual(mode.refreshRate, { 60000, 1001 }))
		rate = "59.94";
	else if (mode.refreshRate.denominator == 1)
		rate = std::to_string(mode.refreshRate.numerator);
	else
		rate = std::to_string(mode.refreshRate.numerator) + "/" +
			std::to_string(mode.refreshRate.denominator);
	return std::to_string(mode.width) + "x" + std::to_string(mode.height) +
		"p" + rate;
}

bool DisplayOutputModesExactlyEqual(const DisplayOutputMode& first,
	const DisplayOutputMode& second)
{
	return first.width == second.width && first.height == second.height &&
		DisplayRefreshRatesExactlyEqual(first.refreshRate, second.refreshRate);
}

std::vector<DisplayOutputModeChoice> RankDisplayOutputModesForInput(
	const DisplayRefreshRational& input, bool interlaced,
	const std::vector<DisplayOutputMode>& configured,
	const std::vector<DisplayOutputMode>& available)
{
	std::vector<DisplayRefreshRational> rates;
	for (const DisplayOutputMode& mode : configured)
	{
		if (!IsDisplayModeAvailable(mode, available)) continue;
		if (std::none_of(rates.begin(), rates.end(),
			[&mode](const DisplayRefreshRational& rate)
				{ return DisplayRefreshRatesExactlyEqual(rate, mode.refreshRate); }))
			rates.push_back(mode.refreshRate);
	}
	const std::vector<DisplayRefreshModeSelection> rankedRates =
		RankDisplayRefreshModesForInput(input, interlaced, rates);
	std::vector<DisplayOutputModeChoice> result;
	for (const DisplayRefreshModeSelection& rate : rankedRates)
		for (const DisplayOutputMode& mode : configured)
			if (DisplayRefreshRatesExactlyEqual(mode.refreshRate, rate.selected) &&
				IsDisplayModeAvailable(mode, available) &&
				std::none_of(result.begin(), result.end(),
					[&mode](const DisplayOutputModeChoice& existing)
						{ return DisplayOutputModesExactlyEqual(existing.mode, mode); }))
				result.push_back({ mode, rate.path, rate.doubledRate });
	return result;
}
bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow,
	RefreshRateSwitchMode mode)
{
	return mode == RefreshRateSwitchMode::Always ||
		(mode == RefreshRateSwitchMode::FullscreenOnly && !isChildWindow);
}

bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow)
{
	return ShouldSwitchRefreshRateForPresentationTarget(isChildWindow,
		RefreshRateSwitchMode::FullscreenOnly);
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
