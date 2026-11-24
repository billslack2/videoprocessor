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
        float hlgMin=0.0f, hlgMax=0.0f;
    };

    struct RgbCodeRange
    {
        int black=0, white=0;
    };

    inline RgbCodeRange NativeRgbCodeRange(const AnalysisLumaSource& source)
    {
        if (source.format!=AnalysisLumaFormat::NativeRgb) return {};
        switch (source.encoding)
        {
        case VideoFrameEncoding::BGRA_8BIT:
        case VideoFrameEncoding::ARGB_8BIT:
            return source.range==AnalysisLumaRange::Full ? RgbCodeRange{0,255} : RgbCodeRange{};
        case VideoFrameEncoding::R12B:
        case VideoFrameEncoding::R12L:
            // The existing native readers normalize packed 12-bit RGB to
            // the same full 10-bit code domain as the renderer's formatter.
            return source.range==AnalysisLumaRange::Full ? RgbCodeRange{0,1023} : RgbCodeRange{};
        case VideoFrameEncoding::R210:
            return source.range==AnalysisLumaRange::Limited ? RgbCodeRange{64,960} : RgbCodeRange{};
        case VideoFrameEncoding::R10b:
        case VideoFrameEncoding::R10l:
            return source.range==AnalysisLumaRange::Limited ? RgbCodeRange{64,940} : RgbCodeRange{};
        default:
            return {};
        }
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
        if (!source.backingOwner || frame.color.primaries!=PL_COLOR_PRIM_BT_2020 ||
            (frame.color.transfer!=PL_COLOR_TRC_PQ && frame.color.transfer!=PL_COLOR_TRC_HLG))
            return false;
        // Sample the captured RGB even when the renderer uploaded P010.
        if (NativeRgbCodeRange(source).white) {
            const auto levels=source.range==AnalysisLumaRange::Full ?
                PL_COLOR_LEVELS_FULL : PL_COLOR_LEVELS_LIMITED;
            return frame.repr.levels==levels && (frame.repr.sys==PL_COLOR_SYSTEM_RGB ||
                frame.repr.sys==PL_COLOR_SYSTEM_BT_2020_NC);
        }
        return IsLimitedBt2020Yuv(source) && frame.repr.sys==PL_COLOR_SYSTEM_BT_2020_NC &&
            frame.repr.levels==PL_COLOR_LEVELS_LIMITED;
    }

    inline bool InferHlgMeasurementColor(pl_color_space& source, pl_color_space target)
    {
        pl_color_space_infer_map(&source,&target);
        // libplacebo adapts HLG to an HDR destination directly. Only measure
        // when source luminance exceeds the inferred destination luminance.
        return source.hdr.max_luma>target.hdr.max_luma+1e-6f;
    }


    inline bool ReplacesDetector(const Decision& decision, const pl_color_space& source,
        const pl_color_space& target)
    {
        if (!decision.replaceDetector || !decision.hlgMax)
            return decision.replaceDetector;
        // A queued HLG packet can outlive a target change. Its luminance
        // measurements are valid only under the original inference contract.
        if (source.transfer!=PL_COLOR_TRC_HLG) return false;
        auto color=source;
        return InferHlgMeasurementColor(color,target) &&
            color.hdr.min_luma==decision.hlgMin && color.hdr.max_luma==decision.hlgMax;
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

    inline void Apply(const Decision& decision, pl_frame& frame, const pl_color_space& target,
        pl_render_params& params)
    {
        if (!params.peak_detect_params || !ReplacesDetector(decision,frame.color,target)) return;
        Apply(decision.measurement,frame.color.hdr);
        params.peak_detect_params=nullptr;
    }

    template<unsigned Size, int Black, int White>
    const std::array<float,Size>& PqNitsTable()
    {
        static const auto values=[] {
            std::array<float,Size> table{};
            for (size_t i=0; i<table.size(); ++i) {
                const float pq=(static_cast<float>(i)-Black)/(White-Black);
                table[i]=pl_hdr_rescale(PL_HDR_PQ,PL_HDR_NITS,(std::min)(1.0f,(std::max)(0.0f,pq)));
            }
            return table;
        }();
        return values;
    }

    // A 128x72 stratified grid is too small to estimate a 99.995th percentile.
    // Use the sample maximum as an approximation of the GPU histogram peak.
    inline Measurement Measure(const AnalysisLumaSource& source, const pl_color_space& color=pl_color_space_hdr10)
    {
        const auto rgb=NativeRgbCodeRange(source);
        if ((!rgb.white && !IsLimitedBt2020Yuv(source)) || !source.IsValid()) return {};
        const bool hlg=color.transfer==PL_COLOR_TRC_HLG;
        const bool rgb8=rgb.white==255;
        const bool bgra=source.encoding==VideoFrameEncoding::BGRA_8BIT;
        const float* pqNits=nullptr;
        if (!hlg) {
            if (rgb8)
                pqNits=PqNitsTable<256,0,255>().data();
            else if (rgb.white==960)
                pqNits=PqNitsTable<1024,64,960>().data();
            else if (rgb.white==940)
                pqNits=PqNitsTable<1024,64,940>().data();
            else
                pqNits=PqNitsTable<1024,0,1023>().data();
        }
        const auto toNits=[pqNits](float pq) {
            const float bounded=(std::min)(1.0f,(std::max)(0.0f,pq));
            return pqNits[static_cast<size_t>(1023.0f*bounded+0.5f)];
        };
        const float rgbScale=rgb.white ? 1.0f/(rgb.white-rgb.black) : 0.0f;
        const int columns=(std::min)(source.width,128);
        const int rows=(std::min)(source.height,72);
        Measurement result;
        double sumPq=0, sumRgb=0;
        for (int j=0; j<rows; ++j)
        {
            const int y=static_cast<int>((int64_t(2*j+1)*source.height)/(2*rows));
            const auto* row=source.data+static_cast<size_t>(y)*source.rowBytes;
            for (int i=0; i<columns; ++i)
            {
                const int x=static_cast<int>((int64_t(2*i+1)*source.width)/(2*columns));
                float red, green, blue;
                if (rgb.white) {
                    uint16_t r, g, b;
                    if (rgb8) {
                        const auto* pixel=row+static_cast<size_t>(x)*4;
                        r=pixel[bgra?2:1];
                        g=pixel[bgra?1:2];
                        b=pixel[bgra?0:3];
                    } else if (!source.SampleNativeRgb10(x,y,r,g,b)) {
                        return {};
                    }
                    red=hlg ? (r-rgb.black)*rgbScale : pqNits[r];
                    green=hlg ? (g-rgb.black)*rgbScale : pqNits[g];
                    blue=hlg ? (b-rgb.black)*rgbScale : pqNits[b];
                } else {
                    AnalysisLumaSample sample;
                    if (!source.Sample(x,y,sample)) return {};
                    // Decode non-constant luminance YCbCr to nonlinear RGB
                    // before interpreting the PQ or HLG transfer.
                    const float luma=(sample.luma-64.0f)/876.0f;
                    const float cb=(sample.chromaU-512.0f)/896.0f;
                    const float cr=(sample.chromaV-512.0f)/896.0f;
                    red=luma+1.4746f*cr;
                    green=luma-0.164553f*cb-0.571353f*cr;
                    blue=luma+1.8814f*cb;
                    if (!hlg) {
                        red=toNits(red);
                        green=toNits(green);
                        blue=toNits(blue);
                    }
                }
                if (hlg) {
                    // The HLG OOTF depends on all channels and the inferred
                    // black/peak luminance, so separate RGB lookups are insufficient.
                    if ((std::max)({red,green,blue})<=0.0f) continue;
                    float linear[]={red,green,blue};
                    pl_color_linearize(&color,linear);
                    red=linear[0]*PL_COLOR_SDR_WHITE;
                    green=linear[1]*PL_COLOR_SDR_WHITE;
                    blue=linear[2]*PL_COLOR_SDR_WHITE;
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
