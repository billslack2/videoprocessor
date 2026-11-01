#pragma once
#include "CppUnitTest.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/cache.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
namespace Tests {
// Compiled program bytes only: never retain a GPU, renderer, texture, hook or
// frame between tests. Cold MotionRendererTests own unrelated private caches.
struct SubtitleFixtureProgramCache {
    LUID adapter{};D3D_FEATURE_LEVEL featureLevel{};
    std::atomic<uint64_t> misses{0};pl_cache value=nullptr;
    SubtitleFixtureProgramCache(LUID id,D3D_FEATURE_LEVEL level):adapter(id),featureLevel(level) {
        auto params=pl_cache_default_params;
        params.max_object_size=16u*1024u*1024u;params.max_total_size=128u*1024u*1024u;
        params.priv=this;
        params.get=[](void* context,uint64_t)->pl_cache_obj {
            ++static_cast<SubtitleFixtureProgramCache*>(context)->misses;
            return {}; // Observe a real internal miss; no disk/external fallback.
        };
        value=pl_cache_create(&params);
    }
    ~SubtitleFixtureProgramCache(){pl_cache_destroy(&value);}
    static std::shared_ptr<SubtitleFixtureProgramCache> For(pl_d3d11 device) {
        CComPtr<IDXGIDevice> dxgi;CComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC description{};
        Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(SUCCEEDED(device->device->QueryInterface(IID_PPV_ARGS(&dxgi))));
        Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&description)));
        const auto level=device->device->GetFeatureLevel();
        static std::mutex mutex;
        static std::vector<std::shared_ptr<SubtitleFixtureProgramCache>> caches;
        std::lock_guard<std::mutex> guard(mutex);
        for(const auto& cache:caches)if(cache->adapter.LowPart==description.AdapterLuid.LowPart&&
            cache->adapter.HighPart==description.AdapterLuid.HighPart&&cache->featureLevel==level)return cache;
        auto cache=std::make_shared<SubtitleFixtureProgramCache>(description.AdapterLuid,level);
        // At most256MiB retained per module; unusual additional adapters use an
        // equally bounded fixture-local cache instead of growing the registry.
        if(caches.size()<2)caches.push_back(cache);
        return cache;
    }
};
}
