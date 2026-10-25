#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "ToneCurveProvider.h"
#include "ToneCurveRuntime.h"
#include <vprenderer/LibplaceboRenderParameters.h>
#include <libplacebo/tone_mapping.h>
#include <libplacebo/colorspace.h>
#include <libplacebo/gamut_mapping.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

// Samples the tone curve as VP Renderer (Beta) builds it, on a grey scene.
// Nothing links libplacebo: its functions and data are found by name in VP
// Renderer (Beta)'s copy, libplacebo-377.dll. Every curve value comes from
// pl_tone_map_sample.
//
// ColorMap() copies the tone part of LibplaceboRenderParameters::Build()
// (src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp): the
// high-quality colour-map parameters (tone mapping ignores Quality), the chosen curve, its
// VP_LIBPLACEBO_BETA block, and the tone-key loop with its knee_default clamp.
// That file cannot be compiled here, because it imports libplacebo data
// directly. If its mapping changes, change this copy.
//
// Map() then repeats what pl_shader_color_map_ex() in his fork's
// src/shaders/colorspace.c (codex/vp-master-beta 8bccd657) computes and does
// not export, in its order, citing its lines: the source's metadata with what
// peak detection reports, its nominal luminance, infer, round near-equal
// peaks, the signal black, the input peak, the dynamic target and the
// mastering soft clip. On grey, film strength changes nothing (both ends of
// its blend are the same, lines 2341-2379) and gamut overflow correction acts
// on colour only.
namespace
{
    struct Library
    {
        void (*infer)(pl_tone_map_params*) = nullptr;
        float (*sample)(float, const pl_tone_map_params*) = nullptr;
        float (*rescale)(pl_hdr_scaling, pl_hdr_scaling, float) = nullptr;
        void (*inferSpace)(pl_color_space*) = nullptr;
        void (*nominalLuma)(const pl_nominal_luma_params*) = nullptr;
    };

    bool Resolve(Library& library, std::wstring& error)
    {
        if (!ToneCurveRuntime::Load(error))
            return false;
        library.infer = reinterpret_cast<decltype(library.infer)>(ToneCurveRuntime::Find("pl_tone_map_params_infer"));
        library.sample = reinterpret_cast<decltype(library.sample)>(ToneCurveRuntime::Find("pl_tone_map_sample"));
        library.rescale = reinterpret_cast<decltype(library.rescale)>(ToneCurveRuntime::Find("pl_hdr_rescale"));
        library.inferSpace = reinterpret_cast<decltype(library.inferSpace)>(ToneCurveRuntime::Find("pl_color_space_infer"));
        library.nominalLuma = reinterpret_cast<decltype(library.nominalLuma)>(ToneCurveRuntime::Find("pl_color_space_nominal_luma_ex"));
        if (library.infer && library.sample && library.rescale && library.inferSpace && library.nominalLuma)
            return true;
        error = L"libplacebo-377.dll is missing a tone-mapping function";
        return false;
    }

    template<typename T>
    const T* Data(const char* exportName, std::wstring& error)
    {
        const T* value = static_cast<const T*>(ToneCurveRuntime::Find(exportName));
        if (!value)
            error = L"libplacebo-377.dll is missing " + std::wstring(exportName, exportName + strlen(exportName));
        return value;
    }

    // As ToneMapping() in LibplaceboRenderParameters.cpp.
    const char* ToneMapping(const std::string& toneMapping)
    {
        if (toneMapping == "bt2390") return "pl_tone_map_bt2390";
        if (toneMapping == "st2094-40") return "pl_tone_map_st2094_40";
        if (toneMapping == "reinhard") return "pl_tone_map_reinhard";
        return "pl_tone_map_spline";
    }

    bool ColorMap(const ToneCurveProvider::Request& request, bool useTuning,
        pl_color_map_params& map, std::wstring& error)
    {
        const char* presetName = request.quality == "fast" ? "pl_render_fast_params" :
            request.quality == "balanced" ? "pl_render_default_params" : "pl_render_high_quality_params";
        const auto* preset = Data<pl_render_params>(presetName, error);
        if (!preset) return false;
        const auto* defaults = preset->color_map_params;
        if (!defaults) defaults = Data<pl_color_map_params>("pl_color_map_default_params", error);
        if (!defaults) return false;
        map = *defaults;
        if (request.toneMapping != "auto")
        {
            const pl_tone_map_function* function = Data<pl_tone_map_function>(ToneMapping(request.toneMapping), error);
            if (!function) return false;
            map.tone_mapping_function = function;
        }
        if (!useTuning) return true;
        // Its VP_LIBPLACEBO_BETA block; vp_gamut_compression acts on colour only.
        map.dynamic_target = request.dynamicTarget;
        if (request.toneTuning[ToneMappingTuning::FilmStrength])
            map.film_strength = static_cast<float>(*request.toneTuning[ToneMappingTuning::FilmStrength]);
        if (request.toneTuning[ToneMappingTuning::MasteringClip])
            map.mastering_clip = static_cast<float>(*request.toneTuning[ToneMappingTuning::MasteringClip]);
        auto& constants = map.tone_constants;
        float* toneFields[] = { &constants.knee_adaptation, &constants.knee_minimum,
            &constants.knee_maximum, &constants.knee_default, &constants.slope_tuning,
            &constants.slope_offset, &constants.spline_contrast, &constants.knee_offset,
            &constants.reinhard_contrast };
        for (int i = 0; i < ToneMappingTuning::Percentile; ++i)
            if (request.toneTuning[i]) *toneFields[i] = static_cast<float>(*request.toneTuning[i]);
        constants.knee_default = (std::max)(constants.knee_minimum,
            (std::min)(constants.knee_maximum, constants.knee_default));
        return true;
    }

    // The curve's parameters and what the shader does around it.
    struct Mapping
    {
        pl_tone_map_params tone{};
        float exposure = 1.0f;     // dt_exposure
        float virtualTargetNits = 0.0f;
        float clip = 0.0f;         // mclip
        float masterNits = 0.0f;
        bool measured = false;     // peak detection reported the scene
        pl_hdr_metadata detected{}; // what it reported
    };

    // What peak detection reports for the scene's mix
    // (pl_get_detected_hdr_metadata, 1579-1604), its two averages formed as
    // the detector forms them (1242-1251): the mean of the PQ values
    // (avg_pq_y, the knee's) and the mean of linear light (scene_avg, the
    // dynamic target's). On grey, max Y and max RGB are both the peak.
    bool Detected(const Library& library, const ToneCurveProvider::Request& request,
        pl_hdr_metadata& detected)
    {
        if (!request.sceneAverageKnown || request.sceneMix.empty())
            return false;
        double share = 0.0, sumPq = 0.0, sumLinear = 0.0;
        for (const auto& part : request.sceneMix)
        {
            share += part.share;
            sumPq += part.share * library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(part.nits));
            sumLinear += part.share * part.nits;
        }
        if (!(share > 0.0))
            return false;
        detected = pl_hdr_metadata{};
        const float peak = static_cast<float>(request.scenePeakNits);
        detected.max_pq_y = library.rescale(PL_HDR_NITS, PL_HDR_PQ, peak);
        detected.avg_pq_y = static_cast<float>(sumPq / share);
        detected.scene_avg = library.rescale(PL_HDR_PQ, PL_HDR_NITS,
            library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(sumLinear / share)));
        for (float& channel : detected.scene_max)
            channel = peak;
        return true;
    }

    // The source as the shader sees it: a PQ source whose static metadata is
    // the mastering peak, with what peak detection reports filling in what
    // the source does not author (1905-1917), then inferred as
    // pl_color_space_infer_map does to an HDR source (997-1036).
    pl_color_space Source(const Library& library, const ToneCurveProvider::Request& request,
        const pl_hdr_metadata* detected)
    {
        pl_color_space src{};
        src.primaries = PL_COLOR_PRIM_BT_2020;
        src.transfer = PL_COLOR_TRC_PQ;
        src.hdr.max_luma = static_cast<float>(request.masteringPeakNits);
        if (detected)
        {
            // PL_DEF, src/common.h 135 (not exported).
            const auto def = [](float x, float d) { return x ? x : d; };
            src.hdr.max_pq_y  = def(src.hdr.max_pq_y,  detected->max_pq_y);
            src.hdr.avg_pq_y  = def(src.hdr.avg_pq_y,  detected->avg_pq_y);
            src.hdr.scene_avg = def(src.hdr.scene_avg, detected->scene_avg);
            for (int i = 0; i < 3; i++)
                src.hdr.scene_max[i] = def(src.hdr.scene_max[i], detected->scene_max[i]);
        }
        library.inferSpace(&src);
        return src;
    }

    Mapping Map(const Library& library, const pl_color_map_params& map,
        const ToneCurveProvider::Request& request)
    {
        const auto pq = [&](double nits) {
            return library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(nits));
        };
        const auto nits = [&](float value) { return library.rescale(PL_HDR_PQ, PL_HDR_NITS, value); };
        // Peak detection measures the scene; without it, or on a scene with
        // only metadata, there is only metadata.
        Mapping mapping;
        mapping.measured = Detected(library, request, mapping.detected);
        const pl_color_space src = Source(library, request, mapping.measured ? &mapping.detected : nullptr);
        pl_tone_map_params& tone = mapping.tone;
        // A null function becomes pl_tone_map_clip inside infer, as in the
        // shader. The deprecated `param` stays 0: VP never sets it.
        tone.function = map.tone_mapping_function;
        tone.constants = map.tone_constants;
        tone.input_scaling = PL_HDR_PQ;
        tone.output_scaling = PL_HDR_PQ;
        tone.lut_size = map.lut_size;
        tone.hdr = src.hdr;
        // 1947-1954: the source's peak, floor and average, as libplacebo picks them.
        pl_nominal_luma_params luma{};
        luma.color = &src;
        luma.metadata = map.metadata;
        luma.scaling = tone.input_scaling;
        luma.out_min = &tone.input_min;
        luma.out_max = &tone.input_max;
        luma.out_avg = &tone.input_avg;
        library.nominalLuma(&luma);
        // The display as VP sets it (ApplyTargetLuminance): its black and peak.
        tone.output_min = pq(LibplaceboRenderParameters::ResolveTargetBlack(
            static_cast<float>(request.blackNits)));
        tone.output_max = pq(request.targetNits);

        // colorspace.c 1964-1975.
        library.infer(&tone);
        if (std::fabs(tone.input_max - tone.output_max) < 1e-6)
            tone.output_max = tone.input_max;
        if (std::fabs(tone.input_min - tone.output_min) < 1e-6)
            tone.output_min = tone.input_min;
        const float signalBlack = pq(PL_COLOR_HDR_BLACK); // pl_signal_black, src/colorspace.h 26
        if (tone.input_min <= signalBlack) tone.input_min = 0.0f;
        if (tone.output_min <= signalBlack) tone.output_min = 0.0f;

        // 1977-1994. On grey every channel's scene maximum is the scene peak.
        if (!map.inverse_tone_mapping)
        {
            tone.input_max = (std::max)(tone.input_max, tone.output_max);
            const float sceneMax = (std::max)(src.hdr.scene_max[0],
                (std::max)(src.hdr.scene_max[1], src.hdr.scene_max[2]));
            if (sceneMax > 0)
                tone.input_max = (std::max)(tone.input_max, pq(sceneMax));
        }

        // 1999-2050, the dynamic target. It takes the linear average, or the
        // PQ average when there is none (2003-2005).
        const float outMinPhys = tone.output_min;
        float sceneAverage = src.hdr.scene_avg;
        if (!(sceneAverage > 0) && src.hdr.avg_pq_y > 0)
            sceneAverage = nits(src.hdr.avg_pq_y);
        if (map.dynamic_target && sceneAverage > 0 && !map.inverse_tone_mapping &&
            tone.input_max > tone.output_max + 1e-4f)
        {
            const float dtDim = 0.09f, dtSoft = 0.04f;
            const float pPq = tone.output_max;
            const float sPq = tone.input_max;
            const float pNits = nits(pPq);
            const float fPq = pq(sceneAverage);
            const float dtU = pPq;
            const float dtV = fPq - dtDim;
            const float tPq = (std::clamp)(0.5f * (dtU + dtV) +
                std::hypot(0.5f * (dtU - dtV), dtSoft), pPq, sPq);
            if (tPq > pPq + 1e-4f && tPq < sPq - 1e-4f)
            {
                const float tNits = nits(tPq);
                mapping.exposure = pNits / tNits;
                mapping.virtualTargetNits = tNits;
                tone.output_max = tPq;
                tone.output_min = pq(nits(outMinPhys) / mapping.exposure);
            }
        }

        // 2182-2198, the mastering soft clip's strength; it needs the measured peak.
        mapping.masterNits = static_cast<float>(request.masteringPeakNits);
        float clip = (std::clamp)(map.mastering_clip, 0.0f, 1.0f);
        if (clip > 0.0f && mapping.masterNits > 0.0f && mapping.masterNits < 9999.0f &&
            mapping.measured && mapping.detected.max_pq_y > 0.0f)
        {
            // pl_smoothstep, src/common.h 140.
            float x = (nits(mapping.detected.max_pq_y) / mapping.masterNits - 1.1f) / (1.4f - 1.1f);
            x = (std::clamp)(x, 0.0f, 1.0f);
            clip *= 1.0f - x * x * (3.0f - 2.0f * x);
        }
        else
            clip = 0.0f;
        mapping.clip = clip;
        return mapping;
    }

    // One grey source level through the shader's order: mastering soft clip
    // (2199-2212), the curve (pl_tone_map_sample, as its LUT), then the dynamic
    // target's exposure (2377).
    float Apply(const Library& library, const Mapping& mapping, float sourcePq)
    {
        float value = sourcePq;
        if (mapping.clip > 1e-3f)
        {
            const float knee = 0.95f;
            const float source = library.rescale(PL_HDR_PQ, PL_HDR_NITS, value);
            float mcn = source / mapping.masterNits;
            const float mct = (std::max)(mcn - knee, 0.0f) * (1.0f / (1.0f - knee));
            mcn = (std::min)(mcn, knee) + (1.0f - knee) * mct / (1.0f + mct);
            const float clipped = source + (mapping.masterNits * mcn - source) * mapping.clip;
            value = library.rescale(PL_HDR_NITS, PL_HDR_PQ, clipped);
        }
        value = library.sample(value, &mapping.tone);
        if (mapping.exposure != 1.0f)
            value = library.rescale(PL_HDR_NITS, PL_HDR_PQ,
                mapping.exposure * library.rescale(PL_HDR_PQ, PL_HDR_NITS, value));
        return value;
    }

    // The knee the film blend measures against, in PQ, 2307-2324: the scene's
    // average, else the fallback knee, within the knee limits.
    float KneePq(const pl_tone_map_params& tone)
    {
        const pl_tone_map_constants& tc = tone.constants;
        const auto mix = [](float a, float b, float x) { return a + (b - a) * x; }; // PL_MIX
        float knee = tone.input_avg ? tone.input_avg : mix(tone.input_min, tone.input_max, tc.knee_default);
        knee = (std::clamp)(knee, mix(tone.input_min, tone.input_max, tc.knee_minimum),
            mix(tone.input_min, tone.input_max, tc.knee_maximum));
        return knee;
    }

    bool Sample(const ToneCurveProvider::Request& request,
        ToneCurveProvider::Result& result, std::wstring& error)
    {
        result = ToneCurveProvider::Result{};
        Library library;
        if (!Resolve(library, error))
            return false;
        // libplacebo's default, the same whatever the profile chooses: its own
        // curve and constants, peak detection on, High quality. It is drawn on
        // the profile's display (Target nits and black) and the chosen scene.
        ToneCurveProvider::Request fixed = request;
        fixed.quality = "high";
        fixed.toneMapping = "auto";
        fixed.peakDetection = "auto";
        fixed.toneTuning = {};
        fixed.sceneAverageKnown = true;
        pl_color_map_params profile{}, standard{};
        if (!ColorMap(request, true, profile, error) || !ColorMap(fixed, false, standard, error))
            return false;
        const Mapping profileMap = Map(library, profile, request);
        const Mapping standardMap = Map(library, standard, fixed);
        result.profilePq.reserve(request.sourcePq.size());
        result.defaultPq.reserve(request.sourcePq.size());
        for (double x : request.sourcePq)
        {
            result.profilePq.push_back(Apply(library, profileMap, static_cast<float>(x)));
            result.defaultPq.push_back(Apply(library, standardMap, static_cast<float>(x)));
        }
        const pl_tone_map_params& tone = profileMap.tone;
        result.inputMaxPq = tone.input_max;
        result.scenePeakPq = library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(request.scenePeakNits));
        result.outputMaxPq = library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(request.targetNits));
        result.outputMinPq = tone.output_min;
        result.inputMinPq = tone.input_min;
        result.kneePq = KneePq(tone);
        result.curve = tone.function && tone.function->name ? tone.function->name : "clip";
        result.virtualTargetNits = profileMap.virtualTargetNits;
        result.exposure = profileMap.exposure;
        result.masteringClip = profileMap.clip;
        if (profileMap.measured)
        {
            result.scenePqAverageNits = library.rescale(PL_HDR_PQ, PL_HDR_NITS, profileMap.detected.avg_pq_y);
            result.sceneLinearAverageNits = profileMap.detected.scene_avg;
        }
        return true;
    }

    // ---- The Colour tab ----
    // Colour() takes one colour at a time on from the curve's setup above,
    // repeating the rest of pl_shader_color_map_ex() in his fork's
    // colorspace.c, in its order: the mastering space (use_mastering_space,
    // 981-1005), the gamut-mapping setup (2053-2160), the mastering soft clip
    // on RGB (2173-2212), IPT (ipt_convert, 1818-1833), the curve on each cone
    // channel with the film blend (2214-2381), gamut mapping (2384-2431), back
    // to RGB (2433-2444) and gamut overflow correction (2446-2510). The
    // matrices, the curve, the PQ steps and gamut mapping are libplacebo's own
    // functions; only the shader's per-pixel arithmetic is copied. Not
    // modelled: contrast recovery (a detail pass), and the interpolation of
    // the shader's tone and gamut tables (each value here is exact).
    struct ColourLibrary
    {
        pl_matrix3x3 (*rgb2lms)(const pl_raw_primaries*) = nullptr;
        pl_matrix3x3 (*lms2rgb)(const pl_raw_primaries*) = nullptr;
        const pl_matrix3x3* lms2ipt = nullptr;
        const pl_matrix3x3* ipt2lms = nullptr;
        const pl_raw_primaries* (*primaries)(pl_color_primaries) = nullptr;
        pl_matrix3x3 (*rgb2xyz)(const pl_raw_primaries*) = nullptr;
        pl_matrix3x3 (*xyz2rgb)(const pl_raw_primaries*) = nullptr;
        pl_matrix3x3 (*mapping)(const pl_raw_primaries*, const pl_raw_primaries*, pl_rendering_intent) = nullptr;
        bool (*valid)(const pl_raw_primaries*) = nullptr;
        bool (*equal)(const pl_raw_primaries*, const pl_raw_primaries*) = nullptr;
        bool (*compatible)(const pl_raw_primaries*, const pl_raw_primaries*) = nullptr;
        pl_raw_primaries (*clip)(const pl_raw_primaries*, const pl_raw_primaries*) = nullptr;
        void (*apply)(const pl_matrix3x3*, float[3]) = nullptr;
        bool (*toneNoop)(const pl_tone_map_params*) = nullptr;
        bool (*gamutNoop)(const pl_gamut_map_params*) = nullptr;
        void (*gamutSample)(float[3], const pl_gamut_map_params*) = nullptr;
        const pl_gamut_map_function* gamutClip = nullptr;
    };

    bool ResolveColour(ColourLibrary& colour, std::wstring& error)
    {
        if (!ToneCurveRuntime::Load(error))
            return false;
        bool found = true;
        const auto bind = [&](auto& slot, const char* name) {
            slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(ToneCurveRuntime::Find(name));
            found = found && slot;
        };
        bind(colour.rgb2lms, "pl_ipt_rgb2lms");
        bind(colour.lms2rgb, "pl_ipt_lms2rgb");
        bind(colour.lms2ipt, "pl_ipt_lms2ipt");
        bind(colour.ipt2lms, "pl_ipt_ipt2lms");
        bind(colour.primaries, "pl_raw_primaries_get");
        bind(colour.rgb2xyz, "pl_get_rgb2xyz_matrix");
        bind(colour.xyz2rgb, "pl_get_xyz2rgb_matrix");
        bind(colour.mapping, "pl_get_color_mapping_matrix");
        bind(colour.valid, "pl_primaries_valid");
        bind(colour.equal, "pl_raw_primaries_equal");
        bind(colour.compatible, "pl_primaries_compatible");
        bind(colour.clip, "pl_primaries_clip");
        bind(colour.apply, "pl_matrix3x3_apply");
        bind(colour.toneNoop, "pl_tone_map_params_noop");
        bind(colour.gamutNoop, "pl_gamut_map_params_noop");
        bind(colour.gamutSample, "pl_gamut_map_sample");
        bind(colour.gamutClip, "pl_gamut_map_clip");
        if (found)
            return true;
        error = L"libplacebo-377.dll is missing a colour function";
        return false;
    }

    pl_color_primaries Primaries(ToneCurveProvider::Gamut gamut)
    {
        switch (gamut)
        {
        case ToneCurveProvider::Gamut::P3D65: return PL_COLOR_PRIM_DISPLAY_P3; // as VP's P3-D65 target
        case ToneCurveProvider::Gamut::Bt2020: return PL_COLOR_PRIM_BT_2020;
        default: return PL_COLOR_PRIM_BT_709;
        }
    }

    // As GamutMapping() in LibplaceboRenderParameters.cpp.
    const char* GamutMapping(const std::string& gamutMapping)
    {
        if (gamutMapping == "softclip") return "pl_gamut_map_softclip";
        if (gamutMapping == "relative") return "pl_gamut_map_relative";
        if (gamutMapping == "desaturate") return "pl_gamut_map_desaturate";
        return "pl_gamut_map_perceptual";
    }

    void UvOf(const float xyz[3], double& u, double& v)
    {
        const double d = xyz[0] + 15.0 * xyz[1] + 3.0 * xyz[2];
        u = d > 0 ? 4.0 * xyz[0] / d : 0.0;
        v = d > 0 ? 9.0 * xyz[1] / d : 0.0;
    }

    bool Corners(ToneCurveProvider::Gamut gamut, ToneCurveProvider::GamutCorners& corners)
    {
        ColourLibrary colour;
        std::wstring error;
        if (!ResolveColour(colour, error)) return false;
        const pl_raw_primaries* raw = colour.primaries(Primaries(gamut));
        const pl_cie_xy points[4] = { raw->red, raw->green, raw->blue, raw->white };
        for (int i = 0; i < 4; ++i)
        {
            const float xyz[3] = { points[i].x / points[i].y, 1.0f,
                (1.0f - points[i].x - points[i].y) / points[i].y };
            UvOf(xyz, corners.u[i], corners.v[i]);
        }
        return true;
    }

    bool Colour(const ToneCurveProvider::Request& request, bool libplaceboDefault,
        ToneCurveProvider::Gamut display, const std::vector<ToneCurveProvider::ColourSample>& samples,
        std::vector<ToneCurveProvider::ColourPoint>& points, std::wstring& error)
    {
        points.clear();
        Library library;
        ColourLibrary colour;
        if (!Resolve(library, error) || !ResolveColour(colour, error))
            return false;
        // The same settings as the grey charts' lines (Sample()).
        ToneCurveProvider::Request used = request;
        if (libplaceboDefault)
        {
            used.quality = "high";
            used.toneMapping = "auto";
            used.peakDetection = "auto";
            used.toneTuning = {};
            used.sceneAverageKnown = true;
        }
        pl_color_map_params map{};
        if (!ColorMap(used, !libplaceboDefault, map, error))
            return false;
        if (!libplaceboDefault)
        {
            // Build()'s gamut mapping and its VP_LIBPLACEBO_BETA gamut flag.
            if (used.gamutMapping != "auto")
            {
                const pl_gamut_map_function* function = Data<pl_gamut_map_function>(GamutMapping(used.gamutMapping), error);
                if (!function) return false;
                map.gamut_mapping = function;
            }
            map.vp_gamut_compression = used.gamutOverflowCorrection;
        }
        const Mapping mapping = Map(library, map, used);
        const pl_tone_map_params& tone = mapping.tone;
        const float white = PL_COLOR_SDR_WHITE; // the shader's RGB: 1.0 = 203 nits

        // The source: a BT.2020 container whose InfoFrame says P3.
        // use_mastering_space, 981-1005.
        const pl_raw_primaries* container = colour.primaries(PL_COLOR_PRIM_BT_2020);
        const pl_raw_primaries mastering = *colour.primaries(PL_COLOR_PRIM_DISPLAY_P3);
        bool useMastering = colour.valid(&mastering) && !colour.equal(&mastering, container);
        if (useMastering)
        {
            const pl_matrix3x3 m2c = colour.mapping(&mastering, container, PL_INTENT_RELATIVE_COLORIMETRIC);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    if (m2c.m[i][j] < -0.01f) useMastering = false;
        }
        const pl_matrix3x3 spaceMat = colour.mapping(container, &mastering, PL_INTENT_RELATIVE_COLORIMETRIC);
        const pl_raw_primaries* srcSpace = useMastering ? &mastering : container;

        // The display: the chosen gamut, its black and peak from the profile
        // (ApplyTargetLuminance); dst.hdr.prim is inferred from its primaries.
        const pl_raw_primaries* target = colour.primaries(Primaries(display));
        const float blackNits = LibplaceboRenderParameters::ResolveTargetBlack(static_cast<float>(used.blackNits));
        const float peakNits = static_cast<float>(used.targetNits);

        // 2053-2095. Table sizes do not matter to pl_gamut_map_sample.
        pl_gamut_map_params gamut{};
        gamut.function = map.gamut_mapping ? map.gamut_mapping : colour.gamutClip;
        gamut.constants = map.gamut_constants;
        gamut.input_gamut = *srcSpace;
        gamut.output_gamut = *target;
        gamut.lut_size_I = gamut.lut_size_C = gamut.lut_size_h = 1;
        gamut.lut_stride = 3;
        gamut.min_luma = library.rescale(PL_HDR_NITS, PL_HDR_PQ, blackNits);
        gamut.max_luma = library.rescale(PL_HDR_NITS, PL_HDR_PQ, peakNits);
        if (gamut.min_luma <= library.rescale(PL_HDR_NITS, PL_HDR_PQ, PL_COLOR_HDR_BLACK)) // pl_signal_black
            gamut.min_luma = 0.0f;
        if (!map.gamut_expansion && gamut.function->bidirectional &&
            colour.compatible(&gamut.input_gamut, &gamut.output_gamut))
            gamut.output_gamut = colour.clip(&gamut.output_gamut, &gamut.input_gamut);
        // 2097-2121 change the function only for gamut_mode or intent, which VP
        // never sets; 2154-2161 only for saturation mapping, which VP does not offer.
        const bool needTone = !colour.toneNoop(&tone) || mapping.exposure != 1.0f;
        const bool needGamut = !colour.gamutNoop(&gamut);

        const pl_matrix3x3 toContainer = colour.xyz2rgb(container);
        const pl_matrix3x3 toMastering = colour.xyz2rgb(&mastering);
        const pl_matrix3x3 rgb2lmsContainer = colour.rgb2lms(container);
        const pl_matrix3x3 rgb2lms = colour.rgb2lms(srcSpace);
        const pl_matrix3x3 lms2rgbSrc = colour.lms2rgb(srcSpace);
        const pl_matrix3x3 lms2rgb = colour.lms2rgb(target);
        const pl_matrix3x3 displayXyz = colour.rgb2xyz(target);
        double whiteU = 0.0, whiteV = 0.0;
        {
            float xyz[3] = { 1.0f, 1.0f, 1.0f };
            colour.apply(&displayXyz, xyz);
            UvOf(xyz, whiteU, whiteV);
        }

        // ipt_convert, 1818-1833: RGB to LMS, PQ, IPT.
        const auto toIpt = [&](const float rgb[3], float lms[3], float lmspq[3], float ipt[3]) {
            for (int i = 0; i < 3; ++i) lms[i] = rgb[i];
            colour.apply(&rgb2lms, lms);
            for (int i = 0; i < 3; ++i)
                lmspq[i] = library.rescale(PL_HDR_NORM, PL_HDR_PQ, (std::max)(lms[i], 0.0f));
            for (int i = 0; i < 3; ++i) ipt[i] = lmspq[i];
            colour.apply(colour.lms2ipt, ipt);
        };

        // The knee the film blend measures against, 2307-2324.
        const float flareFrac = library.rescale(PL_HDR_PQ, PL_HDR_NITS, tone.output_min) / 10000.0f;
        const float filmStrength = (std::clamp)(map.film_strength, 0.0f, 2.0f);
        const auto mix = [](float a, float b, float x) { return a + (b - a) * x; }; // PL_MIX
        const float knee = KneePq(tone);
        const float kneeIn = library.rescale(PL_HDR_PQ, PL_HDR_NITS, knee) / 10000.0f;
        const float kneeOut = library.rescale(PL_HDR_PQ, PL_HDR_NITS, library.sample(knee, &tone)) / 10000.0f;
        const float kneeGain = (std::max)(kneeOut - flareFrac, 1e-9f) / (std::max)(kneeIn, 1e-9f);

        const auto& patches = ToneCurveProvider::Patches();
        points.reserve(samples.size());
        for (const auto& sample : samples)
        {
            ToneCurveProvider::ColourPoint point;
            if (sample.patch >= patches.size()) { points.push_back(point); continue; }
            const ToneCurveProvider::Patch& patch = patches[sample.patch];
            // The patch's chromaticity, through libplacebo's matrix for its space.
            float xyz[3] = { static_cast<float>(patch.r), static_cast<float>(patch.g), static_cast<float>(patch.b) };
            if (patch.srgb8)
                for (float& c : xyz) // the sRGB decoding, IEC 61966-2-1
                {
                    c /= 255.0f;
                    c = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
            const pl_matrix3x3 patchXyz = colour.rgb2xyz(colour.primaries(Primaries(patch.space)));
            colour.apply(&patchXyz, xyz);
            const float sum = xyz[0] + xyz[1] + xyz[2];
            const float x = xyz[0] / sum, y = xyz[1] / sum;
            // The sample's level is the patch's strongest channel in the
            // mastering space, so every colour is compared at the same drive
            // and a dim colour such as blue is not pushed past the scene.
            float unit[3] = { x / y, 1.0f, (1.0f - x - y) / y };
            colour.apply(&toMastering, unit);
            const float strongest = (std::max)((std::max)(unit[0], (std::max)(unit[1], unit[2])), 1e-9f);
            const float luminance = static_cast<float>(sample.sourceNits) / white / strongest;
            const float source[3] = { x / y * luminance, luminance, (1.0f - x - y) / y * luminance };
            UvOf(source, point.sourceU, point.sourceV);

            // The scene can hold it: in the mastering space, no channel above
            // the scene peak (the explorer's rule).
            float held[3] = { source[0], source[1], source[2] };
            colour.apply(&toMastering, held);
            const float scenePeak = static_cast<float>(used.scenePeakNits) / white * 1.000001f;
            point.valid = held[0] <= scenePeak && held[1] <= scenePeak && held[2] <= scenePeak;

            float rgb[3] = { source[0], source[1], source[2] };
            colour.apply(&toContainer, rgb);
            if (!needTone && !needGamut)
            {
                // The fast path, 2163-2171: a primaries conversion only.
                colour.apply(&rgb2lmsContainer, rgb);
                colour.apply(&lms2rgb, rgb);
            }
            else
            {
                if (useMastering) colour.apply(&spaceMat, rgb);
                // The mastering soft clip on each channel, 2179-2212.
                if (mapping.clip > 1e-3f)
                {
                    const float kneeClip = 0.95f;
                    const float mNorm = library.rescale(PL_HDR_NITS, PL_HDR_NORM, mapping.masterNits);
                    for (float& c : rgb)
                    {
                        float mcn = c * (1.0f / mNorm);
                        const float mct = (std::max)(mcn - kneeClip, 0.0f) * (1.0f / (1.0f - kneeClip));
                        mcn = (std::min)(mcn, kneeClip) + (1.0f - kneeClip) * mct / (1.0f + mct);
                        c = mix(c, mNorm * mcn, mapping.clip);
                    }
                }
                float lms[3], lmspq[3], ipt[3];
                toIpt(rgb, lms, lmspq, ipt);
                if (needTone)
                {
                    // The curve on each cone channel, then the film blend,
                    // 2341-2381. tone_map() is the shader's table of
                    // pl_tone_map_sample; here it is the function itself.
                    float tmO[3];
                    for (int i = 0; i < 3; ++i)
                        tmO[i] = library.rescale(PL_HDR_PQ, PL_HDR_NITS, library.sample(lmspq[i], &tone)) / 10000.0f;
                    const float tmLx = (std::clamp)(white / 10000.0f * (std::max)(rgb[0], (std::max)(rgb[1], rgb[2])), 0.0f, 1.0f);
                    const float tmVx = library.rescale(PL_HDR_NITS, PL_HDR_PQ, tmLx * 10000.0f);
                    const float tmOx = library.rescale(PL_HDR_PQ, PL_HDR_NITS, library.sample(tmVx, &tone)) / 10000.0f;
                    const float tmGa = (std::max)(tmOx - flareFrac, 0.0f) / (std::max)(tmLx, 1e-9f);
                    float tmU = tmLx > kneeIn ? (std::clamp)(1.0f - tmGa * (1.0f / kneeGain), 0.0f, 1.0f) : 0.0f;
                    tmU *= tmU;
                    const float lmsMax = (std::max)((std::max)(lms[0], (std::max)(lms[1], lms[2])), 1e-9f);
                    float result[3];
                    for (int i = 0; i < 3; ++i)
                    {
                        const float tmS = (std::clamp)(lms[i] * (1.0f / lmsMax), 0.0f, 1.0f);
                        const float tmW = filmStrength * mix(tmS, 1.0f, tmU);
                        const float tmR = mix(lms[i] * (tmGa * (white / 10000.0f)), tmO[i] - flareFrac, tmW);
                        result[i] = flareFrac + tmR;
                    }
                    colour.apply(&lms2rgbSrc, result);
                    const float tmE = mapping.exposure * 10000.0f / white;
                    for (int i = 0; i < 3; ++i) rgb[i] = tmE * result[i];
                    toIpt(rgb, lms, lmspq, ipt);
                }
                if (needGamut)
                    colour.gamutSample(ipt, &gamut);
                // Back to linear RGB on the display, 2433-2444.
                for (int i = 0; i < 3; ++i) lms[i] = ipt[i];
                colour.apply(colour.ipt2lms, lms);
                for (int i = 0; i < 3; ++i)
                    lms[i] = library.rescale(PL_HDR_PQ, PL_HDR_NORM, (std::max)(lms[i], 0.0f));
                for (int i = 0; i < 3; ++i) rgb[i] = lms[i];
                colour.apply(&lms2rgb, rgb);
            }

            // Gamut overflow correction, 2446-2510. The chart's display
            // declares its own primaries, so signal and physical are the same.
            if (needTone && map.vp_gamut_compression && !map.inverse_tone_mapping)
            {
                const float wSum = displayXyz.m[1][0] + displayXyz.m[1][1] + displayXyz.m[1][2];
                const float black = blackNits / white, span = (peakNits - blackNits) / white;
                if (span > 0 && wSum > 0)
                {
                    float a[3];
                    for (int i = 0; i < 3; ++i) a[i] = (rgb[i] - black) / span;
                    const float m = (std::min)((std::min)((std::min)(a[0], a[1]), a[2]), 0.0f);
                    const float v[3] = { a[0] - m, a[1] - m, a[2] - m };
                    const float M = (std::max)((std::max)(v[0], v[1]), v[2]);
                    const float lo = (std::min)((std::min)(v[0], v[1]), v[2]);
                    const float saturation = M > 0.0f ? (M - lo) / M : 0.0f;
                    const float reserve = 0.02f * saturation;
                    const float q = 1.0f - reserve;
                    if (m < 0.0f || M > q)
                    {
                        const float over = (std::max)(M - q, 0.0f);
                        const float den = std::sqrt(reserve * reserve + over * over);
                        const float f = den > 0.0f ? over / den : 0.0f;
                        const float s = M <= q ? M : 1.0f - reserve * (1.0f - f);
                        float b[3];
                        for (int i = 0; i < 3; ++i) b[i] = M > 0.0f ? (v[i] / M) * s : 0.0f;
                        const float w[3] = { displayXyz.m[1][0] / wSum, displayXyz.m[1][1] / wSum, displayXyz.m[1][2] / wSum };
                        const float yin = w[0] * a[0] + w[1] * a[1] + w[2] * a[2];
                        const float yb = w[0] * b[0] + w[1] * b[1] + w[2] * b[2];
                        const float cap = yb > 0.0f ? (std::min)(1.0f, (std::max)(yin, 0.0f) / yb) : 1.0f;
                        for (int i = 0; i < 3; ++i) rgb[i] = black + span * (b[i] * cap);
                    }
                }
            }

            // On screen: a channel pushed more than 1% past the display's
            // range is clipped (the explorer's rule); the display shows the
            // clamped colour.
            const float top = peakNits / white;
            for (float& c : rgb)
            {
                if (c > top * 1.01f || c < -top * 0.01f) point.clipped = true;
                c = (std::clamp)(c, 0.0f, top);
            }
            colour.apply(&displayXyz, rgb);
            point.nits = rgb[1] * white;
            UvOf(rgb, point.u, point.v);
            const double dS = std::hypot(point.sourceU - whiteU, point.sourceV - whiteV);
            const double dO = std::hypot(point.u - whiteU, point.v - whiteV);
            point.saturation = dS > 1e-6 ? dO / dS : 1.0;
            double dh = (std::atan2(point.v - whiteV, point.u - whiteU) -
                std::atan2(point.sourceV - whiteV, point.sourceU - whiteU)) * 180.0 / 3.14159265358979323846;
            if (dh > 180.0) dh -= 360.0;
            if (dh < -180.0) dh += 360.0;
            point.hue = dO > 1e-6 && dS > 1e-6 ? dh : 0.0;
            points.push_back(point);
        }
        return true;
    }

    double Rescale(pl_hdr_scaling from, pl_hdr_scaling to, double value)
    {
        Library library;
        std::wstring error;
        if (!Resolve(library, error)) return std::nan("");
        return library.rescale(from, to, static_cast<float>(value));
    }

    double NitsToPq(double nits) { return Rescale(PL_HDR_NITS, PL_HDR_PQ, nits); }
    double PqToNits(double pq) { return Rescale(PL_HDR_PQ, PL_HDR_NITS, pq); }

    // Registered before main(); the page itself never touches libplacebo.
    const bool registered = [] {
        auto& functions = ToneCurveProvider::Registered();
        functions.sample = &Sample;
        functions.nitsToPq = &NitsToPq;
        functions.pqToNits = &PqToNits;
        functions.colour = &Colour;
        functions.corners = &Corners;
        return true;
    }();
}
