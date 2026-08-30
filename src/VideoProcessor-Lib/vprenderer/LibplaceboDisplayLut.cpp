#include "LibplaceboDisplayLut.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <sstream>
#include <vector>
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

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
		if (written == 0 || written >= result.size())
			return std::string();
		std::string path(result.data(), written);
		if (path.rfind("\\\\?\\UNC\\", 0) == 0)
			path = "\\\\" + path.substr(8);
		else if (path.rfind("\\\\?\\", 0) == 0)
			path.erase(0, 4);
		std::replace(path.begin(), path.end(), '/', '\\');
		std::transform(path.begin(), path.end(), path.begin(),
			[](unsigned char character)
			{ return static_cast<char>(std::tolower(character)); });
		return path;
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

	bool ComputeSha256(const std::string& contents, std::string& digest)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		DWORD objectBytes = 0;
		DWORD digestBytes = 0;
		DWORD returned = 0;
		std::vector<unsigned char> object;
		std::vector<unsigned char> bytes;
		bool succeeded = false;

		if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
			&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
			goto done;
		if (!BCRYPT_SUCCESS(BCryptGetProperty(
			algorithm, BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
			&returned, 0)))
			goto done;
		if (!BCRYPT_SUCCESS(BCryptGetProperty(
			algorithm, BCRYPT_HASH_LENGTH,
			reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes),
			&returned, 0)))
			goto done;
		object.resize(objectBytes);
		bytes.resize(digestBytes);
		if (!BCRYPT_SUCCESS(BCryptCreateHash(
			algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0)))
			goto done;
		if (!BCRYPT_SUCCESS(BCryptHashData(
			hash,
			reinterpret_cast<PUCHAR>(const_cast<char*>(contents.data())),
			static_cast<ULONG>(contents.size()), 0)))
			goto done;
		if (!BCRYPT_SUCCESS(BCryptFinishHash(
			hash, bytes.data(), digestBytes, 0)))
			goto done;

		{
			static constexpr char HEX[] = "0123456789abcdef";
			digest.resize(bytes.size() * 2);
			for (size_t index = 0; index < bytes.size(); ++index)
			{
				digest[index * 2] = HEX[bytes[index] >> 4];
				digest[index * 2 + 1] = HEX[bytes[index] & 0x0f];
			}
		}
		succeeded = true;

	done:
		if (hash) BCryptDestroyHash(hash);
		if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
		if (!succeeded) digest.clear();
		return succeeded;
	}

	bool ContainsNonDefaultDomain(const std::string& contents)
	{
		std::istringstream input(contents);
		std::string line;
		while (std::getline(input, line))
		{
			std::istringstream fields(line);
			std::string keyword;
			fields >> keyword;
			if (keyword != "DOMAIN_MIN" && keyword != "DOMAIN_MAX")
				continue;

			double values[3] = {};
			if (!(fields >> values[0] >> values[1] >> values[2]))
				continue; // The authoritative Cube parser rejects malformed input.
			const double expected = keyword == "DOMAIN_MIN" ? 0.0 : 1.0;
			if (values[0] != expected || values[1] != expected ||
				values[2] != expected)
			{
				return true;
			}
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

		result.canonicalPath = FinalPathForHandle(input.Get());
		if (result.canonicalPath.empty())
		{
			result.status = Status::REJECTED;
			result.rejection = Rejection::PATH_IDENTITY_FAILED;
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
			if (finalBase.empty() ||
				!IsPathWithin(finalBase, result.canonicalPath))
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
			if (!ComputeSha256(contents, result.contentSha256))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::HASH_FAILED;
				return result;
			}
			if (ContainsOneDimensionalCubeDirective(contents))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::ONE_DIMENSIONAL;
				return result;
			}
			// VP's v1 calibration contract is normalized RGB in/out. The bundled
			// libplacebo parser treats Cube DOMAIN values as an output rescale,
			// while the Adobe Cube specification defines them as the input domain.
			// Reject that unsupported semantic instead of applying the wrong LUT.
			if (ContainsNonDefaultDomain(contents))
			{
				result.status = Status::REJECTED;
				result.rejection = Rejection::UNSUPPORTED_DOMAIN;
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
		case Rejection::PATH_IDENTITY_FAILED: return "path identity failed";
		case Rejection::HASH_FAILED: return "hash failed";
		case Rejection::PATH_OUTSIDE_BASE: return "bad path";
		case Rejection::INVALID_CUBE: return "invalid cube";
		case Rejection::ONE_DIMENSIONAL: return "1D not supported";
		case Rejection::UNSUPPORTED_DOMAIN: return "domain unsupported";
		case Rejection::UNSAFE_DIMENSIONS: return "unsupported size";
		default: return "";
		}
	}

}
