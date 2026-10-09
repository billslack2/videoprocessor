#pragma once
#include <SubtitleCutPaste.h>
#include <vprenderer/AlphaSourceCropPolicy.h>

// Presentation-only proof. The subtitle detector's sampled bar edges are not
// picture authority; the admitted AR rectangle and current remaining pixels are.
inline bool ApplyRelocatedSubtitleRetention(AlphaSourceCrop::Input& crop,
    const AlphaSourceCrop::CropPresentationAdmissionState& admitted,
    const AnalysisLumaSource& source,
    const ActivePicturePresentationRetentionEvidence& raw,
    const SubtitleCutPasteGeometry& move, bool composed, bool currentMeasurement,
    ActivePicturePresentationRetentionEvidence& corrected)
{
    auto same=[](const ActivePictureBounds& a,const ActivePictureBounds& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom &&
            a.rasterWidth==b.rasterWidth && a.rasterHeight==b.rasterHeight && a.trustedBarAxes==b.trustedBarAxes;
    };
    corrected=raw;
    if(!composed || !move.valid || !currentMeasurement || !crop.automaticCropEnabled ||
        !crop.sharedGeometryAvailable || !admitted.available ||
        admitted.sourceGeneration!=crop.frameSourceGeneration ||
        admitted.presentationEpoch!=crop.framePresentationEpoch ||
        crop.geometrySourceGeneration!=crop.frameSourceGeneration ||
        !same(admitted.trustedCrop,crop.geometry) ||
        crop.classification!=ActivePictureClassification::BAR_CROP_TRUSTED ||
        crop.latestObservationClassification==ActivePictureClassification::FULL_RASTER_TRUSTED ||
        crop.movingPictureTransition || AlphaSourceCrop::HasCurrentPictureTransitionHandoff(crop) ||
        crop.fullRasterPresentationAuthoritative || crop.nearBlackEpisodeFullRaster ||
        !move.destination.Valid() || move.destination.left<crop.geometry.left ||
        move.destination.right>crop.geometry.right || move.destination.top<crop.geometry.top ||
        move.destination.bottom>crop.geometry.bottom) return false;
    int glyphHeight=0;
    for(const auto& line:move.glyphLines) if(line.Valid()) glyphHeight=std::max(glyphHeight,line.bottom-line.top);
    const auto& box=move.content;
    const ActivePictureBounds erased={box.left,box.top,box.right,box.bottom,
        source.width,source.height,0.0,ActivePictureBounds::BarAxes::NONE};
    corrected=EvaluateActivePicturePresentationRetentionExcludingRelocatedOverlay(
        source,crop.geometry,raw,erased,glyphHeight);
    if(!corrected.relocatedOverlayExclusionEvaluated || !corrected.excludedBandsPixelSafe) return false;
    crop.frameLocalPresentationRetentionEvaluated=true;
    crop.frameLocalPresentationRetentionSafe=true;
    crop.currentVisibleBoundsAvailable=false;
    crop.outwardPresentationActive=false;
    crop.outwardExpansionAvailable=false;
    crop.presentationFailOpen=false;
    crop.verticalInspectionPending=false;
    crop.verticalTranslationActive=false;
    crop.verticalTranslationConfirmationPending=false;
    crop.verticalFitConfirmationPending=false;
    crop.verticalTranslationBaseRetentionActive=false;
    crop.verticalTranslationEngageBaseRetentionActive=false;
    return true;
}
