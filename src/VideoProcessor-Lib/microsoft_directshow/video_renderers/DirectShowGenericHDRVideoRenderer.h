/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include "DirectShowVideoRenderer.h"
#include <microsoft_directshow/MadVRShaderLoader.h>
#include <microsoft_directshow/MadVROsdServices.h>
#include <atlbase.h>
#include <atomic>
#include <mutex>


/**
 * DirectShow HDR video renderer.
 *
 * Will try to build a direct VIDEOINFOHEADER2 connection.
 */
class DirectShowGenericHDRVideoRenderer :
	public DirectShowVideoRenderer
{
public:

	DirectShowGenericHDRVideoRenderer(
		GUID rendererCLSID,
		IRendererCallback& callback,
		HWND videoHwnd,
		HWND eventHwnd,
		UINT eventMsg,
		ITimingClock* timingClock,
		DirectShowStartStopTimeMethod directShowStartStopTimeMethod,
		bool useFrameQueue,
		size_t frameQueueMaxSize,
		VideoConversionOverride videoConversionOverride,
		DXVA_NominalRange forceNominalRange,
		DXVA_VideoTransferFunction forceVideoTransferFunction,
		DXVA_VideoTransferMatrix forceVideoTransferMatrix,
		DXVA_VideoPrimaries forceVideoPrimaries);

	virtual ~DirectShowGenericHDRVideoRenderer();

	// IVideoRenderer
	bool OnVideoState(VideoStateComPtr&) override;
	void OnPaint() override { /* not implemented */ }
	std::vector<CString> ActiveShaders() const override { return m_activeShaders; }
	CString ActiveShaderRule() const override { return m_activeShaderRule; }
	bool SelectShaderRule(const CString& ruleName, CString& activeRule,
		bool& rendererRestartRequired) override;
	bool RefreshShaderRule(CString& activeRule,
		bool& rendererRestartRequired) override;
	bool ApplyApplicationState(
		const UnifiedProfileRuntime::Snapshot& snapshot,
		CString& activeState,
		bool& rendererRestartRequired) override;
	bool SupportsNativeStatsOverlay() const override
	{
		return m_osdServices != nullptr && !m_osdFailureLogged;
	}
	bool SetNativeStatsOverlay(const uint8_t* pixels, size_t byteCount,
		int width, int height, int stride) override;

protected:

	// DirectShowVideoRenderer
	void RendererBuild() override;
	void MediaTypeGenerate() override;
	void RendererConnect() override;
	void LiveSourceBuildAndConnect() override;
	void ResolveVideoWindowPlacement(LONG hostWidth, LONG hostHeight,
		bool fullscreen, LONG& x, LONG& y, LONG& width,
		LONG& height) const override;

private:
	void UpdateActiveShaderSelection(const MadVRShaderSelection& selection);
	void RestoreRuntimeShaderRequest();
	void UpdateNlsOsdMode(MadVRNlsMappingMode mode);
	bool DoesOutputAspectRequireRestart(unsigned long desiredAspectX,
		unsigned long desiredAspectY) const;
	bool TryDynamicOutputAspect(
		unsigned long desiredAspectX, unsigned long desiredAspectY);
	bool PrepareOutputAspectForShaderInstall(
		unsigned long desiredAspectX, unsigned long desiredAspectY,
		bool& rendererRestartRequired);
	bool ApplyConfiguredShaderRuleCoherently(const std::string& ruleName,
		bool updateRuntimeRequest, unsigned long desiredAspectX,
		unsigned long desiredAspectY, bool& rendererRestartRequired,
		MadVRShaderSelection& selection);
	MadVRActivePictureGeometry MakeRuntimeGeometry(
		const ActivePictureRectangle& rectangle) const;
	bool ResolveNlsSourceRectangle(ActivePictureRectangle& rectangle,
		bool& usingFullRasterFallback) const;
	void ApplyNativeStatsOverlayOnGraphThread();
	void ClearNativeStatsOverlayOnGraphThread();

	const GUID m_rendererCLSID;
	const DXVA_NominalRange m_forceNominalRange;
	const DXVA_VideoTransferFunction m_forceVideoTransferFunction;
	const DXVA_VideoTransferMatrix m_forceVideoTransferMatrix;
	const DXVA_VideoPrimaries m_forceVideoPrimaries;
	CString m_activeShaderRule = TEXT("None");
	CString m_activeShaderCompanionLabel;
	std::vector<CString> m_activeShaders;
	unsigned long m_outputAspectRatioX = 0;
	unsigned long m_outputAspectRatioY = 0;
	CString m_requestedShaderRule;
	CString m_requestedShaderLabel;
	CString m_inactiveShaderRule;
	bool m_requestedRuleUsesNlsMapping = false;
	MadVRNlsMappingMode m_nlsMappingMode = MadVRNlsMappingMode::OFF;
	double m_nlsTargetAspect = 0.0;
	bool m_requestedShaderApplied = false;
	double m_appliedShaderAspectRatio = 0.0;
	uint64_t m_appliedActivePictureGeneration = 0;
	uint64_t m_rendererGeneration = 0;
	uint64_t m_viewportGeneration = 0;
	uint64_t m_appliedViewportGeneration = 0;
	CComPtr<IMadVROsdServices> m_osdServices;
	std::mutex m_osdMutex;
	std::vector<uint8_t> m_osdPixels;
	int m_osdWidth = 0;
	int m_osdHeight = 0;
	int m_osdStride = 0;
	HBITMAP m_osdBitmap = nullptr;
	std::atomic_bool m_osdFailureLogged{ false };
	bool m_hasLoggedOsdPlacement = false;
	RECT m_lastOsdFullRect{};
	RECT m_lastOsdActiveRect{};
	float m_lastOsdScale = 0.0f;
};
