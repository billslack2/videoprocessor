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

private:
	DirectShowStartStopTimeMethod m_mode;
	VideoTimingController m_controller;
};
