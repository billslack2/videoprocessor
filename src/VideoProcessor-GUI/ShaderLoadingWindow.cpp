#include <pch.h>

#include "ShaderLoadingWindow.h"


namespace
{
	constexpr wchar_t SHADER_LOADING_WINDOW_CLASS[] =
		L"VideoProcessor Shader Loading";
	constexpr int PANEL_WIDTH_DIP = 330;
	constexpr int PANEL_HEIGHT_DIP = 54;

	int ScaleForDpi(int value, UINT dpi)
	{
		return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
	}

	UINT WindowDpi(HWND hwnd)
	{
		using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
		static const auto getDpiForWindow =
			reinterpret_cast<GetDpiForWindowFunction>(GetProcAddress(
				GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		if (getDpiForWindow)
			return getDpiForWindow(hwnd);
		HDC dc = GetDC(hwnd);
		const UINT dpi = dc ? static_cast<UINT>(
			GetDeviceCaps(dc, LOGPIXELSX)) : USER_DEFAULT_SCREEN_DPI;
		if (dc)
			ReleaseDC(hwnd, dc);
		return dpi;
	}
}


ShaderLoadingWindow::~ShaderLoadingWindow()
{
	Destroy();
}


ATOM ShaderLoadingWindow::RegisterWindowClass()
{
	WNDCLASSEXW windowClass{ sizeof(windowClass) };
	windowClass.lpfnWndProc = ShaderLoadingWindow::WindowProc;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.lpszClassName = SHADER_LOADING_WINDOW_CLASS;

	const ATOM atom = RegisterClassExW(&windowClass);
	if (atom || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
		return atom;
	return 0;
}


bool ShaderLoadingWindow::Create(HWND stableOwner)
{
	if (!stableOwner || !IsWindow(stableOwner))
		return false;
	if (IsWindow(m_hwnd) && m_owner == stableOwner)
		return true;

	Destroy();
	if (!RegisterWindowClass() &&
		GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		m_owner = nullptr;
		return false;
	}
	m_owner = stableOwner;
	m_hwnd = CreateWindowExW(
		WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY |
			WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
		SHADER_LOADING_WINDOW_CLASS,
		L"",
		WS_POPUP,
		0,
		0,
		1,
		1,
		stableOwner,
		nullptr,
		GetModuleHandle(nullptr),
		this);
	if (!m_hwnd)
	{
		m_owner = nullptr;
		return false;
	}
	SetLayeredWindowAttributes(m_hwnd, 0, 245, LWA_ALPHA);
	return true;
}


bool ShaderLoadingWindow::Show(
	HWND renderTarget,
	HWND stableOwner,
	const CString& message)
{
	if (!renderTarget || !IsWindow(renderTarget) ||
		!stableOwner || !IsWindow(stableOwner) || !Create(stableOwner))
	{
		Hide();
		m_renderTarget = nullptr;
		return false;
	}

	const bool wasVisible = IsVisible();
	const bool textChanged = m_message != message;
	const bool targetChanged = m_renderTarget != renderTarget;
	if (wasVisible && !textChanged && !targetChanged)
		return true;
	m_renderTarget = renderTarget;
	m_message = message;
	if (!PositionAndRaise())
	{
		Hide();
		m_renderTarget = nullptr;
		return false;
	}
	if (textChanged || !wasVisible)
	{
		// Paint the new text before the popup becomes visible. This prevents the
		// default white window surface or a stale message from flashing for one
		// compositor frame.
		RedrawWindow(m_hwnd, nullptr, nullptr,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
	ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
	return true;
}


void ShaderLoadingWindow::Hide()
{
	if (IsWindow(m_hwnd))
		ShowWindow(m_hwnd, SW_HIDE);
}


void ShaderLoadingWindow::UpdatePosition()
{
	if (IsVisible())
		PositionAndRaise();
}


bool ShaderLoadingWindow::IsVisible() const
{
	return IsWindow(m_hwnd) && IsWindowVisible(m_hwnd);
}


void ShaderLoadingWindow::Destroy()
{
	if (IsWindow(m_hwnd))
		DestroyWindow(m_hwnd);
	m_hwnd = nullptr;
	m_renderTarget = nullptr;
	m_owner = nullptr;
	m_message.Empty();
}


bool ShaderLoadingWindow::PositionAndRaise()
{
	if (!IsWindow(m_hwnd) || !IsWindow(m_renderTarget))
		return false;

	RECT target{};
	if (!GetClientRect(m_renderTarget, &target))
		return false;
	SetLastError(ERROR_SUCCESS);
	if (!MapWindowPoints(
		m_renderTarget,
		nullptr,
		reinterpret_cast<POINT*>(&target),
		2) && GetLastError() != ERROR_SUCCESS)
	{
		return false;
	}

	const UINT dpi = WindowDpi(m_renderTarget);
	const int width = ScaleForDpi(PANEL_WIDTH_DIP, dpi ? dpi : 96);
	const int height = ScaleForDpi(PANEL_HEIGHT_DIP, dpi ? dpi : 96);
	const int x = target.left +
		(std::max<LONG>(1, target.right - target.left) - width) / 2;
	const int y = target.top +
		(std::max<LONG>(1, target.bottom - target.top) - height) / 2;
	const HWND targetRoot = GetAncestor(m_renderTarget, GA_ROOT);
	const bool targetIsTopmost = targetRoot &&
		(GetWindowLongPtr(targetRoot, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	return SetWindowPos(
		m_hwnd,
		targetIsTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		x,
		y,
		width,
		height,
		SWP_NOACTIVATE) != FALSE;
}


void ShaderLoadingWindow::Paint(HDC dc)
{
	RECT client{};
	GetClientRect(m_hwnd, &client);
	const UINT dpi = WindowDpi(m_hwnd);
	const int radius = ScaleForDpi(8, dpi ? dpi : 96);
	HBRUSH background = CreateSolidBrush(RGB(20, 31, 43));
	HPEN border = CreatePen(PS_SOLID, 1, RGB(63, 111, 142));
	HGDIOBJ oldBrush = SelectObject(dc, background);
	HGDIOBJ oldPen = SelectObject(dc, border);
	RoundRect(dc, client.left, client.top, client.right, client.bottom,
		radius, radius);
	SelectObject(dc, oldPen);
	SelectObject(dc, oldBrush);
	DeleteObject(border);
	DeleteObject(background);

	const int fontHeight = -ScaleForDpi(14, dpi ? dpi : 96);
	HFONT font = CreateFontW(
		fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
	HGDIOBJ oldFont = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(235, 244, 250));
	DrawTextW(dc, m_message, m_message.GetLength(), &client,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	SelectObject(dc, oldFont);
	DeleteObject(font);
}


LRESULT CALLBACK ShaderLoadingWindow::WindowProc(
	HWND hwnd,
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	ShaderLoadingWindow* self = reinterpret_cast<ShaderLoadingWindow*>(
		GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		const CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>(lParam);
		self = static_cast<ShaderLoadingWindow*>(create->lpCreateParams);
		SetWindowLongPtr(hwnd, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(self));
	}

	switch (message)
	{
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT paint{};
		HDC dc = BeginPaint(hwnd, &paint);
		if (self)
			self->Paint(dc);
		EndPaint(hwnd, &paint);
		return 0;
	}
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
}
