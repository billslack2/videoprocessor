#pragma once

#include "ActivePictureTransitionModel.h"
#include "ActivePictureLookaheadMode.h"

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


// Bounded source-order association layer for active-picture decisions. It
// owns no video frames and changes neither queue depth nor dequeue timing.
// Callers submit only observations selected by the existing detector cadence.
class ActivePictureDecisionTimeline
{
public:
	static constexpr uint8_t MAX_LOOKAHEAD_FRAMES = 8;
	static constexpr size_t MAX_RETAINED_IDENTITIES = 32;

	void Reset(uint64_t transportGeneration = 0);
	void ResetAnalysis();
	bool TrackAcceptedFrame(const ActivePictureFrameIdentity& identity);
	void MarkConsumed(const ActivePictureFrameIdentity& identity);
	void MarkDiscarded(const ActivePictureFrameIdentity& identity,
		uint64_t detectorFrameNumber);
	void BreakContinuity(uint64_t detectorFrameNumber);
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

	void RetainIdentity(const ActivePictureFrameIdentity& identity,
		uint64_t detectorFrameNumber);
	bool FindRetainedIdentity(uint64_t detectorFrameNumber,
		ActivePictureFrameIdentity& identity) const;
	bool HasObservedSequence(uint64_t acceptedSequence) const;
	bool HasAcceptedIdentity(const ActivePictureFrameIdentity& identity) const;

	ActivePictureTransitionModel m_transition;
	std::deque<ObservedIdentity> m_observed;
	std::deque<ActivePictureFrameIdentity> m_accepted;
	uint64_t m_transportGeneration = 0;
	uint64_t m_lastAcceptedSequence = 0;
	uint64_t m_lastConsumedSequence = 0;
	bool m_hasAcceptedSequence = false;
	ActivePictureFrameIdentity m_lastAcceptedIdentity;
};
