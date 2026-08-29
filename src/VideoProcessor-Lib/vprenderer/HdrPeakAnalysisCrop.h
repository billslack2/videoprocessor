#pragma once

#include <libplacebo/common.h>

#include <algorithm>
#include <cmath>
#include <cstdint>


namespace HdrPeakAnalysisCrop
{
	constexpr double DEFAULT_ANALYSIS_HEIGHT_PERCENT = 75.0;
	constexpr double MIN_ANALYSIS_HEIGHT_PERCENT = 10.0;
	constexpr double MAX_ANALYSIS_HEIGHT_PERCENT = 100.0;

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
		const pl_rect2df& presentation,
		double analysisHeightPercent = DEFAULT_ANALYSIS_HEIGHT_PERCENT)
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
		if (!std::isfinite(analysisHeightPercent) ||
			analysisHeightPercent < MIN_ANALYSIS_HEIGHT_PERCENT ||
			analysisHeightPercent > MAX_ANALYSIS_HEIGHT_PERCENT)
		{
			decision.reason = "analysis height percentage is invalid";
			return decision;
		}

		const double trustedHeight = static_cast<double>(
			trusted.bottom - trusted.top);
		const double analysisHeightFraction = analysisHeightPercent / 100.0;
		const double verticalInset =
			trustedHeight * (1.0 - analysisHeightFraction) * 0.5;
		const float analysisTop = static_cast<float>(trusted.top + verticalInset);
		const float analysisBottom = static_cast<float>(trusted.bottom - verticalInset);

		decision.trustedIntersection = {
			std::max(presentation.x0, static_cast<float>(trusted.left)),
			std::max(presentation.y0, analysisTop),
			std::min(presentation.x1, static_cast<float>(trusted.right)),
			std::min(presentation.y1, analysisBottom)
		};
		if (decision.trustedIntersection.x0 >= decision.trustedIntersection.x1 ||
			decision.trustedIntersection.y0 >= decision.trustedIntersection.y1)
		{
			decision.reason = "central active-picture band does not intersect presentation crop";
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
			decision.reason = "presentation is already inside central active-picture band";
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
		decision.reason = "configured central active-picture height restricts HDR analysis";
		return decision;
	}

	// Automatic motion compensation is deliberately separate from Resolve so
	// the established fixed-percentage policy cannot change. Positive motion
	// reveals the lower bar; negative motion reveals the upper bar. The motion
	// magnitude already includes VP's presentation padding/buffer.
	inline Decision ResolveMotionCompensated(bool enabled,
		bool peakDetectionActive, uint64_t frameSourceGeneration,
		const TrustedPicture& trusted, const pl_rect2df& presentation,
		double signedProtectionPixels)
	{
		Decision decision;
		if (!enabled)
			return decision;
		if (!std::isfinite(signedProtectionPixels))
		{
			decision.outcome = Outcome::FALLBACK;
			decision.reason = "motion protection is invalid";
			return decision;
		}
		if (std::abs(signedProtectionPixels) <= 0.5)
		{
			decision.outcome = Outcome::FULL_PRESENTATION;
			decision.reason = "no pending or active subtitle movement";
			return decision;
		}

		// Establish the current trusted active-picture/presentation intersection
		// without a percentage inset, then trim only the movement-owning edge.
		decision = Resolve(true, peakDetectionActive, frameSourceGeneration,
			trusted, presentation, MAX_ANALYSIS_HEIGHT_PERCENT);
		if (decision.outcome == Outcome::FALLBACK)
			return decision;

		const double protectionPixels = std::abs(signedProtectionPixels);
		if (signedProtectionPixels > 0.0)
			decision.trustedIntersection.y1 = std::min(
				decision.trustedIntersection.y1,
				static_cast<float>(trusted.bottom - protectionPixels));
		else
			decision.trustedIntersection.y0 = std::max(
				decision.trustedIntersection.y0,
				static_cast<float>(trusted.top + protectionPixels));

		if (decision.trustedIntersection.x0 >= decision.trustedIntersection.x1 ||
			decision.trustedIntersection.y0 >= decision.trustedIntersection.y1)
		{
			decision.outcome = Outcome::FALLBACK;
			decision.normalizedCrop = {};
			decision.reason = "motion-protected active-picture band is empty";
			return decision;
		}

		const double presentationWidth = presentation.x1 - presentation.x0;
		const double presentationHeight = presentation.y1 - presentation.y0;
		const double presentationArea = presentationWidth * presentationHeight;
		const double includedArea =
			(decision.trustedIntersection.x1 - decision.trustedIntersection.x0) *
			(decision.trustedIntersection.y1 - decision.trustedIntersection.y0);
		decision.excludedSourcePixels = std::max(0.0,
			presentationArea - includedArea);
		decision.excludedFraction = std::max(0.0, std::min(1.0,
			decision.excludedSourcePixels / presentationArea));
		if (decision.excludedFraction <= 1e-6)
		{
			decision.outcome = Outcome::FULL_PRESENTATION;
			decision.normalizedCrop = {};
			decision.reason = "presentation already excludes subtitle movement edge";
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
			decision.outcome = Outcome::FALLBACK;
			decision.normalizedCrop = {};
			decision.reason = "normalized motion-compensated crop is invalid";
			return decision;
		}

		decision.outcome = Outcome::RESTRICTED;
		decision.reason = signedProtectionPixels > 0.0
			? "subtitle movement protects lower active-picture edge"
			: "subtitle movement protects upper active-picture edge";
		return decision;
	}

	inline Decision ResolvePolicy(bool fixedPercentageEnabled,
		bool motionCompensationEnabled, bool peakDetectionActive,
		uint64_t frameSourceGeneration, const TrustedPicture& trusted,
		const pl_rect2df& presentation, double analysisHeightPercent,
		double signedProtectionPixels)
	{
		// Fixed mode is the established behavior and always wins. Keeping this
		// precedence at the policy seam makes a simultaneously configured
		// experimental option incapable of changing its rectangle.
		if (fixedPercentageEnabled)
			return Resolve(true, peakDetectionActive, frameSourceGeneration,
				trusted, presentation, analysisHeightPercent);
		return ResolveMotionCompensated(motionCompensationEnabled,
			peakDetectionActive, frameSourceGeneration, trusted, presentation,
			signedProtectionPixels);
	}
}
