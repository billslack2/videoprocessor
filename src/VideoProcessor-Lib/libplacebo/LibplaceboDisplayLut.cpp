#include "LibplaceboDisplayLut.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <new>
#include <vector>
#include <windows.h>

namespace LibplaceboDisplayLut
{
	class ScopedHandle
	{
	public:
		explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
		~ScopedHandle()
		{
			if (m_handle != INVALID_HANDLE_VALUE)
				CloseHandle(m_handle);
		}

		HANDLE Get() const { return m_handle; }

	private:
		HANDLE m_handle;
	};

	std::string FinalPathForHandle(HANDLE handle)
	{
		const DWORD required = GetFinalPathNameByHandleA(
			handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (required == 0)
			return std::string();
		std::vector<char> result(static_cast<size_t>(required) + 1);
		const DWORD written = GetFinalPathNameByHandleA(
			handle, result.data(), static_cast<DWORD>(result.size()),
			FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		return written == 0 || written >= result.size()
			? std::string()
			: std::string(result.data(), written);
	}

	std::string NormalizePathForComparison(std::string value)
	{
		std::replace(value.begin(), value.end(), '/', '\\');
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	bool IsPathWithin(const std::string& base, const std::string& candidate)
	{
		std::string basePrefix = NormalizePathForComparison(base);
		if (basePrefix.empty())
			return false;
		if (basePrefix.back() != '\\')
			basePrefix.push_back('\\');
		const std::string normalizedCandidate =
			NormalizePathForComparison(candidate);
		return normalizedCandidate.compare(
			0, basePrefix.size(), basePrefix) == 0;
	}

	bool ContainsOneDimensionalCubeDirective(const std::string& contents)
	{
		constexpr char directive[] = "LUT_1D_SIZE";
		size_t lineStart = 0;
		while (lineStart < contents.size())
		{
			const size_t lineEnd = contents.find_first_of("\r\n", lineStart);
			size_t cursor = lineStart;
			while (cursor < contents.size() &&
				(cursor == lineEnd ||
				 std::isspace(static_cast<unsigned char>(contents[cursor]))))
			{
				++cursor;
			}
			if (cursor < contents.size() && contents[cursor] != '#')
			{
				size_t index = 0;
				while (index + 1 < sizeof(directive) &&
					cursor + index < contents.size() &&
					cursor + index != lineEnd &&
					std::toupper(static_cast<unsigned char>(contents[cursor + index])) ==
					directive[index])
				{
					++index;
				}
				if (index + 1 == sizeof(directive) &&
					(cursor + index == lineEnd ||
					 cursor + index >= contents.size() ||
					 std::isspace(static_cast<unsigned char>(contents[cursor + index]))))
				{
					return true;
				}
			}

			if (lineEnd == std::string::npos)
				break;
			lineStart = lineEnd + 1;
		}
		return false;
	}

	LoadResult Load(
		pl_log log,
		const std::string& path,
		const std::string& constrainedBaseDirectory)
	{
		LoadResult result;
		if (path.empty())
			return result;

		const ScopedHandle input(CreateFileA(
			path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (input.Get() == INVALID_HANDLE_VALUE)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::UNREADABLE;
			return result;
		}

		if (!constrainedBaseDirectory.empty())
		{
			const ScopedHandle base(CreateFileA(
				constrainedBaseDirectory.c_str(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
			const std::string finalBase =
				base.Get() == INVALID_HANDLE_VALUE ? std::string() : FinalPathForHandle(base.Get());
			const std::string finalPath = FinalPathForHandle(input.Get());
			if (finalBase.empty() || finalPath.empty() ||
				!IsPathWithin(finalBase, finalPath))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::PATH_OUTSIDE_BASE;
				return result;
			}
		}

		LARGE_INTEGER length{};
		if (!GetFileSizeEx(input.Get(), &length) || length.QuadPart < 0)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::READ_FAILED;
			return result;
		}
		if (length.QuadPart == 0)
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::EMPTY;
			return result;
		}
		if (static_cast<unsigned long long>(length.QuadPart) > MAX_FILE_BYTES ||
			static_cast<unsigned long long>(length.QuadPart) >
				static_cast<unsigned long long>(
					(std::numeric_limits<size_t>::max)()))
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::TOO_LARGE;
			return result;
		}

		result.fileBytes = static_cast<size_t>(length.QuadPart);
		try
		{
			std::string contents(result.fileBytes, '\0');
			DWORD bytesRead = 0;
			if (!ReadFile(
				input.Get(), &contents[0], static_cast<DWORD>(contents.size()),
				&bytesRead, nullptr) || bytesRead != contents.size())
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::READ_FAILED;
				return result;
			}
			if (ContainsOneDimensionalCubeDirective(contents))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::ONE_DIMENSIONAL;
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
		case Rejection::PATH_OUTSIDE_BASE: return "bad path";
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
