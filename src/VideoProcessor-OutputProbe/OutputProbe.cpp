// Opt-in hardware probe for the DXGI output contracts VP can request. This is
// deliberately separate from the normal test suite: it opens a borderless
// fullscreen window and visibly presents one pattern per test case.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    struct MonitorChoice
    {
        HMONITOR monitor = nullptr;
        MONITORINFOEXW info{ sizeof(info) };
    };

    struct ProbeCase
    {
        const wchar_t* name;
        DXGI_SWAP_EFFECT effect;
        DXGI_FORMAT format;
        DXGI_COLOR_SPACE_TYPE colorSpace;
        bool requestColorSpace;
    };

    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context)
    {
        auto* monitors = reinterpret_cast<std::vector<MonitorChoice>*>(context);
        MonitorChoice choice;
        choice.monitor = monitor;
        if (GetMonitorInfoW(monitor, &choice.info)) monitors->push_back(choice);
        return TRUE;
    }

    LRESULT CALLBACK ProbeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CLOSE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool PumpMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) return false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return true;
    }

    const wchar_t* EffectName(DXGI_SWAP_EFFECT effect)
    {
        switch (effect)
        {
        case DXGI_SWAP_EFFECT_DISCARD: return L"bitblt-discard";
        case DXGI_SWAP_EFFECT_SEQUENTIAL: return L"bitblt-sequential";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return L"flip-sequential";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD: return L"flip-discard";
        default: return L"unknown";
        }
    }

    const wchar_t* ColorName(DXGI_COLOR_SPACE_TYPE colorSpace)
    {
        switch (colorSpace)
        {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709: return L"Full/sRGB/Rec.709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709: return L"Limited/Gamma2.2/Rec.709";
        case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709: return L"Limited/Gamma2.4/Rec.709";
        default: return L"not-requested";
        }
    }

    void PrintUsage()
    {
        std::wprintf(L"VP Active Output Probe\n\n");
        std::wprintf(L"Usage: VideoProcessorOutputProbe.exe --active [--monitor N] [--hold-ms N]\n");
        std::wprintf(L"  --active       Required acknowledgement: this opens a fullscreen test window.\n");
        std::wprintf(L"  --monitor N    One-based monitor number (default: primary monitor).\n");
        std::wprintf(L"  --hold-ms N    Pattern duration per case, 100..5000 ms (default: 700).\n");
        std::wprintf(L"\nThis verifies VP's D3D11/DXGI presenter contracts and Present calls.\n");
        std::wprintf(L"It cannot measure the display's physical gamma/range response.\n");
    }

    bool FindAdapterForMonitor(HMONITOR target, IDXGIAdapter1** result)
    {
        *result = nullptr;
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
                break;
            for (UINT outputIndex = 0;; ++outputIndex)
            {
                ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_OUTPUT_DESC desc{};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == target)
                {
                    *result = adapter.Detach();
                    return true;
                }
            }
        }
        return false;
    }

    enum class Result { Pass, Unsupported, Fail };

    Result RunCase(ID3D11Device* device, ID3D11DeviceContext* context,
        IDXGIFactory2* factory, HWND window, const ProbeCase& test,
        UINT width, UINT height, unsigned holdMs, unsigned index)
    {
        const bool flip = test.effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
            test.effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = test.format;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = flip ? 3 : 1;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = test.effect;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        const HRESULT create = factory->CreateSwapChainForHwnd(device, window,
            &description, nullptr, nullptr, &chain);
        if (FAILED(create))
        {
            std::wprintf(L"FAIL        %-28s create=0x%08lX\n", test.name,
                static_cast<unsigned long>(create));
            return Result::Fail;
        }

        bool supported = true;
        HRESULT set = S_OK;
        if (test.requestColorSpace)
        {
            ComPtr<IDXGISwapChain3> chain3;
            if (FAILED(chain.As(&chain3)))
                supported = false;
            else
            {
                UINT support = 0;
                const HRESULT check = chain3->CheckColorSpaceSupport(test.colorSpace, &support);
                supported = SUCCEEDED(check) &&
                    (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
                if (supported) set = chain3->SetColorSpace1(test.colorSpace);
            }
        }
        if (!supported || FAILED(set))
        {
            std::wprintf(L"UNSUPPORTED %-28s model=%s color=%s set=0x%08lX\n",
                test.name, EffectName(test.effect), ColorName(test.colorSpace),
                static_cast<unsigned long>(set));
            return Result::Unsupported;
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        ComPtr<ID3D11RenderTargetView> target;
        const HRESULT getBuffer = chain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        const HRESULT view = SUCCEEDED(getBuffer)
            ? device->CreateRenderTargetView(backBuffer.Get(), nullptr, &target) : getBuffer;
        if (FAILED(view))
        {
            std::wprintf(L"FAIL        %-28s target=0x%08lX\n", test.name,
                static_cast<unsigned long>(view));
            return Result::Fail;
        }
        const float palette[][4] = {
            { 0.85f, 0.08f, 0.08f, 1.0f }, { 0.08f, 0.75f, 0.15f, 1.0f },
            { 0.08f, 0.20f, 0.85f, 1.0f }, { 0.75f, 0.70f, 0.08f, 1.0f },
        };
        context->ClearRenderTargetView(target.Get(), palette[index % _countof(palette)]);
        const HRESULT present = chain->Present(1, 0);
        if (FAILED(present))
        {
            std::wprintf(L"FAIL        %-28s present=0x%08lX\n", test.name,
                static_cast<unsigned long>(present));
            return Result::Fail;
        }
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(holdMs);
        while (std::chrono::steady_clock::now() < deadline && PumpMessages())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::wprintf(L"PASS        %-28s model=%s color=%s present=0x%08lX\n",
            test.name, EffectName(test.effect), ColorName(test.colorSpace),
            static_cast<unsigned long>(present));
        return Result::Pass;
    }
}

int wmain(int argc, wchar_t** argv)
{
    bool active = false;
    unsigned monitorIndex = 0;
    unsigned holdMs = 700;
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring argument(argv[index]);
        if (argument == L"--active") active = true;
        else if ((argument == L"--monitor" || argument == L"--hold-ms") && index + 1 < argc)
        {
            const unsigned value = static_cast<unsigned>(_wtoi(argv[++index]));
            if (argument == L"--monitor") monitorIndex = value;
            else holdMs = value;
        }
        else if (argument == L"--help" || argument == L"-h" || argument == L"/?")
        {
            PrintUsage();
            return 0;
        }
        else
        {
            std::fwprintf(stderr, L"Unknown argument: %s\n", argument.c_str());
            PrintUsage();
            return 64;
        }
    }
    if (!active)
    {
        std::fwprintf(stderr, L"Refusing to open a fullscreen active test without --active.\n\n");
        PrintUsage();
        return 64;
    }
    holdMs = std::clamp(holdMs, 100u, 5000u);

    std::vector<MonitorChoice> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty())
    {
        std::fwprintf(stderr, L"No active monitors found.\n");
        return 2;
    }
    if (monitorIndex == 0)
    {
        const auto primary = std::find_if(monitors.begin(), monitors.end(),
            [](const MonitorChoice& choice) { return (choice.info.dwFlags & MONITORINFOF_PRIMARY) != 0; });
        monitorIndex = static_cast<unsigned>((primary == monitors.end() ? 0 :
            std::distance(monitors.begin(), primary)) + 1);
    }
    if (monitorIndex == 0 || monitorIndex > monitors.size())
    {
        std::fwprintf(stderr, L"Invalid monitor %u. Active monitors:\n", monitorIndex);
        for (size_t index = 0; index < monitors.size(); ++index)
            std::wprintf(L"  %zu: %s\n", index + 1, monitors[index].info.szDevice);
        return 64;
    }
    const MonitorChoice& selected = monitors[monitorIndex - 1];
    std::wprintf(L"ACTIVE VP DXGI probe: monitor %u (%s), %ums per case\n",
        monitorIndex, selected.info.szDevice, holdMs);

    WNDCLASSW windowClass{};
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpfnWndProc = ProbeWindowProc;
    windowClass.lpszClassName = L"VideoProcessorActiveOutputProbe";
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 2;
    const RECT& rect = selected.info.rcMonitor;
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        windowClass.lpszClassName, L"VP Active Output Probe", WS_POPUP | WS_VISIBLE,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) return 2;
    SetWindowPos(window, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
        rect.bottom - rect.top, SWP_SHOWWINDOW);

    ComPtr<IDXGIAdapter1> adapter;
    if (!FindAdapterForMonitor(selected.monitor, &adapter))
    {
        std::fwprintf(stderr, L"No DXGI adapter owns %s.\n", selected.info.szDevice);
        DestroyWindow(window);
        return 2;
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    const HRESULT createDevice = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context);
    ComPtr<IDXGIFactory2> factory;
    const HRESULT createFactory = adapter
        ? adapter->GetParent(IID_PPV_ARGS(&factory)) : E_FAIL;
    if (FAILED(createDevice) || FAILED(createFactory) || !factory)
    {
        std::fwprintf(stderr, L"D3D11 device/factory setup failed: 0x%08lX\n",
            static_cast<unsigned long>(FAILED(createDevice) ? createDevice : createFactory));
        DestroyWindow(window);
        return 2;
    }

    const ProbeCase cases[] = {
        { L"VP Direct 10-bit Full/sRGB", DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true },
        { L"VP Direct 8-bit Full/sRGB", DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true },
        { L"VP Direct 10-bit Limited/G22", DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709, true },
        { L"VP Direct 10-bit Limited/G24", DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709, true },
        { L"Flip sequential Full/sRGB", DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true },
        { L"VP Composed 10-bit Full/sRGB", DXGI_SWAP_EFFECT_DISCARD, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true },
        { L"Bitblt sequential Full/sRGB", DXGI_SWAP_EFFECT_SEQUENTIAL, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, true },
    };
    unsigned failed = 0;
    unsigned unsupported = 0;
    for (unsigned index = 0; index < _countof(cases) && PumpMessages(); ++index)
    {
        const Result result = RunCase(device.Get(), context.Get(), factory.Get(), window,
            cases[index], static_cast<UINT>(rect.right - rect.left),
            static_cast<UINT>(rect.bottom - rect.top), holdMs, index);
        failed += result == Result::Fail ? 1u : 0u;
        unsupported += result == Result::Unsupported ? 1u : 0u;
    }
    DestroyWindow(window);
    std::wprintf(L"SUMMARY: pass=%u unsupported=%u fail=%u\n",
        static_cast<unsigned>(_countof(cases)) - unsupported - failed, unsupported, failed);
    return failed == 0 ? 0 : 1;
}
