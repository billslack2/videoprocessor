/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <dvdmedia.h>

#include <guid.h>
#include <AspectRatio.h>
#include <ConfigFile.h>
#include <RendererProfileConfig.h>
#include <UnifiedProfileRuntime.h>
#include <video_frame_formatter/CNoopVideoFrameFormatter.h>
#include <video_frame_formatter/CV210toP010VideoFrameFormatter.h>
#include <video_frame_formatter/CUYVYtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CARGBtoP010VideoFrameFormatter.h>
#include <video_frame_formatter/CDeckLinkRGBToP010VideoFrameFormatter.h>
#include <video_frame_formatter/CR210toRGB48VideoFrameFormatter.h>
#include <video_frame_formatter/CR12BtoRGB48VideoFrameFormatter.h>
#include <microsoft_directshow/DirectShowTranslations.h>
#include <microsoft_directshow/MadVRShaderLoader.h>

#include "DirectShowGenericHDRVideoRenderer.h"


DirectShowGenericHDRVideoRenderer::DirectShowGenericHDRVideoRenderer(
	GUID rendererCLSID,
	IRendererCallback& callback,
	HWND videoHwnd,
	HWND eventHwnd,
	UINT eventMsg,
	ITimingClock* timingClock,
	DirectShowStartStopTimeMethod timestamp,
	bool useFrameQueue,
	size_t frameQueueMaxSize,
	VideoConversionOverride videoConversionOverride,
	DXVA_NominalRange forceNominalRange,
	DXVA_VideoTransferFunction forceVideoTransferFunction,
	DXVA_VideoTransferMatrix forceVideoTransferMatrix,
	DXVA_VideoPrimaries forceVideoPrimaries):
	DirectShowVideoRenderer(
		callback,
		videoHwnd,
		eventHwnd,
		eventMsg,
		timingClock,
		timestamp,
		useFrameQueue,
		frameQueueMaxSize,
		videoConversionOverride),
	m_rendererCLSID(rendererCLSID),
	m_forceNominalRange(forceNominalRange),
	m_forceVideoTransferFunction(forceVideoTransferFunction),
	m_forceVideoTransferMatrix(forceVideoTransferMatrix),
	m_forceVideoPrimaries(forceVideoPrimaries)
{
	m_rendererGeneration = MadVRShaderLoader::BeginRendererGeneration();
	RestoreRuntimeShaderRequest();
	callback.OnRendererDetailString(TEXT("DirectShow HDR renderer"));
	DebugLog::Log(
		"Shaders: renderer generation %llu created requested=%S mapping=%s",
		static_cast<unsigned long long>(m_rendererGeneration),
		static_cast<LPCTSTR>(m_requestedShaderRule),
		MadVRNlsMappingModeName(m_nlsMappingMode));
}


DirectShowGenericHDRVideoRenderer::~DirectShowGenericHDRVideoRenderer()
{
}


//
// IVideoRenderer
//


bool DirectShowGenericHDRVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	if (!DirectShowVideoRenderer::OnVideoState(videoState))
		return false;

	// Handle HDR data
	if (videoState->hdrData)
	{
		if (!m_videoState->hdrData || *videoState->hdrData != *(m_videoState->hdrData))
		{
			if (FAILED(m_liveSource->OnHDRData(videoState->hdrData)))
				throw std::runtime_error("Failed to set HDR data");

			// Update the HDR in our local videostate copy
			m_videoState->hdrData = videoState->hdrData;
		}
	}

	return true;
}


//
// DirectShowVideoRenderer
//


void DirectShowGenericHDRVideoRenderer::RendererBuild()
{
	if (FAILED(CoCreateInstance(
		m_rendererCLSID,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IBaseFilter,
		(void**)&m_pRenderer)))
		throw std::runtime_error("Failed to create renderer instance");
}


void DirectShowGenericHDRVideoRenderer::MediaTypeGenerate()
{
	GUID mediaSubType;
	int bitCount;
	LONG heightMultiplier = 1;

	// Smart P010 conversion: Auto-detect input format and convert to P010
	// User selects "YUV/RGB > P010" in UI, we intelligently pick the right converter
	// Also auto-enable for ARGB/BGRA even without explicit selection (makes it "just work")
	const bool needsP010Conversion = 
		(m_videoConversionOverride == VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010) ||
		(m_videoState->videoFrameEncoding == VideoFrameEncoding::ARGB_8BIT) ||
		(m_videoState->videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT) ||
		(m_videoState->videoFrameEncoding == VideoFrameEncoding::R10b) ||
		(m_videoState->videoFrameEncoding == VideoFrameEncoding::R10l) ||
		(m_videoState->videoFrameEncoding == VideoFrameEncoding::R12L);

	if (needsP010Conversion)
	{
		mediaSubType = MEDIASUBTYPE_P010;
		bitCount = 10;
		
		// Auto-detect input format and use appropriate converter
		if (m_videoState->videoFrameEncoding == VideoFrameEncoding::V210)
		{
			// V210 (10-bit 4:2:2) ? P010 (10-bit 4:2:0)
			m_videoFramFormatter = new CV210toP010VideoFrameFormatter();
		}
		else if (m_videoState->videoFrameEncoding == VideoFrameEncoding::UYVY)
		{
			// UYVY (8-bit 4:2:2) ? P010 (10-bit 4:2:0)
			m_videoFramFormatter = new CUYVYtoP010VideoFrameFormatter();
		}
		else if (m_videoState->videoFrameEncoding == VideoFrameEncoding::ARGB_8BIT ||
		         m_videoState->videoFrameEncoding == VideoFrameEncoding::BGRA_8BIT)
		{
			// ARGB/BGRA (8-bit 4:4:4 RGB) ? P010 (10-bit 4:2:0 YUV)
			m_videoFramFormatter = new CARGBtoP010VideoFrameFormatter();
		}
		else if (m_videoState->videoFrameEncoding == VideoFrameEncoding::R10b ||
			m_videoState->videoFrameEncoding == VideoFrameEncoding::R10l ||
			m_videoState->videoFrameEncoding == VideoFrameEncoding::R12L)
		{
			m_videoFramFormatter = new CDeckLinkRGBToP010VideoFrameFormatter();
		}
		else
		{
			throw std::runtime_error("P010 conversion does not support this input format");
		}
	}

	// Default conversions (non-P010)
	else
	{
		switch (m_videoState->videoFrameEncoding)
		{
			// r210 to RGB48
		case VideoFrameEncoding::R210:

			mediaSubType = MEDIASUBTYPE_RGB0;
			bitCount = 48;
			heightMultiplier = -1;

			m_videoFramFormatter = new CR210toRGB48VideoFrameFormatter();
			break;

			// RGB 12-bit to RGB48
		case VideoFrameEncoding::R12B:

			mediaSubType = MEDIASUBTYPE_RGB0;
			bitCount = 48;
			heightMultiplier = -1;

			m_videoFramFormatter = new CR12BtoRGB48VideoFrameFormatter();
			break;

			// No conversion needed
		default:
			mediaSubType = TranslateToMediaSubType(m_videoState->videoFrameEncoding);
			bitCount = VideoFrameEncodingBitsPerPixel(m_videoState->videoFrameEncoding);;

			m_videoFramFormatter = new CNoopVideoFrameFormatter();
		}
	}

	m_videoFramFormatter->OnVideoState(m_videoState);

	// Build pmt
	assert(!m_pmt.pbFormat);
	ZeroMemory(&m_pmt, sizeof(AM_MEDIA_TYPE));

	m_pmt.formattype = FORMAT_VIDEOINFO2;
	m_pmt.cbFormat = sizeof(VIDEOINFOHEADER2);
	m_pmt.majortype = MEDIATYPE_Video;
	m_pmt.subtype = mediaSubType;
	m_pmt.bFixedSizeSamples = TRUE;
	m_pmt.bTemporalCompression = FALSE;

	assert(!m_pmt.pbFormat);
	m_pmt.pbFormat = (BYTE*)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER2));
	if (!m_pmt.pbFormat)
		throw std::runtime_error("Out of mem");

	VIDEOINFOHEADER2* pvi2 = (VIDEOINFOHEADER2*)m_pmt.pbFormat;
	ZeroMemory(pvi2, sizeof(VIDEOINFOHEADER2));

	// Populate bitmap info header
	// https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader

	pvi2->bmiHeader.biSizeImage = m_videoFramFormatter->GetOutFrameSize();
	pvi2->bmiHeader.biBitCount = bitCount;
	pvi2->bmiHeader.biCompression = m_pmt.subtype.Data1;
	pvi2->bmiHeader.biWidth = m_videoState->displayMode->FrameWidth();
	pvi2->bmiHeader.biHeight = ((long)m_videoState->displayMode->FrameHeight()) * heightMultiplier;
	pvi2->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi2->bmiHeader.biPlanes = 1;
	pvi2->bmiHeader.biClrImportant = 0;
	pvi2->bmiHeader.biClrUsed = 0;

	pvi2->AvgTimePerFrame = (REFERENCE_TIME)(UNITS / m_videoState->displayMode->RefreshRateHz());

	// NLS still uses madVR for the actual high-quality resize. We only change
	// the negotiated display aspect ratio so madVR fills the configured scope
	// viewport before applying the nonlinear pre-resize coordinate mapping.
	m_outputAspectRatioX = 0;
	m_outputAspectRatioY = 0;
	if (MadVRShaderLoader::GetRuntimeOutputAspectRatio(m_outputAspectRatioX,
		m_outputAspectRatioY))
	{
		pvi2->dwPictAspectRatioX = static_cast<DWORD>(m_outputAspectRatioX);
		pvi2->dwPictAspectRatioY = static_cast<DWORD>(m_outputAspectRatioY);
		DebugLog::Log("Shaders: negotiated output picture aspect ratio %lu:%lu",
			m_outputAspectRatioX, m_outputAspectRatioY);
	}

	DXVA_ExtendedFormat* colorimetry = (DXVA_ExtendedFormat*)&(pvi2->dwControlFlags);

	colorimetry->VideoPrimaries =
		(m_forceVideoPrimaries != DXVA_VideoPrimaries::DXVA_VideoPrimaries_Unknown) ?
		m_forceVideoPrimaries :
		TranslateVideoPrimaries(m_videoState->colorspace);

	colorimetry->VideoTransferMatrix =
		(m_forceVideoTransferMatrix != DXVA_VideoTransferMatrix::DXVA_VideoTransferMatrix_Unknown) ?
		m_forceVideoTransferMatrix :
		TranslateVideoTransferMatrix(m_videoState->colorspace);

	colorimetry->VideoTransferFunction =
		(m_forceVideoTransferFunction != DXVA_VideoTransferFunction::DXVA_VideoTransFunc_Unknown) ?
		m_forceVideoTransferFunction :
		TranslateVideoTranferFunction(m_videoState->eotf, m_videoState->colorspace);

	colorimetry->NominalRange =
		(m_forceNominalRange != DXVA_NominalRange::DXVA_NominalRange_Unknown) ?
		m_forceNominalRange :
		DXVA_NominalRange::DXVA_NominalRange_Unknown;  // = Let renderer guess

	pvi2->dwControlFlags += AMCONTROL_USED;
	pvi2->dwControlFlags += AMCONTROL_COLORINFO_PRESENT;

	if (m_videoState->displayMode->IsInterlaced())
		pvi2->dwInterlaceFlags = AMINTERLACE_IsInterlaced | AMINTERLACE_DisplayModeBobOrWeave;

	m_pmt.lSampleSize = DIBSIZE(pvi2->bmiHeader);
}


void DirectShowGenericHDRVideoRenderer::RendererConnect()
{
	if (FAILED(m_pGraph->AddFilter(m_pRenderer, L"Renderer")))
		throw std::runtime_error("Failed to add renderer to the graph");

	IEnumPins* pEnum = nullptr;
	IPin* pLiveSourceOutputPin = nullptr;
	IPin* pRendererInputPin = nullptr;

	if (FAILED(m_liveSource->EnumPins(&pEnum)))
		throw std::runtime_error("Failed to get livesource pin enumerator");

	if (pEnum->Next(1, &pLiveSourceOutputPin, nullptr) != S_OK)
	{
		pEnum->Release();

		throw std::runtime_error("Failed to run next on livesource pin");
	}

	pEnum->Release();
	pEnum = nullptr;

	if (FAILED(m_pRenderer->EnumPins(&pEnum)))
	{
		pLiveSourceOutputPin->Release();
		pRendererInputPin->Release();

		throw std::runtime_error("Failed to get livesource pin enumerator");
	}

	if (pEnum->Next(1, &pRendererInputPin, nullptr) != S_OK)
	{
		pLiveSourceOutputPin->Release();
		pRendererInputPin->Release();
		pEnum->Release();

		throw std::runtime_error("Failed to get livesource pin enumerator");
	}

	pEnum->Release();
	pEnum = nullptr;

	// Directly connect
	if (FAILED(m_pGraph->ConnectDirect(pLiveSourceOutputPin, pRendererInputPin, &m_pmt)))
	{
		pLiveSourceOutputPin->Release();
		pRendererInputPin->Release();

		throw std::runtime_error("Failed to connect pins");
	}

	pLiveSourceOutputPin->Release();
	pRendererInputPin->Release();

	// The renderer owns the GPU surface, so external HLSL must be installed
	// through its interface after the graph connection exists. A full renderer
	// rebuild creates a new COM instance and naturally reapplies this chain.
	const MadVRShaderSelection shaderSelection =
		MadVRShaderLoader::ApplyConfiguredShaders(m_pRenderer, *m_videoState);
	UpdateActiveShaderSelection(shaderSelection);
	if (m_requestedRuleUsesNlsMapping)
		UpdateNlsOsdMode(m_nlsMappingMode);
	else if (m_requestedShaderRule.CompareNoCase(TEXT("nls_off")) == 0)
		m_activeShaderRule = TEXT("NLS: Off");
}


void DirectShowGenericHDRVideoRenderer::UpdateActiveShaderSelection(
	const MadVRShaderSelection& shaderSelection)
{
	m_activeShaders.clear();
	m_activeShaderRule = TEXT("None");
	if (!shaderSelection.ruleLabel.empty())
		m_activeShaderRule.Format(TEXT("%S"), shaderSelection.ruleLabel.c_str());
	for (const ActiveMadVRShader& shader : shaderSelection.activeShaders)
	{
		CString label;
		label.Format(TEXT("%s: %S"),
			shader.postResize ? TEXT("Post") : TEXT("Pre"),
			shader.name.c_str());
		m_activeShaders.push_back(label);
	}
}


void DirectShowGenericHDRVideoRenderer::RestoreRuntimeShaderRequest()
{
	const MadVRShaderRuntimeSnapshot runtime =
		MadVRShaderLoader::GetRuntimeShaderState();
	m_nlsMappingMode = runtime.nlsMode;
	m_nlsTargetAspect = runtime.nlsTargetAspect;
	if (runtime.requestedRule.empty())
		return;

	m_requestedShaderRule.Format(TEXT("%S"), runtime.requestedRule.c_str());
	std::string label;
	std::string inactiveRule;
	if (!MadVRShaderLoader::GetRuleActivationInfo(runtime.requestedRule,
		label, inactiveRule, m_requestedRuleUsesNlsMapping))
	{
		m_requestedShaderRule.Empty();
		m_nlsMappingMode = MadVRNlsMappingMode::OFF;
		return;
	}
	m_requestedShaderLabel.Format(TEXT("%S"), label.c_str());
	m_inactiveShaderRule.Format(TEXT("%S"), inactiveRule.c_str());
	m_requestedShaderApplied = false;
	if (m_requestedRuleUsesNlsMapping)
		UpdateNlsOsdMode(MadVRNlsMappingMode::WAITING);
	else if (m_requestedShaderRule.CompareNoCase(TEXT("nls_off")) == 0)
		m_activeShaderRule = TEXT("NLS: Off");
}


void DirectShowGenericHDRVideoRenderer::UpdateNlsOsdMode(
	MadVRNlsMappingMode mode)
{
	m_nlsMappingMode = mode;
	switch (mode)
	{
	case MadVRNlsMappingMode::ACTIVE:
		m_activeShaderRule = TEXT("NLS: Active");
		break;
	case MadVRNlsMappingMode::SCOPE_PASSTHROUGH:
		m_activeShaderRule =
			m_nlsTargetAspect > 2.2 ?
			TEXT("NLS: Scope passthrough") :
			TEXT("NLS: Linear passthrough");
		break;
	case MadVRNlsMappingMode::SAFE_FIT:
		m_activeShaderRule = TEXT("NLS: Safe fit");
		break;
	case MadVRNlsMappingMode::WAITING:
		m_activeShaderRule = TEXT("NLS: Waiting");
		break;
	case MadVRNlsMappingMode::OFF:
	default:
		m_activeShaderRule = TEXT("NLS: Off");
		break;
	}
}


bool DirectShowGenericHDRVideoRenderer::DoesOutputAspectRequireRestart(
	unsigned long desiredAspectX, unsigned long desiredAspectY) const
{
	if (!m_videoState || !m_videoState->displayMode)
	{
		return MadVROutputAspectRequiresRestart(
			m_outputAspectRatioX, m_outputAspectRatioY,
			desiredAspectX, desiredAspectY, 0.0);
	}
	const double nativeAspect =
		static_cast<double>(m_videoState->displayMode->FrameWidth()) /
		std::max<long>(1, m_videoState->displayMode->FrameHeight());
	return MadVROutputAspectRequiresRestart(
		m_outputAspectRatioX, m_outputAspectRatioY,
		desiredAspectX, desiredAspectY, nativeAspect);
}


MadVRActivePictureGeometry
DirectShowGenericHDRVideoRenderer::MakeRuntimeGeometry(
	const ActivePictureRectangle& rectangle) const
{
	return { rectangle.aspectRatio,
		static_cast<double>(rectangle.left) / rectangle.rasterWidth,
		static_cast<double>(rectangle.top) / rectangle.rasterHeight,
		static_cast<double>(rectangle.right) / rectangle.rasterWidth,
		static_cast<double>(rectangle.bottom) / rectangle.rasterHeight,
		rectangle.generation, m_rendererGeneration, true };
}


bool DirectShowGenericHDRVideoRenderer::SelectShaderRule(const CString& ruleName,
	CString& activeRule, bool& rendererRestartRequired)
{
	rendererRestartRequired = false;
	if (!m_pRenderer || !m_videoState)
		return false;

	CT2A ruleUtf8(ruleName, CP_UTF8);
	std::string label;
	std::string inactiveRule;
	bool nlsMapping = false;
	if (!MadVRShaderLoader::GetRuleActivationInfo(std::string(ruleUtf8),
		label, inactiveRule, nlsMapping))
		return false;
	if (m_requestedShaderRule.CompareNoCase(ruleName) == 0)
	{
		activeRule = m_activeShaderRule;
		DebugLog::Log("Shaders: coalesced duplicate manual request for \"%s\"",
			static_cast<const char*>(ruleUtf8));
		return true;
	}
	m_requestedShaderRule = ruleName;
	m_requestedShaderLabel.Format(TEXT("%S"), label.c_str());
	m_inactiveShaderRule.Format(TEXT("%S"), inactiveRule.c_str());
	m_requestedRuleUsesNlsMapping = nlsMapping;

	ActivePictureRectangle activeRectangle;
	const bool aspectAvailable = GetActivePictureRectangle(activeRectangle);
	const double activeAspectRatio = activeRectangle.aspectRatio;
	if (nlsMapping)
	{
		MadVRNlsMappingDecision decision;
		if (!MadVRShaderLoader::EvaluateNlsMapping(std::string(ruleUtf8),
			aspectAvailable, activeAspectRatio, decision))
			return false;
		MadVRShaderLoader::SetRuntimeShaderSelection(
			std::string(ruleUtf8), std::string(ruleUtf8), decision.mode);
		if (decision.mode == MadVRNlsMappingMode::ACTIVE ||
			decision.mode == MadVRNlsMappingMode::SCOPE_PASSTHROUGH ||
			decision.mode == MadVRNlsMappingMode::SAFE_FIT)
		{
			if (!MadVRShaderLoader::SetRuntimeActivePictureGeometry(
				MakeRuntimeGeometry(activeRectangle)))
				return false;
		}
		const MadVRShaderSelection selection =
			MadVRShaderLoader::ApplyConfiguredShaderRule(m_pRenderer,
				*m_videoState, std::string(ruleUtf8));
		UpdateActiveShaderSelection(selection);
		m_nlsTargetAspect = decision.targetAspect;
		UpdateNlsOsdMode(decision.mode);
		m_requestedShaderApplied =
			decision.mode == MadVRNlsMappingMode::ACTIVE ||
			decision.mode == MadVRNlsMappingMode::SCOPE_PASSTHROUGH ||
			decision.mode == MadVRNlsMappingMode::SAFE_FIT;
		m_appliedShaderAspectRatio =
			m_requestedShaderApplied ? activeAspectRatio : 0.0;
		m_appliedActivePictureGeneration =
			m_requestedShaderApplied ? activeRectangle.generation : 0;
		m_appliedScreenProfileGeneration = m_screenProfileGeneration;
		activeRule = m_activeShaderRule;
		rendererRestartRequired = DoesOutputAspectRequireRestart(
			selection.outputAspectRatioX, selection.outputAspectRatioY);
		DebugLog::Log(
			"Shaders: NLS mapping change requested=%s effective=%s mapping=%s rect=%d,%d-%d,%d active_generation=%llu source=%.4f target=%.4f renderer_generation=%llu reason=\"%s\" renderer_restart=%d",
			static_cast<const char*>(ruleUtf8),
			selection.ruleName.c_str(),
			MadVRNlsMappingModeName(decision.mode),
			aspectAvailable ? activeRectangle.left : 0,
			aspectAvailable ? activeRectangle.top : 0,
			aspectAvailable ? activeRectangle.right : 0,
			aspectAvailable ? activeRectangle.bottom : 0,
			static_cast<unsigned long long>(
				aspectAvailable ? activeRectangle.generation : 0),
			decision.sourceAspect, decision.targetAspect,
			static_cast<unsigned long long>(m_rendererGeneration),
			decision.reason.c_str(), rendererRestartRequired ? 1 : 0);
		return !selection.ruleName.empty();
	}

	MadVRShaderLoader::SetRuntimeShaderSelection(
		std::string(ruleUtf8), std::string(ruleUtf8),
		MadVRNlsMappingMode::OFF);
	std::string reason;
	if (!MadVRShaderLoader::ValidateActivePictureAspect(std::string(ruleUtf8),
		aspectAvailable, activeAspectRatio, reason))
	{
		if (!inactiveRule.empty())
		{
			MadVRShaderLoader::SetRuntimeShaderSelection(
				std::string(ruleUtf8), inactiveRule,
				MadVRNlsMappingMode::OFF);
			const MadVRShaderSelection bypassSelection =
				MadVRShaderLoader::ApplyConfiguredShaderRule(m_pRenderer,
					*m_videoState, inactiveRule, false);
			UpdateActiveShaderSelection(bypassSelection);
			rendererRestartRequired = DoesOutputAspectRequireRestart(
				bypassSelection.outputAspectRatioX,
				bypassSelection.outputAspectRatioY);
		}
		m_requestedShaderApplied = false;
		m_appliedShaderAspectRatio = 0.0;
		m_activeShaderRule.Format(TEXT("%s (Waiting)"),
			static_cast<LPCTSTR>(m_requestedShaderLabel));
		activeRule = m_activeShaderRule;
		DebugLog::Log("Shaders: armed rule \"%s\"; waiting because %s",
			static_cast<const char*>(ruleUtf8), reason.c_str());
		return true;
	}

	const MadVRShaderSelection selection =
		MadVRShaderLoader::ApplyConfiguredShaderRule(m_pRenderer, *m_videoState,
			std::string(ruleUtf8));
	UpdateActiveShaderSelection(selection);
	m_requestedShaderApplied = true;
	m_appliedShaderAspectRatio = activeAspectRatio;
	m_appliedActivePictureGeneration = activeRectangle.generation;
	m_appliedScreenProfileGeneration = m_screenProfileGeneration;
	activeRule = m_activeShaderRule;
	if (m_requestedShaderRule.CompareNoCase(TEXT("nls_off")) == 0)
	{
		UpdateNlsOsdMode(MadVRNlsMappingMode::OFF);
		activeRule = m_activeShaderRule;
	}
	rendererRestartRequired = DoesOutputAspectRequireRestart(
		selection.outputAspectRatioX, selection.outputAspectRatioY);
	return !selection.ruleName.empty();
}


bool DirectShowGenericHDRVideoRenderer::RefreshShaderRule(CString& activeRule,
	bool& rendererRestartRequired)
{
	activeRule = m_activeShaderRule;
	rendererRestartRequired = false;
	if (m_requestedShaderRule.IsEmpty() || !m_pRenderer || !m_videoState)
		return false;

	CT2A requestedUtf8(m_requestedShaderRule, CP_UTF8);
	ActivePictureRectangle activeRectangle;
	const bool aspectAvailable = GetActivePictureRectangle(activeRectangle);
	const double activeAspectRatio = activeRectangle.aspectRatio;
	if (m_requestedRuleUsesNlsMapping)
	{
		MadVRNlsMappingDecision decision;
		if (!MadVRShaderLoader::EvaluateNlsMapping(
			std::string(requestedUtf8), aspectAvailable,
			activeAspectRatio, decision))
			return false;
		if (decision.mode == MadVRNlsMappingMode::WAITING)
		{
			if (m_nlsMappingMode == MadVRNlsMappingMode::WAITING)
			{
				UpdateNlsOsdMode(MadVRNlsMappingMode::WAITING);
				return false;
			}

			// A clear high-confidence transition withdraws stale geometry before
			// the replacement rectangle is confirmed. Keep the armed output
			// contract, but temporarily remove the NLS shader so stale crop/
			// stretch parameters cannot visibly damage the new scene.
			MadVRShaderLoader::SetRuntimeShaderSelection(
				std::string(requestedUtf8), std::string(requestedUtf8),
				MadVRNlsMappingMode::WAITING);
			const MadVRShaderSelection waitingSelection =
				MadVRShaderLoader::ApplyConfiguredShaderRule(
					m_pRenderer, *m_videoState,
					std::string(requestedUtf8), false);
			UpdateActiveShaderSelection(waitingSelection);
			UpdateNlsOsdMode(MadVRNlsMappingMode::WAITING);
			m_requestedShaderApplied = false;
			m_appliedShaderAspectRatio = 0.0;
			m_appliedActivePictureGeneration = 0;
			m_appliedScreenProfileGeneration = m_screenProfileGeneration;
			activeRule = m_activeShaderRule;
			rendererRestartRequired = false;
			DebugLog::Log(
				"Shaders: NLS mapping change requested=%s effective=%s "
				"mapping=waiting active_generation=%llu "
				"renderer_generation=%llu reason=\"transition geometry "
				"is not stable; safe passthrough\" renderer_restart=0",
				static_cast<const char*>(requestedUtf8),
				waitingSelection.ruleName.c_str(),
				static_cast<unsigned long long>(
					activeRectangle.generation),
				static_cast<unsigned long long>(m_rendererGeneration));
			return true;
		}
		const bool mappingChanged = decision.mode != m_nlsMappingMode ||
			!m_requestedShaderApplied ||
			std::abs(activeAspectRatio - m_appliedShaderAspectRatio) > 0.01 ||
			activeRectangle.generation != m_appliedActivePictureGeneration ||
			m_screenProfileGeneration != m_appliedScreenProfileGeneration;
		if (!mappingChanged)
			return false;

		const MadVRActivePictureGeometry geometry =
			MakeRuntimeGeometry(activeRectangle);
		if (!MadVRShaderLoader::SetRuntimeActivePictureGeometry(geometry))
		{
			DebugLog::Log(
				"Shaders: rejected NLS geometry from stale renderer generation active_generation=%llu renderer_generation=%llu",
				static_cast<unsigned long long>(activeRectangle.generation),
				static_cast<unsigned long long>(m_rendererGeneration));
			return false;
		}
		MadVRShaderLoader::SetRuntimeShaderSelection(
			std::string(requestedUtf8), std::string(requestedUtf8),
			decision.mode);
		const MadVRShaderSelection selection =
			MadVRShaderLoader::ApplyConfiguredShaderRule(m_pRenderer,
				*m_videoState, std::string(requestedUtf8), false);
		UpdateActiveShaderSelection(selection);
		m_nlsTargetAspect = decision.targetAspect;
		UpdateNlsOsdMode(decision.mode);
		m_requestedShaderApplied = true;
		m_appliedShaderAspectRatio = activeAspectRatio;
		m_appliedActivePictureGeneration = activeRectangle.generation;
		m_appliedScreenProfileGeneration = m_screenProfileGeneration;
		activeRule = m_activeShaderRule;
		rendererRestartRequired = DoesOutputAspectRequireRestart(
			selection.outputAspectRatioX, selection.outputAspectRatioY);
		DebugLog::Log(
			"Shaders: NLS mapping change requested=%s effective=%s mapping=%s rect=%d,%d-%d,%d active_generation=%llu source=%.4f target=%.4f renderer_generation=%llu reason=\"%s\" renderer_restart=%d",
			static_cast<const char*>(requestedUtf8),
			selection.ruleName.c_str(),
			MadVRNlsMappingModeName(decision.mode),
			activeRectangle.left, activeRectangle.top,
			activeRectangle.right, activeRectangle.bottom,
			static_cast<unsigned long long>(activeRectangle.generation),
			decision.sourceAspect, decision.targetAspect,
			static_cast<unsigned long long>(m_rendererGeneration),
			decision.reason.c_str(), rendererRestartRequired ? 1 : 0);
		return true;
	}

	std::string reason;
	const bool shouldApply = MadVRShaderLoader::ValidateActivePictureAspect(
		std::string(requestedUtf8), aspectAvailable, activeAspectRatio, reason);

	// After a graph rebuild the detector deliberately reacquires stability.
	// Preserve the current applied/bypassed state during that short interval so
	// the guard cannot create an aspect restart loop.
	if (!aspectAvailable && !shouldApply)
		return false;
	const bool activeAspectChanged = shouldApply && m_requestedShaderApplied &&
		(std::abs(activeAspectRatio - m_appliedShaderAspectRatio) > 0.01 ||
			activeRectangle.generation != m_appliedActivePictureGeneration ||
			m_screenProfileGeneration != m_appliedScreenProfileGeneration);
	if (shouldApply == m_requestedShaderApplied && !activeAspectChanged)
		return false;

	std::string ruleToApply;
	if (shouldApply)
	{
		ruleToApply = std::string(requestedUtf8);
		MadVRShaderLoader::SetRuntimeActivePictureGeometry(
			MakeRuntimeGeometry(activeRectangle));
	}
	else
	{
		CT2A inactiveUtf8(m_inactiveShaderRule, CP_UTF8);
		ruleToApply = std::string(inactiveUtf8);
		if (ruleToApply.empty())
		{
			DebugLog::Log("Shaders: conditional rule \"%s\" cannot bypass because inactive_rule is missing",
				static_cast<const char*>(requestedUtf8));
			return false;
		}
	}

	const MadVRShaderSelection selection =
		MadVRShaderLoader::ApplyConfiguredShaderRule(m_pRenderer, *m_videoState,
			ruleToApply, shouldApply);
	UpdateActiveShaderSelection(selection);
	m_requestedShaderApplied = shouldApply;
	m_appliedShaderAspectRatio = shouldApply ? activeAspectRatio : 0.0;
	m_appliedActivePictureGeneration = shouldApply ? activeRectangle.generation : 0;
	m_appliedScreenProfileGeneration = m_screenProfileGeneration;
	if (!shouldApply)
		m_activeShaderRule.Format(TEXT("%s (Waiting)"),
			static_cast<LPCTSTR>(m_requestedShaderLabel));
	activeRule = m_activeShaderRule;
	rendererRestartRequired = DoesOutputAspectRequireRestart(
		selection.outputAspectRatioX, selection.outputAspectRatioY);
	DebugLog::Log("Shaders: armed rule \"%s\" %s at active aspect %.4f%s%s",
		static_cast<const char*>(requestedUtf8),
		shouldApply ? "engaged" : "bypassed",
		activeAspectRatio,
		shouldApply ? "" : "; ",
		shouldApply ? "" : reason.c_str());
	return true;
}

bool DirectShowGenericHDRVideoRenderer::SetScreenProfile(bool scopeScreen,
	CString& activeProfile, bool& rendererRestartRequired)
{
	rendererRestartRequired = false;
	m_nlsTargetAspect = scopeScreen ? 2.35 : 16.0 / 9.0;
	MadVRShaderLoader::SetRuntimeNlsTargetAspect(m_nlsTargetAspect);
	activeProfile = scopeScreen ? TEXT("Scope (2.35:1)") : TEXT("Normal (16:9)");
	++m_screenProfileGeneration;
	if (m_requestedShaderRule.IsEmpty())
		return true;
	CString activeRule = m_activeShaderRule;
	bool mappingRestartRequired = false;
	RefreshShaderRule(activeRule, mappingRestartRequired);
	unsigned long desiredAspectX = 0;
	unsigned long desiredAspectY = 0;
	rendererRestartRequired = mappingRestartRequired ||
		(m_requestedRuleUsesNlsMapping &&
			MadVRShaderLoader::GetRuntimeOutputAspectRatio(
				desiredAspectX, desiredAspectY) &&
			DoesOutputAspectRequireRestart(
				desiredAspectX, desiredAspectY));
	return true;
}


bool DirectShowGenericHDRVideoRenderer::ApplyApplicationState(
	const UnifiedProfileRuntime::Snapshot& snapshot,
	CString& activeState,
	bool& rendererRestartRequired)
{
	activeState.Empty();
	rendererRestartRequired = false;
	const RendererProfileConfig::ResolvedViewport& viewport =
		snapshot.viewport;
	m_nlsTargetAspect = viewport.screenAspect.value;
	MadVRShaderLoader::SetRuntimeNlsTargetAspect(m_nlsTargetAspect);
	m_screenProfileGeneration = viewport.generation;
	if (!m_requestedShaderRule.IsEmpty())
	{
		CString activeRule = m_activeShaderRule;
		bool mappingRestartRequired = false;
		RefreshShaderRule(activeRule, mappingRestartRequired);
		unsigned long desiredAspectX = 0;
		unsigned long desiredAspectY = 0;
		rendererRestartRequired = mappingRestartRequired ||
			(m_requestedRuleUsesNlsMapping &&
				MadVRShaderLoader::GetRuntimeOutputAspectRatio(
					desiredAspectX, desiredAspectY) &&
				DoesOutputAspectRequireRestart(
					desiredAspectX, desiredAspectY));
	}
	activeState.Format(TEXT("Viewport: %S (%S)"),
		viewport.profile.c_str(),
		viewport.screenAspect.Canonical().c_str());
	DebugLog::Log(
		"DirectShow application viewport profile=%s aspect=%s numeric=%.7f subtitle_fit=%d subtitle_hold_ms=%llu subtitle_padding=%d generation=%llu renderer_restart=%d",
		viewport.profile.c_str(),
		viewport.screenAspect.Canonical().c_str(),
		viewport.screenAspect.value,
		viewport.subtitleFit ? 1 : 0,
		static_cast<unsigned long long>(
			viewport.subtitleHoldMilliseconds),
		viewport.subtitlePaddingPixels,
		static_cast<unsigned long long>(m_screenProfileGeneration),
		rendererRestartRequired ? 1 : 0);
	return true;
}


void DirectShowGenericHDRVideoRenderer::LiveSourceBuildAndConnect()
{
	DirectShowVideoRenderer::LiveSourceBuildAndConnect();

	if (m_videoState->hdrData)
		m_liveSource->OnHDRData(m_videoState->hdrData);
}
