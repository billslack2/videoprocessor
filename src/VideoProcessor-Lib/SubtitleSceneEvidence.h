#pragma once

#include <SubtitleBoxLookahead.h>
#include <SceneDetector.h>
#include <algorithm>
#include <array>
#include <cstdlib>

// Shadow diagnostics only. No field in this result grants scene, AR, crop,
// subtitle ownership, or history authority. The authoritative SceneDetector is
// never called or mutated here. Scores describe this helper's adjacent frames.
struct SubtitleSceneEvidenceResult
{
    bool evaluated = false, excludedComparisonAvailable = false;
    bool currentMaskAccepted = false;
    uint32_t sampleCount = 0, currentMaskSamples = 0;
    uint32_t excludedSamples = 0, retainedSamples = 0;
    uint32_t rawAverageDifference = 0, rawChangedSamples = 0;
    uint32_t withoutSubtitleAverageDifference = 0, withoutSubtitleChangedSamples = 0;
    const char* reason = "warming";
    const char* maskReason = "measurement-unavailable";
};

class SubtitleSceneEvidenceDiagnostics
{
public:
    static constexpr size_t Columns = 32, Rows = 18, Samples = Columns * Rows;
    static constexpr unsigned MinimumRetainedPercent = 75;

    void Reset() { m_previous = {}; }

    SubtitleSceneEvidenceResult Analyze(const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity, const SubtitleBoxObservation* observation,
        bool currentCueAccepted, bool discontinuity = false)
    {
        SubtitleSceneEvidenceResult result;
        if (!source.IsValid() || source.width < int(Columns) || source.height < int(Rows) ||
            source.width > 8192 || source.height > 4320 || identity.acceptedSequence == 0 ||
            source.generation != identity.transportGeneration)
        {
            Reset(); result.reason = "invalid-source-or-identity"; return result;
        }
        if (discontinuity) Reset();
        // Re-rendering the same source identity cannot overwrite the original
        // measurements, add a second comparison, or upgrade its mask later.
        if (m_previous.valid && SameContext(m_previous.identity, identity) &&
            m_previous.identity.acceptedSequence == identity.acceptedSequence)
        {
            result.reason = "repeated-sequence"; return result;
        }
        Snapshot current;
        current.identity = identity; current.width = source.width; current.height = source.height;
        current.measurementCurrent = ValidateMeasurement(source, identity, observation,
            currentCueAccepted, result.maskReason);
        if (observation)
        {
            current.policyGeneration = observation->policyGeneration;
            current.continuityGeneration = observation->continuityGeneration;
        }
        for (size_t row = 0; row < Rows; ++row)
        {
            const int y = int(((row * 2 + 1) * source.height) / (Rows * 2));
            for (size_t col = 0; col < Columns; ++col)
            {
                const int x = int(((col * 2 + 1) * source.width) / (Columns * 2));
                const size_t index = row * Columns + col;
                AnalysisLumaSample sample;
                if (!source.Sample(x, y, sample))
                {
                    Reset(); result.reason = "sample-failed"; return result;
                }
                current.luma[index] = sample.luma;
                current.excluded[index] = current.measurementCurrent && observation &&
                    observation->text.detected && OwnsExactSample(*observation, x, y);
                result.currentMaskSamples += current.excluded[index] ? 1u : 0u;
            }
        }
        // Excessive ownership invalidates this frame as well as this pair.
        // It must not contaminate the following frame through the union mask.
        if (result.currentMaskSamples * 100 > Samples * (100 - MinimumRetainedPercent))
        {
            current.measurementCurrent = false; current.excluded.fill(false);
            result.maskReason = "insufficient-current-coverage";
        }
        result.currentMaskAccepted = current.measurementCurrent && observation &&
            observation->text.detected;
        current.valid = true; result.sampleCount = uint32_t(Samples);
        const bool adjacent = m_previous.valid && SameContext(m_previous.identity, identity) &&
            m_previous.width == source.width && m_previous.height == source.height &&
            m_previous.identity.acceptedSequence + 1 == identity.acceptedSequence &&
            identity.captureTimestamp > m_previous.identity.captureTimestamp &&
            (!identity.sourceFrameNumber || !m_previous.identity.sourceFrameNumber ||
             identity.sourceFrameNumber > m_previous.identity.sourceFrameNumber);
        if (!adjacent)
        {
            m_previous = current; result.reason = "warming-or-discontinuity"; return result;
        }
        result.evaluated = true;
        const bool usablePair = current.measurementCurrent && m_previous.measurementCurrent &&
            current.policyGeneration == m_previous.policyGeneration &&
            current.continuityGeneration == m_previous.continuityGeneration;
        uint64_t rawTotal = 0, retainedTotal = 0;
        for (size_t i = 0; i < Samples; ++i)
        {
            const unsigned difference = unsigned(std::abs(int(current.luma[i]) - int(m_previous.luma[i])));
            rawTotal += difference; result.rawChangedSamples += difference >= 32 ? 1u : 0u;
            // The same union is removed from BOTH signatures. This handles
            // appearance/disappearance without comparing different pixel sets.
            const bool excluded = usablePair && (current.excluded[i] || m_previous.excluded[i]);
            if (excluded) ++result.excludedSamples;
            else
            {
                ++result.retainedSamples; retainedTotal += difference;
                result.withoutSubtitleChangedSamples += difference >= 32 ? 1u : 0u;
            }
        }
        result.rawAverageDifference = uint32_t(rawTotal / Samples);
        result.excludedComparisonAvailable = usablePair &&
            result.retainedSamples * 100 >= Samples * MinimumRetainedPercent;
        if (result.excludedComparisonAvailable)
        {
            result.withoutSubtitleAverageDifference = uint32_t(retainedTotal / result.retainedSamples);
            result.reason = "diagnostic-comparison";
        }
        else
        {
            result.withoutSubtitleAverageDifference = result.withoutSubtitleChangedSamples = 0;
            result.reason = usablePair ? "insufficient-union-coverage" : "untrusted-measurement-pair";
        }
        m_previous = current;
        return result;
    }

private:
    struct Snapshot
    {
        ActivePictureFrameIdentity identity;
        int width = 0, height = 0;
        uint64_t policyGeneration = 0, continuityGeneration = 0;
        bool valid = false, measurementCurrent = false;
        std::array<uint16_t, Samples> luma{};
        std::array<bool, Samples> excluded{};
    };
    static bool SameContext(const ActivePictureFrameIdentity& a, const ActivePictureFrameIdentity& b)
    {
        return a.transportGeneration == b.transportGeneration &&
            a.sourceFormatGeneration == b.sourceFormatGeneration &&
            a.viewportGeneration == b.viewportGeneration && a.rendererGeneration == b.rendererGeneration;
    }
    static bool ValidateMeasurement(const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity, const SubtitleBoxObservation* o,
        bool accepted, const char*& reason)
    {
        reason = "measurement-unavailable";
        if (!o || !o->analyzed) return false;
        reason = "stale-or-incomplete-measurement";
        if (!SameContext(identity, o->identity) || identity.acceptedSequence != o->identity.acceptedSequence ||
            identity.sourceFrameNumber != o->identity.sourceFrameNumber ||
            identity.captureTimestamp != o->identity.captureTimestamp ||
            o->width != source.width || o->height != source.height || o->discontinuity ||
            o->analysisRefresh || o->pendingRefresh || o->text.held || o->text.workLimit) return false;
        if (!o->text.detected) { reason = "current-negative-measurement"; return true; }
        reason = "cue-not-accepted";
        if (!accepted) return false;
        reason = "incomplete-owned-ink";
        const auto& ink = o->ink;
        const auto& box = o->text.bounds;
        if (!box.Valid() || box.left < 0 || box.top < 0 || box.right > source.width ||
            box.bottom > source.height || !ink || !ink->rawEvidenceComplete ||
            ink->step <= 0 || ink->step > source.width || ink->width != (source.width + ink->step - 1) / ink->step ||
            ink->height <= 0 || ink->height > source.height || ink->sourceRows.size() != size_t(ink->height) ||
            ink->rawInk.size() != (size_t(ink->width) * ink->height + 63) / 64 ||
            ink->ownedInk.size() != ink->rawInk.size()) return false;
        for (size_t i = 0; i < ink->sourceRows.size(); ++i)
            if (ink->sourceRows[i] < 0 || ink->sourceRows[i] >= source.height ||
                (i && ink->sourceRows[i] <= ink->sourceRows[i - 1])) return false;
        reason = o->sharedPicture.required || o->barTrackingAuthority || o->currentAnchorUsesTrackedEdge || o->hasBarTrackingReference
            ? "current-owned-ink-reference-conditioned" : "current-owned-ink";
        return true;
    }
    static bool OwnsExactSample(const SubtitleBoxObservation& o, int x, int y)
    {
        const auto& box = o.text.bounds;
        if (x < box.left || x >= box.right || y < box.top || y >= box.bottom) return false;
        const auto& ink = *o.ink;
        // Do not enlarge ownership from a box, nearest row, or display padding.
        // Unrepresented native coordinates remain in the comparison.
        if (x % ink.step != 0) return false;
        const auto row = std::lower_bound(ink.sourceRows.begin(), ink.sourceRows.end(), y);
        return row != ink.sourceRows.end() && *row == y &&
            ink.Get(x / ink.step, int(row - ink.sourceRows.begin()), true) &&
            ink.Get(x / ink.step, int(row - ink.sourceRows.begin()), false);
    }
    Snapshot m_previous;
};
