#pragma once

#include <dxgi.h>
#include <vector>

// DXGI can change the mode count between the size and data queries during
// a display transition. Publish only a complete list, with bounded retries.
template <typename Mode, typename Query>
HRESULT ReadDisplayModeList(std::vector<Mode>& modes, Query query, unsigned& attempts)
{
	modes.clear();
	attempts = 0;
	while (attempts < 3)
	{
		++attempts;
		UINT count = 0;
		HRESULT result = query(&count, nullptr);
		if (result == DXGI_ERROR_MORE_DATA) continue;
		if (FAILED(result)) return result;
		if (count > 16384) return E_UNEXPECTED;
		if (count == 0) return S_OK;
		std::vector<Mode> candidate(count);
		result = query(&count, candidate.data());
		if (result == DXGI_ERROR_MORE_DATA) continue;
		if (FAILED(result)) return result;
		if (count > candidate.size()) return E_UNEXPECTED;
		candidate.resize(count);
		modes.swap(candidate);
		return result;
	}
	return DXGI_ERROR_MORE_DATA;
}
