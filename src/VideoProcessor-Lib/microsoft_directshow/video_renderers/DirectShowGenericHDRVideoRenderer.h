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
	bool SetScreenProfile(bool scopeScreen, CString& activeProfile,
		bool& rendererRestartRequired) override;

protected:

	// DirectShowVideoRenderer
	void RendererBuild() override;
	void MediaTypeGenerate() override;
	void RendererConnect() override;
	void LiveSourceBuildAndConnect() override;

private:
	void UpdateActiveShaderSelection(const MadVRShaderSelection& selection);
	void RestoreRuntimeShaderRequest();
	void UpdateNlsOsdMode(MadVRNlsMappingMode mode);
	bool DoesOutputAspectRequireRestart(unsigned long desiredAspectX,
		unsigned long desiredAspectY) const;
	MadVRActivePictureGeometry MakeRuntimeGeometry(
		const ActivePictureRectangle& rectangle) const;

	const GUID m_rendererCLSID;
	const DXVA_NominalRange m_forceNominalRange;
	const DXVA_VideoTransferFunction m_forceVideoTransferFunction;
	const DXVA_VideoTransferMatrix m_forceVideoTransferMatrix;
	const DXVA_VideoPrimaries m_forceVideoPrimaries;
	CString m_activeShaderRule = TEXT("None");
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
	uint64_t m_screenProfileGeneration = 0;
	uint64_t m_appliedScreenProfileGeneration = 0;
};
