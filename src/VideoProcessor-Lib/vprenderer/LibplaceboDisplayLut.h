#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#pragma warning(push)
#pragma warning(disable: 4244) // conversion warning in an upstream inline helper
#include <libplacebo/log.h>
#include <libplacebo/shaders/lut.h>
#pragma warning(pop)

namespace LibplaceboDisplayLut
{
	constexpr size_t MAX_FILE_BYTES = 64u * 1024u * 1024u;
	// Match the interoperable madVR calibration range. Larger tables are not
	// useful for this path and can create disproportionate GPU allocations.
	constexpr int MAX_3D_SIZE = 65;

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

	struct FileVersion
	{
		bool available = false;
		uint64_t fileBytes = 0;
		uint64_t fileWriteTime = 0;
	};

	struct LoadResult
	{
		pl_custom_lut* lut = nullptr;
		Status status = Status::DISABLED;
		Rejection rejection = Rejection::NONE;
		size_t fileBytes = 0;
		uint64_t fileWriteTime = 0;
		// Captured from the same handle whose bytes were parsed. Callers must use
		// this observation, rather than probing the path again, when deciding
		// whether a failed transactional reload needs another attempt.
		FileVersion fileVersion;
	};

	// When constrainedBaseDirectory is supplied, the file opened at path must
	// resolve below that directory. Validation and reading use the same handle.
	LoadResult Load(
		pl_log log,
		const std::string& path,
		const std::string& constrainedBaseDirectory = std::string());
	FileVersion ProbeFileVersion(const std::string& path);
	bool SameFileVersion(const FileVersion& left, const FileVersion& right);
	const char* ShortReason(Rejection rejection);
}
