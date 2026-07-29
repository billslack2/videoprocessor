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
	bool shouldRecalculate = false;
};

DisplayRefreshRateResult EvaluateDisplayRefreshRate(
	const DisplayRefreshRateInput& input);
const char* ToString(DisplayRefreshRateDecision decision);
const char* ToString(DisplayRefreshRateReason reason);
