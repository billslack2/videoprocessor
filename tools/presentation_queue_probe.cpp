#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/gpu.h>
#include <vprenderer/DisplayVsyncClock.h>
#include <algorithm>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
static double now() { LARGE_INTEGER q,f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return double(q.QuadPart)*1000/f.QuadPart; }
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) { return DefWindowProc(w,m,a,b); }
int main() {
 WNDCLASSW cls{}; cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandle(nullptr); cls.lpszClassName=L"Vp0211QueueProbe"; RegisterClassW(&cls);
 HWND window=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"VP presentation probe",WS_OVERLAPPEDWINDOW,80,80,220,140,nullptr,nullptr,cls.hInstance,nullptr);
 if(!window)return 2; ShowWindow(window,SW_SHOWNOACTIVATE);
 FILE* detail=fopen("tmp/vp0211-probe/per-frame.csv","w");
 fprintf(detail,"owner,depth,frame,cycle_ms,start_frame_ms,submit_ms,present_ms,present_id,stats_hr,displayed_id,displayed_refresh,sync_refresh,sync_qpc,feedback\n");
 for(int owned=0;owned<2;++owned) for(int depth: {1,2,8}) {
  pl_d3d11_params dev{}; dev.allow_software=false; dev.min_feature_level=D3D_FEATURE_LEVEL_10_0; dev.max_frame_latency=depth;
  pl_d3d11 d=pl_d3d11_create(nullptr,&dev); if(!d)return 3;
  ComPtr<IDXGIDevice1> dxgi; if(FAILED(d->device->QueryInterface(IID_PPV_ARGS(&dxgi))))return 4;
  UINT applied=0; HRESULT set=dxgi->SetMaximumFrameLatency(depth),get=dxgi->GetMaximumFrameLatency(&applied);
  pl_d3d11_swapchain_params params{}; params.window=window; params.disable_10bit_sdr=true;
  ComPtr<IDXGISwapChain1> chain;
  if(owned) {
   ComPtr<IDXGIAdapter> adapter; dxgi->GetAdapter(&adapter); ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));
   DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=200; desc.Height=100; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
   desc.BufferCount=applied+1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT|DXGI_USAGE_UNORDERED_ACCESS; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
   if(FAILED(factory->CreateSwapChainForHwnd(d->device,window,&desc,nullptr,nullptr,&chain)))return 5;
   params.swapchain=chain.Get();
  }
  pl_swapchain sw=pl_d3d11_create_swapchain(d,&params); if(!sw)return 6;
  int width=200,height=100; if(!pl_swapchain_resize(sw,&width,&height))return 7;
  ComPtr<IDXGISwapChain> native; native.Attach(pl_d3d11_swapchain_unwrap(sw)); DXGI_SWAP_CHAIN_DESC actual{}; native->GetDesc(&actual);
  std::vector<double> samples; unsigned statsOk=0,errors=0,feedbackCount=0; UINT lastCount=0;
  DEVMODEW mode{}; mode.dmSize=sizeof(mode); EnumDisplaySettingsW(nullptr,ENUM_CURRENT_SETTINGS,&mode);
  const double hz=mode.dmDisplayFrequency>1 ? mode.dmDisplayFrequency : 60;
  DisplayVsyncClock clock; clock.BeginSource(0,static_cast<int64_t>(now()*1e6),1,hz,false);
  LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
  for(int frame=0;frame<120;++frame) {
   MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
   const double cycle=now();
   clock.SkipMissed(static_cast<int64_t>(cycle*1e6));
   pl_swapchain_frame target{}; if(!pl_swapchain_start_frame(sw,&target))return 8;
   const double acquiredAt=now();
   const float black[4]={0.01f,0.01f,0.01f,1.0f}; pl_tex_clear(d->gpu,target.fbo,black);
   const double submitAt=now(); if(!pl_swapchain_submit_frame(sw))return 9;
   const double start=now(); const HRESULT present=native->Present(1,0); const double duration=now()-start;
   if(present!=S_OK)++errors; if(frame>=12)samples.push_back(duration);
   DXGI_FRAME_STATISTICS stats{}; const HRESULT statResult=native->GetFrameStatistics(&stats); if(SUCCEEDED(statResult))++statsOk;
   native->GetLastPresentCount(&lastCount); bool feedback=false;
   if(present==S_OK)clock.Submitted();
   if(SUCCEEDED(statResult)) feedback=clock.ObserveQueueTail(lastCount,stats.PresentCount,stats.PresentRefreshCount,
       stats.SyncRefreshCount,static_cast<int64_t>(stats.SyncQPCTime.QuadPart*(1e9/frequency.QuadPart)),
       static_cast<int64_t>(now()*1e6),static_cast<int64_t>(start*1e6),applied);
   if(feedback)++feedbackCount;
   fprintf(detail,"%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%u,%ld,%u,%u,%u,%lld,%d\n",owned?"vp":"libplacebo",depth,frame,
       now()-cycle,acquiredAt-cycle,start-submitAt,duration,lastCount,long(statResult),stats.PresentCount,
       stats.PresentRefreshCount,stats.SyncRefreshCount,stats.SyncQPCTime.QuadPart,feedback?1:0);
   if(frame==28)Sleep(40); // queued output may drain; no catch-up timer is used.
  }
  native->GetLastPresentCount(&lastCount); std::sort(samples.begin(),samples.end()); double sum=0; for(double v:samples)sum+=v;
  printf("{\"owner\":\"%s\",\"requested\":%d,\"applied\":%u,\"set_hr\":%ld,\"get_hr\":%ld,\"buffers\":%u,\"present_errors\":%u,\"present_count\":%u,\"stats_samples\":%u,\"feedback_samples\":%u,\"block_avg_ms\":%.4f,\"block_p50_ms\":%.4f,\"block_max_ms\":%.4f}\n",owned?"vp":"libplacebo",depth,applied,long(set),long(get),actual.BufferCount,errors,lastCount,statsOk,feedbackCount,sum/samples.size(),samples[samples.size()/2],samples.back()); fflush(stdout);
  native.Reset(); pl_swapchain_destroy(&sw); chain.Reset(); dxgi.Reset(); pl_d3d11_destroy(&d);
 }
 fclose(detail); DestroyWindow(window); return 0;
}
