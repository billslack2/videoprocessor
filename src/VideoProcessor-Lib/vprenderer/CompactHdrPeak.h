#pragma once

#include <AnalysisLumaSource.h>
#include <libplacebo/renderer.h>
#include <algorithm>
#include <array>

namespace CompactHdrPeak
{
    struct Measurement
    {
        float peakPq=0, averagePq=0;
        float peakRgbNits=0, averageRgbNits=0;
        unsigned samples=0;
    };

    struct Decision
    {
        bool replaceDetector=false;
        Measurement measurement;
    };

    inline bool IsFullRgb8(const AnalysisLumaSource& source)
    {
        return source.format==AnalysisLumaFormat::NativeRgb &&
            source.range==AnalysisLumaRange::Full &&
            (source.encoding==VideoFrameEncoding::BGRA_8BIT ||
                source.encoding==VideoFrameEncoding::ARGB_8BIT);
    }

    inline bool IsLimitedR10l(const AnalysisLumaSource& source)
    {
        return source.format==AnalysisLumaFormat::NativeRgb &&
            source.range==AnalysisLumaRange::Limited && source.encoding==VideoFrameEncoding::R10l;
    }

    inline bool IsLimitedBt2020Yuv(const AnalysisLumaSource& source)
    {
        return source.range==AnalysisLumaRange::Limited && source.colorspace==ColorSpace::BT_2020 &&
            (source.format==AnalysisLumaFormat::P010 || source.format==AnalysisLumaFormat::P210 ||
                (source.format==AnalysisLumaFormat::NativeYuv422 &&
                    (source.encoding==VideoFrameEncoding::V210 ||
                     source.encoding==VideoFrameEncoding::UYVY || source.encoding==VideoFrameEncoding::HDYC)));
    }

    inline bool Supports(const AnalysisLumaSource& source, const pl_frame& frame)
    {
        if (!source.backingOwner || frame.color.transfer!=PL_COLOR_TRC_PQ ||
            frame.color.primaries!=PL_COLOR_PRIM_BT_2020) return false;
        if (IsFullRgb8(source))
            return frame.repr.sys==PL_COLOR_SYSTEM_RGB && frame.repr.levels==PL_COLOR_LEVELS_FULL;
        if (IsLimitedR10l(source))
            return frame.repr.sys==PL_COLOR_SYSTEM_RGB && frame.repr.levels==PL_COLOR_LEVELS_LIMITED;
        return IsLimitedBt2020Yuv(source) && frame.repr.sys==PL_COLOR_SYSTEM_BT_2020_NC &&
            frame.repr.levels==PL_COLOR_LEVELS_LIMITED;
    }

    inline bool HasSourceLuminance(const pl_hdr_metadata& hdr)
    {
        return hdr.avg_pq_y>0.0f && hdr.avg_pq_y<=hdr.max_pq_y && hdr.max_pq_y<=1.0f;
    }

    // A partial authored dynamic record is still authoritative. Keep the
    // synthetic statistics separate from authored dynamic metadata and static
    // mastering/CLL information.
    inline bool HasAuthoredStatistics(const pl_hdr_metadata& hdr)
    {
        return hdr.max_pq_y || hdr.avg_pq_y || hdr.scene_avg ||
            hdr.scene_max[0] || hdr.scene_max[1] || hdr.scene_max[2] || hdr.ootf.num_anchors;
    }

    inline bool Apply(const Measurement& result, pl_hdr_metadata& hdr)
    {
        if (!result.samples || HasAuthoredStatistics(hdr)) return false;
        hdr.max_pq_y=result.peakPq;
        hdr.avg_pq_y=result.averagePq;
        hdr.scene_avg=result.averageRgbNits;
        for (auto& peak : hdr.scene_max) peak=result.peakRgbNits;
        return true;
    }

    inline void Apply(const Decision& decision, pl_frame& frame, pl_render_params& params)
    {
        if (!params.peak_detect_params || !decision.replaceDetector) return;
        Apply(decision.measurement,frame.color.hdr);
        params.peak_detect_params=nullptr;
    }

    // A stratified grid over the encoded source. At this grid size a 99.995th
    // percentile has less than one sample in its tail. Use sample maxima and
    // identify this as an approximation, separate from libplacebo's histogram.
    inline Measurement Measure(const AnalysisLumaSource& source)
    {
        const bool rgb8=IsFullRgb8(source);
        const bool r10l=IsLimitedR10l(source);
        if ((!rgb8 && !r10l && !IsLimitedBt2020Yuv(source)) || !source.IsValid()) return {};
        static const auto nits=[] {
            std::array<float,1024> values{};
            for (size_t i=0; i<values.size(); ++i)
                values[i]=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,static_cast<float>(i)/1023.0f);
            return values;
        }();
        const auto toNits=[&](float pq) {
            const float bounded=(std::min)(1.0f,(std::max)(0.0f,pq));
            return nits[static_cast<size_t>(1023.0f*bounded+0.5f)];
        };
        static const auto rgb8Nits=[] {
            std::array<float,256> values{};
            for (size_t i=0; i<values.size(); ++i)
                values[i]=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,static_cast<float>(i)/255.0f);
            return values;
        }();
        const auto limitedRgbNits=[](uint16_t code) {
            // Match the renderer's limited RGB range. The lookup is built only
            // when a packed R10l request reaches the worker.
            static const auto values=[] {
                std::array<float,1024> table{};
                for (size_t i=0; i<table.size(); ++i) {
                    const float pq=(static_cast<float>(i)-64.0f)/876.0f;
                    table[i]=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,
                        (std::min)(1.0f,(std::max)(0.0f,pq)));
                }
                return table;
            }();
            return values[code];
        };
        const bool bgra=source.encoding==VideoFrameEncoding::BGRA_8BIT;
        const int columns=(std::min)(source.width,128);
        const int rows=(std::min)(source.height,72);
        Measurement result;
        double sumPq=0, sumRgb=0;
        for (int j=0; j<rows; ++j)
        {
            const size_t y=static_cast<size_t>((int64_t(2*j+1)*source.height)/(2*rows));
            const auto* row=source.data+y*source.rowBytes;
            for (int i=0; i<columns; ++i)
            {
                const size_t x=static_cast<size_t>((int64_t(2*i+1)*source.width)/(2*columns));
                float red, green, blue;
                if (rgb8) {
                    const auto* pixel=row+4*x;
                    red=rgb8Nits[pixel[bgra?2:1]];
                    green=rgb8Nits[pixel[bgra?1:2]];
                    blue=rgb8Nits[pixel[bgra?0:3]];
                } else if (r10l) {
                    uint16_t r, g, b;
                    if (!source.SampleNativeRgb10(static_cast<int>(x),static_cast<int>(y),r,g,b)) return {};
                    red=limitedRgbNits(r);
                    green=limitedRgbNits(g);
                    blue=limitedRgbNits(b);
                } else {
                    AnalysisLumaSample sample;
                    if (!source.Sample(static_cast<int>(x),static_cast<int>(y),sample)) return {};
                    // Native readers return 10-bit codes, including 8-bit
                    // limited codes scaled by four. Undo the non-constant
                    // luminance matrix in PQ code space before linearizing.
                    const float luma=(sample.luma-64.0f)/876.0f;
                    const float cb=(sample.chromaU-512.0f)/896.0f;
                    const float cr=(sample.chromaV-512.0f)/896.0f;
                    red=toNits(luma+1.4746f*cr);
                    green=toNits(luma-0.164553f*cb-0.571353f*cr);
                    blue=toNits(luma+1.8814f*cb);
                }
                const float yNits=0.2627f*red+0.6780f*green+0.0593f*blue;
                if (yNits<=0.0f) continue;
                const float pq=pl_hdr_rescale(PL_HDR_NITS,PL_HDR_PQ,yNits);
                // Match the default soft black cutoff domain. This avoids
                // encoded bars shifting the average, independent of AR detection.
                const float fade=(std::min)(1.0f,pq/0.01f);
                const float weightedPq=pq*fade*fade*(3.0f-2.0f*fade);
                if (weightedPq<=0.0f) continue;
                const float maxRgb=(std::max)({red,green,blue});
                result.peakPq=(std::max)(result.peakPq,weightedPq);
                result.peakRgbNits=(std::max)(result.peakRgbNits,maxRgb);
                sumPq+=weightedPq;
                sumRgb+=maxRgb;
                ++result.samples;
            }
        }
        if (result.samples) {
            result.averagePq=static_cast<float>(sumPq/result.samples);
            result.averageRgbNits=static_cast<float>(sumRgb/result.samples);
        }
        return result;
    }

    inline void Smooth(Measurement& current, const Measurement& previous)
    {
        if (!current.samples || !previous.samples) return;
        const auto rapidRise=[](float now,float old) {
            return now>=old ? now : old+0.35f*(now-old);
        };
        current.peakPq=rapidRise(current.peakPq,previous.peakPq);
        current.peakRgbNits=rapidRise(current.peakRgbNits,previous.peakRgbNits);
        current.averagePq=previous.averagePq+0.35f*(current.averagePq-previous.averagePq);
        current.averageRgbNits=previous.averageRgbNits+0.35f*(current.averageRgbNits-previous.averageRgbNits);
    }
}
