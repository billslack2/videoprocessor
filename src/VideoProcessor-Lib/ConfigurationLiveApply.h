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
}
