#pragma once

#include <cwctype>
#include <string>

namespace ModernOperatorStatusPolicy
{
	inline std::wstring NormalizeCapturedValue(const std::wstring& value)
	{
		size_t first = 0;
		while (first < value.size() && iswspace(value[first]))
			++first;
		size_t last = value.size();
		while (last > first && iswspace(value[last - 1]))
			--last;
		const std::wstring trimmed = value.substr(first, last - first);
		if (trimmed.empty() ||
			(trimmed.size() >= 2 && trimmed.front() == L'<' &&
				trimmed.back() == L'>'))
			return L"---";
		return std::wstring(trimmed);
	}
}
