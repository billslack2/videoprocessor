#pragma once

#include <vector>
#include <atlstr.h>
#include <CaptureInput.h>
#include <LibMWCapture/MWCapture.h>

// Source IDs are the full SDK type/index DWORD, not just the input type.
// Keep labels identical in runtime discovery and configuration discovery.
inline bool MagewellInputDescription(
	DWORD source, const std::vector<DWORD>& sources,
	CaptureInputType& inputType, CString& name)
{
	const TCHAR* baseName = nullptr;
	switch (INPUT_TYPE(source))
	{
	case MWCAP_VIDEO_INPUT_TYPE_HDMI:
		inputType = CaptureInputType::HDMI; baseName = TEXT("HDMI"); break;
	case MWCAP_VIDEO_INPUT_TYPE_SDI:
		inputType = CaptureInputType::SDI_ELECTRICAL; baseName = TEXT("SDI"); break;
	case MWCAP_VIDEO_INPUT_TYPE_COMPONENT:
		inputType = CaptureInputType::COMPONENT; baseName = TEXT("Component"); break;
	case MWCAP_VIDEO_INPUT_TYPE_CVBS:
		inputType = CaptureInputType::COMPOSITE; baseName = TEXT("Composite"); break;
	case MWCAP_VIDEO_INPUT_TYPE_YC:
		inputType = CaptureInputType::S_VIDEO; baseName = TEXT("S-Video"); break;
	default:
		return false;
	}

	unsigned int sameType = 0;
	for (DWORD candidate : sources)
		if (INPUT_TYPE(candidate) == INPUT_TYPE(source))
			++sameType;
	if (sameType > 1)
		name.Format(TEXT("%s %u"), baseName,
			static_cast<unsigned int>(INPUT_INDEX(source)) + 1);
	else
		name = baseName;
	return true;
}
