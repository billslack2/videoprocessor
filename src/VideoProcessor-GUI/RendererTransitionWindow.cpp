#include <pch.h>

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


void RendererTransitionWindow::Show(HWND renderTarget)
{
	if (!renderTarget || !IsWindow(renderTarget))
		throw std::runtime_error("Invalid renderer transition target");

	if (!IsWindow(m_hwnd) || m_renderTarget != renderTarget)
	{
		Destroy();
		RegisterWindowClass();
		m_renderTarget = renderTarget;
		m_hwnd = CreateWindowExW(
			WS_EX_NOPARENTNOTIFY,
			RENDERER_TRANSITION_WINDOW_CLASS,
			L"",
			WS_CHILD | WS_CLIPSIBLINGS,
			0,
			0,
			1,
			1,
			renderTarget,
			nullptr,
			GetModuleHandle(nullptr),
			nullptr);
		if (!m_hwnd)
		{
			m_renderTarget = nullptr;
			throw std::runtime_error(
				"Failed to create renderer transition window");
		}
	}

	ResizeAndRaise();
	RedrawWindow(
		m_hwnd,
		nullptr,
		nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
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


void RendererTransitionWindow::Destroy()
{
	if (IsWindow(m_hwnd))
		DestroyWindow(m_hwnd);
	m_hwnd = nullptr;
	m_renderTarget = nullptr;
}


void RendererTransitionWindow::ResizeAndRaise()
{
	if (!IsWindow(m_hwnd) || !IsWindow(m_renderTarget))
		return;

	RECT client{};
	if (!GetClientRect(m_renderTarget, &client))
		return;

	SetWindowPos(
		m_hwnd,
		HWND_TOP,
		0,
		0,
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

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
}
