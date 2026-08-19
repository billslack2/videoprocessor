#pragma once

#include <cstdint>


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
};
