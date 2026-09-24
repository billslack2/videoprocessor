#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "AnalysisLumaSource.h"
#include "ActivePictureTransitionModel.h"


// Shared presentation-only interpretation of the current provisional retention
// proof. Source identity and exact measurement-base checks remain mandatory.
bool CanRetainProvisionalSamplingCrop(const ActivePictureBounds& trusted,
	const ActivePictureBounds& observed, ActivePictureClassification classification,
	bool currentPresentationRetainable);

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


// Diagnostics from the existing side-probe samples. These measurements are
// intentionally excluded from observation identity and crop admission policy.
struct ActivePictureSideProbeCell
{
	int strong = 0;
	int nonBlack = 0;
	int meanLuma = 0;
	int peakLuma = 0;
};
struct ActivePictureSideProbe
{
	bool evaluated = false;
	std::array<ActivePictureSideProbeCell, 12> cells{}; // depth-major, then top-to-bottom zone
};

struct ActivePictureEvidence
{
	bool available = false;
	ActivePictureClassification classification =
		ActivePictureClassification::UNAVAILABLE;
	ActivePictureBounds proposedBounds;
	ActivePictureBounds trustedBounds;
	ActivePictureAxisEvidenceSet axisEvidence;
	ActivePictureEdgeEvidence left;
	ActivePictureEdgeEvidence top;
	ActivePictureEdgeEvidence right;
	ActivePictureEdgeEvidence bottom;
	ActivePictureSideProbe leftSideProbe, rightSideProbe;
	size_t lumaSamples = 0;
	size_t chromaSamples = 0;
	std::string reason;
};


// Shared conversion keeps live and queued observations tied to the same measurement.
ActivePictureObservation MakeActivePictureObservation(const ActivePictureEvidence& evidence,
	uint64_t frameNumber, double framesPerSecond);

// Frame-global darkness is presentation-independent. In particular, startup
// title cards must be classifiable before any trusted crop exists; "false" and
// "not evaluated" are deliberately separate states.
struct ActivePictureGlobalNearBlackEvidence
{
	bool evaluated = false;
	double lumaP90 = 0.0;
	bool nearBlack = false;
	size_t lumaSamples = 0;
};


// Per-frame pixel evidence for retaining an already trusted presentation
// rectangle. This does not grant crop authority and does not apply temporal
// policy. It only answers whether this frame is valid to inspect, whether the
// detector's current proposal remains inside the trusted presentation, and
// whether excluded bands look safe. CanRetainPresentation additionally allows
// the explicitly bounded sampling-edge tolerance; it does not call edge pixels black.
//
// A valid all-black/fade frame is intentionally distinct from an invalid
// analysis source: it has analysisValid=true, globalNearBlack=true, and can be
// currentlyPixelSafe even when no active-picture geometry can be proposed.
// A bright contained logo can also lack credible acquisition geometry. Fresh
// safe-band evidence may retain the existing rectangle without a global-darkness
// requirement; this never creates authority for a new crop.
struct ActivePicturePresentationRetentionEvidence
{
	bool analysisValid = false;
	bool presentationValid = false;
	double globalLumaP90 = 0.0;
	bool globalNearBlack = false;
	bool proposedBoundsAvailable = false;
	bool proposedBoundsContained = false;
	// Retention-only proof for one provisional vertical scan step; no new authority.
	bool samplingReaffirmed = false;
	// Geometry tolerance is distinct from pixel blackness. A one-step
	// provisional border can remain in the same established presentation even
	// when that narrow strip contains real edge pixels. This cannot move the
	// reference rectangle or establish new crop authority.
	bool samplingEquivalent = false;
	int samplingStripPeakY = 0;
	int samplingStripPeakChromaDelta = 0;
	bool samplingStripConflict = false;
	bool excludedBandsPixelSafe = false;
	bool excludedHorizontalBandsPixelSafe = false;
	bool excludedVerticalBandsPixelSafe = false;
	// Current evidence restricted to newly exposed strips. Whole old bars can
	// remain mostly black during a real, gradual format expansion.
	bool expansionStripsAvailable = false;
	ActivePictureBounds expansionBase;
	ActivePictureBounds expansionCandidate;
	ActivePictureEdgeEvidence expandingLeft, expandingTop, expandingRight, expandingBottom;
	bool currentlyPixelSafe = false;
	bool CanRetainPresentation() const
	{
		return analysisValid && presentationValid &&
			(currentlyPixelSafe || samplingEquivalent);
	}
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

ActivePictureGlobalNearBlackEvidence EvaluateActivePictureGlobalNearBlack(
	const AnalysisLumaSource& source);

ActivePictureGlobalNearBlackEvidence EvaluateP010ActivePictureGlobalNearBlack(
	const P010PlaneView& view);

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

// Resolve a frame-local measurement after the transition model chooses the
// presentation base. The returned bounds identify the actual inspected crop.
struct ActivePictureRetentionHandoff
{
	ActivePicturePresentationRetentionEvidence evidence;
	ActivePictureBounds bounds;
	bool refreshed = false;
};
ActivePictureRetentionHandoff ResolveActivePictureRetentionHandoff(
	const AnalysisLumaSource& source, const ActivePictureBounds& measuredBounds,
	const ActivePicturePresentationRetentionEvidence& measured,
	const ActivePictureBounds& presentationBounds);

// Sparse program content on an otherwise near-black frame (for example a
// scrolling title crawl) can resemble a new pair of encoded bars. Such a
// frame may describe visible pixels, but it cannot replace a different
// already-trusted presentation rectangle.
ActivePictureEvidence ConstrainNearBlackGeometryChange(
	const ActivePicturePresentationRetentionEvidence& retention,
	const ActivePictureBounds& trustedPresentation);

// While a sparse near-black title episode is active, a plausible pair of bars
// may be the moving title bounds rather than picture geometry. Full-raster
// authority remains safe, but new bar-crop authority is provisional until the
// episode ends.
ActivePictureEvidence ConstrainNearBlackCropAcquisition(
	const ActivePictureEvidence& evidence,
	bool nearBlackEpisodeActive);

// Opt-in telemetry only: raw analysis-domain samples, independent of crop
// acquisition and never consumed by geometry policy. Both raster endpoints are
// included; coordinates are index * (extent - 1) / (count - 1).
struct ActivePictureDiagnosticGrid
{
	int columns = 0;
	int rows = 0;
	std::vector<AnalysisLumaSample> samples;
};
ActivePictureDiagnosticGrid SampleActivePictureDiagnosticGrid(
	const AnalysisLumaSource& source);

// Experimental shadow evidence for an already known full raster. This
// supplies no crop bounds or authority. Even a positive candidate can also
// describe indistinguishable tinted/noisy bars; callers MUST NOT use it for
// retention, acquisition, or other presentation-policy changes.
// Small chroma offsets are useful only with distributed detail and matching
// adjacent interior. They are not a certificate that tinted padding is picture.
struct FullRasterColorEdgeEvidence
{
    double medianY = 0, medianU = 0, medianV = 0;
    double dispersionY = 0, dispersionU = 0, dispersionV = 0;
    double interiorMedianY = 0, interiorMedianU = 0, interiorMedianV = 0;
    double maxBackgroundDeltaY = 0, maxBackgroundDeltaUV = 0;
    int supportedCells = 0;
    bool candidateSupported = false;
};
struct FullRasterColorEvidence
{
    bool evaluated = false;
    bool candidateSupported = false;
    bool precisionSupported = false;
    size_t sampleCount = 0;
    // Source-coordinate order: left, top, right, bottom.
    FullRasterColorEdgeEvidence edges[4];
    std::string reason;
};
FullRasterColorEvidence EvaluateFullRasterColorEvidence(
    const AnalysisLumaSource& source);
