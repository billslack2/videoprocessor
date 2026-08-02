#include <pch.h>

#include <RationalLiveOutputSequencer.h>

#include <algorithm>
#include <cmath>
#include <limits>

RationalLiveOutputSequencer::RationalLiveOutputSequencer(
	uint32_t timeScale, uint32_t frameDurationTicks,
	VideoReferenceTime theoreticalFrameDuration) :
	m_timeScale(timeScale),
	m_frameDurationTicks(frameDurationTicks),
	m_theoreticalFrameDuration(theoreticalFrameDuration)
{
	// Match the existing live-source recovery boundary: gaps representing less
	// than 100 ms may be routine queue/latest-wins omissions. Larger gaps belong
	// to discontinuity/re-prime handling and must never create far-future PTS.
	if (m_timeScale > 0 && m_frameDurationTicks > 0 &&
		m_frameDurationTicks <=
			(std::numeric_limits<uint64_t>::max)() / 10)
	{
		const uint64_t denominator =
			static_cast<uint64_t>(m_frameDurationTicks) * 10;
		m_maximumLocalSourceGapSlots = static_cast<uint32_t>(
			std::min<uint64_t>((m_timeScale - 1) / denominator,
				(std::numeric_limits<uint32_t>::max)()));
	}
}

RationalLiveOutputTimestampDecision RationalLiveOutputSequencer::Preview(
	const RationalLiveOutputTimestampInput& input)
{
	if (input.epoch == 0 || !IsValidInput(input))
	{
		m_hasPendingDecision = false;
		return {};
	}
	if (!m_epochInitialized || input.epoch != m_epoch)
		ResetToEpoch(input.epoch);

	// Preview must not advance the delivered timeline. A failed DirectShow
	// Deliver() retries the same output time, not a synthetic missing frame.
	const bool rebase = !m_hasCommittedSample || NeedsRebase(input) ||
		input.minimumPresentationStartValid;
	uint64_t observedSourceGapSlots = 0;
	if (input.sourceFrameNumberValid && m_hasCommittedSourceFrame &&
		input.sourceFrameNumber > m_lastCommittedSourceFrame)
	{
		observedSourceGapSlots = input.sourceFrameNumber -
			m_lastCommittedSourceFrame - 1;
	}
	const uint64_t intentionalSourceGapSlotsSuppressed =
		input.accountSourceGap ?
		std::min(observedSourceGapSlots, input.sourceGapSlotsToSuppress) :
		observedSourceGapSlots;
	const uint64_t remainingSourceGapSlots =
		observedSourceGapSlots - intentionalSourceGapSlotsSuppressed;
	const bool materialSourceGapSuppressed =
		remainingSourceGapSlots > m_maximumLocalSourceGapSlots;
	// Capture identity is not presentation identity. Turning a source-counter
	// jump into empty PTS slots makes madVR repeat the previous picture even when
	// VP and every renderer queue are full. The owner separately observes these
	// gaps and requests one serialized re-prime when they are material.
	const uint64_t sourceGapSlots = 0;
	const bool sourceGapSuppressed = observedSourceGapSlots > 0;
	const uint64_t gapSlots = input.presentationGapSlotsBefore;

	VideoReferenceTime segmentStart = rebase ?
		(m_hasCommittedSample ? m_nextPresentationStart :
			input.pipelineOffset + input.presentationLead) :
		m_segmentStart;
	if (input.minimumPresentationStartValid)
		segmentStart = std::max(
			segmentStart, input.minimumPresentationStart);
	const uint64_t segmentSlot = rebase ? 0 : m_segmentSlot;
	const uint64_t startSlot = segmentSlot + gapSlots;

	RationalLiveOutputTimestampDecision decision;
	decision.valid = true;
	decision.outputSequence = m_nextMediaSequence;
	decision.mediaStart = static_cast<int64_t>(m_nextMediaSequence);
	decision.mediaStop = decision.mediaStart + 1;
	decision.discontinuity = m_forceEpochDiscontinuity ||
		input.sourceDiscontinuity;
	if (rebase)
	{
		if (input.cadence == RationalLiveOutputCadence::Rational)
		{
			decision.start = segmentStart + VideoTimingController::RationalTimestamp(
				startSlot, m_frameDurationTicks, m_timeScale, input.ppmCorrection);
		}
		else
		{
			decision.start = segmentStart + static_cast<VideoReferenceTime>(
				llround((static_cast<long double>(startSlot) *
					VideoTimingController::kReferenceTimeTicksPerSecond) /
					input.displayRateHz));
		}
	}
	else
		decision.start = m_segmentStart + OffsetForSlot(startSlot);
	if (rebase)
	{
		if (input.cadence == RationalLiveOutputCadence::Rational)
		{
			decision.stop = segmentStart + VideoTimingController::RationalTimestamp(
				startSlot + 1, m_frameDurationTicks, m_timeScale, input.ppmCorrection);
		}
		else
		{
			decision.stop = segmentStart + static_cast<VideoReferenceTime>(
				llround((static_cast<long double>(startSlot + 1) *
					VideoTimingController::kReferenceTimeTicksPerSecond) /
					input.displayRateHz));
		}
	}
	else
	{
		decision.stop = m_segmentStart + OffsetForSlot(startSlot + 1);
	}
	if (decision.stop <= decision.start)
		decision.stop = decision.start + 1;
	decision.presentationSlotsConsumed = static_cast<uint32_t>(gapSlots) + 1;
	decision.sourceGapSlotsBefore = static_cast<uint32_t>(sourceGapSlots);
	decision.observedSourceGapSlotsBefore = observedSourceGapSlots;
	decision.sourceGapSuppressed = sourceGapSuppressed;
	decision.intentionalSourceGapSlotsSuppressed =
		intentionalSourceGapSlotsSuppressed;
	decision.materialSourceGapSuppressed = materialSourceGapSuppressed;
	decision.observedSourceGapMaterial =
		observedSourceGapSlots > m_maximumLocalSourceGapSlots;

	m_pendingInput = input;
	m_pendingSegmentStart = segmentStart;
	m_pendingDecision = decision;
	m_hasPendingDecision = true;
	return m_pendingDecision;
}

bool RationalLiveOutputSequencer::Commit(
	const RationalLiveOutputTimestampDecision& decision)
{
	if (!decision.valid || !m_hasPendingDecision ||
		decision.outputSequence != m_nextMediaSequence ||
		decision.start != m_pendingDecision.start ||
		decision.stop != m_pendingDecision.stop ||
		decision.mediaStart != m_pendingDecision.mediaStart ||
		decision.mediaStop != m_pendingDecision.mediaStop ||
		decision.discontinuity != m_pendingDecision.discontinuity ||
		decision.presentationSlotsConsumed !=
			m_pendingDecision.presentationSlotsConsumed ||
		decision.sourceGapSlotsBefore !=
			m_pendingDecision.sourceGapSlotsBefore ||
		decision.observedSourceGapSlotsBefore !=
			m_pendingDecision.observedSourceGapSlotsBefore ||
		decision.sourceGapSuppressed !=
			m_pendingDecision.sourceGapSuppressed ||
		decision.intentionalSourceGapSlotsSuppressed !=
			m_pendingDecision.intentionalSourceGapSlotsSuppressed ||
		decision.materialSourceGapSuppressed !=
			m_pendingDecision.materialSourceGapSuppressed ||
		decision.observedSourceGapMaterial !=
			m_pendingDecision.observedSourceGapMaterial)
		return false;

	if (!m_hasCommittedSample || NeedsRebase(m_pendingInput) ||
		m_pendingInput.minimumPresentationStartValid)
		BeginSegment(m_pendingInput, m_pendingSegmentStart);
	m_segmentSlot += m_pendingDecision.presentationSlotsConsumed;
	m_nextPresentationStart = m_pendingDecision.stop;
	++m_nextMediaSequence;
	if (m_pendingInput.sourceFrameNumberValid)
	{
		m_hasCommittedSourceFrame = true;
		m_lastCommittedSourceFrame = m_pendingInput.sourceFrameNumber;
	}
	m_hasCommittedSample = true;
	m_forceEpochDiscontinuity = false;
	m_hasPendingDecision = false;
	return true;
}

void RationalLiveOutputSequencer::ResetToEpoch(uint64_t epoch)
{
	if (epoch == 0)
		return;
	// A cadence rebase is deliberately not a reset.  Repeated notifications for
	// the current pipeline epoch must not restart a DirectShow segment.
	if (m_epochInitialized && m_epoch == epoch)
		return;
	m_epoch = epoch;
	m_epochInitialized = true;
	m_hasCommittedSample = false;
	m_nextMediaSequence = 0;
	m_nextPresentationStart = 0;
	m_segmentStart = 0;
	m_segmentSlot = 0;
	m_cadence = RationalLiveOutputCadence::Rational;
	m_ppmCorrection = 0;
	m_displayRateHz = 0.0;
	m_forceEpochDiscontinuity = true;
	m_hasCommittedSourceFrame = false;
	m_lastCommittedSourceFrame = 0;
	m_hasPendingDecision = false;
	m_pendingSegmentStart = 0;
	m_pendingInput = {};
	m_pendingDecision = {};
}

bool RationalLiveOutputSequencer::NeedsRebase(
	const RationalLiveOutputTimestampInput& input) const
{
	if (input.cadence != m_cadence)
		return true;
	if (input.cadence == RationalLiveOutputCadence::Rational)
		return input.ppmCorrection != m_ppmCorrection;
	// A changed display measurement begins a fresh exact segment at the next
	// committed stop.  Comparing doubles exactly is intentional: the caller
	// already rate-limits its display measurement publication.
	return input.displayRateHz != m_displayRateHz;
}

bool RationalLiveOutputSequencer::IsValidInput(
	const RationalLiveOutputTimestampInput& input) const
{
	if (m_timeScale == 0 || m_frameDurationTicks == 0 ||
		m_theoreticalFrameDuration <= 0)
		return false;
	return input.cadence != RationalLiveOutputCadence::Display ||
		(input.displayRateHz >= 10.0 && input.displayRateHz <= 240.0);
}

VideoReferenceTime RationalLiveOutputSequencer::OffsetForSlot(uint64_t slot) const
{
	if (m_cadence == RationalLiveOutputCadence::Rational)
	{
		return VideoTimingController::RationalTimestamp(
			slot, m_frameDurationTicks, m_timeScale, m_ppmCorrection);
	}
	return static_cast<VideoReferenceTime>(llround(
		(static_cast<long double>(slot) *
			VideoTimingController::kReferenceTimeTicksPerSecond) /
		m_displayRateHz));
}

void RationalLiveOutputSequencer::BeginSegment(
	const RationalLiveOutputTimestampInput& input, VideoReferenceTime start)
{
	m_segmentStart = start;
	m_segmentSlot = 0;
	m_cadence = input.cadence;
	m_ppmCorrection = input.ppmCorrection;
	m_displayRateHz = input.displayRateHz;
}
