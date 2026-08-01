#include <pch.h>

#include <LiveClockPresentationSequencer.h>

#include <algorithm>

namespace
{
	constexpr VideoReferenceTime kMinimumDuration = 50000; // 5 ms
	constexpr VideoReferenceTime kMaximumDuration = 10000000; // 1 second
}

LiveClockPresentationDecision LiveClockPresentationSequencer::Preview(
	const LiveClockPresentationInput& input)
{
	if (input.epoch == 0 || input.streamTime < 0 ||
		input.nominalFrameDuration <= 0)
	{
		m_hasPendingDecision = false;
		return {};
	}
	if (!m_epochInitialized || input.epoch != m_epoch)
		ResetToEpoch(input.epoch);

	const VideoReferenceTime duration = SelectDuration(input);
	if (duration <= 0)
	{
		m_hasPendingDecision = false;
		return {};
	}

	// A timestamp equal to "now" is already vulnerable to becoming late while
	// Receive() traverses the graph.  One nominal frame is the minimum live
	// scheduling margin; an explicit larger legacy lead remains honored.
	const VideoReferenceTime effectiveLead = std::max(
		input.presentationLead, input.nominalFrameDuration);
	const VideoReferenceTime liveAnchor = input.streamTime + effectiveLead;

	LiveClockPresentationDecision decision;
	decision.valid = true;
	decision.outputSequence = m_nextOutputSequence;
	decision.mediaStart = static_cast<int64_t>(m_nextOutputSequence);
	decision.mediaStop = decision.mediaStart + 1;
	decision.reanchored = !m_hasCommittedSample;
	decision.discontinuity = m_forceEpochDiscontinuity ||
		input.sourceDiscontinuity;
	decision.start = m_hasCommittedSample ?
		m_nextPresentationStart : liveAnchor;

	// A same-epoch graph-clock jump can otherwise leave the delivery-owned
	// sequence irrecoverably late.  Catch up only after it is more than two
	// frames behind; ordinary renderer backpressure remains untouched.
	const VideoReferenceTime catchUpTolerance =
		input.nominalFrameDuration * 2;
	if (m_hasCommittedSample &&
		decision.start + catchUpTolerance < input.streamTime)
	{
		decision.start = liveAnchor;
		decision.reanchored = true;
		decision.discontinuity = true;
	}
	decision.duration = duration;
	decision.stop = decision.start + duration;
	if (decision.stop <= decision.start)
		decision.stop = decision.start + 1;

	m_pendingDecision = decision;
	m_hasPendingDecision = true;
	return m_pendingDecision;
}

bool LiveClockPresentationSequencer::Commit(
	const LiveClockPresentationDecision& decision)
{
	if (!decision.valid || !m_hasPendingDecision ||
		decision.outputSequence != m_nextOutputSequence ||
		decision.start != m_pendingDecision.start ||
		decision.stop != m_pendingDecision.stop ||
		decision.mediaStart != m_pendingDecision.mediaStart ||
		decision.mediaStop != m_pendingDecision.mediaStop ||
		decision.discontinuity != m_pendingDecision.discontinuity ||
		decision.reanchored != m_pendingDecision.reanchored)
		return false;

	m_nextPresentationStart = decision.stop;
	++m_nextOutputSequence;
	m_hasCommittedSample = true;
	m_forceEpochDiscontinuity = false;
	m_hasPendingDecision = false;
	return true;
}

void LiveClockPresentationSequencer::ResetToEpoch(uint64_t epoch)
{
	if (epoch == 0)
		return;
	if (m_epochInitialized && epoch == m_epoch)
		return;
	m_epoch = epoch;
	m_epochInitialized = true;
	m_hasCommittedSample = false;
	m_nextOutputSequence = 0;
	m_nextPresentationStart = 0;
	m_forceEpochDiscontinuity = true;
	m_hasPendingDecision = false;
	m_pendingDecision = {};
}

VideoReferenceTime LiveClockPresentationSequencer::SelectDuration(
	const LiveClockPresentationInput& input)
{
	if (!input.observedDurationValid ||
		input.observedFrameDuration < kMinimumDuration ||
		input.observedFrameDuration > kMaximumDuration)
		return input.nominalFrameDuration;

	// Do not turn a missing/discarded capture frame into a long presentation
	// interval.  The live output displays the newest retained picture next.
	const VideoReferenceTime minimumExpected =
		input.nominalFrameDuration / 2;
	const VideoReferenceTime maximumExpected =
		input.nominalFrameDuration + input.nominalFrameDuration / 2;
	if (input.observedFrameDuration < minimumExpected ||
		input.observedFrameDuration > maximumExpected)
		return input.nominalFrameDuration;
	return input.observedFrameDuration;
}
