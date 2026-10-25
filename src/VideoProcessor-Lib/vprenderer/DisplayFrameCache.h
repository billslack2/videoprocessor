#pragma once
#include <libplacebo/gpu.h>

// Owns final output pixels independently of capture and swapchain storage.
// Clear before releasing the associated GPU. Access only on the render thread.
class DisplayFrameCache
{
public:
    DisplayFrameCache() = default;
    DisplayFrameCache(const DisplayFrameCache&) = delete;
    DisplayFrameCache& operator=(const DisplayFrameCache&) = delete;
    ~DisplayFrameCache() { Clear(); }
    void Clear()
    {
        if (gpu) pl_tex_destroy(gpu, &texture);
        gpu = nullptr;
        valid = false;
    }
    bool Capture(pl_gpu device, pl_tex source)
    {
        valid = false;
        if (!device || !source || !source->params.blit_src) return false;
        if (gpu != device) { Clear(); gpu = device; }
        pl_tex_params parameters{};
        parameters.w = source->params.w;
        parameters.h = source->params.h;
        parameters.format = source->params.format;
        parameters.blit_src = parameters.blit_dst = true;
        if (!pl_tex_recreate(gpu, &texture, &parameters)) return false;
        pl_tex_blit_params copy{};
        copy.src = source; copy.dst = texture;
        pl_tex_blit(gpu, &copy);
        valid = true;
        return true;
    }
    bool CopyTo(pl_tex target) const
    {
        if (!valid || !texture || !target || !target->params.blit_dst ||
            target->params.w != texture->params.w || target->params.h != texture->params.h ||
            target->params.format != texture->params.format) return false;
        pl_tex_blit_params copy{};
        copy.src = texture; copy.dst = target;
        pl_tex_blit(gpu, &copy);
        return true;
    }
private:
    pl_gpu gpu = nullptr;
    pl_tex texture = nullptr;
    bool valid = false;
};
