#pragma once

#include <cstdint>

namespace BlackMagicDeckLinkEotfPolicy
{
	constexpr int64_t INVALID_VALUE = -1;

	struct State
	{
		int64_t acceptedValue = INVALID_VALUE;
		int64_t lastIgnoredValue = INVALID_VALUE;
	};

	struct Decision
	{
		State next;
		bool videoStateChanged = false;
		bool logIgnoredValue = false;
	};

	// Retain only CTA-861.3 EOTF values 0-3. Invalid metadata neither
	// replaces the last accepted value nor causes a video-state transition.
	inline Decision Observe(State current, int64_t reportedValue) noexcept
	{
		Decision decision{ current };
		if (reportedValue < 0 || reportedValue > 3)
		{
			decision.logIgnoredValue =
				current.lastIgnoredValue != reportedValue;
			decision.next.lastIgnoredValue = reportedValue;
			return decision;
		}

		decision.next.lastIgnoredValue = INVALID_VALUE;
		if (current.acceptedValue != reportedValue)
		{
			decision.next.acceptedValue = reportedValue;
			decision.videoStateChanged = true;
		}
		return decision;
	}
}
