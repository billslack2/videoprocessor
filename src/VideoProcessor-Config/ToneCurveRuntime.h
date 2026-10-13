#pragma once
#include <string>

// Loads VP Renderer (Beta)'s libplacebo for the tone charts at runtime. The Config
// application does not link libplacebo, so it starts without VP Renderer's
// files; every function and data export is found by name once loaded.
namespace ToneCurveRuntime
{
    // True when libplacebo is loaded. Otherwise `missing` names the first VP
    // Renderer file that could not be found or loaded. Tried once per run.
    bool Load(std::wstring& missing);

    // The named export of the loaded libplacebo, or null.
    void* Find(const char* exportName);
}
