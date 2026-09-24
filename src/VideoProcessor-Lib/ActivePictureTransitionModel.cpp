#include <pch.h>

#include "ActivePictureTransitionModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{
std::atomic<double> g_runtimeStableGeometryDeadbandPercent(
	ActivePictureTransitionModel::DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT);
}


void ActivePictureTransitionModel::Reset()
{
	m_hasStable = false;
	m_stable = {};
	m_stableClassification = ActivePictureClassification::UNAVAILABLE;
	m_recentTrusted = {};
	m_recentTrustedCount = 0;
	ClearCandidate();
	m_unavailableCandidates = 0;
	m_lastAnalyzedFrame = 0;
	m_lastObservedFrame = 0;
}


void ActivePictureTransitionModel::ResetCandidateEvidence()
{
	ClearCandidate();
	m_unavailableCandidates = 0;
	m_lastAnalyzedFrame = 0;
}


void ActivePictureTransitionModel::SetStableGeometryDeadbandPercent(
	double percent)
{
	if (std::isfinite(percent) && percent >= 0.0 &&
		percent <= MAX_STABLE_GEOMETRY_DEADBAND_PERCENT)
		m_stableGeometryDeadbandPercent = percent;
}


void ActivePictureTransitionModel::SetRuntimeStableGeometryDeadbandPercent(
	double percent)
{
	if (std::isfinite(percent) && percent >= 0.0 &&
		percent <= MAX_STABLE_GEOMETRY_DEADBAND_PERCENT)
		g_runtimeStableGeometryDeadbandPercent.store(percent,
			std::memory_order_release);
}


double ActivePictureTransitionModel::GetRuntimeStableGeometryDeadbandPercent()
{
	return g_runtimeStableGeometryDeadbandPercent.load(
		std::memory_order_acquire);
}


uint64_t ActivePictureTransitionModel::AnalysisIntervalFrames(
	double framesPerSecond)
{
	if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0)
		framesPerSecond = 60.0;
	const uint64_t interval = static_cast<uint64_t>(
		std::llround(framesPerSecond * ANALYSIS_PERIOD_SECONDS));
	return std::max<uint64_t>(1, std::min<uint64_t>(6, interval));
}


bool ActivePictureTransitionModel::ShouldAnalyze(
	uint64_t frameNumber, double framesPerSecond)
{
	const uint64_t interval = m_candidateUsesKnownTrustedGeometry ?
		1 : AnalysisIntervalFrames(framesPerSecond);
	if (m_lastAnalyzedFrame != 0 && frameNumber <= m_lastAnalyzedFrame)
		return false;
	if (m_lastAnalyzedFrame != 0 &&
		frameNumber > m_lastAnalyzedFrame &&
		frameNumber - m_lastAnalyzedFrame < interval)
		return false;
	m_lastAnalyzedFrame = frameNumber;
	return true;
}


bool ActivePictureTransitionModel::SameBounds(
	const ActivePictureBounds& left,
	const ActivePictureBounds& right)
{
	if (left.rasterWidth != right.rasterWidth ||
		left.rasterHeight != right.rasterHeight ||
		left.rasterWidth <= 0 || left.rasterHeight <= 0)
		return false;
	const int tolerance = std::max(
		2, std::max(left.rasterWidth / 480, left.rasterHeight / 270));
	return std::abs(left.aspectRatio - right.aspectRatio) <= 0.025 &&
		std::abs(left.left - right.left) <= tolerance &&
		std::abs(left.top - right.top) <= tolerance &&
		std::abs(left.right - right.right) <= tolerance &&
		std::abs(left.bottom - right.bottom) <= tolerance;
}


bool ActivePictureTransitionModel::MateriallyDifferent(
	const ActivePictureBounds& left,
	const ActivePictureBounds& right)
{
	if (left.rasterWidth != right.rasterWidth ||
		left.rasterHeight != right.rasterHeight)
		return true;
	const int tolerance = std::max(
		4, std::max(left.rasterWidth / 240, left.rasterHeight / 135));
	return std::abs(left.aspectRatio - right.aspectRatio) >= 0.06 ||
		std::abs(left.left - right.left) > tolerance ||
		std::abs(left.top - right.top) > tolerance ||
		std::abs(left.right - right.right) > tolerance ||
		std::abs(left.bottom - right.bottom) > tolerance;
}


bool ActivePictureTransitionModel::WithinStableGeometryDeadband(
	const ActivePictureBounds& stable,
	const ActivePictureBounds& observation) const
{
	if (stable.rasterWidth != observation.rasterWidth ||
		stable.rasterHeight != observation.rasterHeight ||
		stable.rasterWidth <= 0 || stable.rasterHeight <= 0)
		return false;

	const double fraction = m_stableGeometryDeadbandPercent / 100.0;
	const int horizontalLimit = std::max(2, static_cast<int>(std::lround(
		stable.rasterWidth * fraction)));
	const int verticalLimit = std::max(2, static_cast<int>(std::lround(
		stable.rasterHeight * fraction)));
	const int stableWidth = stable.right - stable.left;
	const int stableHeight = stable.bottom - stable.top;
	const int observationWidth = observation.right - observation.left;
	const int observationHeight = observation.bottom - observation.top;

	// Limit each edge and the total active-size change. The latter prevents a
	// nominal 2% edge allowance from hiding a 4% contraction on both sides.
	return std::abs(stable.left - observation.left) <= horizontalLimit &&
		std::abs(stable.right - observation.right) <= horizontalLimit &&
		std::abs(stable.top - observation.top) <= verticalLimit &&
		std::abs(stable.bottom - observation.bottom) <= verticalLimit &&
		std::abs(stableWidth - observationWidth) <= horizontalLimit &&
		std::abs(stableHeight - observationHeight) <= verticalLimit;
}

ActivePicturePublicationAdmission ActivePictureTransitionModel::StableRetentionAdmission(
	const ActivePictureBounds& bounds,
	ActivePictureClassification classification) const
{
	if (!m_hasStable) return ActivePicturePublicationAdmission::ACCEPTED;
	if (WithinStableGeometryDeadband(m_stable, bounds))
		return ActivePicturePublicationAdmission::STABLE_GEOMETRY_RETAINED;

	// A contained inset cannot hide new picture outside the accepted frame.
	// Keep the established format through small AR drift or proportional
	// zoom-out. Outward growth, translations, and full-raster evidence retain
	// their normal admission and current-pixel visibility paths.
	if (m_stableClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
		classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
		bounds.rasterWidth != m_stable.rasterWidth ||
		bounds.rasterHeight != m_stable.rasterHeight ||
		bounds.left < m_stable.left || bounds.top < m_stable.top ||
		bounds.right > m_stable.right || bounds.bottom > m_stable.bottom)
		return ActivePicturePublicationAdmission::ACCEPTED;
	const int width = bounds.right - bounds.left;
	const int height = bounds.bottom - bounds.top;
	const int stableWidth = m_stable.right - m_stable.left;
	const int stableHeight = m_stable.bottom - m_stable.top;
	if (width <= 0 || height <= 0 || stableWidth <= 0 || stableHeight <= 0)
		return ActivePicturePublicationAdmission::ACCEPTED;
	// A rectangle separated from all four source edges is an inset composition,
	// not an unambiguous aspect-format boundary. Once a crop is established,
	// keep that presentation instead of zooming into picture-in-picture,
	// split-screen, credits, or an authored windowbox. Fresh source acquisition
	// remains free to establish its initial geometry.
	if (bounds.left > 0 && bounds.top > 0 &&
		bounds.right < bounds.rasterWidth &&
		bounds.bottom < bounds.rasterHeight)
	{
		return ActivePicturePublicationAdmission::CONTAINED_COMPOSITION_RETAINED;
	}
	// Derive aspect from pixels: cached aspectRatio can be temporally smoothed.
	const double relativeAspect = static_cast<double>(width) * stableHeight /
		(static_cast<double>(height) * stableWidth);
	return std::abs(relativeAspect - 1.0) * 100.0 <= STABLE_ASPECT_DEADBAND_PERCENT
		? ActivePicturePublicationAdmission::STABLE_ASPECT_RETAINED
		: ActivePicturePublicationAdmission::ACCEPTED;
}

bool ActivePictureTransitionModel::RetainsIncompleteInwardFormat(
	const ActivePictureBounds& candidate, const ActivePictureAxisEvidenceSet& evidence) const
{
	// Preserve initial acquisition (including a preceding full-raster/menu frame).
	// A failed orthogonal bar is not evidence for a new complete program aspect.
	// Fresh distributed side-picture support can disprove an all-sided inset
	// while granting only the independently verified full-width vertical crop.
	return m_hasStable && m_stableClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
		evidence.HasBlockingFailedBar(candidate) && candidate.rasterWidth == m_stable.rasterWidth &&
		candidate.rasterHeight == m_stable.rasterHeight &&
		candidate.left >= m_stable.left && candidate.top >= m_stable.top &&
		candidate.right <= m_stable.right && candidate.bottom <= m_stable.bottom &&
		candidate.left < candidate.right && candidate.top < candidate.bottom &&
		(candidate.left > m_stable.left || candidate.top > m_stable.top ||
		 candidate.right < m_stable.right || candidate.bottom < m_stable.bottom);
}

const char* ActivePicturePublicationAdmissionName(ActivePicturePublicationAdmission admission)
{
	switch (admission)
	{
	case ActivePicturePublicationAdmission::INCOMPLETE_AXIS_RETAINED: return "incomplete-axis-retained";
	case ActivePicturePublicationAdmission::NOT_EVALUATED: return "not-evaluated";
	case ActivePicturePublicationAdmission::ACCEPTED: return "accepted";
	case ActivePicturePublicationAdmission::DEFERRED: return "current-evidence-deferred";
	case ActivePicturePublicationAdmission::NON_AUTHORITATIVE: return "non-authoritative";
	case ActivePicturePublicationAdmission::STABLE_REFERENCE_MISMATCH: return "stable-reference-mismatch";
	case ActivePicturePublicationAdmission::STABLE_GEOMETRY_RETAINED: return "stable-geometry-deadband";
	case ActivePicturePublicationAdmission::CONTAINED_COMPOSITION_RETAINED: return "contained-composition";
	case ActivePicturePublicationAdmission::STABLE_ASPECT_RETAINED: return "stable-aspect-deadband";
	default: return "unknown";
	}
}

bool ActivePictureTransitionModel::HasCropAuthority(
	const ActivePictureObservation& observation)
{
	if (!observation.available)
		return false;
	const ActivePictureBounds& bounds = observation.bounds;
	if (bounds.rasterWidth <= 0 || bounds.rasterHeight <= 0 ||
		bounds.left < 0 || bounds.top < 0 ||
		bounds.right > bounds.rasterWidth ||
		bounds.bottom > bounds.rasterHeight ||
		bounds.left >= bounds.right || bounds.top >= bounds.bottom)
		return false;
	if (observation.classification ==
		ActivePictureClassification::FULL_RASTER_TRUSTED)
		return IsFullRaster(bounds);
	return observation.classification ==
		ActivePictureClassification::BAR_CROP_TRUSTED &&
		HasAuthorityForCroppedAxes(bounds) && !IsFullRaster(bounds);
}


bool ActivePictureTransitionModel::IsFullRaster(
	const ActivePictureBounds& bounds)
{
	return bounds.rasterWidth > 0 && bounds.rasterHeight > 0 &&
		bounds.left == 0 && bounds.top == 0 &&
		bounds.right == bounds.rasterWidth &&
		bounds.bottom == bounds.rasterHeight;
}


bool ActivePictureTransitionModel::HasAuthorityForCroppedAxes(
	const ActivePictureBounds& bounds)
{
	const uint8_t axes = static_cast<uint8_t>(bounds.trustedBarAxes);
	const bool cropsTop = bounds.top > 0;
	const bool cropsBottom = bounds.bottom < bounds.rasterHeight;
	const bool cropsLeft = bounds.left > 0;
	const bool cropsRight = bounds.right < bounds.rasterWidth;
	const bool cropsTopBottom = cropsTop || cropsBottom;
	const bool cropsLeftRight = cropsLeft || cropsRight;
	if (!cropsTopBottom && !cropsLeftRight)
		return false;
	if (cropsTopBottom && !(cropsTop && cropsBottom))
		return false;
	if (cropsLeftRight && !(cropsLeft && cropsRight))
		return false;
	if (cropsTopBottom &&
		(axes & static_cast<uint8_t>(
			ActivePictureBounds::BarAxes::TOP_BOTTOM)) == 0)
		return false;
	if (cropsLeftRight &&
		(axes & static_cast<uint8_t>(
			ActivePictureBounds::BarAxes::LEFT_RIGHT)) == 0)
		return false;
	return true;
}


void ActivePictureTransitionModel::RememberTrustedGeometry(
	const ActivePictureBounds& bounds,
	ActivePictureClassification classification)
{
	if (classification == ActivePictureClassification::UNAVAILABLE)
		return;
	for (size_t index = 0; index < m_recentTrustedCount; ++index)
	{
		if (!SameBounds(m_recentTrusted[index].bounds, bounds))
			continue;
		// Promote a returning geometry to the front instead of retaining a
		// duplicate, so recency remains meaningful for a three-mode feature.
		const TrustedGeometry known = m_recentTrusted[index];
		for (size_t move = index; move > 0; --move)
			m_recentTrusted[move] = m_recentTrusted[move - 1];
		m_recentTrusted[0] = known;
		return;
	}
	const size_t retained = std::min(
		m_recentTrustedCount, RECENT_TRUSTED_GEOMETRIES - 1);
	for (size_t move = retained; move > 0; --move)
		m_recentTrusted[move] = m_recentTrusted[move - 1];
	m_recentTrusted[0] = { bounds, classification };
	m_recentTrustedCount = std::min(
		RECENT_TRUSTED_GEOMETRIES, m_recentTrustedCount + 1);
}


bool ActivePictureTransitionModel::FindRecentTrustedGeometry(
	const ActivePictureObservation& observation,
	ActivePictureBounds& bounds,
	ActivePictureClassification& classification) const
{
	if (!m_hasStable ||
		!MateriallyDifferent(m_stable, observation.bounds))
	{
		return false;
	}
	for (size_t index = 0; index < m_recentTrustedCount; ++index)
	{
		const TrustedGeometry& known = m_recentTrusted[index];
		if (!SameBounds(known.bounds, observation.bounds))
			continue;
		bounds = known.bounds;
		classification = known.classification;
		return true;
	}
	return false;
}


void ActivePictureTransitionModel::StartCandidate(
	const ActivePictureObservation& observation)
{
	if (m_matchingCandidates > 0 &&
		!SameBounds(m_candidate, observation.bounds) &&
		m_candidateReversals < 255)
		++m_candidateReversals;
	m_candidateUsesKnownTrustedGeometry = false;
	m_candidate = observation.bounds;
	m_candidateClassification = observation.classification;
	m_matchingCandidates = 1;
	m_firstContradictoryFrame = observation.frameNumber;
}


void ActivePictureTransitionModel::ClearCandidate()
{
	m_candidate = {};
	m_candidateClassification = ActivePictureClassification::UNAVAILABLE;
	m_candidateUsesKnownTrustedGeometry = false;
	m_matchingCandidates = 0;
	m_contradictoryCandidates = 0;
	m_candidateReversals = 0;
	m_firstContradictoryFrame = 0;
}


ActivePictureTransitionDecision
ActivePictureTransitionModel::CommitCandidate(
	const ActivePictureObservation& observation,
	const char* reason)
{
	// Confirm against the anchored candidate, then publish the current trusted
	// sample. The first sample's tiny measurement error is not authority to
	// exclude pixels verified on this commit frame. Provisional/history-only
	// recurrence keeps its canonical remembered contract.
	ActivePictureBounds committedBounds = m_candidate;
	if (HasCropAuthority(observation) &&
		observation.classification == m_candidateClassification &&
		observation.bounds.trustedBarAxes == m_candidate.trustedBarAxes &&
		SameBounds(m_candidate, observation.bounds) &&
		StableRetentionAdmission(observation.bounds, observation.classification) ==
			ActivePicturePublicationAdmission::ACCEPTED &&
		!RetainsIncompleteInwardFormat(observation.bounds, observation.axisEvidence))
	{
		committedBounds = observation.bounds;
	}
	ActivePictureTransitionDecision decision;
	decision.state = ActivePictureTransitionState::STABLE;
	decision.bounds = committedBounds;
	decision.stableBounds = m_stable;
	decision.publish = true;
	decision.stable = true;
	decision.diagnostic = true;
	decision.authoritativeClassification = m_candidateClassification;
	decision.knownTrustedGeometryReacquired =
		m_candidateUsesKnownTrustedGeometry;
	decision.matchingCandidates = m_matchingCandidates;
	decision.contradictoryCandidates = m_contradictoryCandidates;
	decision.candidateReversals = m_candidateReversals;
	decision.confidence = 1.0;
	decision.firstContradictoryFrame = m_firstContradictoryFrame;
	decision.decisionLatencyFrames =
		observation.frameNumber >= m_firstContradictoryFrame ?
		observation.frameNumber - m_firstContradictoryFrame : 0;
	decision.reason = reason;
	if (m_hasStable && !SameBounds(m_stable, committedBounds))
	{
		RememberTrustedGeometry(m_stable, m_stableClassification);
	}
	m_stable = committedBounds;
	m_stableClassification = m_candidateClassification;
	m_hasStable = true;
	m_unavailableCandidates = 0;
	ClearCandidate();
	return decision;
}


ActivePictureTransitionDecision ActivePictureTransitionModel::Observe(
	const ActivePictureObservation& observation)
{
	ActivePictureTransitionDecision decision;
	decision.state = m_hasStable ?
		ActivePictureTransitionState::STABLE :
		ActivePictureTransitionState::UNAVAILABLE;
	decision.bounds = m_hasStable ? m_stable : ActivePictureBounds{};
	decision.stableBounds = m_hasStable ? m_stable : ActivePictureBounds{};
	decision.stable = m_hasStable;
	decision.authoritativeClassification = m_hasStable
		? m_stableClassification
		: ActivePictureClassification::UNAVAILABLE;
	if (observation.frameNumber != 0 && m_lastObservedFrame != 0 &&
		observation.frameNumber <= m_lastObservedFrame)
	{
		if (m_matchingCandidates != 0)
		{
			decision.state =
				ActivePictureTransitionState::CANDIDATE_TRANSITION;
			decision.bounds = m_candidate;
			decision.matchingCandidates = m_matchingCandidates;
			decision.contradictoryCandidates = m_contradictoryCandidates;
			decision.candidateReversals = m_candidateReversals;
			decision.firstContradictoryFrame =
				m_firstContradictoryFrame;
		}
		return decision;
	}
	if (observation.frameNumber != 0)
		m_lastObservedFrame = observation.frameNumber;

	if (observation.transitionDeferred)
	{
		decision.diagnostic = decision.diagnostic || m_matchingCandidates != 0;
		ClearCandidate();
		decision.reason = "current presentation evidence defers logical transition";
		return decision;
	}
	if (!observation.available)
	{
		if (m_unavailableCandidates < 255)
			++m_unavailableCandidates;
		if (m_matchingCandidates > 0)
		{
			decision.diagnostic = true;
			decision.matchingCandidates = m_matchingCandidates;
			decision.contradictoryCandidates = m_contradictoryCandidates;
			decision.candidateReversals = m_candidateReversals;
			decision.reason =
				"candidate rejected by unavailable/ambiguous observation";
		}
		ClearCandidate();
		// Black/fade frames carry no geometry evidence. Preserve the last stable
		// mapping so a fade cannot create a false aspect-mode change.
		decision.state = ActivePictureTransitionState::UNAVAILABLE;
		decision.stable = m_hasStable;
		decision.confidence = 0.0;
		return decision;
	}
	m_unavailableCandidates = 0;

	ActivePictureBounds recentTrustedBounds;
	ActivePictureClassification recentTrustedClassification =
		ActivePictureClassification::UNAVAILABLE;
	const bool matchesRecentTrusted = FindRecentTrustedGeometry(
		observation, recentTrustedBounds, recentTrustedClassification);
	if (RetainsIncompleteInwardFormat(matchesRecentTrusted ? recentTrustedBounds : observation.bounds,
		observation.axisEvidence))
	{
		// Axis diagnostics already use the bounded edge trace. Only log a model
		// event here when incomplete evidence actually cancels in-flight proof.
		decision.diagnostic = m_matchingCandidates != 0;
		ClearCandidate();
		decision.reason = "failed bar axis cannot establish a new inward program format";
		return decision;
	}
	// History and look-ahead must obey the same retention policy as live
	// evidence. Test the remembered contract, not its noisy raw recurrence.
	const auto retention = matchesRecentTrusted
		? StableRetentionAdmission(recentTrustedBounds, recentTrustedClassification)
		: (HasCropAuthority(observation)
			? StableRetentionAdmission(observation.bounds, observation.classification)
			: ActivePicturePublicationAdmission::ACCEPTED);
	if (retention != ActivePicturePublicationAdmission::ACCEPTED)
	{
		const bool geometryMoved = !SameBounds(m_stable,
			matchesRecentTrusted ? recentTrustedBounds : observation.bounds);
		ClearCandidate();
		decision.state = ActivePictureTransitionState::STABLE;
		decision.bounds = decision.stableBounds = m_stable;
		decision.stable = true;
		decision.confidence = 1.0;
		decision.diagnostic = geometryMoved;
		if (geometryMoved)
			decision.reason = retention == ActivePicturePublicationAdmission::CONTAINED_COMPOSITION_RETAINED
				? "all-sided inset retained as inner composition"
				: retention == ActivePicturePublicationAdmission::STABLE_ASPECT_RETAINED
					? "contained picture retained within established aspect deadband"
					: "minor trusted geometry change retained within deadband";
		return decision;
	}
	if (matchesRecentTrusted)
	{
		if (!m_candidateUsesKnownTrustedGeometry ||
			!SameBounds(m_candidate, observation.bounds))
		{
			StartCandidate(observation);
			// Restore the complete trusted contract, not only its classification.
			// A provisional recurrence carries proposed coordinates but no bar-axis
			// authority; retaining that incomplete value could bypass directional
			// hysteresis and later publish an internally inconsistent crop.
			m_candidate = recentTrustedBounds;
			m_candidateClassification = recentTrustedClassification;
			m_candidateUsesKnownTrustedGeometry = true;
			decision.diagnostic = true;
			decision.reason =
				"recent trusted geometry candidate";
		}
		else if (m_matchingCandidates < 255)
		{
			++m_matchingCandidates;
		}

		decision.state =
			ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.stableBounds = m_stable;
		decision.stable = true;
		// Keep the last stable presentation while the known geometry is
		// reacquired.  The renderer's outward presentation envelope guarantees
		// visibility; withdrawing the stable rectangle here only creates a flash.
		decision.clearTransition = false;
		decision.matchingCandidates = m_matchingCandidates;
		decision.confidence = std::min(
			1.0, static_cast<double>(m_matchingCandidates) /
			CLEAR_TRANSITION_CONFIRMATIONS);
		decision.firstContradictoryFrame = m_firstContradictoryFrame;
		decision.decisionLatencyFrames =
			observation.frameNumber >= m_firstContradictoryFrame ?
			observation.frameNumber - m_firstContradictoryFrame : 0;
		if (m_matchingCandidates >= CLEAR_TRANSITION_CONFIRMATIONS)
			return CommitCandidate(observation, "recent trusted geometry reacquired");
		return decision;
	}

	if (!HasCropAuthority(observation))
	{
		const bool candidateChanged =
			m_matchingCandidates == 0 ||
			m_candidateClassification != observation.classification ||
			!SameBounds(m_candidate, observation.bounds);
		if (candidateChanged)
			StartCandidate(observation);
		else if (m_matchingCandidates < 255)
			++m_matchingCandidates;
		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.stableBounds =
			m_hasStable ? m_stable : ActivePictureBounds{};
		decision.stable = m_hasStable;
		decision.diagnostic = candidateChanged;
		decision.matchingCandidates = m_matchingCandidates;
		decision.contradictoryCandidates = m_contradictoryCandidates;
		decision.candidateReversals = m_candidateReversals;
		decision.confidence = 0.0;
		decision.reason =
			"provisional geometry lacks affirmative crop authority";
		return decision;
	}

	if (observation.classification ==
		ActivePictureClassification::FULL_RASTER_TRUSTED && !m_hasStable)
	{
		m_candidate = observation.bounds;
		m_candidateClassification = observation.classification;
		m_matchingCandidates = 1;
		m_firstContradictoryFrame = observation.frameNumber;
		return CommitCandidate(
			observation, "safe full-raster authority accepted");
	}

	if (observation.classification ==
		ActivePictureClassification::FULL_RASTER_TRUSTED &&
		!SameBounds(m_stable, observation.bounds))
	{
		const bool candidateChanged = m_matchingCandidates == 0 ||
			m_candidateClassification != observation.classification ||
			!SameBounds(m_candidate, observation.bounds);
		if (candidateChanged)
			StartCandidate(observation);
		else if (m_matchingCandidates < 255)
			++m_matchingCandidates;

		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.stableBounds = m_stable;
		decision.stable = true;
		decision.diagnostic = candidateChanged;
		decision.matchingCandidates = m_matchingCandidates;
		decision.confidence = std::min(1.0,
			static_cast<double>(m_matchingCandidates) /
			CLEAR_TRANSITION_CONFIRMATIONS);
		decision.reason =
			"full-raster transition awaiting adjacent confirmation";
		if (m_matchingCandidates >= CLEAR_TRANSITION_CONFIRMATIONS)
			return CommitCandidate(
				observation, "full-raster transition confirmed");
		return decision;
	}

	if (!m_hasStable)
	{
		if (m_matchingCandidates == 0 ||
			m_candidateClassification != observation.classification ||
			!SameBounds(m_candidate, observation.bounds))
		{
			StartCandidate(observation);
			decision.diagnostic = true;
			decision.reason = "initial geometry candidate";
		}
		else if (m_matchingCandidates < 255)
		{
			++m_matchingCandidates;
			m_candidate.aspectRatio =
				m_candidate.aspectRatio * 0.75 +
				observation.bounds.aspectRatio * 0.25;
		}
		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.matchingCandidates = m_matchingCandidates;
		decision.confidence = static_cast<double>(m_matchingCandidates) /
			INITIAL_CONFIRMATIONS;
		if (m_matchingCandidates >= INITIAL_CONFIRMATIONS)
			return CommitCandidate(
				observation, "initial geometry confirmed");
		return decision;
	}

	if (SameBounds(m_stable, observation.bounds))
	{
		const uint8_t rejectedMatches = m_matchingCandidates;
		const uint8_t rejectedReversals = m_candidateReversals;
		ClearCandidate();
		decision.state = ActivePictureTransitionState::STABLE;
		decision.bounds = m_stable;
		decision.stable = true;
		decision.matchingCandidates = rejectedMatches;
		decision.candidateReversals = rejectedReversals;
		decision.confidence = 1.0;
		if (rejectedMatches > 0)
		{
			decision.diagnostic = true;
			decision.reason =
				"ambiguous transition candidate rejected by stable geometry";
		}
		return decision;
	}

	if (m_contradictoryCandidates < 255)
		++m_contradictoryCandidates;
	if (m_matchingCandidates == 0 ||
		m_candidateClassification != observation.classification ||
		!SameBounds(m_candidate, observation.bounds))
	{
		StartCandidate(observation);
		decision.diagnostic = true;
		decision.reason = "materially different geometry candidate";
	}
	else
	{
		if (m_matchingCandidates < 255)
			++m_matchingCandidates;
		m_candidate.aspectRatio =
			m_candidate.aspectRatio * 0.75 +
			observation.bounds.aspectRatio * 0.25;
	}

	const uint8_t required = CLEAR_TRANSITION_CONFIRMATIONS;
	decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
	decision.bounds = m_candidate;
	decision.stable = true;
	// Candidate changes are presentation-safe in the outward direction and
	// harmlessly conservative in the inward direction. Retain the stable
	// mapping until adjacent evidence commits the new authority.
	decision.clearTransition = false;
	decision.matchingCandidates = m_matchingCandidates;
	decision.contradictoryCandidates = m_contradictoryCandidates;
	decision.candidateReversals = m_candidateReversals;
	decision.confidence =
		std::min(1.0, static_cast<double>(m_matchingCandidates) / required);
	decision.firstContradictoryFrame = m_firstContradictoryFrame;
	decision.decisionLatencyFrames =
		observation.frameNumber >= m_firstContradictoryFrame ?
		observation.frameNumber - m_firstContradictoryFrame : 0;
	if (m_matchingCandidates >= required)
		return CommitCandidate(observation, "trusted transition confirmed");

	return decision;
}


bool ActivePictureTransitionModel::WouldAdmitGeometryChange(
	const ActivePictureObservation& observation) const
{
	return m_hasStable && HasCropAuthority(observation) &&
		!SameBounds(m_stable, observation.bounds) &&
		StableRetentionAdmission(observation.bounds, observation.classification) ==
			ActivePicturePublicationAdmission::ACCEPTED &&
		!RetainsIncompleteInwardFormat(observation.bounds, observation.axisEvidence);
}


bool ActivePictureTransitionModel::AdoptPublishedDecision(
	const ActivePictureTransitionDecision& decision,
	ActivePictureClassification classification, bool transitionDeferred,
	ActivePicturePublicationAdmission* admission,
	const ActivePictureAxisEvidenceSet* currentAxisEvidence,
	bool currentOutwardPictureConfirmed)
{
	if (admission) *admission = ActivePicturePublicationAdmission::ACCEPTED;
	const auto reject = [admission](ActivePicturePublicationAdmission reason) {
		if (admission) *admission = reason;
		return false;
	};
	if (transitionDeferred) return reject(ActivePicturePublicationAdmission::DEFERRED);
	ActivePictureObservation observation;
	observation.available = true;
	observation.bounds = decision.bounds;
	observation.classification = classification;
	if (!decision.publish || !decision.stable ||
		!HasCropAuthority(observation))
		return reject(ActivePicturePublicationAdmission::NON_AUTHORITATIVE);
	// CommitCandidate records the pre-publication stable reference. A queue
	// model with a different history cannot transfer its confirmation to this
	// model. A separately proved outward expansion may tolerate one scan step
	// in the old reference only; exact axes and current target checks remain.
	const auto& base = decision.stableBounds;
	const bool matchingReference = m_hasStable
		? (base.left == m_stable.left && base.top == m_stable.top &&
			base.right == m_stable.right && base.bottom == m_stable.bottom &&
			base.rasterWidth == m_stable.rasterWidth && base.rasterHeight == m_stable.rasterHeight &&
			base.trustedBarAxes == m_stable.trustedBarAxes)
		: (base.left == 0 && base.top == 0 && base.right == 0 && base.bottom == 0 &&
			base.rasterWidth == 0 && base.rasterHeight == 0 &&
			base.trustedBarAxes == ActivePictureBounds::BarAxes::NONE);
	bool equivalentOutwardReference = false;
	if (!matchingReference && currentOutwardPictureConfirmed && m_hasStable &&
		classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
		m_stableClassification == ActivePictureClassification::BAR_CROP_TRUSTED)
	{
		ActivePictureObservation reference;
		reference.available = true;
		reference.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
		reference.bounds = base;
		const auto contains = [](const ActivePictureBounds& outer,
			const ActivePictureBounds& inner) {
			return outer.left <= inner.left && outer.top <= inner.top &&
				outer.right >= inner.right && outer.bottom >= inner.bottom;
		};
		const auto expands = [](const ActivePictureBounds& outer,
			const ActivePictureBounds& inner) {
			return outer.left < inner.left || outer.top < inner.top ||
				outer.right > inner.right || outer.bottom > inner.bottom;
		};
		// Match the source detector's actual scan step, not SameBounds' wider
		// temporal matching tolerance. Also bound total size difference so two
		// opposing edge errors cannot double this allowance.
		const int xStep = std::max(2, m_stable.rasterWidth / 960);
		const int yStep = std::max(2, m_stable.rasterHeight / 540);
		equivalentOutwardReference = HasCropAuthority(reference) &&
			base.rasterWidth == m_stable.rasterWidth &&
			base.rasterHeight == m_stable.rasterHeight &&
			decision.bounds.rasterWidth == m_stable.rasterWidth &&
			decision.bounds.rasterHeight == m_stable.rasterHeight &&
			base.trustedBarAxes == m_stable.trustedBarAxes &&
			decision.bounds.trustedBarAxes == m_stable.trustedBarAxes &&
			std::abs(base.left - m_stable.left) <= xStep &&
			std::abs(base.right - m_stable.right) <= xStep &&
			std::abs(base.top - m_stable.top) <= yStep &&
			std::abs(base.bottom - m_stable.bottom) <= yStep &&
			std::abs((base.right - base.left) - (m_stable.right - m_stable.left)) <= xStep &&
			std::abs((base.bottom - base.top) - (m_stable.bottom - m_stable.top)) <= yStep &&
			contains(decision.bounds, base) && contains(decision.bounds, m_stable) &&
			expands(decision.bounds, base) && expands(decision.bounds, m_stable);
	}
	if (!matchingReference && !equivalentOutwardReference)
		return reject(ActivePicturePublicationAdmission::STABLE_REFERENCE_MISMATCH);
	if (currentAxisEvidence && RetainsIncompleteInwardFormat(decision.bounds, *currentAxisEvidence))
		return reject(ActivePicturePublicationAdmission::INCOMPLETE_AXIS_RETAINED);
	const auto retention = StableRetentionAdmission(decision.bounds, classification);
	if (retention != ActivePicturePublicationAdmission::ACCEPTED)
		return reject(retention);
	if (m_hasStable && !SameBounds(m_stable, decision.bounds))
	{
		RememberTrustedGeometry(m_stable, m_stableClassification);
	}
	m_stable = decision.bounds;
	m_stableClassification = classification;
	m_hasStable = true;
	m_unavailableCandidates = 0;
	ClearCandidate();
	return true;
}


bool ActivePictureTransitionModel::FindRecentTrustedBarGeometry(
    const ActivePictureBounds& bounds, ActivePictureBounds& remembered) const
{
    // Select the same first geometric match used by known-format reacquisition.
    // An incompatible first match cannot be skipped in favor of another entry.
    for (size_t index = 0; index < m_recentTrustedCount; ++index)
    {
        const auto& known = m_recentTrusted[index];
        if (!SameBounds(known.bounds, bounds)) continue;
        if (known.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
            known.bounds.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM)
            return false;
        remembered = known.bounds;
        return true;
    }
    return false;
}
