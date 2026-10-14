#pragma once
#include <MotionConfig.h>
#include <libplacebo/renderer.h>
#include <libplacebo/gpu.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <vector>
#include <string>
#include <cmath>
#include <type_traits>

// Render-thread-only ownership. Every history plane is a private GPU texture;
// capture buffers and the reusable upload textures are never retained here.
class TemporalRenderer {
    struct Stored {
        pl_frame frame{};
        uint64_t signature=0;
        double pts=0;
        pl_gpu gpu=nullptr;
        pl_tex textures[PL_MAX_PLANES]{};
        ~Stored(){if(gpu)for(auto& texture:textures)pl_tex_destroy(gpu,&texture);}
    };
    std::deque<std::unique_ptr<Stored>> history;
    pl_gpu gpu=nullptr;
    uint64_t generation=0,lastSequence=0,nextSignature=1;
    double previousPts=0,nextOutput=0,sourceDuration=0,displayDuration=0,measuredDisplayHz=0;
    bool initialized=false,interlaced=false,topFirst=true,mixing=false;
    int delay=0;
    MotionConfig::Settings settings;
    const pl_filter_config* mixer=nullptr;
    pl_deinterlace_params deinterlace{};
public:
    std::vector<double> outputs;
    double currentPts=0,outputPts=0;
    bool skipOutput=false;
    pl_field lastSubmittedField=PL_FIELD_NONE;
    size_t HistoryDepth() const { return history.size(); }
    const pl_deinterlace_params& DeinterlaceParameters() const { return deinterlace; }
    const pl_filter_config* MixerParameters() const { return mixer; }
    std::string status="Motion: Off";
    void Clear(){history.clear();initialized=false;lastSequence=0;outputs.clear();}
    void Bypass(const std::string& reason){Clear();mixing=false;interlaced=false;skipOutput=false;status=reason;}
    bool Active() const{return mixing||interlaced;}
    bool Mixing() const{return mixing;}
    double DelayMs() const{return Active()?delay*sourceDuration*1000.0:0;}
    void Begin(const MotionConfig::Settings& requested,double sourceHz,double displayHz,
        bool inputInterlaced,int inputOrder,uint64_t epoch,uint64_t sequence,double pts,bool discontinuity) {
        outputs.clear();skipOutput=false;currentPts=pts;
        if(!std::isfinite(sourceHz)||sourceHz<=0||!std::isfinite(pts)) {
            Bypass("Motion: source cadence unavailable");outputs.push_back(pts);outputPts=pts;return;
        }
        if(!std::isfinite(displayHz)||displayHz<0)displayHz=0;
        const bool newTopFirst=requested.fieldOrder=="top"||(requested.fieldOrder=="auto"&&inputOrder==1);
        const bool knownOrder=requested.fieldOrder!="auto"||inputOrder!=0;
        const bool newInterlaced=requested.deinterlace!="off"&&requested.algorithm!="weave"&&knownOrder&&
            (inputInterlaced||requested.deinterlace=="force");
        const double nominalHz=sourceHz*(newInterlaced&&requested.cadence=="field"?2.0:1.0);
        const bool newMix=MotionConfig::Mix(requested,nominalHz,displayHz);
        const bool changed=settings.Fingerprint()!=requested.Fingerprint()||generation!=epoch||
            interlaced!=newInterlaced||mixing!=newMix||(newInterlaced&&topFirst!=newTopFirst)||
            (sourceDuration>0&&std::abs(sourceDuration-1.0/sourceHz)>1e-7)||
            (std::abs(measuredDisplayHz-displayHz)>(std::max)(0.1,displayHz*0.001));
        if(changed||discontinuity||(initialized&&(sequence!=lastSequence+1||pts<=previousPts||pts-previousPts>2.5/sourceHz)))Clear();
        settings=requested;generation=epoch;interlaced=newInterlaced;mixing=newMix;
        topFirst=newTopFirst;measuredDisplayHz=displayHz;
        sourceDuration=1.0/sourceHz;
        deinterlace.algo=requested.algorithm=="bob"?PL_DEINTERLACE_BOB:requested.algorithm=="yadif"?PL_DEINTERLACE_YADIF:PL_DEINTERLACE_BWDIF;
        deinterlace.skip_spatial_check=requested.skipSpatial;
        mixer=nullptr;
        for(const pl_filter_preset* p=pl_frame_mixers;p->name;++p)
            if(requested.mixer==p->name){mixer=p->filter;break;}
        delay=mixing?static_cast<int>(std::ceil(mixer&&mixer->kernel?mixer->kernel->radius:1.0))+1:interlaced?1:0;
        if(!Active()) {
            outputs.push_back(pts);outputPts=pts;
            status=requested.smoothMotion!="off"&&displayHz<=0?"Motion: display cadence unavailable":
                requested.deinterlace!="off"&&inputInterlaced&&!knownOrder?"Deinterlacing: field order unavailable (select Top/Bottom first)":"Motion: bypassed";
            if(requested.smoothMotion=="off"&&!inputInterlaced)status="Motion: Off";
            previousPts=pts;lastSequence=sequence;return;
        }
        // Without a measured display rate, deinterlacing uses its own nominal
        // field/frame clock. Mixing never guesses the display cadence.
        const double outputHz=mixing?displayHz:(displayHz>0?(std::min)(nominalHz,displayHz):nominalHz);
        displayDuration=1.0/outputHz;
        const double start=pts-delay*sourceDuration;
        if(!initialized){nextOutput=start;initialized=true;}
        const double end=start+sourceDuration;
        while(nextOutput<end-1e-8&&outputs.size()<16){outputs.push_back(nextOutput);nextOutput+=displayDuration;}
        if(outputs.size()==16&&nextOutput<end-1e-8){Clear();outputs.push_back(pts);mixing=false;interlaced=false;status="Motion: output cadence exceeds bounded live budget";return;}
        skipOutput=outputs.empty();
        if(!skipOutput)outputPts=outputs.front();
        status=(mixing?"Smooth motion: "+requested.mixer:"Smooth motion: bypassed")+
            (interlaced?"; deinterlacing: "+requested.algorithm+(topFirst?" TFF":" BFF"):"; progressive")+
            "; buffer "+std::to_string(delay)+" frame(s)";
        previousPts=pts;lastSequence=sequence;
    }
    bool Capture(pl_gpu device,const pl_frame& source,uint64_t sequence) {
        gpu=device;if(!Active())return true;
        if(!history.empty()&&history.back()->pts==currentPts)return true;
        // Recycle the oldest slot once the bounded history is full. Copies
        // and draws share the same ordered GPU queue, so reuse cannot overwrite
        // pixels before their earlier render command consumes them.
        std::unique_ptr<Stored> stored;
        if(history.size()==8){stored=std::move(history.front());history.pop_front();}
        else stored=std::make_unique<Stored>();
        stored->gpu=device;stored->pts=currentPts;
        stored->frame=source;stored->frame.overlays=nullptr;stored->frame.num_overlays=0;
        stored->frame.prev=nullptr;stored->frame.next=nullptr;stored->signature=nextSignature++;
        // First null every borrowed plane so partial failure cannot destroy it.
        for(int p=0;p<source.num_planes;++p)stored->frame.planes[p].texture=nullptr;
        for(int p=0;p<source.num_planes;++p){
            const pl_tex original=source.planes[p].texture;
            if(!original||!original->params.blit_src){status="Motion: source texture cannot be retained";return false;}
            pl_tex_params params=original->params;params.initial_data=nullptr;params.user_data=nullptr;
            params.import_handle=static_cast<pl_handle_type>(0);params.export_handle=static_cast<pl_handle_type>(0);
            params.blit_src=true;params.blit_dst=true;params.sampleable=true;
            if(!pl_tex_recreate(device,&stored->textures[p],&params)){status="Motion: history texture allocation failed";return false;}
            stored->frame.planes[p].texture=stored->textures[p];
            if(!stored->frame.planes[p].texture){status="Motion: history texture allocation failed";return false;}
            pl_tex_blit_params copy{};copy.src=original;copy.dst=stored->frame.planes[p].texture;
            pl_tex_blit(device,&copy);
        }
        history.push_back(std::move(stored));
        while(history.size()>8)history.pop_front();
        return true;
    }
    bool Render(pl_renderer renderer,const pl_frame& source,const pl_frame& target,pl_render_params& params) {
        lastSubmittedField=PL_FIELD_NONE;
        if(!Active()||history.empty())return pl_render_image(renderer,&source,&target,&params);
        std::vector<pl_frame> frames;std::vector<const pl_frame*> pointers;
        using Timestamp=std::remove_const_t<std::remove_pointer_t<decltype(pl_frame_mix{}.timestamps)>>;
        std::vector<uint64_t> signatures;std::vector<Timestamp> timestamps;
        frames.reserve(history.size()*2);signatures.reserve(history.size()*2);timestamps.reserve(history.size()*2);
        const bool fields=interlaced&&settings.cadence=="field";
        const double unit=sourceDuration/(fields?2.0:1.0);
        for(size_t i=0;i<history.size();++i){
            for(int f=0;f<(fields?2:1);++f){
                pl_frame frame=history[i]->frame;
                if(interlaced){frame.first_field=topFirst?PL_FIELD_TOP:PL_FIELD_BOTTOM;frame.field=f?pl_field_other(frame.first_field):frame.first_field;
                    frame.prev=i?&history[i-1]->frame:nullptr;
                    frame.next=i+1<history.size()?&history[i+1]->frame:nullptr;
                }
                // Geometry is committed by the current presentation pass; never
                // blend unrelated source rectangles across aspect transitions.
                frame.crop=source.crop;
                frames.push_back(frame);signatures.push_back((history[i]->signature<<1)|f);
                timestamps.push_back(static_cast<Timestamp>((history[i]->pts+f*unit-outputPts)/unit));
            }
        }
        for(auto& frame:frames)pointers.push_back(&frame);
        pl_render_params temporal=params;temporal.deinterlace_params=interlaced?&deinterlace:nullptr;
        if(mixing){temporal.frame_mixer=mixer;pl_frame_mix mix{};mix.num_frames=static_cast<int>(frames.size());mix.frames=pointers.data();mix.signatures=signatures.data();mix.timestamps=timestamps.data();mix.vsync_duration=static_cast<float>(displayDuration/unit);
            return pl_render_image_mix(renderer,&mix,&target,&temporal);
        }
        // A temporal deinterlacer needs both neighbors. During bounded warmup,
        // use Bob rather than asking YADIF/BWDIF to read absent references.
        size_t selected=0;for(size_t i=0;i<frames.size();++i)if(timestamps[i]<=1e-6f)selected=i;
        lastSubmittedField=frames[selected].field;
        if(!frames[selected].prev||!frames[selected].next){auto bob=deinterlace;bob.algo=PL_DEINTERLACE_BOB;temporal.deinterlace_params=&bob;return pl_render_image(renderer,&frames[selected],&target,&temporal);}
        return pl_render_image(renderer,&frames[selected],&target,&temporal);
    }
};
