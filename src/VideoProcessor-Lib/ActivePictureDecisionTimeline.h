#pragma once

#include "ActivePictureTransitionModel.h"

#include <cstddef>
#include <cstdint>
#include <deque>


struct ActivePictureFrameIdentity
{
	uint64_t transportGeneration = 0;
	uint64_t acceptedSequence = 0;
	uint64_t sourceFrameNumber = 0;
	uint64_t captureTimestamp = 0;
	uint64_t sourceFormatGeneration = 0;
	uint64_t viewportGeneration = 0;
	uint64_t rendererGeneration = 0;
};

bool SameActivePictureFrameIdentity(
	const ActivePictureFrameIdentity& left,
	const ActivePictureFrameIdentity& right);


enum class ActivePictureScheduledDecisionValidation
{
	ACCEPTED,
	NON_AUTHORITATIVE,
	EFFECTIVE_IDENTITY_MISMATCH,
	OBSERVATION_CONTEXT_MISMATCH,
	TRUSTED_BOUNDS_MISMATCH
};


enum class ActivePictureNlsIntentMode
{
	WAITING,
	NATIVE,
	ACTIVE,
	LINEAR_PASSTHROUGH,
	SAFE_FIT
};


enum class ActivePictureDecisionAssociation
{
	CONFIRMATION,
	OUTWARD,
	EXACT_INWARD
};


enum class ActivePictureInwardProofValidation
{
	NOT_APPLICABLE,
	ACCEPTED,
	CANDIDATE_UNAVAILABLE,
	CANDIDATE_CONSUMED,
	CONTINUITY_MISMATCH,
	CONTEXT_MISMATCH,
	INSUFFICIENT_LEAD,
	MISSING_IDENTITY,
	MISSING_EVIDENCE,
	EVIDENCE_NOT_TRUSTED,
	EVIDENCE_NEAR_BLACK,
	EVIDENCE_BOUNDS_MISMATCH
};


struct ActivePicturePresentationIntent
{
	double targetAspect = 0.0;
	uint64_t viewportGeneration = 0;
	ActivePictureNlsIntentMode nlsMode =
		ActivePictureNlsIntentMode::WAITING;
	bool horizontalStretch = false;
	double stretchFactor = 1.0;
};


struct ActivePictureFrameDecision
{
	ActivePictureFrameIdentity observationIdentity;
	ActivePictureFrameIdentity effectiveIdentity;
	ActivePictureTransitionDecision transition;
	ActivePicturePresentationIntent presentation;
	uint8_t configuredLookahead = 0;
	uint8_t availableLookahead = 0;
	uint8_t effectiveLookahead = 0;
	uint8_t proofFrameCount = 0;
	uint64_t continuityGeneration = 0;
	uint64_t lookaheadPolicyGeneration = 0;
	ActivePictureDecisionAssociation association =
		ActivePictureDecisionAssociation::CONFIRMATION;
	ActivePictureInwardProofValidation inwardProof =
		ActivePictureInwardProofValidation::NOT_APPLICABLE;
	bool late = false;
};

ActivePictureScheduledDecisionValidation
ValidateActivePictureScheduledDecision(
	const ActivePictureFrameDecision& decision,
	const ActivePictureFrameIdentity& currentIdentity,
	const ActivePictureBounds& currentTrustedBounds,
	ActivePictureClassification currentClassification);
const char* ActivePictureScheduledDecisionValidationName(
	ActivePictureScheduledDecisionValidation validation);
const char* ActivePictureDecisionAssociationName(
	ActivePictureDecisionAssociation association);
const char* ActivePictureInwardProofValidationName(
	ActivePictureInwardProofValidation validation);
bool IsExactInwardActivePictureAssociationGeometry(
	const ActivePictureTransitionDecision& transition,
	ActivePictureClassification classification);


// Bounded source-order association layer for active-picture decisions. It
// owns no video frames and changes neither queue depth nor dequeue timing.
// Callers submit only observations selected by the existing detector cadence.
class ActivePictureDecisionTimeline
{
public:
	static constexpr uint8_t MAX_LOOKAHEAD_FRAMES = 8;
	static constexpr size_t MAX_RETAINED_IDENTITIES = 32;

	void Reset(uint64_t transportGeneration = 0);
	bool TrackAcceptedFrame(const ActivePictureFrameIdentity& identity);
	void MarkConsumed(const ActivePictureFrameIdentity& identity);
	void MarkDiscarded(const ActivePictureFrameIdentity& identity,
		uint64_t detectorFrameNumber);
	void BreakContinuity(uint64_t detectorFrameNumber);
	void InvalidateLookaheadPolicy(bool resetPartialEvidence = false);
	bool TrackLookaheadEvidence(
		const ActivePictureFrameIdentity& identity,
		const ActivePictureObservation& observation,
		bool nearBlackEvaluated,
		bool nearBlack);
	bool IsDecisionCurrent(
		const ActivePictureFrameDecision& decision) const;
	uint64_t LookaheadPolicyGeneration() const
	{
		return m_lookaheadPolicyGeneration;
	}
	bool ShouldAnalyze(uint64_t frameNumber, double framesPerSecond)
	{
		return m_transition.ShouldAnalyze(frameNumber, framesPerSecond);
	}
	bool SubmitScheduledObservation(
		const ActivePictureFrameIdentity& identity,
		const ActivePictureObservation& observation,
		uint8_t configuredLookahead,
		uint8_t availableLookahead,
		ActivePictureFrameDecision& published,
		const ActivePicturePresentationIntent& presentation = {});

	uint64_t TransportGeneration() const { return m_transportGeneration; }
	uint64_t LastConsumedSequence() const { return m_lastConsumedSequence; }

private:
	struct ObservedIdentity
	{
		ActivePictureFrameIdentity identity;
		uint64_t detectorFrameNumber = 0;
	};
	struct LookaheadEvidence
	{
		ActivePictureFrameIdentity identity;
		ActivePictureObservation observation;
		bool nearBlackEvaluated = false;
		bool nearBlack = false;
	};
	struct AcceptedIdentity
	{
		ActivePictureFrameIdentity identity;
		uint64_t continuityGeneration = 0;
	};
	struct ContinuityBoundary
	{
		uint64_t beforeGeneration = 0;
		uint64_t afterGeneration = 0;
		uint64_t acceptedSequence = 0;
	};

	void RetainIdentity(const ActivePictureFrameIdentity& identity,
		uint64_t detectorFrameNumber);
	bool FindRetainedIdentity(uint64_t detectorFrameNumber,
		ActivePictureFrameIdentity& identity) const;
	bool HasObservedSequence(uint64_t acceptedSequence) const;
	bool FindAcceptedIdentity(const ActivePictureFrameIdentity& identity,
		AcceptedIdentity& accepted) const;
	bool FindLookaheadEvidence(
		const ActivePictureFrameIdentity& identity,
		LookaheadEvidence& evidence) const;
	ActivePictureInwardProofValidation ValidateExactInwardProof(
		const ActivePictureFrameIdentity& candidate,
		const ActivePictureFrameIdentity& confirmation,
		const ActivePictureBounds& targetBounds,
		uint64_t requiredLead,
		uint8_t effectiveLookahead,
		uint8_t& proofFrameCount) const;
	void BreakContinuityAtSequence(uint64_t acceptedSequence);
	void RecordContinuityBoundary(uint64_t acceptedSequence);
	void AdvanceContinuityGeneration();

	ActivePictureTransitionModel m_transition;
	std::deque<ObservedIdentity> m_observed;
	std::deque<LookaheadEvidence> m_lookaheadEvidence;
	std::deque<AcceptedIdentity> m_accepted;
	std::deque<ContinuityBoundary> m_continuityBoundaries;
	uint64_t m_transportGeneration = 0;
	uint64_t m_lastAcceptedSequence = 0;
	uint64_t m_lastConsumedSequence = 0;
	bool m_hasAcceptedSequence = false;
	ActivePictureFrameIdentity m_lastAcceptedIdentity;
	uint64_t m_continuityGeneration = 1;
	uint64_t m_lookaheadPolicyGeneration = 1;
};
