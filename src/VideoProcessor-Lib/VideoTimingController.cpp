#include <pch.h>

#include <IntegerMath.h>

#include "VideoTimingController.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace
{
	bool IsReasonableDuration(VideoReferenceTime duration)
	{
		return duration >= 50000LL && duration <= 10000000LL;
	}
}

VideoTimingController::VideoTimingController(const VideoTimingControllerConfig& config)
{
	Configure(config);
}

void VideoTimingController::Configure(const VideoTimingControllerConfig& config)
{
	if (config.timeScale == 0 || config.frameDurationTicks == 0 ||
		config.theoreticalFrameDuration <= 0)
		throw std::invalid_argument("VideoTimingController requires a valid frame rate");
	m_config = config;
	Reset();
}

void VideoTimingController::Reset()
{
	++m_epoch.value;
	if (m_epoch.value == 0)
		++m_epoch.value;
	m_frameOffsetValid = false;
	m_frameOffset = 0;
	m_previousSourceFrameNumber = 0;
	m_frameCount = 0;
	m_forceDiscontinuity = true;
	m_startOffsetValid = false;
	m_startOffset = 0;
	m_previousStop = 0;
	m_rationalFrameDuration = 0;
	m_minFrameAdvance = 0;
	m_maxFrameAdvance = 0;
	m_lastHardwareTimestamp = 0;
	m_hardwareTimestampAnomalyCount = 0;
	m_durationHistory.fill(0);
	m_durationHistoryIndex = 0;
	m_durationHistoryCount = 0;
	m_durationHistorySum = 0;
}

void VideoTimingController::RestartAfterPreroll()
{
	// This deliberately does not force a DirectShow discontinuity or change the
	// epoch: the caller retains ownership of the established segment.
	m_frameOffsetValid = false;
	m_frameOffset = 0;
	m_previousSourceFrameNumber = 0;
	m_frameCount = 0;
	m_startOffsetValid = false;
	m_startOffset = 0;
	m_previousStop = 0;
	m_rationalFrameDuration = 0;
	m_minFrameAdvance = 0;
	m_maxFrameAdvance = 0;
	m_lastHardwareTimestamp = 0;
}

void VideoTimingController::ReplaceEpoch()
{
	Reset();
}

uint64_t VideoTimingController::TrimNumerator(int ppmCorrection) const
{
	const int64_t trim = static_cast<int64_t>(kPpmDenominator) + ppmCorrection;
	return trim > 0 ? static_cast<uint64_t>(trim) : 1ULL;
}

VideoReferenceTime VideoTimingController::RationalTimestamp(
	uint64_t frameIndex,
	uint64_t frameDurationTicks,
	uint64_t timeScale,
	int ppmCorrection)
{
	if (timeScale == 0 || frameDurationTicks == 0)
		return 0;
	const int64_t trim = static_cast<int64_t>(kPpmDenominator) + ppmCorrection;
	const uint64_t trimNumerator = trim > 0 ? static_cast<uint64_t>(trim) : 1ULL;
	uint64_t value = U64_MulDiv(frameIndex, kReferenceTimeTicksPerSecond, 1ULL);
	value = U64_MulDiv(value, frameDurationTicks, timeScale);
	value = U64_MulDiv(value, trimNumerator, kPpmDenominator);
	return static_cast<VideoReferenceTime>(value);
}

VideoReferenceTime VideoTimingController::ClockToReferenceTime(
	uint64_t timestamp,
	uint64_t ticksPerSecond)
{
	if (ticksPerSecond == 0)
		return 0;
	const uint64_t maxSafeValue =
		static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) /
		kReferenceTimeTicksPerSecond;
	if (timestamp > maxSafeValue)
		return static_cast<VideoReferenceTime>(
			(timestamp / ticksPerSecond) * kReferenceTimeTicksPerSecond);
	return static_cast<VideoReferenceTime>(
		U64_MulDiv(timestamp, kReferenceTimeTicksPerSecond, ticksPerSecond));
}

bool VideoTimingController::UsesStartTime() const
{
	return m_config.mode != VideoTimingMode::None;
}

bool VideoTimingController::UsesStopTime() const
{
	switch (m_config.mode)
	{
	case VideoTimingMode::ClockOnly:
	case VideoTimingMode::TheoreticalOnly:
	case VideoTimingMode::None:
		return false;
	default:
		return true;
	}
}

VideoReferenceTime VideoTimingController::TheoreticalDuration() const
{
	return m_config.theoreticalFrameDuration;
}

VideoReferenceTime VideoTimingController::SmartDuration() const
{
	return m_durationHistoryCount == 0 ? TheoreticalDuration() :
		m_durationHistorySum / static_cast<VideoReferenceTime>(m_durationHistoryCount);
}

void VideoTimingController::AddDurationHistory(VideoReferenceTime duration)
{
	if (!IsReasonableDuration(duration))
		return;
	if (m_durationHistoryCount == kDurationHistorySize)
		m_durationHistorySum -= m_durationHistory[m_durationHistoryIndex];
	else
		++m_durationHistoryCount;
	m_durationHistory[m_durationHistoryIndex] = duration;
	m_durationHistorySum += duration;
	m_durationHistoryIndex = (m_durationHistoryIndex + 1) % kDurationHistorySize;
}

TimingDecision VideoTimingController::Decide(const FrameTimingInput& input)
{
	TimingDecision decision;
	decision.epoch = m_epoch;
	if (!m_frameOffsetValid)
	{
		m_frameOffset = input.sourceFrameNumber;
		m_frameOffsetValid = true;
	}
	decision.streamFrameNumber = input.sourceFrameNumber - m_frameOffset;
	decision.mediaStart = static_cast<int64_t>(decision.streamFrameNumber);
	decision.mediaStop = decision.mediaStart + 1;
	++m_frameCount;
	decision.discontinuity =
		m_forceDiscontinuity || m_frameCount == 1 ||
		input.sourceFrameNumber != m_previousSourceFrameNumber + 1;
	m_forceDiscontinuity = false;
	m_previousSourceFrameNumber = input.sourceFrameNumber;
	decision.hasStart = UsesStartTime();
	decision.hasStop = UsesStopTime();

	const VideoReferenceTime hardwareTime = input.hasHardwareTimestamp ?
		ClockToReferenceTime(input.hardwareTimestamp, input.timingClockTicksPerSecond) : 0;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;

	switch (m_config.mode)
	{
	case VideoTimingMode::RationalRational:
		start = RationalTimestamp(decision.streamFrameNumber,
			m_config.frameDurationTicks, m_config.timeScale, input.ppmCorrection) +
			input.pipelineOffset;
		if (decision.streamFrameNumber == 0)
			m_previousStop = start - 1;
		else if (start <= m_previousStop)
			start = m_previousStop + 1;
		stop = RationalTimestamp(decision.streamFrameNumber + 1,
			m_config.frameDurationTicks, m_config.timeScale, input.ppmCorrection) +
			input.pipelineOffset;
		break;

	case VideoTimingMode::ClockRational:
		if (m_rationalFrameDuration == 0)
		{
			const uint64_t trimmedDurationTicks = U64_MulDiv(
				m_config.frameDurationTicks, TrimNumerator(input.ppmCorrection), kPpmDenominator);
			m_rationalFrameDuration = static_cast<VideoReferenceTime>(
				(kReferenceTimeTicksPerSecond * trimmedDurationTicks) / m_config.timeScale);
			m_minFrameAdvance = m_rationalFrameDuration / 4;
			m_maxFrameAdvance = m_rationalFrameDuration * 2;
		}
		if (!m_startOffsetValid)
		{
			m_startOffset = hardwareTime;
			m_startOffsetValid = true;
			start = 0;
		}
		else
		{
			start = hardwareTime - m_startOffset;
			const VideoReferenceTime sincePrevious =
				start - (m_previousStop - m_rationalFrameDuration);
			if (sincePrevious < m_minFrameAdvance)
			{
				start = m_previousStop - m_rationalFrameDuration + m_minFrameAdvance;
				++m_hardwareTimestampAnomalyCount;
			}
			else if (sincePrevious > m_maxFrameAdvance)
			{
				start = m_previousStop - m_rationalFrameDuration + m_maxFrameAdvance;
				++m_hardwareTimestampAnomalyCount;
			}
			if (start <= m_previousStop - m_rationalFrameDuration)
				start = m_previousStop - m_rationalFrameDuration + 1;
		}
		stop = start + m_rationalFrameDuration;
		break;

	case VideoTimingMode::ClockSmart:
	case VideoTimingMode::ClockSmart2:
	case VideoTimingMode::ClockTheoretical:
	case VideoTimingMode::ClockClock:
	case VideoTimingMode::ClockOnly:
		if (!m_startOffsetValid)
		{
			m_startOffset = hardwareTime;
			m_startOffsetValid = true;
		}
		start = hardwareTime - m_startOffset;
		if (m_config.mode == VideoTimingMode::ClockSmart)
		{
			stop = start + TheoreticalDuration();
			if (stop <= m_previousStop)
				stop = m_previousStop + 1;
		}
		else if (m_config.mode == VideoTimingMode::ClockSmart2)
		{
			if (m_lastHardwareTimestamp > 0)
				AddDurationHistory(start - m_lastHardwareTimestamp);
			m_lastHardwareTimestamp = start;
			stop = start + SmartDuration();
			if (stop <= m_previousStop)
				stop = m_previousStop + 1;
		}
		else if (m_config.mode == VideoTimingMode::ClockClock &&
			input.hasNextHardwareTimestamp)
		{
			stop = ClockToReferenceTime(input.nextHardwareTimestamp,
				input.timingClockTicksPerSecond) - m_startOffset;
			if (stop <= m_previousStop)
				stop = m_previousStop + 1;
		}
		else
			stop = start + TheoreticalDuration();
		break;

	case VideoTimingMode::TheoreticalTheoretical:
	case VideoTimingMode::TheoreticalOnly:
		start = static_cast<VideoReferenceTime>(decision.streamFrameNumber) *
			TheoreticalDuration();
		stop = start + TheoreticalDuration();
		break;

	case VideoTimingMode::None:
		break;
	}

	if (decision.hasStop && stop <= start)
		stop = start + 1;
	if (decision.hasStart)
		decision.start = start;
	if (decision.hasStop)
	{
		decision.stop = stop;
		m_previousStop = stop;
	}
	decision.hardwareTimestampAnomalyCount = m_hardwareTimestampAnomalyCount;
	return decision;
}
