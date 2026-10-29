#include "pch.h"
#include "AnalysisFrame.h"
#include <DolbyVisionTunnelInput.h>

#include <algorithm>
#include <utility>

namespace
{
    uint64_t Mix(uint64_t hash, uint64_t value)
    {
        // Stable interpretation key. Frame sequence remains a separate transport
        // identity; this only changes when evidence sampling semantics change.
        constexpr uint64_t prime = 1099511628211ull;
        for (unsigned byte = 0; byte < 8; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= prime;
        }
        return hash;
    }

    uint64_t DomainKey(const PreparedAnalysisFrame& result)
    {
        uint64_t hash = 14695981039346656037ull;
        hash = Mix(hash, 2); // range-aware evidence sampling contract version
        hash = Mix(hash, static_cast<uint64_t>(result.domain));
        hash = Mix(hash, static_cast<uint64_t>(result.source.width));
        hash = Mix(hash, static_cast<uint64_t>(result.source.height));
        hash = Mix(hash, static_cast<uint64_t>(result.source.encoding));
        hash = Mix(hash, static_cast<uint64_t>(result.source.colorspace));
        hash = Mix(hash, result.transferKey);
        hash = Mix(hash, static_cast<uint64_t>(result.sampleRange));
        hash = Mix(hash, result.colorDepth);
        hash = Mix(hash, result.bitShift);
        hash = Mix(hash, result.rgbBitAlignment);
        hash = Mix(hash, result.dolbyFullRange ? 1 : 0);
        hash = Mix(hash, result.source.rowBytes);
        hash = Mix(hash, result.source.chromaRowBytes);
        return hash ? hash : 1;
    }

    bool NativeQueuedYuvSupported(VideoFrameEncoding encoding)
    {
        return encoding == VideoFrameEncoding::UYVY ||
            encoding == VideoFrameEncoding::HDYC ||
            encoding == VideoFrameEncoding::V210 ||
            encoding == VideoFrameEncoding::P210 || encoding == VideoFrameEncoding::P010;
    }

    void NativeContract(PreparedAnalysisFrame& result,
        VideoFrameEncoding encoding)
    {
        const bool rgb8 = encoding == VideoFrameEncoding::BGRA_8BIT ||
            encoding == VideoFrameEncoding::ARGB_8BIT;
        const bool rgb12 = encoding == VideoFrameEncoding::R12B ||
            encoding == VideoFrameEncoding::R12L;
        const bool yuv8 = encoding == VideoFrameEncoding::UYVY ||
            encoding == VideoFrameEncoding::HDYC;
        result.colorDepth = rgb12 ? 12 : ((rgb8 || yuv8) ? 8 : 10);
        result.bitShift = (encoding == VideoFrameEncoding::P210 || encoding == VideoFrameEncoding::P010) ? 6 : 0;
        result.sampleRange = (rgb8 || rgb12) ? VideoFrameSampleRange::FULL :
            VideoFrameSampleRange::LIMITED;
    }
}

bool AnalysisNativeRgbSupported(VideoFrameEncoding encoding)
{
    // Analysis keeps native RGB precision independently of the renderer's
    // upload table. The packed R12 readers are native evidence adapters even
    // though those layouts are not accepted by the generic RGB uploader.
    switch (encoding)
    {
    case VideoFrameEncoding::BGRA_8BIT:
    case VideoFrameEncoding::ARGB_8BIT:
    case VideoFrameEncoding::R210:
    case VideoFrameEncoding::R10b:
    case VideoFrameEncoding::R10l:
    case VideoFrameEncoding::R12B:
    case VideoFrameEncoding::R12L:
        return true;
    default:
        return false;
    }
}

PreparedAnalysisFrame PrepareAnalysisFrame(const AnalysisFrameInput& input)
{
    PreparedAnalysisFrame result;
    result.profile = input.profile;
    result.transferKey = input.transferKey;
    if (input.width <= 0 || input.height <= 0)
        return result;

    auto native = [&](AnalysisLumaFormat format, ColorSpace colorspace)
    {
        AnalysisLumaSource source = {
            input.nativeData, input.nativeBytes, input.width, input.height,
            input.nativeRowBytes, 0, format, input.encoding, colorspace,
            input.sourceGeneration
        };
        return source;
    };
    auto converted = [&](AnalysisLumaFormat format)
    {
        const size_t rowBytes = input.convertedRowBytes ?
            input.convertedRowBytes : static_cast<size_t>(input.width) * 2;
        const size_t chromaRowBytes = input.convertedChromaRowBytes ?
            input.convertedChromaRowBytes : static_cast<size_t>(input.width) * 2;
        AnalysisLumaSource source = {
            input.convertedData, input.convertedBytes, input.width, input.height,
            rowBytes, chromaRowBytes, format, input.encoding, input.colorspace,
            input.sourceGeneration
        };
        return source;
    };

    AnalysisLumaSource backing;
    if (input.dolbyVisionTunnel)
    {
        if (input.dolby.requiresBlackFrame)
            return result;
        if (input.dolby.valid)
        {
            if (!DolbyVisionTunnelEncodingSupported(input.encoding) ||
                !input.convertedOwner || !input.converted422 ||
                !input.convertedContract.IsValid() ||
                input.convertedContract.colorDepth != 12 ||
                input.convertedContract.bitShift != 4 ||
                input.convertedContract.sampleRange !=
                    (input.dolby.fullRange ? VideoFrameSampleRange::FULL :
                        VideoFrameSampleRange::LIMITED))
                return result;
            backing = converted(AnalysisLumaFormat::DecodedDolbyVision422);
            result.domain = AnalysisFrameDomain::DecodedDolbyVision422;
            result.sourceOwner = input.convertedOwner;
            result.sampleRange = input.convertedContract.sampleRange;
            result.colorDepth = input.convertedContract.colorDepth;
            result.bitShift = input.convertedContract.bitShift;
            result.dolbyFullRange = input.dolby.fullRange;
            result.usesConverted = true;
        }
        else
        {
            // A missing/invalid packet cannot unlock the packed RGB transport.
            // The formatter alone can authorize a proven SDR alignment.
            if (!input.dolby.allowSdrFallback || input.tunnelSdrRgbRange == TunnelSdrRgbRange::Unknown ||
                (input.dolby.rgbBitAlignment != 1 &&
                    input.dolby.rgbBitAlignment != 2) ||
                !DolbyVisionTunnelEncodingSupported(input.encoding) ||
                !input.nativeOwner)
                return result;
            backing = native(AnalysisLumaFormat::SdrTunnelRgb,
                ColorSpace::REC_709);
            backing.rgbBitAlignment = input.dolby.rgbBitAlignment;
            result.domain = AnalysisFrameDomain::SdrTunnelRgb;
            result.sourceOwner = input.nativeOwner;
            result.sampleRange = input.tunnelSdrRgbRange == TunnelSdrRgbRange::Limited ?
                VideoFrameSampleRange::LIMITED : VideoFrameSampleRange::FULL;
            result.colorDepth = 8;
            result.rgbBitAlignment = input.dolby.rgbBitAlignment;
        }
    }
    else
    {
        // Analysis describes the captured source raster, not whichever
        // conversion the active renderer happens to upload. Require a native
        // adapter for both live and queued consumers; converted render pixels
        // cannot stand in for a missing or unsupported captured source.
        const bool nativeRgb = AnalysisNativeRgbSupported(input.encoding);
        const bool nativeYuv = NativeQueuedYuvSupported(input.encoding);
        if (!nativeRgb && !nativeYuv)
            return result;
        if (nativeRgb)
        {
            backing = native(AnalysisLumaFormat::NativeRgb,
                input.colorspace);
            result.domain = AnalysisFrameDomain::NativeRgb;
        }
        else
        {
            backing = native(input.encoding == VideoFrameEncoding::P210 ? AnalysisLumaFormat::P210 :
                input.encoding == VideoFrameEncoding::P010 ? AnalysisLumaFormat::P010 : AnalysisLumaFormat::NativeYuv422,
                input.colorspace);
            if (input.encoding == VideoFrameEncoding::P210 || input.encoding == VideoFrameEncoding::P010)
                backing.chromaRowBytes = input.nativeRowBytes;
            result.domain = input.encoding == VideoFrameEncoding::P010 ?
                AnalysisFrameDomain::NativeYuv420 : AnalysisFrameDomain::NativeYuv422;
        }
        result.sourceOwner = input.nativeOwner;
        NativeContract(result, input.encoding);
    }

    if (!result.sourceOwner || !backing.IsValid())
        return {};
    backing.backingOwner = result.sourceOwner;
    switch (result.sampleRange)
    {
    case VideoFrameSampleRange::LIMITED:
        backing.range = AnalysisLumaRange::Limited;
        break;
    case VideoFrameSampleRange::FULL:
        backing.range = AnalysisLumaRange::Full;
        break;
    default:
        backing.range = AnalysisLumaRange::Unknown;
        break;
    }
    result.source = std::move(backing);
    if (!result.source.IsValid())
        return {};
    result.domainKey = DomainKey(result);
    return result;
}
