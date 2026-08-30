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
		UNSUPPORTED_DOMAIN,
		UNSAFE_DIMENSIONS
	};

	struct LoadResult
	{
		pl_custom_lut* lut = nullptr;
		Status status = Status::DISABLED;
		Rejection rejection = Rejection::NONE;
		size_t fileBytes = 0;
	};

	// When constrainedBaseDirectory is supplied, the file opened at path must
	// resolve below that directory. Validation and reading use the same handle.
	LoadResult Load(
		pl_log log,
		const std::string& path,
		const std::string& constrainedBaseDirectory = std::string());
	const char* ShortReason(Rejection rejection);
}
