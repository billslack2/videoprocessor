#pragma once

#include <regex>
#include <string>
#include <vector>

// Ctrl+1..4 select the DeckLink input whose name carries the "(n)" index.
// A single-input card names no index at all; Ctrl+n then selects the first
// DeckLink. Returns the list index to select, or -1 to leave the selection
// alone: no DeckLink listed, or a card that indexes its inputs but has no (n).
inline int DeckLinkShortcutIndex(const std::vector<std::wstring>& deviceNames, int n)
{
    const std::wregex exact(L".*?DeckLink.*?\\(" + std::to_wstring(n) + L"\\)",
        std::regex_constants::icase);
    for (size_t i = 0; i < deviceNames.size(); ++i)
        if (std::regex_search(deviceNames[i], exact))
            return static_cast<int>(i);

    const std::wregex indexed(L"DeckLink.*?\\(\\d+\\)", std::regex_constants::icase);
    const std::wregex deckLink(L"DeckLink", std::regex_constants::icase);
    int firstDeckLink = -1;
    for (size_t i = 0; i < deviceNames.size(); ++i)
    {
        if (std::regex_search(deviceNames[i], indexed))
            return -1;
        if (firstDeckLink < 0 && std::regex_search(deviceNames[i], deckLink))
            firstDeckLink = static_cast<int>(i);
    }
    return firstDeckLink;
}
