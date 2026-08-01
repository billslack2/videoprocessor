/*
 * Delivery-owned presentation sequence for legacy hardware-clock modes.
 *
 * Capture timestamps estimate the duration of a delivered frame, but they do
 * not own DirectShow presentation position.  The first sample in each queue
 * epoch is anchored to current graph stream time plus a bounded live lead.
 * Only a successfully delivered sample advances the sequence, so removing
 * stale live pictures cannot manufacture a future timestamp gap.
 */
#pragma once

#include <cstdint>

#include <VideoTimingController.h>

struct LiveClockPresentationInput
{
	uint64_t epoch = 0;
	VideoReferenceTime streamTime = 0;
	VideoReferenceTime presentationLead = 0;
	VideoReferenceTime nominalFrameDuration = 0;
	VideoReferenceTime observedFrameDuration = 0;
	bool observedDurationValid = false;
	bool sourceDiscontinuity = false;
};

struct LiveClockPresentationDecision
{
	bool valid = false;
	uint64_t outputSequence = 0;
	int64_t mediaStart = 0;
	int64_t mediaStop = 1;
	bool discontinuity = false;
	bool reanchored = false;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;
	VideoReferenceTime duration = 0;
	VideoReferenceTime observedStreamTime = 0;
};

class LiveClockPresentationSequencer
{
public:
	LiveClockPresentationDecision Preview(
		const LiveClockPresentationInput& input);
	bool Commit(const LiveClockPresentationDecision& decision);
	void ResetToEpoch(uint64_t epoch);

private:
	static VideoReferenceTime SelectDuration(
		const LiveClockPresentationInput& input);

	uint64_t m_epoch = 0;
	bool m_epochInitialized = false;
	bool m_hasCommittedSample = false;
	uint64_t m_nextOutputSequence = 0;
	VideoReferenceTime m_nextPresentationStart = 0;
	VideoReferenceTime m_lastCommittedStreamTime = 0;
	bool m_hasCommittedStreamTime = false;
	bool m_forceEpochDiscontinuity = true;
	bool m_hasPendingDecision = false;
	LiveClockPresentationDecision m_pendingDecision;
};
