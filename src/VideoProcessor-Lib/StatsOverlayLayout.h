#pragma once

#include <Windows.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// Shared by the native rasterizer and its tests. Values always start at one
// measured column; continuation rows share that column instead of drifting
// back to the panel's left edge.
namespace StatsOverlayLayout
{
// 62 Consolas columns at 9 px plus equal 10 px horizontal padding.
constexpr int Width = 578;
constexpr int Padding = 10;
constexpr int LineHeight = 20;
constexpr int SectionGap = 4;
constexpr int LabelColumns = 18;

struct Row
{
    std::wstring label;
    std::wstring value;
    int gapBefore = 0;
    bool wrap = false; // system diagnostics retain all values on continuation rows
};

inline int Height(const std::vector<Row>& rows)
{
    int height = Padding * 2;
    for (const auto& row : rows) height += row.gapBefore + LineHeight;
    return height;
}

inline int ValueX(HDC dc)
{
    const std::wstring columns(LabelColumns, L' ');
    SIZE extent{};
    return GetTextExtentPoint32W(dc, columns.c_str(), LabelColumns, &extent)
        ? Padding + extent.cx : Padding + LabelColumns * 9;
}

// Measure the selected production font, including Unicode/fallback glyphs.
// No fixed character limit can safely fit arbitrary operator-supplied names.
inline std::wstring FitText(HDC dc, const std::wstring& text, int availableWidth)
{
    if (text.empty() || availableWidth <= 0) return {};
    const auto fits = [&](const std::wstring& value) {
        SIZE extent{};
        return GetTextExtentPoint32W(dc, value.c_str(),
            static_cast<int>(value.size()), &extent) && extent.cx <= availableWidth;
    };
    if (fits(text)) return text;
    const std::wstring suffix = L"...";
    if (!fits(suffix)) return {};
    size_t low = 0, high = text.size();
    while (low < high)
    {
        const size_t middle = low + (high - low + 1) / 2;
        if (fits(text.substr(0, middle) + suffix)) low = middle;
        else high = middle - 1;
    }
    // Never split a UTF-16 surrogate pair at the ellipsis boundary.
    if (low && text[low - 1] >= 0xD800 && text[low - 1] <= 0xDBFF) --low;
    return text.substr(0, low) + suffix;
}
// Arrange after selecting the actual production font. Long system diagnostics
// wrap on word boundaries; operator-controlled labels retain a compact ellipsis
// policy. The returned rows are used unchanged for height and drawing.
inline std::vector<Row> Arrange(HDC dc, const std::vector<Row>& input)
{
    const int available = Width - Padding - ValueX(dc);
    std::vector<Row> rows;
    for (const auto& original : input)
    {
        Row row = original;
        std::wstring remaining = row.value;
        while (row.wrap && !remaining.empty() && FitText(dc, remaining, available) != remaining)
        {
            int count = 0;
            SIZE extent{};
            if (!GetTextExtentExPointW(dc, remaining.c_str(), static_cast<int>(remaining.size()),
                available, &count, nullptr, &extent) || count <= 0) break;
            size_t cut = remaining.find_last_of(L' ', static_cast<size_t>(count));
            if (cut == std::wstring::npos || cut == 0) cut = static_cast<size_t>(count);
            if (cut && remaining[cut - 1] >= 0xD800 && remaining[cut - 1] <= 0xDBFF) --cut;
            if (!cut) break;
            row.value = remaining.substr(0, cut);
            while (!row.value.empty() && row.value.back() == L' ') row.value.pop_back();
            rows.push_back(row);
            remaining.erase(0, cut);
            remaining.erase(0, remaining.find_first_not_of(L' '));
            row.label.clear(); row.gapBefore = 0;
        }
        row.value = remaining;
        if (!row.value.empty() || !row.label.empty() || original.value.empty())
            rows.push_back(std::move(row));
    }
    return rows;
}

}
