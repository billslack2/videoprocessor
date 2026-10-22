#pragma once

#include <Windows.h>
#include <Unknwn.h>
#include <atlbase.h>

// Minimal declaration from madVR's published mvrInterfaces.h 1.0.10 (BSD).
// Copyright (C) 2011-2017 www.madshi.net. OsdRedrawFrame is obsolete/no-op;
// the command interface explicitly supports redrawing a paused frame.
DECLARE_INTERFACE_IID_(IMadVRCommand, IUnknown, "5E9599D1-C5DB-4A84-98A9-09BC5F8F1B79")
{
    STDMETHOD(SendCommand)(LPCSTR command) = 0;
    STDMETHOD(SendCommandBool)(LPCSTR command, bool parameter) = 0;
    STDMETHOD(SendCommandInt)(LPCSTR command, int parameter) = 0;
    STDMETHOD(SendCommandSize)(LPCSTR command, SIZE parameter) = 0;
    STDMETHOD(SendCommandRect)(LPCSTR command, RECT parameter) = 0;
    STDMETHOD(SendCommandUlonglong)(LPCSTR command, ULONGLONG parameter) = 0;
    STDMETHOD(SendCommandDouble)(LPCSTR command, double parameter) = 0;
    STDMETHOD(SendCommandString)(LPCSTR command, LPWSTR parameter) = 0;
    STDMETHOD(SendCommandBin)(LPCSTR command, LPVOID parameter, int size) = 0;
};

namespace MadVROsdRefresh
{
inline HRESULT RedrawPaused(IUnknown* renderer)
{
    if (!renderer) return E_POINTER;
    CComPtr<IMadVRCommand> command;
    const HRESULT result = renderer->QueryInterface(__uuidof(IMadVRCommand),
        reinterpret_cast<void**>(&command));
    return SUCCEEDED(result) ? command->SendCommand("redraw") : result;
}
}
