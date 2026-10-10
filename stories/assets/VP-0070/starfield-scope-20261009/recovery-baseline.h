#pragma once
#include <SubtitleBarEvidence.h>
#include <ActivePictureEvidence.h>

// Inspection only. Remembered geometry must be re-proved by BOTH physical
// edges in this frame; it is not permission to crop or reuse old subtitle ink.
inline ActivePictureBounds RecoverSubtitleInspectionBars(const AnalysisLumaSource& source,
    const ActivePictureBounds& trusted, uint64_t trustedGeneration,
    ActivePictureClassification classification, bool enabled, bool blocked)
{
    if (!enabled || blocked || !source.IsValid() || !source.generation ||
        trustedGeneration != source.generation ||
        (classification != ActivePictureClassification::PROVISIONAL &&
         classification != ActivePictureClassification::BAR_CROP_TRUSTED) ||
        trusted.rasterWidth != source.width || trusted.rasterHeight != source.height ||
        trusted.left != 0 || trusted.right != source.width ||
        trusted.top <= 0 || trusted.bottom >= source.height || trusted.top >= trusted.bottom)
        return {};
    const auto bars = ExtractSubtitleBarEvidence(source);
    const int tolerance = (std::max)(2, 2 * (source.height / 540));
    if (!bars.available || bars.top <= 0 || bars.bottom >= source.height ||
        trusted.top > bars.top || trusted.bottom < bars.bottom ||
        std::abs(bars.top - trusted.top) > tolerance ||
        std::abs(bars.bottom - trusted.bottom) > tolerance)
        return {};
    // Keep the same Classic contract and caption placement. It may include a
    // sampling fringe of extra black, but must contain all current picture rows.
    return trusted;
}
