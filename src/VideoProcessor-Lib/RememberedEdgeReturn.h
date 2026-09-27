#pragma once
#include "ActivePictureEvidence.h"
#include "ActivePictureDecisionTimeline.h"

// Startup runtime policy; explicit off remains available for rollback.
enum class RememberedEdgeReturnMode { OFF, GUARDED, EXPERIMENTAL, SHADOW };
RememberedEdgeReturnMode ResolveRememberedEdgeReturnMode(const char* option);

// Pure current-frame inspection. Qualified history nominates; the actual bands
// must still match the independently measured clean bar. Never learns history.
struct RememberedEdgeReturnMetrics
{
    const char* reason = "ineligible-context";
    size_t samples = 0, exteriorSamples = 0;
    int mismatches = 0, matchedEdgeSupport = 0;
    int boundaryMismatchSamples = 0, deepMismatchSamples = 0;
    std::array<int, 4> matchedEdgeZones{};
    int edgeFringeSamples = 0, firstFringeY = -1, lastFringeY = -1;
    int lastMismatchY = -1;
    int firstMismatchX = -1, firstMismatchY = -1, maxLumaDelta = 0, maxChromaDelta = 0;
    double referenceY = 0, referenceU = 0, referenceV = 0, referenceDispersion = 0;
};
struct RememberedEdgeReturnResult : RememberedEdgeReturnMetrics
{
    bool candidateAvailable = false;
    ActivePictureEvidence evidence;
};
// Deliberately contains no ActivePictureEvidence or publication proof.
struct RememberedEdgeReturnShadowResult : RememberedEdgeReturnMetrics
{
    bool wouldVerify = false;
    uint64_t sourceGeneration = 0, sourceSequence = 0, timestampMs = 0;
    ActivePictureBounds target, presentation;
    int retainedTopRows = 0, retainedBottomRows = 0;
    RememberedEdge matchedEdge = RememberedEdge::TOP;
    int oppositeLumaSupport = 0, oppositeChromaSupport = 0;
    std::array<int, 4> oppositeLumaZones{}, oppositeChromaZones{};
    int oppositeMaxLumaDelta = 0, oppositeMaxChromaDelta = 0;
};
RememberedEdgeReturnShadowResult InspectRememberedEdgeReturnShadow(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs);
// Separate diagnostic envelope: retain two rows at each nominal edge, rounded
// outward to even source coordinates (three rows when the nominal edge is odd).
RememberedEdgeReturnShadowResult InspectRememberedEdgeReturnGuardedShadow(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs);
RememberedEdgeReturnResult InspectRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs);

// Source views retain their originating generation domain. Inspection validates
// that domain before normalizing a local copy; it never changes the caller's view.
enum class RememberedShadowSourceGeneration { TRANSPORT, FORMAT };
struct RememberedEdgeReturnShadowSample
{
    AnalysisLumaSource source;
    ActivePictureEvidence raw;
    ActivePictureFrameIdentity identity;
    uint64_t policyGeneration = 0, timestampMs = 0;
    bool available = false, cadenceRepeat = false, discontinuity = false;
    RememberedShadowSourceGeneration sourceGenerationDomain = RememberedShadowSourceGeneration::TRANSPORT;
};
struct RememberedEdgeReturnShadowWindowResult
{
    bool allPass = false;
    const char* reason = "ineligible-window";
    uint32_t effectiveFuture = 0;
    size_t expectedFrames = 0, inspectedFrames = 0, passedFrames = 0, totalSamples = 0;
    int failedIndex = -1;
    ActivePictureFrameIdentity firstIdentity, lastIdentity, failureIdentity;
    ActivePictureBounds target, presentation;
    int retainedTopRows = 0, retainedBottomRows = 0;
    RememberedEdgeReturnShadowResult failureInspection;
};
// A single immutable nominee and one bounded current+future window. No history
// training, publication proof, or state is returned or retained across calls.
RememberedEdgeReturnShadowWindowResult InspectRememberedEdgeReturnShadowWindow(
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture);

RememberedEdgeReturnShadowWindowResult InspectRememberedEdgeReturnGuardedShadowWindow(
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture);

// Explicit opt-in authority. Nominal history geometry remains distinct from the
// slightly larger verified presentation envelope. A certificate is current-frame
// scoped and must be revalidated against the live model and queue before adoption.
struct GuardedRememberedEdgeReturnCertificate
{
    bool available = false;
    const char* reason = "ineligible-guarded-certificate";
    RememberedEdgeReturnNomination nomination;
    ActivePictureEvidence currentEvidence;
    ActivePictureTransitionDecision decision;
    std::array<ActivePictureFrameIdentity, ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES + 1> identities{};
    std::array<ActivePictureObservation, ActivePictureDecisionTimeline::MAX_LOOKAHEAD_FRAMES + 1> observations{};
    size_t frameCount = 0;
    uint32_t configuredFuture = 0, actualFuture = 0;
    uint64_t continuityGeneration = 0, policyGeneration = 0, createdTickMs = 0;
    RememberedEdgeReturnShadowWindowResult diagnostic;
};
RememberedEdgeReturnResult InspectGuardedRememberedEdgeReturn(
    const AnalysisLumaSource& source, const ActivePictureEvidence& raw,
    const RememberedEdgeReturnNomination& nomination,
    uint64_t sourceSequence, uint64_t tickMs);
GuardedRememberedEdgeReturnCertificate BuildGuardedRememberedEdgeReturnCertificate(
    const ActivePictureTransitionModel& model,
    const RememberedEdgeReturnNomination& nomination,
    const RememberedEdgeReturnShadowSample* samples, size_t sampleCount,
    uint32_t configuredFuture, uint32_t actualFuture, uint64_t continuityGeneration);
bool ValidateAndAdoptGuardedRememberedEdgeReturn(
    ActivePictureTransitionModel& model,
    const GuardedRememberedEdgeReturnCertificate& certificate,
    const AnalysisLumaSource& currentSource, const ActivePictureEvidence& currentRaw,
    const ActivePictureFrameIdentity& currentIdentity,
    const RememberedEdgeReturnContext& currentContext, uint64_t continuityGeneration,
    ActivePictureTransitionDecision* outDecision = nullptr,
    ActivePictureEvidence* outEvidence = nullptr);
