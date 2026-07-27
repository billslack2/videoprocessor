#pragma once

#include <libplacebo/AlphaPresentationTelemetry.h>

#include <cstddef>
#include <cstdint>

enum class AlphaCadenceAction
{
	Drop = -1,
	None = 0,
	Repeat = 1
};

struct AlphaCadenceCorrectionInput
{
	bool enabled = false;
	uint64_t generation = 0;
	AlphaPresentationEvidence presentationEvidence =
		AlphaPresentationEvidence::Unavailable;
	double captureRateHz = 0.0;
	double displayRateHz = 0.0;
	size_t queueDepth = 0;
	size_t desiredQueueDepth = 1;
	double oldestQueuedAgeMs = 0.0;
	uint64_t presentationDebt = 0;
	uint32_t lastPresentId = 0;
	bool safeSceneBoundary = false;
	uint64_t sceneEventId = 0;
	uint64_t sourceSequence = 0;
};

struct AlphaCadenceCorrectionDecision
{
	AlphaCadenceAction action = AlphaCadenceAction::None;
	AlphaCadenceAction predictedAction = AlphaCadenceAction::None;
	bool ratesCompatible = false;
	bool predictionValid = false;
	bool planned = false;
	bool deadlineFallback = false;
	bool verificationPending = false;
	bool verificationCompleted = false;
	bool lastVerificationValid = false;
	bool lastVerificationSucceeded = false;
	double phaseFrames = 0.0;
	double secondsUntilCorrection = 0.0;
	double secondsUntilPlan = 0.0;
	uint64_t sceneEventId = 0;
};

class AlphaCadenceCorrectionPolicy
{
public:
	AlphaCadenceCorrectionDecision Evaluate(
		const AlphaCadenceCorrectionInput& input);
	void Reset(uint64_t generation = 0);
	void CancelPendingAction();

private:
	static constexpr double MAXIMUM_RATE_MISMATCH_PPM = 200.0;
	static constexpr double PLAN_PHASE_FRAMES = 0.75;
	static constexpr double ACTION_PHASE_FRAMES = 1.0;
	static constexpr double MAXIMUM_QUEUE_AGE_MS = 250.0;
	static constexpr uint32_t MINIMUM_STABLE_SAMPLES = 120;
	static constexpr uint32_t MINIMUM_PREDICTION_SAMPLES = 600;
	static constexpr uint32_t COOLDOWN_FRAMES = 120;
	static constexpr double RATE_FILTER_ALPHA = 1.0 / 600.0;
	static constexpr double MINIMUM_PREDICTION_PPM = 1.0;
	static constexpr double DIRECTION_REVERSAL_PPM = 2.0;

	uint64_t m_generation = 0;
	double m_phaseFrames = 0.0;
	uint32_t m_stableSamples = 0;
	uint32_t m_rateFilterSamples = 0;
	uint32_t m_plannedFrames = 0;
	uint32_t m_cooldownFrames = 0;
	uint64_t m_lastSceneEventId = 0;
	bool m_verificationPending = false;
	AlphaCadenceAction m_verificationAction = AlphaCadenceAction::None;
	uint64_t m_verificationDebt = 0;
	uint32_t m_verificationPresentId = 0;
	bool m_lastVerificationValid = false;
	bool m_lastVerificationSucceeded = false;
	double m_filteredPhasePerFrame = 0.0;
	AlphaCadenceAction m_predictionDirection = AlphaCadenceAction::None;
};
