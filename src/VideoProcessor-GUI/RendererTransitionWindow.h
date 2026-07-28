#pragma once


// Opaque child window kept above the renderer target while presentation
// ownership changes. A visible child also prevents a retired DirectFlip/MPO
// surface from remaining independently visible through the transition.
class RendererTransitionWindow
{
public:
	RendererTransitionWindow() = default;
	~RendererTransitionWindow();

	void Show(HWND renderTarget);
	void Hide();
	void KeepOnTop();
	bool IsVisible() const;
	HWND GetHWND() const { return m_hwnd; }

private:
	static LRESULT CALLBACK WindowProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static ATOM RegisterWindowClass();

	void Destroy();
	void ResizeAndRaise();

	HWND m_hwnd = nullptr;
	HWND m_renderTarget = nullptr;
};
