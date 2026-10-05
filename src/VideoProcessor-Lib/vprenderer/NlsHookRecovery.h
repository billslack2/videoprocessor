#pragma once
#include <libplacebo/renderer.h>

namespace NlsHookRecovery
{
    inline void ResetFailedHook(pl_renderer renderer, uint64_t signature)
    {
        if (!renderer || !signature) return;
        pl_render_errors selected{};
        selected.errors = PL_RENDER_ERR_HOOKS;
        selected.disabled_hooks = &signature;
        selected.num_disabled_hooks = 1;
        pl_renderer_reset_errors(renderer, &selected);
    }

    struct Result
    {
        bool rendered = false;
        bool retried = false;
        uint64_t failedSignature = 0;
        unsigned errors = 0;
    };

    // libplacebo may return success after disabling a failing user hook. Never
    // present that unintended full-screen linear stretch. One retry uses the
    // already-selected source pixels and rebuilt ordinary destination/overlays.
    template<class RebuildFallback>
    Result Render(pl_renderer renderer, pl_gpu gpu, const pl_frame& source,
        pl_frame& target, const pl_frame& fullTarget, pl_render_params& params,
        const pl_hook* activeNls, RebuildFallback rebuildFallback)
    {
        Result result;
        result.rendered = pl_render_image(renderer, &source, &target, &params);
        const auto errors = pl_renderer_get_errors(renderer);
        result.errors = static_cast<unsigned>(errors.errors);
        if (!activeNls) return result;
        bool disabled = false;
        for (int i = 0; i < errors.num_disabled_hooks; ++i)
            disabled |= errors.disabled_hooks[i] == activeNls->signature;
        if (!disabled) return result;
        result.retried = true;
        result.failedSignature = activeNls->signature;
        params.hooks = nullptr;
        params.num_hooks = 0;
        rebuildFallback();
        const float black[] = { 0.0f, 0.0f, 0.0f };
        pl_frame_clear(gpu, &fullTarget, black);
        result.rendered = pl_render_image(renderer, &source, &target, &params);
        return result;
    }
}
