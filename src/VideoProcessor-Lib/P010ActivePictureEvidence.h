#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ActivePictureTransitionModel.h"


struct P010PlaneView
{
	const uint8_t* data = nullptr;
	size_t dataBytes = 0;
	int width = 0;
	int height = 0;
	size_t lumaPitchBytes = 0;
	size_t chromaPitchBytes = 0;
};


struct P010EdgeEvidence
{
	int barPixels = 0;
	double blackFraction = 0.0;
	double lumaFloor = 0.0;
	double lumaP90 = 0.0;
	double lumaDispersion = 0.0;
	double texture = 0.0;
	double neutralChromaFraction = 0.0;
	double innerBoundaryContrast = 0.0;
	double continuity = 0.0;
	double confidence = 0.0;
	bool trusted = false;
};


struct P010ActivePictureEvidence
{
	bool available = false;
	ActivePictureClassification classification =
		ActivePictureClassification::UNAVAILABLE;
	ActivePictureBounds proposedBounds;
	ActivePictureBounds trustedBounds;
	P010EdgeEvidence left;
	P010EdgeEvidence top;
	P010EdgeEvidence right;
	P010EdgeEvidence bottom;
	size_t lumaSamples = 0;
	size_t chromaSamples = 0;
	std::string reason;
};


// Pure, bounded P010 inspection. It has no renderer, DirectShow, configuration,
// or mutable global dependencies, so identical bytes always produce identical
// evidence. At 4K the fixed grids inspect fewer than 30,000 luma samples.
P010ActivePictureEvidence ExtractP010ActivePictureEvidence(
	const P010PlaneView& view);
