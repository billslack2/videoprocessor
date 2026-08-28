#pragma once

#include <libplacebo/common.h>

#include <algorithm>
#include <cmath>
#include <cstdint>


namespace HdrPeakAnalysisCrop
{
	enum class Outcome
	{
		DISABLED,
		FALLBACK,
		FULL_PRESENTATION,
		RESTRICTED
	};

	inline const char* OutcomeName(Outcome outcome)
	{
		switch (outcome)
		{
		case Outcome::DISABLED: return "disabled";
		case Outcome::FALLBACK: return "fallback-full-frame";
		case Outcome::FULL_PRESENTATION: return "full-presentation";
		case Outcome::RESTRICTED: return "restricted";
		default: return "unknown";
		}
	}

	struct TrustedPicture
	{
		int left = 0;
		int top = 0;
		int right = 0;
		int bottom = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
		uint64_t sourceGeneration = 0;
		bool available = false;
	};

	struct Decision
	{
		Outcome outcome = Outcome::DISABLED;
		pl_rect2df normalizedCrop{};
		pl_rect2df trustedIntersection{};
		double excludedFraction = 0.0;
		double excludedSourcePixels = 0.0;
		const char* reason = "disabled";

		bool AppliesRestriction() const
		{
			return outcome == Outcome::RESTRICTED;
		}
	};

	inline bool IsFiniteRect(const pl_rect2df& rectangle)
	{
		return std::isfinite(rectangle.x0) && std::isfinite(rectangle.y0) &&
			std::isfinite(rectangle.x1) && std::isfinite(rectangle.y1);
	}

	inline Decision Resolve(bool enabled, bool peakDetectionActive,
		uint64_t frameSourceGeneration, const TrustedPicture& trusted,
		const pl_rect2df& presentation)
	{
		Decision decision;
		if (!enabled)
			return decision;

		decision.outcome = Outcome::FALLBACK;
		if (!peakDetectionActive)
		{
			decision.reason = "peak detection is inactive";
			return decision;
		}
		if (!trusted.available || trusted.sourceGeneration != frameSourceGeneration)
		{
			decision.reason = "no current trusted active picture";
			return decision;
		}
		if (trusted.rasterWidth <= 0 || trusted.rasterHeight <= 0 ||
			trusted.left < 0 || trusted.top < 0 ||
			trusted.right > trusted.rasterWidth ||
			trusted.bottom > trusted.rasterHeight ||
			trusted.left >= trusted.right || trusted.top >= trusted.bottom)
		{
			decision.reason = "trusted active-picture geometry is invalid";
			return decision;
		}
		if (!IsFiniteRect(presentation) || presentation.x0 >= presentation.x1 ||
			presentation.y0 >= presentation.y1)
		{
			decision.reason = "presentation crop is invalid";
			return decision;
		}

		decision.trustedIntersection = {
			std::max(presentation.x0, static_cast<float>(trusted.left)),
			std::max(presentation.y0, static_cast<float>(trusted.top)),
			std::min(presentation.x1, static_cast<float>(trusted.right)),
			std::min(presentation.y1, static_cast<float>(trusted.bottom))
		};
		if (decision.trustedIntersection.x0 >= decision.trustedIntersection.x1 ||
			decision.trustedIntersection.y0 >= decision.trustedIntersection.y1)
		{
			decision.reason = "trusted picture does not intersect presentation crop";
			return decision;
		}

		const double presentationWidth =
			static_cast<double>(presentation.x1 - presentation.x0);
		const double presentationHeight =
			static_cast<double>(presentation.y1 - presentation.y0);
		const double includedWidth = static_cast<double>(
			decision.trustedIntersection.x1 - decision.trustedIntersection.x0);
		const double includedHeight = static_cast<double>(
			decision.trustedIntersection.y1 - decision.trustedIntersection.y0);
		const double presentationArea = presentationWidth * presentationHeight;
		const double includedArea = includedWidth * includedHeight;
		decision.excludedSourcePixels = std::max(0.0,
			presentationArea - includedArea);
		decision.excludedFraction = std::max(0.0, std::min(1.0,
			decision.excludedSourcePixels / presentationArea));

		constexpr double minimumMeaningfulExclusion = 1e-6;
		if (decision.excludedFraction <= minimumMeaningfulExclusion)
		{
			decision.outcome = Outcome::FULL_PRESENTATION;
			decision.reason = "presentation is already inside trusted picture";
			return decision;
		}

		decision.normalizedCrop = {
			static_cast<float>((decision.trustedIntersection.x0 - presentation.x0) /
				presentationWidth),
			static_cast<float>((decision.trustedIntersection.y0 - presentation.y0) /
				presentationHeight),
			static_cast<float>((decision.trustedIntersection.x1 - presentation.x0) /
				presentationWidth),
			static_cast<float>((decision.trustedIntersection.y1 - presentation.y0) /
				presentationHeight)
		};
		if (!IsFiniteRect(decision.normalizedCrop) ||
			decision.normalizedCrop.x0 < 0.0f ||
			decision.normalizedCrop.y0 < 0.0f ||
			decision.normalizedCrop.x1 > 1.0f ||
			decision.normalizedCrop.y1 > 1.0f ||
			decision.normalizedCrop.x0 >= decision.normalizedCrop.x1 ||
			decision.normalizedCrop.y0 >= decision.normalizedCrop.y1)
		{
			decision.normalizedCrop = {};
			decision.reason = "normalized analysis crop is invalid";
			return decision;
		}

		decision.outcome = Outcome::RESTRICTED;
		decision.reason = "trusted active picture restricts HDR analysis";
		return decision;
	}
}
