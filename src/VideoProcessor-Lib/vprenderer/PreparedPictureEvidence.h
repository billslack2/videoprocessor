#pragma once
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <AnalysisFrame.h>
#include "InwardCaptionEvidence.h"
#include "OptionalAnalysisWorker.h"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

// These are complete pixel measurements, not timeline votes or presentation
// authority. Only the caller can admit them against current model/owner state.
enum class PreparedPictureEvidenceKind : uint8_t
{
    Retention,
    RelativeContrast,
    InwardCaption,
    Count,
};

struct PreparedPictureEvidenceKey
{
    PreparedPictureEvidenceKind kind = PreparedPictureEvidenceKind::Retention;
    PreparedAnalysisFrame frame; // Retain exact immutable storage while reusable.
    ActivePictureFrameIdentity identity;
    ActivePictureBounds base;
    uint64_t policyGeneration = 0, continuityGeneration = 0;
    // Retain the stable raw request slot for this exact immutable frame/key.
    // Identical concurrent completions share it; replacement contexts use a new slot.
    std::shared_ptr<const void> rawOwner;
    AlphaSourceCrop::InwardCaptionReference caption;
};

inline bool SamePreparedPictureBounds(const ActivePictureBounds& a, const ActivePictureBounds& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
        a.rasterWidth == b.rasterWidth && a.rasterHeight == b.rasterHeight &&
        a.aspectRatio == b.aspectRatio && a.trustedBarAxes == b.trustedBarAxes;
}

inline bool SamePreparedPictureOwner(const std::shared_ptr<const void>& a,
    const std::shared_ptr<const void>& b)
{
    return a.get() == b.get() && !a.owner_before(b) && !b.owner_before(a);
}

inline bool SamePreparedPictureFrame(const PreparedAnalysisFrame& a, const PreparedAnalysisFrame& b)
{
    const auto& x = a.source;
    const auto& y = b.source;
    return a.domain == b.domain && a.profile == b.profile && a.domainKey == b.domainKey &&
        a.transferKey == b.transferKey && a.sampleRange == b.sampleRange &&
        a.colorDepth == b.colorDepth && a.bitShift == b.bitShift &&
        a.rgbBitAlignment == b.rgbBitAlignment && a.dolbyFullRange == b.dolbyFullRange &&
        a.usesConverted == b.usesConverted && SamePreparedPictureOwner(a.sourceOwner, b.sourceOwner) &&
        x.data == y.data && x.dataBytes == y.dataBytes && x.width == y.width && x.height == y.height &&
        x.rowBytes == y.rowBytes && x.chromaRowBytes == y.chromaRowBytes &&
        x.format == y.format && x.encoding == y.encoding && x.colorspace == y.colorspace &&
        x.generation == y.generation && x.rgbBitAlignment == y.rgbBitAlignment && x.range == y.range &&
        SamePreparedPictureOwner(x.backingOwner, y.backingOwner);
}

inline bool SamePreparedPictureEvidenceKey(const PreparedPictureEvidenceKey& a,
    const PreparedPictureEvidenceKey& b)
{
    return a.kind == b.kind && SameAnalysisFrameIdentity(a.identity, b.identity) &&
        SamePreparedPictureFrame(a.frame, b.frame) && SamePreparedPictureBounds(a.base, b.base) &&
        a.policyGeneration == b.policyGeneration && a.continuityGeneration == b.continuityGeneration &&
        SamePreparedPictureOwner(a.rawOwner, b.rawOwner) &&
        (a.kind != PreparedPictureEvidenceKind::InwardCaption ||
            (a.caption.valid == b.caption.valid &&
                SamePreparedPictureBounds(a.caption.base, b.caption.base) &&
                SamePreparedPictureBounds(a.caption.candidate, b.caption.candidate) &&
                SamePreparedPictureBounds(a.caption.remembered, b.caption.remembered)));
}

inline bool ValidPreparedPictureEvidenceKey(const PreparedPictureEvidenceKey& key)
{
    const auto& b = key.base;
    return static_cast<size_t>(key.kind) < static_cast<size_t>(PreparedPictureEvidenceKind::Count) &&
        key.frame.IsValid() && key.rawOwner && key.identity.transportGeneration &&
        key.identity.acceptedSequence && key.identity.sourceFormatGeneration &&
        key.identity.analysisDomainKey == key.frame.domainKey &&
        key.frame.profile == AnalysisSourceProfile::QueuedCapture &&
        key.frame.source.generation == key.identity.sourceFormatGeneration &&
        key.policyGeneration && key.continuityGeneration &&
        b.rasterWidth == key.frame.source.width && b.rasterHeight == key.frame.source.height &&
        b.left >= 0 && b.top >= 0 && b.right <= b.rasterWidth && b.bottom <= b.rasterHeight &&
        b.left < b.right && b.top < b.bottom && std::isfinite(b.aspectRatio) && b.aspectRatio > 0 &&
        (key.kind != PreparedPictureEvidenceKind::InwardCaption ||
            (key.caption.valid && SamePreparedPictureBounds(key.caption.base, key.base)));
}

inline PreparedPictureEvidenceKey MakePreparedPictureEvidenceKey(PreparedPictureEvidenceKind kind,
    const PreparedAnalysisFrame& frame, const ActivePictureFrameIdentity& identity,
    const ActivePictureBounds& base, uint64_t policyGeneration, uint64_t continuityGeneration,
    std::shared_ptr<const void> rawOwner,
    const AlphaSourceCrop::InwardCaptionReference& caption = {})
{
    PreparedPictureEvidenceKey key;
    key.kind = kind; key.frame = frame; key.identity = identity; key.base = base;
    key.policyGeneration = policyGeneration; key.continuityGeneration = continuityGeneration;
    key.rawOwner = std::move(rawOwner); key.caption = caption;
    return key;
}

struct PreparedPictureEvidence
{
    PreparedPictureEvidenceKey key;
    ActivePicturePresentationRetentionEvidence retention;
    RelativeBarContrastEvidence relativeContrast;
    AlphaSourceCrop::InwardCaptionEvidence caption;
};

// The caller supplies the exact immutable extraction identified by key.rawOwner.
// Do not use this function to import a differently interpreted raw observation.
inline PreparedPictureEvidence EvaluatePreparedPictureEvidence(const PreparedPictureEvidenceKey& key,
    const ActivePictureEvidence& raw, const ActivePictureArDarknessEvidence& darkness,
    const AnalysisEvaluationBudget* budget = nullptr)
{
    if (!ValidPreparedPictureEvidenceKey(key)) throw std::invalid_argument("invalid prepared picture evidence key");
    CheckAnalysisEvaluationBudget(budget);
    PreparedPictureEvidence result;
    result.key = key;
    switch (key.kind)
    {
    case PreparedPictureEvidenceKind::Retention:
        result.retention = EvaluateActivePicturePresentationRetention(
            key.frame.source, key.base, raw, darkness.global, darkness, budget);
        break;
    case PreparedPictureEvidenceKind::RelativeContrast:
    {
        auto source = key.frame.source;
        // The independent proof's consumer uses transport, not format generation.
        source.generation = key.identity.transportGeneration;
        result.relativeContrast = InspectRelativeBarContrast(source, raw, key.base, budget);
        break;
    }
    case PreparedPictureEvidenceKind::InwardCaption:
        result.caption = AlphaSourceCrop::InspectInwardCaptionEvidence(
            key.frame.source, raw, key.caption, budget);
        break;
    default:
        throw std::invalid_argument("invalid prepared picture evidence kind");
    }
    return result;
}
// A published result means that the key's one component finished completely.
// Its typed valid/analysisValid flags may be false: complete negative results
// are reusable. A cancelled or throwing scan must publish no result at all.
using PreparedPictureEvidenceWorker = OptionalAnalysisWorker<PreparedPictureEvidence>;

class PreparedPictureEvidenceCache
{
public:
    using Result = PreparedPictureEvidence;
    using Worker = PreparedPictureEvidenceWorker;
    using Slot = Worker::Slot;
    static constexpr size_t EntriesPerKind = 2;

    // Request metadata is presentation-thread-owned. Workers never access the
    // cache: they publish immutable results only to their captured Slot.
    // No cache/worker mutex is acquired or waited upon here.
    std::shared_ptr<const Result> Lookup(const PreparedPictureEvidenceKey& key) const
    {
        if (!ValidPreparedPictureEvidenceKey(key)) return nullptr;
        const auto* entry = Find(key);
        if (!entry) return nullptr;
        const auto ready = Worker::Ready(entry->slot);
        return ready && SamePreparedPictureEvidenceKey(ready->key, key) ? ready : nullptr;
    }

    // Existing ready/pending requests return the same slot. The caller checks
    // Lookup and pending before attempting nonblocking Worker::Submit. A failed
    // submission can retry this slot on a later presentation opportunity.
    std::shared_ptr<Slot> Request(const PreparedPictureEvidenceKey& key)
    {
        if (!ValidPreparedPictureEvidenceKey(key)) return nullptr;
        if (auto* entry = Find(key)) return entry->slot;
        const auto kind = static_cast<size_t>(key.kind);
        auto& entries = m_entries[kind];
        size_t selected = m_next[kind];
        for (size_t i = 0; i < EntriesPerKind; ++i)
            if (!entries[i].slot) { selected = i; break; }
        // Allocate before changing metadata: allocation failure preserves the
        // previous requests/results. A fresh slot isolates late old publication.
        auto slot = std::make_shared<Slot>();
        entries[selected].key = key;
        entries[selected].slot = slot;
        m_next[kind] = (selected + 1) % EntriesPerKind;
        return slot;
    }

    void Clear()
    {
        for (auto& entries : m_entries)
            for (auto& entry : entries) entry = {};
        m_next = {};
    }

private:
    struct Entry
    {
        PreparedPictureEvidenceKey key;
        std::shared_ptr<Slot> slot;
    };
    static constexpr size_t KindCount = static_cast<size_t>(PreparedPictureEvidenceKind::Count);
    std::array<std::array<Entry, EntriesPerKind>, KindCount> m_entries;
    std::array<size_t, KindCount> m_next{};
    Entry* Find(const PreparedPictureEvidenceKey& key)
    {
        for (auto& entry : m_entries[static_cast<size_t>(key.kind)])
            if (entry.slot && SamePreparedPictureEvidenceKey(entry.key, key)) return &entry;
        return nullptr;
    }
    const Entry* Find(const PreparedPictureEvidenceKey& key) const
    {
        for (const auto& entry : m_entries[static_cast<size_t>(key.kind)])
            if (entry.slot && SamePreparedPictureEvidenceKey(entry.key, key)) return &entry;
        return nullptr;
    }
};