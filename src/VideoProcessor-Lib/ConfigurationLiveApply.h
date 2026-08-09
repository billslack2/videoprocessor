#pragma once

#include <cstdint>

namespace ConfigurationLiveApply
{
	static const wchar_t ChangedEventName[] =
		L"Local\\VideoProcessor.ConfigurationChanged.v1";

	// Delayed fullscreen placement may restore focus only while VideoProcessor
	// already owns the foreground. This prevents a renderer transition from
	// taking keyboard focus from the separately owned configuration editor (or
	// from any other application the operator has selected).
	inline bool MayActivateFullscreen(uint32_t videoProcessorProcessId,
		uint32_t foregroundProcessId, bool hasForegroundWindow)
	{
		return !hasForegroundWindow ||
			foregroundProcessId == videoProcessorProcessId;
	}

	inline bool MayDispatchGlobalShortcut(uint32_t videoProcessorProcessId,
		uint32_t foregroundProcessId, bool configurationModal)
	{
		return !configurationModal && foregroundProcessId != 0 &&
			foregroundProcessId != videoProcessorProcessId;
	}

	inline bool MayDispatchInjectedShortcut(bool injected,
		uint32_t videoProcessorProcessId, uint32_t foregroundProcessId)
	{
		return injected && MayDispatchGlobalShortcut(videoProcessorProcessId,
			foregroundProcessId, false);
	}

	inline bool ShortcutModifiersMatch(bool expectedControl,
		bool expectedAlt, bool expectedShift, bool control, bool alt,
		bool shift)
	{
		return expectedControl == control && expectedAlt == alt &&
			expectedShift == shift;
	}

	inline bool ShouldEnableBackgroundShortcuts(bool modernInterface,
		bool noUi)
	{
		return modernInterface && !noUi;
	}

	inline bool ShouldSuppressFullscreenTopmost(bool activationPending,
		bool configurationModal, bool editorVisible)
	{
		return activationPending || configurationModal || editorVisible;
	}

	inline bool ShouldPromoteFullscreenAfterLiveFrame(bool exclusiveFullscreen,
		bool fullscreenRequested, bool liveFrameVerified,
		bool suppressTopmost)
	{
		return exclusiveFullscreen && fullscreenRequested &&
			liveFrameVerified && !suppressTopmost;
	}

	inline bool ShouldRecreateFullscreenHostForBackendHandoff(
		bool hasPreviousRenderer, bool previousWasDirectShow,
		bool nextIsDirectShow, bool fullscreenHostExists,
		bool preserveHostForProfileRestart)
	{
		return hasPreviousRenderer && !previousWasDirectShow &&
			nextIsDirectShow && fullscreenHostExists &&
			!preserveHostForProfileRestart;
	}

	inline bool MayEnterConfigurationModal(bool editorVisible,
		bool editorIconic, uint32_t editorProcessId,
		uint32_t foregroundProcessId)
	{
		return editorVisible && !editorIconic && editorProcessId != 0 &&
			editorProcessId == foregroundProcessId;
	}

	inline bool IsDuplicateBackgroundShortcut(uint16_t lastCommand,
		uint16_t candidateCommand, uint64_t elapsedMilliseconds,
		uint64_t duplicateWindowMilliseconds)
	{
		return lastCommand != 0 && lastCommand == candidateCommand &&
			elapsedMilliseconds <= duplicateWindowMilliseconds;
	}

	inline bool MayDispatchWhileConfigurationModal(bool,
		bool)
	{
	// Config disables VP's foreground controls while it is visible, but VP's
	// configured background shortcuts must remain available.
		return true;
	}
}
