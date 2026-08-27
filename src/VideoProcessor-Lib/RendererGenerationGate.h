#pragma once

#include <cstdint>
#include <string>


class RendererGenerationGate
{
public:
	static bool Accept(
		uint32_t messageGeneration,
		uint32_t currentGeneration,
		bool rendererPresent)
	{
		return rendererPresent && messageGeneration != 0 &&
			messageGeneration == currentGeneration;
	}

	static bool AcceptRetiringRefreshRestored(
		const std::string& event, uint32_t messageGeneration,
		uint32_t retiringGeneration)
	{
		return event == "refresh.restored" && messageGeneration != 0 &&
			messageGeneration == retiringGeneration;
	}

	static bool MatchesRendererTarget(const std::string& renderer,
		int actionSelectorIndex, int rendererSelectorIndex,
		bool alphaRenderer)
	{
		if (renderer == "*")
			return true;
		if (renderer == "vprenderer")
			return alphaRenderer;
		return rendererSelectorIndex > 0 &&
			actionSelectorIndex == rendererSelectorIndex;
	}
};
