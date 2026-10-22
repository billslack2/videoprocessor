#pragma once
#include <cstdint>

// A consumed but not yet committed intent must not be mistaken for an idle mailbox.
inline bool SubtitleUpdateHasCommittedContext(bool pending, uint64_t committed, uint64_t requested)
{
    return !pending && committed == requested;
}

template<class Settings>
bool SameSubtitlePictureContext(const Settings& current, const Settings& next)
{
    return current.configuredScreenAspect == next.configuredScreenAspect &&
        current.configuredScreenTarget == next.configuredScreenTarget &&
        current.verticalAlignment == next.verticalAlignment &&
        current.screenEdgePadding == next.screenEdgePadding &&
        current.anamorphicScale == next.anamorphicScale &&
        current.automaticSourceCropRequested == next.automaticSourceCropRequested &&
        current.automaticSourceCrop == next.automaticSourceCrop &&
        current.cropNarrowerContentToFillScreen == next.cropNarrowerContentToFillScreen &&
        current.cropNarrowerContentAspectLimitConfigured == next.cropNarrowerContentAspectLimitConfigured &&
        current.cropNarrowerContentAspectLimit == next.cropNarrowerContentAspectLimit &&
        current.cropWiderContentToFillScreen == next.cropWiderContentToFillScreen &&
        current.cropWiderContentAspectLimitConfigured == next.cropWiderContentAspectLimitConfigured &&
        current.cropWiderContentAspectLimit == next.cropWiderContentAspectLimit &&
        current.fixedCropAspectConfigured == next.fixedCropAspectConfigured &&
        current.fixedCropAspect == next.fixedCropAspect &&
        current.subtitleDetectionOptimization == next.subtitleDetectionOptimization &&
        current.subtitleScriptHint == next.subtitleScriptHint &&
        current.subtitleNearBarDistance == next.subtitleNearBarDistance &&
        current.subtitleHoldMs == next.subtitleHoldMs;
}


// Explicit presentation-only allowlist. All other fields remain in the complete
// effective settings fingerprint, including detector and viewport policies.
// New settings therefore default to the conservative boundary-reset path.
template<class Settings, class Fingerprint>
bool IsSubtitlePresentationOnlyUpdate(const Settings& current, const Settings& next,
    Fingerprint fingerprint)
{
    if (!SameSubtitlePictureContext(current, next)) return false;
    if (fingerprint(current) == fingerprint(next)) return false;
    auto normalized = next;
    normalized.subtitleCutPasteBackground = current.subtitleCutPasteBackground;
    normalized.subtitleGeneratedGray = current.subtitleGeneratedGray;
    normalized.subtitleBoxPadding = current.subtitleBoxPadding;
    normalized.subtitlePaddingPolicy = current.subtitlePaddingPolicy;
    normalized.subtitleMode = current.subtitleMode;
    normalized.subtitleMoveInset = current.subtitleMoveInset;
    normalized.subtitleTextReductionPercent = current.subtitleTextReductionPercent;
    normalized.subtitleRoundedCorners = current.subtitleRoundedCorners;
    normalized.subtitleFloatBackground = current.subtitleFloatBackground;
    normalized.subtitleTemporalFill = current.subtitleTemporalFill;
    normalized.subtitleSpatialFill = current.subtitleSpatialFill;
    normalized.scopeSubtitleFit = current.scopeSubtitleFit;
    // These presets affect subtitle placement and tone-map metering, not source AR.
    normalized.scopeSubtitleHoldMs = current.scopeSubtitleHoldMs;
    normalized.scopeSubtitleEngageDriftMs = current.scopeSubtitleEngageDriftMs;
    normalized.scopeSubtitleReleaseDriftMs = current.scopeSubtitleReleaseDriftMs;
    normalized.scopeSubtitlePaddingPixels = current.scopeSubtitlePaddingPixels;
    normalized.scopeSubtitleTargetBufferPixels = current.scopeSubtitleTargetBufferPixels;
    normalized.hdrPeakAnalysisPictureOnly = current.hdrPeakAnalysisPictureOnly;
    normalized.hdrPeakAnalysisMotionCompensation = current.hdrPeakAnalysisMotionCompensation;
    normalized.hdrPeakAnalysisHeightPercent = current.hdrPeakAnalysisHeightPercent;
    normalized.hdrPeakAnalysisPosition = current.hdrPeakAnalysisPosition;
    return fingerprint(current) == fingerprint(normalized);
}
