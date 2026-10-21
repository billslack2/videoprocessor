#pragma once

#include <ActivePictureEvidence.h>
#include <algorithm>
#include <chrono>

// Render-thread only, scoped to one source frame's output batch. Cache pixel
// observations, never crop decisions, evidence leases or presentation timers.
class ActivePictureMeasurementCache
{
public:
    struct VerticalBarContent
    {
        bool upper = false, lower = false;
        int upperTop = 0, upperBottom = 0, lowerTop = 0, lowerBottom = 0;
        int upperPeak = 0, lowerPeak = 0;
    };

    struct Statistics
    {
        size_t globalRequests = 0, globalHits = 0;
        size_t pictureRequests = 0, pictureHits = 0;
        size_t retentionRequests = 0, retentionHits = 0;
        size_t subtitleRequests = 0, subtitleHits = 0;
        double subtitleScanMs = 0, subtitleScanPeakMs = 0;
    };

    explicit ActivePictureMeasurementCache(bool enabled = true) : m_enabled(enabled) {}

    void Begin(const AnalysisLumaSource& source,
        const ActivePictureFrameIdentity& identity,
        uint64_t policyGeneration, uint64_t continuityGeneration)
    {
        const bool cacheable = m_enabled && source.backingOwner && source.IsValid() &&
            source.generation && identity.transportGeneration &&
            identity.acceptedSequence && identity.analysisDomainKey;
        if (!cacheable || !m_cacheable ||
            !SameAnalysisFrameIdentity(identity, m_identity) ||
            policyGeneration != m_policyGeneration ||
            continuityGeneration != m_continuityGeneration ||
            !SameSource(source, m_source))
        {
            m_globalValid = m_pictureValid = m_retentionValid = m_subtitleValid = false;
        }
        m_source = source; // Retain immutable pixels while observations are reusable.
        m_identity = identity;
        m_policyGeneration = policyGeneration;
        m_continuityGeneration = continuityGeneration;
        m_cacheable = cacheable;
    }

    ActivePictureGlobalNearBlackEvidence GlobalNearBlack()
    {
        ++m_statistics.globalRequests;
        if (!m_cacheable) return EvaluateActivePictureGlobalNearBlack(m_source);
        if (m_cacheable && m_globalValid)
        {
            ++m_statistics.globalHits;
            return m_global;
        }
        m_global = EvaluateActivePictureGlobalNearBlack(m_source);
        m_globalValid = m_cacheable;
        return m_global;
    }

    ActivePictureEvidence Picture()
    {
        ++m_statistics.pictureRequests;
        if (!m_cacheable) return ExtractActivePictureEvidence(m_source);
        if (m_cacheable && m_pictureValid)
        {
            ++m_statistics.pictureHits;
            return m_picture;
        }
        m_picture = ExtractActivePictureEvidence(m_source);
        m_pictureValid = m_cacheable;
        return m_picture;
    }

    ActivePicturePresentationRetentionEvidence Retention(
        const ActivePictureBounds& presentation)
    {
        ++m_statistics.retentionRequests;
        if (!m_cacheable)
            return EvaluateActivePicturePresentationRetention(m_source, presentation);
        if (m_cacheable && m_retentionValid &&
            SameBounds(presentation, m_presentation))
        {
            ++m_statistics.retentionHits;
            return m_retention;
        }
        m_retention = EvaluateActivePicturePresentationRetention(m_source, presentation);
        m_presentation = presentation;
        m_retentionValid = m_cacheable;
        return m_retention; // Callers may refine their copy with current worker evidence.
    }

    VerticalBarContent SubtitleContent(const ActivePictureBounds& presentation, int blackCode)
    {
        ++m_statistics.subtitleRequests;
        if (m_cacheable && m_subtitleValid && blackCode == m_subtitleBlackCode &&
            SameBounds(presentation, m_subtitlePresentation)) {
            ++m_statistics.subtitleHits;
            return m_subtitle;
        }
        const auto started = std::chrono::steady_clock::now();
        VerticalBarContent measured;
        const int width = m_source.width, height = m_source.height;
        if (m_source.IsValid() && presentation.rasterWidth == width &&
            presentation.rasterHeight == height && presentation.top >= 0 &&
            presentation.top < presentation.bottom && presentation.bottom <= height) {
            const int x0 = width / 32, x1 = width - x0;
            const int xStep = (std::max)(1, width / 1920);
            const int rowStep = (std::max)(1, height / 1080);
            const int sampledColumns = (std::max)(1, (x1 - x0) / xStep);
            const int minimumSamples = (std::max)(8, sampledColumns / 192);
            const int minimumSpan = (std::max)(24, width / 48);
            const int limit = (std::min)(1023, (std::max)(0, blackCode + 32));
            const auto scan = [&](int top, int bottom, int& first, int& last, int& peak) {
                first = height; last = peak = 0;
                int rows = 0;
                for (int y = top; y < bottom; y += rowStep) {
                    AnalysisLumaRowContent row;
                    if (!m_source.MeasureLumaRowContent(y, x0, x1, xStep,
                            static_cast<uint16_t>(limit), row)) {
                        first = last = peak = 0;
                        return false;
                    }
                    if (row.samples < minimumSamples || row.right - row.left < minimumSpan)
                        continue;
                    ++rows;
                    peak = (std::max)(peak, row.samples);
                    first = (std::min)(first, y);
                    last = (std::max)(last, y + rowStep);
                }
                if (rows < 2) { first = last = 0; return false; }
                return true;
            };
            measured.upper = presentation.top > rowStep * 2 &&
                scan(rowStep, presentation.top - rowStep,
                    measured.upperTop, measured.upperBottom, measured.upperPeak);
            measured.lower = presentation.bottom + rowStep * 2 < height &&
                scan(presentation.bottom + rowStep, height - rowStep,
                    measured.lowerTop, measured.lowerBottom, measured.lowerPeak);
        }
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        m_statistics.subtitleScanMs += elapsed;
        m_statistics.subtitleScanPeakMs = (std::max)(m_statistics.subtitleScanPeakMs, elapsed);
        m_subtitle = measured;
        m_subtitlePresentation = presentation;
        m_subtitleBlackCode = blackCode;
        m_subtitleValid = m_cacheable;
        return measured;
    }

    const Statistics& Snapshot() const { return m_statistics; }

private:
    static bool SameSource(const AnalysisLumaSource& a, const AnalysisLumaSource& b)
    {
        return a.data == b.data && a.dataBytes == b.dataBytes &&
            a.width == b.width && a.height == b.height &&
            a.rowBytes == b.rowBytes && a.chromaRowBytes == b.chromaRowBytes &&
            a.format == b.format && a.encoding == b.encoding &&
            a.colorspace == b.colorspace && a.generation == b.generation &&
            a.rgbBitAlignment == b.rgbBitAlignment && a.range == b.range &&
            a.backingOwner.get() == b.backingOwner.get() &&
            !a.backingOwner.owner_before(b.backingOwner) &&
            !b.backingOwner.owner_before(a.backingOwner);
    }

    static bool SameBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right &&
            a.bottom == b.bottom && a.rasterWidth == b.rasterWidth &&
            a.rasterHeight == b.rasterHeight && a.aspectRatio == b.aspectRatio &&
            a.trustedBarAxes == b.trustedBarAxes;
    }

    AnalysisLumaSource m_source;
    ActivePictureFrameIdentity m_identity;
    uint64_t m_policyGeneration = 0, m_continuityGeneration = 0;
    bool m_enabled = true;
    bool m_cacheable = false;
    bool m_globalValid = false, m_pictureValid = false, m_retentionValid = false;
    ActivePictureGlobalNearBlackEvidence m_global;
    ActivePictureEvidence m_picture;
    ActivePicturePresentationRetentionEvidence m_retention;
    ActivePictureBounds m_presentation;
    VerticalBarContent m_subtitle;
    ActivePictureBounds m_subtitlePresentation;
    int m_subtitleBlackCode = 0;
    bool m_subtitleValid = false;
    Statistics m_statistics;
};
