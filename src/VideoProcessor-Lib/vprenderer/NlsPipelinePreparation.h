#pragma once

#include <libplacebo/cache.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/options.h>
#include <libplacebo/shaders/custom.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// Only CPU-owned snapshots cross this boundary. The compiler owns its device,
// immediate context, renderer, hooks and textures. pl_cache is explicitly
// thread-safe; the owner must Stop() before destroying that cache or its DLL.
class NlsPipelinePreparation {
public:
    enum class Status { Missing, Pending, Ready, Failed };

    struct Lut {
        pl_custom_lut value{};
        std::vector<float> data;
        const pl_custom_lut* Copy(const pl_custom_lut* input) {
            if (!input) return nullptr;
            value = *input;
            const size_t count = size_t(input->size[0]) *
                std::max(1, input->size[1]) * std::max(1, input->size[2]) * 3;
            if (!input->data || !count || count > 64u * 1024u * 1024u)
                throw std::runtime_error("invalid preparation LUT");
            data.assign(input->data, input->data + count);
            value.data = data.data();
            return &value;
        }
    };

    struct Frame {
        pl_frame value{};
        pl_tex_params textures[PL_MAX_PLANES]{};
        std::string formats[PL_MAX_PLANES];
        pl_dovi_metadata dovi{};
        std::vector<unsigned char> profile;
        Lut lut;
        void Copy(const pl_frame& input) {
            // VP uses profile bytes, not externally owned ICC objects. Refuse
            // an unsupported contract instead of borrowing GPU-backed state.
            if (input.icc || input.acquire || input.release || input.film_grain.type == PL_FILM_GRAIN_H274 || input.num_planes <= 0 ||
                input.num_planes > PL_MAX_PLANES)
                throw std::runtime_error("unsupported preparation frame");
            value = input;
            value.prev = value.next = nullptr;
            value.user_data = nullptr;
            value.overlays = nullptr;
            value.num_overlays = 0; // UI overlays have their own warm draw passes.
            if (input.repr.dovi) { dovi = *input.repr.dovi; value.repr.dovi = &dovi; }
            if (input.profile.data && input.profile.len) {
                const auto bytes = static_cast<const unsigned char*>(input.profile.data);
                profile.assign(bytes, bytes + input.profile.len);
                value.profile.data = profile.data();
            }
            value.lut = lut.Copy(input.lut);
            for (int i = 0; i < input.num_planes; ++i) {
                if (!input.planes[i].texture) throw std::runtime_error("missing preparation texture");
                textures[i] = input.planes[i].texture->params;
                formats[i] = textures[i].format->name;
                textures[i].format = nullptr;
                textures[i].initial_data = nullptr;
                textures[i].user_data = nullptr;
                value.planes[i].texture = nullptr;
            }
        }
        bool Create(pl_gpu gpu) {
            for (int i = 0; i < value.num_planes; ++i) {
                auto p = textures[i];
                p.format = pl_find_named_fmt(gpu, formats[i].c_str());
                if (!p.format) return false;
                p.export_handle = p.import_handle = static_cast<pl_handle_type>(0);
                p.shared_mem = {};
                p.blit_dst = true;
                value.planes[i].texture = pl_tex_create(gpu, &p);
                if (!value.planes[i].texture) return false;
                const float black[4] = {0, 0, 0, 1};
                pl_tex_clear(gpu, value.planes[i].texture, black);
            }
            return true;
        }
        void Destroy(pl_gpu gpu) {
            for (int i = 0; i < value.num_planes; ++i)
                pl_tex_destroy(gpu, &value.planes[i].texture);
        }
    };

    struct Request {
        Frame source, target;
        Lut lut;
        pl_options options = nullptr;
        pl_blend_params blend{};
        pl_error_diffusion_kernel diffusion{};
        LUID adapter{};
        bool noCompute = false;
        int featureLevel = 0;
        std::string shader;
        float stretch = 1.0f, axis = 0.0f;
        float mixDuration = 0.5f;
        bool processedSource = false;
        bool subtitleOverlay = false;
        Request(const pl_frame& input, const pl_frame& output,
            const pl_render_params& params, std::string text)
            : shader(std::move(text)) {
            source.Copy(input);
            target.Copy(output);
            options = pl_options_alloc(nullptr);
            if (!options) throw std::bad_alloc();
            try {
                pl_options_reset(options, &params);
                options->params.hooks = nullptr;
                options->params.num_hooks = 0;
                options->params.info_callback = nullptr;
                options->params.info_priv = nullptr;
                options->params.lut = lut.Copy(params.lut);
                // pl_options_reset does not redirect these two pointers.
                if (params.blend_params) { blend = *params.blend_params; options->params.blend_params = &blend; }
                if (params.error_diffusion) { diffusion = *params.error_diffusion; options->params.error_diffusion = &diffusion; }
            } catch (...) { pl_options_free(&options); throw; }
        }
        ~Request() { pl_options_free(&options); }
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;

        bool Compile(pl_cache cache) {
            struct Resources {
                pl_d3d11 device = nullptr;
                pl_renderer renderer = nullptr;
                const pl_hook* hook = nullptr;
                Request& request;
                explicit Resources(Request& r) : request(r) {}
                ~Resources() {
                    if (!device) return;
                    request.source.Destroy(device->gpu);
                    request.target.Destroy(device->gpu);
                    pl_mpv_user_shader_destroy(&hook);
                    pl_renderer_destroy(&renderer);
                    pl_d3d11_destroy(&device);
                }
            } owned(*this);
            pl_d3d11_params p{};
            p.adapter_luid = adapter;
            p.no_compute = noCompute;
            p.min_feature_level = p.max_feature_level = featureLevel;
            owned.device = pl_d3d11_create(nullptr, &p);
            if (!owned.device) return false;
            const auto gpu = owned.device->gpu;
            pl_gpu_set_cache(gpu, cache);
            owned.renderer = pl_renderer_create(nullptr, gpu);
            if (!shader.empty()) owned.hook = pl_mpv_user_shader_parse(gpu, shader.data(), shader.size());
            if (processedSource) {
                const auto format = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 16, 0,
                    static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_BLITTABLE));
                if (!format) return false;
                target.formats[0] = format->name;
                target.value.repr = pl_color_repr_rgb;
                target.value.repr.levels = PL_COLOR_LEVELS_FULL;
                target.value.repr.alpha = PL_ALPHA_NONE;
            }
            if (!owned.renderer || (!shader.empty() && !owned.hook) || !source.Create(gpu) || !target.Create(gpu)) return false;
            for (int i = 0; owned.hook && i < owned.hook->num_parameters; ++i) {
                const auto& parameter = owned.hook->parameters[i];
                if (!parameter.name || !parameter.data || parameter.type != PL_VAR_FLOAT) continue;
                if (std::string(parameter.name) == "stretch_ratio") parameter.data->f = stretch;
                if (std::string(parameter.name) == "warp_axis") parameter.data->f = axis;
            }
            auto params = options->params;
            params.hooks = owned.hook ? &owned.hook : nullptr;
            params.num_hooks = owned.hook ? 1 : 0;
            if (processedSource) {
                params.frame_mixer = nullptr;
                params.dither_params = nullptr; params.error_diffusion = nullptr; params.blend_params = nullptr;
            }
            // Neighbour metadata/textures are identical synthetic inputs; only
            // the shader topology matters. Prepare both field directions.
            pl_frame neighbor = source.value;
            if (source.value.field != PL_FIELD_NONE)
                source.value.prev = source.value.next = &neighbor;
            if (!pl_render_image(owned.renderer, &source.value, &target.value, &params)) return false;
            if (source.value.field != PL_FIELD_NONE) {
                source.value.field = pl_field_other(source.value.field);
                if (!pl_render_image(owned.renderer, &source.value, &target.value, &params)) return false;
            }
            if (source.value.field != PL_FIELD_NONE && params.deinterlace_params &&
                params.deinterlace_params->algo != PL_DEINTERLACE_BOB) {
                auto fallback = *params.deinterlace_params;
                fallback.algo = PL_DEINTERLACE_BOB;
                auto bob = params; bob.deinterlace_params = &fallback;
                source.value.prev = source.value.next = nullptr;
                for (int field = 0; field < 2; ++field) {
                    if (!pl_render_image(owned.renderer, &source.value, &target.value, &bob)) return false;
                    source.value.field = pl_field_other(source.value.field);
                }
                source.value.prev = source.value.next = &neighbor;
            }
            // Per-source subtitle layers are composited before scaling/tone mapping.
            // Prepare both presence/absence; only this private device owns the texture.
            if (subtitleOverlay) {
                pl_overlay_part part{};
                part.src = {0,0,float(source.textures[0].w),float(source.textures[0].h)};
                part.dst = part.src;
                pl_overlay overlay{}; overlay.tex = source.value.planes[0].texture;
                overlay.mode = PL_OVERLAY_NORMAL; overlay.coords = PL_OVERLAY_COORDS_SRC_FRAME;
                overlay.repr = pl_color_repr_rgb; overlay.repr.levels = PL_COLOR_LEVELS_FULL;
                overlay.repr.alpha = PL_ALPHA_INDEPENDENT; overlay.color = source.value.color;
                overlay.parts = &part; overlay.num_parts = 1;
                source.value.overlays = &overlay; source.value.num_overlays = 1;
                const bool rendered = pl_render_image(owned.renderer, &source.value, &target.value, &params);
                source.value.overlays = nullptr; source.value.num_overlays = 0;
                if (!rendered) return false;
            }
            if (params.frame_mixer) {
                using Timestamp = std::remove_const_t<std::remove_pointer_t<decltype(pl_frame_mix{}.timestamps)>>;
                // Match real capture spacing and output duration. Compressing
                // sixteen samples into a fraction of a frame invents an
                // oversized blend that the live cadence never requested.
                // Cover bounded startup/history sizes and fractional phases.
                const pl_frame* frames[16]{};
                uint64_t signatures[16]{};
                Timestamp timestamps[16]{};
                for (int count = 1; count <= 16; ++count) for (float phase : {0.0f, 0.25f, 0.5f, 0.75f}) {
                    for (int i = 0; i < count; ++i) {
                        frames[i] = &source.value;
                        signatures[i] = uint64_t(i + 1);
                        timestamps[i] = Timestamp(i - count / 2 + phase);
                    }
                    pl_frame_mix mix{};
                    mix.num_frames = count; mix.frames = frames;
                    mix.signatures = signatures; mix.timestamps = timestamps; mix.vsync_duration = mixDuration;
                    if (!pl_render_image_mix(owned.renderer, &mix, &target.value, &params)) return false;
                    pl_gpu_finish(gpu); // Bound background GPU work in flight.
                }
            }
            const auto errors = pl_renderer_get_errors(owned.renderer);
            for (int i = 0; i < errors.num_disabled_hooks; ++i)
                if (owned.hook && errors.disabled_hooks[i] == owned.hook->signature) return false;
            pl_gpu_finish(gpu);
            return true;
        }
    };

    ~NlsPipelinePreparation() { Stop(); }
    Status Get(const std::string& key) const {
        const auto publication = std::atomic_load(&current);
        if (publication && publication->key == key)
            return publication->status.load(std::memory_order_acquire);
        const auto ready = std::atomic_load(&completed);
        return ready && std::find(ready->begin(), ready->end(), key) != ready->end()
            ? Status::Ready : Status::Missing;
    }
    // A replacement cancels pending work, not an in-flight driver call. A
    // superseded completion can populate the content-addressed cache but can
    // never activate a different selection. No lock is held while compiling.
    bool Submit(const std::string& key, std::function<bool()> compile) {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || stopping) return false;
        if (!worker.joinable()) worker = std::thread([this] { Run(); });
        auto publication = std::make_shared<Publication>(key);
        ++generation;
        pending = std::move(compile);
        std::atomic_store(&current, std::move(publication));
        changed.notify_one();
        return true;
    }
    // Selection changes cancel obsolete work but may retain successful exact
    // pipeline contracts. Device/settings invalidation still forgets everything.
    void Invalidate(bool forgetCompleted = true) {
        std::lock_guard<std::mutex> lock(mutex);
        if (forgetCompleted) std::atomic_store(&completed, std::shared_ptr<const ReadyKeys>{});
        ++generation;
        std::atomic_store(&current, std::shared_ptr<Publication>{});
        pending = {};
    }
    // Called by the renderer's existing background retirement, never a UI
    // command handler. The cache and module remain alive until this returns.
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            std::atomic_store(&completed, std::shared_ptr<const ReadyKeys>{});
            pending = {};
            ++generation;
            std::atomic_store(&current, std::shared_ptr<Publication>{});
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }
private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::function<bool()> pending;
    struct Publication {
        std::string key;
        std::atomic<Status> status{Status::Pending};
        explicit Publication(const std::string& identity) : key(identity) {}
    };
    using ReadyKeys = std::vector<std::string>;
    // Immutable bounded snapshots keep Get off the compiler mutex. No GPU
    // resources are retained, only successful preparation identities.
    std::shared_ptr<const ReadyKeys> completed;
    std::shared_ptr<Publication> current;
    uint64_t generation = 0;
    bool stopping = false;
    void Run() {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            std::function<bool()> compile;
            uint64_t workGeneration;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [this] { return stopping || bool(pending); });
                if (stopping) break;
                compile = std::move(pending);
                workGeneration = generation;
            }
            bool success = false;
            try { success = compile(); } catch (...) {}
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!stopping && generation == workGeneration) {
                    const auto publication = std::atomic_load(&current);
                    if (publication) {
                        if (success) {
                            const auto previous = std::atomic_load(&completed);
                            auto ready = std::make_shared<ReadyKeys>(previous ? *previous : ReadyKeys{});
                            ready->erase(std::remove(ready->begin(), ready->end(), publication->key), ready->end());
                            if (ready->size() >= 16) ready->erase(ready->begin());
                            ready->push_back(publication->key);
                            std::atomic_store(&completed, std::shared_ptr<const ReadyKeys>(std::move(ready)));
                        }
                        publication->status.store(success ? Status::Ready : Status::Failed,
                            std::memory_order_release);
                    }
                }
            }
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
    }
};
