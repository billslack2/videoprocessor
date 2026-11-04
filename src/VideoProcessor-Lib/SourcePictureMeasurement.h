#pragma once
#include <ActivePictureEvidence.h>
#include <array>
#include <vector>

// Source facts only. No crop, caption lease, temporal vote or presentation authority.
enum class SourcePictureOutcome { NotMeasured, Partial, Measured, CanvasEstimate };
enum class SourcePictureEdge : unsigned { Left, Top, Right, Bottom };
struct SourcePictureEdgeCandidate {
    int coordinate=0, minimum=0, maximum=0;
    unsigned support=0, coverage=0;
    std::array<unsigned,4> regionSupport{};
    // Exact supporting native ray positions, indexed over 128 fixed probes.
    // Merging retains their union together with the coordinate uncertainty.
    std::array<uint64_t,2> supportingRays{};
    bool strong=false, contradicted=false, overlayLike=false;
    bool perpendicularInsetProved=false; // Local native edge corroborated by both perpendicular contacts.
    bool rasterContactProved=false; // Independently corroborated current native contact.
};
struct SourcePictureComponent {
    int left=0, top=0, right=0, bottom=0;
    unsigned samples=0;
    bool glyphLike=false;
};
// Current measured ray profile only; no temporal or crop permission.
struct SourcePictureTransitionProfile {
    unsigned side=0,ray=0;
    int minimum=0,maximum=0,transitionMinimum=0,transitionMaximum=0;
    int readMinimum=0,readMaximum=0;
    bool filtered=false;
};
struct SourcePictureObservation {
    SourcePictureOutcome outcome=SourcePictureOutcome::NotMeasured;
    uint64_t sourceGeneration=0;
    int width=0,height=0;
    bool complete=false, rectangleAvailable=false;
    // CanvasEstimate is a structural inference, not a precisely measured blank margin.
    bool canvasSupported=false, blankMarginsUnresolved=false;
    // Independent sampled source reference survives inconclusive geometry.
    ActivePictureBlackReference blackReference;
    ActivePictureBounds measuredBounds;
    // Optional outward current-source sampling guard, distinct from the
    // measured physical transition center. Never derive this from retained AR.
    bool verificationBoundsAvailable=false;
    ActivePictureBounds verificationBounds;
    std::array<std::vector<SourcePictureEdgeCandidate>,4> edges;
    std::vector<SourcePictureComponent> components;
    // Bounded current sample windows for selected boundaries. Overflow disables
    // optional temporal equivalence; it does not change current measurement.
    bool transitionProfilesComplete=true;
    std::vector<SourcePictureTransitionProfile> transitionProfiles;
    unsigned filteredEdgeMask=0;
    std::array<int,4> edgeMinimum{},edgeMaximum{},transitionMinimum{},transitionMaximum{};
    // Sparse topology is not an absence certificate or permission to crop.
    int overviewStepX=0, overviewStepY=0;
    bool denseCoverage=false;
    // Bounded sampled-cell membership. Zero is sampled background; nonzero
    // identifies components[index-1]. 0xffff is occupied but unclassified
    // after the component cap. It never describes unsampled pixels.
    int overviewWidth=0, overviewHeight=0;
    bool topologyComplete=false;
    std::vector<uint16_t> overviewLabels;
    struct DarkTerminalProof {
        bool evaluated=false,accepted=false;
        int side=-1;
        double earlyMean=0,earlyScore=0,integratedMean=0,integratedScore=0,lateMean=0;
        std::array<double,4> regionalMean{};
        unsigned supportingRays=0,supportingRegions=0;
    } darkTerminal;
    struct RasterContactProof {
        bool opposed=false,sparseEvaluated=false,sparseAccepted=false,insetVeto=false,glyphVeto=false;
        int sparseSide=-1;
        unsigned contactPatches=0,textureRegions=0;
        double peakContrast=0;
        std::array<unsigned,4> textureSupport{};
    } rasterContact;
    struct RelativeMatteContactProof {
        bool evaluated=false,accepted=false,referencesMatched=false;
        unsigned coverage=0,support=0,regions=0;
        std::array<double,3> reference{},noise{},texture{};
        double lumaMargin=0,chromaMargin=0;
    };
    std::array<RelativeMatteContactProof,4> relativeMatteContacts;
    struct PerpendicularInsetProof {
        bool evaluated=false,accepted=false,ambiguous=false;
        int side=-1,coordinate=0,minimum=0,maximum=0,probeSpacing=0;
        unsigned support=0,regions=0,eligibleCandidates=0;
        std::array<int,2> nearest{{-1,-1}};
        std::array<unsigned,2> outside{};
    } perpendicularInset;
    unsigned lumaQuantum=1; // Observed perimeter code spacing, capped at one 8-bit-equivalent step.
    size_t samples=0;
    const char* reason="not-measured";
};
struct SourcePictureCandidateAssessment {
    bool evaluated=false, contradicted=false, canvasScaffoldSupported=false;
    unsigned exteriorComponents=0, exteriorRegions=0;
    const char* reason="not-evaluated";
};
SourcePictureObservation MeasureSourcePicture(const AnalysisLumaSource& source,
    const AnalysisEvaluationBudget* budget=nullptr);
SourcePictureCandidateAssessment ReconcileSourcePictureCandidate(
    const SourcePictureObservation& observation,const ActivePictureBounds& candidate,
    const AnalysisEvaluationBudget* budget=nullptr);
