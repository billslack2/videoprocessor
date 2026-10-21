#pragma once

#include <ActivePictureEvidence.h>

// Render-thread only, scoped to one source frame's output batch. Cache pixel
// observations, never crop decisions, evidence leases or presentation timers.
class ActivePictureMeasurementCache
{
public:
    struct Statistics
    {
        size_t globalRequests = 0, globalHits = 0;
        size_t pictureRequests = 0, pictureHits = 0;
        size_t retentionRequests = 0, retentionHits = 0;
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
            m_globalValid = m_pictureValid = m_retentionValid = false;
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
    Statistics m_statistics;
};
