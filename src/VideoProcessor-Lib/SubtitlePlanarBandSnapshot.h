#pragma once
#include <AnalysisLumaSource.h>
#include <SubtitleBoxDetector.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Worker-only snapshot of complete native planar rows at the top and bottom of
// a source. Byte comparison is deliberately stricter than Sample(): padding
// bits may reject reuse, but no native luma/chroma change can pass unnoticed.
// This object owns only copied bytes, never the source buffer or its owner.
class SubtitlePlanarBandSnapshot {
public:
    static constexpr size_t MaxBytes=64u*1024u*1024u;

    bool Capture(const AnalysisLumaSource& source,int topEnd,int bottomStart) {
        Reset();
        if(!Supported(source) || topEnd<0 || bottomStart<topEnd || bottomStart>source.height ||
            (source.format==AnalysisLumaFormat::P010 && ((topEnd|bottomStart)&1)))return false;
        const size_t row=size_t(source.width)*2;
        const int chromaDivisor=source.format==AnalysisLumaFormat::P010?2:1;
        const size_t lumaRows=size_t(topEnd)+size_t(source.height-bottomStart);
        const size_t chromaRows=size_t(topEnd/chromaDivisor)+size_t((source.height-bottomStart)/chromaDivisor);
        if(!lumaRows || row>MaxBytes || lumaRows+chromaRows>MaxBytes/row)return false;
        m_bytes.resize(row*(lumaRows+chromaRows));
        size_t offset=0;
        const auto copyRows=[&](const uint8_t* base,size_t pitch,int first,int end) {
            for(int y=first;y<end;++y) {
                std::memcpy(m_bytes.data()+offset,base+size_t(y)*pitch,row);offset+=row;
            }
        };
        copyRows(source.data,source.rowBytes,0,topEnd);
        copyRows(source.data,source.rowBytes,bottomStart,source.height);
        const auto* chroma=source.data+source.rowBytes*size_t(source.height);
        copyRows(chroma,source.chromaRowBytes,0,topEnd/chromaDivisor);
        copyRows(chroma,source.chromaRowBytes,bottomStart/chromaDivisor,source.height/chromaDivisor);
        m_width=source.width;m_height=source.height;m_topEnd=topEnd;m_bottomStart=bottomStart;
        m_format=source.format;m_encoding=source.encoding;m_colorspace=source.colorspace;m_range=source.range;
        return true;
    }

    bool Matches(const AnalysisLumaSource& source) const {
        if(m_bytes.empty() || !Supported(source) || source.width!=m_width || source.height!=m_height ||
            source.format!=m_format || source.encoding!=m_encoding || source.colorspace!=m_colorspace ||
            source.range!=m_range)return false;
        const size_t row=size_t(m_width)*2;
        const int divisor=m_format==AnalysisLumaFormat::P010?2:1;
        size_t offset=0;
        const auto matchRows=[&](const uint8_t* base,size_t pitch,int first,int end) {
            for(int y=first;y<end;++y) {
                if(std::memcmp(m_bytes.data()+offset,base+size_t(y)*pitch,row)!=0)return false;
                offset+=row;
            }
            return true;
        };
        const auto* chroma=source.data+source.rowBytes*size_t(source.height);
        return matchRows(source.data,source.rowBytes,0,m_topEnd) &&
            matchRows(source.data,source.rowBytes,m_bottomStart,m_height) &&
            matchRows(chroma,source.chromaRowBytes,0,m_topEnd/divisor) &&
            matchRows(chroma,source.chromaRowBytes,m_bottomStart/divisor,m_height/divisor);
    }

    bool Covers(const SubtitleBoxRect& r) const {
        return !m_bytes.empty() && r.Valid() && r.left>=0 && r.right<=m_width &&
            r.top>=0 && r.bottom<=m_height && (r.bottom<=m_topEnd || r.top>=m_bottomStart ||
                (m_topEnd==m_bottomStart));
    }
    void Reset() {m_bytes.clear();m_width=m_height=m_topEnd=m_bottomStart=0;}
private:
    static bool Supported(const AnalysisLumaSource& source) {
        return (source.format==AnalysisLumaFormat::P010 || source.format==AnalysisLumaFormat::P210 ||
            source.format==AnalysisLumaFormat::DecodedDolbyVision422) && source.IsValid();
    }
    std::vector<uint8_t> m_bytes;
    int m_width=0,m_height=0,m_topEnd=0,m_bottomStart=0;
    AnalysisLumaFormat m_format=AnalysisLumaFormat::P010;
    VideoFrameEncoding m_encoding=VideoFrameEncoding::UNKNOWN;
    ColorSpace m_colorspace=ColorSpace::UNKNOWN;
    AnalysisLumaRange m_range=AnalysisLumaRange::Unknown;
};
