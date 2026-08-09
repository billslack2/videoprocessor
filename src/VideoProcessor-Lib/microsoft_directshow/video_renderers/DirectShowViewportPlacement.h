/*
 * Windowed DirectShow/madVR scope-canvas placement policy.
 *
 * madVR's screen-config masked borders apply in fullscreen, but its windowed
 * child window otherwise fills VP's host window.  When NLS is active, give a
 * wider configured viewport an equivalent windowed canvas by reducing the
 * child window height and centering it vertically.  The host remains the
 * owner, so returning the native rectangle is always safe.
 */
#pragma once

#include <cmath>

struct DirectShowViewportPlacement
{
	long x = 0;
	long y = 0;
	long width = 0;
	long height = 0;
	bool usesWindowedScopeCanvas = false;
};

inline DirectShowViewportPlacement ResolveDirectShowViewportPlacement(
	long hostWidth, long hostHeight, bool fullscreen,
	bool nlsRequested, double targetAspect)
{
	DirectShowViewportPlacement placement;
	placement.width = hostWidth;
	placement.height = hostHeight;
	if (hostWidth <= 0 || hostHeight <= 0 || fullscreen || !nlsRequested ||
		!std::isfinite(targetAspect) || targetAspect <= 1.0)
		return placement;

	const double hostAspect = static_cast<double>(hostWidth) / hostHeight;
	// Only a wider target creates a scope canvas.  A narrower target would
	// require cropping or horizontal letterboxing, neither of which VP owns.
	if (targetAspect <= hostAspect + 0.0001)
		return placement;

	const long scopeHeight = static_cast<long>(std::lround(
		static_cast<double>(hostWidth) / targetAspect));
	if (scopeHeight <= 0 || scopeHeight >= hostHeight)
		return placement;

	placement.y = (hostHeight - scopeHeight) / 2;
	placement.height = scopeHeight;
	placement.usesWindowedScopeCanvas = true;
	return placement;
}
