#include <pch.h>

#include <RationalLiveOutputSequencer.h>

namespace
{
VideoTimingControllerConfig MakeConfig(
	uint32_t timeScale, uint32_t frameDurationTicks,
	VideoReferenceTime theoreticalFrameDuration)
{
	VideoTimingControllerConfig config;
	config.mode = VideoTimingMode::RationalRational;
	config.timeScale = timeScale;
	config.frameDurationTicks = frameDurationTicks;
	config.theoreticalFrameDuration = theoreticalFrameDuration;
	return config;
}
}

RationalLiveOutputSequencer::RationalLiveOutputSequencer(
	uint32_t timeScale, uint32_t frameDurationTicks,
	VideoReferenceTime theoreticalFrameDuration) :
	m_controller(MakeConfig(
		timeScale, frameDurationTicks, theoreticalFrameDuration))
{
}

RationalLiveOutputTimestampDecision RationalLiveOutputSequencer::Preview(
	const RationalLiveOutputTimestampInput& input)
{
	if (input.epoch != m_epoch)
		ResetToEpoch(input.epoch);

	// Preview must not advance the delivered timeline. A failed DirectShow
	// Deliver() retries the same output time, not a synthetic missing frame.
	VideoTimingController previewController = m_controller;
	const TimingDecision timingDecision = previewController.Decide(
		MakeTimingInput(m_nextOutputSequence, input));
	m_pendingInput = input;
	m_pendingDecision = MakeDecision(
		m_nextOutputSequence, timingDecision, input);
	m_hasPendingDecision = true;
	return m_pendingDecision;
}

bool RationalLiveOutputSequencer::Commit(
	const RationalLiveOutputTimestampDecision& decision)
{
	if (!decision.valid || !m_hasPendingDecision ||
		decision.outputSequence != m_nextOutputSequence ||
		decision.start != m_pendingDecision.start ||
		decision.stop != m_pendingDecision.stop ||
		decision.mediaStart != m_pendingDecision.mediaStart ||
		decision.mediaStop != m_pendingDecision.mediaStop)
		return false;

	(void)m_controller.Decide(
		MakeTimingInput(m_nextOutputSequence, m_pendingInput));
	m_hasPendingDecision = false;
	++m_nextOutputSequence;
	return true;
}

void RationalLiveOutputSequencer::ResetToEpoch(uint64_t epoch)
{
	m_epoch = epoch;
	m_nextOutputSequence = 0;
	m_hasPendingDecision = false;
	m_pendingInput = {};
	m_pendingDecision = {};
	m_controller.ResetToEpoch({ epoch });
}

FrameTimingInput RationalLiveOutputSequencer::MakeTimingInput(
	uint64_t outputSequence, const RationalLiveOutputTimestampInput& input)
{
	FrameTimingInput timingInput;
	timingInput.sourceFrameNumber = outputSequence;
	timingInput.ppmCorrection = input.ppmCorrection;
	timingInput.pipelineOffset = input.pipelineOffset;
	return timingInput;
}

RationalLiveOutputTimestampDecision RationalLiveOutputSequencer::MakeDecision(
	uint64_t outputSequence, const TimingDecision& timingDecision,
	const RationalLiveOutputTimestampInput& input)
{
	RationalLiveOutputTimestampDecision decision;
	decision.valid = timingDecision.valid && timingDecision.hasStart &&
		timingDecision.hasStop;
	decision.outputSequence = outputSequence;
	decision.mediaStart = timingDecision.mediaStart;
	decision.mediaStop = timingDecision.mediaStop;
	decision.discontinuity = timingDecision.discontinuity;
	decision.start = timingDecision.start + input.presentationLead;
	decision.stop = timingDecision.stop + input.presentationLead;
	return decision;
}
