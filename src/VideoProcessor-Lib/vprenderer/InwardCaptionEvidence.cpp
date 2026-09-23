#include <pch.h>
#include "InwardCaptionEvidence.h"
#include "AlphaSourceCropPolicy.h"
#include <algorithm>

namespace AlphaSourceCrop
{
    InwardCaptionEvidence InspectInwardCaptionEvidence(
        const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
        const ActivePictureBounds& base, const ActivePictureTransitionModel& model)
    {
        InwardCaptionEvidence result;
        result.reason = "ineligible-base-or-observation";
        if (!source.IsValid() || source.width < 320 || source.height < 180 ||
            !raw.available || raw.classification != ActivePictureClassification::PROVISIONAL ||
            raw.axisEvidence.horizontal.FailedBar() ||
            base.rasterWidth != source.width || base.rasterHeight != source.height ||
            base.left != 0 || base.right != source.width || base.top <= 0 ||
            base.bottom >= source.height || base.top >= base.bottom ||
            base.trustedBarAxes != ActivePictureBounds::BarAxes::TOP_BOTTOM ||
            raw.proposedBounds.rasterWidth != source.width || raw.proposedBounds.rasterHeight != source.height ||
            raw.proposedBounds.left != 0 || raw.proposedBounds.right != source.width)
            return result;
        const auto dark = EvaluateActivePictureGlobalNearBlack(source);
        result.reason = "near-black";
        if (!dark.evaluated || dark.nearBlack) return result;

        auto hypothesis = EvaluateSymmetricVerticalBarHypothesis(source, raw);
        result.reason = "unproven-symmetric-boundary";
        if (hypothesis.classification != ActivePictureClassification::BAR_CROP_TRUSTED)
            return result;
        const auto candidate = hypothesis.trustedBounds;
        ActivePictureBounds remembered;
        result.reason = "not-known-inward-format";
        if (candidate.top <= base.top || candidate.bottom >= base.bottom ||
            !model.FindRecentTrustedBarGeometry(candidate, remembered) ||
            remembered.left != 0 || remembered.right != source.width ||
            remembered.rasterWidth != source.width || remembered.rasterHeight != source.height ||
            remembered.top <= base.top || remembered.bottom >= base.bottom ||
            (static_cast<double>(base.bottom-base.top)/(remembered.bottom-remembered.top)-1.0)*100.0 <=
                ActivePictureTransitionModel::STABLE_ASPECT_DEADBAND_PERCENT ||
            (static_cast<double>(base.bottom-base.top)/(candidate.bottom-candidate.top)-1.0)*100.0 <=
                ActivePictureTransitionModel::STABLE_ASPECT_DEADBAND_PERCENT) return result;

        // History identifies a possible boundary; only fresh pixels can support
        // it. Inspect BOTH prospective bands, not the old taller picture's bars.
        const int xStep = std::max(1, source.width / 480);
        const int yStep = std::max(1, source.height / 540);
        const int columns = (source.width + xStep - 1) / xStep;
        const int floor = static_cast<int>(std::min(raw.top.lumaFloor, raw.bottom.lumaFloor));
        const int limit = std::min(104, floor + 32);
        bool sampleFailed = false;
        struct Occupied { bool present=false; int top=0, bottom=0, peak=0; };
        auto inspect = [&](int first, int last) {
            Occupied occupied;
            int rows = 0;
            for (int y=first; y<last; y+=yStep)
            {
                int count=0, left=source.width, right=0;
                for (int x=0; x<source.width; x+=xStep)
                {
                    AnalysisLumaSample sample;
                    if (!source.Sample(x,y,sample)) { sampleFailed=true; continue; }
                    if (sample.luma <= limit) continue;
                    ++count; left=std::min(left,x); right=std::max(right,x);
                }
                if (count < std::max(2,columns/192) || right-left < std::max(24,source.width/48)) continue;
                if (rows++ == 0) occupied.top=y;
                occupied.bottom=std::min(last,y+yStep);
                occupied.peak=std::max(occupied.peak,count);
            }
            occupied.present=rows>=2;
            return occupied;
        };
        const auto upper=inspect(0,candidate.top);
        const auto lower=inspect(candidate.bottom,source.height);
        result.reason = "not-one-localized-band";
        if (sampleFailed || upper.present == lower.present) return result;
        // Require a black gap between picture and overlay. One-sided picture
        // growth connected to the inferred edge is not a caption certificate.
        if ((upper.present && upper.bottom > candidate.top-2*yStep) ||
            (lower.present && lower.top < candidate.bottom+2*yStep)) return result;
        VerticalBarContentInput content;
        content.upperContent=upper.present; content.lowerContent=lower.present;
        content.upperOccupiedDepth=upper.bottom-upper.top;
        content.lowerOccupiedDepth=lower.bottom-lower.top;
        content.upperPeakSamples=upper.peak; content.lowerPeakSamples=lower.peak;
        content.upperBarPixels=candidate.top; content.lowerBarPixels=source.height-candidate.bottom;
        content.sampledColumns=columns;
        content.upperRequiredShift=upper.present ? static_cast<float>(candidate.top-upper.top) : 0.0f;
        content.lowerRequiredShift=lower.present ? static_cast<float>(lower.bottom-candidate.bottom) : 0.0f;
        if (EvaluateVerticalBarContent(content).action != VerticalBarPresentationAction::TRANSLATE)
            return result;

        auto protection=candidate;
        protection.top=std::min(remembered.top,std::min(candidate.top,raw.proposedBounds.top));
        protection.bottom=std::max(remembered.bottom,std::max(candidate.bottom,raw.proposedBounds.bottom));
        if (upper.present) protection.top=std::max(0,std::min(protection.top,upper.top)-2*yStep);
        if (lower.present) protection.bottom=std::min(source.height,std::max(protection.bottom,lower.bottom)+2*yStep);
        protection.top &= ~1;
        protection.bottom=std::min(source.height,(protection.bottom+1)&~1);
        protection.aspectRatio=static_cast<double>(source.width)/(protection.bottom-protection.top);
        protection.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        result.reason = "unprotected-visible-pixels";
        if (protection.top <= base.top || protection.bottom >= base.bottom) return result;
        const auto outside = EvaluateActivePicturePresentationRetention(source,protection);
        if (!outside.analysisValid || !outside.presentationValid || !outside.excludedBandsPixelSafe ||
            !outside.proposedBoundsContained || outside.globalNearBlack) return result;

        // Correct only the vertical axis proved here. Never erase a failed
        // orthogonal bar or let history alone acquire crop authority.
        // The model restores remembered bounds within its normal measurement
        // tolerance. Bind protection to that same authority; the envelope above
        // contains both the fresh measured picture and the remembered rectangle.
        hypothesis.trustedBounds=remembered;
        hypothesis.axisEvidence.vertical.state=ActivePictureAxisState::TRUSTED_BARS;
        hypothesis.axisEvidence.vertical.reason=ActivePictureAxisReason::BAR_CONFIRMED;
        hypothesis.axisEvidence.vertical.scanComplete=true;
        hypothesis.axisEvidence.vertical.barCandidate=true;
        hypothesis.reason="known inward format with current isolated bar content and protected envelope";
        result.valid=true;
        result.picture=hypothesis;
        result.protectedBounds=protection;
        result.reason="current-caption-protected-known-return";
        return result;
    }
}

