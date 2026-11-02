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
    bool rasterContactProved=false; // Independently corroborated current native contact.
};
struct SourcePictureComponent {
    int left=0, top=0, right=0, bottom=0;
    unsigned samples=0;
    bool glyphLike=false;
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
    std::array<std::vector<SourcePictureEdgeCandidate>,4> edges;
    std::vector<SourcePictureComponent> components;
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
