#pragma once

#include <cstdint>
#include <string>

#include "ActivePictureTransitionModel.h"


namespace AlphaSourceCrop
{
	struct Input
	{
		bool automaticCropEnabled = false;
		bool sharedGeometryAvailable = false;
		bool latestObservationSupportsCrop = false;
		bool sceneVerificationHoldActive = false;
		bool latestObservationIsProvisional = false;
		bool subtitleDisplacementActive = false;
		ActivePictureClassification classification =
			ActivePictureClassification::UNAVAILABLE;
		ActivePictureBounds geometry;
		uint64_t geometrySourceGeneration = 0;
		uint64_t frameSourceGeneration = 0;
		int rasterWidth = 0;
		int rasterHeight = 0;
	};

	struct Decision
	{
		ActivePictureBounds sourceBounds;
		bool applyCrop = false;
		std::string reason;
	};

	// Pure crop-authority boundary for Alpha. Observers and consumers may propose
	// geometry, but only a generation-current shared BAR_CROP_TRUSTED snapshot
	// reaffirmed by the latest symmetric observation can contract the source
	// rectangle. The user-visible Off state always returns the complete raster.
	Decision Evaluate(const Input& input);
}
