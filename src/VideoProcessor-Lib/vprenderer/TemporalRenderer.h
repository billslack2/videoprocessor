#pragma once
#include <MotionConfig.h>
#include <vprenderer/FrameHdrStatus.h>
#include <libplacebo/renderer.h>
#include <libplacebo/gpu.h>
#include <libplacebo/shaders/custom.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <functional>
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <type_traits>

// Render-thread-only ownership. Every history plane is a private GPU texture;
// capture buffers and the reusable upload textures are never retained here.
class TemporalRenderer {
public:
    struct PresentationGeometry {
        pl_rect2d familyBase{};
        pl_rect2d sourceCrop{};
        pl_rect2d verifiedVisible{};
        uint64_t viewportEpoch=0;
        int owner=0;
        bool valid=false,visibleSafe=false,emptyBandsSafe=false,cleaned=false;
        uint64_t outputContract=0;
    };
    // Mode selection is not proof that this source picture was cleaned. A
    // rejected/pending cue keeps raw pixels and cannot blend into a repaired
    // picture even if its final crop happens to be identical.
    static bool HasCleanPresentation(bool compositionMode,bool sourceComposed,
        bool exactNoCaptionConfirmed) {
        return compositionMode && (sourceComposed || exactNoCaptionConfirmed);
    }
    static bool CompatiblePresentationGeometry(const PresentationGeometry& a,
        const PresentationGeometry& b,const pl_rect2df& outputCrop) {
        if(!a.valid)return true; // Standalone users without host annotations.
        if(!b.valid||a.viewportEpoch!=b.viewportEpoch||a.owner!=b.owner||
            !SameRect(a.familyBase,b.familyBase)||a.cleaned!=b.cleaned)return false;
        if(a.cleaned&&b.cleaned)return true;
        if(std::abs(outputCrop.x0-b.sourceCrop.x0)<0.5f&&
            std::abs(outputCrop.y0-b.sourceCrop.y0)<0.5f&&
            std::abs(outputCrop.x1-b.sourceCrop.x1)<0.5f&&
            std::abs(outputCrop.y1-b.sourceCrop.y1)<0.5f)return true;
        if(!b.visibleSafe)return false;
        if(b.verifiedVisible.x1<=b.verifiedVisible.x0||
            b.verifiedVisible.y1<=b.verifiedVisible.y0)return b.emptyBandsSafe;
        return outputCrop.x0<=b.verifiedVisible.x0&&
            outputCrop.y0<=b.verifiedVisible.y0&&
            outputCrop.x1>=b.verifiedVisible.x1&&
            outputCrop.y1>=b.verifiedVisible.y1;
    }
    using NativePrepare = std::function<bool(const pl_frame& fieldFrame,
        const pl_deinterlace_params& algorithm,pl_frame& clean,pl_tex& layer,pl_rect2d& bounds)>;
private:
    struct Stored {
        struct Prepared {
            pl_frame frame{};
            pl_tex texture=nullptr,subtitle=nullptr;
            pl_rect2d subtitleBounds{};
            uint64_t prevSignature=0,nextSignature=0,signature=0;
            pl_deinterlace_algorithm algorithm=PL_DEINTERLACE_BOB;
            bool valid=false;
        };
        struct OutputContract {
            pl_rect2df sourceCrop{},targetCrop{},peakCrop{};
            pl_color_space targetColor{};
            pl_color_repr targetRepr{};
            pl_rotation sourceRotation=PL_ROTATION_0,targetRotation=PL_ROTATION_0;
            const pl_custom_lut* targetLut=nullptr;
            pl_icc_object targetIcc=nullptr;
            uint64_t epoch=0,profileSignature=0;
            std::vector<uint64_t> hooks;
            int width=0,height=0;
            bool valid=false;
        };
        struct Processed : OutputContract {
            pl_frame frame{};
            RendererHdrStatus hdrStatus{};
            pl_tex texture=nullptr;
            uint64_t inputSignature=0,signature=0,prevSignature=0,nextSignature=0;
        };
        // Records an actual successful output of this complete packet. It is
        // not a processed texture or a claim about libplacebo's internal cache.
        OutputContract completeOutput;
        bool completeComposition=false;
        std::array<Prepared,2> prepared{};
        std::array<Processed,2> processed{};
        NativePrepare nativePrepare;
        bool hasSubtitle=false;
        RendererHdrStatus hdrSource{};
        bool hdrSourceAvailable=false;
        pl_dovi_metadata dovi{};
        pl_frame frame{};
        pl_tex subtitle=nullptr;
        pl_rect2d subtitleBounds{};
        uint64_t sequence=0;
        PresentationGeometry presentation{};
        uint64_t signature=0;
        double pts=0;
        pl_gpu gpu=nullptr;
        pl_tex textures[PL_MAX_PLANES]{};
        ~Stored(){if(gpu){for(auto& texture:textures)pl_tex_destroy(gpu,&texture);pl_tex_destroy(gpu,&subtitle);
            for(auto& field:prepared){pl_tex_destroy(gpu,&field.texture);pl_tex_destroy(gpu,&field.subtitle);}
            for(auto& field:processed)pl_tex_destroy(gpu,&field.texture);}}
    };
    static bool SameRect(pl_rect2d a,pl_rect2d b) {
        return a.x0==b.x0&&a.y0==b.y0&&a.x1==b.x1&&a.y1==b.y1;
    }
    static bool CompatiblePresentation(const Stored& selected,const Stored& candidate,
        const pl_rect2df& outputCrop) {
        return CompatiblePresentationGeometry(selected.presentation,candidate.presentation,outputCrop);
    }
    std::deque<std::unique_ptr<Stored>> history;
    pl_gpu gpu=nullptr;
    pl_renderer sourceRenderer=nullptr;
    uint64_t sourceRendererEpoch=0;
    bool sourceRendererCompleteMode=false;
    pl_tex compositionSurface=nullptr;
    uint64_t generation=0,lastSequence=0,nextSignature=1;
    double previousPts=0,nextOutput=0,sourceDuration=0,displayDuration=0,measuredDisplayHz=0;
    double submittedOutputPts=0;
    bool submittedOutputAvailable=false;
    uint64_t subtitleFallbackSequence=0;
    const char* subtitleFallbackReason="not-selected";
    bool initialized=false,interlaced=false,topFirst=true,mixing=false,composition=false,nativeMode=false;
    bool lastSourceDiscontinuity=false;
    int delay=0;
    MotionConfig::Settings settings;
    const pl_filter_config* mixer=nullptr;
    pl_deinterlace_params deinterlace{};
public:
    struct ProcessingStatistics {
        uint64_t sourceRenders=0,sourceReuses=0,outputRenders=0;
        uint64_t sourceRendererCreations=0,sourceRendererHistoryFlushes=0;
        uint64_t subtitleFallbackOutputs=0;
        uint64_t completeOutputRenders=0;
    };
    const ProcessingStatistics& ProcessingSnapshot() const { return processingStatistics; }
private:
    ProcessingStatistics processingStatistics;
    RendererHdrStatus lastHdrStatus{};
    bool lastHdrStatusAvailable=false;
    void ResetHistory(bool keepProcessingRenderer) {
        lastHdrStatusAvailable=false;lastHdrStatus={};
        submittedOutputAvailable=false;subtitleFallbackSequence=0;subtitleFallbackReason="history-reset";history.clear();
        if(keepProcessingRenderer && sourceRenderer) {
            // Discard all temporal/peak evidence, retaining only the renderer's
            // reusable GPU programs. A queue gap grants no old-pixel authority.
            pl_renderer_flush_cache(sourceRenderer);
            pl_renderer_reset_errors(sourceRenderer,nullptr);
            ++processingStatistics.sourceRendererHistoryFlushes;
        } else {
            pl_renderer_destroy(&sourceRenderer);sourceRendererEpoch=0;sourceRendererCompleteMode=false;
        }
        if(gpu)pl_tex_destroy(gpu,&compositionSurface);
        nativeMode=false;initialized=false;lastSequence=0;lastSubtitleSequence=0;lastSourceDiscontinuity=false;outputs.clear();
    }
public:
    std::vector<double> outputs;
    double currentPts=0,outputPts=0;
    bool skipOutput=false;
    pl_field lastSubmittedField=PL_FIELD_NONE;
    uint64_t lastSubtitleSequence=0;
    size_t HistoryDepth() const { return history.size(); }
    bool CompositionEnabled() const { return composition; }
    bool HdrStatusSnapshot(RendererHdrStatus& result) const {
        result=lastHdrStatus;return lastHdrStatusAvailable;
    }
    bool AnnotateHdrSource(uint64_t sequence,const pl_frame& frame,
        const DolbyVisionLuminanceStatus& dv={}) {
        if(history.empty()||history.back()->sequence!=sequence)return false;
        auto& packet=*history.back();
        packet.hdrSource=FrameHdrStatus::Source(frame,sequence,dv);
        packet.hdrSourceAvailable=true;return true;
    }
    bool NativeCompositionRequired() const { return composition&&interlaced&&Active(); }
    bool CompositionOutputReady() const {
        if(!composition||!Active())return true;
        for(const auto& packet:history)
            if(packet->pts<=outputPts+1e-8)return true;
        return false;
    }
    uint64_t LastSubtitleSequence() const { return lastSubtitleSequence; }
    uint64_t SelectedPacketSequence() const {
        if(subtitleFallbackSequence)return subtitleFallbackSequence;
        uint64_t selected=0;
        for(const auto& packet:history)
            if(packet->pts<=outputPts+1e-8)selected=packet->sequence;
        return selected;
    }
    double SelectedPacketPts() const {
        if(subtitleFallbackSequence)for(const auto& packet:history)
            if(packet->sequence==subtitleFallbackSequence)return packet->pts;
        double selected=0.0;
        for(const auto& packet:history)
            if(packet->pts<=outputPts+1e-8)selected=packet->pts;
        return selected;
    }
    double SourceDurationSeconds() const { return sourceDuration; }
    uint64_t SelectedSubtitleSequence() const {
        if(subtitleFallbackSequence)return subtitleFallbackSequence;
        uint64_t selected=0;
        for(const auto& packet:history)
            if(packet->pts<=outputPts+1e-8)selected=(packet->subtitle||packet->hasSubtitle)?packet->sequence:0;
        return selected;
    }
    bool HasCurrentFrame(uint64_t sequence) const { return !history.empty() && history.back()->sequence==sequence; }
    bool HasCurrentCompleteComposition(uint64_t sequence) const {
        return mixing&&composition&&!interlaced&&!history.empty()&&
            history.back()->sequence==sequence&&history.back()->pts==currentPts&&
            history.back()->completeComposition;
    }
    bool AnnotatePresentation(uint64_t sequence,const PresentationGeometry& geometry) {
        if(history.empty()||history.back()->sequence!=sequence)return false;
        history.back()->presentation=geometry;
        return true;
    }
    bool SubtitleFallbackActive() const { return subtitleFallbackSequence!=0; }
    const char* SubtitleFallbackReason() const { return subtitleFallbackReason; }
    bool CompleteCompositionSelected() const {
        if(!mixing||!composition||interlaced)return false;
        const auto selected=SelectedPacketSequence();
        for(const auto& packet:history)
            if(packet->sequence==selected)return packet->completeComposition;
        return false;
    }
    const char* ProcessingPath() const {
        return CompleteCompositionSelected()?"complete_packet_library_mix":"source_before_blend";
    }
    // A failed source cue may repeat one complete processed picture. It never
    // applies an old layer/cleanup to current pixels or renews source evidence.
    // Two missing selected source intervals are allowed; output repeats do not
    // advance this bound, and the next valid source cue always wins.
    bool SelectSubtitleFallback(uint64_t epoch,uint64_t viewportEpoch,
        const pl_frame& source,const pl_frame& target,const pl_render_params& params,
        int outputWidth,int outputHeight,uint64_t outputContract=0) {
        subtitleFallbackSequence=0;subtitleFallbackReason="not-eligible";
        if(!mixing||!composition||interlaced||history.empty())return false;
        const Stored* selected=nullptr;
        for(const auto& packet:history)if(packet->pts<=outputPts+1e-8)selected=packet.get();
        if(!selected)return false;
        if(selected->subtitle||selected->hasSubtitle) {
            subtitleFallbackReason="current-cue";return false;
        }
        if(!selected->presentation.valid||selected->presentation.viewportEpoch!=viewportEpoch||
            (outputContract&&selected->presentation.outputContract!=outputContract))
            return false;
        subtitleFallbackReason="no-complete-packet";
        for(auto it=history.rbegin();it!=history.rend();++it) {
            const auto& packet=**it;
            if(packet.sequence>=selected->sequence||!(packet.subtitle||packet.hasSubtitle))continue;
            if(!WithinSubtitleFallbackWindow(packet,*selected)) {
                subtitleFallbackReason="source-window-expired";return false;
            }
            const auto& a=packet.presentation;const auto& b=selected->presentation;
            if(!a.valid||!a.cleaned||a.viewportEpoch!=viewportEpoch||
                (outputContract&&a.outputContract!=outputContract)||
                !SameRect(a.familyBase,b.familyBase)||!SameRect(a.sourceCrop,b.sourceCrop)||
                packet.completeComposition!=selected->completeComposition ||
                !MatchesProcessedOutput(packet.completeComposition?packet.completeOutput:
                    static_cast<const Stored::OutputContract&>(packet.processed[0]),
                    source,target,params,epoch,outputWidth,outputHeight,outputContract!=0)) {
                subtitleFallbackReason="context-or-cache-mismatch";return false;
            }
            subtitleFallbackSequence=packet.sequence;
            subtitleFallbackReason="complete-packet-repeat";return true;
        }
        return false;
    }
    const pl_deinterlace_params& DeinterlaceParameters() const { return deinterlace; }
    const pl_filter_config* MixerParameters() const { return mixer; }
    std::string status="Motion: Off";
    void MarkSubmitted(){if(Active() && SelectedPacketSequence()){submittedOutputPts=outputPts;submittedOutputAvailable=true;}}
    // Full resource release remains mandatory for renderer/device teardown.
    void Clear(){ResetHistory(false);}
    // Same-device queue recovery drops every packet and HDR/mix observation.
    // Keeping compiled programs prevents recovery from recompiling each frame.
    void ResetHistoryAfterSourceGap(){ResetHistory(true);}
    void Bypass(const std::string& reason){Clear();mixing=false;interlaced=false;skipOutput=false;status=reason;}
    bool Active() const{return mixing||interlaced;}
    bool Mixing() const{return mixing;}
    double ScheduledOutputRateHz() const {
        return Active() && initialized && std::isfinite(displayDuration) && displayDuration>0.0
            ? 1.0/displayDuration : 0.0;
    }
    double DelayMs() const{return Active()?delay*sourceDuration*1000.0:0;}
    void Begin(const MotionConfig::Settings& requested,double sourceHz,double displayHz,
        bool inputInterlaced,int inputOrder,uint64_t epoch,uint64_t sequence,double pts,bool discontinuity,
        bool sourceSubtitleComposition=false,bool cadenceRepeat=false) {
        outputs.clear();skipOutput=false;currentPts=pts;subtitleFallbackSequence=0;
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
            interlaced!=newInterlaced||mixing!=newMix||composition!=sourceSubtitleComposition||(newInterlaced&&topFirst!=newTopFirst)||
            (sourceDuration>0&&std::abs(1.0/sourceDuration-sourceHz)>(std::max)(0.05,sourceHz*0.001))||
            (std::abs(measuredDisplayHz-displayHz)>(std::max)(0.1,displayHz*0.001));
        if(cadenceRepeat && initialized && !changed && discontinuity==lastSourceDiscontinuity &&
            sequence==lastSequence && pts==previousPts && Active()) {
            if(submittedOutputAvailable && !history.empty()) {
                outputPts=submittedOutputPts;
                outputs.push_back(outputPts);
            } else skipOutput=true;
            return; // No new source fact, cadence tick, or history reset.
        }
        if(changed||discontinuity||(initialized&&(sequence!=lastSequence+1||pts<=previousPts||pts-previousPts>2.5/sourceHz))) {
            const bool forwardGap=!changed && !discontinuity && initialized &&
                sequence>lastSequence && sequence-lastSequence>1 && pts>previousPts;
            if(forwardGap)ResetHistoryAfterSourceGap();
            else Clear();
        }
        settings=requested;generation=epoch;interlaced=newInterlaced;mixing=newMix;composition=sourceSubtitleComposition;
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
            previousPts=pts;lastSequence=sequence;lastSourceDiscontinuity=discontinuity;return;
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
            "; buffer "+std::to_string(delay)+" frame(s)"+
            (composition?"; subtitles synchronized":"");
        previousPts=pts;lastSequence=sequence;lastSourceDiscontinuity=discontinuity;
    }
    bool Capture(pl_gpu device,const pl_frame& source,uint64_t sequence,pl_tex subtitleLayer=nullptr,pl_rect2d layerBounds={},bool* capturedNew=nullptr) {
        // A successful duplicate keeps its first immutable GPU packet. The
        // host must bind presentation metadata only when this receipt is true.
        if(capturedNew)*capturedNew=false;
        if(gpu && gpu!=device)Clear(); // Release resources with their original GPU owner.
        gpu=device;if(!Active())return true;
        auto fail=[&](const char* reason) {
            if(composition)Clear(); // Never mix around a partially captured packet.
            status=reason;
            return false;
        };
        if(!history.empty()&&history.back()->pts==currentPts)return true;
        // Recycle the oldest slot once the bounded history is full. Copies
        // and draws share the same ordered GPU queue, so reuse cannot overwrite
        // pixels before their earlier render command consumes them.
        std::unique_ptr<Stored> stored;
        if(history.size()==8){stored=std::move(history.front());history.pop_front();}
        else stored=std::make_unique<Stored>();
        stored->gpu=device;stored->pts=currentPts;stored->sequence=sequence;
        stored->nativePrepare={};stored->hasSubtitle=false;stored->hdrSourceAvailable=false;
        stored->completeComposition=false;stored->completeOutput.valid=false;
        for(auto& field:stored->prepared)field.valid=false;
        for(auto& field:stored->processed)field.valid=false;
        stored->subtitleBounds={};
        stored->frame=source;stored->frame.overlays=nullptr;stored->frame.num_overlays=0;
        if(source.repr.dovi){stored->dovi=*source.repr.dovi;stored->frame.repr.dovi=&stored->dovi;}
        stored->frame.prev=nullptr;stored->frame.next=nullptr;stored->signature=nextSignature++;
        // First null every borrowed plane so partial failure cannot destroy it.
        for(int p=0;p<source.num_planes;++p)stored->frame.planes[p].texture=nullptr;
        for(int p=0;p<source.num_planes;++p){
            const pl_tex original=source.planes[p].texture;
            if(!original||!original->params.blit_src){return fail("Motion: source texture cannot be retained");}
            pl_tex_params params=original->params;params.initial_data=nullptr;params.user_data=nullptr;
            params.import_handle=static_cast<pl_handle_type>(0);params.export_handle=static_cast<pl_handle_type>(0);
            params.blit_src=true;params.blit_dst=true;params.sampleable=true;
            if(!pl_tex_recreate(device,&stored->textures[p],&params)){return fail("Motion: history texture allocation failed");}
            stored->frame.planes[p].texture=stored->textures[p];
            if(!stored->frame.planes[p].texture){return fail("Motion: history texture allocation failed");}
            pl_tex_blit_params copy{};copy.src=original;copy.dst=stored->frame.planes[p].texture;
            pl_tex_blit(device,&copy);
        }
        if(subtitleLayer) {
            if(!subtitleLayer->params.blit_src || subtitleLayer->params.w!=source.planes[0].texture->params.w ||
                subtitleLayer->params.h!=source.planes[0].texture->params.h) {
                return fail("Motion: subtitle layer cannot be retained");
            }
            if(layerBounds.x1<=layerBounds.x0 || layerBounds.y1<=layerBounds.y0)
                layerBounds={0,0,subtitleLayer->params.w,subtitleLayer->params.h};
            layerBounds.x0=(std::max)(0,layerBounds.x0);
            layerBounds.y0=(std::max)(0,layerBounds.y0);
            layerBounds.x1=(std::min)(subtitleLayer->params.w,layerBounds.x1);
            layerBounds.y1=(std::min)(subtitleLayer->params.h,layerBounds.y1);
            // Field parity is defined in the full source raster. An even
            // upper boundary keeps ROI-local parity identical to source parity.
            if(interlaced){layerBounds.y0&=~1;layerBounds.y1=(std::min)(subtitleLayer->params.h,(layerBounds.y1+1)&~1);}
            if(layerBounds.x1<=layerBounds.x0 || layerBounds.y1<=layerBounds.y0) {
                return fail("Motion: subtitle layer bounds are empty");
            }
            pl_tex_params params=subtitleLayer->params;
            params.w=layerBounds.x1-layerBounds.x0;
            params.h=layerBounds.y1-layerBounds.y0;
            params.initial_data=nullptr;params.user_data=nullptr;
            params.import_handle=static_cast<pl_handle_type>(0);
            params.export_handle=static_cast<pl_handle_type>(0);
            params.blit_src=true;params.blit_dst=true;params.sampleable=true;
            if(!pl_tex_recreate(device,&stored->subtitle,&params)){
                return fail("Motion: subtitle layer allocation failed");
            }
            pl_tex_blit_params copy{};
            copy.src=subtitleLayer;copy.dst=stored->subtitle;
            copy.src_rc.x0=layerBounds.x0;copy.src_rc.y0=layerBounds.y0;
            copy.src_rc.x1=layerBounds.x1;copy.src_rc.y1=layerBounds.y1;
            copy.src_rc.z0=0;copy.src_rc.z1=1;
            copy.dst_rc.x0=0;copy.dst_rc.y0=0;
            copy.dst_rc.x1=params.w;copy.dst_rc.y1=params.h;
            copy.dst_rc.z0=0;copy.dst_rc.z1=1;
            pl_tex_blit(device,&copy);
            stored->subtitleBounds=layerBounds;
        } else pl_tex_destroy(device,&stored->subtitle);
        history.push_back(std::move(stored));
        while(history.size()>8)history.pop_front();
        if(capturedNew)*capturedNew=true;
        return true;
    }
    bool CaptureComposition(pl_gpu device,const pl_frame& cleanRgbFullRaster,
        pl_tex subtitleRgbaFullRaster,uint64_t sequence,pl_rect2d layerBounds={},bool* capturedNew=nullptr) {
        if(capturedNew)*capturedNew=false;
        if(!composition||NativeCompositionRequired())return false;
        if(nativeMode){Clear();nativeMode=false;}
        const bool normalized=cleanRgbFullRaster.num_planes==1 &&
            cleanRgbFullRaster.planes[0].texture &&
            cleanRgbFullRaster.planes[0].components>=3 &&
            cleanRgbFullRaster.repr.sys==PL_COLOR_SYSTEM_RGB &&
            cleanRgbFullRaster.repr.levels==PL_COLOR_LEVELS_FULL &&
            cleanRgbFullRaster.repr.alpha==PL_ALPHA_NONE &&
            !cleanRgbFullRaster.repr.dovi &&
            !cleanRgbFullRaster.lut &&
            !cleanRgbFullRaster.icc && !cleanRgbFullRaster.profile.data &&
            !cleanRgbFullRaster.acquire && !cleanRgbFullRaster.release &&
                  !cleanRgbFullRaster.planes[0].texture->params.user_data &&
            !cleanRgbFullRaster.overlays && cleanRgbFullRaster.num_overlays==0 &&
            std::abs(cleanRgbFullRaster.crop.x0)<0.001f &&
            std::abs(cleanRgbFullRaster.crop.y0)<0.001f &&
            std::abs(cleanRgbFullRaster.crop.x1-cleanRgbFullRaster.planes[0].texture->params.w)<0.001f &&
            std::abs(cleanRgbFullRaster.crop.y1-cleanRgbFullRaster.planes[0].texture->params.h)<0.001f;
        if(!normalized) {
            Clear();status="Motion: subtitle picture is not normalized RGB";return false;
        }
        if(!history.empty()) {
            const auto& prior=history.back()->frame;
            const pl_tex priorTexture=prior.planes[0].texture;
            const pl_tex currentTexture=cleanRgbFullRaster.planes[0].texture;
            if(!priorTexture || priorTexture->params.w!=currentTexture->params.w ||
                priorTexture->params.h!=currentTexture->params.h ||
                prior.color.primaries!=cleanRgbFullRaster.color.primaries ||
                prior.color.transfer!=cleanRgbFullRaster.color.transfer) {
                Clear();status="Motion: subtitle picture format changed";return false;
            }
        }
        return Capture(device,cleanRgbFullRaster,sequence,subtitleRgbaFullRaster,layerBounds,capturedNew);
    }
    // This explicit packet kind also applies to normalized no-caption sources.
    // The first capture owns its immutable pixels and marker; a duplicate may
    // not relabel them with a later preparation result for the same PTS.
    bool CaptureCompleteComposition(pl_gpu device,const pl_frame& completeRgb,
        uint64_t sequence,bool hasSubtitle,bool* capturedNew=nullptr) {
        if(capturedNew)*capturedNew=false;
        if(!mixing||!composition||interlaced)return false;
        if(gpu&&gpu!=device)Clear();
        if(!history.empty()&&history.back()->pts==currentPts) {
            if(history.back()->sequence==sequence&&history.back()->completeComposition)return true;
            status="Motion: complete packet duplicate identity or kind changed";return false;
        }
        bool added=false;
        if(!CaptureComposition(device,completeRgb,nullptr,sequence,{},&added))return false;
        if(added) {
            history.back()->completeComposition=true;
            history.back()->hasSubtitle=hasSubtitle;
        }
        if(capturedNew)*capturedNew=added;
        return true;
    }
    // Explicit late-current completion, not an ordinary duplicate capture. The
    // caller must bind a newly complete current cue to the same immutable source
    // and preparation context. Only the latest no-cue packet can be upgraded.
    // Build a private replacement first so allocation/validation failure leaves
    // the original pixels, metadata, history and output clock intact.
    bool ReviseCompleteComposition(pl_gpu device,const pl_frame& completeRgb,
        uint64_t sequence,bool* revised=nullptr) {
        if(revised)*revised=false;
        if(!mixing||!composition||interlaced||gpu!=device||history.empty())return false;
        const auto& prior=*history.back();
        if(prior.sequence!=sequence||prior.pts!=currentPts||!prior.completeComposition||
            prior.hasSubtitle||prior.subtitle||completeRgb.num_planes!=1||
            !completeRgb.planes[0].texture||completeRgb.planes[0].components<3||
            completeRgb.repr.sys!=PL_COLOR_SYSTEM_RGB||completeRgb.repr.levels!=PL_COLOR_LEVELS_FULL||
            completeRgb.repr.alpha!=PL_ALPHA_NONE||completeRgb.repr.dovi||completeRgb.lut||
            completeRgb.icc||completeRgb.profile.data||completeRgb.acquire||completeRgb.release||
            completeRgb.overlays||completeRgb.num_overlays||
            completeRgb.planes[0].texture->params.user_data)return false;
        const auto& before=prior.frame.planes[0];const auto& after=completeRgb.planes[0];
        const auto input=after.texture;
        if(!before.texture||!input->params.blit_src||
            input->params.w!=before.texture->params.w||input->params.h!=before.texture->params.h||
            input->params.format!=before.texture->params.format||
            !pl_color_space_equal(&prior.frame.color,&completeRgb.color)||
            !pl_color_repr_equal(&prior.frame.repr,&completeRgb.repr)||
            prior.frame.rotation!=completeRgb.rotation||
            after.components!=before.components||after.shift_x!=before.shift_x||
            after.shift_y!=before.shift_y||after.flipped!=before.flipped||
            after.address_mode!=before.address_mode||
            std::abs(completeRgb.crop.x0)>0.001f||std::abs(completeRgb.crop.y0)>0.001f||
            std::abs(completeRgb.crop.x1-input->params.w)>0.001f||
            std::abs(completeRgb.crop.y1-input->params.h)>0.001f)return false;
        for(int c=0;c<after.components;++c)
            if(after.component_mapping[c]!=before.component_mapping[c])return false;
        std::unique_ptr<Stored> replacement;
        try { replacement=std::make_unique<Stored>(); }
        catch(...) { return false; }
        replacement->gpu=device;replacement->pts=prior.pts;replacement->sequence=prior.sequence;
        replacement->frame=completeRgb;
        replacement->frame.planes[0].texture=nullptr;
        replacement->frame.prev=nullptr;replacement->frame.next=nullptr;
        auto storage=input->params;storage.initial_data=nullptr;storage.user_data=nullptr;
        storage.import_handle=static_cast<pl_handle_type>(0);storage.export_handle=static_cast<pl_handle_type>(0);
        storage.blit_src=true;storage.blit_dst=true;storage.sampleable=true;
        if(!pl_tex_recreate(device,&replacement->textures[0],&storage))return false;
        replacement->frame.planes[0].texture=replacement->textures[0];
        pl_tex_blit_params copy{};copy.src=input;copy.dst=replacement->textures[0];
        pl_tex_blit(device,&copy);
        replacement->completeComposition=true;replacement->hasSubtitle=true;
        replacement->signature=nextSignature++;
        history.back()=std::move(replacement);
        subtitleFallbackSequence=0;subtitleFallbackReason="current-cue-recovered";
        if(revised)*revised=true;
        return true;
    }
    // The callback owns its shader inputs and may use scratch textures only for
    // the duration of a call. Capture immediately copies raw planes; prepared
    // field pictures and layers are copied when the output field is rendered.
    bool CaptureNativeComposition(pl_gpu device,const pl_frame& raw,
        uint64_t sequence,NativePrepare prepare,bool hasSubtitle=false,bool* capturedNew=nullptr) {
        if(capturedNew)*capturedNew=false;
        if(!NativeCompositionRequired()||!prepare)return false;
        if(!history.empty()&&history.back()->pts==currentPts) {
            if(history.back()->sequence==sequence)return true;
            status="Motion: duplicate subtitle PTS changed source identity";return false;
        }
        if(raw.num_planes<1||raw.num_planes>PL_MAX_PLANES||!raw.planes[0].texture||
            raw.acquire||raw.release||raw.icc||raw.profile.data||raw.lut||
            raw.overlays||raw.num_overlays||
            (raw.repr.sys==PL_COLOR_SYSTEM_DOLBYVISION&&!raw.repr.dovi)) {
            Clear();status="Motion: native subtitle packet has borrowed metadata";return false;
        }
        for(int p=0;p<raw.num_planes;++p)
            if(!raw.planes[p].texture||raw.planes[p].texture->params.user_data) {
                Clear();status="Motion: native subtitle packet has borrowed texture";return false;
            }
        if(!nativeMode){if(!history.empty())Clear();nativeMode=true;}
        if(!history.empty()) {
            const auto& prior=history.back()->frame;
            bool compatible=prior.num_planes==raw.num_planes&&
                prior.repr.sys==raw.repr.sys&&prior.repr.levels==raw.repr.levels&&
                prior.repr.alpha==raw.repr.alpha&&
                pl_bit_encoding_equal(&prior.repr.bits,&raw.repr.bits)&&
                prior.color.primaries==raw.color.primaries&&
                prior.color.transfer==raw.color.transfer;
            for(int p=0;compatible&&p<raw.num_planes;++p) {
                const auto& a=prior.planes[p];const auto& b=raw.planes[p];
                compatible=a.texture->params.w==b.texture->params.w&&
                    a.texture->params.h==b.texture->params.h&&
                    a.texture->params.format==b.texture->params.format&&
                    a.components==b.components&&a.shift_x==b.shift_x&&
                    a.shift_y==b.shift_y&&a.flipped==b.flipped&&
                    a.address_mode==b.address_mode;
                for(int c=0;compatible&&c<a.components;++c)
                    compatible=a.component_mapping[c]==b.component_mapping[c];
            }
            if(!compatible){Clear();status="Motion: native subtitle picture format changed";return false;}
        }
        bool added=false;
        if(!Capture(device,raw,sequence,nullptr,{},&added))return false;
        auto& packet=*history.back();
        packet.nativePrepare=std::move(prepare);packet.hasSubtitle=hasSubtitle;
        if(raw.repr.dovi){packet.dovi=*raw.repr.dovi;packet.frame.repr.dovi=&packet.dovi;}
        else packet.frame.repr.dovi=nullptr;
        if(capturedNew)*capturedNew=added;
        return true;
    }
    bool PrepareNativeField(size_t index,int fieldIndex) {
        if(index>=history.size()||fieldIndex<0||fieldIndex>1)return false;
        Stored& packet=*history[index];
        auto& prepared=packet.prepared[fieldIndex];
        const Stored* prev=index?history[index-1].get():nullptr;
        const Stored* next=index+1<history.size()?history[index+1].get():nullptr;
        if (packet.presentation.valid) {
            const auto& crop=packet.presentation.sourceCrop;
            const pl_rect2df presented={float(crop.x0),float(crop.y0),
                float(crop.x1),float(crop.y1)};
            if(prev&&!CompatiblePresentation(packet,*prev,presented))prev=nullptr;
            if(next&&!CompatiblePresentation(packet,*next,presented))next=nullptr;
        }
        const uint64_t prevSignature=prev?prev->signature:0;
        const uint64_t nextSignature=next?next->signature:0;
        pl_deinterlace_params effective=deinterlace;
        if(!prev||!next)effective.algo=PL_DEINTERLACE_BOB;
        if(prepared.valid&&prepared.prevSignature==prevSignature&&
            prepared.nextSignature==nextSignature&&prepared.algorithm==effective.algo)
            return true;
        if(!packet.nativePrepare){status="Motion: native subtitle callback missing";return false;}
        pl_frame field=packet.frame;
        field.crop={0,0,float(packet.frame.planes[0].texture->params.w),
            float(packet.frame.planes[0].texture->params.h)};
        field.rotation=PL_ROTATION_0;
        field.first_field=topFirst?PL_FIELD_TOP:PL_FIELD_BOTTOM;
        field.field=fieldIndex?pl_field_other(field.first_field):field.first_field;
        field.prev=prev?&prev->frame:nullptr;
        field.next=next?&next->frame:nullptr;
        pl_frame clean{};pl_tex layer=nullptr;pl_rect2d bounds{};
        if(!packet.nativePrepare(field,effective,clean,layer,bounds)) {
            status="Motion: native subtitle field preparation failed";return false;
        }
        const pl_tex input=clean.num_planes==1?clean.planes[0].texture:nullptr;
        if(!input||clean.planes[0].components<3||
            clean.repr.sys!=PL_COLOR_SYSTEM_RGB||
            clean.repr.levels!=PL_COLOR_LEVELS_FULL||clean.repr.alpha!=PL_ALPHA_NONE||
            clean.repr.dovi||clean.lut||clean.icc||clean.profile.data||
            clean.acquire||clean.release||clean.overlays||clean.num_overlays||
            !input->params.blit_src||input->params.user_data||
            input->params.w!=field.planes[0].texture->params.w||
            input->params.h!=field.planes[0].texture->params.h||
            std::abs(clean.crop.x0)>0.001f||std::abs(clean.crop.y0)>0.001f||
            std::abs(clean.crop.x1-input->params.w)>0.001f||
            std::abs(clean.crop.y1-input->params.h)>0.001f) {
            status="Motion: native subtitle callback returned invalid RGB";return false;
        }
        pl_tex_params params=input->params;
        params.initial_data=nullptr;params.user_data=nullptr;
        params.import_handle=static_cast<pl_handle_type>(0);
        params.export_handle=static_cast<pl_handle_type>(0);
        params.sampleable=true;params.blit_dst=true;params.blit_src=true;
        if(!pl_tex_recreate(gpu,&prepared.texture,&params)) {
            status="Motion: native field allocation failed";return false;
        }
        pl_tex_blit_params copy{};copy.src=input;copy.dst=prepared.texture;
        pl_tex_blit(gpu,&copy);
        prepared.frame=clean;
        prepared.frame.planes[0].texture=prepared.texture;
        prepared.frame.rotation=PL_ROTATION_0;
        prepared.frame.field=PL_FIELD_NONE;prepared.frame.first_field=PL_FIELD_NONE;
        prepared.frame.prev=nullptr;prepared.frame.next=nullptr;
        prepared.frame.overlays=nullptr;prepared.frame.num_overlays=0;
        prepared.subtitleBounds={};
        if(layer) {
            if(!layer->params.blit_src||layer->params.user_data||
                layer->params.w!=input->params.w||layer->params.h!=input->params.h) {
                status="Motion: native subtitle callback returned invalid layer";return false;
            }
            if(bounds.x1<=bounds.x0||bounds.y1<=bounds.y0)
                bounds={0,0,layer->params.w,layer->params.h};
            bounds.x0=(std::max)(0,bounds.x0);bounds.y0=(std::max)(0,bounds.y0);
            bounds.x1=(std::min)(layer->params.w,bounds.x1);
            bounds.y1=(std::min)(layer->params.h,bounds.y1);
            if(bounds.x1<=bounds.x0||bounds.y1<=bounds.y0) {
                status="Motion: native subtitle layer bounds are empty";return false;
            }
            pl_tex_params layerParams=layer->params;
            layerParams.w=bounds.x1-bounds.x0;layerParams.h=bounds.y1-bounds.y0;
            layerParams.initial_data=nullptr;layerParams.user_data=nullptr;
            layerParams.import_handle=static_cast<pl_handle_type>(0);
            layerParams.export_handle=static_cast<pl_handle_type>(0);
            layerParams.sampleable=true;layerParams.blit_dst=true;layerParams.blit_src=true;
            if(!pl_tex_recreate(gpu,&prepared.subtitle,&layerParams)) {
                status="Motion: native subtitle layer allocation failed";return false;
            }
            pl_tex_blit_params layerCopy{};
            layerCopy.src=layer;layerCopy.dst=prepared.subtitle;
            layerCopy.src_rc={bounds.x0,bounds.y0,0,bounds.x1,bounds.y1,1};
            layerCopy.dst_rc={0,0,0,layerParams.w,layerParams.h,1};
            pl_tex_blit(gpu,&layerCopy);
            prepared.subtitleBounds=bounds;
        } else pl_tex_destroy(gpu,&prepared.subtitle);
        prepared.prevSignature=prevSignature;prepared.nextSignature=nextSignature;
        prepared.algorithm=effective.algo;prepared.signature=this->nextSignature++;
        prepared.valid=true;
        return true;
    }
    struct NativeEntry {size_t index;int field;double offset;};
    std::vector<NativeEntry> NativeEntriesForOutput() const {
        std::vector<NativeEntry> all;
        const bool fields=interlaced&&settings.cadence=="field";
        const double unit=sourceDuration/(fields?2.0:1.0);
        all.reserve(history.size()*(fields?2:1));
        for(size_t i=0;i<history.size();++i)
            for(int f=0;f<(fields?2:1);++f)
                all.push_back({i,f,(history[i]->pts+f*unit-outputPts)/unit});
        if(all.empty())return all;
        if(!mixing) {
            NativeEntry selected=all.front();
            for(const auto& entry:all)if(entry.offset<=1e-6)selected=entry;
            return {selected};
        }
        std::vector<NativeEntry> selected;
        if(!mixer||!mixer->kernel||mixer->kernel==&pl_filter_function_oversample) {
            // Built-in oversample integrates the current frame through one
            // display interval, then needs the first frame beyond that span.
            size_t current=0;
            for(size_t i=0;i<all.size();++i)
                if(all[i].offset<=1e-6)current=i;
            const double interval=displayDuration/unit;
            selected.push_back(all[current]);
            bool endpoint=false;
            for(size_t i=current+1;i<all.size();++i) {
                selected.push_back(all[i]);
                if(all[i].offset>interval+1e-6){endpoint=true;break;}
            }
            if(!endpoint&&selected.size()==1&&current+1<all.size())
                selected.push_back(all[current+1]);
        } else {
            pl_render_params radiusParams=pl_render_fast_params;
            radiusParams.frame_mixer=mixer;
            const double radius=pl_frame_mix_radius(&radiusParams)*
                (std::max)(1.0,displayDuration/unit)+1e-6;
            for(const auto& entry:all)
                if(std::abs(entry.offset)<=radius)selected.push_back(entry);
            if(selected.empty()) {
                NativeEntry nearest=all.front();double distance=std::abs(nearest.offset);
                for(const auto& entry:all)if(std::abs(entry.offset)<distance) {
                    nearest=entry;distance=std::abs(entry.offset);
                }
                selected.push_back(nearest);
            }
        }
        return selected;
    }
    bool PreflightCompositionOutput() {
        if(!composition||!Active())return true;
        if(history.empty()||!gpu){status="Motion: subtitle picture history unavailable";return false;}
        if(!CompositionOutputReady()){
            status="Motion: subtitle composition buffering";return false;
        }
        if(!nativeMode)return true;
        const bool fields=interlaced&&settings.cadence=="field";
        const double unit=sourceDuration/(fields?2.0:1.0);
        size_t cueIndex=history.size();
        for(size_t i=0;i<history.size();++i)
            if(history[i]->pts<=outputPts+1e-8)cueIndex=i;
        if(cueIndex<history.size()) {
            const int field=fields&&outputPts>=history[cueIndex]->pts+unit-1e-8?1:0;
            if(!PrepareNativeField(cueIndex,field)){Clear();return false;}
        }
        for(const auto& entry:NativeEntriesForOutput())
            if(!PrepareNativeField(entry.index,entry.field)){Clear();return false;}
        return true;
    }
    // Source peak-detection history must not be reset by the inexpensive
    // output passes (which deliberately have peak detection disabled).
    pl_renderer ProcessingRenderer(pl_renderer fallback) {
        if(!Active()||!gpu||history.empty())return fallback;
        if(!sourceRenderer) {
            sourceRenderer=pl_renderer_create(nullptr,gpu);
            if(sourceRenderer)++processingStatistics.sourceRendererCreations;
        }
        return sourceRenderer?sourceRenderer:fallback;
    }
    static bool SameCrop(const pl_rect2df& a,const pl_rect2df& b) {
        return a.x0==b.x0&&a.y0==b.y0&&a.x1==b.x1&&a.y1==b.y1;
    }
    static bool WithinSubtitleFallbackWindow(const Stored& prior,const Stored& selected,
        double duration) {
        return prior.sequence<selected.sequence && selected.sequence-prior.sequence<=2 &&
            std::isfinite(duration)&&duration>0 && std::isfinite(prior.pts)&&
            std::isfinite(selected.pts)&&selected.pts>prior.pts &&
            selected.pts-prior.pts<=2.0*duration+1e-6;
    }
    bool WithinSubtitleFallbackWindow(const Stored& prior,const Stored& selected) const {
        return WithinSubtitleFallbackWindow(prior,selected,sourceDuration);
    }
    static bool MatchesProcessedOutput(const Stored::OutputContract& cached,
        const pl_frame& source,const pl_frame& target,const pl_render_params& params,
        uint64_t epoch,int width,int height,bool frozenOutputContract=false,
        bool ignoreSourceAnalysis=false) {
        const auto peak=params.peak_detect_params?params.peak_detect_params->analysis_crop:pl_rect2df{};
        if(!cached.valid||cached.epoch!=epoch||cached.width!=width||cached.height!=height||
            cached.sourceRotation!=source.rotation||cached.targetRotation!=target.rotation||
            !SameCrop(cached.sourceCrop,source.crop)||!SameCrop(cached.targetCrop,target.crop))return false;
        // The host defers acquiring the swapchain until packet selection. Its
        // immutable output-contract annotation plus processing epoch binds all
        // target/profile settings even though this provisional target has no
        // color/representation metadata yet. Standalone callers use full keys.
        if(frozenOutputContract)return true;
        if((!ignoreSourceAnalysis&&!SameCrop(cached.peakCrop,peak))||
            cached.hooks.size()!=size_t(params.num_hooks)||
            !pl_color_space_equal(&cached.targetColor,&target.color)||
            !pl_color_repr_equal(&cached.targetRepr,&target.repr)||cached.targetLut!=target.lut||
            cached.targetIcc!=target.icc||cached.profileSignature!=target.profile.signature)return false;
        for(int h=0;h<params.num_hooks;++h)
            if(cached.hooks[h]!=params.hooks[h]->signature)return false;
        return true;
    }
    bool ProcessSource(pl_renderer renderer,size_t index,int fieldIndex,
        const pl_frame& source,const pl_frame& target,pl_render_params& params,
        uint64_t epoch,Stored::Processed*& result) {
        auto& packet=*history[index];
        if(nativeMode&&!PrepareNativeField(index,fieldIndex))return false;
        pl_frame input=nativeMode?packet.prepared[fieldIndex].frame:packet.frame;
        input.crop=source.crop;input.rotation=source.rotation;
        uint64_t prevSignature=0,nextSignature=0;
        pl_deinterlace_params algorithm=deinterlace;
        if(interlaced&&!nativeMode) {
            input.first_field=topFirst?PL_FIELD_TOP:PL_FIELD_BOTTOM;
            input.field=fieldIndex?pl_field_other(input.first_field):input.first_field;
            input.prev=index&&CompatiblePresentation(packet,*history[index-1],source.crop)?&history[index-1]->frame:nullptr;
            input.next=index+1<history.size()&&CompatiblePresentation(packet,*history[index+1],source.crop)?&history[index+1]->frame:nullptr;
            prevSignature=input.prev?history[index-1]->signature:0;
            nextSignature=input.next?history[index+1]->signature:0;
            if(!input.prev||!input.next)algorithm.algo=PL_DEINTERLACE_BOB;
        }
        const uint64_t inputSignature=nativeMode?packet.prepared[fieldIndex].signature:packet.signature;
        auto& cached=packet.processed[fieldIndex];
        std::vector<uint64_t> hookSignatures;
        for(int h=0;h<params.num_hooks;++h)hookSignatures.push_back(params.hooks[h]->signature);
        const pl_rect2df peakCrop=params.peak_detect_params?params.peak_detect_params->analysis_crop:pl_rect2df{};
        const pl_tex output=target.num_planes==1?target.planes[0].texture:nullptr;
        if(!output)return false;
        if(cached.valid&&cached.epoch==epoch&&cached.inputSignature==inputSignature&&
            cached.prevSignature==prevSignature&&cached.nextSignature==nextSignature&&
            cached.width==output->params.w&&cached.height==output->params.h&&
            cached.sourceRotation==source.rotation&&cached.targetRotation==target.rotation&&
            SameCrop(cached.sourceCrop,source.crop)&&SameCrop(cached.targetCrop,target.crop)&&
            SameCrop(cached.peakCrop,peakCrop)&&cached.hooks==hookSignatures&&
            pl_color_space_equal(&cached.targetColor,&target.color)&&
            pl_color_repr_equal(&cached.targetRepr,&target.repr)&&cached.targetLut==target.lut&&
            cached.targetIcc==target.icc&&cached.profileSignature==target.profile.signature) {
            ++processingStatistics.sourceReuses;result=&cached;return true;
        }
        cached.valid=false;
        pl_tex_params storage{};
        storage.w=output->params.w;storage.h=output->params.h;
        storage.format=pl_find_fmt(gpu,PL_FMT_FLOAT,4,16,0,
            static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_BLITTABLE));
        storage.sampleable=true;storage.renderable=true;storage.blit_src=true;storage.blit_dst=true;
        if(!storage.format||!pl_tex_recreate(gpu,&cached.texture,&storage)) {
            status="Motion: processed source allocation failed";return false;
        }
        // Compose each packet's own subtitle before scaling and tone mapping.
        // Smooth motion blends complete processed source pictures afterward.
        pl_overlay overlay{};pl_overlay_part part{};
        const pl_tex layer=composition?(nativeMode?packet.prepared[fieldIndex].subtitle:packet.subtitle):nullptr;
        if(layer) {
            const auto bounds=nativeMode?packet.prepared[fieldIndex].subtitleBounds:packet.subtitleBounds;
            part.src={0,0,float(layer->params.w),float(layer->params.h)};
            part.dst={float(bounds.x0),float(bounds.y0),float(bounds.x1),float(bounds.y1)};
            overlay.tex=layer;overlay.mode=PL_OVERLAY_NORMAL;overlay.coords=PL_OVERLAY_COORDS_SRC_FRAME;
            overlay.repr=pl_color_repr_rgb;overlay.repr.levels=PL_COLOR_LEVELS_FULL;
            overlay.repr.alpha=PL_ALPHA_INDEPENDENT;overlay.color=input.color;
            overlay.parts=&part;overlay.num_parts=1;
            input.overlays=&overlay;input.num_overlays=1;
        }
        pl_frame destination=target;
        destination.num_planes=1;destination.planes[0]={};
        destination.planes[0].texture=cached.texture;destination.planes[0].components=4;
        for(int c=0;c<4;++c)destination.planes[0].component_mapping[c]=c;
        destination.repr=pl_color_repr_rgb;destination.repr.levels=PL_COLOR_LEVELS_FULL;
        destination.repr.alpha=PL_ALPHA_NONE;
        destination.overlays=nullptr;destination.num_overlays=0;
        destination.acquire=nullptr;destination.release=nullptr;
        destination.prev=nullptr;destination.next=nullptr;
        destination.field=PL_FIELD_NONE;destination.first_field=PL_FIELD_NONE;
        // The target transfer, gamut and calibration LUT are applied here.
        // Float storage defers only output-depth dithering to presentation.
        ProcessingRenderer(renderer);
        if(!sourceRenderer)return false;
        if(sourceRendererCompleteMode) {
            pl_renderer_flush_cache(sourceRenderer);
            pl_renderer_reset_errors(sourceRenderer,nullptr);
            ++processingStatistics.sourceRendererHistoryFlushes;
            sourceRendererCompleteMode=false;
        }
        if(sourceRendererEpoch!=epoch) {
            pl_renderer_reset_errors(sourceRenderer,nullptr);
            sourceRendererEpoch=epoch;
        }
        // Preserve authored metadata before inference supplies fallback values.
        const auto hdrSource=packet.hdrSourceAvailable?packet.hdrSource:
            FrameHdrStatus::Source(input,packet.sequence);
        pl_frames_infer(sourceRenderer,&input,&destination);
        const float black[3]={0,0,0};
        pl_frame_clear(gpu,&destination,black);
        auto processing=params;
        processing.frame_mixer=nullptr;
        processing.deinterlace_params=interlaced&&!nativeMode?&algorithm:nullptr;
        if(composition)processing.deband_params=nullptr; // Already done during normalization.
        processing.dither_params=nullptr;processing.error_diffusion=nullptr;
        processing.blend_params=nullptr;
        if(!pl_render_image(sourceRenderer,&input,&destination,&processing))return false;
        cached.hdrStatus=FrameHdrStatus::Capture(hdrSource,sourceRenderer,
            processing.peak_detect_params!=nullptr);
        cached.hdrStatus.processingEpoch=epoch;
        cached.frame=destination;
        cached.frame.lut=nullptr;cached.frame.icc=nullptr;cached.frame.profile={};
        cached.frame.overlays=nullptr;cached.frame.num_overlays=0;
        cached.frame.repr=destination.repr;cached.frame.color=destination.color;
        cached.width=output->params.w;cached.height=output->params.h;
        cached.sourceCrop=source.crop;cached.targetCrop=target.crop;cached.peakCrop=peakCrop;
        cached.sourceRotation=source.rotation;cached.targetRotation=target.rotation;
        cached.targetColor=target.color;cached.targetRepr=target.repr;
        cached.targetLut=target.lut;cached.targetIcc=target.icc;cached.profileSignature=target.profile.signature;
        cached.epoch=epoch;cached.inputSignature=inputSignature;
        cached.prevSignature=prevSignature;cached.nextSignature=nextSignature;
        cached.hooks=std::move(hookSignatures);cached.signature=this->nextSignature++;
        cached.valid=true;++processingStatistics.sourceRenders;result=&cached;return true;
    }
    static void RecordOutputContract(Stored::OutputContract& receipt,
        const pl_frame& source,const pl_frame& target,const pl_render_params& params,
        uint64_t epoch,int width,int height) {
        receipt.sourceCrop=source.crop;receipt.targetCrop=target.crop;
        receipt.peakCrop=params.peak_detect_params?params.peak_detect_params->analysis_crop:pl_rect2df{};
        receipt.sourceRotation=source.rotation;receipt.targetRotation=target.rotation;
        receipt.targetColor=target.color;receipt.targetRepr=target.repr;
        receipt.targetLut=target.lut;receipt.targetIcc=target.icc;
        receipt.profileSignature=target.profile.signature;receipt.epoch=epoch;
        receipt.width=width;receipt.height=height;
        receipt.hooks.clear();
        for(int h=0;h<params.num_hooks;++h)receipt.hooks.push_back(params.hooks[h]->signature);
        receipt.valid=true;
    }
    bool RenderCompleteComposition(const pl_frame& source,const pl_frame& target,
        pl_render_params& params,uint64_t epoch,
        const PresentationGeometry* selectedOutputGeometry=nullptr) {
        const auto selectedSequence=SelectedPacketSequence();
        size_t selectedIndex=history.size();
        for(size_t i=0;i<history.size();++i)
            if(history[i]->sequence==selectedSequence)selectedIndex=i;
        const pl_tex output=target.num_planes==1?target.planes[0].texture:nullptr;
        if(selectedIndex==history.size()||!output)return false;
        auto& selected=*history[selectedIndex];
        if(!selected.completeComposition)return false;
        const bool held=subtitleFallbackSequence!=0;
        const auto entries=held?std::vector<NativeEntry>{{selectedIndex,0,0.0}}:NativeEntriesForOutput();
        std::vector<pl_frame> owned;
        std::vector<const pl_frame*> frames;
        std::vector<uint64_t> signatures;
        using Timestamp=std::remove_const_t<std::remove_pointer_t<decltype(pl_frame_mix{}.timestamps)>>;
        std::vector<Timestamp> timestamps;
        owned.reserve(entries.size());
        for(const auto& entry:entries) {
            const auto& packet=*history[entry.index];
            if(!packet.completeComposition)continue;
            if(entry.index!=selectedIndex&&!CompatiblePresentationGeometry(
                selectedOutputGeometry?*selectedOutputGeometry:selected.presentation,
                packet.presentation,source.crop))continue;
            auto frame=packet.frame;frame.crop=source.crop;frame.rotation=source.rotation;
            owned.push_back(frame);signatures.push_back(packet.signature);
            timestamps.push_back(static_cast<Timestamp>(entry.offset));
        }
        for(const auto& frame:owned)frames.push_back(&frame);
        if(frames.empty())return false;
        ProcessingRenderer(nullptr);
        if(!sourceRenderer)return false;
        // The existing processing renderer owns the library cache and remains
        // the target of host hook-error recovery. Hard mode/epoch changes and
        // source gaps discard peak/mix history. Ordinary crop/target changes
        // are checked by the library's frame cache, without resetting peaks.
        if(!sourceRendererCompleteMode || sourceRendererEpoch!=epoch) {
            pl_renderer_flush_cache(sourceRenderer);
            pl_renderer_reset_errors(sourceRenderer,nullptr);
            ++processingStatistics.sourceRendererHistoryFlushes;
        }
        sourceRendererCompleteMode=true;sourceRendererEpoch=epoch;
        auto rendering=params;
        rendering.frame_mixer=mixer;
        rendering.deband_params=nullptr; // Native source preparation already did this.
        pl_frame_mix mix{};mix.num_frames=static_cast<int>(frames.size());
        mix.frames=frames.data();mix.signatures=signatures.data();mix.timestamps=timestamps.data();
        mix.vsync_duration=static_cast<float>(displayDuration/sourceDuration);
        // Tone mapping and blending deliberately use the normal library path,
        // including the reconstructed text, matching non-motion preparation.
        // A late target change simply renders this same owned complete packet.
        if(!pl_render_image_mix(sourceRenderer,&mix,&target,&rendering))return false;
        RecordOutputContract(selected.completeOutput,source,target,params,epoch,
            output->params.w,output->params.h);
        ++processingStatistics.outputRenders;++processingStatistics.completeOutputRenders;
        if(held)++processingStatistics.subtitleFallbackOutputs;
        lastSubtitleSequence=SelectedSubtitleSequence();
        const auto authored=selected.hdrSourceAvailable?selected.hdrSource:
            FrameHdrStatus::Source(selected.frame,selected.sequence);
        // Keep the authored selected-source identity, but label detector state
        // as belonging to the library render/mix rather than that source alone.
        lastHdrStatus=FrameHdrStatus::Capture(authored,sourceRenderer,params.peak_detect_params!=nullptr);
        lastHdrStatus.detectedFromMix=true;
        lastHdrStatusAvailable=true;
        return true;
    }
    bool RenderProcessed(pl_renderer renderer,const pl_frame& source,
        const pl_frame& target,pl_render_params& params,uint64_t epoch,
        const PresentationGeometry* selectedOutputGeometry=nullptr) {
        lastHdrStatusAvailable=false;lastHdrStatus={};
        if(composition&&!PreflightCompositionOutput())return false;
        const Stored* selected=nullptr;
        const auto selectedSequence=SelectedPacketSequence();
        size_t selectedIndex=history.size();
        for(size_t i=0;i<history.size();++i)if(history[i]->sequence==selectedSequence) {
            selected=history[i].get();selectedIndex=i;break;
        }
        const bool held=subtitleFallbackSequence && selectedIndex<history.size();
        const auto entries=held?std::vector<NativeEntry>{{selectedIndex,0,0.0}}:NativeEntriesForOutput();
        std::vector<const pl_frame*> frames;
        std::vector<uint64_t> signatures;
        std::vector<RendererHdrStatus> hdrStatuses;
        using Timestamp=std::remove_const_t<std::remove_pointer_t<decltype(pl_frame_mix{}.timestamps)>>;
        std::vector<Timestamp> timestamps;
        std::array<std::array<bool,2>,8> used{};
        for(const auto& entry:entries) {
            if(selected&&history[entry.index].get()!=selected&&
                !CompatiblePresentationGeometry(selectedOutputGeometry ? *selectedOutputGeometry :
                    selected->presentation,history[entry.index]->presentation,source.crop))continue;
            Stored::Processed* processed=nullptr;
            if(held) {
                // The host selected before acquiring the target. Recheck actual
                // output metadata/mapping here; a changed contract reprocesses
                // this same owned clean picture and matching layer, never the
                // current raw picture. Rejecting only the cache would otherwise
                // send the host through its acquired-frame black-clear path.
                processed=&history[entry.index]->processed[0];
                const pl_tex output=target.num_planes==1?target.planes[0].texture:nullptr;
                if(output&&MatchesProcessedOutput(*processed,source,target,params,epoch,
                    output->params.w,output->params.h,false,true)) {
                    ++processingStatistics.sourceReuses;
                } else {
                    if(!ProcessSource(renderer,entry.index,entry.field,source,target,params,epoch,processed))return false;
                    subtitleFallbackReason="complete-packet-remapped";
                }
            } else if(!ProcessSource(renderer,entry.index,entry.field,source,target,params,epoch,processed))return false;
            used[entry.index][entry.field]=true;
            frames.push_back(&processed->frame);signatures.push_back(processed->signature);
            hdrStatuses.push_back(processed->hdrStatus);
            timestamps.push_back(static_cast<Timestamp>(entry.offset));
        }
        if(frames.empty())return false;
        if(composition)lastSubtitleSequence=SelectedSubtitleSequence();
        if(interlaced) {
            const double unit=sourceDuration/(settings.cadence=="field"?2.0:1.0);
            lastSubmittedField=topFirst?PL_FIELD_TOP:PL_FIELD_BOTTOM;
            if(selected&&settings.cadence=="field"&&outputPts>=selected->pts+unit-1e-8)
                lastSubmittedField=pl_field_other(lastSubmittedField);
        }
        // Expensive source work is complete. Keep the final target overlays
        // live, but never run peak detection, tone mapping, NLS or LUTs again.
        pl_frame destination=target;
        destination.lut=nullptr;destination.icc=nullptr;destination.profile={};
        auto presentation=pl_render_fast_params;
        presentation.dither_params=params.dither_params;
        presentation.error_diffusion=params.error_diffusion;
        presentation.blend_params=params.blend_params;
        presentation.dynamic_constants=true;
        presentation.info_callback=params.info_callback;presentation.info_priv=params.info_priv;
        bool rendered=false;
        if(mixing&&!held) {
            presentation.frame_mixer=mixer;
            pl_frame_mix mix{};mix.num_frames=static_cast<int>(frames.size());
            mix.frames=frames.data();mix.signatures=signatures.data();mix.timestamps=timestamps.data();
            const double unit=sourceDuration/(interlaced&&settings.cadence=="field"?2.0:1.0);
            mix.vsync_duration=static_cast<float>(displayDuration/unit);
            // Stable libplacebo mixes in the target transfer domain. Use an
            // explicitly linear float target on both runtimes so blending is
            // consistent, then only encode/dither and add the live OSD.
            const pl_tex output=destination.planes[0].texture;
            pl_tex_params storage{};storage.w=output->params.w;storage.h=output->params.h;
            storage.format=pl_find_fmt(gpu,PL_FMT_FLOAT,4,16,0,
                static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE|PL_FMT_CAP_RENDERABLE|PL_FMT_CAP_BLITTABLE));
            // The mixer clears borders when target.crop letterboxes the output.
            // Renderability alone cannot satisfy pl_frame_clear's blit contract.
            storage.sampleable=true;storage.renderable=true;storage.blit_dst=true;
            if(!storage.format||!pl_tex_recreate(gpu,&compositionSurface,&storage))return false;
            pl_frame linear=destination;linear.planes[0]={};
            linear.planes[0].texture=compositionSurface;linear.planes[0].components=4;
            for(int c=0;c<4;++c)linear.planes[0].component_mapping[c]=c;
            linear.repr=pl_color_repr_rgb;linear.repr.levels=PL_COLOR_LEVELS_FULL;
            linear.repr.alpha=PL_ALPHA_NONE;linear.color=frames.front()->color;
            linear.color.transfer=PL_COLOR_TRC_LINEAR;
            linear.overlays=nullptr;linear.num_overlays=0;
            auto blending=pl_render_fast_params;blending.frame_mixer=mixer;
            blending.dynamic_constants=true;
            blending.info_callback=params.info_callback;blending.info_priv=params.info_priv;
            rendered=pl_render_image_mix(renderer,&mix,&linear,&blending);
            if(rendered)rendered=pl_render_image(renderer,&linear,&destination,&presentation);
        } else {
            size_t current=0;for(size_t i=0;i<frames.size();++i)if(timestamps[i]<=1e-6)current=i;
            rendered=pl_render_image(renderer,frames[current],&destination,&presentation);
        }
        if(rendered) {
            ++processingStatistics.outputRenders;
            if(held)++processingStatistics.subtitleFallbackOutputs;
            // The output cue is the latest included source at/before the output
            // timestamp, exactly as in the non-mixing presentation selection.
            // Its snapshot survives later neighbour renders and cache reuse.
            size_t cue=0;for(size_t i=0;i<timestamps.size();++i)if(timestamps[i]<=1e-6)cue=i;
            lastHdrStatus=hdrStatuses[cue];lastHdrStatusAvailable=true;
        }
        // Keep only the active filter support. Raw history remains available
        // for deinterlacing and geometry guards; float output caches are bounded.
        // Keep at most one already processed caption anchor for the bounded
        // missing-source fallback. No new texture or source proof is produced.
        const Stored* normalSelected=nullptr;size_t anchorIndex=history.size();
        for(const auto& packet:history)if(packet->pts<=outputPts+1e-8)normalSelected=packet.get();
        if(mixing&&composition&&!interlaced&&normalSelected)
            for(size_t i=0;i<history.size();++i) {
                const auto& packet=*history[i];
                if(packet.sequence<=normalSelected->sequence && (packet.subtitle||packet.hasSubtitle) &&
                    packet.processed[0].valid && packet.presentation.valid && packet.presentation.cleaned &&
                    (packet.sequence==normalSelected->sequence || WithinSubtitleFallbackWindow(packet,*normalSelected)))
                    anchorIndex=i;
            }
        for(size_t i=0;i<history.size();++i)for(int f=0;f<2;++f)if(!used[i][f]&&!(i==anchorIndex&&f==0)) {
            auto& processed=history[i]->processed[f];
            pl_tex_destroy(gpu,&processed.texture);processed.valid=false;
            if(nativeMode) {
                auto& prepared=history[i]->prepared[f];
                pl_tex_destroy(gpu,&prepared.texture);pl_tex_destroy(gpu,&prepared.subtitle);
                prepared.valid=false;
            }
        }
        return rendered;
    }
    bool RenderComposition(pl_renderer renderer,const pl_frame& source,
        const pl_frame& target,pl_render_params& params,uint64_t epoch=0) {
        return RenderProcessed(renderer,source,target,params,epoch);
    }
    bool Render(pl_renderer renderer,const pl_frame& source,const pl_frame& target,
        pl_render_params& params,uint64_t epoch=0,
        const PresentationGeometry* selectedOutputGeometry=nullptr) {
        lastSubmittedField=PL_FIELD_NONE;
        lastHdrStatusAvailable=false;lastHdrStatus={};
        if(CompleteCompositionSelected())return RenderCompleteComposition(source,target,params,epoch,selectedOutputGeometry);
        if(composition&&Active())return RenderProcessed(renderer,source,target,params,epoch,selectedOutputGeometry);
        if(!Active()||history.empty())return pl_render_image(renderer,&source,&target,&params);
        return RenderProcessed(renderer,source,target,params,epoch,selectedOutputGeometry);
    }
};
