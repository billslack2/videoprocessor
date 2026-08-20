#include <pch.h>

#include "DirectShowVideoTimingAdapter.h"

#include <stdexcept>

namespace
{
	VideoTimingControllerConfig MakeConfig(
		DirectShowStartStopTimeMethod mode,
		uint32_t timeScale,
		uint32_t frameDurationTicks,
		VideoReferenceTime theoreticalFrameDuration)
	{
		VideoTimingControllerConfig config;
		config.mode = DirectShowVideoTimingAdapter::ToVideoTimingMode(mode);
		config.timeScale = timeScale;
		config.frameDurationTicks = frameDurationTicks;
		config.theoreticalFrameDuration = theoreticalFrameDuration;
		return config;
	}
}

DirectShowVideoTimingAdapter::DirectShowVideoTimingAdapter(
	DirectShowStartStopTimeMethod mode,
	uint32_t timeScale,
	uint32_t frameDurationTicks,
	VideoReferenceTime theoreticalFrameDuration) :
	m_mode(mode),
	m_controller(MakeConfig(mode, timeScale, frameDurationTicks,
		theoreticalFrameDuration))
{
}

DirectShowTimingDecision DirectShowVideoTimingAdapter::Decide(
	const DirectShowFrameTimingInput& input)
{
	DirectShowTimingDecision decision;
	decision.base = m_controller.Decide(input.timing);
	decision.hasStart = decision.base.hasStart;
	decision.hasStop = decision.base.hasStop;
	decision.start = decision.base.start;
	decision.stop = decision.base.stop;
	if (decision.base.valid && UsesPresentationLead(m_mode))
	{
		decision.start += input.presentationLead;
		if (decision.hasStop)
			decision.stop += input.presentationLead;
	}
	return decision;
}

VideoTimingMode DirectShowVideoTimingAdapter::ToVideoTimingMode(
	DirectShowStartStopTimeMethod mode)
{
	switch (mode)
	{
	case DS_SSTM_CLOCK_SMART: return VideoTimingMode::ClockSmart;
	case DS_SSTM_CLOCK_THEO: return VideoTimingMode::ClockTheoretical;
	case DS_SSTM_CLOCK_CLOCK: return VideoTimingMode::ClockClock;
	case DS_SSTM_THEO_THEO: return VideoTimingMode::TheoreticalTheoretical;
	case DS_SSTM_RATIONAL_RATIONAL: return VideoTimingMode::RationalRational;
	case DS_SSTM_CLOCK_RATIONAL: return VideoTimingMode::ClockRational;
	case DS_SSTM_CLOCK_SMART2: return VideoTimingMode::ClockSmart2;
	case DS_SSTM_CLOCK_NONE: return VideoTimingMode::ClockOnly;
	case DS_SSTM_THEO_NONE: return VideoTimingMode::TheoreticalOnly;
	case DS_SSTM_NONE: return VideoTimingMode::None;
	}
	throw std::invalid_argument("Unknown DirectShow timestamp mode");
}

bool DirectShowVideoTimingAdapter::UsesPresentationLead(
	DirectShowStartStopTimeMethod mode)
{
	switch (mode)
	{
	case DS_SSTM_RATIONAL_RATIONAL:
	case DS_SSTM_CLOCK_SMART:
	case DS_SSTM_CLOCK_SMART2:
	case DS_SSTM_CLOCK_THEO:
	case DS_SSTM_CLOCK_CLOCK:
		return true;
	default:
		return false;
	}
}

bool DirectShowVideoTimingAdapter::UsesLiveTimestampCatchUp(
	DirectShowStartStopTimeMethod mode)
{
	switch (mode)
	{
	case DS_SSTM_CLOCK_SMART:
	case DS_SSTM_CLOCK_THEO:
	case DS_SSTM_CLOCK_CLOCK:
	case DS_SSTM_THEO_THEO:
	case DS_SSTM_CLOCK_RATIONAL:
	case DS_SSTM_CLOCK_SMART2:
		return true;
	default:
		return false;
	}
}

bool DirectShowVideoTimingAdapter::UsesStartOnlyLiveTimestampCatchUp(
	DirectShowStartStopTimeMethod mode)
{
	return mode == DS_SSTM_CLOCK_NONE || mode == DS_SSTM_THEO_NONE;
}

bool DirectShowVideoTimingAdapter::UsesLiveHardwareClockTimestamps(
	DirectShowStartStopTimeMethod mode)
{
	switch (mode)
	{
	case DS_SSTM_CLOCK_SMART:
	case DS_SSTM_CLOCK_SMART2:
	case DS_SSTM_CLOCK_THEO:
	case DS_SSTM_CLOCK_CLOCK:
	case DS_SSTM_CLOCK_RATIONAL:
	case DS_SSTM_CLOCK_NONE:
		return true;
	default:
		return false;
	}
}

bool DirectShowVideoTimingAdapter::AllowsSceneAwareTimestampCorrection(
	DirectShowStartStopTimeMethod mode)
{
	// Scene cadence owns a display-slot timeline. It cannot replace an absolute
	// hardware-clock timeline without compressing every undelivered source frame.
	return !UsesLiveHardwareClockTimestamps(mode);
}

void DirectShowLiveTimestampCatchUp::ResetToEpoch(uint64_t epoch) noexcept
{
	m_epoch = epoch;
	m_pending = false;
	m_lastStopValid = false;
	m_lastStop = 0;
	m_offset = 0;
	m_targetStartValid = false;
	m_targetStart = 0;
}

void DirectShowLiveTimestampCatchUp::Arm(uint64_t epoch) noexcept
{
	if (m_epoch != epoch)
		ResetToEpoch(epoch);
	m_pending = true;
	m_targetStartValid = false;
	m_targetStart = 0;
}

void DirectShowLiveTimestampCatchUp::ArmAt(
	uint64_t epoch,
	VideoReferenceTime targetStart) noexcept
{
	if (m_epoch != epoch)
		ResetToEpoch(epoch);
	m_pending = true;
	m_targetStartValid = true;
	// A graph-now anchor catches up to live time, but DirectShow must never see
	// the next interval start before the last successfully delivered stop.
	m_targetStart = m_lastStopValid && m_lastStop > targetStart ?
		m_lastStop : targetStart;
}

DirectShowLiveCatchUpDecision DirectShowLiveTimestampCatchUp::Adjust(
	uint64_t epoch,
	VideoReferenceTime start,
	VideoReferenceTime stop) noexcept
{
	if (m_epoch != epoch)
		ResetToEpoch(epoch);

	DirectShowLiveCatchUpDecision decision;
	decision.start = start + m_offset;
	decision.stop = stop + m_offset;
	if (m_pending && (m_targetStartValid || m_lastStopValid))
	{
		const VideoReferenceTime targetStart =
			m_targetStartValid ? m_targetStart : m_lastStop;
		m_offset += targetStart - decision.start;
		decision.start = start + m_offset;
		decision.stop = stop + m_offset;
		decision.rebased = true;
		m_pending = false;
		m_targetStartValid = false;
		m_targetStart = 0;
	}
	decision.offset = m_offset;
	decision.adjusted = m_offset != 0;
	return decision;
}

void DirectShowLiveTimestampCatchUp::CommitSuccessfulStop(
	uint64_t epoch,
	VideoReferenceTime stop) noexcept
{
	if (m_epoch != epoch)
		ResetToEpoch(epoch);
	m_lastStop = stop;
	m_lastStopValid = true;
}
