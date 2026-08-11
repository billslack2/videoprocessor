#include <pch.h>

#include <ActivePictureAnalyzer.h>

void ActivePictureAnalyzer::Reset()
{
	m_transition.Reset();
}

ActivePictureAnalyzerResult ActivePictureAnalyzer::Analyze(
	const ActivePictureAnalyzerInput& input)
{
	ActivePictureAnalyzerResult result;
	m_transition.SetStableGeometryDeadbandPercent(
		ActivePictureTransitionModel::GetRuntimeStableGeometryDeadbandPercent());
	if (input.frame.width <= 0 || input.frame.height <= 0 ||
		!m_transition.ShouldAnalyze(input.frameNumber, input.framesPerSecond))
		return result;

	result.analyzed = true;
	if (!input.frame.data)
	{
		result.evidence.reason = "P010 sample pointer is unavailable";
		result.decision = m_transition.Observe(
			{ {}, input.frameNumber, false,
			ActivePictureClassification::UNAVAILABLE, input.framesPerSecond });
		return result;
	}

	result.evidence = ExtractP010ActivePictureEvidence(input.frame);
	ActivePictureObservation observation;
	observation.frameNumber = input.frameNumber;
	observation.available = result.evidence.available;
	observation.framesPerSecond = input.framesPerSecond;
	if (result.evidence.available)
	{
		observation.bounds = result.evidence.classification ==
			ActivePictureClassification::BAR_CROP_TRUSTED ?
			result.evidence.trustedBounds : result.evidence.proposedBounds;
		observation.classification = result.evidence.classification;
	}
	result.decision = m_transition.Observe(observation);
	return result;
}
