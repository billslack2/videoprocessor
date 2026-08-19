/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <MadVRShaderTransactionPolicy.h>

#include "DirectShowViewportPlacement.h"

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
#include <microsoft_directshow/DirectShowTranslations.h>
#include <microsoft_directshow/MadVRShaderLoader.h>
#include <vprenderer/NativeStatsOverlayPlacement.h>
#include <vprenderer/NativeStatsOverlayBitmap.h>

#include "DirectShowGenericHDRVideoRenderer.h"
#include "MadVRIngressPolicy.h"


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
		NlsMappingModeName(m_nlsMappingMode));
}


DirectShowGenericHDRVideoRenderer::~DirectShowGenericHDRVideoRenderer()
{
	if (IsGraphThread())
		ClearNativeStatsOverlayOnGraphThread();
}


//
// IVideoRenderer
//


bool DirectShowGenericHDRVideoRenderer::OnVideoState(VideoStateComPtr& videoState)
{
	if (!IsGraphThread())
	{
		if (!DirectShowVideoRenderer::OnVideoState(videoState))
			return false;
		const VideoStateComPtr state = videoState;
		PostCoalescedGraphCommand(GRAPH_COMMAND_HDR_STATE,
			[this, state]()
			{
				if (!m_liveSource || !state->hdrData)
					return;
				if (FAILED(m_liveSource->OnHDRData(state->hdrData)))
					throw std::runtime_error("Failed to set HDR data");
				m_videoState->hdrData = state->hdrData;
			});
		return true;
	}
	AssertGraphThread();

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
	AssertGraphThread();
	if (FAILED(CoCreateInstance(
		m_rendererCLSID,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_IBaseFilter,
		(void**)&m_pRenderer)))
		throw std::runtime_error("Failed to create renderer instance");

	const HRESULT osdResult = m_pRenderer->QueryInterface(
		__uuidof(IMadVROsdServices), reinterpret_cast<void**>(&m_osdServices));
	if (SUCCEEDED(osdResult))
		DebugLog::Log("madVR OSD: renderer generation %llu bitmap service available",
			static_cast<unsigned long long>(m_rendererGeneration));
	else
		DebugLog::Log("madVR OSD: renderer generation %llu bitmap service unavailable (0x%08lX); using legacy overlay",
			static_cast<unsigned long long>(m_rendererGeneration), osdResult);
}

bool DirectShowGenericHDRVideoRenderer::SetNativeStatsOverlay(
	const uint8_t* pixels, size_t byteCount, int width, int height, int stride)
{
	if (!m_osdServices || (pixels && (width <= 0 || height <= 0 ||
		stride < width * 4 || byteCount < static_cast<size_t>(stride) * height)))
		return false;
	{
		std::lock_guard<std::mutex> guard(m_osdMutex);
		if (pixels)
			m_osdPixels.assign(pixels, pixels + byteCount);
		else
			m_osdPixels.clear();
		m_osdWidth = pixels ? width : 0;
		m_osdHeight = pixels ? height : 0;
		m_osdStride = pixels ? stride : 0;
	}
	return PostCoalescedGraphCommand(GRAPH_COMMAND_MADVR_NATIVE_OSD, [this]()
	{
		ApplyNativeStatsOverlayOnGraphThread();
	});
}

void DirectShowGenericHDRVideoRenderer::ApplyNativeStatsOverlayOnGraphThread()
{
	AssertGraphThread();
	if (!m_osdServices)
		return;
	std::vector<uint8_t> pixels;
	int width = 0;
	int height = 0;
	int stride = 0;
	{
		std::lock_guard<std::mutex> guard(m_osdMutex);
		pixels = m_osdPixels;
		width = m_osdWidth;
		height = m_osdHeight;
		stride = m_osdStride;
	}
	if (pixels.empty())
	{
		ClearNativeStatsOverlayOnGraphThread();
		return;
	}
	RECT full{};
	RECT active{};
	const HRESULT rectResult = m_osdServices->OsdGetVideoRects(&full, &active);
	if (FAILED(rectResult))
	{
		if (!m_osdFailureLogged)
			DebugLog::Log("madVR OSD: OsdGetVideoRects failed (0x%08lX); falling back", rectResult);
		m_osdFailureLogged = true;
		return;
	}
	const NativeStatsOverlayPlacement::Rect output{
		static_cast<float>(full.left), static_cast<float>(full.top),
		static_cast<float>(full.right), static_cast<float>(full.bottom) };
	const NativeStatsOverlayPlacement::Rect picture{
		static_cast<float>(active.left), static_cast<float>(active.top),
		static_cast<float>(active.right), static_cast<float>(active.bottom) };
	const auto placement = NativeStatsOverlayPlacement::Place(
		picture, output, static_cast<float>(width), static_cast<float>(height));
	if (!placement.panel.IsValid())
		return;
	const int bitmapWidth = std::max(1, static_cast<int>(std::lround(placement.panel.Width())));
	const int bitmapHeight = std::max(1, static_cast<int>(std::lround(placement.panel.Height())));
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = bitmapWidth;
	info.bmiHeader.biHeight = -bitmapHeight;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	BITMAPINFO sourceInfo{};
	sourceInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	sourceInfo.bmiHeader.biWidth = width;
	sourceInfo.bmiHeader.biHeight = -height;
	sourceInfo.bmiHeader.biPlanes = 1;
	sourceInfo.bmiHeader.biBitCount = 32;
	sourceInfo.bmiHeader.biCompression = BI_RGB;
	HDC screen = GetDC(nullptr);
	HDC memory = CreateCompatibleDC(screen);
	void* targetPixels = nullptr;
	HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS,
		&targetPixels, nullptr, 0);
	if (bitmap)
	{
		HGDIOBJ old = SelectObject(memory, bitmap);
		// The native OSD is sometimes reduced to remain inside a windowed
		// picture. HALFTONE avoids the jagged, broken glyphs produced by the
		// default color-on-color bitmap shrink while leaving 1:1 OSD rendering
		// and placement unchanged.
		SetStretchBltMode(memory, HALFTONE);
		SetBrushOrgEx(memory, 0, 0, nullptr);
		StretchDIBits(memory, 0, 0, bitmapWidth, bitmapHeight,
			0, 0, width, height, pixels.data(), &sourceInfo, DIB_RGB_COLORS, SRCCOPY);
		NativeStatsOverlayBitmap::RestoreScaledAlphaNearest(
			pixels.data(), width, height, stride,
			static_cast<uint8_t*>(targetPixels), bitmapWidth, bitmapHeight,
			bitmapWidth * 4);
		SelectObject(memory, old);
	}
	DeleteDC(memory);
	ReleaseDC(nullptr, screen);
	if (!bitmap)
		return;
	const int x = static_cast<int>(std::lround(placement.panel.left - full.left));
	const int y = static_cast<int>(std::lround(placement.panel.top - full.top));
	const HRESULT setResult = m_osdServices->OsdSetBitmap("VideoProcessor.Diagnostics",
		bitmap, nullptr, 0, x, y, false, 100, 0,
		MADVR_BITMAP_INFO_DISPLAY | MADVR_BITMAP_MASKING_AWARE,
		nullptr, nullptr, nullptr);
	if (FAILED(setResult))
	{
		DeleteObject(bitmap);
		if (!m_osdFailureLogged)
			DebugLog::Log("madVR OSD: OsdSetBitmap failed (0x%08lX); falling back", setResult);
		m_osdFailureLogged = true;
		return;
	}
	if (m_osdBitmap)
		DeleteObject(m_osdBitmap);
	m_osdBitmap = bitmap;
	const bool placementChanged = !m_hasLoggedOsdPlacement ||
		std::memcmp(&full, &m_lastOsdFullRect, sizeof(RECT)) != 0 ||
		std::memcmp(&active, &m_lastOsdActiveRect, sizeof(RECT)) != 0 ||
		std::fabs(placement.scale - m_lastOsdScale) > 0.001f;
	if (placementChanged)
	{
		DebugLog::Log("madVR OSD: gen=%llu full=%ld,%ld-%ld,%ld active=%ld,%ld-%ld,%ld bitmap=%dx%d scale=%.3f panel=%d,%d inset_clamped=%d",
			static_cast<unsigned long long>(m_rendererGeneration), full.left, full.top,
			full.right, full.bottom, active.left, active.top, active.right, active.bottom,
			bitmapWidth, bitmapHeight, placement.scale, x, y, placement.insetClamped ? 1 : 0);
		m_lastOsdFullRect = full;
		m_lastOsdActiveRect = active;
		m_lastOsdScale = placement.scale;
		m_hasLoggedOsdPlacement = true;
	}
}

void DirectShowGenericHDRVideoRenderer::ClearNativeStatsOverlayOnGraphThread()
{
	AssertGraphThread();
	if (m_osdServices)
		m_osdServices->OsdSetBitmap("VideoProcessor.Diagnostics", nullptr, nullptr,
			0, 0, 0, false, 0, 0, 0, nullptr, nullptr, nullptr);
	if (m_osdBitmap)
	{
		DeleteObject(m_osdBitmap);
		m_osdBitmap = nullptr;
	}
}


void DirectShowGenericHDRVideoRenderer::MediaTypeGenerate()
{
	AssertGraphThread();
	GUID mediaSubType;
	int bitCount;
	LONG heightMultiplier = 1;

	// Smart P010 conversion: Auto-detect input format and convert to P010
	// User selects "YUV/RGB > P010" in UI, we intelligently pick the right converter
	// Automatically choose the proven P010 contract for formats that do not
	// have a validated direct madVR media subtype, including packed R12B/R12L.
	const bool needsP010Conversion = MadVRUsesP010Ingress(
		m_videoState->videoFrameEncoding, m_videoConversionOverride);

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
		else if (m_videoState->videoFrameEncoding == VideoFrameEncoding::UYVY ||
			m_videoState->videoFrameEncoding == VideoFrameEncoding::HDYC)
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
		else if (IsDeckLinkPackedRgbP010Encoding(
			m_videoState->videoFrameEncoding))
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

	colorimetry->NominalRange = ResolveDirectShowNominalRange(
		m_forceNominalRange, m_videoFramFormatter->GetOutputContract());

	pvi2->dwControlFlags += AMCONTROL_USED;
	pvi2->dwControlFlags += AMCONTROL_COLORINFO_PRESENT;

	if (m_videoState->displayMode->IsInterlaced())
		pvi2->dwInterlaceFlags = AMINTERLACE_IsInterlaced | AMINTERLACE_DisplayModeBobOrWeave;

	m_pmt.lSampleSize = DIBSIZE(pvi2->bmiHeader);
}


void DirectShowGenericHDRVideoRenderer::RendererConnect()
{
	AssertGraphThread();
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
	m_activeShaderSections.clear();
	m_activeShaderSectionsAvailable =
		shaderSelection.activeSectionsAvailable;
	m_activeShaderRule = TEXT("None");
	m_activeShaderCompanionLabel.Empty();
	if (!shaderSelection.ruleLabel.empty())
		m_activeShaderRule.Format(TEXT("%S"), shaderSelection.ruleLabel.c_str());
	if (!shaderSelection.companionRuleLabel.empty())
		m_activeShaderCompanionLabel.Format(
			TEXT("%S"), shaderSelection.companionRuleLabel.c_str());
	for (const ActiveMadVRShader& shader : shaderSelection.activeShaders)
	{
		CString label;
		label.Format(TEXT("%s: %S"),
			shader.postResize ? TEXT("Post") : TEXT("Pre"),
			shader.name.c_str());
		m_activeShaders.push_back(label);
	}
	for (const std::string& section : shaderSelection.activeSections)
		m_activeShaderSections.emplace_back(CStringA(section.c_str()));
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
	case MadVRNlsMappingMode::LINEAR_PASSTHROUGH:
		m_activeShaderRule = TEXT("NLS: Passthrough");
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
	if (!m_activeShaderCompanionLabel.IsEmpty())
	{
		m_activeShaderRule += TEXT(" + ");
		m_activeShaderRule += m_activeShaderCompanionLabel;
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

bool DirectShowGenericHDRVideoRenderer::TryDynamicOutputAspect(
	unsigned long desiredAspectX, unsigned long desiredAspectY)
{
	if (!m_liveSource || !m_liveSource->GetVideoOutputPin() ||
		!m_liveSource->GetVideoOutputPin()->
			RequestDynamicPictureAspectRatio(
				desiredAspectX, desiredAspectY))
		return false;
	m_outputAspectRatioX = desiredAspectX;
	m_outputAspectRatioY = desiredAspectY;
	DebugLog::Log(
		"Shaders: output picture aspect changing dynamically to %s; "
		"renderer_restart=0",
		desiredAspectX > 0 && desiredAspectY > 0 ?
			(std::to_string(desiredAspectX) + ":" +
				std::to_string(desiredAspectY)).c_str() :
			"native");
	return true;
}


bool DirectShowGenericHDRVideoRenderer::PrepareOutputAspectForShaderInstall(
	unsigned long desiredAspectX, unsigned long desiredAspectY,
	bool& rendererRestartRequired)
{
	if (!DoesOutputAspectRequireRestart(desiredAspectX, desiredAspectY))
		return true;
	if (TryDynamicOutputAspect(desiredAspectX, desiredAspectY))
		return true;

	// Never install a shader under a media DAR from a different presentation
	// plan. Keep the currently coherent presentation until the covered renderer
	// replacement negotiates the prepared geometry and DAR together.
	rendererRestartRequired = true;
	const bool prepared =
		MadVRShaderLoader::PrepareNlsOutputContractRendererReplacement();
	DebugLog::Log(
		"Shaders: deferred shader installation because dynamic picture aspect "
		"was rejected; output_contract_prepared=%d renderer_restart=1",
		prepared ? 1 : 0);
	return false;
}


bool DirectShowGenericHDRVideoRenderer::ApplyConfiguredShaderRuleCoherently(
	const std::string& ruleName, bool updateRuntimeRequest,
	unsigned long desiredAspectX, unsigned long desiredAspectY,
	bool& rendererRestartRequired, MadVRShaderSelection& selection)
{
	bool applied = false;
	const auto operation = [this, &ruleName, updateRuntimeRequest,
		desiredAspectX, desiredAspectY, &rendererRestartRequired,
		&selection, &applied]()
	{
		if (!PrepareOutputAspectForShaderInstall(
			desiredAspectX, desiredAspectY, rendererRestartRequired))
			return;
		selection = MadVRShaderLoader::ApplyConfiguredShaderRule(
			m_pRenderer, *m_videoState, ruleName, updateRuntimeRequest);
		applied = true;
	};

	ALiveSourceVideoOutputPin* outputPin = m_liveSource ?
		m_liveSource->GetVideoOutputPin() : nullptr;
	const bool held = outputPin ?
		outputPin->RunWithDeliveryHeld(operation) : false;
	if (!outputPin)
		operation();
	DebugLog::Log(
		"Shaders: coherent aspect/chain transaction selector=%s delivery_held=%d applied=%d renderer_restart=%d",
		ruleName.c_str(), held ? 1 : 0, applied ? 1 : 0,
		rendererRestartRequired ? 1 : 0);
	return applied;
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


bool DirectShowGenericHDRVideoRenderer::ResolveNlsSourceRectangle(
	ActivePictureRectangle& rectangle, bool& usingFullRasterFallback) const
{
	usingFullRasterFallback = false;
	if (GetActivePictureRectangle(rectangle))
		return true;

	// A detector reset temporarily withdraws crop authority, but the negotiated
	// raster remains a valid, complete source geometry. Treat it exactly as the
	// Alpha renderer does: use the full raster until a stable crop replaces it.
	// This prevents madVR NLS from being stranded in WAITING after a graph or
	// renderer-generation transition.
	if (!m_videoState || !m_videoState->displayMode)
		return false;
	const int rasterWidth = m_videoState->displayMode->FrameWidth();
	const int rasterHeight = m_videoState->displayMode->FrameHeight();
	const NlsSourceGeometry geometry = ResolveNlsSourceGeometry(false,
		0, 0, rasterWidth, rasterHeight, rasterWidth, rasterHeight);
	if (!geometry.valid)
		return false;

	rectangle = { geometry.left, geometry.top, geometry.right,
		geometry.bottom, rasterWidth, rasterHeight, geometry.aspect,
		m_rendererGeneration, true };
	usingFullRasterFallback = true;
	return true;
}


bool DirectShowGenericHDRVideoRenderer::SelectShaderRule(const CString& ruleName,
	CString& activeRule, bool& rendererRestartRequired)
{
	if (!IsGraphThread())
	{
		const CString requestedRule(ruleName);
		activeRule = requestedRule;
		rendererRestartRequired = false;
		return PostCoalescedGraphCommand(
			GRAPH_COMMAND_SHADER_SELECT,
			[this, requestedRule]()
			{
				CString appliedRule;
				bool restartRequired = false;
				SelectShaderRule(
					requestedRule, appliedRule, restartRequired);
				if (restartRequired)
					QueueRendererRestartCompletion();
			});
	}
	AssertGraphThread();

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
	CT2A requestedUtf8(m_requestedShaderRule, CP_UTF8);
	if (ShouldCoalesceMadVRShaderRequest(
		MadVRShaderLoader::RuleSelectorsEqual(
			static_cast<const char*>(requestedUtf8),
			static_cast<const char*>(ruleUtf8)),
		m_shaderRuleApplicationPending))
	{
		activeRule = m_activeShaderRule;
		DebugLog::Log("Shaders: coalesced duplicate manual request for \"%s\"",
			static_cast<const char*>(ruleUtf8));
		return true;
	}
	const bool previousRuleUsedNlsMapping = m_requestedRuleUsesNlsMapping;
	m_requestedShaderRule = ruleName;
	m_shaderRuleApplicationPending = true;
	m_requestedShaderLabel.Format(TEXT("%S"), label.c_str());
	m_inactiveShaderRule.Format(TEXT("%S"), inactiveRule.c_str());
	m_requestedRuleUsesNlsMapping = nlsMapping;
	ApplyVideoWindowPlacement();

	ActivePictureRectangle activeRectangle;
	bool aspectAvailable = GetActivePictureRectangle(activeRectangle);
	double activeAspectRatio = activeRectangle.aspectRatio;
	if (nlsMapping)
	{
		bool usingFullRasterFallback = false;
		aspectAvailable = ResolveNlsSourceRectangle(activeRectangle,
			usingFullRasterFallback);
		activeAspectRatio = activeRectangle.aspectRatio;
		MadVRNlsMappingDecision decision;
		if (!MadVRShaderLoader::EvaluateNlsMapping(std::string(ruleUtf8),
			aspectAvailable, activeAspectRatio, decision))
			return false;
		MadVRActivePictureGeometry geometry;
		if (aspectAvailable)
			geometry = MakeRuntimeGeometry(activeRectangle);
		decision = ConstrainMadVRNlsMappingToGeometry(decision, geometry);
		MadVRShaderLoader::SetRuntimeNlsDecision(decision);
		MadVRShaderLoader::SetRuntimeShaderSelection(
			std::string(ruleUtf8), std::string(ruleUtf8), decision.mode);
		if (decision.mode == MadVRNlsMappingMode::ACTIVE ||
			decision.mode == MadVRNlsMappingMode::LINEAR_PASSTHROUGH ||
			decision.mode == MadVRNlsMappingMode::SAFE_FIT)
		{
			if (!MadVRShaderLoader::SetRuntimeActivePictureGeometry(geometry))
				return false;
		}
		unsigned long desiredAspectX = 0;
		unsigned long desiredAspectY = 0;
		MadVRShaderLoader::GetRuntimeOutputAspectRatio(
			desiredAspectX, desiredAspectY);
		MadVRShaderSelection selection;
		if (!ApplyConfiguredShaderRuleCoherently(
			std::string(ruleUtf8), true, desiredAspectX, desiredAspectY,
			rendererRestartRequired, selection))
		{
			activeRule = m_activeShaderRule;
			return true;
		}
		UpdateActiveShaderSelection(selection);
		m_shaderRuleApplicationPending = false;
		m_nlsTargetAspect = decision.targetAspect;
		UpdateNlsOsdMode(decision.mode);
		m_requestedShaderApplied =
			decision.mode == MadVRNlsMappingMode::ACTIVE ||
			decision.mode == MadVRNlsMappingMode::LINEAR_PASSTHROUGH ||
			decision.mode == MadVRNlsMappingMode::SAFE_FIT;
		m_appliedShaderAspectRatio =
			m_requestedShaderApplied ? activeAspectRatio : 0.0;
		m_appliedActivePictureGeneration =
			m_requestedShaderApplied ? activeRectangle.generation : 0;
		m_appliedViewportGeneration = m_viewportGeneration;
		activeRule = m_activeShaderRule;
		rendererRestartRequired = DoesOutputAspectRequireRestart(
			selection.outputAspectRatioX, selection.outputAspectRatioY);
		if (rendererRestartRequired &&
			TryDynamicOutputAspect(selection.outputAspectRatioX,
				selection.outputAspectRatioY))
			rendererRestartRequired = false;
		if (rendererRestartRequired)
			MadVRShaderLoader::
				PrepareNlsOutputContractRendererReplacement();
		DebugLog::Log(
			"Shaders: NLS backend=madvr mapping change requested=%s effective=%s mapping=%s rect=%d,%d-%d,%d active_generation=%llu source=%.4f target=%.4f requested_ratio=%.5f max_ratio=%.5f axis=%s geometry=%s renderer_generation=%llu reason=\"%s\" renderer_restart=%d",
			static_cast<const char*>(ruleUtf8),
			selection.ruleName.c_str(),
			NlsMappingModeName(decision.mode),
			aspectAvailable ? activeRectangle.left : 0,
			aspectAvailable ? activeRectangle.top : 0,
			aspectAvailable ? activeRectangle.right : 0,
			aspectAvailable ? activeRectangle.bottom : 0,
			static_cast<unsigned long long>(
				aspectAvailable ? activeRectangle.generation : 0),
			decision.sourceAspect, decision.targetAspect,
			decision.requestedRatio, decision.maximumRatio,
			NlsMappingAxisName(decision),
			usingFullRasterFallback ? "full-raster-fallback" : "detected-crop",
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
			unsigned long desiredAspectX = 0;
			unsigned long desiredAspectY = 0;
			MadVRShaderLoader::GetRuntimeOutputAspectRatio(
				desiredAspectX, desiredAspectY);
			MadVRShaderSelection bypassSelection;
			if (!ApplyConfiguredShaderRuleCoherently(
					inactiveRule, false, desiredAspectX, desiredAspectY,
					rendererRestartRequired, bypassSelection))
			{
				activeRule = m_activeShaderRule;
				return true;
			}
			UpdateActiveShaderSelection(bypassSelection);
			rendererRestartRequired = DoesOutputAspectRequireRestart(
				bypassSelection.outputAspectRatioX,
				bypassSelection.outputAspectRatioY);
			if (rendererRestartRequired && TryDynamicOutputAspect(
				bypassSelection.outputAspectRatioX,
				bypassSelection.outputAspectRatioY))
				rendererRestartRequired = false;
		}
		m_shaderRuleApplicationPending = false;
		m_requestedShaderApplied = false;
		m_appliedShaderAspectRatio = 0.0;
		m_activeShaderRule.Format(TEXT("%s (Waiting)"),
			static_cast<LPCTSTR>(m_requestedShaderLabel));
		activeRule = m_activeShaderRule;
		DebugLog::Log("Shaders: armed rule \"%s\"; waiting because %s",
			static_cast<const char*>(ruleUtf8), reason.c_str());
		return true;
	}

	unsigned long desiredAspectX = 0;
	unsigned long desiredAspectY = 0;
	MadVRShaderLoader::GetRuntimeOutputAspectRatio(
		desiredAspectX, desiredAspectY);
	MadVRShaderSelection selection;
	if (!ApplyConfiguredShaderRuleCoherently(
		std::string(ruleUtf8), true, desiredAspectX, desiredAspectY,
		rendererRestartRequired, selection))
	{
		activeRule = m_activeShaderRule;
		return true;
	}
	UpdateActiveShaderSelection(selection);
	m_shaderRuleApplicationPending = false;
	m_requestedShaderApplied = true;
	m_appliedShaderAspectRatio = activeAspectRatio;
	m_appliedActivePictureGeneration = activeRectangle.generation;
	m_appliedViewportGeneration = m_viewportGeneration;
	activeRule = m_activeShaderRule;
	if (previousRuleUsedNlsMapping && selection.activeShaders.empty())
	{
		UpdateNlsOsdMode(MadVRNlsMappingMode::OFF);
		activeRule = m_activeShaderRule;
	}
	rendererRestartRequired = DoesOutputAspectRequireRestart(
		selection.outputAspectRatioX, selection.outputAspectRatioY);
	if (rendererRestartRequired &&
		TryDynamicOutputAspect(selection.outputAspectRatioX,
			selection.outputAspectRatioY))
		rendererRestartRequired = false;
	return !selection.ruleName.empty();
}


bool DirectShowGenericHDRVideoRenderer::RefreshShaderRule(CString& activeRule,
	bool& rendererRestartRequired)
{
	if (!IsGraphThread())
	{
		activeRule.Empty();
		rendererRestartRequired = false;
		return PostCoalescedGraphCommand(
			GRAPH_COMMAND_SHADER_REFRESH,
			[this]()
			{
				CString refreshedRule;
				bool restartRequired = false;
				RefreshShaderRule(refreshedRule, restartRequired);
				if (restartRequired)
					QueueRendererRestartCompletion();
			});
	}
	AssertGraphThread();

	activeRule = m_activeShaderRule;
	rendererRestartRequired = false;
	if (m_requestedShaderRule.IsEmpty() || !m_pRenderer || !m_videoState)
		return false;

	CT2A requestedUtf8(m_requestedShaderRule, CP_UTF8);
	ActivePictureRectangle activeRectangle;
	bool aspectAvailable = GetActivePictureRectangle(activeRectangle);
	double activeAspectRatio = activeRectangle.aspectRatio;
	if (m_requestedRuleUsesNlsMapping)
	{
		bool usingFullRasterFallback = false;
		aspectAvailable = ResolveNlsSourceRectangle(activeRectangle,
			usingFullRasterFallback);
		activeAspectRatio = activeRectangle.aspectRatio;
		// The refresh timer runs quickly while transitions are being acquired.
		// Avoid reparsing shader configuration when detector state cannot
		// possibly change the current mapping.
		if (!m_shaderRuleApplicationPending && !aspectAvailable &&
			m_nlsMappingMode == MadVRNlsMappingMode::WAITING)
		{
			UpdateNlsOsdMode(MadVRNlsMappingMode::WAITING);
			return false;
		}
		if (!m_shaderRuleApplicationPending && aspectAvailable &&
			m_requestedShaderApplied &&
			activeRectangle.generation ==
				m_appliedActivePictureGeneration &&
			m_viewportGeneration ==
				m_appliedViewportGeneration)
			return false;

		MadVRNlsMappingDecision decision;
		if (!MadVRShaderLoader::EvaluateNlsMapping(
			std::string(requestedUtf8), aspectAvailable,
			activeAspectRatio, decision))
			return false;
		MadVRActivePictureGeometry geometry;
		if (aspectAvailable)
			geometry = MakeRuntimeGeometry(activeRectangle);
		decision = ConstrainMadVRNlsMappingToGeometry(decision, geometry);
		MadVRShaderLoader::SetRuntimeNlsDecision(decision);
		if (decision.mode == MadVRNlsMappingMode::WAITING)
		{
			if (!m_shaderRuleApplicationPending &&
				m_nlsMappingMode == MadVRNlsMappingMode::WAITING)
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
			unsigned long desiredAspectX = 0;
			unsigned long desiredAspectY = 0;
			MadVRShaderLoader::GetRuntimeOutputAspectRatio(
				desiredAspectX, desiredAspectY);
			MadVRShaderSelection waitingSelection;
			if (!ApplyConfiguredShaderRuleCoherently(
					std::string(requestedUtf8), false,
					desiredAspectX, desiredAspectY,
					rendererRestartRequired, waitingSelection))
			{
				activeRule = m_activeShaderRule;
				return true;
			}
			UpdateActiveShaderSelection(waitingSelection);
			m_shaderRuleApplicationPending = false;
			UpdateNlsOsdMode(MadVRNlsMappingMode::WAITING);
			m_requestedShaderApplied = false;
			m_appliedShaderAspectRatio = 0.0;
			m_appliedActivePictureGeneration = 0;
			m_appliedViewportGeneration = m_viewportGeneration;
			activeRule = m_activeShaderRule;
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
		const bool mappingChanged = m_shaderRuleApplicationPending ||
			decision.mode != m_nlsMappingMode ||
			!m_requestedShaderApplied ||
			std::abs(activeAspectRatio - m_appliedShaderAspectRatio) > 0.01 ||
			activeRectangle.generation != m_appliedActivePictureGeneration ||
			m_viewportGeneration != m_appliedViewportGeneration;
		if (!mappingChanged)
			return false;

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
		unsigned long desiredAspectX = 0;
		unsigned long desiredAspectY = 0;
		MadVRShaderLoader::GetRuntimeOutputAspectRatio(
			desiredAspectX, desiredAspectY);
		MadVRShaderSelection selection;
		if (!ApplyConfiguredShaderRuleCoherently(
				std::string(requestedUtf8), false,
				desiredAspectX, desiredAspectY,
				rendererRestartRequired, selection))
		{
			activeRule = m_activeShaderRule;
			return true;
		}
		UpdateActiveShaderSelection(selection);
		m_shaderRuleApplicationPending = false;
		m_nlsTargetAspect = decision.targetAspect;
		UpdateNlsOsdMode(decision.mode);
		m_requestedShaderApplied = true;
		m_appliedShaderAspectRatio = activeAspectRatio;
		m_appliedActivePictureGeneration = activeRectangle.generation;
		m_appliedViewportGeneration = m_viewportGeneration;
		activeRule = m_activeShaderRule;
		rendererRestartRequired = DoesOutputAspectRequireRestart(
			selection.outputAspectRatioX, selection.outputAspectRatioY);
		if (rendererRestartRequired &&
			TryDynamicOutputAspect(selection.outputAspectRatioX,
				selection.outputAspectRatioY))
			rendererRestartRequired = false;
		if (rendererRestartRequired)
			MadVRShaderLoader::
				PrepareNlsOutputContractRendererReplacement();
		DebugLog::Log(
			"Shaders: NLS backend=madvr mapping change requested=%s effective=%s mapping=%s rect=%d,%d-%d,%d active_generation=%llu source=%.4f target=%.4f requested_ratio=%.5f max_ratio=%.5f axis=%s geometry=%s renderer_generation=%llu reason=\"%s\" renderer_restart=%d",
			static_cast<const char*>(requestedUtf8),
			selection.ruleName.c_str(),
			NlsMappingModeName(decision.mode),
			activeRectangle.left, activeRectangle.top,
			activeRectangle.right, activeRectangle.bottom,
			static_cast<unsigned long long>(activeRectangle.generation),
			decision.sourceAspect, decision.targetAspect,
			decision.requestedRatio, decision.maximumRatio,
			NlsMappingAxisName(decision),
			usingFullRasterFallback ? "full-raster-fallback" : "detected-crop",
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
			m_viewportGeneration != m_appliedViewportGeneration);
	if (!m_shaderRuleApplicationPending &&
		shouldApply == m_requestedShaderApplied && !activeAspectChanged)
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

	unsigned long desiredAspectX = 0;
	unsigned long desiredAspectY = 0;
	MadVRShaderLoader::GetRuntimeOutputAspectRatio(
		desiredAspectX, desiredAspectY);
	MadVRShaderSelection selection;
	if (!ApplyConfiguredShaderRuleCoherently(
			ruleToApply, shouldApply, desiredAspectX, desiredAspectY,
			rendererRestartRequired, selection))
	{
		activeRule = m_activeShaderRule;
		return true;
	}
	UpdateActiveShaderSelection(selection);
	m_shaderRuleApplicationPending = false;
	m_requestedShaderApplied = shouldApply;
	m_appliedShaderAspectRatio = shouldApply ? activeAspectRatio : 0.0;
	m_appliedActivePictureGeneration = shouldApply ? activeRectangle.generation : 0;
	m_appliedViewportGeneration = m_viewportGeneration;
	if (!shouldApply)
		m_activeShaderRule.Format(TEXT("%s (Waiting)"),
			static_cast<LPCTSTR>(m_requestedShaderLabel));
	activeRule = m_activeShaderRule;
	rendererRestartRequired = DoesOutputAspectRequireRestart(
		selection.outputAspectRatioX, selection.outputAspectRatioY);
	if (rendererRestartRequired && TryDynamicOutputAspect(
		selection.outputAspectRatioX, selection.outputAspectRatioY))
		rendererRestartRequired = false;
	DebugLog::Log("Shaders: armed rule \"%s\" %s at active aspect %.4f%s%s",
		static_cast<const char*>(requestedUtf8),
		shouldApply ? "engaged" : "bypassed",
		activeAspectRatio,
		shouldApply ? "" : "; ",
		shouldApply ? "" : reason.c_str());
	return true;
}

bool DirectShowGenericHDRVideoRenderer::ApplyApplicationState(
	const UnifiedProfileRuntime::Snapshot& snapshot,
	CString& activeState,
	bool& rendererRestartRequired,
	bool& liveResetRequired)
{
	liveResetRequired = false;
	if (!IsGraphThread())
	{
		const UnifiedProfileRuntime::Snapshot state(snapshot);
		activeState.Format(TEXT("Viewport: %S (%S, alignment %S%s)"),
			snapshot.viewport.profile.c_str(),
			snapshot.viewport.hasScreenAspect ?
				snapshot.viewport.screenAspect.Canonical().c_str() :
				"native; NLS target unavailable",
			snapshot.viewport.verticalAlignment.c_str(),
			snapshot.viewport.verticalAlignment == "center" ? "" : " unsupported");
		rendererRestartRequired = false;
		return PostCoalescedGraphCommand(
			GRAPH_COMMAND_APPLICATION_STATE,
			[this, state]()
			{
				CString appliedState;
				bool restartRequired = false;
				bool resetRequired = false;
				ApplyApplicationState(
					state, appliedState, restartRequired, resetRequired);
				if (restartRequired)
					QueueRendererRestartCompletion();
			});
	}
	AssertGraphThread();

	activeState.Empty();
	rendererRestartRequired = false;
	const RendererProfileConfig::ResolvedViewport& viewport =
		snapshot.viewport;
	// madVR owns the physical display and exposes no reliable panel geometry to
	// VP. An explicit application viewport is therefore required; absence means
	// native geometry rather than the former hard-coded 16:9 assumption.
	m_nlsTargetAspect = viewport.hasScreenAspect ?
		viewport.screenAspect.value : 0.0;
	MadVRShaderLoader::SetRuntimeNlsTargetAspect(m_nlsTargetAspect);
	ApplyVideoWindowPlacement();
	m_viewportGeneration = viewport.generation;
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
	activeState.Format(TEXT("Viewport: %S (%S, alignment %S%s)"),
		viewport.profile.c_str(),
		viewport.hasScreenAspect ?
			viewport.screenAspect.Canonical().c_str() :
			"native; NLS target unavailable",
		viewport.verticalAlignment.c_str(),
		viewport.verticalAlignment == "center" ? "" : " unsupported");
	DebugLog::Log(
		"DirectShow application viewport profile=%s target=%s numeric=%.7f explicit=%d vertical_alignment=%s placement_supported=%d automatic_crop=%d subtitle_fit=%d subtitle_hold_ms=%llu subtitle_padding=%d generation=%llu renderer_restart=%d",
		viewport.profile.c_str(),
		viewport.hasScreenAspect ?
			viewport.screenAspect.Canonical().c_str() : "unavailable",
		m_nlsTargetAspect,
		viewport.hasScreenAspect ? 1 : 0,
		viewport.verticalAlignment.c_str(),
		viewport.verticalAlignment == "center" ? 1 : 0,
		viewport.automaticCrop ? 1 : 0,
		viewport.subtitleFit ? 1 : 0,
		static_cast<unsigned long long>(
			viewport.subtitleHoldMilliseconds),
		viewport.subtitlePaddingPixels,
		static_cast<unsigned long long>(m_viewportGeneration),
		rendererRestartRequired ? 1 : 0);
	return true;
}


void DirectShowGenericHDRVideoRenderer::ResolveVideoWindowPlacement(
	LONG hostWidth, LONG hostHeight, bool fullscreen, LONG& x, LONG& y,
	LONG& width, LONG& height) const
{
	const DirectShowViewportPlacement placement =
		ResolveDirectShowViewportPlacement(hostWidth, hostHeight, fullscreen,
			m_requestedRuleUsesNlsMapping, m_nlsTargetAspect);
	x = placement.x;
	y = placement.y;
	width = placement.width;
	height = placement.height;
	if (placement.usesWindowedScopeCanvas)
	{
		DebugLog::Log(
			"DirectShow windowed NLS scope canvas host=%ldx%ld rect=%ld,%ld,%ld,%ld target=%.7f",
			hostWidth, hostHeight, x, y, width, height, m_nlsTargetAspect);
	}
}


void DirectShowGenericHDRVideoRenderer::LiveSourceBuildAndConnect()
{
	AssertGraphThread();
	DirectShowVideoRenderer::LiveSourceBuildAndConnect();

	if (m_videoState->hdrData)
		m_liveSource->OnHDRData(m_videoState->hdrData);
}
