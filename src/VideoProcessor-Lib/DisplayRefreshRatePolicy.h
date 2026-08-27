#pragma once

#include <cstdint>

enum class DisplayRefreshRateDecision
{
	Accepted,
	Warming,
	Quarantined,
	Unavailable
};

enum class DisplayRefreshRateReason
{
	Accepted,
	NoSamples,
	InsufficientSamples,
	Stabilizing,
	NonFiniteCandidate,
	StaleCandidate,
	InvalidRawCadence,
	InvalidIntervalRange,
	RawCadenceExceedsCandidate,
	HarmonicMismatch,
	NominalMismatch,
	UnexplainedCompensation
};

struct DisplayRefreshRateInput
{
	double candidateRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	double nominalRateHz = 0.0;
	double minimumWaitIntervalMs = 0.0;
	double maximumWaitIntervalMs = 0.0;
	uint64_t compensatedIntervals = 0;
	uint64_t rawWaitIntervals = 0;
	// Short current-rate evidence used to begin the one deterministic
	// reset/prefill sequence. It deliberately precedes long phase confidence.
	double startupObservationSeconds = 0.0;
	// Duration of the current, post-transition observation window. This is
	// distinct from the longer phase-correction stability requirement.
	double readinessObservationSeconds = 0.0;
	bool fresh = false;
	bool stable = false;
};

struct DisplayRefreshRateResult
{
	DisplayRefreshRateDecision decision =
		DisplayRefreshRateDecision::Unavailable;
	DisplayRefreshRateReason reason =
		DisplayRefreshRateReason::NoSamples;
	double selectedRateHz = 0.0;
	double startupRateHz = 0.0;
	bool startupValidated = false;
	// The candidate passed all freshness, raw-cadence, interval-range, harmonic,
	// and nominal-family checks. It may still be in the longer stabilization
	// period required for phase-sensitive correction. Output readiness can use
	// this bounded, already-validated observation without treating it as HDMI
	// lock proof.
	double readinessRateHz = 0.0;
	bool readinessValidated = false;
	bool shouldRecalculate = false;
};

struct DisplayRefreshRational
{
	uint32_t numerator = 0;
	uint32_t denominator = 0;
};

bool DisplayRefreshRatesExactlyEqual(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

// Windows and GPU drivers can report a refresh rate that differs from the
// requested rational by a few millihertz after a display-topology rebuild.
// This comparison is deliberately for restore acknowledgement only; mode
// selection continues to require exact rational equality.
bool DisplayRefreshRatesEquivalentForRestore(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

class DisplayRefreshRestoreVerifier
{
public:
	explicit DisplayRefreshRestoreVerifier(
		DisplayRefreshRational expected,
		unsigned int requiredConsecutiveMatches = 2)
		: m_expected(expected),
		  m_requiredConsecutiveMatches(
			requiredConsecutiveMatches == 0 ? 1 : requiredConsecutiveMatches)
	{
	}

	bool Observe(bool querySucceeded, DisplayRefreshRational observed);
	unsigned int ConsecutiveMatches() const
	{
		return m_consecutiveMatches;
	}

private:
	DisplayRefreshRational m_expected;
	unsigned int m_requiredConsecutiveMatches = 2;
	unsigned int m_consecutiveMatches = 0;
};

DisplayRefreshRateResult EvaluateDisplayRefreshRate(
	const DisplayRefreshRateInput& input);
const char* ToString(DisplayRefreshRateDecision decision);
const char* ToString(DisplayRefreshRateReason reason);
