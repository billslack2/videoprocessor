#pragma once

#include <cstddef>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4244) // conversion warning in an upstream inline helper
#include <libplacebo/log.h>
#include <libplacebo/shaders/lut.h>
#pragma warning(pop)

namespace LibplaceboDisplayLut
{
	constexpr size_t MAX_FILE_BYTES = 64u * 1024u * 1024u;
	constexpr int MAX_3D_SIZE = 128;

	enum class Status
	{
		DISABLED,
		ACTIVE,
		REJECTED
	};

	enum class Rejection
	{
		NONE,
		UNREADABLE,
		EMPTY,
		TOO_LARGE,
		READ_FAILED,
		PATH_OUTSIDE_BASE,
		INVALID_CUBE,
		ONE_DIMENSIONAL,
		UNSAFE_DIMENSIONS
	};

	struct LoadResult
	{
		pl_custom_lut* lut = nullptr;
		Status status = Status::DISABLED;
		Rejection rejection = Rejection::NONE;
		size_t fileBytes = 0;
	};

	enum class ContractRejection
	{
		NONE,
		OUTPUT_NOT_SIGNALED,
		P3_NOT_SUPPORTED,
		PROFILE_MISMATCH
	};

	// When constrainedBaseDirectory is supplied, the file opened at path must
	// resolve below that directory. Validation and reading use the same handle.
	LoadResult Load(
		pl_log log,
		const std::string& path,
		const std::string& constrainedBaseDirectory = std::string());
	const char* ShortReason(Rejection rejection);
	bool TargetMatchesSignal(
		enum pl_color_primaries targetPrimaries,
		enum pl_color_transfer targetTransfer,
		enum pl_color_levels targetRange,
		enum pl_color_primaries signalPrimaries,
		enum pl_color_transfer signalTransfer,
		enum pl_color_levels signalRange);
	ContractRejection ValidateContract(
		enum pl_color_primaries requestedPrimaries,
		enum pl_color_transfer requestedTransfer,
		enum pl_color_levels requestedRange,
		double requestedNits,
		enum pl_color_primaries targetPrimaries,
		enum pl_color_transfer targetTransfer,
		enum pl_color_levels targetRange,
		double targetNits,
		bool targetMatchesSignaledOutput);
	const char* ShortReason(ContractRejection rejection);
}
