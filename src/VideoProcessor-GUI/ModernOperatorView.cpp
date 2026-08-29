#include <pch.h>

#include "ModernOperatorView.h"
#include "resource.h"
#include "version.h"
#include "BuildIdentityPolicy.h"
#include <ModernOperatorLayout.h>

namespace
{
	constexpr UINT IDC_MODERN_CONFIGURATION = 12001;
	constexpr UINT IDC_MODERN_CAPTURE_RESTART = 12002;
	constexpr UINT IDC_MODERN_RENDERER_RESTART = 12003;
	constexpr UINT IDC_MODERN_QUEUE_RESET = 12004;
	constexpr UINT IDC_MODERN_VIDEO_ONLY = 12005;
	constexpr UINT IDC_MODERN_VIEW = 12006;
	constexpr UINT IDC_MODERN_PCIE_SPEED_WARNING = 12007;
	constexpr UINT IDC_MODERN_PCIE_WIDTH_WARNING = 12008;

	const COLORREF Background = RGB(6, 13, 20);
	const COLORREF Header = RGB(15, 26, 37);
	const COLORREF Card = RGB(16, 31, 43);
	const COLORREF Border = RGB(35, 56, 72);
	const COLORREF Text = RGB(238, 246, 255);
	const COLORREF Muted = RGB(139, 176, 205);
	const COLORREF Accent = RGB(56, 215, 162);
}

BEGIN_MESSAGE_MAP(ModernOperatorView, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_DRAWITEM()
	ON_BN_CLICKED(IDC_MODERN_CONFIGURATION, &ModernOperatorView::OnConfiguration)
	ON_BN_CLICKED(IDC_MODERN_CAPTURE_RESTART, &ModernOperatorView::OnCaptureRestart)
	ON_BN_CLICKED(IDC_MODERN_RENDERER_RESTART, &ModernOperatorView::OnRendererRestart)
	ON_BN_CLICKED(IDC_MODERN_QUEUE_RESET, &ModernOperatorView::OnQueueReset)
	ON_BN_CLICKED(IDC_MODERN_VIDEO_ONLY, &ModernOperatorView::OnToggleVideoOnly)
	ON_BN_CLICKED(IDC_MODERN_VIEW, &ModernOperatorView::OnToggleView)
END_MESSAGE_MAP()

bool ModernOperatorView::Create(CWnd* parent)
{
	const CString className = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
	if (!CreateEx(WS_EX_CONTROLPARENT, className, TEXT("Modern operator view"),
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 1, 1), parent, 0))
		return false;

	CClientDC screen(this);
	const int dpi = screen.GetDeviceCaps(LOGPIXELSX);
	m_regularFont.CreateFont(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Segoe UI"));
	m_boldFont.CreateFont(-MulDiv(13, dpi, 96), 0, 0, 0, FW_SEMIBOLD,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Segoe UI"));
	m_titleFont.CreateFont(-MulDiv(18, dpi, 96), 0, 0, 0, FW_BOLD,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Segoe UI"));
	m_settingsFont.CreateFont(-MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
		TEXT("Segoe MDL2 Assets"));

	CreateButton(m_configuration, IDC_MODERN_CONFIGURATION,
		TEXT("Open configuration"));
	CreateButton(m_videoOnly, IDC_MODERN_VIDEO_ONLY, TEXT("Video Only"));
	CreateButton(m_view, IDC_MODERN_VIEW, TEXT("Fullscreen"));
	CreateButton(m_captureRestart, IDC_MODERN_CAPTURE_RESTART, TEXT("Restart capture"));
	CreateButton(m_rendererRestart, IDC_MODERN_RENDERER_RESTART, TEXT("Restart"));
	CreateButton(m_queueReset, IDC_MODERN_QUEUE_RESET, TEXT("Reset queues"));
	if (m_tooltips.Create(this, TTS_ALWAYSTIP))
	{
		m_tooltips.AddTool(&m_videoOnly,
			TEXT("Hide the controls. Press Ctrl+Shift+U to return."));
		m_tooltips.AddTool(&m_view,
			TEXT("Switch Windowed / Fullscreen. Shortcut: Alt+Enter."));
		m_tooltips.AddTool(&m_configuration, TEXT("Open configuration"));
		m_tooltips.Activate(TRUE);
	}
	LayoutControls();
	return true;
}

BOOL ModernOperatorView::PreTranslateMessage(MSG* message)
{
	if (m_tooltips.GetSafeHwnd())
		m_tooltips.RelayEvent(message);
	return CWnd::PreTranslateMessage(message);
}

void ModernOperatorView::CreateButton(CButton& button, UINT id,
	const CString& text)
{
	button.Create(text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
		CRect(0, 0, 1, 1), this, id);
	button.SetFont(&m_regularFont);
}

int ModernOperatorView::Px(int logicalPixels) const
{
	CClientDC screen(const_cast<ModernOperatorView*>(this));
	return MulDiv(logicalPixels, screen.GetDeviceCaps(LOGPIXELSX), 96);
}

void ModernOperatorView::LayoutControls()
{
	if (!GetSafeHwnd())
		return;
	CRect client;
	GetClientRect(&client);
	CClientDC screen(this);
	const auto header = ModernOperatorLayout::CalculateHeaderControls(
		client.Width(), screen.GetDeviceCaps(LOGPIXELSX));
	auto place = [](CButton& button, const ModernOperatorLayout::Rect& rect)
	{
		button.MoveWindow(rect.x, rect.y, rect.width, rect.height);
	};
	place(m_configuration, header.configuration);
	place(m_view, header.fullscreen);
	place(m_videoOnly, header.videoOnly);
	m_captureRestart.MoveWindow(Px(390), Px(116), Px(127), Px(29));
	m_rendererRestart.MoveWindow(Px(179), Px(538), Px(77), Px(29));
	m_queueReset.MoveWindow(Px(394), Px(615), Px(117), Px(29));
}

void ModernOperatorView::DrawWideValue(CDC& dc, int x, int y, int width,
	const CString& value)
{
	dc.SetTextColor(Text);
	dc.SelectObject(&m_regularFont);
	CRect valueRect(Px(x), Px(y), Px(x + width), Px(y + 17));
	dc.DrawText(value, valueRect,
		DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ModernOperatorView::DrawWarningIcon(CDC& dc, int x, int y)
{
	const int left = Px(x);
	const int top = Px(y);
	const int size = Px(13);
	CPoint triangle[] = {
		CPoint(left + size / 2, top),
		CPoint(left, top + size - Px(1)),
		CPoint(left + size, top + size - Px(1)) };
	CPen border(PS_SOLID, 1, RGB(119, 81, 12));
	CBrush fill(RGB(238, 180, 49));
	const auto oldPen = dc.SelectObject(&border);
	const auto oldBrush = dc.SelectObject(&fill);
	dc.Polygon(triangle, 3);
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldPen);
	dc.SetTextColor(RGB(32, 28, 15));
	dc.SelectObject(&m_boldFont);
	dc.DrawText(TEXT("!"), CRect(left, top + Px(1), left + size,
		top + size + Px(1)), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ModernOperatorView::DrawHardwareRow(CDC& dc, int x, int y, int width,
	const CString& label, const CString& value, bool warning)
{
	dc.SelectObject(&m_regularFont);
	dc.SetTextColor(Muted);
	dc.TextOut(Px(x), Px(y), label);
	const int valueRight = x + width - (warning ? 18 : 0);
	dc.SetTextColor(Text);
	CRect valueRect(Px(x + width / 2), Px(y), Px(valueRight), Px(y + 17));
	dc.DrawText(value, valueRect,
		DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	if (warning)
		DrawWarningIcon(dc, valueRight + 4, y + 1);
}

void ModernOperatorView::DrawQueueMetric(CDC& dc, int x, int y,
	const CString& label, const CString& value)
{
	dc.SelectObject(&m_regularFont);
	dc.SetTextColor(Muted);
	dc.TextOut(Px(x), Px(y), label);
	dc.SetTextColor(Text);
	dc.SelectObject(&m_boldFont);
	// Keep the 13 px value baseline cadence tight enough that the semibold
	// glyph descenders clear the card's bottom border at every DPI.
	dc.TextOut(Px(x), Px(y + 13), value);
}

void ModernOperatorView::SetStatus(const ModernOperatorStatus& status)
{
	const bool presentationChanged =
		m_status.videoOnly != status.videoOnly ||
		m_status.fullscreenRequested != status.fullscreenRequested;
	if (m_tooltips.GetSafeHwnd())
	{
		auto updateWarningTooltip = [this](bool wasVisible, bool visible,
			const CRect& rect, UINT_PTR id)
		{
			if (wasVisible == visible)
				return;
			if (visible)
				m_tooltips.AddTool(this, TEXT("May impact performance"), &rect, id);
			else
				m_tooltips.DelTool(this, id);
		};
		updateWarningTooltip(m_status.hardwareSpeedWarning,
			status.hardwareSpeedWarning,
			CRect(Px(243), Px(367), Px(256), Px(380)),
			IDC_MODERN_PCIE_SPEED_WARNING);
		updateWarningTooltip(m_status.hardwareWidthWarning,
			status.hardwareWidthWarning,
			CRect(Px(243), Px(385), Px(256), Px(398)),
			IDC_MODERN_PCIE_WIDTH_WARNING);
	}
	m_status = status;
	if (presentationChanged)
	{
		m_videoOnly.Invalidate(FALSE);
		m_view.Invalidate(FALSE);
	}
	// Live telemetry changes once per second. Keep that repaint strictly inside
	// the information column so the renderer child and custom caption never
	// participate in the periodic redraw.
	CRect client;
	GetClientRect(&client);
	InvalidateRect(CRect(0, Px(70), Px(536), client.bottom), FALSE);
}

BOOL ModernOperatorView::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void ModernOperatorView::OnSize(UINT type, int width, int height)
{
	CWnd::OnSize(type, width, height);
	LayoutControls();
}

void ModernOperatorView::DrawCard(CDC& dc, const CRect& rect,
	const CString& eyebrow, const CString& title, const CString& state)
{
	CPen pen(PS_SOLID, 1, Border);
	CBrush brush(Card);
	const auto oldPen = dc.SelectObject(&pen);
	const auto oldBrush = dc.SelectObject(&brush);
	dc.RoundRect(rect, CPoint(Px(10), Px(10)));
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldPen);

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(Muted);
	dc.SelectObject(&m_regularFont);
	dc.TextOut(rect.left + Px(11), rect.top + Px(4), eyebrow);
	dc.SetTextColor(Text);
	dc.SelectObject(&m_boldFont);
	dc.TextOut(rect.left + Px(11), rect.top + Px(18), title);
	if (!state.IsEmpty())
	{
		dc.SetTextColor(Accent);
		CRect stateRect(
			rect.left + rect.Width() / 2,
			rect.top + Px(17),
			rect.right - Px(11),
			rect.top + Px(36));
		dc.DrawText(state, stateRect,
			DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
}

void ModernOperatorView::DrawRows(CDC& dc, int x, int y, int width,
	const std::initializer_list<std::pair<CString, CString>>& rows)
{
	dc.SelectObject(&m_regularFont);
	for (const auto& row : rows)
	{
		dc.SetTextColor(Muted);
		dc.TextOut(Px(x), Px(y), row.first);
		dc.SetTextColor(Text);
		CRect valueRect(Px(x + width / 2), Px(y),
			Px(x + width), Px(y + 17));
		dc.DrawText(row.second, valueRect,
			DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		y += 18;
	}
}

void ModernOperatorView::OnPaint()
{
	CPaintDC paintDc(this);
	CRect paintRect;
	paintDc.GetClipBox(&paintRect);
	CDC dc;
	dc.CreateCompatibleDC(&paintDc);
	CBitmap buffer;
	buffer.CreateCompatibleBitmap(&paintDc,
		std::max(1, paintRect.Width()), std::max(1, paintRect.Height()));
	const auto oldBitmap = dc.SelectObject(&buffer);
	dc.SetViewportOrg(-paintRect.left, -paintRect.top);
	CRect client;
	GetClientRect(&client);
	dc.FillSolidRect(client, Background);
	dc.FillSolidRect(0, 0, client.Width(), Px(55), Header);
	dc.FillSolidRect(0, Px(54), client.Width(), 1, Border);
	dc.SetBkMode(TRANSPARENT);

	const HICON appIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	if (appIcon)
	{
		::DrawIconEx(dc.GetSafeHdc(), Px(18), Px(13), appIcon,
			Px(29), Px(29), 0, nullptr, DI_NORMAL);
	}
	dc.SetTextColor(Text);
	dc.SelectObject(&m_titleFont);
	dc.TextOut(Px(58), Px(17), TEXT("VideoProcessor"));
	dc.SetTextColor(Muted);
	dc.SelectObject(&m_regularFont);
	// The compact configuration gear is right-anchored. Give the build
	// identifier all remaining header space.
	const int versionRight = std::max(
		Px(207), static_cast<int>(client.right) - Px(252));
	CRect versionRect(Px(207), Px(20), versionRight, Px(40));
	const std::wstring buildIdentity = BuildIdentityPolicy::Format(
		VERSION_BRANCH, VERSION_COMMIT_SHORT, VERSION_DESCRIBE, VERSION_DIRTY);
	dc.DrawText(buildIdentity.c_str(), versionRect,
		DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

	DrawCard(dc, CRect(Px(16), Px(70), Px(528), Px(158)),
		TEXT(""), TEXT("Input source"), m_status.captureState);
	CRect sourceIcon(Px(27), Px(112), Px(59), Px(144));
	CPen sourcePen(PS_SOLID, 1, RGB(40, 117, 158));
	CBrush sourceBrush(RGB(17, 48, 65));
	const auto oldSourcePen = dc.SelectObject(&sourcePen);
	const auto oldSourceBrush = dc.SelectObject(&sourceBrush);
	dc.RoundRect(sourceIcon, CPoint(Px(7), Px(7)));
	dc.SelectObject(oldSourceBrush);
	dc.SelectObject(oldSourcePen);
	dc.FillSolidRect(Px(39), Px(124), Px(8), Px(8), RGB(35, 194, 246));
	dc.FillSolidRect(Px(41), Px(126), Px(4), Px(4), Card);
	dc.SetTextColor(Text); dc.SelectObject(&m_regularFont);
	dc.TextOut(Px(69), Px(121), m_status.captureDevice);

	DrawCard(dc, CRect(Px(16), Px(166), Px(268), Px(318)),
		TEXT(""), TEXT("Input"), m_status.inputLock);
	DrawWideValue(dc, 27, 208, 230, m_status.inputMode);
	DrawRows(dc, 27, 226, 230, {
		{ TEXT("Frame rate"), m_status.inputRate },
		{ TEXT("Format"), m_status.inputFormat }, { TEXT("Bit depth"), m_status.inputBitDepth },
		{ TEXT("Frames"), m_status.inputFrames }, { TEXT("Missed"), m_status.inputMissed } });

	DrawCard(dc, CRect(Px(276), Px(166), Px(528), Px(318)),
		TEXT(""), TEXT("Captured video"), m_status.capturedValid);
	DrawWideValue(dc, 287, 208, 230, m_status.capturedMode);
	DrawRows(dc, 287, 226, 230, {
		{ TEXT("Rate"), m_status.capturedRate },
		{ TEXT("Pixel format"), m_status.capturedPixelFormat },
		{ TEXT("Primaries"), m_status.capturedPrimaries },
		{ TEXT("Transfer"), m_status.capturedTransfer } });

	// Hardware link contains two values, so keep it compact and give the
	// renderer card enough height for its metadata plus a bottom action.
	DrawCard(dc, CRect(Px(16), Px(326), Px(268), Px(420)),
		TEXT(""), TEXT("Hardware link"));
	DrawHardwareRow(dc, 27, 366, 230, TEXT("PCIe speed"),
		m_status.hardware[0], m_status.hardwareSpeedWarning);
	DrawHardwareRow(dc, 27, 384, 230, TEXT("PCIe width"),
		m_status.hardware[1], m_status.hardwareWidthWarning);

	DrawCard(dc, CRect(Px(276), Px(326), Px(528), Px(438)),
		TEXT(""), TEXT("HDR luminance"));
	DrawRows(dc, 287, 366, 230, {
		{ TEXT("MaxCLL"), m_status.maxCll }, { TEXT("MaxFALL"), m_status.maxFall },
		{ TEXT("Mastering min"), m_status.masteringMin }, { TEXT("Mastering max"), m_status.masteringMax } });

	DrawCard(dc, CRect(Px(16), Px(428), Px(268), Px(575)),
		TEXT(""), TEXT("Renderer"), m_status.rendererState);
	if (m_status.directShowRenderer)
	{
		DrawRows(dc, 27, 475, 230, {
			{ TEXT("Renderer"), m_status.rendererName },
			{ TEXT("Uptime"), m_status.rendererUptime },
			{ TEXT("Start/Stop"), m_status.rendererStartStopMethod } });
	}
	else if (m_status.vpRenderer)
	{
		DrawRows(dc, 27, 475, 230, {
			{ TEXT("Renderer"), m_status.rendererName },
			{ TEXT("Uptime"), m_status.rendererUptime } });
	}
	else
	{
		DrawRows(dc, 27, 475, 230, {
			{ TEXT("Renderer"), m_status.rendererName },
			{ TEXT("Uptime"), m_status.rendererUptime } });
	}

	DrawCard(dc, CRect(Px(276), Px(446), Px(528), Px(575)),
		TEXT(""), TEXT("Latency"));
	DrawRows(dc, 287, 488, 230, {
		{ TEXT("VP Processing"), m_status.vpLatency },
		{ TEXT("Presentation"), m_status.ptsLead },
		{ TEXT("Total"), m_status.outputLatency } });

	DrawCard(dc, CRect(Px(16), Px(583), Px(528), Px(655)),
		TEXT(""), TEXT("Queue health"));
	if (m_status.singleQueue)
	{
		DrawQueueMetric(dc, 27, 621, TEXT("Queued"), m_status.queueTotal);
		DrawQueueMetric(dc, 112, 621, TEXT("Capacity"), m_status.queueCapacity);
		DrawQueueMetric(dc, 207, 621, TEXT("Drops"), m_status.dropped);
	}
	else
	{
		DrawQueueMetric(dc, 27, 621, TEXT("Raw"), m_status.queueRaw);
		DrawQueueMetric(dc, 78, 621, TEXT("Converted"), m_status.queueConverted);
		DrawQueueMetric(dc, 163, 621, TEXT("Total"), m_status.queueTotal);
		DrawQueueMetric(dc, 218, 621, TEXT("Max"), m_status.queueCapacity);
		DrawQueueMetric(dc, 267, 621, TEXT("Drops"), m_status.dropped);
	}

	paintDc.BitBlt(paintRect.left, paintRect.top,
		paintRect.Width(), paintRect.Height(), &dc,
		paintRect.left, paintRect.top, SRCCOPY);
	dc.SelectObject(oldBitmap);
}

void ModernOperatorView::OnDrawItem(int, LPDRAWITEMSTRUCT item)
{
	CDC dc;
	dc.Attach(item->hDC);
	CRect rect(item->rcItem);
	const bool pressed = (item->itemState & ODS_SELECTED) != 0;
	const bool active =
		(item->CtlID == IDC_MODERN_VIDEO_ONLY && m_status.videoOnly) ||
		(item->CtlID == IDC_MODERN_VIEW && m_status.fullscreenRequested);
	COLORREF fill = active ? RGB(20, 82, 68) : RGB(20, 42, 58);
	if (pressed)
		fill = RGB(30, 65, 82);
	dc.FillSolidRect(rect, fill);
	CPen border(PS_SOLID, 1, Border);
	const auto oldPen = dc.SelectObject(&border);
	dc.SelectStockObject(NULL_BRUSH);
	dc.RoundRect(rect, CPoint(Px(7), Px(7)));
	dc.SelectObject(oldPen);
	if (item->CtlID == IDC_MODERN_CONFIGURATION)
	{
		// E713 is the Windows Settings glyph. Segoe MDL2 Assets is DPI-aware and
		// keeps the small toolbar mark cleaner than custom raster/GDI geometry.
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(pressed ? Accent : RGB(190, 214, 232));
		if (m_settingsFont.GetSafeHandle())
		{
			dc.SelectObject(&m_settingsFont);
			dc.DrawText(CString(L"\xE713"), rect,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		else
		{
			dc.SelectObject(&m_titleFont);
			dc.DrawText(CString(L"\x2699"), rect,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		dc.Detach();
		return;
	}
	CString text;
	GetDlgItem(item->CtlID)->GetWindowText(text);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(Text);
	dc.SelectObject(&m_regularFont);
	dc.DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.Detach();
}

void ModernOperatorView::OnCaptureRestart()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::CaptureRestart));
}

void ModernOperatorView::OnRendererRestart()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::RendererRestart));
}

void ModernOperatorView::OnQueueReset()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::QueueReset));
}

void ModernOperatorView::OnConfiguration()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::OpenConfiguration));
}

void ModernOperatorView::OnToggleVideoOnly()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::ToggleVideoOnly));
}

void ModernOperatorView::OnToggleView()
{
	GetParent()->PostMessage(WM_MODERN_OPERATOR_ACTION,
		static_cast<WPARAM>(ModernOperatorAction::ToggleView));
}
