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

	static uint32_t ArmPostSwapRuleReapply(
		bool acceptedRendererDiffers, uint32_t successorGeneration)
	{
		return acceptedRendererDiffers ? successorGeneration : 0;
	}

	static bool ConsumePostSwapRuleReapply(
		uint32_t& pendingGeneration,
		uint32_t messageGeneration,
		uint32_t currentGeneration,
		bool rendererPresent,
		bool successorRunning)
	{
		if (!successorRunning || pendingGeneration == 0 ||
			pendingGeneration != messageGeneration ||
			!Accept(messageGeneration, currentGeneration, rendererPresent))
		{
			return false;
		}

		pendingGeneration = 0;
		return true;
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
