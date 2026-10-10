#pragma once
#include <ActivePictureDecisionTimeline.h>

namespace AlphaSourceCrop
{
// Presentation-only proof. This type deliberately cannot be submitted to the
// active-picture model or native-geometry history as a measurement.
struct SubtitleAssistedAcquisitionInput
{
    ActivePictureFrameIdentity identity, proofIdentity;
    uint64_t sourceGeneration = 0, policyGeneration = 0, continuityGeneration = 0;
    ActivePictureBounds candidate, protectedBounds;
    bool sourceProofAccepted = false, independentRawBandsClear = false, discontinuity = false;
};
struct SubtitleAssistedAcquisitionDecision
{
    bool confirmed = false, inhibited = false;
    unsigned matchingFrames = 0;
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    ActivePictureBounds candidate, protectedBounds;
    const char* reason = "source-proof-unavailable";
};
class SubtitleAssistedAcquisitionGate
{
public:
    static constexpr unsigned RequiredFrames = 4;
    void Reset() { m_previous = {}; m_valid = m_confirmed = m_inhibited = m_hasRejected = false; m_matches = 0; }
    // Read-only nominee for a NEW independent raw-band audit. Returning bounds
    // provides no display permission and never changes confirmation or history.
    bool CandidateForRevalidation(const ActivePictureFrameIdentity& identity,
        uint64_t policyGeneration, uint64_t continuityGeneration, ActivePictureBounds& bounds) const
    {
        SubtitleAssistedAcquisitionInput query;query.identity=identity;
        query.policyGeneration=policyGeneration;query.continuityGeneration=continuityGeneration;
        const auto& previous=m_previous.identity;const auto& c=m_previous.candidate;
        if(!m_valid || m_previous.discontinuity || !SameSource(query,m_previous) ||
            identity.acceptedSequence<=previous.acceptedSequence || identity.captureTimestamp<previous.captureTimestamp ||
            (identity.sourceFrameNumber && previous.sourceFrameNumber && identity.sourceFrameNumber<=previous.sourceFrameNumber) ||
            c.rasterWidth<320 || c.rasterHeight<180 || c.left!=0 || c.right!=c.rasterWidth ||
            c.top<=0 || c.bottom>=c.rasterHeight || c.top>=c.bottom) return false;
        bounds=c;return true;
    }
    SubtitleAssistedAcquisitionDecision Evaluate(const SubtitleAssistedAcquisitionInput& input)
    {
        SubtitleAssistedAcquisitionDecision result;
        const bool sameSource = m_valid && SameSource(input, m_previous);
        const bool repeated = sameSource && input.identity.acceptedSequence == m_previous.identity.acceptedSequence;
        const bool sameGeometry = sameSource && SameBounds(input.candidate, m_previous.candidate);
        const bool seek = sameSource && (input.identity.acceptedSequence < m_previous.identity.acceptedSequence ||
            input.identity.captureTimestamp < m_previous.identity.captureTimestamp ||
            (input.identity.sourceFrameNumber && m_previous.identity.sourceFrameNumber &&
             input.identity.sourceFrameNumber < m_previous.identity.sourceFrameNumber));
        if (input.discontinuity)
        {
            Reset(); m_previous = input; m_valid = true;
            result.reason = "source-discontinuity"; return result;
        }
        // A malformed or foreign proof cannot reset an existing inhibition.
        if (!Valid(input)) return Fail(input, "invalid-current-source-proof", false);
        if (m_hasRejected && SameSource(input, m_rejected) &&
            input.identity.acceptedSequence == m_rejected.identity.acceptedSequence)
            return Fail(input, "rejected-sequence-cannot-rearm", false);
        if (!sameSource || seek) Reset();
        else if (!sameGeometry)
        {
            if (m_confirmed) m_inhibited = true;
            m_confirmed = false; m_matches = 0;
        }
        if (repeated)
        {
            if (!sameGeometry || !SameIdentity(input.identity, m_previous.identity) || !input.sourceProofAccepted)
                return Fail(input, "repeat-proof-unavailable", false);
            result.confirmed = m_confirmed && !m_inhibited;
            result.inhibited = m_inhibited; result.matchingFrames = m_matches;
            if (result.confirmed) { result.candidate = input.candidate; result.protectedBounds = input.protectedBounds;
                result.identity = input.identity; result.policyGeneration = input.policyGeneration; result.continuityGeneration = input.continuityGeneration; }
            result.reason = result.confirmed ? "current-repeat-assisted-proof" : "repeat-cannot-confirm";
            return result;
        }
        if (input.independentRawBandsClear) { m_inhibited = false; m_confirmed = false; m_matches = 0; }
        const bool contiguous = m_valid && input.identity.acceptedSequence == m_previous.identity.acceptedSequence + 1 &&
            (!input.identity.sourceFrameNumber || !m_previous.identity.sourceFrameNumber ||
             input.identity.sourceFrameNumber > m_previous.identity.sourceFrameNumber);
        if (m_valid && !contiguous)
        {
            if (m_confirmed) m_inhibited = true;
            m_confirmed = false; m_matches = 0;
        }
        if (!input.sourceProofAccepted) return Fail(input, "source-proof-unavailable", true);
        if (m_inhibited) return Fail(input, "inhibited-until-independent-raw-clear", true);
        if (m_matches < RequiredFrames) ++m_matches;
        m_confirmed = m_matches >= RequiredFrames;
        m_previous = input; m_valid = true;
        result.confirmed = m_confirmed; result.matchingFrames = m_matches;
        if (m_confirmed) { result.candidate = input.candidate; result.protectedBounds = input.protectedBounds;
                result.identity = input.identity; result.policyGeneration = input.policyGeneration; result.continuityGeneration = input.continuityGeneration; }
        result.reason = m_confirmed ? "current-source-assisted-presentation" : "confirming-current-source-proof";
        return result;
    }
private:
    static bool SameIdentity(const ActivePictureFrameIdentity& a, const ActivePictureFrameIdentity& b)
    {
        return a.transportGeneration == b.transportGeneration && a.acceptedSequence == b.acceptedSequence &&
            a.sourceFrameNumber == b.sourceFrameNumber && a.captureTimestamp == b.captureTimestamp &&
            a.sourceFormatGeneration == b.sourceFormatGeneration && a.viewportGeneration == b.viewportGeneration &&
            a.rendererGeneration == b.rendererGeneration;
    }
    static bool SameSource(const SubtitleAssistedAcquisitionInput& a, const SubtitleAssistedAcquisitionInput& b)
    {
        return a.identity.transportGeneration == b.identity.transportGeneration &&
            a.identity.sourceFormatGeneration == b.identity.sourceFormatGeneration &&
            a.identity.viewportGeneration == b.identity.viewportGeneration && a.identity.rendererGeneration == b.identity.rendererGeneration &&
            a.policyGeneration == b.policyGeneration && a.continuityGeneration == b.continuityGeneration;
    }
    static bool SameBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
            a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight;
    }
    static bool Valid(const SubtitleAssistedAcquisitionInput& input)
    {
        const auto& c = input.candidate; const auto& p = input.protectedBounds;
        return input.identity.acceptedSequence != 0 && input.sourceGeneration == input.identity.transportGeneration &&
            SameIdentity(input.identity, input.proofIdentity) &&
            c.rasterWidth >= 320 && c.rasterHeight >= 180 && c.left == 0 && c.right == c.rasterWidth &&
            c.top > 0 && c.bottom < c.rasterHeight && c.top < c.bottom &&
            p.rasterWidth == c.rasterWidth && p.rasterHeight == c.rasterHeight && p.left == 0 && p.right == c.rasterWidth &&
            p.top >= 0 && p.top <= c.top && p.bottom >= c.bottom && p.bottom <= c.rasterHeight;
    }
    SubtitleAssistedAcquisitionDecision Fail(const SubtitleAssistedAcquisitionInput& input, const char* reason, bool record)
    {
        if (m_confirmed) m_inhibited = true;
        m_confirmed = false; m_matches = 0;
        m_rejected = input; m_hasRejected = true;
        if (record) { m_previous = input; m_valid = true; }
        SubtitleAssistedAcquisitionDecision result; result.inhibited = m_inhibited; result.reason = reason; return result;
    }
    SubtitleAssistedAcquisitionInput m_previous, m_rejected;
    bool m_valid = false, m_confirmed = false, m_inhibited = false, m_hasRejected = false;
    unsigned m_matches = 0;
};
}




