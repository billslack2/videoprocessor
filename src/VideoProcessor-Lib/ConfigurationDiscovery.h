#pragma once

#include <string>
#include <vector>

namespace ConfigurationDiscovery
{
	// Read-only machine discovery shared by VideoProcessor and its standalone
	// configuration editor. Returned renderer order is the exact one-based order
	// used by VP's renderer selector and action renderer indices.
	std::vector<std::wstring> RendererNames(bool hideLegacyRenderers = true);
	std::vector<std::wstring> CaptureDeviceNames();
	std::vector<std::wstring> CaptureConnectionNames(
		const std::wstring& captureDeviceName);
	std::vector<std::wstring> ActiveMonitorNames();
}
