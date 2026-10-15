#pragma once
#include <ActivePictureDecisionTimeline.h>
#include <libplacebo/renderer.h>
#include <array>

// Lives on the stack for one source's output loop. Owns no GPU textures or
// source pixels. Reuse only the uploaded planes; color/DV/crop metadata must
// still be constructed for each output and temporal history owns its copies.
struct SourceUploadBatch {
    bool Restore(bool repeated, const ActivePictureFrameIdentity& current,
        pl_gpu device, int width, int height, const pl_tex* resources,
        int expectedPlanes, pl_frame& output) const {
        if (!repeated || !valid || !SameAnalysisFrameIdentity(identity,current) ||
            gpu!=device || w!=width || h!=height || count!=expectedPlanes) return false;
        for (int i=0;i<count;++i)
            if (!resources[i] || planes[i].texture!=resources[i]) return false;
        output.num_planes=count;
        for (int i=0;i<count;++i) output.planes[i]=planes[i];
        return true;
    }
    void Store(const ActivePictureFrameIdentity& current, pl_gpu device,
        int width,int height,const pl_frame& uploaded) {
        valid=false;
        if (uploaded.num_planes<1 || uploaded.num_planes>2) return;
        for (int i=0;i<uploaded.num_planes;++i) if (!uploaded.planes[i].texture) return;
        identity=current; gpu=device; w=width; h=height; count=uploaded.num_planes;
        for (int i=0;i<count;++i) planes[i]=uploaded.planes[i];
        valid=true;
    }
private:
    ActivePictureFrameIdentity identity{};
    pl_gpu gpu=nullptr;
    std::array<pl_plane,2> planes{};
    int w=0,h=0,count=0;
    bool valid=false;
};
