#pragma once

#include <Unknwn.h>
#include <Windows.h>

// Public madVR OSD interface (mvrInterfaces.h 1.0.10).  VP uses only the
// bitmap path so it does not own madVR's render-target lifetime.
typedef void (__stdcall *OSDMOUSECALLBACK)(LPCSTR, LPVOID, UINT, WPARAM, int, int);
constexpr DWORD MADVR_BITMAP_INFO_DISPLAY = 2;
constexpr DWORD MADVR_BITMAP_MASKING_AWARE = 8;

DECLARE_INTERFACE_IID_(IMadVROsdServices, IUnknown, "3AE03A88-F613-4BBA-AD3E-EE236976BF9A")
{
	STDMETHOD(OsdSetBitmap)(LPCSTR name, HBITMAP leftEye, HBITMAP rightEye,
		COLORREF colorKey, int posX, int posY, bool posRelativeToVideoRect,
		int zOrder, DWORD duration, DWORD flags, OSDMOUSECALLBACK callback,
		LPVOID callbackContext, LPVOID reserved) = 0;
	STDMETHOD(OsdGetVideoRects)(RECT* fullOutputRect, RECT* activeVideoRect) = 0;
	STDMETHOD(OsdSetRenderCallback)(LPCSTR name, IUnknown* callback, LPVOID reserved) = 0;
	STDMETHOD(OsdRedrawFrame)() = 0;
};
