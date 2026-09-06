#pragma once

#include <VideoState.h>


enum class CaptureVideoStateChangeClass
{
	Initial,
	Duplicate,
	StaticHdrMetadataOnly,
	MaterialSignal,
	Invalid
};


inline const char* ToString(CaptureVideoStateChangeClass changeClass) noexcept
{
	switch (changeClass)
	{
	case CaptureVideoStateChangeClass::Initial:
		return "initial";
	case CaptureVideoStateChangeClass::Duplicate:
		return "duplicate";
	case CaptureVideoStateChangeClass::StaticHdrMetadataOnly:
		return "metadata-only";
	case CaptureVideoStateChangeClass::MaterialSignal:
		return "material";
	case CaptureVideoStateChangeClass::Invalid:
		return "invalid";
	}
	return "unknown";
}


inline bool HasSameMaterialCaptureContract(
	const VideoState& previous,
	const VideoState& current) noexcept
{
	if (!previous.valid || !current.valid ||
		static_cast<bool>(previous.displayMode) !=
			static_cast<bool>(current.displayMode))
	{
		return false;
	}
	if (previous.displayMode &&
		*previous.displayMode != *current.displayMode)
	{
		return false;
	}
	return previous.videoFrameEncoding == current.videoFrameEncoding &&
		previous.eotf == current.eotf &&
		previous.colorspace == current.colorspace &&
		previous.invertedVertical == current.invertedVertical;
}


inline bool HasSameStaticHdrMetadata(
	const VideoState& previous,
	const VideoState& current) noexcept
{
	if (static_cast<bool>(previous.hdrData) !=
		static_cast<bool>(current.hdrData))
	{
		return false;
	}
	return !previous.hdrData || *previous.hdrData == *current.hdrData;
}


inline CaptureVideoStateChangeClass ClassifyCaptureVideoStateChange(
	const VideoState* previousValid,
	const VideoState& current) noexcept
{
	if (!current.valid)
		return CaptureVideoStateChangeClass::Invalid;
	if (!previousValid || !previousValid->valid)
		return CaptureVideoStateChangeClass::Initial;
	if (!HasSameMaterialCaptureContract(*previousValid, current))
		return CaptureVideoStateChangeClass::MaterialSignal;
	return HasSameStaticHdrMetadata(*previousValid, current) ?
		CaptureVideoStateChangeClass::Duplicate :
		CaptureVideoStateChangeClass::StaticHdrMetadataOnly;
}


inline bool CaptureStateChangeMayRetainRendererIngress(
	CaptureVideoStateChangeClass changeClass,
	bool materialStateAcknowledgementPending = false) noexcept
{
	if (changeClass == CaptureVideoStateChangeClass::Invalid)
		return true;
	if (materialStateAcknowledgementPending)
		return false;
	return changeClass == CaptureVideoStateChangeClass::Duplicate ||
		changeClass == CaptureVideoStateChangeClass::StaticHdrMetadataOnly;
}
