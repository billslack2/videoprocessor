#pragma once

// UI-owned loading notice for slow VP Renderer pipeline preparation. This is
// deliberately a top-level popup instead of a libplacebo overlay: it remains
// paintable while the render thread is compiling a shader program.
class ShaderLoadingWindow
{
public:
	ShaderLoadingWindow() = default;
	~ShaderLoadingWindow();

	bool Show(HWND renderTarget, HWND stableOwner, const CString& message);
	void Hide();
	void UpdatePosition();
	bool IsVisible() const;

private:
	static ATOM RegisterWindowClass();
	static LRESULT CALLBACK WindowProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	void Destroy();
	bool Create(HWND stableOwner);
	bool PositionAndRaise();
	void Paint(HDC dc);

	HWND m_hwnd = nullptr;
	HWND m_renderTarget = nullptr;
	HWND m_owner = nullptr;
	CString m_message;
};
