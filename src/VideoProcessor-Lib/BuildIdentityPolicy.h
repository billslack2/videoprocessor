#pragma once

#include <string>

namespace BuildIdentityPolicy
{
	inline std::wstring Trim(const std::wstring& value)
	{
		const size_t first = value.find_first_not_of(L" \t\r\n");
		if (first == std::wstring::npos)
			return {};
		const size_t last = value.find_last_not_of(L" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	inline std::wstring Format(const std::wstring& branch,
		const std::wstring& shortCommit, const std::wstring& fallbackVersion)
	{
		const std::wstring cleanBranch = Trim(branch);
		const std::wstring cleanCommit = Trim(shortCommit);
		const std::wstring cleanFallback = Trim(fallbackVersion);
		const std::wstring identity = !cleanBranch.empty() ? cleanBranch :
			(!cleanFallback.empty() ? cleanFallback : L"detached");
		return cleanCommit.empty() ? identity :
			identity + L" @ " + cleanCommit;
	}
}
