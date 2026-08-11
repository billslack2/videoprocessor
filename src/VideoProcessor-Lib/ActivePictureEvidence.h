#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "AnalysisLumaSource.h"
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


struct ActivePictureEdgeEvidence
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


struct ActivePictureEvidence
{
	bool available = false;
	ActivePictureClassification classification =
		ActivePictureClassification::UNAVAILABLE;
	ActivePictureBounds proposedBounds;
	ActivePictureBounds trustedBounds;
	ActivePictureEdgeEvidence left;
	ActivePictureEdgeEvidence top;
	ActivePictureEdgeEvidence right;
	ActivePictureEdgeEvidence bottom;
	size_t lumaSamples = 0;
	size_t chromaSamples = 0;
	std::string reason;
};


// Per-frame pixel evidence for retaining an already trusted presentation
// rectangle. This does not grant crop authority and does not apply temporal
// policy. It only answers whether this frame is valid to inspect, whether the
// detector's current proposal remains inside the trusted presentation, and
// whether every pixel band that presentation would exclude still looks safe.
//
// A valid all-black/fade frame is intentionally distinct from an invalid
// analysis source: it has analysisValid=true, globalNearBlack=true, and can be
// currentlyPixelSafe even when no active-picture geometry can be proposed.
struct ActivePicturePresentationRetentionEvidence
{
	bool analysisValid = false;
	bool presentationValid = false;
	double globalLumaP90 = 0.0;
	bool globalNearBlack = false;
	bool proposedBoundsAvailable = false;
	bool proposedBoundsContained = false;
	bool excludedBandsPixelSafe = false;
	bool currentlyPixelSafe = false;
	// When excluded pixels are visibly occupied but remain spatially bounded,
	// this is the smallest measured outward-only presentation envelope. It
	// never grants inward crop authority; callers may only merge it with an
	// already trusted presentation rectangle.
	bool outwardVisibleBoundsAvailable = false;
	ActivePictureBounds outwardVisibleBounds;
	ActivePictureEvidence activePicture;
	ActivePictureEdgeEvidence excludedLeft;
	ActivePictureEdgeEvidence excludedTop;
	ActivePictureEdgeEvidence excludedRight;
	ActivePictureEdgeEvidence excludedBottom;
	size_t lumaSamples = 0;
	size_t chromaSamples = 0;
	std::string reason;
};


// Pure, bounded P010 inspection. It has no renderer, DirectShow, configuration,
// or mutable global dependencies, so identical bytes always produce identical
// evidence. At 4K the fixed grids inspect fewer than 30,000 luma samples.
ActivePictureEvidence ExtractP010ActivePictureEvidence(
	const P010PlaneView& view);

// The active-picture policy is format-neutral. The historical P010 entry
// point remains above for callers with a planar frame; native RGB callers use
// this bounded source sampler and retain source-raster coordinates.
ActivePictureEvidence ExtractActivePictureEvidence(
	const AnalysisLumaSource& source);

// Startup-only recovery for a scope frame whose subtitle/UI contaminates one
// encoded bar before any trusted presentation exists. One clean bar is mirrored
// and the opposite bar must still be predominantly coherent black with a broad
// picture boundary. The ordinary extractor remains conservative/provisional.
ActivePictureEvidence EvaluateSymmetricVerticalBarHypothesis(
	const AnalysisLumaSource& source,
	const ActivePictureEvidence& observed);

// Bounded presentation-retention inspection. The excluded-band predicate uses
// the same black, luma-dispersion, texture, neutral-chroma, and continuity
// limits as bar acquisition, but deliberately does not require inner-boundary
// contrast: an already trusted crop may be retained through a dark fade, while
// visible or colored pixels outside it still fail open.
ActivePicturePresentationRetentionEvidence EvaluateActivePicturePresentationRetention(
	const AnalysisLumaSource& source,
	const ActivePictureBounds& trustedPresentation);

ActivePicturePresentationRetentionEvidence
	EvaluateP010ActivePicturePresentationRetention(
		const P010PlaneView& view,
		const ActivePictureBounds& trustedPresentation);
