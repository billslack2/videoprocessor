#pragma once
#include <SubtitleBoxDetector.h>
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <algorithm>
#include <chrono>

// A measurement belongs to source pixels, never retained presentation geometry.
struct SubtitleBoxObservation
{
    ActivePictureFrameIdentity identity;
    SubtitleBoxResult text;
    int width = 0, height = 0, pictureTop = 0, pictureBottom = 0;
    bool analyzed = false, barAuthority = false, discontinuity = false;
    double analysisMs = 0.0;
};

struct SubtitleBoxPreview
{
    SubtitleBoxObservation current;
    SubtitleBoxResult text;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    unsigned matchingFrames = 0;
    bool available = false, futureAvailable = false, currentLinesConfirmed = false;
    double newScanMs = 0.0;
    unsigned newScanFrames = 0;
};

namespace SubtitleBoxLookahead
{
    constexpr size_t MaxFrames = 8;

    inline SubtitleBoxObservation Measure(SubtitleBoxDetector& scanner,
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureFrameIdentity& identity, bool discontinuity)
    {
        SubtitleBoxObservation result;
        const auto start = std::chrono::steady_clock::now();
        result.identity = identity; result.analyzed = true;
        result.width = source.width; result.height = source.height;
        result.discontinuity = discontinuity;
        // A clean opposite edge can establish the current frame's bar behind
        // sparse glyphs; this helper inspects pixels and never retains old bars.
        const auto bars = EvaluateSymmetricVerticalBarHypothesis(source, raw, true);
        const auto& bounds = bars.trustedBounds;
        result.barAuthority = source.IsValid() && bars.available &&
            bars.classification == ActivePictureClassification::BAR_CROP_TRUSTED &&
            (static_cast<unsigned>(bounds.trustedBarAxes) &
                static_cast<unsigned>(ActivePictureBounds::BarAxes::TOP_BOTTOM)) != 0 &&
            bounds.rasterWidth == source.width && bounds.rasterHeight == source.height &&
            bounds.top >= 0 && bounds.bottom <= source.height && bounds.bottom > bounds.top &&
            (bounds.top > 0 || bounds.bottom < source.height);
        if (result.barAuthority)
        {
            result.pictureTop = bounds.top; result.pictureBottom = bounds.bottom;
            scanner.Reset();
            result.text = scanner.Analyze(source, bounds.top, bounds.bottom,
                identity.acceptedSequence, identity.viewportGeneration);
        }
        result.analysisMs = std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        return result;
    }

    inline bool SameContext(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        const auto& x = a.identity; const auto& y = b.identity;
        return x.transportGeneration == y.transportGeneration &&
            x.sourceFormatGeneration == y.sourceFormatGeneration &&
            x.viewportGeneration == y.viewportGeneration && x.rendererGeneration == y.rendererGeneration &&
            a.width == b.width && a.height == b.height &&
            a.pictureTop == b.pictureTop && a.pictureBottom == b.pictureBottom &&
            a.barAuthority && b.barAuthority;
    }

    template<size_t N>
    inline bool SimilarSignature(const std::array<uint64_t, N>& a,
        const std::array<uint64_t, N>& b)
    {
        int difference = 0, total = 0;
        auto bits = [](uint64_t value) { int n = 0; for (; value; value &= value - 1) ++n; return n; };
        for (size_t i = 0; i < a.size(); ++i)
        { difference += bits(a[i] ^ b[i]); total += bits(a[i] | b[i]); }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SimilarSignature(const std::array<uint8_t, 1024>& a,
        const std::array<uint8_t, 1024>& b)
    {
        unsigned difference = 0, total = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            difference += static_cast<unsigned>(std::abs(int(a[i])-int(b[i])));
            total += (std::max)(a[i],b[i]);
        }
        return total > 0 && difference * 100 <= total * 25;
    }

    inline bool SameLine(const SubtitleBoxRect& a, const SubtitleBoxRect& b, int tolerance)
    {
        return a.Valid() && b.Valid() && std::abs(a.left-b.left) <= tolerance &&
            std::abs(a.right-b.right) <= tolerance && std::abs(a.top-b.top) <= tolerance &&
            std::abs(a.bottom-b.bottom) <= tolerance;
    }

    inline bool ContainsCurrentInk(const SubtitleBoxRect& box, const SubtitleBoxResult& text)
    {
        if (!box.Valid()) return false;
        for (int i = 0; i < text.lineCount; ++i)
        {
            const auto& line = text.lineBounds[i];
            if (line.left < box.left || line.top < box.top ||
                line.right > box.right || line.bottom > box.bottom) return false;
        }
        return true;
    }

    inline bool ContainsMatchedInk(const SubtitleBoxRect& box, const SubtitleBoxResult& current,
        const SubtitleBoxResult& accepted, int tolerance)
    {
        for (int i = 0; i < current.lineCount; ++i)
            for (int j = 0; j < accepted.lineCount; ++j)
                if (SameLine(current.lineBounds[i],accepted.lineBounds[j],tolerance) &&
                    SimilarSignature(current.lineSignatures[i],accepted.lineSignatures[j]))
                {
                    const auto& line = current.lineBounds[i];
                    if (line.left < box.left || line.top < box.top ||
                        line.right > box.right || line.bottom > box.bottom) return false;
                }
        return true;
    }

    inline bool SameCue(const SubtitleBoxObservation& a, const SubtitleBoxObservation& b)
    {
        if (!SameContext(a,b) || !a.text.detected || !b.text.detected) return false;
        const int tolerance = (std::max)(4, a.height / 180);
        // Every common-height line must agree. An unchanged bottom line cannot
        // lend confirmation to a changed upper line. Missing lines may be added.
        int matches = 0;
        for (int i = 0; i < a.text.lineCount; ++i)
            for (int j = 0; j < b.text.lineCount; ++j)
            {
                const auto& x = a.text.lineBounds[i]; const auto& y = b.text.lineBounds[j];
                if (std::abs(x.top-y.top) > tolerance || std::abs(x.bottom-y.bottom) > tolerance) continue;
                if (!SameLine(x,y,tolerance) ||
                    !SimilarSignature(a.text.lineSignatures[i], b.text.lineSignatures[j])) return false;
                ++matches;
            }
        return matches > 0 && matches == (std::min)(a.text.lineCount,b.text.lineCount);
    }

    inline SubtitleBoxPreview Resolve(const SubtitleBoxObservation* frames, size_t count,
        uint64_t policy, uint64_t continuity)
    {
        SubtitleBoxPreview result;
        if (!frames || !count || !frames[0].analyzed) return result;
        result.available = true; result.current = frames[0]; result.text = frames[0].text;
        result.policyGeneration = policy; result.continuityGeneration = continuity;
        result.futureAvailable = count > 1;
        if (!frames[0].barAuthority || !frames[0].text.detected) { result.text = {}; return result; }
        result.matchingFrames = 1;
        for (size_t i = 1; i < (std::min)(count,MaxFrames); ++i)
        {
            const auto& next = frames[i];
            if (!next.analyzed || next.discontinuity ||
                next.identity.acceptedSequence != frames[i-1].identity.acceptedSequence + 1 ||
                !SameCue(frames[0],next) || !SameCue(frames[i-1],next)) break;
            ++result.matchingFrames;
        }
        const int tolerance = (std::max)(4,frames[0].height/180);
        result.currentLinesConfirmed = true;
        for (int line = 0; line < frames[0].text.lineCount; ++line)
        {
            unsigned support = 0;
            for (size_t frame = 0; frame < result.matchingFrames; ++frame)
                for (int other = 0; other < frames[frame].text.lineCount; ++other)
                    if (SameLine(frames[0].text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance) &&
                        SimilarSignature(frames[0].text.lineSignatures[line],frames[frame].text.lineSignatures[other]))
                    { ++support; break; }
            result.currentLinesConfirmed = result.currentLinesConfirmed && support >= 2;
        }
        for (size_t i = 1; i < result.matchingFrames; ++i)
        {
            const auto& next = frames[i];
            bool companionsConfirmed = true;
            // A transient picture-side stroke group must not enlarge the box
            // just because the actual bar subtitle persists beneath it.
            for (int line = 0; line < next.text.lineCount; ++line)
            {
                bool presentNow = false;
                for (int current = 0; current < frames[0].text.lineCount; ++current)
                    presentNow = presentNow || (SameLine(next.text.lineBounds[line],
                        frames[0].text.lineBounds[current],tolerance) &&
                        SimilarSignature(next.text.lineSignatures[line],frames[0].text.lineSignatures[current]));
                if (presentNow) continue;
                unsigned support = 0;
                for (size_t frame = 0; frame < result.matchingFrames; ++frame)
                    for (int other = 0; other < frames[frame].text.lineCount; ++other)
                        if (SameLine(next.text.lineBounds[line],frames[frame].text.lineBounds[other],tolerance) &&
                            SimilarSignature(next.text.lineSignatures[line],frames[frame].text.lineSignatures[other]))
                        { ++support; break; }
                companionsConfirmed = companionsConfirmed && support >= 2;
            }
            if (!companionsConfirmed) continue;
            const auto& r = next.text.bounds;
            auto& box = result.text.bounds;
            box = {(std::min)(box.left,r.left),(std::min)(box.top,r.top),
                (std::max)(box.right,r.right),(std::max)(box.bottom,r.bottom)};
            if (next.text.lineCount > result.text.lineCount)
            {
                result.text.lineCount = next.text.lineCount;
                result.text.lineBounds = next.text.lineBounds;
                result.text.lineSignatures = next.text.lineSignatures;
                result.text.signature = next.text.signature;
            }
        }
        return result;
    }

    inline bool IsCurrent(const SubtitleBoxPreview& result,
        const ActivePictureFrameIdentity& identity, uint64_t policy, uint64_t continuity)
    {
        return result.available && SameActivePictureFrameIdentity(result.current.identity,identity) &&
            result.policyGeneration == policy && result.continuityGeneration == continuity;
    }
}

// Only presentation advances this state. Looking into future pixels never moves
// the current cue forward, and repeated output cannot manufacture confirmation.
class SubtitleBoxPresentation
{
public:
    void Reset() { m_previous = {}; m_reference = {}; m_result = {}; m_hasPrevious = false; }
    SubtitleBoxResult Consume(const SubtitleBoxPreview& preview)
    {
        if (!preview.available) { Reset(); return {}; }
        if (m_hasPrevious && SameActivePictureFrameIdentity(m_previous.current.identity,preview.current.identity) &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration) return m_result;
        bool same = m_hasPrevious && !preview.current.discontinuity &&
            m_previous.policyGeneration == preview.policyGeneration &&
            m_previous.continuityGeneration == preview.continuityGeneration &&
            preview.current.identity.acceptedSequence == m_previous.current.identity.acceptedSequence + 1 &&
            SubtitleBoxLookahead::SameCue(m_previous.current,preview.current) &&
            SubtitleBoxLookahead::SameCue(m_reference,preview.current) &&
            preview.text.lineCount >= m_result.lineCount && m_result.detected &&
            ((preview.text.lineCount > m_result.lineCount &&
                (!preview.futureAvailable || preview.currentLinesConfirmed ||
                    SubtitleBoxLookahead::ContainsMatchedInk(m_result.bounds,preview.current.text,m_result,
                        (std::max)(4,preview.current.height/180)))) ||
                SubtitleBoxLookahead::ContainsCurrentInk(m_result.bounds,preview.current.text));
        auto result = preview.text;
        if (!preview.current.barAuthority || !preview.current.text.detected ||
            (!same && preview.futureAvailable &&
                (preview.matchingFrames < 2 || !preview.currentLinesConfirmed))) result = {};
        if (result.detected)
        {
            if (same)
            {
                // Fresh matching strokes still support the established cue,
                // but one unmatched companion cannot enlarge its stable box.
                if (preview.futureAvailable && !preview.currentLinesConfirmed &&
                    result.lineCount > m_result.lineCount) result = m_result;
                const auto& old = m_result.bounds;
                if (result.lineCount > m_result.lineCount)
                {
                    result.bounds = {(std::min)(old.left,result.bounds.left),(std::min)(old.top,result.bounds.top),
                        (std::max)(old.right,result.bounds.right),(std::max)(old.bottom,result.bounds.bottom)};
                    m_reference.text = result;
                }
                else result.bounds = old;
                result.lineCount = (std::max)(result.lineCount,m_result.lineCount);
                result.cue = m_result.cue; result.observations = m_result.observations + 1;
                result.revised = !SubtitleBoxLookahead::SameLine(old,result.bounds,0);
            }
            else
            {
                result.cue = ++m_nextCue; result.observations = 1; result.revised = false;
                m_reference = preview.current; m_reference.text = result;
            }
        }
        m_previous = preview;
        // Keep only accepted cue evidence as the temporal reference. Rejected
        // transient companions must not poison the next frame's comparison.
        m_previous.current.text = result;
        m_result = result; m_hasPrevious = true;
        return result;
    }
private:
    SubtitleBoxPreview m_previous;
    SubtitleBoxObservation m_reference;
    SubtitleBoxResult m_result;
    uint64_t m_nextCue = 0;
    bool m_hasPrevious = false;
};
