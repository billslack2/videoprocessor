#pragma once

#include <VideoTimingController.h>
#include <microsoft_directshow/DirectShowRendererStartStopTimeMethod.h>

// The one deliberate DirectShow boundary for VP-0066-2.  It translates the
// existing public timestamp-mode names into the graph-independent controller;
// it does not own a pin, sample, queue, or clock object.
struct DirectShowFrameTimingInput
{
	FrameTimingInput timing;
	// The pin's existing GetRampedLeadTime() result.  It is presentation-only:
	// never feed it into controller monotonic/rational base-time state.
	VideoReferenceTime presentationLead = 0;
};

struct DirectShowTimingDecision
{
	TimingDecision base;
	bool hasStart = false;
	bool hasStop = false;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;
};

struct DirectShowLiveCatchUpDecision
{
	bool adjusted = false;
	bool rebased = false;
	VideoReferenceTime start = 0;
	VideoReferenceTime stop = 0;
	VideoReferenceTime offset = 0;
};

// Delivery-thread-only continuity state for legacy timestamp modes. Startup
// samples are stamped during conversion, before the downstream renderer has
// accepted its initial reservoir. If the one-shot live convergence transaction
// removes stale converted samples, their timestamp span must be removed at the
// final delivery boundary as well. The first retained sample is joined to the
// last successfully delivered stop; the same offset is then applied for the
// remainder of that graph epoch.
class DirectShowLiveTimestampCatchUp
{
public:
	void ResetToEpoch(uint64_t epoch) noexcept;
	void Arm(uint64_t epoch) noexcept;
	DirectShowLiveCatchUpDecision Adjust(
		uint64_t epoch,
		VideoReferenceTime start,
		VideoReferenceTime stop) noexcept;
	void CommitSuccessfulStop(
		uint64_t epoch,
		VideoReferenceTime stop) noexcept;

private:
	uint64_t m_epoch = 0;
	bool m_pending = false;
	bool m_lastStopValid = false;
	VideoReferenceTime m_lastStop = 0;
	VideoReferenceTime m_offset = 0;
};

class DirectShowVideoTimingAdapter
{
public:
	DirectShowVideoTimingAdapter(
		DirectShowStartStopTimeMethod mode,
		uint32_t timeScale,
		uint32_t frameDurationTicks,
		VideoReferenceTime theoreticalFrameDuration);

	DirectShowTimingDecision Decide(const DirectShowFrameTimingInput& input);
	void Reset() { m_controller.Reset(); }
	void ResetToEpoch(PipelineEpoch epoch) { m_controller.ResetToEpoch(epoch); }
	void RestartAfterPreroll() { m_controller.RestartAfterPreroll(); }
	PipelineEpoch Epoch() const { return m_controller.Epoch(); }

	static VideoTimingMode ToVideoTimingMode(DirectShowStartStopTimeMethod mode);
	static bool UsesPresentationLead(DirectShowStartStopTimeMethod mode);
	static bool UsesLiveTimestampCatchUp(DirectShowStartStopTimeMethod mode);
	static bool UsesStartOnlyLiveTimestampCatchUp(DirectShowStartStopTimeMethod mode);

private:
	DirectShowStartStopTimeMethod m_mode;
	VideoTimingController m_controller;
};
