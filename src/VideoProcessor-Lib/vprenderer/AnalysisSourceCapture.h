#pragma once

#include <Windows.h>
#include <ActivePictureDecisionTimeline.h>
#include <ActivePictureEvidence.h>
#include <DebugLog.h>
#include <algorithm>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

// Opt-in developer diagnostic. The source is the original analysis buffer,
// before subtitle composition, tone mapping, or output scaling. Never a replay
// input or an evidence override. Existing files are never overwritten.
class AnalysisSourceCapture
{
public:
    void Observe(const AnalysisLumaSource& source, const ActivePictureFrameIdentity& identity,
        const ActivePictureBounds& detector, ActivePictureClassification classification,
        bool detectorAvailable, uint64_t detectorSequence, const ActivePictureBounds& logical)
    {
        if (m_done) return;
        try {
            if (!m_checked) {
                m_checked=true;
                const DWORD required=GetEnvironmentVariableW(L"VP_ANALYSIS_CAPTURE_PATH",nullptr,0);
                if (!required) { m_done=true; return; }
                if (required>32760) { Fail("path-too-long",ERROR_INVALID_NAME); return; }
                std::vector<wchar_t> path(required);
                const DWORD read=GetEnvironmentVariableW(L"VP_ANALYSIS_CAPTURE_PATH",path.data(),required);
                if (!read || read>=required) { Fail("path-read",GetLastError()); return; }
                m_prefix.assign(path.data(),read);
                const bool drive=m_prefix.size()>3 &&
                    ((m_prefix[0]>=L'A' && m_prefix[0]<=L'Z') || (m_prefix[0]>=L'a' && m_prefix[0]<=L'z')) &&
                    m_prefix[1]==L':' && (m_prefix[2]==L'\\' || m_prefix[2]==L'/');
                const bool unc=m_prefix.size()>4 && m_prefix[0]==L'\\' && m_prefix[1]==L'\\';
                if ((!drive && !unc) || m_prefix.back()==L'\\' || m_prefix.back()==L'/') {
                    Fail("absolute-file-prefix-required",ERROR_INVALID_NAME); return;
                }
                DebugLog::Log("ANALYSIS SOURCE CAPTURE: armed sequence_min=120 prefix=%ls",m_prefix.c_str());
            }
            if (identity.acceptedSequence<120) return;
            m_done=true; // One attempt only, including any I/O failure.
            constexpr size_t MaxBytes=256u*1024u*1024u;
            if (!source.IsValid() || source.width>8192 || source.height>4320 ||
                !source.dataBytes || source.dataBytes>MaxBytes ||
                source.generation!=identity.transportGeneration) {
                Fail("invalid-or-unbounded-source",ERROR_INVALID_DATA); return;
            }
            std::ostringstream json; json.imbue(std::locale::classic());
            auto bounds=[&](const ActivePictureBounds& b) {
                json<<"{\"left\":"<<b.left<<",\"top\":"<<b.top<<",\"right\":"<<b.right
                    <<",\"bottom\":"<<b.bottom<<",\"rasterWidth\":"<<b.rasterWidth
                    <<",\"rasterHeight\":"<<b.rasterHeight<<",\"trustedBarAxes\":"<<int(b.trustedBarAxes)<<"}";
            };
            json<<"{\n\"schema\":1,\n\"stage\":\"raw-analysis-before-subtitle-composition\",\n"
                <<"\"byteOrder\":\"little-endian\",\n\"dataBytes\":"<<source.dataBytes
                <<",\n\"width\":"<<source.width<<",\n\"height\":"<<source.height
                <<",\n\"rowBytes\":"<<source.rowBytes<<",\n\"chromaRowBytes\":"<<source.chromaRowBytes
                <<",\n\"format\":"<<int(source.format)<<",\n\"formatName\":\""<<AnalysisLumaFormatName(source)
                <<"\",\n\"encoding\":"<<int(source.encoding)<<",\n\"colorspace\":"<<int(source.colorspace)
                <<",\n\"generation\":"<<source.generation<<",\n\"identity\":{\"transportGeneration\":"<<identity.transportGeneration
                <<",\"acceptedSequence\":"<<identity.acceptedSequence<<",\"sourceFrameNumber\":"<<identity.sourceFrameNumber
                <<",\"captureTimestamp\":"<<identity.captureTimestamp<<",\"sourceFormatGeneration\":"<<identity.sourceFormatGeneration
                <<",\"viewportGeneration\":"<<identity.viewportGeneration<<",\"rendererGeneration\":"<<identity.rendererGeneration<<"},\n"
                <<"\"detectorAvailable\":"<<(detectorAvailable?"true":"false")<<",\n\"detectorSequence\":"<<detectorSequence
                <<",\n\"detectorClassification\":"<<int(classification)<<",\n\"detectorBounds\":";
            bounds(detector); json<<",\n\"logicalBounds\":";bounds(logical);json<<"\n}\n";
            const auto metadata=json.str();
            const auto rawPath=m_prefix+L".raw", jsonPath=m_prefix+L".json";
            const HANDLE raw=CreateFileW(rawPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (raw==INVALID_HANDLE_VALUE) { Fail("create-raw",GetLastError()); return; }
            const HANDLE meta=CreateFileW(jsonPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (meta==INVALID_HANDLE_VALUE) {
                const DWORD error=GetLastError();CloseHandle(raw);DeleteFileW(rawPath.c_str());Fail("create-json",error);return;
            }
            DWORD error=0;
            const bool written=WriteAll(raw,source.data,source.dataBytes,error) &&
                WriteAll(meta,reinterpret_cast<const uint8_t*>(metadata.data()),metadata.size(),error);
            const bool rawClosed=CloseHandle(raw)!=FALSE,metaClosed=CloseHandle(meta)!=FALSE;
            if (!written || !rawClosed || !metaClosed) {
                if (!error) error=GetLastError();
                DeleteFileW(rawPath.c_str());DeleteFileW(jsonPath.c_str());Fail("write-or-close",error);return;
            }
            DebugLog::Log("ANALYSIS SOURCE CAPTURE: saved raw=%ls metadata=%ls bytes=%zu dimensions=%dx%d format=%d generation=%llu sequence=%llu detector_sequence=%llu composed=0",
                rawPath.c_str(),jsonPath.c_str(),source.dataBytes,source.width,source.height,int(source.format),
                source.generation,identity.acceptedSequence,detectorSequence);
        } catch (...) { Fail("exception",ERROR_UNHANDLED_EXCEPTION); }
    }
private:
    static bool WriteAll(HANDLE handle,const uint8_t* bytes,size_t count,DWORD& error)
    {
        while (count) {
            const DWORD chunk=static_cast<DWORD>((std::min)(count,size_t(4u*1024u*1024u)));
            DWORD written=0;
            if (!WriteFile(handle,bytes,chunk,&written,nullptr) || written!=chunk) {
                error=GetLastError();if(!error)error=ERROR_WRITE_FAULT;return false;
            }
            bytes+=written;count-=written;
        }
        return true;
    }
    void Fail(const char* reason,DWORD error) {
        m_done=true;
        DebugLog::Log("ANALYSIS SOURCE CAPTURE: failed reason=%s error=%lu prefix=%ls composed=0",reason,error,m_prefix.c_str());
    }
    bool m_checked=false,m_done=false;
    std::wstring m_prefix;
};
