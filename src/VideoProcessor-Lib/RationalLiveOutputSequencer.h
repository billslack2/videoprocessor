/*
 * Delivery-owned presentation sequence for the live DirectShow path.
 *
 * Capture identity is deliberately absent. A caller may discard stale live
 * pictures before delivery without creating a gap in the DirectShow timeline;
 * only a successfully delivered output sample advances this sequence.  The
 * same sequence owns both ordinary Rational/Rational output and the optional
 * display-rate scene cadence.  Changing cadence rebases at the next committed
 * stop; it never creates a second timeline or a same-epoch reset.
 */
#pragma once

#include <cstdint>

#include <VideoTimingController.h>

enum class RationalLiveOutputCadence : uint8_t
{
	Rational,
	Display
};

struct RationalLiveOutputTimestampInput
{
	uint64_t epoch = 0;
	int ppmCorrection = 0;
	VideoReferenceTime pipelineOffset = 0;
	VideoReferenceTime presentationLead = 0;
	// Display cadence is selected only while Scene Detect owns output cadence.
	// A valid displayRateHz is required in that mode.
	RationalLiveOutputCadence cadence = RationalLiveOutputCadence::Rational;
	double displayRateHz = 0.0;
	// A source discontinuity is not a request to reset this sequence.  It is
	// carried to the one final DirectShow sample stamp.
	bool sourceDiscontinuity = false;
	// Renderer-gap repeat: consume this many presentation intervals before
	// stamping this one actual IMediaSample. Media time still advances once.
	uint32_t presentationGapSlotsBefore = 0;
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
	uint32_t presentationSlotsConsumed = 1;
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
	bool NeedsRebase(const RationalLiveOutputTimestampInput& input) const;
	bool IsValidInput(const RationalLiveOutputTimestampInput& input) const;
	VideoReferenceTime OffsetForSlot(uint64_t slot) const;
	void BeginSegment(const RationalLiveOutputTimestampInput& input,
		VideoReferenceTime start);

	uint32_t m_timeScale = 0;
	uint32_t m_frameDurationTicks = 0;
	VideoReferenceTime m_theoreticalFrameDuration = 0;
	uint64_t m_epoch = 0;
	bool m_epochInitialized = false;
	bool m_hasCommittedSample = false;
	uint64_t m_nextMediaSequence = 0;
	VideoReferenceTime m_nextPresentationStart = 0;
	VideoReferenceTime m_segmentStart = 0;
	uint64_t m_segmentSlot = 0;
	RationalLiveOutputCadence m_cadence =
		RationalLiveOutputCadence::Rational;
	int m_ppmCorrection = 0;
	double m_displayRateHz = 0.0;
	bool m_forceEpochDiscontinuity = true;
	bool m_hasPendingDecision = false;
	VideoReferenceTime m_pendingSegmentStart = 0;
	RationalLiveOutputTimestampInput m_pendingInput;
	RationalLiveOutputTimestampDecision m_pendingDecision;
};
