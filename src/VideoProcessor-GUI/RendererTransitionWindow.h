#pragma once


// Opaque, owner-bound popup kept above the renderer target while presentation
// ownership changes. The popup is deliberately not a child of the target:
// DirectShow renderers create their own child video window, and fullscreen
// target replacement must not destroy the transition cover.
class RendererTransitionWindow
{
public:
	RendererTransitionWindow() = default;
	~RendererTransitionWindow();

	void Show(HWND renderTarget, HWND stableOwner);
	void Hide();
	void KeepOnTop();
	bool IsVisible() const;
	HRESULT SynchronizeComposition() const;
	HWND GetHWND() const { return m_hwnd; }
	HWND GetOwnerHWND() const { return m_owner; }

private:
	static LRESULT CALLBACK WindowProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	static ATOM RegisterWindowClass();

	void Destroy();
	bool ResizeAndRaise();

	HWND m_hwnd = nullptr;
	HWND m_renderTarget = nullptr;
	HWND m_owner = nullptr;
};
