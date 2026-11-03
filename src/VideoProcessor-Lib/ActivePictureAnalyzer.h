/*
 * Worker-owned active-picture analysis stage.
 *
 * Consumes a P010 frame view and applies the existing confidence/hysteresis
 * model. It has no DirectShow sample, renderer, queue, worker, or UI state.
 */
#pragma once

#include <cstdint>

#include <ActivePictureEvidence.h>

struct ActivePictureAnalyzerInput
{
	P010PlaneView frame;
	uint64_t frameNumber = 0;
	double framesPerSecond = 60.0;
	// Conversion/publication generation; the borrowed sample is read synchronously.
	uint64_t sourceGeneration = 1;
};

struct ActivePictureAnalyzerResult
{
	bool analyzed = false;
	ActivePictureEvidence evidence;
	ActivePictureTransitionDecision decision;
};

class ActivePictureAnalyzer
{
public:
	void Reset();
	ActivePictureAnalyzerResult Analyze(const ActivePictureAnalyzerInput& input);

private:
	ActivePictureTransitionModel m_transition;
	uint64_t m_sourceGeneration = 0;
};
