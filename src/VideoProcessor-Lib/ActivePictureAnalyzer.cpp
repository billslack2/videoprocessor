#include <pch.h>

#include <ActivePictureAnalyzer.h>
#include <SourcePictureDetector.h>

void ActivePictureAnalyzer::Reset()
{
	m_transition.Reset();
	m_sourceGeneration = 0;
}

ActivePictureAnalyzerResult ActivePictureAnalyzer::Analyze(
	const ActivePictureAnalyzerInput& input)
{
	ActivePictureAnalyzerResult result;
	if (m_sourceGeneration != input.sourceGeneration) {
		Reset();
		m_sourceGeneration = input.sourceGeneration;
	}
	m_transition.SetStableGeometryDeadbandPercent(
		ActivePictureTransitionModel::GetRuntimeStableGeometryDeadbandPercent());
	if (input.frame.width <= 0 || input.frame.height <= 0 ||
		!m_transition.ShouldAnalyze(input.frameNumber, input.framesPerSecond))
		return result;

	result.analyzed = true;
    AnalysisLumaSource source{input.frame.data,input.frame.dataBytes,input.frame.width,input.frame.height,
        input.frame.lumaPitchBytes,input.frame.chromaPitchBytes,AnalysisLumaFormat::P010,
        VideoFrameEncoding::UNKNOWN,ColorSpace::UNKNOWN,input.sourceGeneration};

	if (!input.frame.data)
	{
        result.evidence=MissingSourcePictureEvidence(source,"P010 sample pointer is unavailable");
        result.decision=m_transition.Observe(MakeActivePictureObservation(
            result.evidence,input.frameNumber,input.framesPerSecond));
		return result;
	}

    ActivePictureFrameIdentity identity;
    identity.transportGeneration=identity.sourceFormatGeneration=input.sourceGeneration;
    identity.acceptedSequence=identity.sourceFrameNumber=input.frameNumber;
    // This adapter always consumes converted P010, before subtitle relocation.
    identity.analysisDomainKey=1;
    // The conversion worker owns the live sample during this synchronous call.
    // Do not retain it in the immutable cache: relocation may mutate it afterward.
    result.evidence=DetectPreparedSourcePictureBounded(source,identity).evidence;
    result.evidence.currentSourceIdentity=identity;
    result.evidence.currentSourceIdentityAvailable=true;
    result.decision=m_transition.Observe(MakeActivePictureObservation(
        result.evidence,input.frameNumber,input.framesPerSecond,&identity));
	return result;
}
