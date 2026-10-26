#pragma once

#include <vprenderer/NlsPipelinePreparation.h>
#include <SubtitleCutPasteShader.h>

// Reuses the generation-aware worker and CPU-only frame snapshot. Subtitle
// hooks are private to this device; live nativePrepare callbacks never cross
// the thread boundary.
struct SubtitlePipelinePreparationRequest {
    NlsPipelinePreparation::Frame source, target;
    bool warmPresentation = false;
    pl_options options = nullptr;
    LUID adapter{};
    int featureLevel = 0;
    bool noCompute = false;
    bool temporal = false;
    SubtitleGeneratedGrayStyle style;
    int spatialFill = 0;

    SubtitlePipelinePreparationRequest(const pl_frame& input,
        const pl_render_params& params, bool temporalComposition,
        const SubtitleGeneratedGrayStyle& generated, int fill, const pl_frame* output = nullptr)
        : temporal(temporalComposition), style(generated), spatialFill(fill) {
        source.Copy(input);
        if (output) { target.Copy(*output); warmPresentation = true; }
        options = pl_options_alloc(nullptr);
        if (!options) throw std::bad_alloc();
        pl_options_reset(options, &params);
        // Only owned filter/deband/deinterlace options are used below.
        options->params.hooks = nullptr;
        options->params.num_hooks = 0;
        options->params.info_callback = nullptr;
        options->params.info_priv = nullptr;
    }
    ~SubtitlePipelinePreparationRequest() { pl_options_free(&options); }
    SubtitlePipelinePreparationRequest(const SubtitlePipelinePreparationRequest&) = delete;
    SubtitlePipelinePreparationRequest& operator=(const SubtitlePipelinePreparationRequest&) = delete;

    bool Compile(pl_cache cache) {
        struct Resources {
            pl_d3d11 device = nullptr;
            SubtitlePreparationRenderer renderer;
            const pl_hook* hook = nullptr;
            pl_tex surface = nullptr, layer = nullptr;
            NlsPipelinePreparation::Frame& source;
            NlsPipelinePreparation::Frame& target;
            Resources(NlsPipelinePreparation::Frame& input, NlsPipelinePreparation::Frame& output) : source(input), target(output) {}
            ~Resources() {
                if (!device) return;
                source.Destroy(device->gpu);
                target.Destroy(device->gpu);
                pl_tex_destroy(device->gpu, &surface);
                pl_tex_destroy(device->gpu, &layer);
                pl_mpv_user_shader_destroy(&hook);
                renderer.Reset();
                pl_d3d11_destroy(&device);
            }
        } owned(source, target);
        pl_d3d11_params deviceParams{};
        deviceParams.adapter_luid = adapter;
        deviceParams.min_feature_level = deviceParams.max_feature_level = featureLevel;
        deviceParams.no_compute = noCompute;
        owned.device = pl_d3d11_create(nullptr, &deviceParams);
        if (!owned.device) return false;
        const auto gpu = owned.device->gpu;
        pl_gpu_set_cache(gpu, cache);

        owned.hook = CreateSubtitleCutPasteHook(gpu);
        if (!owned.renderer.Get(gpu) || !owned.hook || !source.Create(gpu)) return false;

        const int w = source.textures[0].w, h = source.textures[0].h;
        SubtitleCutPasteGeometry geometry{};
        geometry.valid = true;
        geometry.pictureTop = h / 4;
        geometry.pictureBottom = h * 3 / 4;
        geometry.source = { w / 4, h * 7 / 8, w * 3 / 4, h * 15 / 16 };
        geometry.destination = { w / 4, h * 5 / 8, w * 3 / 4, h * 11 / 16 };
        geometry.content = geometry.destination;
        geometry.pictureCapture = geometry.destination;
        geometry.generatedCleanup = geometry.source;
        geometry.glyphLines[0] = geometry.source;
        geometry.glyphScale = 1.0f;
        const auto processing = CaptureSubtitleSourceProcessingOptions(options->params);
        const auto* processingPtr = temporal ? &processing : nullptr;
        const bool fields = source.value.field != PL_FIELD_NONE;
        pl_frame neighbor = source.value;
        if (fields) source.value.prev = source.value.next = &neighbor;

        // Normalization, full composition, cleanup, and independent-alpha
        // layers are distinct graphs. Warm blur on/off for both selectable
        // backgrounds; geometry and styling remain dynamic shader uniforms.
        for (int field = 0; field < (fields ? 2 : 1); ++field) {
            if (field) source.value.field = pl_field_other(source.value.field);
            pl_deinterlace_params deinterlace = options->params.deinterlace_params ?
                *options->params.deinterlace_params : pl_deinterlace_params{};
            for (int algorithm = 0; algorithm < (fields ? 2 : 1); ++algorithm) {
                if (algorithm) deinterlace.algo = PL_DEINTERLACE_BOB; // Missing-neighbor fallback.
                const auto* deinterlacePtr = fields ? &deinterlace : nullptr;
                pl_frame composed{};
                if (!RenderSubtitleFullRaster(owned.renderer, gpu, source.value, nullptr,
                    &owned.surface, composed, temporal, false, false, deinterlacePtr, processingPtr)) return false;
                for (int background : {3, 5}) for (bool blur : {false, true}) {
                    auto generated = style;
                    generated.blurPixels = blur ? (std::max)(1.0f, style.blurPixels) : 0.0f;
                    for (auto phase : {SubtitleCompositionPhase::All, SubtitleCompositionPhase::CleanupOnly}) {
                        if (!BindSubtitleCutPasteHook(owned.hook, geometry, background,
                            source.value.color.transfer, generated, spatialFill, phase) ||
                            !RenderSubtitleFullRaster(owned.renderer, gpu, source.value, owned.hook,
                                &owned.surface, composed, temporal, background == 5, false,
                                deinterlacePtr, processingPtr)) return false;
                        pl_gpu_finish(gpu);
                    }
                    auto glyphs = source.value;
                    glyphs.field = glyphs.first_field = PL_FIELD_NONE;
                    glyphs.prev = glyphs.next = nullptr;
                    if (!BindSubtitleCutPasteHook(owned.hook, geometry, background,
                        glyphs.color.transfer, generated, spatialFill, SubtitleCompositionPhase::LayerOnly) ||
                        !ComposeSubtitleLayerBeforeCrop(owned.renderer, gpu, glyphs, owned.hook,
                            &owned.layer, composed, nullptr, processingPtr)) return false;
                    pl_gpu_finish(gpu);
                }
            }
        }
        if (warmPresentation) {
            if (!target.Create(gpu)) return false;
            pl_frame normalized{};
            auto input = source.value;
            input.field = input.first_field = PL_FIELD_NONE;
            input.prev = input.next = nullptr;
            if (!RenderSubtitleFullRaster(owned.renderer, gpu, input, nullptr,
                &owned.surface, normalized, temporal, false, false, nullptr, processingPtr)) return false;
            auto finalParams = options->params;
            finalParams.deinterlace_params = nullptr; finalParams.frame_mixer = nullptr;
            if (temporal) finalParams.deband_params = nullptr;
            NlsPipelinePreparation::Request final(normalized, target.value, finalParams, "");
            final.adapter = adapter; final.featureLevel = featureLevel; final.noCompute = noCompute;
            final.processedSource = temporal; final.subtitleOverlay = temporal;
            if (!final.Compile(cache)) return false;
        }
        return true;
    }
};
