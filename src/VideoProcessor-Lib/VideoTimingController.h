/*
 * Graph-independent live-video timestamp policy.
 *
 * This intentionally owns values and timing state only.  It never sees an
 * IMediaSample, a queue, a renderer, or a DirectShow graph.
 */
#pragma once

#include <array>
#include <cstdint>

using VideoReferenceTime = int64_t;

struct PipelineEpoch
{
	uint64_t value = 0;
};

enum class VideoTimingMode : uint8_t
{
	ClockSmart,
	ClockTheoretical,
	ClockClock,
	TheoreticalTheoretical,
	RationalRational,
	ClockRational,
	ClockSmart2,
	ClockOnly,
	TheoreticalOnly,
	None
};

struct VideoTimingControllerConfig
{
	VideoTimingMode mode = VideoTimingMode::RationalRational;
	uint32_t timeScale = 0;
	uint32_t frameDurationTicks = 0;
	VideoReferenceTime theoreticalFrameDuration = 0;
};

struct FrameTimingInput
{
	uint64_t sourceFrameNumber = 0;
	uint64_t hardwareTimestamp = 0;
	uint64_t timingClockTicksPerSecond = 0;
	bool hasHardwareTimestamp = false;
	uint64_t nextHardwareTimestamp = 0;
	bool hasNextHardwareTimestamp = false;
	// Some callers already obtain the next timestamp as 100-ns reference time.
	// Keeping this value-based alternative avoids leaking that caller's clock or
	// graph dependency into the controller.
	VideoReferenceTime nextReferenceTime = 0;
	bool hasNextReferenceTime = false;
	int ppmCorrection = 0;
	VideoReferenceTime pipelineOffset = 0;
};

struct TimingDecision
{
	PipelineEpoch epoch;
	bool valid = true;
	uint64_t streamFrameNumber = 0;
	int64_t mediaStart = 0;
	int64_t mediaStop = 1;
	bool discontinuity = false;
	bool hasStart = false;
	bool hasStop = false;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;
	uint32_t hardwareTimestampAnomalyCount = 0;
};

class VideoTimingController
{
public:
	static constexpr uint64_t kReferenceTimeTicksPerSecond = 10000000ULL;
	static constexpr uint64_t kPpmDenominator = 1000000ULL;
	static constexpr size_t kDurationHistorySize = 100;

	explicit VideoTimingController(const VideoTimingControllerConfig& config);

	void Configure(const VideoTimingControllerConfig& config);
	void Reset();
	// The DirectShow coordinator owns the live queue epoch.  This lets the
	// value-only controller attach its decisions to that authoritative epoch.
	void ResetToEpoch(PipelineEpoch epoch);
	void RestartAfterPreroll();
	void ReplaceEpoch();
	PipelineEpoch Epoch() const { return m_epoch; }
	TimingDecision Decide(const FrameTimingInput& input);

	static VideoReferenceTime RationalTimestamp(
		uint64_t frameIndex,
		uint64_t frameDurationTicks,
		uint64_t timeScale,
		int ppmCorrection);
	static VideoReferenceTime ClockToReferenceTime(
		uint64_t timestamp,
		uint64_t ticksPerSecond);

private:
	bool UsesStopTime() const;
	bool UsesStartTime() const;
	bool RequiresHardwareTimestamp() const;
	VideoReferenceTime TheoreticalDuration() const;
	VideoReferenceTime SmartDuration() const;
	void AddDurationHistory(VideoReferenceTime duration);
	void ClearTimingState();
	uint64_t TrimNumerator(int ppmCorrection) const;

	VideoTimingControllerConfig m_config;
	PipelineEpoch m_epoch;
	bool m_frameOffsetValid = false;
	uint64_t m_frameOffset = 0;
	uint64_t m_previousSourceFrameNumber = 0;
	uint64_t m_frameCount = 0;
	bool m_forceDiscontinuity = false;
	bool m_startOffsetValid = false;
	VideoReferenceTime m_startOffset = 0;
	VideoReferenceTime m_previousStop = 0;
	VideoReferenceTime m_rationalFrameDuration = 0;
	VideoReferenceTime m_minFrameAdvance = 0;
	VideoReferenceTime m_maxFrameAdvance = 0;
	VideoReferenceTime m_lastHardwareTimestamp = 0;
	uint32_t m_hardwareTimestampAnomalyCount = 0;
	std::array<VideoReferenceTime, kDurationHistorySize> m_durationHistory{};
	size_t m_durationHistoryIndex = 0;
	size_t m_durationHistoryCount = 0;
	VideoReferenceTime m_durationHistorySum = 0;
};
