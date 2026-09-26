#include <pch.h>
#include "SparseBoundaryCropExperiment.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool SameContext(const LocalBoundaryDiagnosticContext& a,
        const LocalBoundaryDiagnosticContext& b)
    {
        return a.sourceGeneration == b.sourceGeneration &&
            a.rendererGeneration == b.rendererGeneration &&
            a.viewportGeneration == b.viewportGeneration &&
            a.sourceFormatGeneration == b.sourceFormatGeneration &&
            a.policyGeneration == b.policyGeneration &&
            a.continuityGeneration == b.continuityGeneration &&
            a.sceneGeneration == b.sceneGeneration;
    }

    bool ExactBase(const ActivePictureBounds& a, const ActivePictureBounds& b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
            a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight &&
            a.trustedBarAxes == b.trustedBarAxes;
    }

    bool ValidEstablishedBase(const AnalysisLumaSource& source, const SparseBoundaryCropExperimentInput& input)
    {
        const auto& base = input.establishedBase;
        if (input.establishedBaseOrigin != ActivePictureAuthorityOrigin::NATIVE ||
            !input.establishedBaseSourceGeneration || input.establishedBaseSourceGeneration != source.generation ||
            base.rasterWidth != source.width || base.rasterHeight != source.height ||
            base.left != 0 || base.right != source.width || base.top < 0 ||
            base.bottom > source.height || base.top >= base.bottom) return false;
        return (base.top == 0 && base.bottom == source.height &&
                base.trustedBarAxes == ActivePictureBounds::BarAxes::NONE) ||
            (base.top > 0 && base.bottom < source.height &&
                base.trustedBarAxes == ActivePictureBounds::BarAxes::TOP_BOTTOM);
    }

    bool SupportedPrecision(VideoFrameEncoding encoding)
    {
        switch (encoding)
        {
        case VideoFrameEncoding::V210:
        case VideoFrameEncoding::R210:
        case VideoFrameEncoding::R10b:
        case VideoFrameEncoding::R10l:
        case VideoFrameEncoding::R12B:
        case VideoFrameEncoding::R12L:
            return true;
        default:
            return false;
        }
    }

    bool NativeAuthority(const ActivePictureEvidence& evidence)
    {
        return evidence.available && evidence.authorityOrigin == ActivePictureAuthorityOrigin::NATIVE &&
            (evidence.classification == ActivePictureClassification::BAR_CROP_TRUSTED ||
             evidence.classification == ActivePictureClassification::FULL_RASTER_TRUSTED);
    }

    int Coordinate(int index, int extent, int count)
    {
        return static_cast<int>(static_cast<int64_t>(index) * (extent - 1) / (count - 1));
    }

    bool InspectExterior(const AnalysisLumaSource& source,
        const ActivePictureDiagnosticGrid* grid, int columns, int rows,
        LocalBoundaryAnchorTelemetry& exterior, size_t& sourceReads)
    {
        exterior.evaluated = false;
        exterior.topSamples = exterior.bottomSamples = 0;
        exterior.topViolations = exterior.bottomViolations = 0;
        exterior.topPeakLuma = exterior.bottomPeakLuma = 0;
        exterior.topPeakLumaDelta = exterior.bottomPeakLumaDelta = 0;
        exterior.topPeakChromaDelta = exterior.bottomPeakChromaDelta = 0;
        if (columns < 2 || columns > 128 || rows < 2 || rows > 270 ||
            exterior.topRow <= 0 || exterior.bottomRow >= rows - 1 ||
            exterior.topRow >= exterior.bottomRow)
            return false;
        for (int row = 0; row < rows; ++row)
        {
            if (row >= exterior.topRow && row <= exterior.bottomRow) continue;
            const bool top = row < exterior.topRow;
            for (int column = 0; column < columns; ++column)
            {
                AnalysisLumaSample sample;
                if (grid)
                    sample = grid->samples[static_cast<size_t>(row) * columns + column];
                else
                {
                    ++sourceReads;
                    if (!source.Sample(Coordinate(column, source.width, columns),
                        Coordinate(row, source.height, rows), sample)) return false;
                }
                const int deltaUV = (std::max)(std::abs(static_cast<int>(sample.chromaU) - 512),
                    std::abs(static_cast<int>(sample.chromaV) - 512));
                auto& count = top ? exterior.topSamples : exterior.bottomSamples;
                auto& violations = top ? exterior.topViolations : exterior.bottomViolations;
                auto& peak = top ? exterior.topPeakLuma : exterior.bottomPeakLuma;
                auto& delta = top ? exterior.topPeakLumaDelta : exterior.bottomPeakLumaDelta;
                auto& uv = top ? exterior.topPeakChromaDelta : exterior.bottomPeakChromaDelta;
                ++count;
                violations += sample.luma > exterior.blackThreshold || deltaUV > 32;
                peak = (std::max)(peak, static_cast<int>(sample.luma));
                delta = (std::max)(delta, static_cast<int>(sample.luma) - exterior.blackFloor);
                uv = (std::max)(uv, deltaUV);
            }
        }
        exterior.evaluated = true;
        return true;
    }
}

void SparseBoundaryCropExperiment::Reset()
{
    m_discovery.Reset();
    m_reference = {};
    m_bounds = {};
    m_context = {};
    m_hasContext = false;
    m_transitionMode = false;
    m_establishedBase = {};
    m_nextDiscoveryTick = 0;
    m_width = m_height = m_columns = m_rows = 0;
}

SparseBoundaryCropExperimentResult SparseBoundaryCropExperiment::Observe(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const SparseBoundaryCropExperimentInput& input)
{
    SparseBoundaryCropExperimentResult result;
    result.evidence = raw;
    const auto& context = input.context;
    auto reset = [&](const char* reason)
    {
        result.reset = true;
        result.referenceAvailable = false;
        result.candidateAvailable = false;
        result.reason = reason;
        Reset();
    };
    // BEGIN SPARSE TRANSITION MODE: a RED stub can disable only this mode.
    const bool transitionMode = input.transitionEligible;
    if (!input.enabled || input.startupEligible == transitionMode || NativeAuthority(raw))
    {
        reset(!input.enabled ? "disabled" :
            (input.startupEligible && transitionMode ? "conflicting-eligibility" :
            (!input.startupEligible && !transitionMode ? "startup-ineligible" : "native-authority")));
        return result;
    }
    if (raw.authorityOrigin != ActivePictureAuthorityOrigin::NATIVE)
    {
        reset("non-native-input");
        return result;
    }
    if (raw.axisEvidence.horizontal.state == ActivePictureAxisState::TRUSTED_BARS ||
        (raw.left.trusted && raw.right.trusted))
    {
        reset("orthogonal-bar-conflict");
        return result;
    }
    if (!source.IsValid() || source.width < 16 || source.height < 16 ||
        source.generation == 0 || source.generation != context.sourceGeneration ||
        context.acceptedSequence == 0)
    {
        reset("invalid-source-identity");
        return result;
    }
    if (!SupportedPrecision(source.encoding))
    {
        reset("precision-unsupported");
        return result;
    }
    if (transitionMode)
    {
        if (!ValidEstablishedBase(source, input))
        {
            reset("invalid-established-base");
            return result;
        }
        // A prior native full-width frame cannot disprove current windowbox or
        // pillarbox content. Require the current native side scans to finish
        // without a side-bar proposal; keep their original metadata unchanged.
        const auto& horizontal = raw.axisEvidence.horizontal;
        if (!raw.available || !horizontal.scanComplete || horizontal.barCandidate || horizontal.FailedBar() ||
            raw.proposedBounds.left != 0 || raw.proposedBounds.right != source.width ||
            raw.proposedBounds.rasterWidth != source.width || raw.proposedBounds.rasterHeight != source.height)
        {
            reset("transition-side-evidence-ambiguous");
            return result;
        }
        const auto darkness = EvaluateActivePictureGlobalNearBlack(source);
        if (!darkness.evaluated || darkness.nearBlack)
        {
            reset("transition-near-black");
            return result;
        }
    }
    const bool repeated = context.cadenceRepeat ||
        (m_hasContext && context.acceptedSequence == m_context.acceptedSequence);
    const char* discontinuity = nullptr;
    if (input.sceneCut) discontinuity = "scene-cut";
    else if (m_hasContext)
    {
        if (transitionMode != m_transitionMode) discontinuity = "acquisition-mode-change";
        else if (transitionMode && !ExactBase(m_establishedBase, input.establishedBase))
            discontinuity = "established-base-change";
        else if (!SameContext(m_context, context)) discontinuity = "context-change";
        else if (source.width != m_width || source.height != m_height) discontinuity = "raster-change";
        else if (context.timestampMs < m_context.timestampMs) discontinuity = "time-backward";
        else if (context.acceptedSequence < m_context.acceptedSequence) discontinuity = "sequence-backward";
        else if (context.acceptedSequence > m_context.acceptedSequence &&
            context.acceptedSequence - m_context.acceptedSequence > 1) discontinuity = "source-sequence-gap";
        else if (context.timestampMs - m_context.timestampMs > LOCAL_BOUNDARY_HISTORY_MAX_GAP_MS)
            discontinuity = "source-time-gap";
    }
    if (!discontinuity && m_reference.active &&
        context.timestampMs - m_reference.createdAtMs >= LOCAL_BOUNDARY_ANCHOR_MAX_AGE_MS)
        discontinuity = "pending-reference-expired";
    if (discontinuity) reset(discontinuity);
    m_context = context;
    m_hasContext = true;
    m_transitionMode = transitionMode;
    m_establishedBase = transitionMode ? input.establishedBase : ActivePictureBounds{};
    m_width = source.width;
    m_height = source.height;
    if (discontinuity) return result; // Never reseed from the reset frame.
    if (repeated)
    {
        result.referenceAvailable = m_reference.active;
        result.referenceId = m_reference.anchorId;
        result.referenceBounds = m_bounds;
        result.fixedExterior = m_reference;
        result.fixedExterior.current = false;
        result.fixedExterior.evaluated = false;
        result.fixedExterior.sameFrame = true;
        result.reason = "same-frame";
        return result;
    }
    result.current = true;
    ActivePictureDiagnosticGrid grid;
    if (context.timestampMs >= m_nextDiscoveryTick)
    {
        m_nextDiscoveryTick = context.timestampMs + SPARSE_BOUNDARY_DISCOVERY_INTERVAL_MS;
        grid = SampleActivePictureDiagnosticGrid(source);
        result.discoverySampled = true;
        result.discoverySamples = grid.samples.size();
        result.diagnostic = m_discovery.ObserveGrid(grid, source.width, source.height,
            source.encoding, context,
            input.startupEligible && !m_reference.active ? &source : nullptr);
        if (result.diagnostic.anchor.released)
        {
            result.fixedExterior = result.diagnostic.anchor;
            result.referenceId = m_reference.anchorId;
            result.referenceBounds = m_bounds;
            reset(result.diagnostic.anchor.reason);
            return result;
        }
        if (!m_reference.active && result.diagnostic.anchor.created)
        {
            m_reference = result.diagnostic.anchor;
            m_columns = grid.columns;
            m_rows = grid.rows;
            // Preserve the whole unmeasured gap next to each witness, and round
            // outward for chroma alignment. This remains sampled evidence: a
            // lone pixel between exterior grid points is not proven absent.
            const int top = m_reference.topExteriorLastY & ~1;
            const int bottom = (std::min)(source.height,
                (m_reference.bottomExteriorFirstY + 2) & ~1);
            if (top <= 0 || bottom >= source.height || top >= bottom ||
                (source.width & 1) != 0 || (bottom & 1) != 0)
            {
                reset("invalid-rounded-reference");
                return result;
            }
            m_bounds = {0, top, source.width, bottom, source.width, source.height,
                static_cast<double>(source.width) / (bottom - top),
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
        }
    }
    if (!m_reference.active)
    {
        result.reason = result.discoverySampled ? result.diagnostic.hypothesis.reason : "discovery-pending";
        return result;
    }
    if (transitionMode && !ActivePictureTransitionModel::IsSparseBoundaryInwardTransitionGeometry(
        m_establishedBase, m_bounds, ActivePictureTransitionModel::GetRuntimeStableGeometryDeadbandPercent()))
    {
        reset("transition-not-material-inward");
        return result;
    }
    result.referenceAvailable = true;
    result.referenceId = m_reference.anchorId;
    result.referenceBounds = m_bounds;
    result.fixedExterior = m_reference;
    result.fixedExterior.created = result.discoverySampled && result.diagnostic.anchor.created;
    result.fixedExterior.current = true;
    result.fixedExterior.sameFrame = false;
    result.fixedExterior.sourceSequence = context.acceptedSequence;
    result.fixedExterior.ageMs = context.timestampMs - m_reference.createdAtMs;
    if (!InspectExterior(source, result.discoverySampled ? &grid : nullptr,
        m_columns, m_rows, result.fixedExterior, result.fixedExteriorSamples))
    {
        reset("fixed-exterior-unavailable");
        return result;
    }
    if (result.fixedExterior.topViolations || result.fixedExterior.bottomViolations)
    {
        result.fixedExterior.reason = "fixed-exterior-violation";
        reset(result.fixedExterior.reason);
        return result;
    }
    result.fixedExterior.reason = "fixed-exterior-clean";
    result.retention = EvaluateActivePicturePresentationRetention(source, m_bounds);
    if (!result.retention.analysisValid || !result.retention.presentationValid ||
        !result.retention.currentlyPixelSafe || !result.retention.excludedBandsPixelSafe ||
        result.retention.outwardVisibleBoundsAvailable)
    {
        reset("ordinary-retention-rejected");
        return result;
    }
    if (NativeAuthority(result.retention.activePicture))
    {
        // The caller's raw measurement cannot hide independently remeasured
        // native authority or replace it with an experiment.
        reset("native-authority");
        return result;
    }
    if (result.retention.globalNearBlack)
    {
        result.reason = "global-near-black";
        return result;
    }
    result.candidateAvailable = true;
    result.evidence.available = true;
    result.evidence.classification = ActivePictureClassification::BAR_CROP_TRUSTED;
    result.evidence.proposedBounds = result.evidence.trustedBounds = m_bounds;
    result.evidence.authorityOrigin = transitionMode ? ActivePictureAuthorityOrigin::SPARSE_TRANSITION_EXPERIMENT :
        ActivePictureAuthorityOrigin::SPARSE_EXPERIMENT;
    result.evidence.sparseTransitionProof = {};
    if (transitionMode)
    {
        auto& proof = result.evidence.sparseTransitionProof;
        proof.available = true;
        proof.establishedBase = m_establishedBase;
        proof.guardedBounds = m_bounds;
        proof.referenceId = m_reference.anchorId;
        proof.sourceGeneration = source.generation;
        proof.sourceSequence = context.acceptedSequence;
    }
    result.evidence.reason = transitionMode ?
        "opt-in sparse boundary inward transition experiment; semantic ambiguity remains" :
        "opt-in sparse boundary startup experiment; semantic ambiguity remains";
    result.reason = transitionMode ? "current-experimental-inward-candidate" : "current-experimental-startup-candidate";
    return result;
}
