#pragma once

#include <atlbase.h>
#include <dxgi.h>
#include <libplacebo/d3d11.h>

namespace SwapchainOutputProbe
{
    // A probe must not lend a COM reference to the subsequent same-HWND
    // recreation. DXGI permits only one live flip swapchain per window;
    // retaining the old chain makes libplacebo silently fall back to bitblt.
    inline bool Matches(pl_swapchain swapchain, IDXGISwapChain* owned,
        HMONITOR expected)
    {
        if (!expected) return false;
        CComPtr<IDXGISwapChain> native;
        if (owned) native = owned;
        else if (swapchain) native.Attach(pl_d3d11_swapchain_unwrap(swapchain));
        CComPtr<IDXGIOutput> output;
        DXGI_OUTPUT_DESC description{};
        return native && SUCCEEDED(native->GetContainingOutput(&output)) &&
            output && SUCCEEDED(output->GetDesc(&description)) &&
            description.Monitor == expected;
    }
}
