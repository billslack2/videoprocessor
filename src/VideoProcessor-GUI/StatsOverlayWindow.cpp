/*
 * Copyright(C) 2025 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <DisplayRasterInfo.h>
#include <OsdTimingPolicy.h>
#include "StatsOverlayWindow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

// Bitmap rasterizer only: the active renderer owns OSD presentation.
StatsOverlayWindow::StatsOverlayWindow()
	: m_font(nullptr), m_alphaFont(nullptr)
{
}

StatsOverlayWindow::~StatsOverlayWindow()
{
	Destroy();
}

bool StatsOverlayWindow::Initialize()
{
	if (m_font && m_alphaFont)
		return true;
	Destroy();
	m_font = CreateFont(LINE_HEIGHT, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	m_alphaFont = CreateFont(ALPHA_LINE_HEIGHT, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	if (m_font && m_alphaFont)
		return true;
	Destroy();
	return false;
}

void StatsOverlayWindow::Destroy()
{
	if (m_font)
	{
		DeleteObject(m_font);
		m_font = nullptr;
	}

	if (m_alphaFont)
	{
		DeleteObject(m_alphaFont);
		m_alphaFont = nullptr;
	}
}

void StatsOverlayWindow::UpdateStats(const StatsData& stats)
{
	std::lock_guard<std::mutex> lock(m_statsMutex);
	m_stats = stats;
}

bool StatsOverlayWindow::RenderBgra(
	std::vector<uint8_t>& pixels, int& width, int& height, int& stride)
{
	if (!Initialize())
		return false;
	StatsData stats;
	{
		std::lock_guard<std::mutex> lock(m_statsMutex);
		stats = m_stats;
	}
	HDC screen = GetDC(nullptr);
	HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
	if (!memory)
	{
		if (screen) ReleaseDC(nullptr, screen);
		return false;
	}
	HGDIOBJ oldFont = SelectObject(memory, stats.isAlphaRenderer ? m_alphaFont : m_font);
	const auto rows = StatsOverlayLayout::Arrange(memory, BuildStatsRows(stats));
	SelectObject(memory, oldFont);
	width = WINDOW_WIDTH;
	height = StatsOverlayLayout::Height(rows);
	stride = width * 4;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(
		screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, screen);
	if (!bitmap || !memory || !bits)
	{
		if (memory) DeleteDC(memory);
		if (bitmap) DeleteObject(bitmap);
		return false;
	}
	HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
	RECT rect{ 0, 0, width, height };
	HBRUSH brush = CreateSolidBrush(BACKGROUND_COLOR);
	FillRect(memory, &rect, brush);
	DeleteObject(brush);
	DrawStats(memory, rows, stats.isAlphaRenderer);
	SelectObject(memory, oldBitmap);
	pixels.assign(
		static_cast<uint8_t*>(bits),
		static_cast<uint8_t*>(bits) + static_cast<size_t>(stride) * height);
	for (size_t i = 3; i < pixels.size(); i += 4)
		pixels[i] = 220;
	DeleteDC(memory);
	DeleteObject(bitmap);
	return true;
}


bool StatsOverlayWindow::RenderProfileChangesBgra(
	const std::vector<ProfileChangeOverlay::Item>& items,
	uint8_t opacity, std::vector<uint8_t>& pixels,
	int& width, int& height, int& stride)
{
	if (items.empty() || opacity == 0)
		return false;

	HDC screen = GetDC(nullptr);
	HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
	if (screen) ReleaseDC(nullptr, screen);
	if (!memory) return false;

	HFONT labelFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, TEXT("Segoe UI"));
	HFONT valueFont = CreateFont(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, TEXT("Segoe UI"));
	if (!labelFont || !valueFont)
	{
		if (labelFont) DeleteObject(labelFont);
		if (valueFont) DeleteObject(valueFont);
		DeleteDC(memory);
		return false;
	}

	constexpr int horizontalPadding = 12;
	constexpr int separatorGap = 12;
	constexpr int minimumColumnWidth = 90;
	constexpr int indicatorDiameter = 10;
	constexpr int indicatorGap = 7;
	std::vector<int> columnWidths;
	columnWidths.reserve(items.size());
	for (const auto& item : items)
	{
		const CString label(item.label.c_str());
		const CString value(item.value.c_str());
		SIZE labelSize{};
		SIZE valueSize{};
		HGDIOBJ oldFont = SelectObject(memory, labelFont);
		GetTextExtentPoint32(memory, label, label.GetLength(), &labelSize);
		SelectObject(memory, valueFont);
		GetTextExtentPoint32(memory, value, value.GetLength(), &valueSize);
		SelectObject(memory, oldFont);
		const int indicatorWidth = item.indicator ==
			ProfileChangeOverlay::Item::Indicator::None ? 0 :
			indicatorDiameter + indicatorGap;
		columnWidths.push_back((std::max)(minimumColumnWidth,
			static_cast<int>((std::max)(labelSize.cx,
				valueSize.cx + indicatorWidth)) +
			horizontalPadding * 2));
	}

	width = 4;
	for (int columnWidth : columnWidths) width += columnWidth;
	width += separatorGap * static_cast<int>(items.size() - 1);
	height = 64;
	stride = width * 4;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS,
		&bits, nullptr, 0);
	if (!bitmap || !bits)
	{
		if (bitmap) DeleteObject(bitmap);
		DeleteObject(labelFont);
		DeleteObject(valueFont);
		DeleteDC(memory);
		return false;
	}
	std::memset(bits, 0, static_cast<size_t>(stride) * height);
	HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
	SetBkMode(memory, TRANSPARENT);
	// GDI writes this DIB as BGRA while the native overlay upload presents the
	// first color byte as red. Swap red and blue at drawing time so the on-screen
	// colors match the design values below.
	const auto overlayColor = [](BYTE red, BYTE green, BYTE blue)
	{
		return RGB(blue, green, red);
	};
	RECT panel{ 0, 0, width, height };
	HBRUSH background = CreateSolidBrush(overlayColor(20, 29, 37));
	HPEN panelPen = CreatePen(PS_SOLID, 1, overlayColor(66, 82, 94));
	HGDIOBJ oldBrush = SelectObject(memory, background);
	HGDIOBJ oldPen = SelectObject(memory, panelPen);
	RoundRect(memory, panel.left, panel.top, panel.right, panel.bottom, 8, 8);
	SelectObject(memory, oldBrush);
	SelectObject(memory, oldPen);
	DeleteObject(background);
	DeleteObject(panelPen);

	RECT accent{ 0, 0, 4, height };
	HBRUSH accentBrush = CreateSolidBrush(overlayColor(0, 190, 210));
	FillRect(memory, &accent, accentBrush);
	DeleteObject(accentBrush);

	int x = 4;
	for (size_t index = 0; index < items.size(); ++index)
	{
		const int columnWidth = columnWidths[index];
		const CString label(items[index].label.c_str());
		const CString value(items[index].value.c_str());
		SelectObject(memory, labelFont);
		SetTextColor(memory, overlayColor(174, 187, 197));
		TextOut(memory, x + horizontalPadding, 8,
			label, label.GetLength());
		SelectObject(memory, valueFont);
		SetTextColor(memory, overlayColor(248, 250, 252));
		int valueX = x + horizontalPadding;
		if (items[index].indicator !=
			ProfileChangeOverlay::Item::Indicator::None)
		{
			const bool enabled = items[index].indicator ==
				ProfileChangeOverlay::Item::Indicator::On;
			const COLORREF color = enabled ? overlayColor(52, 211, 153) :
				overlayColor(148, 163, 184);
			HPEN indicatorPen = CreatePen(PS_SOLID, 2, color);
			HBRUSH indicatorBrush = enabled ? CreateSolidBrush(color) :
				static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
			HGDIOBJ previousPen = SelectObject(memory, indicatorPen);
			HGDIOBJ previousBrush = SelectObject(memory, indicatorBrush);
			Ellipse(memory, valueX, 35, valueX + indicatorDiameter,
				35 + indicatorDiameter);
			SelectObject(memory, previousBrush);
			SelectObject(memory, previousPen);
			if (enabled) DeleteObject(indicatorBrush);
			DeleteObject(indicatorPen);
			valueX += indicatorDiameter + indicatorGap;
		}
		TextOut(memory, valueX, 29,
			value, value.GetLength());
		x += columnWidth;
		if (index + 1 < items.size())
		{
			HPEN separator = CreatePen(
				PS_SOLID, 1, overlayColor(76, 91, 103));
			HGDIOBJ previousPen = SelectObject(memory, separator);
			MoveToEx(memory, x + separatorGap / 2, 11, nullptr);
			LineTo(memory, x + separatorGap / 2, height - 11);
			SelectObject(memory, previousPen);
			DeleteObject(separator);
			x += separatorGap;
		}
	}

	SelectObject(memory, oldBitmap);
	pixels.assign(static_cast<uint8_t*>(bits),
		static_cast<uint8_t*>(bits) + static_cast<size_t>(stride) * height);
	const uint8_t alpha = static_cast<uint8_t>(
		(static_cast<unsigned int>(opacity) * 224) / 255);
	for (size_t index = 0; index + 3 < pixels.size(); index += 4)
	{
		pixels[index + 3] = (pixels[index] || pixels[index + 1] ||
			pixels[index + 2]) ? alpha : 0;
	}
	DeleteObject(bitmap);
	DeleteObject(labelFont);
	DeleteObject(valueFont);
	DeleteDC(memory);
	return true;
}

bool StatsOverlayWindow::RenderSweepBannerBgra(const CString& status,
	SweepBannerState state,
	std::vector<uint8_t>& pixels, int& width, int& height, int& stride)
{
	if (status.IsEmpty())
		return false;
	width = 1400;
	height = 390;
	stride = width * 4;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HDC screen = GetDC(nullptr);
	HBITMAP bitmap = CreateDIBSection(
		screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	HDC memory = bitmap ? CreateCompatibleDC(screen) : nullptr;
	ReleaseDC(nullptr, screen);
	if (!bitmap || !memory || !bits)
	{
		if (memory) DeleteDC(memory);
		if (bitmap) DeleteObject(bitmap);
		return false;
	}
	HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
	RECT rect{ 0, 0, width, height };
	HBRUSH background = CreateSolidBrush(RGB(15, 45, 68));
	FillRect(memory, &rect, background);
	DeleteObject(background);
	const COLORREF stateColor = state == SweepBannerState::Passed ? RGB(50, 230, 110) :
		state == SweepBannerState::Expected ? RGB(80, 210, 235) :
		state == SweepBannerState::Measure ? RGB(255, 190, 0) :
		state == SweepBannerState::Failed ? RGB(255, 75, 75) : RGB(255, 190, 0);
	const CString stateLabel = state == SweepBannerState::Passed ? L"PASS" :
		state == SweepBannerState::Expected ? L"EXPECTED" :
		state == SweepBannerState::Measure ? L"MEASURE" :
		state == SweepBannerState::Failed ? L"FAIL" : L"TESTING";
	HBRUSH border = CreateSolidBrush(stateColor);
	FrameRect(memory, &rect, border);
	DeleteObject(border);
	SetBkMode(memory, TRANSPARENT);
	HFONT titleFont = CreateFont(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	HFONT stateFont = CreateFont(58, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	HFONT valueFont = CreateFont(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	HFONT oldFont = static_cast<HFONT>(SelectObject(memory, titleFont));
	SetTextColor(memory, RGB(220, 235, 245));
	const CString title(TEXT("VP OUTPUT SWEEP - ACTIVE TEST"));
	TextOut(memory, 26, 16, title, title.GetLength());
	SelectObject(memory, stateFont);
	SetTextColor(memory, stateColor);
	TextOut(memory, 26, 86, stateLabel, stateLabel.GetLength());
	CString value(status);
	SelectObject(memory, valueFont);
	SetTextColor(memory, RGB(255, 255, 255));
	RECT valueRect{ 330, 86, width - 28, height - 18 };
	::DrawText(memory, value, -1, &valueRect,
		DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
	SelectObject(memory, oldFont);
	DeleteObject(titleFont);
	DeleteObject(stateFont);
	DeleteObject(valueFont);
	SelectObject(memory, oldBitmap);
	pixels.assign(static_cast<uint8_t*>(bits),
		static_cast<uint8_t*>(bits) + static_cast<size_t>(stride) * height);
	for (size_t i = 3; i < pixels.size(); i += 4)
		pixels[i] = 255;
	DeleteDC(memory);
	DeleteObject(bitmap);
	return true;
}

bool StatsOverlayWindow::RenderSweepSummaryBgra(
	const std::vector<SweepSummaryItem>& items, size_t page, size_t itemsPerPage,
	std::vector<uint8_t>& pixels, int& width, int& height, int& stride)
{
	if (items.empty() || itemsPerPage == 0)
		return false;
	const size_t pageCount = (items.size() + itemsPerPage - 1) / itemsPerPage;
	page %= pageCount;
	const size_t first = page * itemsPerPage;
	const size_t count = (std::min)(itemsPerPage, items.size() - first);
	width = 1400;
	height = 168 + static_cast<int>(count) * 160;
	stride = width * 4;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HDC screen = GetDC(nullptr);
	HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits,
		nullptr, 0);
	HDC memory = bitmap ? CreateCompatibleDC(screen) : nullptr;
	ReleaseDC(nullptr, screen);
	if (!bitmap || !memory || !bits)
	{
		if (memory) DeleteDC(memory);
		if (bitmap) DeleteObject(bitmap);
		return false;
	}
	HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
	RECT rect{ 0, 0, width, height };
	HBRUSH background = CreateSolidBrush(RGB(15, 45, 68));
	FillRect(memory, &rect, background);
	DeleteObject(background);
	HBRUSH border = CreateSolidBrush(RGB(115, 185, 230));
	FrameRect(memory, &rect, border);
	DeleteObject(border);
	SetBkMode(memory, TRANSPARENT);
	HFONT titleFont = CreateFont(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	HFONT valueFont = CreateFont(29, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
	HFONT oldFont = static_cast<HFONT>(SelectObject(memory, titleFont));
	SetTextColor(memory, RGB(220, 235, 245));
	CString title;
	title.Format(L"VP OUTPUT SWEEP SUMMARY - PAGE %zu/%zu", page + 1, pageCount);
	TextOut(memory, 26, 16, title, title.GetLength());
	const auto countState = [&items](SweepResultState state)
	{
		return static_cast<size_t>(std::count_if(items.begin(), items.end(),
			[state](const SweepSummaryItem& item) { return item.state == state; }));
	};
	const size_t passed = countState(SweepResultState::Passed);
	const size_t expected = countState(SweepResultState::Expected);
	const size_t measure = countState(SweepResultState::Measure);
	const size_t failed = countState(SweepResultState::Failed);
	CString totals;
	totals.Format(L"%zu PASS / %zu EXPECTED / %zu MEASURE / %zu FAIL",
		passed, expected, measure, failed);
	SetTextColor(memory, failed > 0 ? RGB(255, 75, 75) :
		measure > 0 ? RGB(255, 190, 0) :
		expected > 0 ? RGB(80, 210, 235) : RGB(50, 230, 110));
	TextOut(memory, 26, 70, totals, totals.GetLength());
	SelectObject(memory, valueFont);
	int y = 132;
	for (size_t index = first; index < first + count; ++index)
	{
		const SweepSummaryItem& item = items[index];
		const COLORREF color = item.state == SweepResultState::Passed ? RGB(50, 230, 110) :
			item.state == SweepResultState::Expected ? RGB(80, 210, 235) :
			item.state == SweepResultState::Measure ? RGB(255, 190, 0) : RGB(255, 75, 75);
		const CString verdict = item.state == SweepResultState::Passed ? L"PASS" :
			item.state == SweepResultState::Expected ? L"EXPECTED" :
			item.state == SweepResultState::Measure ? L"MEASURE" : L"FAIL";
		SetTextColor(memory, color);
		TextOut(memory, 26, y, verdict, verdict.GetLength());
		SetTextColor(memory, RGB(255, 255, 255));
		RECT labelRect{ 270, y, width - 28, y + 70 };
		::DrawText(memory, item.label, -1, &labelRect,
			DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
		SetTextColor(memory, RGB(185, 215, 235));
		RECT detailRect{ 270, y + 72, width - 28, y + 150 };
		::DrawText(memory, item.detail, -1, &detailRect,
			DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
		y += 160;
	}
	SelectObject(memory, oldFont);
	DeleteObject(titleFont);
	DeleteObject(valueFont);
	SelectObject(memory, oldBitmap);
	pixels.assign(static_cast<uint8_t*>(bits),
		static_cast<uint8_t*>(bits) + static_cast<size_t>(stride) * height);
	for (size_t i = 3; i < pixels.size(); i += 4)
		pixels[i] = 255;
	DeleteDC(memory);
	DeleteObject(bitmap);
	return true;
}

std::vector<StatsOverlayLayout::Row> StatsOverlayWindow::BuildStatsRows(const StatsData& stats)
{
    std::vector<StatsOverlayLayout::Row> rows;
    int gap = 0;
    const auto add = [&](const wchar_t* label, const CString& value, bool wrap = true) {
        rows.push_back({label, value.GetString(), gap, wrap}); gap = 0;
    };
    const auto present = [](const CString& value, const wchar_t* fallback = L"---") {
        return value.IsEmpty() ? CString(fallback) : value;
    };
    const auto format = [](const wchar_t* pattern, auto... values) {
        CString value; value.Format(pattern, values...); return value;
    };
    const auto detail = [&](const wchar_t* marker) {
        const CString tag(marker);
        const int at = stats.outputMode.Find(tag);
        CString value = at >= 0 ? stats.outputMode.Mid(at + tag.GetLength()) : CString();
        const int end = value.Find(L" | ");
        return end >= 0 ? value.Left(end) : value;
    };
    add(L"Renderer:", present(stats.rendererName));
    if (!stats.outputSweep.IsEmpty()) add(L"OUTPUT SWEEP:", stats.outputSweep, false);
    const auto resolution = DisplayRasterInfo::ResolutionLines(
        stats.resolution.GetString(), stats.outputMode.GetString());
    for (size_t i = 0; i < resolution.size(); ++i)
    {
        CString value(resolution[i].c_str()); value.TrimLeft();
        add(i == 0 ? L"Resolution:" : L"", value);
    }
    if (stats.isAlphaRenderer)
        add(L"Aspect Ratio:", format(L"%s (%s)", present(stats.aspectRatio).GetString(),
            present(stats.aspectName, L"Unknown").GetString()));
    add(L"Refresh:", format(L"%.6f Hz", stats.refreshRate));
    CString displayRate = stats.displayRefreshRate > 0.0
        ? format(L"%.6f Hz%s", stats.displayRefreshRate,
            stats.displayRefreshRateOverridden ? L" *" : L"") : CString(L"---");
    if (!stats.displayRefreshRateStatus.IsEmpty())
        displayRate.AppendFormat(L" (%s)", stats.displayRefreshRateStatus.GetString());
    add(L"- Display Rate:", displayRate);
    if (stats.measuredRefreshRate > 0.0)
    {
        add(L"- Est. Rate:", format(L"%.6f Hz", stats.measuredRefreshRate));
        add(L"- Est. PPM:", format(L"%+d ppm", stats.ppmDeviation));
    }
    if (stats.hasPPMCorrection || (!stats.ppmSource.IsEmpty() && stats.ppmSource != L"N/A"))
        add(L"- Applied PPM:", format(L"%+d ppm", stats.ppmCorrection));
    if ((stats.method == L"Rational-Rational" || stats.method == L"Clock-Rational") && stats.hasPPMCorrection)
        add(L"- Delivery Rate:", format(L"%.6f Hz", stats.refreshRate *
            (1.0 - static_cast<double>(stats.ppmCorrection) / 1000000.0)));
    add(L"Input EOTF:", present(stats.eotf));
    add(L"Input Colorspace:", present(stats.colorspace));
    add(L"DTM Profile:", present(stats.dtmProfile), false);
    add(L"HDR10 (nits):", present(stats.hdr10));
    add(L"Measured (nits):", present(stats.measuredHdr));
    add(L"Pixel Format:", present(stats.pixelFormat));
    add(L"Viewport:", present(stats.viewport, L"default (renderer native)"), false);
    add(L"Surface:", present(stats.surfaceMode));
    if (!stats.outputMode.IsEmpty())
    {
        const int targetSeparator = stats.outputMode.Find(L" | ");
        const CString target = targetSeparator >= 0 ? stats.outputMode.Left(targetSeparator) : L"Target ---";
        const CString transport = targetSeparator >= 0 ? stats.outputMode.Mid(targetSeparator + 3) : stats.outputMode;
        const int separator = transport.Find(L" -> ");
        const CString actualAndDetails = separator >= 0 ? transport.Mid(separator + 4) : L"---";
        const int detailsSeparator = actualAndDetails.Find(L" | ");
        const CString actual = detailsSeparator >= 0 ? actualAndDetails.Left(detailsSeparator) : actualAndDetails;
        const int firstSlash = actual.Find(L'/');
        const int secondSlash = firstSlash >= 0 ? actual.Find(L'/', firstSlash + 1) : -1;
        const int lastSlash = actual.ReverseFind(L'/');
        const CString presentation = firstSlash >= 0 ? actual.Left(firstSlash) : actual;
        const CString rangeCode = firstSlash >= 0 && secondSlash > firstSlash
            ? actual.Mid(firstSlash + 1, secondSlash - firstSlash - 1) : L"?";
        const CString transferCode = secondSlash >= 0 && lastSlash > secondSlash
            ? actual.Mid(secondSlash + 1, lastSlash - secondSlash - 1) : L"?";
        const wchar_t* range = rangeCode == L"F" ? L"RGB Full" : rangeCode == L"L" ? L"RGB Limited" : L"RGB ?";
        const CString transfer = transferCode.Find(L"sRGB") >= 0 ? L"sRGB" : transferCode;
        const wchar_t* primaries = target.Find(L"BT.2020") >= 0 ? L"BT.2020" : L"Rec.709";
        add(L"Output:", format(L"%s / %s / %s", range, transfer.GetString(), primaries));
        const auto addDetail = [&](const wchar_t* label, const wchar_t* marker) {
            const CString value = detail(marker); if (!value.IsEmpty()) add(label, value, false);
        };
        addDetail(L"Signal:", L" | SIGNAL: ");
        addDetail(L"Swapchain:", L" | SWAPCHAIN: ");
        add(L"Present:", presentation);
        addDetail(L"Presenter:", L" | PRESENTER: ");
        addDetail(L"Contract:", L" | CONTRACT: ");
        addDetail(L"Display:", L" | DISPLAY: ");
        addDetail(L"Gamma:", L" | GAMMA: ");
        addDetail(L"Status:", L" | STATUS: ");
    }
    if (!stats.displayLut.IsEmpty()) add(L"LUT:", stats.displayLut, false);
    add(L"Video Conv:", present(stats.videoConversion));
    add(L"Shader Rule:", present(stats.activeShaderRule, L"None"), false);
    add(L"Shaders:", stats.activeShaders.empty() ? CString(L"None")
        : format(L"%zu Active", stats.activeShaders.size()));
    for (const CString& shader : stats.activeShaders)
        add(L"", L"- " + shader, false);
    if (stats.hasConversionData)
    {
        add(L"Conv Time:", format(L"%.2f ms", stats.currentConversionTimeUs / 1000.0));
        add(L"10s Avg/Max:", format(L"%.2f / %.2f ms", stats.avgConversionTime10s / 1000.0,
            stats.maxConversionTime10s / 1000.0));
    }
    // A section gap only exists when its following section exists. Alpha omits
    // the entire DirectShow scheduling section, including its leading gap.
    if (!stats.isAlphaRenderer)
    {
        gap = StatsOverlayLayout::SectionGap;
        add(L"Method:", present(stats.method));
        add(L"Offset:", format(L"%d ms", stats.frameOffsetMs));
    }
    gap = StatsOverlayLayout::SectionGap;
    const bool delayKnown = stats.isAlphaRenderer ? stats.presentationTargetTimingKnown : stats.scheduledLatencyKnown;
    add(L"Delay:", delayKnown ? format(L"%.0f ms", stats.isAlphaRenderer
        ? stats.captureToPresentationTargetMs : stats.scheduledLatencyMs) : CString(L"---"));
    add(L"- Renderer:", stats.vpInternalLatencyKnown ? format(L"%.0f ms", stats.vpInternalLatencyMs) : CString(L"---"));
    CString presentDelay(L"---");
    if (delayKnown) presentDelay = format(L"%.0f ms", stats.isAlphaRenderer
        ? stats.presentationTargetLeadMs : stats.dsScheduleLeadMs);
    else if (stats.isAlphaRenderer && !stats.presentationTimingStatus.IsEmpty())
        presentDelay = format(L"--- (%s)", OsdTimingPolicy::Status(stats.presentationTimingStatus.GetString()).c_str());
    add(L"- Present:", presentDelay);
    gap = StatsOverlayLayout::SectionGap;
    if (stats.isAlphaRenderer)
        add(L"Queue:", format(L"%zu / %zu", stats.rawQueueSize, stats.maxQueueSize));
    else
        add(L"Queue R/C/T:", format(L"%zu/%zu/%zu/%zu%s", stats.rawQueueSize,
            stats.convertedQueueSize, stats.currentQueueSize, stats.maxQueueSize, stats.isQueueFull ? L" [FULL]" : L""));
    if (stats.renderLoadKnown)
    {
        CString gpuTime(L"measuring...");
        if (OsdTimingPolicy::WarmingEnabled && stats.renderLoadSettling) gpuTime = L"settling...";
        else if (stats.renderLoadGpuValid) gpuTime = format(L"avg/max %.2f / %.2f ms; %zu/%zu async (10 s)",
            stats.renderLoadGpuAvgMs, stats.renderLoadGpuPeakMs,
            stats.renderLoadGpuFrames, stats.renderLoadFrames);
        add(L"GPU frame:", gpuTime);
        if (stats.isAlphaRenderer)
        {
            const auto& timing=stats.processingPerformance;
            const auto cpuTiming=[&](const RendererRenderLoadStat& cost,uint64_t count) {
                if(!timing.available)return CString(L"measuring...");
                if(!count)return CString(L"no calls in window");
                return format(L"avg/max %.2f / %.2f ms; n=%llu (%.1f s)",cost.average,cost.peak,
                    static_cast<unsigned long long>(count),timing.windowMilliseconds/1000.0);
            };
            add(L"Prep CPU/wait:",cpuTiming(timing.preparationCpu,timing.preparationCalls));
            add(L"New CPU/wait:",cpuTiming(timing.sourceDrawCpu,timing.sourceDraws));
            add(L"Reuse CPU/wait:",cpuTiming(timing.reusedDrawCpu,timing.reusedDraws));
        }
        const CString cpuLoad = stats.cpuUsageKnown ? format(L"%.0f/%.0f%%", stats.cpuUsageAvgPercent,
            stats.cpuUsagePeakPercent) : CString(L"measuring");
        const CString gpuLoad = stats.renderLoadGpuPercentValid ? format(L"%.0f/%.0f%%",
            stats.renderLoadGpuAvgPercent, stats.renderLoadGpuPercent) : CString(L"measuring");
        add(L"10s avg/max:", format(L"CPU %s, GPU output load %s", cpuLoad.GetString(), gpuLoad.GetString()));
        const double sourceRate = OsdTimingPolicy::FrameBudgetRate(stats.measuredRefreshRate, stats.theoreticalRefreshRate);
        add(L"Source interval:", sourceRate > 0.0 ? format(L"%.2f ms @ %.3f fps", 1000.0 / sourceRate,
            sourceRate) : CString(L"unavailable"));
        CString outputInterval(L"measuring...");
        if (stats.renderLoadFramePeriodMs > 0.0)
            outputInterval = stats.renderLoadFramePeriodFromRenderCadence
                ? format(L"%.2f ms @ %.3f fps", stats.renderLoadFramePeriodMs, 1000.0 / stats.renderLoadFramePeriodMs)
                : CString(L"cadence unavailable");
        add(L"Output interval:", outputInterval);
    }
    add(L"VFrames:", format(L"%llu", stats.rendererCapturedFrames));
    add(L"Capture/queue:", format(L"%llu / %llu drops", stats.capturedDroppedFrames, stats.queueDroppedFrames));
    const auto& passive = stats.outputPresentation.passive;
    if (passive.supported)
    {
        const bool pending = stats.playbackCounterResetSerial > passive.appliedResetSerial;
        const bool incidents = passive.unexpectedDrops || passive.holdIncidents || passive.excessRefreshes;
        if (pending)
        {
            add(L"Display D/R:", L"reset pending");
            add(L"Display glitches:", L"reset pending");
            add(L"Display evidence:", L"reset pending");
        }
        else if (passive.fresh || incidents)
        {
            add(L"Display D/R:", format(L"%llu / >=%llu (%s)", passive.unexpectedDrops,
                passive.excessRefreshes, passive.fresh ? L"observed" : L"recorded"));
            add(L"Display glitches:", format(L">=%llu excessive holds", passive.holdIncidents));
            add(L"Display evidence:", format(L"%llu outcomes / %llu gaps; partial%s", passive.matchedOutcomes,
                passive.coverageGaps, passive.fresh ? L"" : L"; stale/unavailable"));
        }
        else
        {
            const wchar_t* reason = passive.errorCode == ERROR_ACCESS_DENIED ? L"access denied" :
                passive.errorCode == ERROR_FILE_NOT_FOUND ? L"observer not installed" :
                passive.errorCode && passive.errorCode != ERROR_NOT_READY ? L"observer unavailable" :
                passive.running ? L"measuring" : L"unavailable";
            add(L"Display D/R:", format(L"unavailable (%s)", reason));
            add(L"Display glitches:", L"unavailable");
            add(L"Display evidence:", format(L"%llu outcomes / %llu gaps; %s", passive.matchedOutcomes,
                passive.coverageGaps, reason));
        }
    }
    else
    {
    add(L"Display D/R:", L"unavailable (not measured)");
    const auto& output = stats.outputPresentation;
    const wchar_t* outputReason = L"unavailable";
    switch (output.reason)
    {
    case RendererOutputPresentationReason::NoSwapchain: outputReason = L"no swapchain"; break;
    case RendererOutputPresentationReason::Unsupported: outputReason = L"unsupported"; break;
    case RendererOutputPresentationReason::StatisticsFailed: outputReason = L"statistics failed"; break;
    case RendererOutputPresentationReason::Disjoint: outputReason = L"disjoint; rewarming"; break;
    case RendererOutputPresentationReason::Stale: outputReason = L"stale observations"; break;
    case RendererOutputPresentationReason::ObservationGap: outputReason = L"observation gap"; break;
    default:
        switch (output.state)
        {
        case RendererOutputPresentationState::Warming: outputReason = L"warming"; break;
        case RendererOutputPresentationState::Observing: outputReason = L"observing"; break;
        case RendererOutputPresentationState::Limited: outputReason = L"limited observations"; break;
        default: break;
        }
        break;
    }
    // A clean zero requires current verified evidence. Previously recorded
    // incidents remain visible even if observation later becomes unavailable.
    // Session totals are partial observations, not total display drops/repeats.
    const bool hasObservations = output.supported && output.verifiedIntervals > 0 &&
        output.state == RendererOutputPresentationState::Observing &&
        output.reason == RendererOutputPresentationReason::None;
    const bool hasRecordedIncidents = output.observedLongHolds > 0 || output.excessRefreshes > 0;
    if (stats.isAlphaRenderer &&
        stats.playbackCounterResetSerial > output.appliedResetSerial)
    {
        // An asynchronous render-thread reset must hide old published counts.
        // Fresh observation coverage is still required before showing zero.
        add(L"Display holds:", L"reset pending");
        add(L"Display evidence:", L"reset pending");
    }
    else if (hasObservations || hasRecordedIncidents)
    {
        add(L"Display holds:", format(L"%llu events / %llu excess refreshes (%s)",
            output.observedLongHolds, output.excessRefreshes, hasObservations ? L"observed" : L"recorded"));
        add(L"Display evidence:", format(L"%llu intervals / %llu gaps; partial; %s",
            output.verifiedIntervals, output.observationGaps, outputReason));
    }
    else
    {
        add(L"Display holds:", output.state == RendererOutputPresentationState::Unavailable &&
            output.reason == RendererOutputPresentationReason::None
            ? CString(L"unavailable") : format(L"unavailable (%s)", outputReason));
        add(L"Display evidence:", output.supported ? CString(outputReason) : CString(L"unavailable"));
    }
    }
    add(L"Cadence drift:", format(L"estimate: %s", present(stats.cadenceIntervalEstimate, L"unavailable").GetString()));
    add(L"Scene Mode:", present(stats.sceneDetectMode, L"Off"));
    CString sceneStatus;
    if (stats.sceneDetectMode.IsEmpty() || stats.sceneDetectMode.CompareNoCase(L"Off") == 0) sceneStatus = L"None";
    else if (!stats.sceneTimingStatus.IsEmpty()) sceneStatus = OsdTimingPolicy::Status(stats.sceneTimingStatus.GetString()).c_str();
    else if (!stats.sceneTimingReady) sceneStatus = OsdTimingPolicy::WarmingEnabled ? L"Warming" : L"Measuring";
    else sceneStatus = stats.sceneTimingRatesCompatible ? L"Ready" : L"Unavailable";
    add(L"- Status:", sceneStatus);
    add(L"Intentional D/R:", format(L"%llu / %llu", stats.sceneDetectCorrectionDrops, stats.sceneDetectCorrectionRepeats));
    add(L"- Detected:", format(L"%llu", stats.sceneDetectDetected));
    return rows;
}

void StatsOverlayWindow::DrawStats(HDC hdc,
    const std::vector<StatsOverlayLayout::Row>& rows, bool alphaRenderer)
{
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, alphaRenderer ? m_alphaFont : m_font));
    SetTextColor(hdc, TEXT_COLOR);
    const int valueX = StatsOverlayLayout::ValueX(hdc);
    int y = PADDING;
    for (const auto& row : rows)
    {
        y += row.gapBefore;
        const auto label = StatsOverlayLayout::FitText(hdc, row.label, valueX - PADDING - 9);
        const auto value = StatsOverlayLayout::FitText(hdc, row.value, WINDOW_WIDTH - PADDING - valueX);
        TextOutW(hdc, PADDING, y, label.c_str(), static_cast<int>(label.size()));
        TextOutW(hdc, valueX, y, value.c_str(), static_cast<int>(value.size()));
        y += StatsOverlayLayout::LineHeight;
    }
    SelectObject(hdc, oldFont);
}
