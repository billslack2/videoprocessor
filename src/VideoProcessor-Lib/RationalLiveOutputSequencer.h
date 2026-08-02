/*
 * Delivery-owned presentation sequence for the live DirectShow path.
 *
 * The sequence commits only after a successful DirectShow delivery. Capture
 * identity is retained so a latest-wins discard consumes presentation time
 * without consuming media time. This keeps a live timeline aligned with
 * capture when routine downstream backpressure causes VP to omit a picture.
 * The same sequence owns both ordinary Rational/Rational output and the
 * optional display-rate scene cadence. Changing cadence rebases at the next
 * committed stop; it never creates a second timeline or a same-epoch reset.
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
	// Final-delivery capture identity. A forward jump represents captured live
	// time intentionally omitted before delivery. Equal values are permitted for
	// an explicit upstream repeat and do not advance capture identity.
	uint64_t sourceFrameNumber = 0;
	bool sourceFrameNumberValid = false;
	// False when the caller intentionally removed stale/cadence work or when
	// capture already classified this frame as a source discontinuity. Commit
	// still adopts the new source identity, but no presentation hole is added.
	bool accountSourceGap = true;
	// Exact VP-owned omissions (startup convergence or a planned scene drop)
	// already accounted by the caller. Any remaining small gap is still live
	// time and is preserved on the presentation timeline.
	uint64_t sourceGapSlotsToSuppress = 0;
	// A one-shot live catch-up may align the next segment with current graph
	// time. The owner keeps this value stable until Commit succeeds.
	bool minimumPresentationStartValid = false;
	VideoReferenceTime minimumPresentationStart = 0;
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
	uint32_t sourceGapSlotsBefore = 0;
	uint64_t observedSourceGapSlotsBefore = 0;
	bool sourceGapSuppressed = false;
	uint64_t intentionalSourceGapSlotsSuppressed = 0;
	bool materialSourceGapSuppressed = false;
	bool observedSourceGapMaterial = false;
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
	uint32_t m_maximumLocalSourceGapSlots = 0;
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
	bool m_hasCommittedSourceFrame = false;
	uint64_t m_lastCommittedSourceFrame = 0;
	bool m_hasPendingDecision = false;
	VideoReferenceTime m_pendingSegmentStart = 0;
	RationalLiveOutputTimestampInput m_pendingInput;
	RationalLiveOutputTimestampDecision m_pendingDecision;
};
