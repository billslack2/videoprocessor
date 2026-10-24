#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCurrentCue.h>
#include <SubtitleCutPaste.h>
#include <SubtitleBackingLip.h>
#include <SubtitleCutPasteShader.h>
#include <vprenderer/TemporalRenderer.h>
#include <vprenderer/SourcePreparationReceipt.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/options.h>
#include <array>
#include <memory>
#include <vector>
#include <chrono>
#include <sstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
constexpr int NativeWidth=3840,NativeHeight=2160,PictureTop=384,PictureBottom=1776;
constexpr double SourceHz=24000.0/1001.0,DisplayHz=59.951;
ActivePictureFrameIdentity ReconstructionIdentity(uint64_t sequence) {
    auto id=ActivePictureFrameIdentity{2,sequence,sequence,sequence*41708,2,3,4};
    id.analysisDomainKey=9;return id;
}
ActivePictureBounds ReconstructionBase() {
    return {0,PictureTop,NativeWidth,PictureBottom,NativeWidth,NativeHeight,
        double(NativeWidth)/(PictureBottom-PictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
}
struct NativeReconstructionPixels {
    std::vector<uint8_t> pixels;
    bool hasCue;
    explicit NativeReconstructionPixels(bool cue=true,int glyphs=12,int scene=0)
        :pixels(size_t(NativeWidth)*NativeHeight*4,255),hasCue(cue) {
        for(int y=0;y<NativeHeight;++y)for(int x=0;x<NativeWidth;++x) {
            const auto index=(size_t(y)*NativeWidth+x)*4;
            const uint8_t value=y<PictureTop||y>=PictureBottom?0:
                uint8_t(45+scene*30+(x/80+y/60)%45);
            pixels[index]=pixels[index+1]=pixels[index+2]=value;
        }
        if(!cue)return;
        const int left=(NativeWidth-((glyphs-1)*78+48))/2;
        Fill(left-96,PictureBottom-24,left+(glyphs-1)*78+144,PictureBottom,0);
        for(int line=0;line<2;++line)for(int n=0;n<glyphs;++n) {
            const int x=left+n*78,y=PictureBottom+2+line*168;
            Fill(x,y,x+48,y+84,175);
            Fill(x+12,y+12,x+36,y+72,0);
        }
    }
    void Fill(int l,int t,int r,int b,uint8_t value) {
        for(int y=t;y<b;++y)for(int x=l;x<r;++x) {
            const auto index=(size_t(y)*NativeWidth+x)*4;
            pixels[index]=pixels[index+1]=pixels[index+2]=value;
        }
    }
    AnalysisLumaSource Source() const {
        AnalysisLumaSource s;s.data=pixels.data();s.dataBytes=pixels.size();
        s.width=NativeWidth;s.height=NativeHeight;s.rowBytes=NativeWidth*4;
        s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;
        s.colorspace=ColorSpace::BT_2020;s.range=AnalysisLumaRange::Full;s.generation=2;return s;
    }
};
// Heap-owned: exercise real native measurement, admission and finite cleanup,
// rather than uploading an already reconstructed placeholder to the renderer.
struct MeasuredReconstructionCue {
    std::array<SubtitleBoxObservation,2> observations;
    SubtitleBoxPreview preview;
    SubtitleBoxPresentation tracker;
    SubtitleCueContinuationGuard guard;
    SubtitlePaddingGuard padding;
    SubtitleCutPastePresentation compositor;
    SubtitleLayoutAnchor anchor;
    SubtitleBackingLipContinuity backing;
    struct { SubtitleCueContinuation proof; SubtitleBoxResult accepted; } decision;
    SubtitleCutPasteGeometry geometry;
    size_t proofReads=0;
    void Acquire(const NativeReconstructionPixels& pixels,uint64_t sequence) {
        Assert::IsTrue(pixels.hasCue&&sequence>=2);
        const auto source=pixels.Source();
        for(int i=0;i<2;++i) {
            SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
            observations[i]=SubtitleBoxLookahead::Measure(detector,source,
                ExtractActivePictureEvidence(source),ReconstructionIdentity(sequence-1+i),false,{},7,8);
            Assert::IsTrue(observations[i].text.detected,L"UHD native glyph acquisition failed");
        }
        preview=SubtitleBoxLookahead::Resolve(observations.data(),2,7,8);
        Assert::IsTrue(tracker.Consume(preview).detected);
        preview=SubtitleBoxLookahead::Resolve(&observations[1],1,7,8);
        Admit(source,sequence);
    }
    void Advance(const NativeReconstructionPixels& pixels,uint64_t sequence) {
        const auto source=pixels.Source();SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        observations[1]=SubtitleBoxLookahead::Measure(detector,source,ExtractActivePictureEvidence(source),
            ReconstructionIdentity(sequence),false,{},7,8);
        Assert::IsTrue(observations[1].text.detected);
        preview=SubtitleBoxLookahead::Resolve(&observations[1],1,7,8);Admit(source,sequence);
    }
    void Admit(const AnalysisLumaSource& source,uint64_t sequence) {
        auto current=source;
        tracker.RefreshReferencePixels(preview,current);
        decision.proof={};decision.accepted=tracker.Consume(preview);
        Assert::IsTrue(decision.accepted.detected&&!decision.accepted.confirmationPending&&!decision.accepted.held);
        const auto* proof=decision.proof.valid?&decision.proof:nullptr;
        const auto safe=padding.Consume(decision.accepted,preview,{36,24,18},{},proof);
        geometry=compositor.Consume(decision.accepted,preview,safe,24,nullptr,proof);
        geometry=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(geometry,
            {0,PictureTop,NativeWidth,PictureBottom},24),30,safe,true,false);
        geometry=anchor.Consume(geometry,decision.accepted,preview,safe,24,false,proof);
        Assert::IsTrue(geometry.valid,L"Measured UHD cue did not produce current reconstruction geometry");
        if(!geometry.generatedCleanup.Valid()) {
            const auto cap=backing.Consume(current,preview,decision.accepted,ReconstructionIdentity(sequence),7,8,
                65536,&geometry,proof);
            Assert::IsTrue(cap.valid,L"Measured UHD backing strip was not currently proved");
            geometry=ApplyCurrentSubtitleBackingLip(geometry,cap,ReconstructionIdentity(sequence),7,8);
        }
        Assert::IsTrue(geometry.valid&&geometry.generatedCleanup.Valid());
        ++proofReads; // Native measurement/admission passes, not sample counts.
    }
};
struct ReconstructionMotionGpu {
    pl_d3d11 device=nullptr;
    pl_renderer renderer=nullptr;
    pl_options options=nullptr;
    const pl_hook* hook=nullptr;
    pl_tex inputTexture=nullptr,outputTexture=nullptr,surface=nullptr;
    pl_frame input{},output{},complete{},mapped{};
    SubtitlePreparationRenderer preparation;
    TemporalRenderer temporal;
    MotionConfig::Settings settings;
    SubtitleGeneratedGrayStyle style;
    pl_peak_detect_params peak=pl_peak_detect_default_params;
    pl_color_map_params colorMap=pl_color_map_default_params;
    TemporalRenderer::PresentationGeometry presentation{};
    uint64_t processingEpoch=11,viewportEpoch=7,outputContract=19;
    ReconstructionMotionGpu() {
        pl_d3d11_params d{};d.allow_software=true;device=pl_d3d11_create(nullptr,&d);
        Assert::IsTrue(device!=nullptr,L"D3D11 required for the joined reconstruction/motion regression");
        renderer=pl_renderer_create(nullptr,device->gpu);hook=CreateSubtitleCutPasteHook(device->gpu);
        options=pl_options_alloc(nullptr);Assert::IsTrue(options!=nullptr);
        Assert::IsTrue(pl_options_load(options,"upscaler=ewa_lanczossharp,downscaler=mitchell,plane_upscaler=ewa_lanczos,deband=yes,sigmoid=yes,peak_detect=yes,peak_percentile=99.99500274658203,contrast_recovery=0.10000000149011612,error_diffusion=sierra-3,dynamic_constants=yes"),
            L"The production reconstruction/motion settings must load on this dependency");
#ifdef VP_LIBPLACEBO_BETA
        // These three shipped beta extensions do not exist in the stable API.
        Assert::IsTrue(pl_options_load(options,"mastering_clip=0.6000000238418579,vp_gamut_compression=yes,film_strength=1.2300000190734863"));
#endif
        Assert::IsTrue(renderer&&hook);
        pl_tex_params t{};t.w=NativeWidth;t.h=NativeHeight;t.format=pl_find_named_fmt(device->gpu,"rgba8");
        t.sampleable=true;t.blit_src=true;t.blit_dst=true;t.host_writable=true;
        inputTexture=pl_tex_create(device->gpu,&t);
        t.w=2000;t.h=1125;t.renderable=true;t.host_readable=true;
        outputTexture=pl_tex_create(device->gpu,&t);Assert::IsTrue(inputTexture&&outputTexture);
        input.num_planes=1;input.planes[0].texture=inputTexture;input.planes[0].components=4;
        for(int c=0;c<4;++c)input.planes[0].component_mapping[c]=c;
        input.repr=pl_color_repr_rgb;input.repr.levels=PL_COLOR_LEVELS_FULL;input.repr.alpha=PL_ALPHA_NONE;
        input.color=pl_color_space_hdr10;input.color.hdr.max_luma=1000;input.color.hdr.min_luma=.005f;
        input.crop={0,0,float(NativeWidth),float(NativeHeight)};
        output=input;output.planes[0].texture=outputTexture;output.crop={0,0,2000,1125};
        output.color=pl_color_space_srgb;output.color.primaries=PL_COLOR_PRIM_BT_2020;
        output.color.transfer=PL_COLOR_TRC_GAMMA24;output.color.hdr.max_luma=117;output.color.hdr.min_luma=.008f;
        settings.smoothMotion="always";settings.mixer="oversample";settings.deinterlace="off";
        style.opacity=.75f;style.blurPixels=30;style.maxLuminance=.25f;
        style.textColor={1.f,1.f,0.f};
        Assert::IsTrue(options->params.peak_detect_params&&options->params.color_map_params&&
            options->params.error_diffusion,L"Actual HDR peak, tone mapping and Sierra3 must remain enabled");
        peak=*options->params.peak_detect_params;colorMap=*options->params.color_map_params;
        Assert::IsFalse(peak.allow_delayed);
        presentation.valid=true;presentation.cleaned=true;presentation.visibleSafe=true;
        presentation.familyBase=presentation.sourceCrop={0,PictureTop,NativeWidth,PictureBottom};
        presentation.verifiedVisible=presentation.familyBase;presentation.viewportEpoch=viewportEpoch;
        presentation.owner=1;presentation.outputContract=outputContract;
    }
    ~ReconstructionMotionGpu() {
        temporal.Clear();preparation.Reset();if(hook)pl_mpv_user_shader_destroy(&hook);
        if(options)pl_options_free(&options);
        if(device) {
            pl_tex_destroy(device->gpu,&surface);pl_tex_destroy(device->gpu,&inputTexture);
            pl_tex_destroy(device->gpu,&outputTexture);pl_renderer_destroy(&renderer);pl_d3d11_destroy(&device);
        }
    }
    pl_render_params Parameters() {
        auto p=options->params;p.peak_detect_params=&peak;p.color_map_params=&colorMap;
        Assert::IsTrue(p.error_diffusion!=nullptr);return p;
    }
    void Capture(const NativeReconstructionPixels& pixels,uint64_t sequence,
        const SubtitleCutPasteGeometry* geometry=nullptr,uint64_t generation=2,bool discontinuity=false) {
        pl_tex_transfer_params upload{};upload.tex=inputTexture;upload.ptr=const_cast<uint8_t*>(pixels.pixels.data());
        Assert::IsTrue(pl_tex_upload(device->gpu,&upload));
        temporal.Begin(settings,SourceHz,DisplayHz,false,0,generation,sequence,
            double(sequence-2)/SourceHz,discontinuity,true);
        auto params=Parameters();const auto sourceProcessing=CaptureSubtitleSourceProcessingOptions(params);
        const auto calls=preparation.RenderCallCount();
        if(geometry)Assert::IsTrue(PrepareCompleteSubtitleSourcePacket(preparation,device->gpu,input,hook,
            *geometry,5,style,2,&surface,complete,&sourceProcessing));
        else Assert::IsTrue(RenderSubtitleFullRaster(preparation,device->gpu,input,nullptr,&surface,complete,
            true,false,false,nullptr,&sourceProcessing));
        Assert::AreEqual(calls+1,preparation.RenderCallCount(),L"A source packet must prepare exactly once");
        Assert::IsTrue(complete.repr.alpha==PL_ALPHA_NONE&&complete.num_planes==1&&complete.num_overlays==0);
        bool captured=false;
        Assert::IsTrue(temporal.CaptureCompleteComposition(device->gpu,complete,sequence,geometry!=nullptr,&captured));
        Assert::IsTrue(captured);
        auto pose=presentation;pose.cleaned=geometry!=nullptr;pose.viewportEpoch=viewportEpoch;
        Assert::IsTrue(temporal.AnnotatePresentation(sequence,pose));
        Assert::IsTrue(temporal.AnnotateHdrSource(sequence,input));
        temporal.outputPts=double(sequence-2)/SourceHz;
        mapped=complete;mapped.crop={0,float(PictureTop),float(NativeWidth),float(PictureBottom)};
    }
    bool Select() {
        auto p=Parameters();return temporal.SelectSubtitleFallback(processingEpoch,viewportEpoch,mapped,output,p,
            outputTexture->params.w,outputTexture->params.h,outputContract);
    }
    void Draw() {
        auto p=Parameters();Assert::IsTrue(p.peak_detect_params&&!p.peak_detect_params->allow_delayed);
        Assert::IsTrue(temporal.Render(renderer,mapped,output,p,processingEpoch));temporal.MarkSubmitted();
        Assert::IsTrue(p.peak_detect_params==&peak&&!peak.allow_delayed,L"Motion cannot mutate shared HDR options");
    }
    RendererHdrStatus Hdr() {
        RendererHdrStatus h;Assert::IsTrue(temporal.HdrStatusSnapshot(h));
        Assert::IsTrue(h.hdr&&h.peakDetectionEnabled);return h;
    }
    size_t YellowPixels() {
        std::vector<uint8_t> rgb(size_t(2000)*1125*4);pl_tex_transfer_params read{};
        read.tex=outputTexture;read.ptr=rgb.data();Assert::IsTrue(pl_tex_download(device->gpu,&read));
        size_t yellow=0;bool opaque=true;
        for(size_t i=0;i<rgb.size();i+=4) {
            opaque=opaque&&rgb[i+3]==255;
            if(int(rgb[i])+int(rgb[i+1])>2*int(rgb[i+2])+40&&rgb[i]>40&&rgb[i+1]>40)++yellow;
        }
        Assert::IsTrue(opaque,L"Complete presentation must be opaque");return yellow;
    }
};
}

namespace Tests {
TEST_CLASS(ReconstructionMotionTests) { public:
    TEST_METHOD(UhdNativeCueUsesOneCompletePreparationAcrossInsertedOutputs) {
        NativeReconstructionPixels pixels;auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(pixels,2);
        auto g=std::make_unique<ReconstructionMotionGpu>();const auto start=std::chrono::steady_clock::now();
        g->Capture(pixels,2,&cue->geometry);Assert::IsFalse(g->Select());g->Draw();
        Assert::AreEqual(uint64_t(2),g->temporal.LastSubtitleSequence());
        Assert::AreEqual(uint64_t(2),g->Hdr().sourceSequence);
        Assert::IsTrue(g->YellowPixels()>100,L"Actual reconstructed glyphs must survive the joined HDR/motion path");
        const auto preparations=g->preparation.RenderCallCount(),proofReads=cue->proofReads;
        const auto first=g->temporal.ProcessingSnapshot();
        for(int output=1;output<=8;++output) {
            g->temporal.outputPts=double(output)/9.0/SourceHz;
            Assert::IsFalse(g->Select());g->Draw();
            Assert::AreEqual(uint64_t(2),g->temporal.SelectedPacketSequence());
            Assert::AreEqual(uint64_t(2),g->Hdr().sourceSequence);
        }
        Assert::AreEqual(preparations,g->preparation.RenderCallCount());Assert::AreEqual(proofReads,cue->proofReads);
        Assert::IsTrue(first.completeOutputRenders>0,L"The joined test must reach the production complete library-mix route");
        Assert::IsTrue(g->temporal.ProcessingSnapshot().completeOutputRenders>=first.completeOutputRenders+8);
        Assert::IsTrue(g->YellowPixels()>100);
        const auto flushes=g->temporal.ProcessingSnapshot().sourceRendererHistoryFlushes;
        g->mapped.crop.y0+=2;g->Draw();g->mapped.crop.y0-=2;g->Draw();
        Assert::AreEqual(flushes,g->temporal.ProcessingSnapshot().sourceRendererHistoryFlushes,
            L"Per-source crop jitter must use normal library cache invalidation without flushing peak history");
        ++g->processingEpoch;g->Draw();
        Assert::AreEqual(flushes+1,g->temporal.ProcessingSnapshot().sourceRendererHistoryFlushes,
            L"A true processing epoch change must retire old library processing history");
        std::ostringstream text;text<<"UHD native reconstruction/motion executed; native_admissions="<<proofReads
            <<" preparation_calls="<<preparations<<" inserted_outputs=8 elapsed_ms="
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        Logger::WriteMessage(text.str().c_str());
    }
    TEST_METHOD(UhdCompletePacketHoldExpiresAndNewMeasuredCueWins) {
        NativeReconstructionPixels pixels,blank(false,12,2),changed(true,9,1);
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(pixels,2);
        auto replacement=std::make_unique<MeasuredReconstructionCue>();replacement->Acquire(changed,6);
        auto g=std::make_unique<ReconstructionMotionGpu>();g->Capture(pixels,2,&cue->geometry);g->Draw();
        for(uint64_t sequence=3;sequence<=4;++sequence) {
            g->Capture(blank,sequence);Assert::IsTrue(g->Select());
            for(int inserted=0;inserted<6;++inserted) {
                g->temporal.outputPts=double(sequence-2)/SourceHz+double(inserted)/7.0/SourceHz;
                Assert::IsTrue(g->Select());g->Draw();
                Assert::AreEqual(uint64_t(2),g->temporal.SelectedPacketSequence());
                Assert::AreEqual(uint64_t(2),g->Hdr().sourceSequence);
            }
            Assert::IsTrue(g->YellowPixels()>100,L"Hold must keep the complete repaired picture and glyphs together");
        }
        g->Capture(blank,5);Assert::IsFalse(g->Select());g->Draw();
        Assert::AreEqual(uint64_t(5),g->temporal.SelectedPacketSequence());
        Assert::AreEqual(uint64_t(0),g->temporal.LastSubtitleSequence());
        Assert::AreEqual(size_t(0),g->YellowPixels(),L"Missing cue cannot survive more than two source intervals");
        g->Capture(changed,6,&replacement->geometry);Assert::IsFalse(g->Select());g->Draw();
        Assert::AreEqual(uint64_t(6),g->temporal.LastSubtitleSequence());
        Assert::AreEqual(uint64_t(6),g->Hdr().sourceSequence);Assert::IsTrue(g->YellowPixels()>100);
    }
    TEST_METHOD(UhdInFlightHistoryAndFailedPreparationPreservePacketAuthority) {
        NativeReconstructionPixels pixels,blank(false,12,2);
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(pixels,2);
        auto g=std::make_unique<ReconstructionMotionGpu>();size_t drawn=0;
        // Deliberately no per-output download or gpu_finish: exercise more
        // distinct UHD source packets than the eight-entry retained history.
        for(uint64_t sequence=2;sequence<=13;++sequence) {
            if(sequence>2)cue->Advance(pixels,sequence);
            g->Capture(pixels,sequence,&cue->geometry);
            for(double pts:g->temporal.outputs) {
                g->temporal.outputPts=pts;if(!g->temporal.CompositionOutputReady())continue;
                Assert::IsFalse(g->Select());g->Draw();++drawn;
                Assert::IsTrue(g->temporal.SelectedPacketSequence()<=sequence);
            }
            Assert::IsTrue(g->temporal.HistoryDepth()<=8);
        }
        Assert::IsTrue(drawn>=20,L"Joined UHD history must include inserted Smooth Motion draws");
        Assert::AreEqual(uint64_t(12),g->preparation.RenderCallCount());
        Assert::IsTrue(g->temporal.ProcessingSnapshot().completeOutputRenders>=drawn);
        g->temporal.outputPts=11.0/SourceHz;g->Draw();
        Assert::AreEqual(uint64_t(13),g->temporal.LastSubtitleSequence());Assert::IsTrue(g->YellowPixels()>100);
        const auto calls=g->preparation.RenderCallCount();pl_frame rejected{};
        auto invalid=cue->geometry;invalid.valid=false;
        Assert::IsFalse(PrepareCompleteSubtitleSourcePacket(g->preparation,g->device->gpu,g->input,g->hook,
            invalid,5,g->style,2,&g->surface,rejected));
        Assert::AreEqual(calls,g->preparation.RenderCallCount());Assert::AreEqual(0,rejected.num_planes);
        bool captured=true;
        Assert::IsFalse(g->temporal.CaptureCompleteComposition(g->device->gpu,rejected,14,true,&captured));
        Assert::IsFalse(captured);Assert::IsFalse(g->temporal.HasCurrentFrame(14));
        g->Capture(blank,14);Assert::AreEqual(uint64_t(0),g->temporal.SelectedSubtitleSequence());
        g->Draw();Assert::AreEqual(size_t(0),g->YellowPixels());
    }
    TEST_METHOD(UhdConclusiveCuePacketSurvivesAnalysisOnlyCadenceRetry) {
        auto pixels=std::make_shared<NativeReconstructionPixels>();
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(*pixels,2);
        auto g=std::make_unique<ReconstructionMotionGpu>();g->Capture(*pixels,2,&cue->geometry);g->Draw();
        Assert::IsTrue(g->temporal.HasCurrentCompleteComposition(2));
        const auto calls=g->preparation.RenderCallCount();
        const auto depth=g->temporal.HistoryDepth();const auto pts=g->temporal.SelectedPacketPts();
        const auto yellow=g->YellowPixels();Assert::IsTrue(yellow>100);
        SourcePreparationContext context;context.identity=ReconstructionIdentity(2);
        context.source=pixels->Source();context.source.backingOwner=pixels;
        context.queuedSource=context.source;context.gpu=g->device->gpu;
        context.processingEpoch=g->processingEpoch;context.policyGeneration=7;
        context.continuityGeneration=8;context.viewportEpoch=g->viewportEpoch;
        context.outputContract=g->outputContract;
        SourcePreparationReceipt receipt;receipt.Commit(context,false,true);
        for(int attempt=0;attempt<2;++attempt) {
            const auto reuse=receipt.Begin(context,SourceRepeatKind::CadenceRepeat);
            Assert::IsTrue(reuse.upload&&!reuse.analysis&&!reuse.subtitle);
            const bool keep=ReuseConclusiveSubtitlePacketForSourceRetry(reuse,SourceRepeatKind::CadenceRepeat,
                true,true,true,g->temporal.HasCurrentCompleteComposition(2),true);
            Assert::IsTrue(keep,L"AR-only retry must retain the exact already-complete source/cue packet");
            g->temporal.Begin(g->settings,SourceHz,DisplayHz,false,0,2,2,0.0,false,true,true);
            // This is the production reuse branch: no new preparation or capture.
            g->Draw();receipt.Commit(context,attempt==1,true);
            Assert::AreEqual(calls,g->preparation.RenderCallCount());
            Assert::AreEqual(depth,g->temporal.HistoryDepth());Assert::AreEqual(pts,g->temporal.SelectedPacketPts());
            Assert::AreEqual(uint64_t(2),g->temporal.SelectedSubtitleSequence());
            Assert::AreEqual(uint64_t(2),g->Hdr().sourceSequence);Assert::AreEqual(yellow,g->YellowPixels());
        }
        Assert::IsTrue(receipt.ReuseAnalysis(context,SourceRepeatKind::CadenceRepeat));
        Assert::IsTrue(receipt.ReuseSubtitle(context,SourceRepeatKind::CadenceRepeat));
        const SourcePreparationReuse retry{true,false,false};
        for(int fault=0;fault<8;++fault) {
            auto reuse=retry;auto kind=SourceRepeatKind::CadenceRepeat;
            bool matches=true,conclusive=true,complete=true,owned=true,snapshot=true;
            switch(fault) {
            case 0:reuse.upload=false;break;case 1:kind=SourceRepeatKind::NewSource;break;
            case 2:kind=SourceRepeatKind::InsertedOutput;break;case 3:matches=false;break;
            case 4:conclusive=false;break;case 5:complete=false;break;
            case 6:owned=false;break;case 7:snapshot=false;break;
            }
            Assert::IsFalse(ReuseConclusiveSubtitlePacketForSourceRetry(reuse,kind,
                matches,conclusive,complete,owned,snapshot));
        }
        auto changed=context;++changed.identity.acceptedSequence;
        const auto wrong=receipt.Begin(changed,SourceRepeatKind::CadenceRepeat);
        Assert::IsFalse(ReuseConclusiveSubtitlePacketForSourceRetry(wrong,SourceRepeatKind::CadenceRepeat,
            true,true,true,g->temporal.HasCurrentCompleteComposition(2),true));
    }
    TEST_METHOD(NativeRecoveryNominationRequiresCurrentCompleteEvidence) {
        auto pixels=std::make_unique<NativeReconstructionPixels>();
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(*pixels,2);
        auto current=std::make_unique<SubtitleBoxPreview>(cue->preview);
        SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        current->current.text=detector.Analyze(pixels->Source(),PictureTop,PictureBottom,2,3,true);
        current->current.ink=detector.InkSnapshot();
        Assert::IsTrue(current->current.text.detected&&current->current.text.configuredSearchComplete);
        Assert::IsTrue(current->current.ink&&current->current.ink->rawEvidenceComplete);
        const auto accepted=cue->tracker.Consume(*current);
        Assert::IsTrue(accepted.detected&&!accepted.held&&!accepted.confirmationPending);
        const auto identity=ReconstructionIdentity(2);
        Assert::IsTrue(CanRecoverCurrentSubtitlePacket(true,true,true,false,false,
            accepted,*current,{},identity,7,8),L"Current complete measured worker evidence must nominate recovery");
        for(int fault=0;fault<17;++fault) {
            auto bad=std::make_unique<SubtitleBoxPreview>(*current);auto text=accepted;
            bool retry=true,prior=true,composed=true,incomplete=false,exhausted=false;
            switch(fault) {
            case 0:retry=false;break;case 1:prior=false;break;case 2:composed=false;break;
            case 3:incomplete=true;break;case 4:exhausted=true;break;
            case 5:text.held=true;break;case 6:text.confirmationPending=true;break;
            case 7:bad->current.text.configuredSearchComplete=false;break;
            case 8:bad->current.pendingRefresh=true;break;case 9:bad->current.analysisRefresh=true;break;
            case 10:++bad->current.identity.acceptedSequence;break;
            case 11:++bad->policyGeneration;break;case 12:++bad->current.continuityGeneration;break;
            case 13:bad->current.analyzed=false;break;case 14:bad->current.ink.reset();break;
            case 15:++text.capturePanel.left;break;case 16:++text.lineCapturePanels[0].left;break;
            }
            Assert::IsFalse(CanRecoverCurrentSubtitlePacket(retry,prior,composed,incomplete,exhausted,
                text,*bad,{},identity,7,8),L"Partial, refreshed or mismatched evidence cannot nominate packet replacement");
        }
        auto missing=std::make_unique<SubtitleBoxPreview>(cue->preview);
        missing->available=false;missing->current.analyzed=false;
        missing->current.text={};missing->current.ink.reset();
        SubtitleCueContinuation glyphOnly;glyphOnly.valid=true;glyphOnly.localGlyphsOnly=true;
        glyphOnly.identity=identity;glyphOnly.accepted=accepted;
        Assert::IsFalse(CanRecoverCurrentSubtitlePacket(true,true,true,false,false,
            accepted,*missing,glyphOnly,identity,7,8),L"Glyph-only continuation cannot replace complete packet pixels");
    }
    TEST_METHOD(UhdLateCurrentCueReplacesNoCuePacketBeforeSealingSourceReuse) {
        auto pixels=std::make_shared<NativeReconstructionPixels>();
        auto g=std::make_unique<ReconstructionMotionGpu>();
        // The source has real glyphs in its bar, but no worker observation was
        // available when its first complete raw packet was captured.
        g->Capture(*pixels,2);g->Draw();
        Assert::AreEqual(uint64_t(0),g->temporal.SelectedSubtitleSequence());
        Assert::AreEqual(size_t(0),g->YellowPixels());
        SourcePreparationContext context;context.identity=ReconstructionIdentity(2);
        context.source=pixels->Source();context.source.backingOwner=pixels;
        context.queuedSource=context.source;context.gpu=g->device->gpu;
        context.processingEpoch=g->processingEpoch;context.policyGeneration=7;
        context.continuityGeneration=8;context.viewportEpoch=g->viewportEpoch;
        context.outputContract=g->outputContract;
        SourcePreparationReceipt receipt;receipt.Commit(context,true,false);
        const auto calls=g->preparation.RenderCallCount();
        for(int inserted=0;inserted<3;++inserted) {
            const auto reuse=receipt.Begin(context,SourceRepeatKind::InsertedOutput);
            Assert::IsTrue(reuse.upload&&reuse.analysis&&reuse.subtitle);
            g->temporal.outputPts=double(inserted+1)/8.0/SourceHz;g->Draw();
            receipt.Commit(context,true,false);
        }
        Assert::AreEqual(calls,g->preparation.RenderCallCount());
        const auto retry=receipt.Begin(context,SourceRepeatKind::CadenceRepeat);
        Assert::IsTrue(retry.upload&&!retry.analysis&&!retry.subtitle);
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(*pixels,2);
        Assert::IsTrue(cue->decision.accepted.detected&&cue->geometry.valid);
        Assert::IsFalse(CanRecoverCurrentSubtitlePacket(true,true,true,true,false,
            cue->decision.accepted,cue->preview,cue->decision.proof,ReconstructionIdentity(2),7,8));
        // A composed but incomplete retry must leave the first packet unsealed,
        // allowing another exact-source attempt instead of hiding the cue forever.
        receipt.Commit(context,true,false);
        const auto afterIncomplete=receipt.Begin(context,SourceRepeatKind::CadenceRepeat);
        Assert::IsTrue(afterIncomplete.upload&&!afterIncomplete.analysis&&!afterIncomplete.subtitle);
        auto missing=std::make_unique<SubtitleBoxPreview>(cue->preview);
        missing->available=false;missing->current.analyzed=false;missing->current.text={};missing->current.ink.reset();
        Assert::IsFalse(CanRecoverCurrentSubtitlePacket(true,true,true,false,false,
            cue->decision.accepted,*missing,{},ReconstructionIdentity(2),7,8));
        // A late complete worker result, rather than a retained glyph proof,
        // is the authority available in the current beta detector.
        auto late=cue->preview;SubtitleBoxDetector lateDetector;lateDetector.SetNearBarDistance(20);
        late.current.text=lateDetector.Analyze(pixels->Source(),PictureTop,PictureBottom,2,3,true);
        late.current.ink=lateDetector.InkSnapshot();
        const auto lateAccepted=cue->tracker.Consume(late);
        Assert::IsTrue(CanRecoverCurrentSubtitlePacket(true,true,true,false,false,
            lateAccepted,late,{},ReconstructionIdentity(2),7,8));
        g->temporal.Begin(g->settings,SourceHz,DisplayHz,false,0,2,2,0.0,false,true,true);
        const auto pts=g->temporal.SelectedPacketPts();const auto depth=g->temporal.HistoryDepth();
        auto params=g->Parameters();const auto options=CaptureSubtitleSourceProcessingOptions(params);
        pl_frame recovered{};
        Assert::IsTrue(PrepareCompleteSubtitleSourcePacket(g->preparation,g->device->gpu,g->input,g->hook,
            cue->geometry,5,g->style,2,&g->surface,recovered,&options));
        // A failed transactional revision must leave the original owned packet
        // present and drawable; preparation success alone cannot relabel it.
        bool revised=true;
        Assert::IsFalse(g->temporal.ReviseCompleteComposition(g->device->gpu,recovered,3,&revised));
        Assert::IsFalse(revised);
        const auto currentPts=g->temporal.currentPts;g->temporal.currentPts+=1.0/SourceHz;
        Assert::IsFalse(g->temporal.ReviseCompleteComposition(g->device->gpu,recovered,2,&revised));
        Assert::IsFalse(revised);g->temporal.currentPts=currentPts;
        auto wrongFormat=recovered;wrongFormat.color.transfer=PL_COLOR_TRC_SRGB;
        Assert::IsFalse(g->temporal.ReviseCompleteComposition(g->device->gpu,wrongFormat,2,&revised));
        Assert::IsFalse(revised);
        pl_d3d11_params otherParams{};otherParams.allow_software=true;
        auto otherDevice=pl_d3d11_create(nullptr,&otherParams);
        Assert::IsTrue(otherDevice!=nullptr);
        const bool wrongDeviceAccepted=g->temporal.ReviseCompleteComposition(otherDevice->gpu,recovered,2,&revised);
        pl_d3d11_destroy(&otherDevice);
        Assert::IsFalse(wrongDeviceAccepted||revised);
        Assert::IsFalse(g->temporal.ReviseCompleteComposition(g->device->gpu,{},2,&revised));
        Assert::IsFalse(revised);Assert::AreEqual(depth,g->temporal.HistoryDepth());
        Assert::AreEqual(uint64_t(0),g->temporal.SelectedSubtitleSequence());
        g->Draw();Assert::AreEqual(size_t(0),g->YellowPixels());
        Assert::IsTrue(g->temporal.ReviseCompleteComposition(g->device->gpu,recovered,2,&revised));
        Assert::IsTrue(revised);Assert::AreEqual(depth,g->temporal.HistoryDepth());
        Assert::AreEqual(pts,g->temporal.SelectedPacketPts());
        auto pose=g->presentation;pose.cleaned=true;
        Assert::IsTrue(g->temporal.AnnotatePresentation(2,pose));
        Assert::IsTrue(g->temporal.AnnotateHdrSource(2,g->input));
        receipt.Commit(context,true,true);
        g->Draw();Assert::AreEqual(uint64_t(2),g->temporal.SelectedSubtitleSequence());
        Assert::AreEqual(uint64_t(2),g->Hdr().sourceSequence);
        Assert::IsTrue(g->YellowPixels()>100,L"Late native recovery must reach actual selected GPU pixels");
        Assert::IsTrue(receipt.ReuseSubtitle(context,SourceRepeatKind::CadenceRepeat));
        revised=true;
        Assert::IsFalse(g->temporal.ReviseCompleteComposition(g->device->gpu,recovered,2,&revised),
            L"The completed cue packet cannot be revised again");
        Assert::IsFalse(revised);g->Draw();Assert::IsTrue(g->YellowPixels()>100);
    }
    TEST_METHOD(UhdCompleteCaptureRejectsIdentityAndContextRebinding) {
        NativeReconstructionPixels pixels,blank(false,12,1);
        auto cue=std::make_unique<MeasuredReconstructionCue>();cue->Acquire(pixels,2);
        auto g=std::make_unique<ReconstructionMotionGpu>();g->Capture(pixels,2,&cue->geometry);g->Draw();
        bool captured=true;
        Assert::IsTrue(g->temporal.CaptureCompleteComposition(g->device->gpu,g->complete,2,false,&captured));
        Assert::IsFalse(captured);Assert::AreEqual(uint64_t(2),g->temporal.SelectedSubtitleSequence());
        Assert::IsFalse(g->temporal.CaptureCompleteComposition(g->device->gpu,g->complete,3,true,&captured));
        Assert::IsFalse(captured);Assert::AreEqual(uint64_t(2),g->temporal.SelectedSubtitleSequence());
        g->Capture(blank,3);++g->viewportEpoch;Assert::IsFalse(g->Select());
        --g->viewportEpoch;++g->outputContract;Assert::IsFalse(g->Select());
        --g->outputContract;++g->processingEpoch;Assert::IsFalse(g->Select());
        g->temporal.ResetHistoryAfterSourceGap();Assert::AreEqual(size_t(0),g->temporal.HistoryDepth());
        g->Capture(blank,4);Assert::IsFalse(g->Select());g->Draw();
        Assert::AreEqual(size_t(0),g->YellowPixels());Assert::AreEqual(uint64_t(0),g->temporal.LastSubtitleSequence());
    }
};
}