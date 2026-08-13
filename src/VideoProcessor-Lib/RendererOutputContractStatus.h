#pragma once

#include <cstdint>
#include <string>

namespace RendererOutputContract
{
	enum class Presentation
	{
		UNKNOWN,
		BITBLT,
		FLIP,
	};

	enum class Range
	{
		UNKNOWN,
		FULL,
		LIMITED,
	};

	enum class Transfer
	{
		UNKNOWN,
		SRGB,
		GAMMA22,
		GAMMA24,
	};

	enum class Primaries
	{
		UNKNOWN,
		REC709,
		BT2020,
	};

	struct Status
	{
		bool available = false;
		bool safeToRender = false;
		bool requestedContractActive = false;
		bool vpOwnsPresentation = false;
		bool dxgiAppliedVerified = false;
		bool strictContract = false;
		uint64_t successfulPresents = 0;
		uint32_t swapchainBitDepth = 0;
		Presentation presentation = Presentation::UNKNOWN;
		Range range = Range::UNKNOWN;
		Transfer transfer = Transfer::UNKNOWN;
		Primaries primaries = Primaries::UNKNOWN;
		std::string dxgiDeclaration;
		std::string swapchainFormat;
		std::string reason;
	};
}
