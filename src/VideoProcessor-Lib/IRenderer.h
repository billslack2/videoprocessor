/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <VideoFrame.h>
#include <VideoState.h>
#include <SubtitleRepositionMode.h>
#include <vector>

namespace UnifiedProfileRuntime
{
	struct Snapshot;
}


enum RendererState
{
	RENDERSTATE_READY,
	RENDERSTATE_RENDERING,
	RENDERSTATE_STOPPED,

	// States which will not be sent by the card but which can be used
	// by clients when they are expecting a callback for example
	RENDERSTATE_UNKNOWN,
	RENDERSTATE_STOPPING,
	RENDERSTATE_STARTING,
	RENDERSTATE_FAILED
};


const TCHAR* ToString(const RendererState rendererState);


/**
 * Renderer callback
 */
struct IRendererCallback
{
	// Note that this will be synchronous with calls to the renderer. No external thread
	// will cause calls. Most likely this will be a result of OnWindowsEvent() and Stop() handling.
	virtual void OnRendererState(RendererState rendererState) = 0;

	// The renderer can report a human-readable string to say what it's doing
	// No need to do anything but just display.
	virtual void OnRendererDetailString(const CString& details) = 0;
};


/**
 * Video renderer interface
 */
class IVideoRenderer
{
public:

	virtual ~IVideoRenderer() {}

	// Add to the Queues section
	virtual size_t GetConvertedQueueSize() = 0;

	//
	// Runtime
	//

	// Update the video information.
	// The renderer can decide to stop after this and it will signal so by returning false,
	// a return of true means the new state was accepted.
	// ! Only can be called if Start() exectued correctly and before Stop() is called
	virtual bool OnVideoState(VideoStateComPtr&) = 0;

	// True when EOTF/colorspace/HDR metadata changes can be applied without a
	// renderer restart. Display-mode and pixel-format changes may still reject
	// OnVideoState and request a rebuild.
	virtual bool SupportsDynamicVideoState() const { return false; }

	// Draw the current buffer as frame
	// VideoFrames can be buffered and they can be internally refcounted, hence non-constant
	// ! Only can be called if Start() exectued correctly and before Stop() is called
	virtual void OnVideoFrame(VideoFrame&) = 0;

	// Handler for windows events for the graph's pEvent
	virtual HRESULT OnWindowsEvent(LONG_PTR param1, LONG_PTR param2) = 0;

	// Construct the graph
	virtual void Build() = 0;

	// Ask the renderer to start, this can take some time and you'll get notified
	// through the IRendererCallback
	virtual void Start() = 0;

	// Ask the renderer to stop, this can take some time and you'll get notified
	// through the IRendererCallback
	virtual void Stop() = 0;

	// Reset the internal state and the video stream.
	virtual void Reset() = 0;

	// Flush and re-prime only the live-source queue without rebuilding the
	// renderer graph. Renderers without a live source may ignore this request.
	virtual void ResetLiveQueue() {}

	//
	// GUI
	//

	// Window got resized
	virtual void OnSize() = 0;

	// Window needs repainting
	virtual void OnPaint() = 0;

	// Display mode/output capabilities changed without necessarily resizing the
	// render window. Renderers that negotiate an output contract may refresh it.
	virtual void OnDisplayChange() {}

	//
	// Queues
	//

	// Set the video frame queue max size.
	// Only valid te be called if the RendererState called back RENDERSTATE_RENDERING
	// Queues might not be implemented by all renderers, this will throw if it cannot be set
	virtual void SetFrameQueueMaxSize(size_t) = 0;

	// Prefer visually-safe scene boundaries when dropping a late queued frame.
	// Disabled by default; enabled only by the renderer queue UI.
	virtual void SetSceneAwareTimingCorrection(bool) = 0;
	// Select the full output raster or an optional configured screen viewport
	// (for example a constant-image-height scope screen). Renderers without
	// screen-profile support return false.
	virtual bool SetScreenProfile(bool scopeScreen, CString& activeProfile,
		bool& rendererRestartRequired)
	{
		activeProfile.Empty();
		rendererRestartRequired = false;
		return false;
	}
	// Applies one coherent application-owned profile/runtime snapshot. Renderer
	// backends consume this state but never resolve keys or persist profiles.
	virtual bool ApplyApplicationState(
		const UnifiedProfileRuntime::Snapshot& snapshot,
		CString& activeState, bool& rendererRestartRequired)
	{
		activeState.Empty();
		rendererRestartRequired = false;
		return false;
	}
	// Select a named renderer display profile, or "auto" to return to the
	// configured input-driven display rules.  A selected profile is a manual
	// override and may require the renderer to rebuild safely.
	virtual bool SelectDisplayRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired)
	{
		activeRule.Empty();
		rendererRestartRequired = false;
		return false;
	}
	// Select a renderer-managed external shader rule at runtime. Renderers that
	// do not expose a compatible shader interface return false.
	virtual bool SelectShaderRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired)
	{
		activeRule.Empty();
		rendererRestartRequired = false;
		return false;
	}
	// Re-evaluates an armed conditional shader rule against live content.
	// Returns true only when its applied/bypassed state changed.
	virtual bool RefreshShaderRule(CString& activeRule,
		bool& rendererRestartRequired)
	{
		activeRule.Empty();
		rendererRestartRequired = false;
		return false;
	}
	virtual void SetSceneCorrectionUpstreamSample(bool) {}
	// Optional burned-in subtitle relocation. Renderers without a writable live
	// source may ignore this request.
	virtual void SetSubtitleRepositioning(bool) {}
	virtual void SetSubtitleRepositioningMode(SubtitleRepositionMode mode)
	{
		SetSubtitleRepositioning(mode != SubtitleRepositionMode::DISABLED);
	}

	// Supply the measured display refresh and capture rates used by the
	// scene-aware whole-frame phase predictor. Renderers that do not implement
	// scene correction may ignore these values.
	virtual void SetSceneTimingRates(double, double) {}
	virtual void SetSceneTimingReadiness(bool, uint64_t) {}

	// Supply the physical display vblank phase in QPC ticks.  This is optional
	// diagnostic/control information for scene-aware timing correction; normal
	// renderers may ignore it.
	virtual void SetSceneTimingPhase(int64_t, int64_t, int64_t) {}

	// Get the current frame queue size, negative means no queue
	// Only valid te be called if the RendererState called back RENDERSTATE_RENDERING
	// Queues might not be implemented by all renderers, this will return 0 if there is no queueing possible.
	virtual size_t GetFrameQueueSize() = 0;

	//
	// Metrics
	//

	// Get the time it took in milliseconds from the frame's capture timestamp to the entry of the renderer
	// as measured by the given clock.
	// This value does not need to be per frame, sampling is fine
	// Only valid te be called if the RendererState called back RENDERSTATE_RENDERING
	virtual double EntryLatencyMs() const = 0;

	// Get the time it took in milliseconds from the frame's capture timestamp to the point where we hand it
	// over to the bit which puts the image on the wire. It's the furthest possible timestamp we can take.
	// This value does not need to be per frame, sampling is fine
	// Only valid te be called if the RendererState called back RENDERSTATE_RENDERING
	virtual double ExitLatencyMs() const = 0;

	// Get the amount of dropped frames due to queue actions
	virtual uint64_t DroppedFrameCount() const = 0;

	// Requested and negotiated renderer output presentation/range/transfer.
	// Renderers without an explicit output contract return false.
	virtual bool GetOutputModeInfo(CString& details) const
	{
		details.Empty();
		return false;
	}

	// Concise display-calibration LUT status for the Ctrl+I OSD. Renderers
	// without this feature return false so the OSD does not imply support.
	virtual bool GetDisplayLutInfo(CString& details) const
	{
		details.Empty();
		return false;
	}

	// Optional renderer-native OSD. The renderer must deep-copy the BGRA pixels
	// before returning; callers retain ownership of the supplied buffer.
	virtual bool SupportsNativeStatsOverlay() const { return false; }
	virtual bool SetNativeStatsOverlay(const uint8_t*, size_t, int, int, int)
	{
		return false;
	}

	// Source-side whole-frame actions moved to a detected scene boundary.
	virtual uint64_t SceneAwareCorrectionDropCount() const { return 0; }
	virtual uint64_t SceneAwareCorrectionRepeatCount() const { return 0; }
	virtual uint64_t SceneAwareDetectedCount() const { return 0; }
	// Optional renderer-native detector lifecycle state. The UI falls back to
	// its existing configuration label when a renderer does not provide this.
	virtual bool GetSceneDetectionStatus(CString& status) const
	{
		status.Empty();
		return false;
	}
	virtual uint64_t SceneAwareLateCandidateCount() const { return 0; }
	// Successfully installed renderer-native shader chain, formatted for display
	// as entries such as "Pre: Debanding" or "Post: Adaptive sharpen".
	virtual std::vector<CString> ActiveShaders() const { return {}; }
	// Human-readable label for the shader rule selected for this renderer run.
	virtual CString ActiveShaderRule() const { return TEXT("None"); }
	// Live prediction of the next scene-aware whole-frame correction.  action is
	// +1 for a repeat, -1 for a drop, and 0 when no prediction is available.
	// planned becomes true once the delivery thread is actively seeking a safe
	// scene boundary within its correction window.
	virtual bool GetSceneTimingPrediction(double& secondsUntilCorrection,
		double& secondsUntilPlan, int& action, bool& planned) const
	{
		secondsUntilCorrection = 0.0;
		secondsUntilPlan = 0.0;
		action = 0;
		planned = false;
		return false;
	}
	// Most recent scene-boundary correction. secondsFromDeadline is positive
	// when applied early, negative when it was overdue.
	virtual bool GetSceneTimingLastCorrection(int& action,
		double& secondsFromDeadline, uint64_t& correctionTick) const
	{
		action = 0;
		secondsFromDeadline = 0.0;
		correctionTick = 0;
		return false;
	}
	virtual bool SceneTimingRatesCompatible() const { return false; }
	// Optional renderer-native timing lifecycle. This distinguishes a stable
	// display measurement from a correction forecast that is intentionally
	// suppressed because the measured mismatch is negligible.
	virtual bool GetSceneTimingStatus(CString& status) const
	{
		status.Empty();
		return false;
	}

	// Get conversion performance metrics (if available from the video frame formatter)
	// Returns true if data is available, false if no conversion or no performance tracking
	// currentUs: Latest frame conversion time in microseconds
	// avg10s: Average conversion time over last 10 seconds (?s)
	// max10s: Maximum conversion time over last 10 seconds (?s)
	virtual bool GetConversionPerformance(double& currentUs, double& avg10s, double& max10s) const
	{
		currentUs = 0.0;
		avg10s = 0.0;
		max10s = 0.0;
		return false;  // No data available by default
	}
	
	// Get PPM correction information (if supported by the timing mode)
	// Returns true if PPM correction data is available, false if not supported
	// ppmValue: Current PPM correction value being applied (positive = faster, negative = slower)
	// hasCorrection: Whether a non-zero PPM correction is being applied
	// source: Source of the PPM correction ("VideoProcessor.cfg", "default", "N/A")
	virtual bool GetPPMCorrectionInfo(int& ppmValue, bool& hasCorrection, CString& source) const
	{
		ppmValue = 0;
		hasCorrection = false;
		source = TEXT("N/A");
		return false;  // No PPM correction support by default
	}

	// Get frame rate measurement and PPM deviation (for timing diagnostics)
	// Returns true if frame rate measurement data is available, false if not supported
	// measuredFps: Actual measured frame rate (Hz)
	// ppmDeviation: PPM deviation between theoretical and measured rates (parts per million)
	virtual bool GetFrameRateAndPPM(double& measuredFps, int& ppmDeviation) const
	{
		measuredFps = 0.0;
		ppmDeviation = 0;
		return false;  // No frame rate measurement by default
	}
};
