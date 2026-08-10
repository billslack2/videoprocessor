#pragma once

#include <cstdint>

namespace ApplicationShutdownPolicy
{
	enum class CloseSource
	{
		MainDialog,
		OperatorView,
		WindowedVideoHost,
		FullscreenHost,
		RendererSurface,
		StatsOverlay,
		OwnedTopLevel
	};

	enum class Lifecycle
	{
		Running,
		StoppingRenderer,
		RetiringRenderer,
		Terminating
	};

	struct CloseDecision
	{
		bool routeToCoordinator = false;
		bool consumeOriginal = false;
		bool preserveSourceSurface = false;
		bool advanceExistingTermination = false;
	};

	constexpr uintptr_t SystemCommandMask = 0xfff0;
	constexpr uintptr_t CloseSystemCommand = 0xf060;

	inline bool IsCloseSystemCommand(uintptr_t command)
	{
		return (command & SystemCommandMask) == CloseSystemCommand;
	}

	inline bool IsAltF4(uint32_t message, uintptr_t key, bool altDown)
	{
		return (message == 0x0104 || message == 0x0100) &&
			key == 0x73 && altDown;
	}

	inline CloseDecision ResolveClose(CloseSource, Lifecycle lifecycle)
	{
		return { true, true, true,
			lifecycle == Lifecycle::Terminating };
	}

	// DeckLink StopCapture is synchronous, but its separate busy-state
	// notification is not guaranteed to arrive during application teardown.
	// Once the renderer is retired and StopCapture returns, shutdown can safely
	// detach the callback/device without waiting for that advisory notification.
	inline bool MayFinalizeCaptureAfterStopReturns(bool terminationRequested,
		bool rendererRetired)
	{
		return terminationRequested && rendererRetired;
	}
}
