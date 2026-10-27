#include <Windows.h>
#include <atlbase.h>
#include <cstdio>
#include <vector>
#include <vprenderer/SwapchainOutputProbe.h>
#include <libplacebo/gpu.h>
#include <libplacebo/swapchain.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL line=%d condition=%s\n", __LINE__, #x); return false; } } while (0)
static bool run(bool software, bool retain, bool owned, HMONITOR monitor) {
 pl_d3d11_params dp{}; dp.force_software=software; dp.allow_software=software;
 pl_d3d11 d=pl_d3d11_create(nullptr,&dp); CHECK(d);
 struct D {pl_d3d11& d;~D(){pl_d3d11_destroy(&d);}} dc{d};
 MONITORINFO info{sizeof(info)};CHECK(GetMonitorInfoW(monitor,&info));
 HWND w=CreateWindowExW(0,L"STATIC",L"VP hidden recovery probe",WS_POPUP,info.rcMonitor.left+32,info.rcMonitor.top+32,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(w);
 struct W {HWND w;~W(){DestroyWindow(w);}} wc{w};
 pl_d3d11_swapchain_params sp{};sp.window=w;
 pl_swapchain sw=pl_d3d11_create_swapchain(d,&sp);CHECK(sw);
 struct S {pl_swapchain& s;~S(){pl_swapchain_destroy(&s);}} sc{sw};
 auto effect=[&](){CComPtr<IDXGISwapChain> n;n.Attach(pl_d3d11_swapchain_unwrap(sw));DXGI_SWAP_CHAIN_DESC desc{};n->GetDesc(&desc);return desc.SwapEffect;};
 CHECK(effect()==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
 for(int iteration=0;iteration<3;++iteration){
  int width=64,height=64;CHECK(pl_swapchain_resize(sw,&width,&height));
  pl_swapchain_frame f{};CHECK(pl_swapchain_start_frame(sw,&f));
  const float color[4]={iteration/3.0f,0.25f,0.5f,1};pl_tex_clear(d->gpu,f.fbo,color);
  CHECK(pl_swapchain_submit_frame(sw));pl_swapchain_swap_buffers(sw);
  CComPtr<IDXGISwapChain> n;n.Attach(pl_d3d11_swapchain_unwrap(sw));
  CComPtr<IDXGIOutput> output;DXGI_OUTPUT_DESC desc{};auto hr=n->GetContainingOutput(&output);if(output)output->GetDesc(&desc);printf("probe sw=%d old=%d owned=%d requested=%p actual=%p hr=%08X hwnd_monitor=%p\n",software,retain,owned,monitor,desc.Monitor,unsigned(hr),MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST));
  CHECK(SwapchainOutputProbe::Matches(owned?nullptr:sw,owned?n.p:nullptr,monitor)==!software);
  CHECK(!SwapchainOutputProbe::Matches(sw,nullptr,nullptr));
  if(!retain)n.Release();
  pl_swapchain_destroy(&sw);sw=pl_d3d11_create_swapchain(d,&sp);CHECK(sw);
  CHECK(effect()==(retain?DXGI_SWAP_EFFECT_DISCARD:DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL));
  if(retain)break;
 }
 printf("PASS api=%d software=%d old_lifetime=%d owned_probe=%d monitor=%p\n",PL_API_VER,software,retain,owned,monitor);return true;
}
int main(){std::vector<HMONITOR> monitors;EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR m,HDC,LPRECT,LPARAM p)->BOOL{reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(m);return TRUE;},reinterpret_cast<LPARAM>(&monitors));int total=0;for(bool software:{true,false})for(bool retain:{true,false})for(bool owned:{false,true})for(auto m:monitors){if(!run(software,retain,owned,m))return 1;++total;}printf("PASS total=%d\n",total);return monitors.empty()?2:0;}
