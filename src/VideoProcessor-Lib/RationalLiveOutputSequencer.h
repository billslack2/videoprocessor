/*
 * Delivery-owned presentation sequence for the deployed RATIONAL_RATIONAL
 * live DirectShow path.
 *
 * Capture identity is deliberately absent. A caller may discard stale live
 * pictures before delivery without creating a gap in the DirectShow timeline;
 * only a successfully delivered output sample advances this sequence.
 */
#pragma once

#include <cstdint>

#include <VideoTimingController.h>

struct RationalLiveOutputTimestampInput
{
	uint64_t epoch = 0;
	int ppmCorrection = 0;
	VideoReferenceTime pipelineOffset = 0;
	VideoReferenceTime presentationLead = 0;
};

struct RationalLiveOutputTimestampDecision
{
	bool valid = false;
	uint64_t outputSequence = 0;
	int64_t mediaStart = 0;
	int64_t mediaStop = 1;
	bool discontinuity = false;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;
};

class RationalLiveOutputSequencer
{
public:
	RationalLiveOutputSequencer(
		uint32_t timeScale, uint32_t frameDurationTicks,
		VideoReferenceTime theoreticalFrameDuration);

	RationalLiveOutputTimestampDecision Preview(
		const RationalLiveOutputTimestampInput& input);
	bool Commit(const RationalLiveOutputTimestampDecision& decision);
	void ResetToEpoch(uint64_t epoch);

private:
	static FrameTimingInput MakeTimingInput(
		uint64_t outputSequence,
		const RationalLiveOutputTimestampInput& input);
	static RationalLiveOutputTimestampDecision MakeDecision(
		uint64_t outputSequence, const TimingDecision& timingDecision,
		const RationalLiveOutputTimestampInput& input);

	VideoTimingController m_controller;
	uint64_t m_epoch = 0;
	uint64_t m_nextOutputSequence = 0;
	bool m_hasPendingDecision = false;
	RationalLiveOutputTimestampInput m_pendingInput;
	RationalLiveOutputTimestampDecision m_pendingDecision;
};
