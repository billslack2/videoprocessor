#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

class DirectShowPresentationLeadPolicy
{
public:
	static constexpr int64_t kLegacyLeadTime100ns = 180LL * 10000LL;
	static constexpr size_t kMaximumFrames = 16;

	static int64_t Resolve100ns(bool configured, size_t frames,
		int64_t nominalFrameDuration100ns, bool legacyModeUsesLead)
	{
		if (!configured)
			return legacyModeUsesLead ? kLegacyLeadTime100ns : 0;
		if (nominalFrameDuration100ns <= 0)
			return 0;
		const size_t boundedFrames =
			(std::min)(frames, kMaximumFrames);
		return static_cast<int64_t>(boundedFrames) *
			nominalFrameDuration100ns;
	}
};
