/*
 * Copyright(C) 2026 Bill Slack
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <microsoft_directshow/MadVRRuntimeInterfaces.h>
#include <NlsGeometryPolicy.h>
#include <cstdint>

// The published madVR interface defines videoCropRect in originalVideoSize
// coordinates. Never use arAdjustedVideoSize or output rectangles here: the
// NLS output DAR changes them and would feed our own stretch back as input.
inline NlsSourceGeometry ReadMadVRNlsSourceGeometry(IMadVRInfo* info,
	int rasterWidth, int rasterHeight)
{
	if (!info || rasterWidth <= 0 || rasterHeight <= 0)
		return {};
	SIZE before{}, after{};
	RECT crop{}, check{};
	if (info->GetSize("originalVideoSize", &before) != S_OK ||
		info->GetRect("videoCropRect", &crop) != S_OK ||
		info->GetSize("originalVideoSize", &after) != S_OK ||
		info->GetRect("videoCropRect", &check) != S_OK ||
		before.cx != rasterWidth || before.cy != rasterHeight ||
		after.cx != before.cx || after.cy != before.cy ||
		crop.left != check.left || crop.top != check.top ||
		crop.right != check.right || crop.bottom != check.bottom)
		return {};
	return ResolveNlsSourceGeometry(true, crop.left, crop.top,
		crop.right, crop.bottom, rasterWidth, rasterHeight);
}

// Graph-owner state only. madVR owns detection and its temporal stabilization;
// VP tracks changes without inventing a fallback or retaining withdrawn bounds.
class MadVRNlsSourceGeometryState
{
public:
	uint64_t Update(const NlsSourceGeometry& geometry)
	{
		if (geometry.valid != m_geometry.valid ||
			(geometry.valid && (geometry.left != m_geometry.left ||
				geometry.top != m_geometry.top ||
				geometry.right != m_geometry.right ||
				geometry.bottom != m_geometry.bottom ||
				geometry.aspect != m_geometry.aspect)))
			++m_generation;
		m_geometry = geometry;
		return m_generation;
	}
	void Reset()
	{
		m_geometry = {};
		++m_generation;
	}
private:
	NlsSourceGeometry m_geometry;
	uint64_t m_generation = 0;
};
