#include <pch.h>

#include "ActivePictureTransitionModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{
std::atomic<double> g_runtimeStableGeometryDeadbandPercent(
	ActivePictureTransitionModel::DEFAULT_STABLE_GEOMETRY_DEADBAND_PERCENT);

bool ExactSparseBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
        a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight &&
        a.trustedBarAxes == b.trustedBarAxes;
}

}


void ActivePictureTransitionModel::Reset()
{
	m_hasStable = false;
	m_stableOrigin = ActivePictureAuthorityOrigin::NATIVE;
	m_stable = {};
	m_stableClassification = ActivePictureClassification::UNAVAILABLE;
	m_recentTrusted = {};
	m_recentTrustedCount = 0;
	m_qualifiedNativeGeometry = {};
	m_rememberedContext = {};
	m_rememberedContextFresh = false;
	ClearCandidate();
	m_unavailableCandidates = 0;
	m_lastAnalyzedFrame = 0;
	m_lastObservedFrame = 0;
}


void ActivePictureTransitionModel::ResetPresentationState()
{
    const auto qualification = m_qualifiedNativeGeometry;
    const auto context = m_rememberedContext;
    Reset();
    m_qualifiedNativeGeometry = qualification;
    m_rememberedContext = context;
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
	const uint64_t interval = (m_candidateUsesKnownTrustedGeometry ||
        m_candidateOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN) ?
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
	// A failed axis cannot authorize removing pixels on that axis. Current
	// strict vertical profiles or the existing broad-side path can still admit
	// an independently verified top/bottom crop which preserves every column.
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

bool ActivePictureTransitionModel::IsSparseBoundaryInwardTransitionGeometry(
    const ActivePictureBounds& base, const ActivePictureBounds& target, double deadbandPercent)
{
    if (!std::isfinite(deadbandPercent) || deadbandPercent < 0.0 ||
        deadbandPercent > MAX_STABLE_GEOMETRY_DEADBAND_PERCENT ||
        base.rasterWidth < 320 || base.rasterHeight < 180 ||
        base.left != 0 || base.right != base.rasterWidth || base.top < 0 ||
        base.bottom > base.rasterHeight || base.top >= base.bottom ||
        target.rasterWidth != base.rasterWidth || target.rasterHeight != base.rasterHeight ||
        target.left != 0 || target.right != base.rasterWidth ||
        target.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        target.top <= base.top || target.bottom >= base.bottom || target.top >= target.bottom)
        return false;
    const bool fullBase = IsFullRaster(base) && base.trustedBarAxes == ActivePictureBounds::BarAxes::NONE;
    const bool verticalBase = base.top > 0 && base.bottom < base.rasterHeight &&
        base.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM;
    if (!fullBase && !verticalBase) return false;
    const int baseHeight = base.bottom - base.top;
    const int targetHeight = target.bottom - target.top;
    const double targetAspect = static_cast<double>(target.rasterWidth) / targetHeight;
    if (targetHeight < target.rasterHeight / 3 || targetAspect < 1.0 || targetAspect > 4.0)
        return false;
    const int limit = std::max(2, static_cast<int>(std::lround(base.rasterHeight * deadbandPercent / 100.0)));
    if (target.top - base.top <= limit && base.bottom - target.bottom <= limit &&
        baseHeight - targetHeight <= limit) return false;
    // The experiment never turns tiny inward sampling drift into a new format.
    return (static_cast<double>(baseHeight) / targetHeight - 1.0) * 100.0 > STABLE_ASPECT_DEADBAND_PERCENT;
}

bool ActivePictureTransitionModel::ValidSparseTransitionObservation(
    const ActivePictureObservation& observation) const
{
    const auto& proof = observation.sparseTransitionProof;
    const auto& horizontal = observation.axisEvidence.horizontal;
    if (observation.authorityOrigin != ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT ||
        !m_hasStable || m_stableOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        !proof.available || !proof.referenceId || !proof.sourceGeneration ||
        !proof.sourceSequence || proof.sourceSequence != observation.frameNumber ||
        !ExactSparseBounds(proof.establishedBase, m_stable) ||
        !ExactSparseBounds(proof.guardedBounds, observation.bounds) ||
        !horizontal.scanComplete || horizontal.barCandidate || horizontal.FailedBar() ||
        horizontal.state == ActivePictureAxisState::TRUSTED_BARS ||
        !HasCropAuthority(observation)) return false;
    const bool fullBase = IsFullRaster(m_stable);
    if (m_stableClassification != (fullBase ? ActivePictureClassification::FULL_RASTER_TRUSTED :
        ActivePictureClassification::BAR_CROP_TRUSTED)) return false;
    return IsSparseBoundaryInwardTransitionGeometry(m_stable, observation.bounds, m_stableGeometryDeadbandPercent);
}

bool ActivePictureTransitionModel::SameSparseCandidateProof(const ActivePictureObservation& observation) const
{
    if (observation.authorityOrigin != ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT) return true;
    const auto& a = m_candidateSparseTransitionProof;
    const auto& b = observation.sparseTransitionProof;
    return a.available && b.available && a.referenceId == b.referenceId &&
        a.sourceGeneration == b.sourceGeneration && ExactSparseBounds(a.establishedBase, b.establishedBase) &&
        ExactSparseBounds(a.guardedBounds, b.guardedBounds);
}

void ActivePictureTransitionModel::SetRememberedEdgeReturnContext(const RememberedEdgeReturnContext& context)
{
    const auto& old = m_rememberedContext;
    const bool sameSourceContext = old.sourceGeneration && old.sourceGeneration == context.sourceGeneration &&
        old.rendererGeneration == context.rendererGeneration && old.sourceFormatGeneration == context.sourceFormatGeneration;
    const bool sameProofContext = sameSourceContext && old.enabled == context.enabled &&
        old.viewportGeneration == context.viewportGeneration && old.policyGeneration == context.policyGeneration;
    const bool backward = sameSourceContext && (context.sourceSequence < old.sourceSequence ||
        context.timestampMs < old.timestampMs || context.sceneId < old.sceneId);
    if (!sameSourceContext || backward)
        m_qualifiedNativeGeometry = {};
    const bool discontinuity = !sameProofContext || backward || context.discontinuity ||
        context.sceneId != old.sceneId || (context.sourceSequence > old.sourceSequence &&
        (context.sourceSequence - old.sourceSequence > 1 || context.timestampMs - old.timestampMs > 500));
    if (discontinuity && m_candidateOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN)
        ClearCandidate();
    m_rememberedContextFresh = context.enabled && context.sourceGeneration && context.sceneId &&
        context.sourceSequence && context.timestampMs && !context.cadenceRepeat &&
        (!sameSourceContext || context.sourceSequence > old.sourceSequence) && !backward;
    m_rememberedContext = context;
}

bool ActivePictureTransitionModel::RecordIndependentNativeGeometry(const ActivePictureObservation& raw)
{
    const auto& context = m_rememberedContext;
    if (!m_rememberedContextFresh || context.discontinuity || !m_hasStable || m_stableOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        m_stableClassification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        raw.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE || raw.transitionDeferred ||
        raw.sparseTransitionProof.available || raw.rememberedEdgeReturnProof.available ||
        // A conservative boundary envelope is safe geometry, not an exact edge
        // measurement from which to train the separate remembered-edge path.
        raw.axisEvidence.HasVerticalCropBoundaryGuard() ||
        raw.frameNumber != context.sourceSequence || !HasCropAuthority(raw) ||
        raw.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
        raw.bounds.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
        raw.bounds.left != 0 || raw.bounds.right != raw.bounds.rasterWidth ||
        !ExactSparseBounds(raw.bounds, m_stable) ||
        raw.axisEvidence.vertical.state != ActivePictureAxisState::TRUSTED_BARS ||
        !raw.axisEvidence.vertical.scanComplete || !raw.axisEvidence.horizontal.scanComplete ||
        raw.axisEvidence.HasBlockingFailedBar(raw.bounds)) return false;
    QualifiedNativeGeometry* entry = nullptr;
    for (auto& value : m_qualifiedNativeGeometry)
        if (value.id && ExactSparseBounds(value.bounds, raw.bounds)) { entry = &value; break; }
    // Expired qualification cannot be revived by a single new native sample.
    if (entry && (context.timestampMs < entry->lastTickMs ||
        context.timestampMs - entry->lastTickMs > REMEMBERED_RETURN_MAX_AGE_MS ||
        context.sceneId < entry->lastSceneId || (entry->lastSceneId != 0 &&
        context.sceneId - entry->lastSceneId > REMEMBERED_RETURN_MAX_SCENE_DISTANCE)))
        *entry = {};
    if (!entry || !entry->id)
    {
        if (!entry) entry = &*std::min_element(m_qualifiedNativeGeometry.begin(), m_qualifiedNativeGeometry.end(),
            [](const QualifiedNativeGeometry& a, const QualifiedNativeGeometry& b) { return a.lastTickMs < b.lastTickMs; });
        *entry = {};
        entry->bounds = raw.bounds;
        entry->id = ++m_nextQualifiedNativeId;
        entry->sourceGeneration = context.sourceGeneration;
    }
    if (context.sourceSequence <= entry->lastSequence) return false;
    if (entry->pendingSceneId != context.sceneId || entry->lastSequence == UINT64_MAX ||
        context.sourceSequence != entry->lastSequence + 1)
    {
        entry->pendingSceneId = context.sceneId;
        entry->pendingVerifications = 1;
    }
    else if (entry->pendingVerifications < 3) ++entry->pendingVerifications;
    if (entry->pendingVerifications >= 3 && entry->lastSceneId != context.sceneId)
    {
        if (entry->confirmedScenes < UINT32_MAX) ++entry->confirmedScenes;
        entry->lastSceneId = context.sceneId;
        ++entry->revision;
    }
    entry->lastSequence = context.sourceSequence;
    entry->lastTickMs = context.timestampMs;
    return true;
}

RememberedEdgeReturnNomination ActivePictureTransitionModel::NominateRememberedEdgeReturn(
    const ActivePictureBounds& observed, bool topTrusted, bool bottomTrusted) const
{
    RememberedEdgeReturnNomination result;
    const auto& context = m_rememberedContext;
    if (!m_rememberedContextFresh || context.discontinuity || !m_hasStable ||
        m_stableOrigin != ActivePictureAuthorityOrigin::NATIVE ||
        observed.rasterWidth != m_stable.rasterWidth || observed.rasterHeight != m_stable.rasterHeight ||
        observed.left != 0 || observed.right != observed.rasterWidth ||
        observed.top < 0 || observed.bottom > observed.rasterHeight || observed.top >= observed.bottom) return result;
    for (const auto& entry : m_qualifiedNativeGeometry)
    {
        if (!entry.id || entry.confirmedScenes < 2 || entry.sourceGeneration != context.sourceGeneration ||
            context.timestampMs < entry.lastTickMs || context.timestampMs - entry.lastTickMs > REMEMBERED_RETURN_MAX_AGE_MS ||
            context.sceneId < entry.lastSceneId || context.sceneId - entry.lastSceneId > REMEMBERED_RETURN_MAX_SCENE_DISTANCE ||
            !IsSparseBoundaryInwardTransitionGeometry(m_stable, entry.bounds, m_stableGeometryDeadbandPercent)) continue;
        const bool top = topTrusted && observed.top == entry.bounds.top;
        const bool bottom = bottomTrusted && observed.bottom == entry.bounds.bottom;
        if (top == bottom) continue; // Exactly one current independently trusted edge recurs.
        if (result.available) return {}; // Recency cannot resolve an ambiguous prior.
        result.available = true;
        result.establishedBase = m_stable;
        result.rememberedBounds = entry.bounds;
        result.historyId = entry.id;
        result.historyRevision = entry.revision;
        result.sourceGeneration = context.sourceGeneration;
        result.confirmedSceneCount = entry.confirmedScenes;
        result.lastIndependentNativeSequence = entry.lastSequence;
        result.lastIndependentNativeTickMs = entry.lastTickMs;
        result.matchedEdge = top ? RememberedEdge::TOP : RememberedEdge::BOTTOM;
        result.observedEdgeCoordinate = top ? observed.top : observed.bottom;
        result.sceneId = context.sceneId;
        result.sourceSequence = context.sourceSequence;
        result.timestampMs = context.timestampMs;
        result.rendererGeneration = context.rendererGeneration;
        result.viewportGeneration = context.viewportGeneration;
        result.sourceFormatGeneration = context.sourceFormatGeneration;
        result.policyGeneration = context.policyGeneration;
    }
    return result;
}

bool ActivePictureTransitionModel::ValidRememberedEdgeReturnObservation(const ActivePictureObservation& observation) const
{
    const auto& proof = observation.rememberedEdgeReturnProof;
    const auto& nomination = proof.nomination;
    const auto& context = m_rememberedContext;
    const auto& horizontal = observation.axisEvidence.horizontal;
    if (observation.authorityOrigin != ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN ||
        !context.enabled || context.cadenceRepeat || context.discontinuity || !m_hasStable ||
        m_stableOrigin != ActivePictureAuthorityOrigin::NATIVE || !proof.available || !nomination.available ||
        !HasCropAuthority(observation) || !context.sourceGeneration || !context.sceneId ||
        nomination.sourceGeneration != context.sourceGeneration || nomination.sceneId != context.sceneId ||
        nomination.rendererGeneration != context.rendererGeneration || nomination.viewportGeneration != context.viewportGeneration ||
        nomination.sourceFormatGeneration != context.sourceFormatGeneration || nomination.policyGeneration != context.policyGeneration ||
        proof.sourceSequence != observation.frameNumber || proof.sourceSequence != context.sourceSequence ||
        nomination.sourceSequence != proof.sourceSequence || proof.timestampMs != context.timestampMs ||
        nomination.timestampMs != proof.timestampMs || !proof.sourceSequence || !proof.timestampMs ||
        !ExactSparseBounds(nomination.establishedBase, m_stable) ||
        !ExactSparseBounds(nomination.rememberedBounds, observation.bounds) ||
        !horizontal.scanComplete || horizontal.barCandidate || horizontal.FailedBar() ||
        horizontal.state == ActivePictureAxisState::TRUSTED_BARS ||
        !observation.axisEvidence.vertical.scanComplete ||
        observation.axisEvidence.vertical.reason != ActivePictureAxisReason::BAR_ASYMMETRY ||
        !observation.axisEvidence.vertical.FailedBar() ||
        !IsSparseBoundaryInwardTransitionGeometry(m_stable, observation.bounds, m_stableGeometryDeadbandPercent)) return false;
    const bool top = nomination.matchedEdge == RememberedEdge::TOP;
    if ((!top && nomination.matchedEdge != RememberedEdge::BOTTOM) ||
        nomination.observedEdgeCoordinate != (top ? observation.bounds.top : observation.bounds.bottom)) return false;
    for (const auto& entry : m_qualifiedNativeGeometry)
        if (entry.id == nomination.historyId && entry.revision == nomination.historyRevision &&
            entry.sourceGeneration == context.sourceGeneration && entry.confirmedScenes >= 2 &&
            entry.confirmedScenes == nomination.confirmedSceneCount &&
            entry.lastSequence == nomination.lastIndependentNativeSequence && entry.lastTickMs == nomination.lastIndependentNativeTickMs &&
            ExactSparseBounds(entry.bounds, observation.bounds) && context.timestampMs >= entry.lastTickMs &&
            context.timestampMs - entry.lastTickMs <= REMEMBERED_RETURN_MAX_AGE_MS &&
            context.sceneId >= entry.lastSceneId && context.sceneId - entry.lastSceneId <= REMEMBERED_RETURN_MAX_SCENE_DISTANCE)
            return true;
    return false;
}

bool ActivePictureTransitionModel::SameRememberedCandidateProof(const ActivePictureObservation& observation) const
{
    if (observation.authorityOrigin != ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN) return true;
    const auto& a = m_candidateRememberedProof.nomination;
    const auto& b = observation.rememberedEdgeReturnProof.nomination;
    return m_candidateRememberedProof.available && observation.rememberedEdgeReturnProof.available &&
        a.historyId == b.historyId && a.historyRevision == b.historyRevision && a.sourceGeneration == b.sourceGeneration &&
        a.sceneId == b.sceneId && a.rendererGeneration == b.rendererGeneration && a.viewportGeneration == b.viewportGeneration &&
        a.sourceFormatGeneration == b.sourceFormatGeneration && a.policyGeneration == b.policyGeneration &&
        a.matchedEdge == b.matchedEdge && a.observedEdgeCoordinate == b.observedEdgeCoordinate &&
        ExactSparseBounds(a.establishedBase, b.establishedBase) && ExactSparseBounds(a.rememberedBounds, b.rememberedBounds);
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
	if (observation.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE &&
		(observation.classification != ActivePictureClassification::BAR_CROP_TRUSTED ||
		 bounds.left != 0 || bounds.right != bounds.rasterWidth ||
		 bounds.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM))
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
	ActivePictureClassification classification, ActivePictureAuthorityOrigin origin)
{
	// Experimental geometry must never become a remembered native format.
	if (origin != ActivePictureAuthorityOrigin::NATIVE ||
		classification == ActivePictureClassification::UNAVAILABLE)
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
		// Vertical crop permission belongs to its exact current aperture. History
		// must not replace that aperture with a nearby, more tightly cropped one.
		if (observation.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(observation.bounds) &&
			!observation.axisEvidence.SupportsVerticalCropDespiteSideAmbiguity(known.bounds))
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
	m_candidateOrigin = observation.authorityOrigin;
	m_candidateSparseTransitionProof = observation.sparseTransitionProof;
	m_candidateRememberedProof = observation.rememberedEdgeReturnProof;
	m_candidateRememberedLastSequence = observation.frameNumber;
	m_matchingCandidates = 1;
	m_firstContradictoryFrame = observation.frameNumber;
}


void ActivePictureTransitionModel::ClearCandidate()
{
	m_candidate = {};
	m_candidateClassification = ActivePictureClassification::UNAVAILABLE;
	m_candidateOrigin = ActivePictureAuthorityOrigin::NATIVE;
	m_candidateSparseTransitionProof = {};
	m_candidateRememberedProof = {};
	m_candidateRememberedLastSequence = 0;
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
	if (m_candidateOrigin == ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT &&
		(!ValidSparseTransitionObservation(observation) || !SameSparseCandidateProof(observation)))
	{
		ClearCandidate();
		ActivePictureTransitionDecision retained;
		retained.state = m_hasStable ? ActivePictureTransitionState::STABLE : ActivePictureTransitionState::UNAVAILABLE;
		retained.stable = m_hasStable;
		retained.bounds = retained.stableBounds = m_stable;
		retained.authoritativeClassification = m_stableClassification;
		retained.authorityOrigin = retained.stableAuthorityOrigin = m_stableOrigin;
		retained.reason = "experimental inward proof no longer matches established base";
		return retained;
	}
    if (m_candidateOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN &&
        (!ValidRememberedEdgeReturnObservation(observation) || !SameRememberedCandidateProof(observation)))
    {
        ClearCandidate();
        ActivePictureTransitionDecision retained;
        retained.state = ActivePictureTransitionState::STABLE;
        retained.stable = m_hasStable;
        retained.bounds = retained.stableBounds = m_stable;
        retained.authoritativeClassification = m_stableClassification;
        retained.authorityOrigin = retained.stableAuthorityOrigin = m_stableOrigin;
        retained.reason = "remembered return proof no longer matches native history and base";
        return retained;
    }
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
		(!RetainsIncompleteInwardFormat(observation.bounds, observation.axisEvidence) ||
		 ValidSparseTransitionObservation(observation) || ValidRememberedEdgeReturnObservation(observation)))
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
	decision.authorityOrigin = m_candidateOrigin;
	decision.stableAuthorityOrigin = m_stableOrigin;
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
		RememberTrustedGeometry(m_stable, m_stableClassification, m_stableOrigin);
	}
	m_stable = committedBounds;
	m_stableClassification = m_candidateClassification;
	m_stableOrigin = m_candidateOrigin;
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
	decision.authorityOrigin = m_stableOrigin;
	decision.stableAuthorityOrigin = m_stableOrigin;
	decision.authoritativeClassification = m_hasStable
		? m_stableClassification
		: ActivePictureClassification::UNAVAILABLE;
	const bool sparseTransition = observation.authorityOrigin == ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT;
	const bool validSparseTransition = sparseTransition && ValidSparseTransitionObservation(observation);
    const bool rememberedReturn = observation.authorityOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN;
    const bool validRememberedReturn = rememberedReturn && ValidRememberedEdgeReturnObservation(observation);
    if (rememberedReturn && !validRememberedReturn)
    {
        ClearCandidate();
        decision.reason = "remembered return proof does not match current native history and base";
        return decision;
    }
	if (sparseTransition && !validSparseTransition)
	{
		ClearCandidate();
		decision.reason = "experimental inward proof does not match current native base";
		return decision;
	}
	if (observation.frameNumber != 0 && m_lastObservedFrame != 0 &&
		observation.frameNumber <= m_lastObservedFrame)
	{
		if (m_matchingCandidates != 0)
		{
			decision.state =
				ActivePictureTransitionState::CANDIDATE_TRANSITION;
			decision.bounds = m_candidate;
			decision.authorityOrigin = m_candidateOrigin;
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
    if (rememberedReturn && m_candidateOrigin == ActivePictureAuthorityOrigin::REMEMBERED_EDGE_RETURN)
    {
        if (m_candidateRememberedLastSequence == UINT64_MAX || observation.frameNumber != m_candidateRememberedLastSequence + 1)
            ClearCandidate();
        else m_candidateRememberedLastSequence = observation.frameNumber;
    }


	if (observation.authorityOrigin == ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT && m_hasStable)
	{
		ClearCandidate();
		decision.reason = "experimental startup cannot replace established authority";
		return decision;
	}
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
	// History may name native formats, but cannot canonicalize or promote an
	// experimental target into native authority or shorten its current proof.
	const bool matchesRecentTrusted = observation.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
		FindRecentTrustedGeometry(observation, recentTrustedBounds, recentTrustedClassification);
	if (!validSparseTransition && !validRememberedReturn && RetainsIncompleteInwardFormat(
		matchesRecentTrusted ? recentTrustedBounds : observation.bounds, observation.axisEvidence))
	{
		// Axis diagnostics already use the bounded edge trace. Only log a model
		// event here when incomplete evidence actually cancels in-flight proof.
		decision.diagnostic = m_matchingCandidates != 0;
		ClearCandidate();
		decision.reason = "failed bar axis cannot establish a new inward program format";
		return decision;
	}
	const bool exactNativeVerification = m_hasStable &&
		m_stableOrigin != ActivePictureAuthorityOrigin::NATIVE &&
		observation.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
		observation.classification == m_stableClassification && HasCropAuthority(observation) &&
		!observation.axisEvidence.HasBlockingFailedBar(observation.bounds) &&
		m_stable.left == observation.bounds.left && m_stable.top == observation.bounds.top &&
		m_stable.right == observation.bounds.right && m_stable.bottom == observation.bounds.bottom &&
		m_stable.rasterWidth == observation.bounds.rasterWidth &&
		m_stable.rasterHeight == observation.bounds.rasterHeight &&
		m_stable.trustedBarAxes == observation.bounds.trustedBarAxes;
	if (exactNativeVerification)
	{
		// Current, non-deferred native authority verifies this exact rectangle.
		// Near matches and remembered/provisional observations cannot upgrade it.
		m_stableOrigin = ActivePictureAuthorityOrigin::NATIVE;
		ClearCandidate();
		decision.authorityOrigin = m_stableOrigin;
		decision.stableAuthorityOrigin = m_stableOrigin;
		decision.publish = true;
		decision.diagnostic = true;
		decision.reason = "exact native evidence verified experimental geometry";
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
			m_candidateOrigin = ActivePictureAuthorityOrigin::NATIVE;
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
		decision.authorityOrigin = m_candidateOrigin;
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
			m_candidateOrigin != observation.authorityOrigin ||
			!SameSparseCandidateProof(observation) ||
			!SameRememberedCandidateProof(observation) ||
			!SameBounds(m_candidate, observation.bounds);
		if (candidateChanged)
			StartCandidate(observation);
		else if (m_matchingCandidates < 255)
			++m_matchingCandidates;
		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.authorityOrigin = m_candidateOrigin;
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
		m_candidateOrigin = observation.authorityOrigin;
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
			m_candidateOrigin != observation.authorityOrigin ||
			!SameSparseCandidateProof(observation) ||
			!SameRememberedCandidateProof(observation) ||
			!SameBounds(m_candidate, observation.bounds);
		if (candidateChanged)
			StartCandidate(observation);
		else if (m_matchingCandidates < 255)
			++m_matchingCandidates;

		decision.state = ActivePictureTransitionState::CANDIDATE_TRANSITION;
		decision.bounds = m_candidate;
		decision.authorityOrigin = m_candidateOrigin;
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
			m_candidateOrigin != observation.authorityOrigin ||
			!SameSparseCandidateProof(observation) ||
			!SameRememberedCandidateProof(observation) ||
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
		decision.authorityOrigin = m_candidateOrigin;
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
		m_candidateOrigin != observation.authorityOrigin ||
		!SameSparseCandidateProof(observation) ||
		!SameRememberedCandidateProof(observation) ||
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
	decision.authorityOrigin = m_candidateOrigin;
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


bool ActivePictureTransitionModel::NativeObservationReaffirmsSparseEntry(
	const ActivePictureObservation& observation, const ActivePictureBounds& entry) const
{
	// Corroborate only this already-published sparse startup rectangle. A native
	// observation supplies independent evidence, not an upgrade of its origin.
	const auto& bounds = observation.bounds;
	return m_hasStable && m_stableOrigin == ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT &&
		m_stableClassification == ActivePictureClassification::BAR_CROP_TRUSTED &&
		m_stable.left == entry.left && m_stable.top == entry.top &&
		m_stable.right == entry.right && m_stable.bottom == entry.bottom &&
		m_stable.rasterWidth == entry.rasterWidth && m_stable.rasterHeight == entry.rasterHeight &&
		m_stable.trustedBarAxes == entry.trustedBarAxes &&
		observation.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
		observation.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
		HasCropAuthority(observation) && !observation.transitionDeferred &&
		!observation.axisEvidence.HasBlockingFailedBar(bounds) &&
		bounds.trustedBarAxes == entry.trustedBarAxes &&
		bounds.left >= entry.left && bounds.top >= entry.top &&
		bounds.right <= entry.right && bounds.bottom <= entry.bottom &&
		WithinStableGeometryDeadband(entry, bounds);
}

bool ActivePictureTransitionModel::WouldAdmitGeometryChange(
	const ActivePictureObservation& observation) const
{
	const bool sparseTransition = ValidSparseTransitionObservation(observation);
    const bool rememberedReturn = ValidRememberedEdgeReturnObservation(observation);
	return m_hasStable && (observation.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE || sparseTransition || rememberedReturn) &&
		HasCropAuthority(observation) && !SameBounds(m_stable, observation.bounds) &&
		StableRetentionAdmission(observation.bounds, observation.classification) ==
			ActivePicturePublicationAdmission::ACCEPTED &&
		(sparseTransition || rememberedReturn || !RetainsIncompleteInwardFormat(observation.bounds, observation.axisEvidence));
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
	// Sparse experimental proof is live-only; no queued certificate may promote it.
	if (decision.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE)
		return reject(ActivePicturePublicationAdmission::NON_AUTHORITATIVE);
	ActivePictureObservation observation;
	observation.available = true;
	observation.bounds = decision.bounds;
	observation.classification = classification;
	observation.authorityOrigin = decision.authorityOrigin;
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
		RememberTrustedGeometry(m_stable, m_stableClassification, m_stableOrigin);
	}
	m_stable = decision.bounds;
	m_stableClassification = classification;
	m_stableOrigin = decision.authorityOrigin;
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
