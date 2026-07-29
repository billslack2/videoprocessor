/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */


#include <pch.h>

#include "RendererId.h"
#include <algorithm>
#if defined(_WIN64)
#include <libplacebo/LibplaceboPluginVideoRenderer.h>
#endif


bool RendererId::operator< (const RendererId& other) const {
	return name < other.name;
}


std::vector<RendererId> RendererId::OrderForDisplay(
	const std::vector<RendererId>& rendererIds)
{
	std::vector<RendererId> externalRendererIds;
	std::vector<RendererId> alphaRendererIds;
	for (const auto& rendererId : rendererIds)
	{
		CString normalizedRendererName(rendererId.name);
		normalizedRendererName.MakeLower();
		if (normalizedRendererName.Find(TEXT("decklink")) >= 0)
			continue;

		if (rendererId.backend == RendererBackend::LIBPLACEBO)
			alphaRendererIds.push_back(rendererId);
		else
			externalRendererIds.push_back(rendererId);
	}

	std::sort(externalRendererIds.begin(), externalRendererIds.end());
	std::reverse(externalRendererIds.begin(), externalRendererIds.end());
	alphaRendererIds.insert(
		alphaRendererIds.end(),
		externalRendererIds.begin(),
		externalRendererIds.end());
	return alphaRendererIds;
}


bool RendererId::MatchesConfiguredName(const CString& configuredName) const
{
	return name.CompareNoCase(configuredName) == 0 ||
		(backend == RendererBackend::LIBPLACEBO &&
		 configuredName.CompareNoCase(TEXT("libplacebo")) == 0);
}


RendererId RendererId::Libplacebo()
{
	RendererId id;
	id.name = TEXT("VideoProcessor Renderer (Alpha)");
	id.backend = RendererBackend::LIBPLACEBO;
	return id;
}


bool RendererId::IsLibplaceboAvailable()
{
#if defined(_WIN64)
	return LibplaceboPluginVideoRenderer::IsAvailable();
#else
	return false;
#endif
}
