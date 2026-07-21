/*
 * Copyright(C) 2026 Bill Slack
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 */

#pragma once

#include <Unknwn.h>


// Public renderer interface for installing external Direct3D 9 HLSL shaders.
// The IID and method layout follow the renderer's published SDK interface.
#define MADVR_SHADER_STAGE_PRE_SCALE 0
#define MADVR_SHADER_STAGE_POST_SCALE 1

DECLARE_INTERFACE_IID_(IMadVRExternalPixelShaders, IUnknown,
	"B6A6D5D4-9637-4C7D-AAAE-BC0B36F5E433")
{
	STDMETHOD(ClearPixelShaders)(int stage) = 0;
	STDMETHOD(AddPixelShader)(LPCSTR sourceCode, LPCSTR compileProfile,
		int stage, LPVOID reserved) = 0;
};
