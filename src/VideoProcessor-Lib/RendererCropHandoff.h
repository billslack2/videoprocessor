#pragma once

#include <ActivePictureEvidence.h>

// A short-lived hint explicitly transferred by a display-only host transition.
// This contains source geometry only: no subtitle translation, output rectangle,
// temporal candidates, or authority to skip inspection of the new frame.
struct RendererCropHandoff
{
	ActivePictureBounds bounds;
	uint64_t sourceFormatKey = 0;
	uint64_t viewportGeneration = 0;
	uint64_t transportGeneration = 0;
	uint64_t verifiedTick = 0;
};

inline bool CanRestoreRendererCrop(const RendererCropHandoff& hint,
	const AnalysisLumaSource& source, uint64_t sourceFormatKey,
	uint64_t viewportGeneration, uint64_t now,
	ActivePicturePresentationRetentionEvidence& retention)
{
	retention = {};
	if (!hint.verifiedTick || now < hint.verifiedTick ||
		now - hint.verifiedTick > 10000 ||
		!hint.sourceFormatKey || hint.sourceFormatKey != sourceFormatKey ||
		hint.viewportGeneration != viewportGeneration ||
		hint.transportGeneration != source.generation ||
		hint.bounds.trustedBarAxes == ActivePictureBounds::BarAxes::NONE)
		return false;
	retention = EvaluateActivePicturePresentationRetention(source, hint.bounds);
	return retention.analysisValid && retention.presentationValid &&
		retention.currentlyPixelSafe && !retention.outwardVisibleBoundsAvailable;
}
