#include "LibplaceboDisplayLut.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>

namespace LibplaceboDisplayLut
{
	LoadResult Load(pl_log log, const std::string& path)
	{
		LoadResult result;
		if (path.empty())
			return result;

		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::UNREADABLE;
			return result;
		}

		const std::streamoff length = input.tellg();
		if (length <= 0)
		{
			result.status = Status::REJECTED;
			result.rejection =
				length == 0 ? Rejection::EMPTY : Rejection::READ_FAILED;
			return result;
		}
		if (static_cast<unsigned long long>(length) > MAX_FILE_BYTES ||
			static_cast<unsigned long long>(length) >
				static_cast<unsigned long long>(
					std::numeric_limits<size_t>::max()))
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::TOO_LARGE;
			return result;
		}

		result.fileBytes = static_cast<size_t>(length);
		try
		{
			std::string contents(result.fileBytes, '\0');
			input.seekg(0, std::ios::beg);
			if (!input.read(&contents[0], static_cast<std::streamsize>(contents.size())))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::READ_FAILED;
				return result;
			}

			result.lut =
				pl_lut_parse_cube(log, contents.data(), contents.size());
		}
		catch (const std::bad_alloc&)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::TOO_LARGE;
			return result;
		}

		if (!result.lut)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::INVALID_CUBE;
			return result;
		}

		if (result.lut->size[0] > 0 &&
			result.lut->size[1] == 0 &&
			result.lut->size[2] == 0)
		{
			pl_lut_free(&result.lut);
			result.status = Status::REJECTED;
			result.rejection = Rejection::ONE_DIMENSIONAL;
			return result;
		}

		if (result.lut->size[0] != result.lut->size[1] ||
			result.lut->size[1] != result.lut->size[2])
		{
			pl_lut_free(&result.lut);
			result.status = Status::REJECTED;
			result.rejection = Rejection::UNSAFE_DIMENSIONS;
			return result;
		}

		for (int dimension : result.lut->size)
		{
			if (dimension < 2 || dimension > MAX_3D_SIZE)
			{
				pl_lut_free(&result.lut);
				result.status = Status::REJECTED;
				result.rejection = Rejection::UNSAFE_DIMENSIONS;
				return result;
			}
		}

		result.status = Status::ACTIVE;
		return result;
	}

	const char* ShortReason(Rejection rejection)
	{
		switch (rejection)
		{
		case Rejection::UNREADABLE: return "file unavailable";
		case Rejection::EMPTY: return "empty file";
		case Rejection::TOO_LARGE: return "file too large";
		case Rejection::READ_FAILED: return "read failed";
		case Rejection::INVALID_CUBE: return "invalid cube";
		case Rejection::ONE_DIMENSIONAL: return "1D not supported";
		case Rejection::UNSAFE_DIMENSIONS: return "unsupported size";
		default: return "";
		}
	}

	bool TargetMatchesSignal(
		enum pl_color_primaries targetPrimaries,
		enum pl_color_transfer targetTransfer,
		enum pl_color_levels targetRange,
		enum pl_color_primaries signalPrimaries,
		enum pl_color_transfer signalTransfer,
		enum pl_color_levels signalRange)
	{
		return signalPrimaries != PL_COLOR_PRIM_UNKNOWN &&
			signalTransfer != PL_COLOR_TRC_UNKNOWN &&
			signalRange != PL_COLOR_LEVELS_UNKNOWN &&
			targetPrimaries == signalPrimaries &&
			targetTransfer == signalTransfer &&
			targetRange == signalRange;
	}

	ContractRejection ValidateContract(
		enum pl_color_primaries requestedPrimaries,
		enum pl_color_transfer requestedTransfer,
		enum pl_color_levels requestedRange,
		double requestedNits,
		enum pl_color_primaries targetPrimaries,
		enum pl_color_transfer targetTransfer,
		enum pl_color_levels targetRange,
		double targetNits,
		bool targetMatchesSignaledOutput)
	{
		if (!targetMatchesSignaledOutput)
			return ContractRejection::OUTPUT_NOT_SIGNALED;
		if (requestedPrimaries == PL_COLOR_PRIM_DISPLAY_P3)
			return ContractRejection::P3_NOT_SUPPORTED;
		if ((requestedPrimaries != PL_COLOR_PRIM_UNKNOWN &&
			 requestedPrimaries != targetPrimaries) ||
			(requestedTransfer != PL_COLOR_TRC_UNKNOWN &&
			 requestedTransfer != targetTransfer) ||
			(requestedRange != PL_COLOR_LEVELS_UNKNOWN &&
			 requestedRange != targetRange) ||
			(requestedNits > 0.0 &&
			 std::abs(requestedNits - targetNits) > 0.01))
		{
			return ContractRejection::PROFILE_MISMATCH;
		}
		return ContractRejection::NONE;
	}

	const char* ShortReason(ContractRejection rejection)
	{
		switch (rejection)
		{
		case ContractRejection::OUTPUT_NOT_SIGNALED:
			return "output not signaled";
		case ContractRejection::P3_NOT_SUPPORTED:
			return "P3 not supported";
		case ContractRejection::PROFILE_MISMATCH:
			return "profile mismatch";
		default:
			return "";
		}
	}
}
