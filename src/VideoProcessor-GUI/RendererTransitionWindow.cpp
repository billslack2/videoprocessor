#include <pch.h>
#include <dwmapi.h>

#include "RendererTransitionWindow.h"


namespace
{
	constexpr wchar_t RENDERER_TRANSITION_WINDOW_CLASS[] =
		L"VideoProcessor Renderer Transition";
}


RendererTransitionWindow::~RendererTransitionWindow()
{
	Destroy();
}


ATOM RendererTransitionWindow::RegisterWindowClass()
{
	WNDCLASSEXW windowClass{ sizeof(windowClass) };
	windowClass.lpfnWndProc = RendererTransitionWindow::WindowProc;
	windowClass.hInstance = GetModuleHandle(nullptr);
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	windowClass.hbrBackground =
		reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	windowClass.lpszClassName = RENDERER_TRANSITION_WINDOW_CLASS;

	const ATOM atom = RegisterClassExW(&windowClass);
	if (atom || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
		return atom;

	throw std::runtime_error(
		"Failed to register renderer transition window class");
}


void RendererTransitionWindow::Show(HWND renderTarget, HWND stableOwner)
{
	if (!renderTarget || !IsWindow(renderTarget))
		throw std::runtime_error("Invalid renderer transition target");
	if (!stableOwner || !IsWindow(stableOwner))
		throw std::runtime_error("Invalid renderer transition owner");

	if (!IsWindow(m_hwnd) || m_owner != stableOwner)
	{
		Destroy();
		RegisterWindowClass();
		m_renderTarget = renderTarget;
		m_owner = stableOwner;
		m_hwnd = CreateWindowExW(
			WS_EX_NOACTIVATE | WS_EX_NOPARENTNOTIFY | WS_EX_TOOLWINDOW,
			RENDERER_TRANSITION_WINDOW_CLASS,
			L"",
			WS_POPUP,
			0,
			0,
			1,
			1,
			stableOwner,
			nullptr,
			GetModuleHandle(nullptr),
			nullptr);
		if (!m_hwnd)
		{
			m_renderTarget = nullptr;
			m_owner = nullptr;
			throw std::runtime_error(
				"Failed to create renderer transition window");
		}
	}
	else
	{
		m_renderTarget = renderTarget;
	}

	if (!ResizeAndRaise())
		throw std::runtime_error(
			"Failed to position renderer transition window");
	if (!RedrawWindow(
			m_hwnd,
			nullptr,
			nullptr,
			RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN))
	{
		throw std::runtime_error(
			"Failed to paint renderer transition window");
	}
}


void RendererTransitionWindow::Hide()
{
	if (IsWindow(m_hwnd))
		ShowWindow(m_hwnd, SW_HIDE);
}


void RendererTransitionWindow::KeepOnTop()
{
	if (IsWindow(m_hwnd) && IsWindowVisible(m_hwnd))
		ResizeAndRaise();
}


bool RendererTransitionWindow::IsVisible() const
{
	return IsWindow(m_hwnd) && IsWindowVisible(m_hwnd);
}


HRESULT RendererTransitionWindow::SynchronizeComposition() const
{
	BOOL compositionEnabled = FALSE;
	const HRESULT compositionResult =
		DwmIsCompositionEnabled(&compositionEnabled);
	if (FAILED(compositionResult))
		return compositionResult;
	if (!compositionEnabled)
		return S_FALSE;
	return DwmFlush();
}


void RendererTransitionWindow::Destroy()
{
	if (IsWindow(m_hwnd))
		DestroyWindow(m_hwnd);
	m_hwnd = nullptr;
	m_renderTarget = nullptr;
	m_owner = nullptr;
}


bool RendererTransitionWindow::ResizeAndRaise()
{
	if (!IsWindow(m_hwnd) || !IsWindow(m_renderTarget))
		return false;

	RECT client{};
	if (!GetClientRect(m_renderTarget, &client))
		return false;
	SetLastError(ERROR_SUCCESS);
	if (!MapWindowPoints(
			m_renderTarget,
			nullptr,
			reinterpret_cast<POINT*>(&client),
			2))
	{
		const DWORD mapError = GetLastError();
		if (mapError != ERROR_SUCCESS)
			return false;
	}

	const HWND targetRoot = GetAncestor(m_renderTarget, GA_ROOT);
	const bool targetIsTopmost =
		targetRoot &&
		(GetWindowLongPtr(targetRoot, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	return SetWindowPos(
		m_hwnd,
		targetIsTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		client.left,
		client.top,
		std::max<LONG>(1, client.right - client.left),
		std::max<LONG>(1, client.bottom - client.top),
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}


LRESULT CALLBACK RendererTransitionWindow::WindowProc(
	HWND hwnd,
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	switch (message)
	{
	case WM_ERASEBKGND:
	{
		RECT client{};
		GetClientRect(hwnd, &client);
		FillRect(
			reinterpret_cast<HDC>(wParam),
			&client,
			reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
		return 1;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT paint{};
		HDC dc = BeginPaint(hwnd, &paint);
		RECT client{};
		GetClientRect(hwnd, &client);
		FillRect(
			dc,
			&client,
			reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
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
