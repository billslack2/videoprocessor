#pragma once

#include <ActivePictureDecisionTimeline.h>

namespace AlphaSourceCrop
{

// Presentation-only anti-pumping gate. Every allowed frame still needs its own
// successful composition/cleanup certificate. This state never supplies pixels,
// crop authority, or history, and never holds a crop through failed composition.
struct SubtitlePresentationContinuityInput
{
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    ActivePictureBounds logical;
    uint64_t cue = 0; // diagnostic identity, never a rearm condition
    bool certificateAccepted = false;
    bool independentRawBandsClear = false;
    bool discontinuity = false;
};

struct SubtitlePresentationContinuityDecision
{
    bool allow = false, inhibited = false;
    const char* reason = "certificate-unavailable";
};

class SubtitlePresentationContinuityGate
{
public:
    void Reset() { m_valid = m_wasAllowed = m_inhibited = false; m_previous = {}; }

    // A read-only work gate, not a certificate or state transition. Evaluate
    // must still consume the frame when this skips an expensive native audit.
    // The current certificateAccepted field is intentionally ignored here.
    bool CanAttempt(const SubtitlePresentationContinuityInput& input) const
    {
        if (!Valid(input) || input.discontinuity) return false;
        if (StartsNewContext(input)) return true;
        const bool repeated = input.identity.acceptedSequence == m_previous.identity.acceptedSequence;
        if (repeated) return m_wasAllowed && !m_inhibited;
        return !m_inhibited || input.independentRawBandsClear;
    }
    SubtitlePresentationContinuityDecision Evaluate(const SubtitlePresentationContinuityInput& input)
    {
        SubtitlePresentationContinuityDecision result;
        if (!Valid(input))
        {
            // Invalid input must not erase a latch and permit a later same-context
            // frame to rearm. Widen immediately; explicit Reset owns real resets.
            if (m_wasAllowed) m_inhibited = true;
            m_wasAllowed = false;
            result.inhibited = m_inhibited;
            result.reason = "invalid-continuity-context";
            return result;
        }
        const bool newContext = StartsNewContext(input);
        if (newContext) Reset();
        const bool repeated = m_valid &&
            input.identity.acceptedSequence == m_previous.identity.acceptedSequence;
        if (repeated)
        {
            // Do not reuse a cached success: each repeat must bring a fresh
            // accepted certificate. Once lost, this sequence cannot restore it,
            // even if a later call claims raw-band clearance or a different cue.
            if (m_wasAllowed && !input.certificateAccepted) m_inhibited = true;
            result.allow = input.certificateAccepted && m_wasAllowed && !m_inhibited;
            m_wasAllowed = result.allow;
            result.inhibited = m_inhibited;
            result.reason = result.allow ? "current-repeat-certificate" :
                m_inhibited ? "inhibited-until-independent-raw-clear" : "repeat-cannot-engage";
            return result;
        }
        // Only fresh current RAW-source clearance can end the episode. A new
        // cue, a held/missing cue, or synthetic cleanup does not reset the latch.
        if (input.independentRawBandsClear)
        {
            m_inhibited = false; m_wasAllowed = false;
        }
        else if (m_wasAllowed && !input.certificateAccepted)
            m_inhibited = true;
        result.allow = input.certificateAccepted && !m_inhibited;
        result.inhibited = m_inhibited;
        result.reason = result.allow ? "current-certificate-continuous" :
            m_inhibited ? "inhibited-until-independent-raw-clear" :
            input.independentRawBandsClear ? "independent-raw-clear-rearmed" : "certificate-unavailable";
        m_previous = input; m_valid = true; m_wasAllowed = result.allow;
        return result;
    }

private:
    static bool Valid(const SubtitlePresentationContinuityInput& input)
    {
        const auto& b = input.logical;
        return input.identity.acceptedSequence != 0 && b.rasterWidth > 0 && b.rasterHeight > 0 &&
            b.left >= 0 && b.top >= 0 && b.right <= b.rasterWidth && b.bottom <= b.rasterHeight &&
            b.left < b.right && b.top < b.bottom;
    }
    static bool SameContext(const SubtitlePresentationContinuityInput& a,
        const SubtitlePresentationContinuityInput& b)
    {
        const auto& x = a.identity; const auto& y = b.identity;
        const auto& p = a.logical; const auto& q = b.logical;
        return x.transportGeneration == y.transportGeneration &&
            x.sourceFormatGeneration == y.sourceFormatGeneration &&
            x.viewportGeneration == y.viewportGeneration && x.rendererGeneration == y.rendererGeneration &&
            a.policyGeneration == b.policyGeneration && a.continuityGeneration == b.continuityGeneration &&
            p.left == q.left && p.top == q.top && p.right == q.right && p.bottom == q.bottom &&
            p.rasterWidth == q.rasterWidth && p.rasterHeight == q.rasterHeight &&
            p.trustedBarAxes == q.trustedBarAxes;
    }
    bool StartsNewContext(const SubtitlePresentationContinuityInput& input) const
    {
        return !m_valid || !SameContext(input, m_previous) || input.discontinuity ||
            input.identity.acceptedSequence < m_previous.identity.acceptedSequence ||
            input.identity.captureTimestamp < m_previous.identity.captureTimestamp ||
            (input.identity.sourceFrameNumber && m_previous.identity.sourceFrameNumber &&
             input.identity.sourceFrameNumber < m_previous.identity.sourceFrameNumber);
    }
    SubtitlePresentationContinuityInput m_previous;
    bool m_valid = false, m_wasAllowed = false, m_inhibited = false;
};

}
