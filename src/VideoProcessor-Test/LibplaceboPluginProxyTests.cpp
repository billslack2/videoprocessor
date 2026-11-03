#include "pch.h"
#include <SourcePictureDetector.h>
#include "CppUnitTest.h"

#include "../VideoProcessor-Lib/vprenderer/LibplaceboPluginVideoRenderer.h"
#include "../VideoProcessor-Lib/AspectRatioOsd.h"
#include "../VideoProcessor-Lib/vprenderer/OptionalRendererLayout.h"

#include <type_traits>
#include <tuple>
#include <array>
#include <limits>
#include <vprenderer/ShaderCacheStorage.h>
#include <vprenderer/AlphaSourceCropPolicy.h>
#include <vector>
#include <map>
#include <sstream>
#include <AnalysisLumaSource.h>
#include <SubtitleMeasurementWorker.h>
#include <set>
#include <string>
#include <algorithm>
#include <cstdio>
#include <ConfigFile.h>
#include <UnifiedProfileRuntime.h>
#include "../VideoProcessor-Lib/vprenderer/LibplaceboRendererPluginApi.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <memory>
#include "DolbyVisionTunnelTestSender.h"

namespace
{
    constexpr ULONGLONG ColdShaderPreparationBudgetMs=180000;
    std::mutex snapshotTestLogMutex;
    std::ofstream snapshotTestLog;
    std::atomic<unsigned> snapshotTestReads{0};
    std::atomic<unsigned> nativeP210Initializations{0};
    std::vector<std::string> decisionTestMessages;
    void __cdecl SnapshotTestLog(const char* message)
    {
        if (strstr(message, "native P210 (no CPU repack) upload")) ++nativeP210Initializations;
        if (strstr(message, "Configuration disk read:")) ++snapshotTestReads;
        std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
        if(strstr(message,"VP presentation decision:"))decisionTestMessages.emplace_back(message);
        if (snapshotTestLog) snapshotTestLog << message << std::endl;
    }
    std::mutex classicPoseLogMutex;
    std::vector<std::string> classicPoseMessages;
    struct ClassicShaderPreparationSelection {
        std::string state;
        uint64_t key=0,generation=0,format=0,requested=0,effective=0;
        bool AllowsInitialBuildFallback(uint64_t sourceGeneration,uint64_t sourceFormat,unsigned mode) const {
            return (state=="missing" || state=="pending") && key && generation==sourceGeneration &&
                format==sourceFormat && requested==mode && effective==0;
        }
    };
    ClassicShaderPreparationSelection classicCurrentShaderPreparation;
    using ClassicSubmittedPreparationKey=std::array<uint64_t,3>;
    std::map<ClassicSubmittedPreparationKey,ClassicShaderPreparationSelection> classicSubmittedShaderPreparation;
    using ClassicPreparationHistory=std::map<ClassicSubmittedPreparationKey,ClassicShaderPreparationSelection>;
    bool RecordSubmittedShaderPreparation(ClassicPreparationHistory& history,
        const AnalysisDiagnosticRecordV1& record,const ClassicShaderPreparationSelection& current)
    {
        if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))return false;
        const ClassicSubmittedPreparationKey key{record.rendererInstanceKey,record.transportGeneration,record.acceptedSequence};
        // This history describes the pixels actually displayed. A repeated image
        // retains its original pipeline state even when the consumed input has
        // just made the current shader key Ready. Missing originals fail closed.
        if((record.flags&16384u) || record.cadenceRepeat || record.counters[0]!=record.counters[1])
            return (record.flags&16384u) && record.cadenceRepeat &&
                !strcmp(record.reason,"complete-output-input-cadence-repeat") &&
                record.acceptedSequence==record.counters[1] &&
                record.counters[0]==record.counters[1]+1 && history.find(key)!=history.end() &&
                record.rendererGeneration==record.transportGeneration &&
                history.at(key).generation==record.transportGeneration &&
                history.at(key).format==record.sourceFormatGeneration;
        if(record.acceptedSequence!=record.counters[0] || !record.acceptedSequence)return false;
        history[key]=current;
        return true;
    }
    void ResetShaderPreparationHistory(ClassicPreparationHistory& history,ClassicShaderPreparationSelection& current)
    {
        history.clear();current={};
    }
    void __cdecl ClassicPoseTestLog(const char* message)
    {
        if(!strstr(message,"VP submitted geometry:") && !strstr(message,"Alpha source crop:") &&
            !strstr(message,"Alpha queue ") && !strstr(message,"Alpha backlog recovery:") && !strstr(message,"Alpha native presentation commit:") && !strstr(message,"SUBTITLE ") &&
            !strstr(message,"application profile") && !strstr(message,"Renderer configuration snapshot") &&
            !strstr(message,"VP source preparation stage:") && !strstr(message,"VP source preparation dispatch:") && !strstr(message,"VP accepted source preparation unavailable:") &&
            !strstr(message,"VP spatial detector interval:") &&
            !strstr(message,"VP subtitle canvas ") && !strstr(message,"VP subtitle cadence cache:") && !strstr(message,"VP source measurement interval:") && !strstr(message,"VP aspect telemetry:") && !strstr(message,"VP subtitle background preparation:") &&
            !strstr(message,"Subtitle shader preparation rejected:") && !strstr(message,"shader cache") && !strstr(message,"Alpha relocated caption evidence:") &&
            !strstr(message,"Alpha relocated caption geometry:") && !strstr(message,"Alpha caption unknown presentation:"))return;
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        const std::string line(message);
        if(line.find("VP subtitle background preparation: stage=selection state=")!=std::string::npos) {
            auto integer=[&](const char* key) {
                const auto at=line.find(key);
                return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };
            const auto at=line.find(" state=")+7;const auto end=line.find(' ',at);
            classicCurrentShaderPreparation.state=line.substr(at,end-at);
            classicCurrentShaderPreparation.key=integer(" key=");
            classicCurrentShaderPreparation.generation=integer(" generation=");
            classicCurrentShaderPreparation.format=integer(" format=");
            classicCurrentShaderPreparation.requested=integer(" requested=");
            classicCurrentShaderPreparation.effective=integer(" effective=");
        }
        classicPoseMessages.emplace_back(message);
    }
    // Background callbacks retain all failure evidence. Forward only preparation
    // progress from the test thread; final flushing runs after renderer teardown,
    // including when an assertion unwinds while compilation is still finishing.
    struct ClassicPreparationLogForwarder {
        size_t cursor=0;
        void Flush() {
            std::vector<std::string> progress;
            {
                std::lock_guard<std::mutex> guard(classicPoseLogMutex);
                for(;cursor<classicPoseMessages.size();++cursor) {
                    const auto& line=classicPoseMessages[cursor];
                    if(line.find("VP subtitle cadence cache:")!=std::string::npos ||
                        line.find("VP subtitle background preparation:")!=std::string::npos ||
                        line.find("Subtitle shader preparation rejected:")!=std::string::npos ||
                        line.find("shader cache")!=std::string::npos)
                        progress.push_back(line);
                }
            }
            for(const auto& line:progress)
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(line.c_str());
        }
        ~ClassicPreparationLogForwarder(){Flush();}
    };
    std::vector<AnalysisDiagnosticRecordV1> classicPoseRecords;
    bool classicPoseAllRecordStages=false;
    std::atomic<bool> classicPoseCaptureSubtitleAdmissions{false};
    void StoreCallbackOrderedRecord(std::vector<AnalysisDiagnosticRecordV1>& records,const AnalysisDiagnosticRecordV1& record)
    {
        // Emit forwards recordSequence==0 before the optional recorder assigns
        // its own sequence. This owned copy uses ONLY the mutex-ordered test
        // callback ordinal; it is not a production recorder/source sequence.
        auto observed=record;observed.recordSequence=records.size()+1;
        records.push_back(observed);
    }
    void __cdecl ClassicPoseTestRecord(void*,const AnalysisDiagnosticRecordV1* record)
    {
        if(!record || record->structSize!=sizeof(*record))return;
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        const bool captureSubtitleAdmission=classicPoseCaptureSubtitleAdmissions.load(std::memory_order_relaxed) &&
            !strcmp(record->stage,"subtitle-canvas-admission");
        if(!classicPoseAllRecordStages && strcmp(record->stage,"submitted-presentation") && strcmp(record->stage,"presentation") && strcmp(record->stage,"ar-decision") &&
            strcmp(record->stage,"subtitle-output") && !captureSubtitleAdmission)return;
        RecordSubmittedShaderPreparation(classicSubmittedShaderPreparation,*record,classicCurrentShaderPreparation);
        StoreCallbackOrderedRecord(classicPoseRecords,*record);
    }
    bool TrySubmittedSubtitleCleanup(const std::string& line,const AnalysisDiagnosticRecordV1& output,
        uint64_t sourceGeneration,SubtitleBoxRect& cleanup)
    {
        const bool layout=line.find("SUBTITLE LAYOUT:")!=std::string::npos;
        const bool capture=line.find("SUBTITLE CAPTURE:")!=std::string::npos;
        if(!layout&&!capture)return false;
        const auto frameAt=line.find(" frame=");
        const auto cleanupAt=line.find(layout?" accepted_cleanup=":" cleanup=");
        if(frameAt==std::string::npos||cleanupAt==std::string::npos)return false;
        const auto source=uint64_t(std::strtoull(line.c_str()+frameAt+7,nullptr,10));
        if(!source||strcmp(output.stage,"submitted-presentation") ||
            (output.flags&3u)!=3u || (output.flags&16384u)!=0 || (output.flags&64u)==0 ||
            output.transportGeneration!=sourceGeneration || output.rendererGeneration!=sourceGeneration ||
            output.acceptedSequence!=source || output.counters[0]!=source || output.counters[1]!=source ||
            output.counters[2]!=source || output.counters[5]!=source)return false;
        SubtitleBoxRect actual;
        const auto* coordinates=line.c_str()+cleanupAt+(layout?18:9);
        if(sscanf_s(coordinates,"%d,%d-%d,%d",&actual.left,&actual.top,&actual.right,&actual.bottom)!=4 ||
            !actual.Valid() || actual.left<0 || actual.top<0)return false;
        cleanup=actual;return true;
    }
    // OSD publication can precede the success-only final record. Consume the last
    // completed selected output in vector order, never an in-flight raw attempt.
    void UseCompletedSubmittedPresentation(DetectedPictureAspect& observed)
    {
        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
        for(auto it=classicPoseRecords.rbegin();it!=classicPoseRecords.rend();++it) {
            if(strcmp(it->stage,"submitted-presentation") || !(it->flags&1u) ||
                it->transportGeneration!=observed.sourceGeneration || it->acceptedSequence>observed.presentedSequence)continue;
            observed.presentedSequence=it->acceptedSequence;
            const double w=it->metrics[10]-it->metrics[8],h=it->metrics[11]-it->metrics[9];
            observed.inUseAvailable=w>0 && h>0;observed.inUseRatio=observed.inUseAvailable?w/h:0;
            observed.inUseState=observed.inUseAvailable?PictureAspectUse::PRESENTATION:PictureAspectUse::UNAVAILABLE;
            return;
        }
        observed.presentedSequence=0;observed.inUseAvailable=false;
    }
    bool IsExactCompletedOutput(const AnalysisDiagnosticRecordV1& record,uint64_t generation,unsigned mode)
    {
        return !strcmp(record.stage,"submitted-presentation") && (record.flags&1u)!=0 &&
            record.transportGeneration==generation && record.rendererGeneration==generation &&
            record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence && record.counters[3]==mode &&
            record.counters[4]>0 && (record.flags&1024u)==0 && std::abs(record.metrics[2])<0.01 &&
            (record.flags&32u)!=0 && (record.flags&64u)!=0 && record.counters[5]==record.acceptedSequence;
    }
    bool IsSourceBoundComposedCaption(const AnalysisDiagnosticRecordV1& current,
        const AnalysisDiagnosticRecordV1* previous,uint64_t generation,unsigned mode,double sourcePeriod)
    {
        if(!std::isfinite(sourcePeriod) || sourcePeriod<=0 || !std::isfinite(current.metrics[0]) ||
            !std::isfinite(current.metrics[5]) || !IsExactCompletedOutput(current,generation,mode) || (current.flags&2u)==0 ||
            current.counters[2]!=current.counters[1])return false;
        if(current.counters[0]==current.counters[1])return (current.flags&16384u)==0;
        if(!previous || (previous->flags&16384u)!=0 || !current.cadenceRepeat ||
            strcmp(current.reason,"complete-output-input-cadence-repeat") ||
            current.flags!=(previous->flags|16384u) || current.counters[0]!=current.counters[1]+1 ||
            current.acceptedSequence!=previous->acceptedSequence ||
            current.rendererInstanceKey!=previous->rendererInstanceKey ||
            current.transportGeneration!=previous->transportGeneration ||
            current.rendererGeneration!=previous->rendererGeneration ||
            current.viewportGeneration!=previous->viewportGeneration ||
            current.sourceFormatGeneration!=previous->sourceFormatGeneration ||
            current.analysisDomainKey!=previous->analysisDomainKey ||
            current.policyGeneration!=previous->policyGeneration || current.continuityGeneration!=previous->continuityGeneration ||
            current.sourceFrameNumber!=previous->sourceFrameNumber || current.captureTimestamp!=previous->captureTimestamp ||
            current.metrics[0]!=previous->metrics[0] || current.metrics[5]<=current.metrics[0] ||
            current.metrics[5]-current.metrics[0]>1.5*sourcePeriod ||
            !IsSourceBoundComposedCaption(*previous,nullptr,generation,mode,sourcePeriod))return false;
        for(int i=1;i<8;++i)if(current.counters[i]!=previous->counters[i])return false;
        for(int i=0;i<4;++i)if(current.published[i]!=previous->published[i] ||
            current.candidate[i]!=previous->candidate[i] || current.displayed[i]!=previous->displayed[i])return false;
        for(int i=8;i<16;++i)if(current.metrics[i]!=previous->metrics[i])return false;
        return true;
    }
    bool HasCompletePersistentThreeLineCaption(const std::vector<AnalysisDiagnosticRecordV1>& records,
        const AnalysisDiagnosticRecordV1& output,const AnalysisDiagnosticRecordV1& input)
    {
        if((output.flags&3u)!=3u || input.acceptedSequence!=output.counters[1] ||
            input.transportGeneration!=output.transportGeneration || !output.recordSequence)return false;
        uint64_t before=output.recordSequence;
        if(output.flags&16384u) {
            const AnalysisDiagnosticRecordV1* original=nullptr;
            for(const auto& r:records)if(!strcmp(r.stage,"submitted-presentation") &&
                r.rendererInstanceKey==output.rendererInstanceKey && r.transportGeneration==output.transportGeneration &&
                r.acceptedSequence==output.counters[1] && (r.flags&3u)==3u && !(r.flags&16384u) &&
                r.recordSequence<output.recordSequence && (!original || r.recordSequence>original->recordSequence))original=&r;
            if(!original)return false;
            before=original->recordSequence;
        }
        // RenderLocked uses these selected input timeline locals throughout;
        // submitted-presentation leaves policy/continuity unpopulated.
        const AnalysisDiagnosticRecordV1* proof=nullptr;
        for(const auto& r:records)if(!strcmp(r.stage,"subtitle-output") && r.recordSequence && r.recordSequence<before &&
            r.rendererInstanceKey==output.rendererInstanceKey && r.acceptedSequence==input.acceptedSequence &&
            r.transportGeneration==input.transportGeneration && r.rendererGeneration==input.rendererGeneration &&
            r.viewportGeneration==input.viewportGeneration && r.sourceFormatGeneration==input.sourceFormatGeneration &&
            r.analysisDomainKey==input.analysisDomainKey && r.sourceFrameNumber==input.sourceFrameNumber &&
            r.captureTimestamp==input.captureTimestamp && r.policyGeneration==input.policyGeneration &&
            r.continuityGeneration==input.continuityGeneration && r.counters[0]==input.acceptedSequence &&
            r.counters[1]==input.analysisDomainKey && (!proof || r.recordSequence>proof->recordSequence))proof=&r;
        return proof && proof->resultCode==3 && proof->counters[4]==3 &&
            (proof->flags&(1u|4u|16u))==(1u|4u|16u) && !(proof->flags&32u);
    }
    void CopyUnreadCallbackTail(const std::vector<AnalysisDiagnosticRecordV1>& source,size_t& cursor,
        std::vector<AnalysisDiagnosticRecordV1>& tail)
    {
        if(cursor>source.size())throw std::runtime_error("Callback history reset during an active cadence fixture");
        tail.assign(source.begin()+cursor,source.end());cursor=source.size();
    }
    class PersistentCaptionProofIndex {
        using Key=std::tuple<uint64_t,uint64_t,uint64_t>;
        std::map<Key,std::vector<AnalysisDiagnosticRecordV1>> sources;
    public:
        void Observe(const AnalysisDiagnosticRecordV1& record) {
            if(!strcmp(record.stage,"subtitle-output") || !strcmp(record.stage,"submitted-presentation"))
                sources[{record.rendererInstanceKey,record.transportGeneration,record.acceptedSequence}].push_back(record);
        }
        const std::vector<AnalysisDiagnosticRecordV1>& For(const AnalysisDiagnosticRecordV1& output) const {
            const auto found=sources.find({output.rendererInstanceKey,output.transportGeneration,output.counters[1]});
            static const std::vector<AnalysisDiagnosticRecordV1> empty;
            return found==sources.end()?empty:found->second;
        }
    };
    bool SourceSpatialProductionEnabled() {
        return SourcePictureDetectorEnabled();
    }
    bool IsMeasuredSpatialNativeSubmission(const AnalysisDiagnosticRecordV1& record) {
        return (record.flags&1u) && record.counters[4]!=0 && (record.flags&32u) && (record.flags&64u) &&
            ((record.flags>>24)&3u)==unsigned(ActivePictureDetectorKind::SourceSpatialMeasured);
    }

    struct ScopedCadenceDiagnostics {
        std::string previous;bool existed=false,enabled=false;
        explicit ScopedCadenceDiagnostics(bool activate):enabled(activate) {
            if(!enabled)return;
            char* value=nullptr;size_t length=0;
            if(_dupenv_s(&value,&length,"VP_CADENCE_CACHE_DIAGNOSTICS")!=0)throw std::runtime_error("Cannot read cadence diagnostic environment");
            existed=value!=nullptr;if(value){previous=value;free(value);}
            if(!SetEnvironmentVariableA("VP_CADENCE_CACHE_DIAGNOSTICS","1"))throw std::runtime_error("Cannot enable cadence diagnostic environment");
        }
        ~ScopedCadenceDiagnostics(){if(enabled)SetEnvironmentVariableA("VP_CADENCE_CACHE_DIAGNOSTICS",existed?previous.c_str():nullptr);}
    };
    void LogSubmittedFailureTail(const std::vector<AnalysisDiagnosticRecordV1>& records) {
        size_t first=records.size(),remaining=48;
        for(size_t i=records.size();i>0;--i)if(!strcmp(records[i-1].stage,"submitted-presentation")&&(records[i-1].flags&1u)) {
            first=i-1;if(--remaining==0)break;
        }
        for(size_t i=first;i<records.size();++i) {
            const auto& output=records[i];
            if(strcmp(output.stage,"submitted-presentation")||!(output.flags&1u))continue;
            std::ostringstream line;line<<"UHD FINAL: source="<<output.acceptedSequence<<" generation="<<output.transportGeneration
                <<" consumed="<<output.counters[0]<<" selected="<<output.counters[1]<<" cue="<<output.counters[2]
                <<" mode="<<output.counters[3]<<" native="<<output.counters[4]<<" cert="<<output.counters[5]
                <<" cert_origin="<<output.counters[6]<<" flags="<<output.flags<<" repeat="<<output.cadenceRepeat
                <<" domain="<<output.analysisDomainKey<<" viewport="<<output.viewportGeneration<<" reason="<<output.reason;
            for(int e=0;e<4;++e)line<<" native"<<e<<'='<<output.published[e]<<" cert"<<e<<'='<<output.candidate[e]<<" actual"<<e<<'='<<output.metrics[8+e];
            for(const auto& raw:records)if(raw.rendererInstanceKey==output.rendererInstanceKey&&raw.transportGeneration==output.transportGeneration&&
                raw.acceptedSequence==output.counters[0]&&!strcmp(raw.stage,"subtitle-output"))
                line<<" raw_flags="<<raw.flags<<" raw_reason="<<raw.reason<<" accepted_lines="<<raw.resultCode
                    <<" raw_lines="<<raw.counters[4]<<" decision="<<raw.detail;
            Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(line.str().c_str());
        }
    }
    struct NativeP210Buffer : IUnknown
    {
        std::atomic<int> references{0};
        HRESULT QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
        ULONG AddRef() override { ++references; return 1; }
        ULONG Release() override { --references; return 0; }
    };
    struct SnapshotTestCallback : IRendererCallback
    {
        std::atomic<RendererState> state{RENDERSTATE_UNKNOWN};
        void OnRendererState(RendererState value, uint32_t) override { state = value; }
        void OnRendererDetailString(const CString&, uint32_t) override {}
    };
}



using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace VideoProcessorTest
{
	TEST_CLASS(LibplaceboPluginProxyTests)
	{
	public:
        TEST_METHOD(ComposedCaptionRequiresFreshSourceOrOneIdenticalCompletedImageRepeat) {
            AnalysisDiagnosticRecordV1 original{};strcpy_s(original.stage,"submitted-presentation");
            original.flags=1u|2u|32u|64u;original.transportGeneration=original.rendererGeneration=7;
            original.acceptedSequence=50;original.counters[0]=original.counters[1]=original.counters[2]=original.counters[5]=50;
            original.counters[3]=3;original.counters[4]=4;original.metrics[0]=original.metrics[5]=1.0;
            original.policyGeneration=11;original.continuityGeneration=13;original.sourceFrameNumber=50;original.captureTimestamp=1000;
            Assert::IsTrue(IsSourceBoundComposedCaption(original,nullptr,7,3,1.0/60));
            auto repeat=original;repeat.flags|=16384u;repeat.cadenceRepeat=true;
            strcpy_s(repeat.reason,"complete-output-input-cadence-repeat");repeat.counters[0]=51;repeat.metrics[5]=1.0+1.0/60;
            Assert::IsTrue(IsSourceBoundComposedCaption(repeat,&original,7,3,1.0/60));
            for(int fault=0;fault<14;++fault) {
                auto invalid=repeat;auto prior=original;
                if(fault==0)invalid.flags&=~2u;
                if(fault==1)++invalid.counters[2];
                if(fault==2)++invalid.counters[0];
                if(fault==3)++invalid.viewportGeneration;
                if(fault==4)++invalid.sourceFormatGeneration;
                if(fault==5)++invalid.analysisDomainKey;
                if(fault==6)invalid.metrics[9]=1;
                if(fault==7)invalid.metrics[5]=1.1;
                if(fault==8)prior.flags|=16384u;
                if(fault==9)invalid.metrics[5]=std::numeric_limits<double>::quiet_NaN();
                if(fault==10)++invalid.policyGeneration;
                if(fault==11)++invalid.continuityGeneration;
                if(fault==12)++invalid.sourceFrameNumber;
                if(fault==13)++invalid.captureTimestamp;
                Assert::IsFalse(IsSourceBoundComposedCaption(invalid,&prior,7,3,1.0/60));
            }
        }
        TEST_METHOD(PersistentComposedCaptionRequiresAllThreeOriginalSourceLines) {
            AnalysisDiagnosticRecordV1 input{};input.acceptedSequence=50;input.transportGeneration=input.rendererGeneration=7;
            input.sourceFormatGeneration=13;input.analysisDomainKey=17;input.sourceFrameNumber=150;
            input.captureTimestamp=9000;input.viewportGeneration=2;input.policyGeneration=11;input.continuityGeneration=12;
            auto output=input;strcpy_s(output.stage,"submitted-presentation");output.rendererInstanceKey=19;
            output.flags=1u|2u|32u|64u;output.recordSequence=100;
            output.counters[0]=output.counters[1]=output.counters[2]=output.counters[5]=50;
            output.counters[3]=3;output.counters[4]=4;
            // Production submitted-presentation does not populate source frame,
            // capture timestamp or timeline generations. The exact selected
            // analysis-input supplies that immutable source context.
            output.sourceFrameNumber=output.captureTimestamp=output.policyGeneration=output.continuityGeneration=0;
            auto raw=input;strcpy_s(raw.stage,"subtitle-output");raw.rendererInstanceKey=19;raw.recordSequence=99;
            raw.policyGeneration=11;raw.continuityGeneration=12;raw.counters[0]=50;raw.counters[1]=17;
            raw.flags=1u|4u|16u;raw.resultCode=3;raw.counters[4]=3;
            Assert::IsTrue(HasCompletePersistentThreeLineCaption({raw},output,input));
            for(int fault=0;fault<16;++fault) {
                auto invalid=raw;
                if(fault==0)invalid.resultCode=2;
                if(fault==1)invalid.counters[4]=2;
                if(fault==2)++invalid.acceptedSequence;
                if(fault==3)invalid.recordSequence=101;
                if(fault==4)++invalid.sourceFormatGeneration;
                if(fault==5)++invalid.analysisDomainKey;
                if(fault==6)++invalid.sourceFrameNumber;
                if(fault==7)++invalid.captureTimestamp;
                if(fault==8)++invalid.viewportGeneration;
                if(fault==9)++invalid.policyGeneration;
                if(fault==10)++invalid.continuityGeneration;
                if(fault==11)++invalid.rendererInstanceKey;
                if(fault==12)++invalid.transportGeneration;
                if(fault==13)invalid.flags&=~16u;
                if(fault==14)invalid.flags&=~4u;
                if(fault==15)invalid.flags|=32u;
                Assert::IsTrue(IsSourceBoundComposedCaption(output,nullptr,7,3,1.0/60),
                    L"The previous composed-only check admits a partial or unrelated line proof.");
                Assert::IsFalse(HasCompletePersistentThreeLineCaption({invalid},output,input));
            }
            auto partial=raw;partial.recordSequence=99;partial.resultCode=2;
            auto earlier=raw;earlier.recordSequence=98;
            Assert::IsFalse(HasCompletePersistentThreeLineCaption({earlier,partial},output,input),
                L"An earlier good attempt cannot hide the latest partial attempt.");
            auto future=partial;future.recordSequence=102;
            Assert::IsTrue(HasCompletePersistentThreeLineCaption({raw,future},output,input));
            auto repeat=output;repeat.recordSequence=110;repeat.flags|=16384u;repeat.cadenceRepeat=true;
            repeat.counters[0]=51;strcpy_s(repeat.reason,"complete-output-input-cadence-repeat");
            Assert::IsTrue(HasCompletePersistentThreeLineCaption({raw,output,future},repeat,input),
                L"A repeat uses its original selected image proof before the original submission, never a later consumed-input attempt.");
            // Production Emit supplies zero recorder sequences to its callback.
            // Exercise the same ordered storage used by the callback, retaining
            // original source identity and the repeat's original submission cut.
            std::vector<AnalysisDiagnosticRecordV1> captured;
            auto zeroRaw=raw;zeroRaw.recordSequence=0;
            auto zeroOutput=output;zeroOutput.recordSequence=0;
            StoreCallbackOrderedRecord(captured,zeroRaw);
            StoreCallbackOrderedRecord(captured,zeroOutput);
            const auto capturedOutput=captured.back();
            Assert::IsTrue(HasCompletePersistentThreeLineCaption(captured,capturedOutput,input));
            auto zeroFuture=partial;zeroFuture.recordSequence=0;
            StoreCallbackOrderedRecord(captured,zeroFuture);
            auto zeroRepeat=repeat;zeroRepeat.recordSequence=0;
            StoreCallbackOrderedRecord(captured,zeroRepeat);
            Assert::IsTrue(HasCompletePersistentThreeLineCaption(captured,captured.back(),input));
            Assert::AreEqual(uint64_t(0),zeroRaw.recordSequence);
            Assert::AreEqual(input.sourceFrameNumber,captured.front().sourceFrameNumber);
            Assert::AreEqual(input.captureTimestamp,captured.front().captureTimestamp);
            Assert::AreEqual(input.sourceFormatGeneration,captured.front().sourceFormatGeneration);
            Assert::AreEqual(input.analysisDomainKey,captured.front().analysisDomainKey);
            Assert::AreEqual(uint64_t(1),captured.front().recordSequence);
            Assert::AreEqual(uint64_t(4),captured.back().recordSequence);
            auto consumed=input;consumed.acceptedSequence=51;
            Assert::IsFalse(HasCompletePersistentThreeLineCaption({raw,output},repeat,consumed));
        }
        TEST_METHOD(IncrementalCaptionProofIndexPreservesOriginalOrdinalsAndRepeatCutoff) {
            std::vector<AnalysisDiagnosticRecordV1> all,tail;size_t cursor=0;PersistentCaptionProofIndex index;
            AnalysisDiagnosticRecordV1 input{};input.acceptedSequence=50;input.transportGeneration=input.rendererGeneration=7;
            input.sourceFormatGeneration=13;input.analysisDomainKey=17;input.sourceFrameNumber=150;
            input.captureTimestamp=9000;input.viewportGeneration=2;input.policyGeneration=11;input.continuityGeneration=12;
            auto noise=input;strcpy_s(noise.stage,"worker-read");
            for(int i=0;i<1000;++i)StoreCallbackOrderedRecord(all,noise);
            CopyUnreadCallbackTail(all,cursor,tail);Assert::AreEqual(size_t(1000),tail.size());
            for(const auto& record:tail)index.Observe(record);
            auto raw=input;strcpy_s(raw.stage,"subtitle-output");raw.rendererInstanceKey=19;
            raw.counters[0]=50;raw.counters[1]=17;raw.flags=1u|4u|16u;raw.resultCode=3;raw.counters[4]=3;
            auto output=input;strcpy_s(output.stage,"submitted-presentation");output.rendererInstanceKey=19;
            output.flags=3;output.counters[0]=output.counters[1]=50;
            StoreCallbackOrderedRecord(all,raw);StoreCallbackOrderedRecord(all,output);const auto original=all.back();
            CopyUnreadCallbackTail(all,cursor,tail);Assert::AreEqual(size_t(2),tail.size());
            Assert::AreEqual(uint64_t(1001),tail.front().recordSequence);
            for(const auto& record:tail)index.Observe(record);
            Assert::IsTrue(HasCompletePersistentThreeLineCaption(index.For(original),original,input));
            auto partial=raw;partial.resultCode=2;StoreCallbackOrderedRecord(all,partial);
            auto repeat=output;repeat.flags|=16384u;repeat.counters[0]=51;StoreCallbackOrderedRecord(all,repeat);const auto repeated=all.back();
            StoreCallbackOrderedRecord(all,output);const auto laterFresh=all.back();
            auto foreign=raw;foreign.rendererInstanceKey=20;StoreCallbackOrderedRecord(all,foreign);
            foreign=raw;++foreign.transportGeneration;StoreCallbackOrderedRecord(all,foreign);
            CopyUnreadCallbackTail(all,cursor,tail);Assert::AreEqual(size_t(5),tail.size());
            for(const auto& record:tail)index.Observe(record);
            Assert::AreEqual(size_t(5),index.For(original).size(),L"Only this renderer/generation/source's caption proofs and submissions enter its join.");
            Assert::IsTrue(HasCompletePersistentThreeLineCaption(index.For(repeated),repeated,input),L"A repeat uses its original successful output cutoff, not a later partial attempt.");
            Assert::IsFalse(HasCompletePersistentThreeLineCaption(index.For(laterFresh),laterFresh,input),L"A fresh later attempt must see the latest partial proof.");
            Assert::AreEqual(HasCompletePersistentThreeLineCaption(all,repeated,input),HasCompletePersistentThreeLineCaption(index.For(repeated),repeated,input));
            Assert::AreEqual(HasCompletePersistentThreeLineCaption(all,laterFresh,input),HasCompletePersistentThreeLineCaption(index.For(laterFresh),laterFresh,input));
            CopyUnreadCallbackTail(all,cursor,tail);Assert::IsTrue(tail.empty());
            Assert::AreEqual(uint64_t(1001),index.For(original).front().recordSequence,L"Tail copies retain absolute callback ordinals without rebasing or duplicate delivery.");
        }
        TEST_METHOD(ActualCleanupCoverageRequiresAnExactSuccessfullyComposedSource) {
            AnalysisDiagnosticRecordV1 output{};strcpy_s(output.stage,"submitted-presentation");
            output.flags=1u|2u|64u;output.transportGeneration=output.rendererGeneration=7;
            output.acceptedSequence=55;output.counters[0]=output.counters[1]=output.counters[2]=output.counters[5]=55;
            const std::string layout="SUBTITLE LAYOUT: frame=55 cue=1 measured_cleanup=1,2-3,4 accepted_cleanup=1548,1718-2292,1898 display_edges=0,262-3840,1898";
            SubtitleBoxRect cleanup;
            Assert::IsTrue(TrySubmittedSubtitleCleanup(layout,output,7,cleanup));
            Assert::AreEqual(1548,cleanup.left);Assert::AreEqual(1718,cleanup.top);
            Assert::AreEqual(2292,cleanup.right);Assert::AreEqual(1898,cleanup.bottom);
            Assert::IsTrue(TrySubmittedSubtitleCleanup("SUBTITLE CAPTURE: frame=55 cue=1 cleanup=1548,1718-2292,1898",output,7,cleanup));
            for(int fault=0;fault<8;++fault) {
                auto invalid=output;
                if(fault==0)invalid.flags&=~2u;
                if(fault==1)invalid.flags&=~1u;
                if(fault==2)invalid.flags&=~64u;
                if(fault==3)++invalid.transportGeneration;
                if(fault==4)++invalid.counters[1];
                if(fault==5)++invalid.counters[2];
                if(fault==6)++invalid.counters[5];
                if(fault==7)invalid.flags|=16384u;
                Assert::IsFalse(TrySubmittedSubtitleCleanup(layout,invalid,7,cleanup));
            }
            Assert::IsFalse(TrySubmittedSubtitleCleanup("SUBTITLE LAYOUT: frame=56 accepted_cleanup=1548,1718-2292,1898",output,7,cleanup));
            Assert::IsFalse(TrySubmittedSubtitleCleanup("SUBTITLE LAYOUT: frame=55 measured_cleanup=1548,1718-2292,1898",output,7,cleanup));
            Assert::IsFalse(TrySubmittedSubtitleCleanup("SUBTITLE LAYOUT: frame=55 accepted_cleanup=10,20-5,15",output,7,cleanup));
        }
        TEST_METHOD(ReusedRendererAddressCannotReuseAnotherFixtureShaderPreparation) {
            ClassicPreparationHistory history;ClassicShaderPreparationSelection selected;
            selected.state="ready";selected.key=99;selected.generation=7;selected.format=13;
            selected.requested=3;selected.effective=3;
            AnalysisDiagnosticRecordV1 output{};strcpy_s(output.stage,"submitted-presentation");
            output.flags=1u;output.rendererInstanceKey=19;
            output.transportGeneration=output.rendererGeneration=7;
            output.acceptedSequence=output.counters[0]=output.counters[1]=6;
            output.sourceFormatGeneration=13;
            Assert::IsTrue(RecordSubmittedShaderPreparation(history,output,selected));
            // Allocators may reuse the renderer address; source generations and
            // sequence numbers also restart in a new fixture or cadence phase.
            auto pending=selected;pending.state="pending";pending.key=101;
            pending.format=17;pending.effective=0;output.sourceFormatGeneration=17;
            Assert::IsTrue(RecordSubmittedShaderPreparation(history,output,pending));
            const ClassicSubmittedPreparationKey key{19,7,6};
            Assert::IsTrue(history.at(key).AllowsInitialBuildFallback(7,17,3),
                L"A fresh output at a reused renderer address must not inherit an old fixture's Ready state or source format.");
            ResetShaderPreparationHistory(history,selected);
            Assert::IsTrue(history.empty());Assert::IsTrue(selected.state.empty());
            Assert::AreEqual(uint64_t(0),selected.key);Assert::AreEqual(uint64_t(0),selected.format);
            Assert::IsFalse(selected.AllowsInitialBuildFallback(7,17,3));
        }
        TEST_METHOD(RepeatedWarmupImageKeepsItsOriginalShaderPreparationProvenance) {
            ClassicPreparationHistory history;
            ClassicShaderPreparationSelection pending;
            pending.state="pending";pending.key=99;pending.generation=7;pending.format=13;
            pending.requested=2;pending.effective=0;
            AnalysisDiagnosticRecordV1 original{};strcpy_s(original.stage,"submitted-presentation");
            original.flags=1u|32u|64u;original.rendererInstanceKey=19;
            original.transportGeneration=original.rendererGeneration=7;
            original.acceptedSequence=original.counters[0]=original.counters[1]=50;
            original.sourceFormatGeneration=13;
            Assert::IsTrue(RecordSubmittedShaderPreparation(history,original,pending));
            auto ready=pending;ready.state="ready";ready.effective=2;
            auto repeat=original;repeat.flags|=16384u;repeat.cadenceRepeat=true;
            strcpy_s(repeat.reason,"complete-output-input-cadence-repeat");repeat.counters[0]=51;
            Assert::IsTrue(RecordSubmittedShaderPreparation(history,repeat,ready));
            const ClassicSubmittedPreparationKey key{19,7,50};
            Assert::IsTrue(history.at(key).AllowsInitialBuildFallback(7,13,2),
                L"A repeated image must retain the original Pending shader provenance, not the current input's Ready state.");
            ClassicPreparationHistory absent;
            Assert::IsFalse(RecordSubmittedShaderPreparation(absent,repeat,ready));
            Assert::IsTrue(absent.empty(),L"A missing original cannot acquire fabricated warmup provenance.");
            for(int fault=0;fault<10;++fault) {
                auto invalid=repeat;auto seeded=history;
                if(fault==0)invalid.flags&=~16384u;
                if(fault==1)invalid.cadenceRepeat=false;
                if(fault==2)strcpy_s(invalid.reason,"not-a-complete-output-repeat");
                if(fault==3)++invalid.counters[0];
                if(fault==4)++invalid.acceptedSequence;
                if(fault==5)++invalid.sourceFormatGeneration;
                if(fault==6)++invalid.rendererGeneration;
                if(fault==7)++seeded.at(key).generation;
                if(fault==8)++seeded.at(key).format;
                if(fault==9)invalid.flags&=~1u;
                const auto before=seeded.at(key);
                Assert::IsFalse(RecordSubmittedShaderPreparation(seeded,invalid,ready));
                Assert::AreEqual(size_t(1),seeded.size());
                Assert::IsTrue(seeded.at(key).state==before.state&&seeded.at(key).key==before.key&&
                    seeded.at(key).generation==before.generation&&seeded.at(key).format==before.format&&
                    seeded.at(key).requested==before.requested&&seeded.at(key).effective==before.effective,
                    L"An invalid repeat must not replace even an existing original preparation record.");
            }
            for(int fault=0;fault<2;++fault) {
                auto foreign=repeat;auto seeded=history;
                if(fault==0)++foreign.rendererInstanceKey;
                if(fault==1)++foreign.transportGeneration;
                Assert::IsFalse(RecordSubmittedShaderPreparation(seeded,foreign,ready));
                Assert::AreEqual(size_t(1),seeded.size());
                Assert::IsTrue(seeded.at(key).AllowsInitialBuildFallback(7,13,2));
            }            auto fresh=original;fresh.acceptedSequence=fresh.counters[0]=fresh.counters[1]=52;
            Assert::IsTrue(RecordSubmittedShaderPreparation(history,fresh,ready));
            const ClassicSubmittedPreparationKey freshKey{19,7,52};
            Assert::IsFalse(history.at(freshKey).AllowsInitialBuildFallback(7,13,2));
        }
        TEST_METHOD(ShaderBuildingCaptionExceptionRequiresExactInitialPipelineKey) {
            ClassicShaderPreparationSelection prepared;
            prepared.state="pending";prepared.key=99;prepared.generation=7;prepared.format=13;
            prepared.requested=3;prepared.effective=0;
            Assert::IsTrue(prepared.AllowsInitialBuildFallback(7,13,3));
            prepared.state="missing";Assert::IsTrue(prepared.AllowsInitialBuildFallback(7,13,3));
            for(int fault=0;fault<8;++fault) {
                auto invalid=prepared;
                if(fault==0)invalid.state="ready";
                if(fault==1)invalid.state="failed";
                if(fault==2)invalid.state="";
                if(fault==3)invalid.key=0;
                if(fault==4)++invalid.generation;
                if(fault==5)++invalid.format;
                if(fault==6)++invalid.requested;
                if(fault==7)invalid.effective=3;
                Assert::IsFalse(invalid.AllowsInitialBuildFallback(7,13,3));
            }
        }
        TEST_METHOD(ShaderCacheOverridePreservesDefaultsSeparatesVariantsAndRejectsRelativePaths) {
            const std::string executable="C:\\Program Files\\TestPlatform\\testhost.exe";
            const std::string relative="vprenderer\\VideoProcessorShaderCache.bin";
            Assert::IsTrue(ShaderCacheStorage::Resolve("",executable,relative,false)==
                "C:\\Program Files\\TestPlatform\\"+relative);
            Assert::IsTrue(ShaderCacheStorage::Resolve("","",relative,false)==relative);
            const auto standard=ShaderCacheStorage::Resolve("E:\\cache\\vp",executable,relative,false);
            const auto beta=ShaderCacheStorage::Resolve("E:\\cache\\vp",executable,relative,true);
            Assert::IsTrue(standard=="E:\\cache\\vp\\VideoProcessorShaderCache-standard.bin");
            Assert::IsTrue(beta=="E:\\cache\\vp\\VideoProcessorShaderCache-beta.bin");
            Assert::IsTrue(ShaderCacheStorage::Resolve("E:\\cache\\vp",executable,relative,true,true)==
                "E:\\cache\\vp\\VideoProcessorShaderCache-beta.clear");
            for(const std::string value:{"relative","C:relative","\\rooted-without-drive"})
                Assert::ExpectException<std::invalid_argument>([&] {ShaderCacheStorage::Resolve(value,executable,relative,false);});
            Assert::IsTrue(ShaderCacheStorage::TemporaryPath(standard,10,1)!=ShaderCacheStorage::TemporaryPath(standard,11,1));
            Assert::IsTrue(ShaderCacheStorage::TemporaryPath(standard,10,1)!=ShaderCacheStorage::TemporaryPath(standard,10,2));
        }

        TEST_METHOD(BetaUsesSeparatePluginAndRuntimeNames)
        {
            using namespace OptionalRendererLayout;
            Assert::IsTrue(PluginPath(L"C:\\VP", true) == L"C:\\VP\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll");
            Assert::IsTrue(PluginPath(L"C:\\VP") == L"C:\\VP\\vprenderer\\VideoProcessorVPRenderer.dll");
            Assert::AreEqual(L"libplacebo-388.dll", PrivateDependencies(true)[3]);
            Assert::AreEqual(L"libplacebo-360.dll", PrivateDependencies()[3]);
        }
        TEST_METHOD(SpatialCanvasEstimateNeverClaimsMeasuredBoundsOrChangesSubmittedAspect)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Submitted(7,1,2.35,16.0/9.0,true,true);
            const auto used=telemetry.Read().inUseRatio;
            telemetry.ObservePhysical(7,2,true,false,PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE,
                0,0,3840,2160,ActivePictureDetectorKind::SourceSpatialCanvas);
            const auto reading=telemetry.Read();
            Assert::IsFalse(reading.available);
            Assert::IsFalse(reading.physicalBoundsAvailable);
            Assert::AreEqual(0.0,reading.ratio);
            Assert::AreEqual(used,reading.inUseRatio);
            Assert::AreEqual(int(PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE),int(reading.physicalConfidence));
            Assert::IsTrue(AspectRatioOsd::Format(reading).detected==
                L"Canvas estimate (blank margins unresolved; source spatial)");
            telemetry.Submitted(7,2,16.0/9.0,16.0/9.0,true,false);
            Assert::AreEqual(used,telemetry.Read().inUseRatio,
                L"Neither source inference nor failed submission can change the actually used aspect");
        }

        TEST_METHOD(SpatialMeasuredAndUnavailableReadingsExposeTheirRealProducingMode)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3,10,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0,263,3840,1897,ActivePictureDetectorKind::SourceSpatialMeasured);
            const auto measured=telemetry.Read();
            Assert::IsTrue(measured.available);
            Assert::AreEqual(3840.0/1634.0,measured.ratio);
            const auto label=AspectRatioOsd::Format(measured).retained;
            Assert::IsTrue(label.find(L"0,263..3840,1897")!=std::wstring::npos);
            Assert::IsTrue(label.find(L"source spatial")!=std::wstring::npos);
            telemetry.ObservePhysical(3,11,false,false,PhysicalAspectConfidence::UNKNOWN,
                0,0,0,0,ActivePictureDetectorKind::SourceSpatialUnavailable);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsTrue(telemetry.Read().stableDetection.available && telemetry.Read().stableDetection.held);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==label);
            telemetry.ObservePhysical(3,12,true,false,PhysicalAspectConfidence::UNKNOWN,
                0,0,0,0,ActivePictureDetectorKind::SourceSpatialUnavailable);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsTrue(telemetry.Read().stableDetection.available && telemetry.Read().stableDetection.held);
            Assert::AreEqual(uint64_t(10),telemetry.Read().stableDetection.acceptedSequence);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==label);
            Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
        }

        TEST_METHOD(LegacyPhysicalObservationKeepsExistingLabelAndSourceResetClearsSpatialProvenance)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3,1,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0,263,3840,1897);
            Assert::AreEqual(int(ActivePictureDetectorKind::Legacy),int(telemetry.Read().detectorKind));
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).detected.find(L"experimental")==std::wstring::npos);
            telemetry.ObservePhysical(3,2,true,false,PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE,
                0,0,3840,2160,ActivePictureDetectorKind::SourceSpatialCanvas);
            telemetry.Source(4);
            Assert::IsFalse(telemetry.Read().physicalEvaluated);
            Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
            Assert::AreEqual(int(ActivePictureDetectorKind::Legacy),int(telemetry.Read().detectorKind));
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).detected==L"Unavailable");
        }

		TEST_METHOD(AspectOsdMapsEveryNamedFormat)
		{
			struct Expected { double value; const wchar_t* ratio; const wchar_t* name; };
			const Expected expected[] = {
				{ 4.0 / 3.0, L"4:3", L"TV" },
				{ 1.37, L"1.37:1", L"Academy" },
				{ 1.43, L"1.43:1", L"IMAX 70mm" },
				{ 1.66, L"1.66:1", L"European widescreen" },
				{ 16.0 / 9.0, L"16:9", L"HDTV" },
				{ 1.85, L"1.85:1", L"Flat" },
				{ 1.90, L"1.90:1", L"Digital IMAX" },
				{ 2.00, L"2.00:1", L"Univisium" },
				{ 2.20, L"2.20:1", L"70mm / Todd-AO" },
				{ 21.0 / 9.0, L"21:9", L"Consumer ultrawide" },
				{ 2.35, L"2.35:1", L"CinemaScope" },
				{ 2.39, L"2.39:1", L"Scope" },
				{ 2.40, L"2.40:1", L"Scope" },
				{ 2.76, L"2.76:1", L"Ultra Panavision 70" }
			};
			for (const auto& item : expected)
			{
				const auto label = AspectRatioOsd::Classify(item.value);
				Assert::IsTrue(label.ratio == item.ratio);
				Assert::IsTrue(label.name == item.name);
			}
			Assert::IsTrue(AspectRatioOsd::Classify(1.50).name == L"nonstandard");
			Assert::IsTrue(AspectRatioOsd::Classify(1.503).ratio == L"1.50:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.50).ratio == L"2.50:1");
			Assert::IsTrue(AspectRatioOsd::Classify(0.0).ratio == L"---");
		}

		TEST_METHOD(AspectNamesClassifyWithoutChangingMeasurements)
		{
			const double isolatedTargets[] = {
				4.0 / 3.0, 1.37, 1.43, 1.66, 16.0 / 9.0,
				1.85, 1.90, 2.00, 2.20, 2.76
			};
			for (double target : isolatedTargets)
			{
				const auto atTarget = AspectRatioOsd::Classify(target);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 0.99).ratio == atTarget.ratio);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 1.01).ratio == atTarget.ratio);
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 0.9899).name == L"nonstandard");
				Assert::IsTrue(AspectRatioOsd::Classify(
					target * 1.0101).name == L"nonstandard");
			}
			const auto atBoundary = AspectRatioOsd::Classify(2.76 * 1.01);
			Assert::IsTrue(atBoundary.ratio == L"2.76:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.76 * 1.01001).name == L"nonstandard");
			Assert::IsTrue(AspectRatioOsd::Classify(2.39).ratio == L"2.39:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.40).ratio == L"2.40:1");
			Assert::IsTrue(AspectRatioOsd::Classify(
				2.0 * 2.39 * 2.40 / (2.39 + 2.40)).ratio == L"2.39:1");
			Assert::IsTrue(AspectRatioOsd::Classify(2.345).ratio == L"2.35:1");
		}

        TEST_METHOD(CaptionEnvelopeCannotRelabelPhysicalAspect2_24)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(3, 34938, 3840.0 / 1634.0, true, false);
            telemetry.Observe(3, 34939, 3840.0 / 1714.0, true, true);
            const auto reading = telemetry.Read();
            Assert::IsTrue(!reading.available ||
                std::abs(reading.ratio - 3840.0 / 1634.0) < 0.000001,
                L"A provisional caption envelope cannot become measured physical aspect.");
        }

        TEST_METHOD(AspectDisplayRoundsNativeMeasurementWithoutChangingIt)
        {
            AspectRatioOsd osd;
            DetectedPictureAspect r;
            r.sourceGeneration = 1; r.available = true;
            r.ratio = 3840.0 / 1650.0;
            const auto first = osd.Update(r).detected;
            r.ratio = 3840.0 / 1636.0;
            const auto second = osd.Update(r).detected;
            Assert::IsTrue(first.find(L"2.33") != std::wstring::npos);
            Assert::IsTrue(second.find(L"2.35") != std::wstring::npos);
            Assert::IsTrue(first != second);
        }

        TEST_METHOD(AspectDisplayRoundingKeepsPresentationLabels)
        {
            AspectRatioOsd osd;
            DetectedPictureAspect r;
            r.sourceGeneration=1; r.available=true; r.ratio=1.50;
            Assert::IsTrue(osd.Update(r).detected == L"1.50:1");
            r.ratio=1.509;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1");
            r.ratio=1.514;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1");
            r.inUseAvailable=true; r.inUseState=PictureAspectUse::DETECTED; r.inUseRatio=2.39;
            Assert::IsTrue(osd.Update(r).inUse == L"2.39:1 (Scope)");
        }
        TEST_METHOD(DetectedAspectDeadbandRetainsAnchorAcross60HzJitterAndUnknowns)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Submitted(3,1,2.39,16.0/9.0,true,true);
            telemetry.ObservePhysical(3,1,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0,263,3840,1897,ActivePictureDetectorKind::SourceSpatialMeasured);
            const auto anchor=AspectRatioOsd::Format(telemetry.Read()).retained;
            const double measuredAnchor=3840.0/1634.0;
            Assert::AreEqual(measuredAnchor,telemetry.Read().ratio);
            Assert::IsTrue(telemetry.Read().stableDetection.available && !telemetry.Read().stableDetection.held);
            Assert::AreEqual(uint64_t(1),telemetry.Read().stableDetection.acceptedSequence);
            Assert::AreEqual(uint64_t(3),telemetry.Read().stableDetection.sourceGeneration);
            // Two seconds of distinct 60 Hz source observations. Telemetry has
            // no frame-count dwell and must not erase a good detection on a gap.
            for(uint64_t sequence=2;sequence<=121;++sequence) {
                if(sequence%5==0) {
                    telemetry.ObservePhysical(3,sequence,true,false,PhysicalAspectConfidence::UNKNOWN,
                        0,0,0,0,ActivePictureDetectorKind::SourceSpatialUnavailable);
                    Assert::IsFalse(telemetry.Read().available);
                    Assert::AreEqual(0.0,telemetry.Read().ratio);
                    Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
                } else if(sequence%5==1) {
                    telemetry.Observe(3,sequence,1.90,true,true);
                    Assert::IsTrue(telemetry.Read().provisional);
                    Assert::IsFalse(telemetry.Read().available);
                } else {
                    const int top=260+int(sequence%7),bottom=1895+int(sequence%5);
                    telemetry.ObservePhysical(3,sequence,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                        0,top,3840,bottom,ActivePictureDetectorKind::SourceSpatialMeasured);
                    Assert::IsTrue(telemetry.Read().available);
                    Assert::AreEqual(3840.0/(bottom-top),telemetry.Read().ratio);
                    Assert::AreEqual(top,telemetry.Read().physicalBounds[1]);
                    Assert::AreEqual(bottom,telemetry.Read().physicalBounds[3]);
                }
                Assert::AreEqual(sequence,telemetry.Read().detectedSequence);
                const auto& stable=telemetry.Read().stableDetection;
                Assert::IsTrue(stable.available && stable.held && stable.physicalBoundsAvailable);
                Assert::AreEqual(measuredAnchor,stable.ratio);
                Assert::AreEqual(uint64_t(1),stable.acceptedSequence);
                Assert::AreEqual(uint64_t(3),stable.sourceGeneration);
                Assert::AreEqual(263,stable.physicalBounds[1]);Assert::AreEqual(1897,stable.physicalBounds[3]);
                Assert::AreEqual(int(PhysicalAspectConfidence::STRONG_ABRUPT_EDGE),int(stable.confidence));
                Assert::AreEqual(int(ActivePictureDetectorKind::SourceSpatialMeasured),int(stable.detectorKind));
                Assert::AreEqual(sequence-1,telemetry.Read().publicationGeneration);
                Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor,
                    L"Small current measurement variation and missing evidence must retain the accepted detection value.");
                const double actualPresented=sequence%2?1.90:2.39;
                telemetry.Submitted(3,sequence,actualPresented,16.0/9.0,true,true);
                Assert::AreEqual(actualPresented,telemetry.Read().inUseRatio,
                    L"Stable detection is not permission to freeze or alter actually submitted geometry.");
                Assert::AreEqual(sequence,telemetry.Read().presentedSequence);
                Assert::AreEqual(sequence,telemetry.Read().publicationGeneration);
                Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            }
        }
        TEST_METHOD(DetectedAspectDeadbandIsAnchoredAndLargeChangesAreImmediate)
        {
            PictureAspectTelemetry telemetry;uint64_t sequence=1;
            telemetry.Observe(7,sequence++,2.35,true,false);
            const auto anchor=AspectRatioOsd::Format(telemetry.Read()).retained;
            // Successive close measurements cannot walk the anchor. The total
            // distance is always measured from the last accepted detection.
            for(double ratio:{2.36,2.38,2.40,2.42,2.35*1.03}) {
                telemetry.Observe(7,sequence++,ratio,true,false);
                Assert::AreEqual(ratio,telemetry.Read().ratio);
                Assert::AreEqual(2.35,telemetry.Read().stableDetection.ratio);
                Assert::AreEqual(uint64_t(1),telemetry.Read().stableDetection.acceptedSequence);
                Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor,
                    L"The inclusive three-percent band is anchored; per-step drift cannot move it.");
            }
            PictureAspectTelemetry lower;
            lower.Observe(7,1,2.35,true,false);
            lower.Observe(7,2,2.35*0.97,true,false);
            Assert::AreEqual(2.35,lower.Read().stableDetection.ratio);
            Assert::AreEqual(uint64_t(1),lower.Read().stableDetection.acceptedSequence);
            lower.Observe(7,3,2.279,true,false);
            Assert::AreEqual(2.279,lower.Read().ratio);
            Assert::AreEqual(2.279,lower.Read().stableDetection.ratio);
            Assert::AreEqual(uint64_t(3),lower.Read().stableDetection.acceptedSequence);
            Assert::IsFalse(lower.Read().stableDetection.held);
            const auto outsideSequence=sequence;
            telemetry.Observe(7,sequence++,2.421,true,false);
            Assert::AreEqual(2.421,telemetry.Read().stableDetection.ratio);
            Assert::AreEqual(outsideSequence,telemetry.Read().stableDetection.acceptedSequence);
            Assert::IsFalse(telemetry.Read().stableDetection.held);
            auto changed=AspectRatioOsd::Format(telemetry.Read()).retained;
            Assert::IsTrue(changed.find(L"2.42:1")==0 && changed!=anchor,
                L"The first valid measurement outside three percent updates immediately.");
            telemetry.Observe(7,sequence++,1.90,true,false);
            const auto imax=AspectRatioOsd::Format(telemetry.Read()).retained;
            Assert::IsTrue(imax.find(L"1.90:1")==0);
            const auto imaxSequence=telemetry.Read().stableDetection.acceptedSequence;
            Assert::AreEqual(sequence-1,imaxSequence);
            Assert::AreEqual(1.90,telemetry.Read().stableDetection.ratio);
            telemetry.Observe(7,sequence++,1.92,true,false);
            Assert::AreEqual(1.92,telemetry.Read().ratio);
            Assert::AreEqual(1.90,telemetry.Read().stableDetection.ratio);
            Assert::AreEqual(imaxSequence,telemetry.Read().stableDetection.acceptedSequence);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==imax);
            telemetry.Observe(7,sequence++,2.35,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained.find(L"2.35:1")==0,
                L"A real IMAX-to-scope transition has no additional confirmation delay.");
        }
        TEST_METHOD(DetectedAspectStableHistoryRejectsStaleInvalidAndCanvasThenResets)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Submitted(8,99,2.39,16.0/9.0,true,true);
            telemetry.Observe(8,100,2.35,true,false);
            const auto anchor=AspectRatioOsd::Format(telemetry.Read()).retained;
            uint64_t sequence=101;
            for(double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
                telemetry.Observe(8,sequence++,invalid,true,false);
                Assert::IsFalse(telemetry.Read().available);
                Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            }
            telemetry.ObservePhysical(8,105,true,false,PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE,
                0,0,3840,2160,ActivePictureDetectorKind::SourceSpatialCanvas);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            // Even an unknown newer source advances ordering; a late older
            // measurement cannot masquerade as a new aspect transition.
            telemetry.Observe(8,104,1.90,true,false);
            Assert::AreEqual(0.0,telemetry.Read().ratio);
            Assert::AreEqual(uint64_t(105),telemetry.Read().detectedSequence);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            // First qualified completion of source105 is inside the band.
            // That consumes its qualification slot without walking the anchor.
            telemetry.Observe(8,105,2.34,true,false);
            Assert::AreEqual(2.34,telemetry.Read().ratio);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            telemetry.Observe(8,105,1.90,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==anchor);
            Assert::IsTrue(telemetry.Read().stableDetection.available && telemetry.Read().stableDetection.held);
            Assert::AreEqual(2.35,telemetry.Read().stableDetection.ratio);
            Assert::AreEqual(uint64_t(100),telemetry.Read().stableDetection.acceptedSequence);
            Assert::AreEqual(uint64_t(8),telemetry.Read().stableDetection.sourceGeneration);
            Assert::AreEqual(2.39,telemetry.Read().inUseRatio);
            Assert::AreEqual(uint64_t(99),telemetry.Read().presentedSequence);
            telemetry.ClearDetection();
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().stableDetection.available);
            Assert::AreEqual(0.0,telemetry.Read().stableDetection.ratio);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==L"Unavailable");
            Assert::AreEqual(2.39,telemetry.Read().inUseRatio);
            telemetry.Observe(8,1,1.90,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained.find(L"1.90:1")==0,
                L"Explicit ClearDetection resets the source ordering anchor as well as the stable value.");
            telemetry.Source(9);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().stableDetection.available);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==L"Unavailable");
            telemetry.ObservePhysical(9,1,true,false,PhysicalAspectConfidence::SOURCE_CANVAS_ESTIMATE,
                0,0,3840,2160,ActivePictureDetectorKind::SourceSpatialCanvas);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained.find(L"1.90:1")==std::wstring::npos);
            Assert::IsFalse(telemetry.Read().stableDetection.available);
            telemetry.Observe(9,2,2.0,true,true);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().stableDetection.available);
            telemetry.Observe(9,3,2.0,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained.find(L"2.00:1")==0);
            Assert::AreEqual(uint64_t(3),telemetry.Read().stableDetection.acceptedSequence);
            Assert::AreEqual(uint64_t(9),telemetry.Read().stableDetection.sourceGeneration);
            Assert::IsFalse(telemetry.Read().stableDetection.held);
        }
        TEST_METHOD(DetectedAspectSameSourceCompletionAcquiresWithoutWaitingForNextFrame)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3,1,false,false,PhysicalAspectConfidence::UNKNOWN,
                0,0,0,0,ActivePictureDetectorKind::SourceSpatialUnavailable);
            Assert::IsFalse(telemetry.Read().stableDetection.available);
            // Runtime publishes the initial Unknown before the exact same
            // source's async packet becomes ready. Completion is not a new frame.
            telemetry.ObservePhysical(3,1,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0,263,3840,1897,ActivePictureDetectorKind::SourceSpatialMeasured);
            Assert::IsTrue(telemetry.Read().available);
            Assert::IsTrue(telemetry.Read().stableDetection.available,
                L"The first qualified completion of an already observed source must acquire immediately.");
            Assert::AreEqual(uint64_t(1),telemetry.Read().stableDetection.acceptedSequence);
            Assert::AreEqual(3840.0/1634.0,telemetry.Read().stableDetection.ratio);
            const auto scope=AspectRatioOsd::Format(telemetry.Read()).retained;
            telemetry.ObservePhysical(3,1,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0,70,3840,2090,ActivePictureDetectorKind::SourceSpatialMeasured);
            Assert::AreEqual(3840.0/1634.0,telemetry.Read().ratio,
                L"The second qualified interpretation cannot replace this source's current measurement either.");
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==scope,
                L"A second qualified interpretation of the same source cannot replace its accepted anchor.");
            telemetry.ObservePhysical(3,2,true,false,PhysicalAspectConfidence::UNKNOWN,
                0,0,0,0,ActivePictureDetectorKind::SourceSpatialUnavailable);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==scope);
            telemetry.Observe(3,2,1.90,true,false);
            Assert::AreEqual(1.90,telemetry.Read().ratio);
            Assert::AreEqual(1.90,telemetry.Read().stableDetection.ratio);
            Assert::AreEqual(uint64_t(2),telemetry.Read().stableDetection.acceptedSequence);
            const auto imax=AspectRatioOsd::Format(telemetry.Read()).retained;
            telemetry.Observe(3,2,0.0,false,false);
            telemetry.Observe(3,2,2.35,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==imax,
                L"Unknown after qualification cannot reopen that source's already consumed completion slot.");
            telemetry.Observe(3,1,2.35,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==imax);
            telemetry.Observe(3,3,1.91,true,false);
            Assert::AreEqual(1.91,telemetry.Read().ratio);
            Assert::AreEqual(uint64_t(2),telemetry.Read().stableDetection.acceptedSequence);
            telemetry.Observe(3,3,2.35,true,false);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==imax,
                L"The first qualified in-band completion still consumes its source slot, even though the anchor is retained.");
            Assert::AreEqual(uint64_t(2),telemetry.Read().stableDetection.acceptedSequence);
        }
        TEST_METHOD(AspectCurrentMeasurementRejectsLateAndDuplicatePublications)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3,100,false,false,PhysicalAspectConfidence::UNKNOWN,0,0,0,0);
            telemetry.ObservePhysical(3,100,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,0,263,3840,1897);
            const auto measured=telemetry.Read();
            telemetry.ObservePhysical(3,100,false,false,PhysicalAspectConfidence::UNKNOWN,0,0,0,0);
            Assert::IsTrue(telemetry.Read().available,L"An incomplete duplicate cannot erase this exact source's completed measurement.");
            telemetry.ObservePhysical(3,99,true,false,PhysicalAspectConfidence::UNKNOWN,0,0,0,0);
            Assert::AreEqual(uint64_t(100),telemetry.Read().detectedSequence,L"Late source results cannot roll current reporting backward.");
            telemetry.ObservePhysical(3,100,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,0,0,3840,2160);
            Assert::AreEqual(measured.ratio,telemetry.Read().ratio,L"One immutable source has one accepted qualified interpretation.");
            telemetry.Observe(3,99,16.0/9.0,true,false);
            Assert::AreEqual(measured.ratio,telemetry.Read().ratio);
            telemetry.ObservePhysical(3,101,true,false,PhysicalAspectConfidence::UNKNOWN,0,0,0,0);
            Assert::IsFalse(telemetry.Read().available,L"A newer evaluated inconclusive source must remain Unknown, not inherit a fresh measurement.");
            Assert::AreEqual(uint64_t(101),telemetry.Read().detectedSequence);
            Assert::IsTrue(telemetry.Read().stableDetection.available && telemetry.Read().stableDetection.held);
            telemetry.ObservePhysical(3,100,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,0,263,3840,1897);
            Assert::IsFalse(telemetry.Read().available);
            Assert::AreEqual(uint64_t(101),telemetry.Read().detectedSequence);
            telemetry.ObservePhysical(3,101,true,true,PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,0,263,3840,1897);
            Assert::IsTrue(telemetry.Read().available,L"The first qualified completion of the current source still publishes immediately.");
            telemetry.Source(4);
            telemetry.ObservePhysical(4,1,true,true,PhysicalAspectConfidence::SOURCE_RASTER_EXTENT,0,0,3840,2160);
            Assert::AreEqual(uint64_t(1),telemetry.Read().detectedSequence);
        }
        TEST_METHOD(AspectPhysicalUnknownCannotReusePreviousCurrentMeasurement)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3, 100, true, true,
                PhysicalAspectConfidence::STRONG_ABRUPT_EDGE, 0, 263, 3840, 1897);
            Assert::IsTrue(telemetry.Read().available);
            const auto acceptedLabel=AspectRatioOsd::Format(telemetry.Read()).retained;
            Assert::IsTrue(acceptedLabel.find(L"0,263..3840,1897") != std::wstring::npos);
            telemetry.ObservePhysical(3, 101, true, true,
                PhysicalAspectConfidence::UNKNOWN, 0, 262, 3840, 1976);
            Assert::IsFalse(telemetry.Read().available);
            Assert::AreEqual(0.0, telemetry.Read().ratio);
            Assert::AreEqual(uint64_t(101), telemetry.Read().detectedSequence);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==acceptedLabel);
            Assert::IsTrue(telemetry.Read().stableDetection.available && telemetry.Read().stableDetection.held);
            Assert::AreEqual(uint64_t(100),telemetry.Read().stableDetection.acceptedSequence);
            telemetry.Source(4);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
            Assert::IsFalse(telemetry.Read().physicalEvaluated);
        }
        TEST_METHOD(AspectOsdSeparatesCurrentMeasurementRetainedEstimateAndAppliedAspect)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(3, 10, true, true, PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0, 7, 3840, 1969);
            telemetry.Submitted(3, 10, 16.0/9.0, 16.0/9.0, false, true);
            const auto accepted = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(accepted.detected.find(L"Current: 1.96:1") == 0);
            Assert::IsTrue(accepted.detected.find(L"legacy") != std::wstring::npos);
            Assert::IsTrue(accepted.detected.find(L"retained: 1.96:1") != std::wstring::npos);
            Assert::IsTrue(accepted.inUse.find(L"16:9") == 0);
            telemetry.ObservePhysical(3, 11, true, false, PhysicalAspectConfidence::UNKNOWN,
                0, 0, 0, 0);
            const auto unknown = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(unknown.detected.find(L"Current: Unknown") == 0);
            Assert::IsTrue(unknown.detected.find(L"retained: 1.96:1") != std::wstring::npos);
            Assert::IsTrue(unknown.retained == accepted.retained);
            Assert::IsTrue(unknown.inUse == accepted.inUse);
            // A new current producer cannot inherit the retained producer label.
            telemetry.ObservePhysical(3, 12, false, false, PhysicalAspectConfidence::UNKNOWN,
                0, 0, 0, 0, ActivePictureDetectorKind::SourceSpatialUnavailable);
            const auto pending = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(pending.detected.find(L"Current: Pending (source spatial)") == 0);
            Assert::IsTrue(pending.retained == accepted.retained);
            telemetry.ObservePhysical(3, 13, true, true, PhysicalAspectConfidence::STRONG_ABRUPT_EDGE,
                0, 6, 3840, 1970, ActivePictureDetectorKind::SourceSpatialMeasured);
            const auto varied = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(varied.detected.find(L"0,6..3840,1970") != std::wstring::npos);
            Assert::IsTrue(varied.retained == accepted.retained);
            Assert::IsTrue(varied.inUse == accepted.inUse);
        }

        TEST_METHOD(AspectPhysicalFullRasterRequiresExplicitCurrentExtentProof)
        {
            PictureAspectTelemetry telemetry;
            telemetry.ObservePhysical(1, 10, true, true, PhysicalAspectConfidence::SOURCE_RASTER_EXTENT,
                0, 0, 3840, 2160);
            Assert::IsTrue(telemetry.Read().available);
            const auto acceptedLabel=AspectRatioOsd::Format(telemetry.Read()).retained;
            Assert::IsTrue(acceptedLabel.find(L"1.78:1 [0,0..3840,2160; stable]")==0);
            Assert::AreEqual(int(PhysicalAspectConfidence::SOURCE_RASTER_EXTENT),int(telemetry.Read().stableDetection.confidence));
            telemetry.ObservePhysical(1, 11, true, false, PhysicalAspectConfidence::SOURCE_RASTER_EXTENT,
                0, 0, 3840, 2160);
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsFalse(telemetry.Read().physicalBoundsAvailable);
            Assert::AreEqual(0.0,telemetry.Read().ratio);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained==acceptedLabel);
            Assert::AreEqual(uint64_t(10),telemetry.Read().stableDetection.acceptedSequence);
        }
        TEST_METHOD(AspectDisplayResetsForUnavailableNewSourceAndRenderer)
        {
            AspectRatioOsd osd;
            DetectedPictureAspect r; r.sourceGeneration=1; r.available=true; r.ratio=1.50;
            osd.Update(r); r.available=false;
            Assert::IsTrue(osd.Update(r).detected == L"Unavailable");
            r.available=true; r.ratio=1.509;
            Assert::IsTrue(osd.Update(r).detected == L"1.51:1");
            r.sourceGeneration=2; r.ratio=1.519;
            Assert::IsTrue(osd.Update(r).detected == L"1.52:1");
            osd.Reset(); r.ratio=1.529;
            Assert::IsTrue(osd.Update(r).detected == L"1.53:1");
        }

        TEST_METHOD(AspectTelemetryUsesNativeStartupWithoutClaimingDetection)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Submitted(1, 1, 3840.0/2160.0, 3840.0/2160.0, false, true);
            const auto rows = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(rows.inUse == L"16:9 (HDTV)");
            Assert::IsTrue(rows.detected == L"Unavailable");
            telemetry.Observe(1, 2, 16.0/9.0, true, false);
            telemetry.Submitted(1, 2, 16.0/9.0, 16.0/9.0, false, true);
            Assert::IsTrue(telemetry.Read().inUseState == PictureAspectUse::DETECTED);
        }
        TEST_METHOD(AspectTelemetryKeepsSubmittedScopeWhileMenuCandidateIsLive)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 10, 2.39, true, false);
            telemetry.Submitted(1, 10, 2.39, 16.0/9.0, true, true);
            telemetry.Observe(1, 11, 1.66, true, true);
            telemetry.Submitted(1, 11, 2.39, 16.0/9.0, true, true);
            const auto rows = AspectRatioOsd::Format(telemetry.Read());
            Assert::IsTrue(rows.inUse == L"2.39:1 (Scope; held)");
            Assert::IsFalse(telemetry.Read().available);
            Assert::IsTrue(telemetry.Read().provisional);
            Assert::AreEqual(2.39,telemetry.Read().stableDetection.ratio);
            Assert::IsTrue(rows.retained.find(L"2.39:1")==0 && rows.retained.find(L"stable")!=std::wstring::npos);
            telemetry.ClearDetection();
            telemetry.Submitted(1, 12, 2.39, 16.0/9.0, true, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained == L"Unavailable");
            Assert::AreEqual(2.39, telemetry.Read().inUseRatio);
        }
        TEST_METHOD(AspectTelemetryDoesNotHideARealSubmittedAspectChange)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 1, 2.39, true, false);
            telemetry.Submitted(1, 1, 2.39, 16.0/9.0, true, true);
            telemetry.Observe(1, 2, 1.66, true, false);
            telemetry.Submitted(1, 2, 1.66, 16.0/9.0, true, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).inUse == L"1.66:1 (European widescreen)");
            telemetry.Observe(1, 3, 2.17, true, false);
            Assert::AreEqual(2.17,telemetry.Read().ratio);
            Assert::AreEqual(2.17,telemetry.Read().stableDetection.ratio);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).retained.find(L"2.17:1")==0);
        }
        TEST_METHOD(AspectTelemetryPreservesPresentedPacketAndRejectsFailedSubmission)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 20, 2.39, true, false);
            telemetry.Submitted(1, 19, 2.17, 16.0/9.0, true, true);
            Assert::AreEqual(uint64_t(19), telemetry.Read().presentedSequence);
            Assert::AreEqual(uint64_t(20), telemetry.Read().detectedSequence);
            Assert::AreEqual(2.17, telemetry.Read().inUseRatio);
            telemetry.Submitted(1, 21, 1.66, 16.0/9.0, true, false);
            Assert::AreEqual(2.17, telemetry.Read().inUseRatio);
            telemetry.ClearPresentation();
            Assert::IsFalse(telemetry.Read().inUseAvailable);
            Assert::IsTrue(telemetry.Read().available);
        }
        TEST_METHOD(AspectTelemetryResetsAcrossRealSourceChangeAndUsesNonWideRaster)
        {
            PictureAspectTelemetry telemetry;
            telemetry.Observe(1, 1, 2.39, true, false);
            telemetry.Submitted(1, 1, 2.39, 16.0/9.0, true, true);
            telemetry.Source(2);
            Assert::IsFalse(telemetry.Read().inUseAvailable || telemetry.Read().available);
            telemetry.Submitted(2, 1, 4.0/3.0, 4.0/3.0, false, true);
            Assert::IsTrue(AspectRatioOsd::Format(telemetry.Read()).inUse == L"4:3 (TV)");
            telemetry.Observe(2, 2, 0.0, true, false);
            Assert::IsFalse(telemetry.Read().available);
        }

		TEST_METHOD(RendererMetadataCyclesReuseAcceptedSnapshotWithoutDiskReads)
		{
			// Exercise the real renderer DLL and D3D initialization without capture hardware.
			HMODULE testModule = nullptr;
			Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SnapshotTestLog), &testModule) != FALSE);
			wchar_t modulePath[MAX_PATH] = {};
			GetModuleFileNameW(testModule, modulePath, MAX_PATH);
			const std::wstring fullPath(modulePath);
			const std::wstring root = fullPath.substr(0, fullPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            const std::wstring pluginPath = root + L"\\..\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll";
#else
			const std::wstring pluginPath = root + L"\\vprenderer\\VideoProcessorVPRenderer.dll";
#endif
			HMODULE plugin = LoadLibraryExW(pluginPath.c_str(), nullptr,
				LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			Assert::IsNotNull(plugin, L"Build the Release renderer DLL before this integration test");
			const auto version = reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_VERSION_EXPORT));
			const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
			const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
				GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
			Assert::IsTrue(version && create && destroy);
			Assert::AreEqual(VP_LIBPLACEBO_PLUGIN_API_VERSION, version());
			char temp[MAX_PATH] = {};
			GetTempPathA(MAX_PATH, temp);
			const std::string path = std::string(temp) + "vp0188-renderer-synthetic.cfg";
			const std::string initial = "[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
				"[vprenderer]\nquality: high\nprofile_update_mode: live\n"
				"[vprenderer.fast]\nshortcut: F4\nquality: fast\n";
			std::ofstream(path) << initial;
			snapshotTestLog.open(root + L"\\vp0188-renderer-integration.log", std::ios::trunc);
			snapshotTestReads = 0;
			SnapshotTestCallback callback;
			struct Cleanup
			{
				HMODULE plugin;
				HWND window;
				IVideoRenderer* renderer = nullptr;
				VideoProcessorLibplaceboDestroyRendererFn destroy;
				~Cleanup()
				{
					if (renderer) { renderer->Stop(); destroy(renderer); }
					if (window) DestroyWindow(window);
					FreeLibrary(plugin);
					std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
					snapshotTestLog.close();
				}
			} cleanup{plugin, CreateWindowExW(0, L"STATIC", L"VP-0188 renderer test",
				WS_POPUP, 0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr), nullptr, destroy};
			Assert::IsNotNull(cleanup.window);
			cleanup.renderer = create(&callback, 188, cleanup.window, nullptr, false, 1,
				VideoConversionOverride::VIDEOCONVERSION_NONE, path.c_str(), SnapshotTestLog);
			Assert::IsNotNull(cleanup.renderer);
			ConfigFile config;
			Assert::IsTrue(config.Load(path));
			UnifiedProfileRuntime::Runtime runtime;
			std::string error;
			Assert::IsTrue(runtime.Initialize(config, {}, error), std::wstring(error.begin(), error.end()).c_str());
			CString active;
			bool restart = false, reset = false;
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(), active, restart, reset));
			VideoStateComPtr state = new VideoState();
			state->valid = true;
			state->displayMode = std::make_shared<DisplayMode>(320, 180, false, 60000, 1001);
			state->videoFrameEncoding = VideoFrameEncoding::BGRA_8BIT;
			state->eotf = EOTF::PQ;
			state->colorspace = ColorSpace::BT_2020;
			state->hdrData = std::make_shared<HDRData>();
			state->hdrData->masteringDisplayMaxLuminance = 1000;
			state->hdrData->maxCll = 1000;
			state->hdrData->maxFall = 400;
			Assert::IsTrue(cleanup.renderer->OnVideoState(state));
			cleanup.renderer->Build();
			cleanup.renderer->Start();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (callback.state != RENDERSTATE_RENDERING && callback.state != RENDERSTATE_FAILED &&
				std::chrono::steady_clock::now() < deadline)
			{
				MSG message;
				while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{ TranslateMessage(&message); DispatchMessageW(&message); }
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			Assert::IsTrue(callback.state == RENDERSTATE_RENDERING, L"Renderer did not initialize; see vp0188-renderer-integration.log");
			const unsigned reads = snapshotTestReads.load();
			// Disk now disagrees with the accepted snapshot. Source updates must ignore it.
			std::ofstream(path) << "[general]\nswitch_refresh_rate: never\n[vprenderer]\nquality: balanced\n";
			for (int cycle = 0; cycle < 10; ++cycle)
			{
				VideoStateComPtr missing = new VideoState(*state);
				missing->hdrData.reset();
				Assert::IsTrue(cleanup.renderer->OnVideoState(missing));
				Assert::IsTrue(cleanup.renderer->OnVideoState(state));
			}
			Assert::AreEqual(reads, snapshotTestReads.load(), L"HDR cycles reread the config file");
			UnifiedProfileRuntime::SelectionResult selection;
			Assert::IsTrue(runtime.SelectKey("F4", {}, selection, error));
			Assert::IsTrue(selection.changed);
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*selection.snapshot, active, restart, reset));
			Assert::AreEqual(reads, snapshotTestReads.load(), L"Profile selection reread the config file");
			Assert::IsTrue(config.Load(path));
			UnifiedProfileRuntime::RefreshResult reload;
			Assert::IsTrue(runtime.Reload(config, {}, reload, error));
			Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*reload.snapshot, active, restart, reset));
			Assert::AreEqual(reads, snapshotTestReads.load(), L"Accepted host reload was reread by renderer");
			Logger::WriteMessage("VP-0188: real renderer initialized; 10 HDR withdrawal/restoration cycles, profile F4, and explicit reload passed with zero renderer config disk reads.\n");
			DeleteFileA(path.c_str());
		}

        TEST_METHOD(RealRendererUploadsNativeP210AndP010ForSdrAndHdrWithoutConversion)
        { VerifyNativeCaptureAspect(false); }

        TEST_METHOD(BetaRendererPublishesSubmittedAspectForNativeCapture)
        { VerifyNativeCaptureAspect(true); }

        TEST_METHOD(StandardRendererVerboseTelemetryMatchesSubmittedNativeOutput)
        { VerifyNativeCaptureAspect(false,true); }
        TEST_METHOD(BetaRendererVerboseTelemetryMatchesSubmittedNativeOutput)
        { VerifyNativeCaptureAspect(true,true); }

        void VerifyNativeCaptureAspect(bool beta,bool telemetry=false)
        {
            HMODULE module = nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SnapshotTestLog), &module) != FALSE);
            wchar_t modulePath[MAX_PATH] = {};
            GetModuleFileNameW(module, modulePath, MAX_PATH);
            std::wstring path(modulePath); path.resize(path.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            path += L"\\..";
#endif
            path += beta ? L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll" :
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct PluginCleanup { HMODULE plugin; ~PluginCleanup(){ FreeLibrary(plugin); } } pluginCleanup{plugin};
            const auto create = reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy = reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
                GetProcAddress(plugin, VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create && destroy);
            const auto configureLogging=reinterpret_cast<VideoProcessorLibplaceboConfigureLoggingFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_LOGGING_EXPORT));
            Assert::IsTrue(configureLogging!=nullptr);
            configureLogging(telemetry);
            struct ResetLogging {VideoProcessorLibplaceboConfigureLoggingFn fn;~ResetLogging(){fn(false);}} resetLogging{configureLogging};
            {std::lock_guard<std::mutex> guard(snapshotTestLogMutex);decisionTestMessages.clear();}
            VideoStateComPtr state = new VideoState();
            for (const auto nativeEncoding : {VideoFrameEncoding::P210, VideoFrameEncoding::P010})
            {
            state->valid = true; state->videoFrameEncoding = nativeEncoding;
            state->displayMode = std::make_shared<DisplayMode>(130, 130, false, 60000, 1001);
            std::vector<uint16_t> pixels(state->BytesPerFrame()/2);
            for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint16_t((64+(i%877)) << 6);
            for (const auto eotf : {EOTF::SDR, EOTF::PQ})
            {
                state->eotf = eotf; state->colorspace = eotf == EOTF::PQ ? ColorSpace::BT_2020 : ColorSpace::REC_709;
                char temporary[MAX_PATH] = {}, configPath[MAX_PATH] = {};
                Assert::IsTrue(GetTempPathA(MAX_PATH, temporary) != 0);
                Assert::IsTrue(GetTempFileNameA(temporary, "p21", 0, configPath) != 0);
                std::ofstream(configPath) << "[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                    "[vprenderer]\nquality: fast\n";
                if(telemetry)std::ofstream(configPath,std::ios::app)<<"\n[logging]\nvideo_timecode: true\n";
                SnapshotTestCallback callback; NativeP210Buffer buffer;
                const unsigned initialized = nativeP210Initializations.load();
                {
                    struct Cleanup
                    {
                        HWND window; IVideoRenderer* renderer; VideoProcessorLibplaceboDestroyRendererFn destroy;
                        const char* config;
                        ~Cleanup(){ if(renderer){ renderer->Stop(); destroy(renderer); }
                            if(window) DestroyWindow(window); DeleteFileA(config); }
                    } cleanup{CreateWindowExW(0, L"STATIC", L"Native P210 fixture", WS_POPUP,
                        0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr), nullptr, destroy, configPath};
                    Assert::IsNotNull(cleanup.window);
                    // An explicit P010 consumer must wait for legacy v210 capture.
                    auto* forced = create(&callback, 1, cleanup.window, nullptr, false, 1,
                        VideoConversionOverride::VIDEOCONVERSION_V210_TO_P010, configPath, SnapshotTestLog);
                    Assert::IsNotNull(forced);
                    const bool accepted = forced->OnVideoState(state);
                    destroy(forced);
                    Assert::IsFalse(accepted);
                    cleanup.renderer = create(&callback, 2, cleanup.window, nullptr, false, 1,
                        VideoConversionOverride::VIDEOCONVERSION_NONE, configPath, SnapshotTestLog);
                    Assert::IsNotNull(cleanup.renderer);
                    Assert::IsTrue(cleanup.renderer->OnVideoState(state));
                    cleanup.renderer->Build(); cleanup.renderer->Start();
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                    uint64_t sequence = 1; bool presented = false;
                    while (std::chrono::steady_clock::now() < deadline && callback.state != RENDERSTATE_FAILED)
                    {
                        MSG message;
                        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                        { TranslateMessage(&message); DispatchMessageW(&message); }
                        if (callback.state == RENDERSTATE_RENDERING)
                        {
                            VideoFrame frame(pixels.data(), sequence++, 0, &buffer);
                            cleanup.renderer->OnVideoFrame(frame);
                            CString ingress;
                            presented = cleanup.renderer->HasPresentedLiveFrame() &&
                                cleanup.renderer->GetVideoIngressInfo(ingress) &&
                                ingress.Find(nativeEncoding==VideoFrameEncoding::P010 ? L"P010 (card capture; 10-bit 4:2:0; no CPU repack)" : L"P210 (native capture; no CPU repack)") >= 0;
                            if (presented) break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                    Assert::IsTrue(presented, L"Native P210 failed to upload and present");
                    DetectedPictureAspect aspect;
                    const auto aspectDeadline = std::chrono::steady_clock::now()+std::chrono::seconds(2);
                    do {
                        cleanup.renderer->GetDetectedPictureAspect(aspect);
                        if (aspect.inUseAvailable) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    } while (std::chrono::steady_clock::now()<aspectDeadline);
                    Assert::IsTrue(aspect.inUseAvailable, L"Actual renderer did not publish submitted AR");
                    Assert::AreEqual(1.0,aspect.inUseRatio,0.0001,L"Source is 130x130; target window aspect must not replace it");
                    Assert::IsTrue(aspect.presentedSequence>0);
                    if(telemetry) {
                        bool matched=false;
                        const auto telemetryDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                        do {
                            {std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
                             for(const auto& line:decisionTestMessages) {
                                if(line.find("\"submitted\":true")!=std::string::npos &&
                                   line.find("\"generation\":"+std::to_string(aspect.sourceGeneration)+",")!=std::string::npos &&
                                   line.find("\"selected\":"+std::to_string(aspect.presentedSequence)+",")!=std::string::npos &&
                                   line.find("\"facts_current\":true")!=std::string::npos &&
                                   line.find("\"actual_source\":[0,0,130,130]")!=std::string::npos &&
                                   line.find("\"scan_reason\":")!=std::string::npos &&
                                   line.find("\"timecode_drawn\":true")!=std::string::npos)matched=true;
                             }}
                            if(!matched)std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }while(!matched && std::chrono::steady_clock::now()<telemetryDeadline);
                        if(!matched){std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
                            for(const auto& line:decisionTestMessages)Logger::WriteMessage(line.c_str());}
                        Assert::IsTrue(matched,L"Verbose plugin telemetry must include current source facts and actual submitted bounds.");
                        {std::lock_guard<std::mutex> guard(snapshotTestLogMutex);decisionTestMessages.clear();}
                    } else {
                        std::lock_guard<std::mutex> guard(snapshotTestLogMutex);
                        Assert::IsTrue(decisionTestMessages.empty(),L"Detailed decision telemetry must be opt-in.");
                    }

                    Assert::AreEqual(initialized+(nativeEncoding==VideoFrameEncoding::P210?1u:0u), nativeP210Initializations.load());
                }
                Assert::AreEqual(0, buffer.references.load(), L"Renderer retained a capture buffer after shutdown");
            }
            }
        }

        TEST_METHOD(StandardRendererRetainsUncertainPictureAndAcceptsRealTransitions)
        { VerifyNativeAspectTransitionSequence(false); }
        TEST_METHOD(BetaRendererRetainsUncertainPictureAndAcceptsRealTransitions)
        { VerifyNativeAspectTransitionSequence(true); }

        TEST_METHOD(StandardRendererKeepsNativeAndConfiguredFillSeparate)
        { VerifyNativeAspectTransitionSequence(false,true); }
        TEST_METHOD(BetaRendererKeepsNativeAndConfiguredFillSeparate)
        { VerifyNativeAspectTransitionSequence(true,true); }

        TEST_METHOD(StandardRendererKeepsClassicTranslatedFilledPoseAcrossNaturalSkippedScans)
        { VerifyClassicTranslatedFilledPose(false); }
        TEST_METHOD(BetaRendererKeepsClassicTranslatedFilledPoseAcrossNaturalSkippedScans)
        { VerifyClassicTranslatedFilledPose(true); }

        void VerifyClassicTranslatedFilledPose(bool beta)
        {
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup { HMODULE value; ~ModuleCleanup(){FreeLibrary(value);} } moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseMessages.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);}
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0 && GetTempFileNameA(temp,"cpt",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.native]\nautomatic_crop: true\nsubtitle_fit: true\n"
                "crop_narrower_content_to_fill_screen: false\ncrop_wider_content_to_fill_screen: true\n"
                "crop_wider_content_aspect_limit: 2.42:1\n"
                "[vprenderer.subtitles.classic]\ntype: classic\noffset_pixels: 0\n"
                "subtitle_engage_drift_ms: 0\nsubtitle_release_drift_ms: 0\n"
                "subtitle_target_buffer_pixels: 0\nsubtitle_hold_seconds: 2\n";
            // Scaled native version of the logged 250-pixel same-size shift.
            // These exact pixels define both the native plane and caption extent.
            constexpr int width=960,height=540,bar=68,pictureBottom=height-bar;
            constexpr int captionTop=498,captionBottom=514;
            VideoStateComPtr state=new VideoState();state->valid=true;
            state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(state->BytesPerFrame()/2,uint16_t(512<<6));
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                native[size_t(y)*width+x]=uint16_t((y>=bar&&y<pictureBottom?300:64)<<6);
            auto caption=native;
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=342+glyph*22;x<356+glyph*22;++x)
                    if(x<345+glyph*22 || x>=353+glyph*22 || (y>=captionTop+6&&y<captionTop+9))
                        caption[size_t(y)*width+x]=uint16_t(700<<6);
            const ActivePictureBounds physical{0,bar,width,pictureBottom,width,height,double(width)/(pictureBottom-bar),
                ActivePictureBounds::BarAxes::TOP_BOTTOM};
            // The dense bar renderer includes max(8,height/90) native pixels and
            // the explicitly configured zero offset/reserve. Both extents are even.
            const int shift=captionBottom+(std::max)(8,height/90)-pictureBottom;
            Assert::AreEqual(50,shift);
            auto shifted=physical;shifted.top+=shift;shifted.bottom+=shift;
            AlphaSourceCrop::AspectLimitFillInput fillInput;fillInput.sourceBounds=shifted;
            fillInput.contentReferenceAvailable=true;fillInput.contentReferenceBounds=physical;
            fillInput.trustedContentAuthorityAccepted=true;fillInput.screenAspect=2.35;
            fillInput.cropWiderContentToFillScreen=fillInput.widerLimitConfigured=true;fillInput.widerAspectLimit=2.42;
            const auto filled=AlphaSourceCrop::EvaluateAspectLimitFill(fillInput);
            Assert::IsTrue(filled.applied && filled.sourceBounds.left>0 && filled.sourceBounds.right<width);
            const auto& expected=filled.sourceBounds;
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic translated filled pose fixture",WS_POPUP,
                0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,43,cleanup.window,nullptr,false,1,
                VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);Assert::IsTrue(cleanup.renderer->OnVideoState(state));
            cleanup.renderer->Build();cleanup.renderer->Start();uint64_t sequence=1;
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(callback.state==RENDERSTATE_RENDERING) {
                    VideoFrame frame(pixels.data(),sequence++,0,&buffer);cleanup.renderer->OnVideoFrame(frame);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(17));
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);return value;
            };
            auto messages=[] {
                std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;
            };
            auto source=[](const std::string& line,const char* name) {
                const auto at=line.find(name);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(name),nullptr,10));
            };
            auto bounds=[](const std::string& line,const char* name,double& left,double& top,double& right,double& bottom) {
                const auto at=line.find(name);return at!=std::string::npos &&
                    sscanf_s(line.c_str()+at+strlen(name),"%lf,%lf-%lf,%lf",&left,&top,&right,&bottom)==4;
            };
            auto equal=[&](const std::string& line,const char* name,const ActivePictureBounds& wanted) {
                double l=0,t=0,r=0,b=0;return bounds(line,name,l,t,r,b) &&
                    std::abs(l-wanted.left)<0.01 && std::abs(t-wanted.top)<0.01 &&
                    std::abs(r-wanted.right)<0.01 && std::abs(b-wanted.bottom)<0.01;
            };
            const auto acquireDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            bool nativeAcquired=false;
            do {
                const auto value=pump(native);
                // The accepted native pixel plane and optional exact-edge telemetry
                // have separate readiness. Never label missing physical evidence exact.
                if(value.available)Assert::IsTrue(!value.provisional&&std::abs(value.ratio-physical.aspectRatio)<0.005,
                    L"Available physical measurement disagrees with the source-defined edges.");
                auto initialFillInput=fillInput;initialFillInput.sourceBounds=physical;
                const auto initial=AlphaSourceCrop::EvaluateAspectLimitFill(initialFillInput).sourceBounds;
                for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos &&
                    source(line," generation=")==1 && source(line," selected=")>0 && source(line," native=")==1 &&
                    source(line," accepted=")>0 && source(line," format=")>0 && source(line," domain=")>0 &&
                    equal(line," native_rect=",physical) && line.find("reason=accepted-native-and-presentation-committed")!=std::string::npos) {
                    const auto selected=source(line," selected=");
                    for(const auto& output:messages())if(output.find("VP submitted geometry:")!=std::string::npos &&
                        source(output," generation=")==1 && source(output," source=")==selected &&
                        source(output," selected=")==selected && equal(output," actual_source=",initial))nativeAcquired=true;
                }
                if(nativeAcquired&&!value.available)Logger::WriteMessage(
                    "Native-plane acquisition verified from accepted source pixels and exact configured output; optional physical-edge telemetry remains UNKNOWN.\n");
            }while(!nativeAcquired&&std::chrono::steady_clock::now()<acquireDeadline&&callback.state!=RENDERSTATE_FAILED);
            if(!nativeAcquired)for(const auto& line:messages())Logger::WriteMessage(line.c_str());
            Assert::IsTrue(nativeAcquired,L"The real plugin must independently acquire the source-defined native bar plane before caption onset.");
            const uint64_t captionFirst=sequence;uint64_t committedPose=0,lastPresented=0;
            bool scheduled=false,skipped=false;unsigned submittedPoseEvents=0;
            std::set<uint64_t> observedPresentedSources;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            do {
                lastPresented=pump(caption).presentedSequence;
                const auto log=messages();
                for(const auto& line:log) {
                    if(line.find("Alpha native presentation commit:")!=std::string::npos && source(line," selected=")>=captionFirst &&
                        equal(line," actual=",expected) && line.find("reason=accepted-native-and-presentation-committed")!=std::string::npos)
                        if(!committedPose)committedPose=source(line," selected=");
                }
                scheduled=skipped=false;submittedPoseEvents=0;
                uint64_t previousIntervalSource=0;
                for(const auto& line:log) {
                    if(line.find("VP source measurement interval:")!=std::string::npos) {
                        const auto intervalSource=source(line," source=");
                        const auto batches=source(line," batches="),requests=source(line," subtitle_requests="),hits=source(line," subtitle_hits=");
                        // This counter interval begins after the prior interval log.
                        // Stable policy suppresses crop-change logs, but actual scan
                        // requests still distinguish dense scans from skipped sources.
                        if(committedPose && previousIntervalSource>=committedPose && batches>0 && requests>=hits) {
                            const auto scans=requests-hits;
                            scheduled|=scans>0;skipped|=scans<batches;
                        }
                        previousIntervalSource=intervalSource;
                    }
                    if(line.find("Alpha source crop:")!=std::string::npos && source(line," sequence=")>=captionFirst) {
                        scheduled|=line.find(" dense_scan=scheduled")!=std::string::npos;
                        skipped|=line.find(" dense_scan=skipped")!=std::string::npos;
                    }
                    if(committedPose && line.find("VP submitted geometry:")!=std::string::npos && source(line," selected=")>=committedPose) {
                        ++submittedPoseEvents;
                        if(!equal(line," actual_source=",expected)) {
                            for(const auto& diagnostic:log)Logger::WriteMessage(diagnostic.c_str());
                            Logger::WriteMessage(line.c_str());
                            Assert::Fail(L"A naturally skipped or scheduled source changed the successfully submitted Classic translated/filled pose.");
                        }
                    }
                }
                if(committedPose && lastPresented>=committedPose)observedPresentedSources.insert(lastPresented);
                if(committedPose && observedPresentedSources.size()>=24 && scheduled && skipped && submittedPoseEvents)break;
            }while(std::chrono::steady_clock::now()<deadline&&callback.state!=RENDERSTATE_FAILED);
            for(const auto& line:messages())Logger::WriteMessage(line.c_str());
            Logger::WriteMessage((std::string("Classic direct pose observations: committed=")+std::to_string(committedPose)+
                " distinct_presented="+std::to_string(observedPresentedSources.size())+
                " scheduled="+std::to_string(scheduled)+" skipped="+std::to_string(skipped)).c_str());
            bool nativeIngress=false,sourcePrepared=false;
            for(const auto& line:messages())if(line.find("VP source preparation dispatch: stage=accepted-before-notify")!=std::string::npos &&
                line.find("lookahead=0")!=std::string::npos && line.find("source_owned=1 pixel_scans_on_caller=0")!=std::string::npos) {
                nativeIngress|=line.find("native_queued=1")!=std::string::npos;
            }
            for(const auto& line:messages())if(line.find("VP source preparation stage:")!=std::string::npos &&
                line.find("owned=1 native_complete=1 subtitle_required=1 subtitle_complete=1")!=std::string::npos)
                sourcePrepared=true;
            Assert::IsTrue(nativeIngress && sourcePrepared,
                L"With AR lookahead disabled, ingress must dispatch native work and bounded preparation must complete current native and subtitle work before presentation.");
            Assert::IsTrue(committedPose>0,L"Real plugin never committed the independently acquired Classic translation plus configured side fill.");
            Assert::IsTrue(scheduled&&skipped,L"The real renderer must exercise natural scheduled and skipped dense scans; the fixture cannot force a pose.");
            Assert::IsTrue(observedPresentedSources.size()>=24 && submittedPoseEvents>0,
                L"The stable final pose must survive at least 24 distinct observed selected source identities.");
        }

        TEST_METHOD(StandardRendererClassicLifecycleWithNormalDriftAndWiderFill)
        { VerifyClassicLifecycleMatrix(false,false); }
        TEST_METHOD(StandardRendererClassicLifecycleWithNormalDriftAndNarrowerFill)
        { VerifyClassicLifecycleMatrix(false,true); }
        TEST_METHOD(BetaRendererClassicLifecycleWithNormalDriftAndWiderFill)
        { VerifyClassicLifecycleMatrix(true,false); }
        TEST_METHOD(BetaRendererClassicLifecycleWithNormalDriftAndNarrowerFill)
        { VerifyClassicLifecycleMatrix(true,true); }
        TEST_METHOD(StandardRendererClassicUnknownCandidateSubmitsItsCurrentSource)
        { VerifyClassicLifecycleMatrix(false,false,true); }
        TEST_METHOD(BetaRendererClassicUnknownCandidateSubmitsItsCurrentSource)
        { VerifyClassicLifecycleMatrix(true,false,true); }

        void VerifyClassicLifecycleMatrix(bool beta,bool narrower,bool unknownAdmissionRegression=false)
        {
            struct ProbeClock final : ITimingClock {
                LARGE_INTEGER frequency{};
                ProbeClock(){Assert::IsTrue(QueryPerformanceFrequency(&frequency)!=FALSE && frequency.QuadPart>0);}
                timingclocktime_t TimingClockNow() override {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
                timingclocktime_t TimingClockTicksPerSecond() const override {return frequency.QuadPart;}
                const TCHAR* TimingClockDescription() override {return _T("Classic lifecycle integration QPC");}
            } clock;
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup {HMODULE value;~ModuleCleanup(){FreeLibrary(value);}} moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
            Assert::IsNotNull(reinterpret_cast<void*>(diagnostics));
            struct DiagnosticCleanup {VideoProcessorLibplaceboConfigureDiagnosticFn configure;bool captureAdmissions;
                ~DiagnosticCleanup(){
                    configure(nullptr,nullptr,nullptr,nullptr);
                    if(captureAdmissions) {
                        std::lock_guard<std::mutex> lock(classicPoseLogMutex);
                        classicPoseCaptureSubtitleAdmissions.store(false,std::memory_order_relaxed);
                    }
                }} diagnosticCleanup{diagnostics,unknownAdmissionRegression};
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);classicPoseAllRecordStages=false;classicPoseCaptureSubtitleAdmissions.store(unknownAdmissionRegression,std::memory_order_relaxed);}
            diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseMessages.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);}
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0 && GetTempFileNameA(temp,"clm",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.scope]\nautomatic_crop: true\nsubtitle_fit: true\n"
                "crop_narrower_content_to_fill_screen: true\ncrop_wider_content_to_fill_screen: true\n"
                "crop_narrower_content_aspect_limit: 2.32:1\ncrop_wider_content_aspect_limit: 2.42:1\n"
                "[vprenderer.subtitles.classic]\ntype: classic\nshortcut: F5\noffset_pixels: 15\n"
                "subtitle_engage_drift_ms: 500\nsubtitle_release_drift_ms: 2000\n"
                "subtitle_target_buffer_pixels: 10\nsubtitle_hold_seconds: 2\n"
                "[vprenderer.subtitles.reconstruction]\ntype: generated_gray\nshortcut: F6\noffset_pixels: 10\n"
                "text_reduction_percent: 30\ncleanup_fill: stretch\nsubtitle_hold_ms: 250\n";
            constexpr int width=960,height=540,captionHeight=16;
            const int bar=narrower?64:68,pictureBottom=height-bar;
            const int captionBottom=pictureBottom+20,captionTop=captionBottom-captionHeight;
            VideoStateComPtr state=new VideoState();state->valid=true;state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,(narrower||unknownAdmissionRegression)?24000:60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(state->BytesPerFrame()/2,uint16_t(512<<6));
            auto paintPicture=[&](std::vector<uint16_t>& pixels,int top,int bottom) {
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    pixels[size_t(y)*width+x]=uint16_t((y>=top&&y<bottom?300:64)<<6);
            };
            paintPicture(native,bar,pictureBottom);auto caption=native;auto expanded=native;paintPicture(expanded,8,height-8);
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=342+glyph*22;x<356+glyph*22;++x)
                    if(x<345+glyph*22 || x>=353+glyph*22 || (y>=captionTop+6&&y<captionTop+9))
                        caption[size_t(y)*width+x]=uint16_t(700<<6);
            const ActivePictureBounds physical{0,bar,width,pictureBottom,width,height,double(width)/(pictureBottom-bar),ActivePictureBounds::BarAxes::TOP_BOTTOM};
            const int target=(std::min)(bar,captionBottom+(std::max)(8,height/90)+15-pictureBottom+10);
            const int shift=(target+1)&~1;
            auto fillFor=[&](ActivePictureBounds bounds) {
                AlphaSourceCrop::AspectLimitFillInput fill;fill.sourceBounds=bounds;fill.contentReferenceAvailable=true;
                fill.contentReferenceBounds=physical;fill.trustedContentAuthorityAccepted=true;fill.screenAspect=2.35;
                fill.cropNarrowerContentToFillScreen=fill.narrowerLimitConfigured=true;fill.narrowerAspectLimit=2.32;
                fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;fill.widerAspectLimit=2.42;
                return AlphaSourceCrop::EvaluateAspectLimitFill(fill);
            };
            const auto initialFill=fillFor(physical);auto translated=physical;translated.top+=shift;translated.bottom+=shift;
            const auto translatedFill=fillFor(translated);const auto initial=initialFill.sourceBounds,settled=translatedFill.sourceBounds;
            Assert::IsTrue(initialFill.applied&&translatedFill.applied);
            Assert::IsTrue(narrower?initial.top>physical.top:initial.left>physical.left);
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic normal-drift lifecycle matrix",WS_POPUP,0,0,640,360,
                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,44,cleanup.window,&clock,true,12,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);
            ConfigFile config;Assert::IsTrue(config.Load(configPath));UnifiedProfileRuntime::Runtime runtime;std::string error;
            const bool initialized=runtime.Initialize(config,{},error);
            if(!initialized)Logger::WriteMessage(error.c_str());
            Assert::IsTrue(initialized,std::wstring(error.begin(),error.end()).c_str());
            CString active;bool restart=false,reset=false;
            Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(),active,restart,reset));
            Assert::IsFalse(restart);
            cleanup.renderer->SetQueueFramePolicy(4,4,true);
            cleanup.renderer->SetActivePictureLookaheadFrames(4);
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;size_t finalRecordCursor=0;
            auto messages=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;};
            auto dump=[&] {for(const auto& line:messages())Logger::WriteMessage(line.c_str());};
            auto require=[&](bool valid,const wchar_t* message) {if(!valid){dump();Assert::Fail(message);}};
            auto integer=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };

            struct Geometry {uint64_t sequence=0,nativeSource=0,certificateSource=0;double left=0,top=0,right=0,bottom=0,pts=0;unsigned mode=0,certificateOrigin=0,flags=0;int native[4]{};bool composed=false,poseEligible=false;int routedTranslation=0;double poseDelta=0,minimumTop=0,maximumTop=0;};
            auto geometries=[&] {
                std::vector<Geometry> result;std::vector<AnalysisDiagnosticRecordV1> trace;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                for(;finalRecordCursor<trace.size();++finalRecordCursor) {
                    const auto& record=trace[finalRecordCursor];
                    if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))continue;
                    require(record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence,
                        L"Actual successful output lacks a coherent selected source identity.");
                    Geometry g;g.sequence=record.counters[1];g.pts=record.metrics[0];g.mode=unsigned(record.counters[3]);
                    g.left=record.metrics[8];g.top=record.metrics[9];g.right=record.metrics[10];g.bottom=record.metrics[11];
                    g.composed=(record.flags&2u)!=0;g.poseEligible=(record.flags&1024u)!=0;
                    g.nativeSource=record.counters[4];g.certificateSource=record.counters[5];
                    g.certificateOrigin=unsigned(record.counters[6]);g.flags=record.flags;
                    for(int e=0;e<4;++e)g.native[e]=record.published[e];
                    g.poseDelta=record.metrics[2];g.routedTranslation=int(std::lround(record.metrics[4]));
                    g.minimumTop=record.metrics[6];g.maximumTop=record.metrics[7];
                    require(std::isfinite(g.pts) && g.pts>0 && std::isfinite(g.left) && std::isfinite(g.top) &&
                        std::isfinite(g.right) && std::isfinite(g.bottom) && g.left<g.right && g.top<g.bottom,
                        L"Actual successful output has invalid native source coordinates or QPC timestamps.");
                    result.push_back(g);
                }
                return result;
            };
            auto same=[](const Geometry& actual,const ActivePictureBounds& wanted) {
                return std::abs(actual.left-wanted.left)<0.01&&std::abs(actual.top-wanted.top)<0.01&&
                    std::abs(actual.right-wanted.right)<0.01&&std::abs(actual.bottom-wanted.bottom)<0.01;
            };
            auto sameSize=[&](const Geometry& g) {
                return std::abs(g.right-g.left-(settled.right-settled.left))<0.01&&
                    std::abs(g.bottom-g.top-(settled.bottom-settled.top))<0.01;
            };
            const auto sourcePeriod=std::chrono::duration<double>(1001.0/((narrower||unknownAdmissionRegression)?24000.0:60000.0));
            auto nextSource=std::chrono::steady_clock::now();
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                if(callback.state==RENDERSTATE_RENDERING){VideoFrame frame(pixels.data(),sequence++,clock.TimingClockNow(),&buffer);cleanup.renderer->OnVideoFrame(frame);}
                nextSource+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(sourcePeriod);
                std::this_thread::sleep_until(nextSource);
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);UseCompletedSubmittedPresentation(value);return value;
            };
            auto scans=[&](uint64_t first) {
                bool scheduled=false,skipped=false;
                for(const auto& line:messages())if(line.find("Alpha source crop:")!=std::string::npos&&integer(line," sequence=")>=first) {
                    scheduled|=line.find(" dense_scan=scheduled")!=std::string::npos;skipped|=line.find(" dense_scan=skipped")!=std::string::npos;
                }
                return scheduled&&(narrower||skipped);
            };
            uint64_t acceptedNativeSource=0;std::map<uint64_t,Geometry> preProbeSubmitted;
            auto acquireNative=[&] {
                const auto first=sequence,deadlineMs=GetTickCount64()+15000;bool gotActual=false,gotNative=false;
                do {
                    const auto value=pump(native);
                    gotNative=value.available&&!value.provisional&&value.detectedSequence>=first&&std::abs(value.ratio-physical.aspectRatio)<0.005;
                    for(const auto& g:geometries()) {
                        if(unknownAdmissionRegression)preProbeSubmitted.emplace(g.sequence,g);
                        if(g.sequence>=first&&same(g,initial)) {
                            gotActual=true;
                            if(unknownAdmissionRegression&&g.nativeSource)acceptedNativeSource=g.nativeSource;
                        }
                    }
                    if(gotNative&&gotActual)return;
                }while(GetTickCount64()<deadlineMs&&callback.state!=RENDERSTATE_FAILED);
                require(false,L"Normal-drift fixture failed independent native-plane acquisition or exact configured fill.");
            };
            acquireNative();
            if(unknownAdmissionRegression) {
                require(acceptedNativeSource>0,
                    L"Classic fixture did not establish an accepted native source before the cue stimulus.");
                Logger::WriteMessage("Classic unknown-candidate regression: inspect exact-current UNKNOWN admissions and submitted sources\n");
                const uint64_t onset=sequence;
                std::map<uint64_t,AnalysisDiagnosticRecordV1> unknownCandidates;
                std::map<uint64_t,AnalysisDiagnosticRecordV1> currentRawUnknownWithoutCandidate;
                std::set<uint64_t> establishedPanRawSources;
                std::map<uint64_t,Geometry> submitted=preProbeSubmitted;
                uint64_t firstUnknownSource=0;
                const auto probeDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                auto exactRawCurrent=[](const AnalysisDiagnosticRecordV1& record) {
                    const auto& m=record.metrics;
                    return record.counters[3]==record.acceptedSequence && record.counters[4]==record.acceptedSequence &&
                        record.counters[5]==1 && record.counters[6]==record.transportGeneration &&
                        record.counters[7]==record.transportGeneration &&
                        std::isfinite(m[8]) && std::isfinite(m[9]) && std::isfinite(m[10]) && std::isfinite(m[11]) &&
                        m[8]>=0 && m[9]>=0 && m[10]<=width && m[11]<=height && m[8]<m[10] && m[9]<m[11];
                };
                auto validateNoAuthority=[&](const Geometry& geometry,const wchar_t* message) {
                    require(geometry.native[0]==physical.left && geometry.native[1]==physical.top &&
                        geometry.native[2]==physical.right && geometry.native[3]==physical.bottom &&
                        geometry.nativeSource==acceptedNativeSource &&
                        !(geometry.flags&(32u|64u|8192u)),message);
                };
                auto joinAndValidate=[&] {
                    for(const auto& g:geometries())submitted.emplace(g.sequence,g);
                    std::vector<AnalysisDiagnosticRecordV1> trace;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                    for(const auto& record:trace) {
                        if(strcmp(record.stage,"subtitle-canvas-admission") || record.acceptedSequence<onset ||
                            record.counters[0]!=static_cast<unsigned>(AlphaSourceCrop::SubtitleCanvasMode::CLASSIC) ||
                            record.counters[1]!=static_cast<unsigned>(AlphaSourceCrop::CaptionObservationState::UNKNOWN) ||
                            !exactRawCurrent(record))continue;
                        const auto source=record.acceptedSequence;
                        const auto& m=record.metrics;
                        if(record.flags&8u) {
                            if(!std::isfinite(m[4]) || !std::isfinite(m[5]) || !std::isfinite(m[6]) || !std::isfinite(m[7]) ||
                                m[4]<0 || m[5]<0 || m[6]>width || m[7]>height || m[4]>=m[6] || m[5]>=m[7])continue;
                            unknownCandidates.emplace(source,record);
                            if(!firstUnknownSource || source<firstUnknownSource)firstUnknownSource=source;
                        } else {
                            currentRawUnknownWithoutCandidate.emplace(source,record);
                        }
                    }
                    for(const auto& item:unknownCandidates) {
                        const auto output=submitted.find(item.first);
                        if(output==submitted.end())continue;
                        const auto& admission=item.second;
                        const auto& geometry=output->second;
                        const auto& m=admission.metrics;
                        require(!(admission.flags&1u) && (admission.flags&2u) && !(admission.flags&4u),
                            L"A current UNKNOWN candidate must submit as an unprocessed Classic canvas without waiting or composing.");
                        require(admission.counters[2]<item.first,
                            L"The UNKNOWN candidate was not preceded by a submitted Classic source.");
                        require(geometry.mode==0 && !geometry.composed,
                            L"The UNKNOWN source changed out of Classic mode or claimed caption composition.");
                        require(m[7]>initial.bottom,
                            L"The fixture candidate does not challenge the previously submitted configured-fill viewport.");
                        bool candidateContainsFixtureInk=false;
                        for(int y=(std::max)(0,static_cast<int>(std::floor(m[5])));
                            y<(std::min)(height,static_cast<int>(std::ceil(m[7]))) && !candidateContainsFixtureInk;++y)
                            for(int x=(std::max)(0,static_cast<int>(std::floor(m[4])));
                                x<(std::min)(width,static_cast<int>(std::ceil(m[6])));++x)
                                if(caption[size_t(y)*width+x]==uint16_t(700<<6)){candidateContainsFixtureInk=true;break;}
                        require(candidateContainsFixtureInk,
                            L"The exact-current text candidate does not cover the fixture's actual caption pixels.");
                        require(geometry.left<=m[4] && geometry.top<=m[5] &&
                            geometry.right>=m[6] && geometry.bottom>=m[7],
                            L"The actual submitted source window clipped exact-current candidate pixels.");
                        require(std::abs((geometry.right-geometry.left)-(initial.right-initial.left))<0.01 &&
                            std::abs((geometry.bottom-geometry.top)-(initial.bottom-initial.top))<0.01 &&
                            std::abs(geometry.left-initial.left)<0.01 && std::abs(geometry.right-initial.right)<0.01 &&
                            geometry.top>=initial.top-0.01 && geometry.top<=settled.top+0.01 &&
                            geometry.bottom<=height+0.01,
                            L"Classic UNKNOWN continuation changed the configured same-size fill viewport.");
                        validateNoAuthority(geometry,
                            L"Classic UNKNOWN candidate output changed accepted native geometry/source or carried certificate/no-caption authority.");
                    }
                    for(const auto& item:currentRawUnknownWithoutCandidate) {
                        const auto& admission=item.second;const auto& m=admission.metrics;
                        const auto prior=submitted.find(admission.counters[2]);
                        const bool priorPan=prior!=submitted.end() &&
                            std::abs(prior->second.left-m[0])<0.01 && std::abs(prior->second.right-m[2])<0.01 &&
                            std::abs((prior->second.bottom-prior->second.top)-(m[3]-m[1]))<0.01 &&
                            std::abs(prior->second.top-m[1])>0.01;
                        const bool horizontalCurrent=admission.measured[0]<=m[8] && admission.measured[2]>=m[10];
                        const bool extendsBelow=m[11]>admission.measured[3];
                        const bool extendsAbove=m[9]<admission.measured[1];
                        const bool bottomPan=priorPan && prior->second.top>m[1]+0.01 && extendsBelow && !extendsAbove;
                        const bool topPan=priorPan && prior->second.top<m[1]-0.01 && extendsAbove && !extendsBelow;
                        if(!(horizontalCurrent && (bottomPan||topPan))) {
                            const bool newlyUnanchoredRaw=horizontalCurrent && !priorPan &&
                                ((extendsBelow&&!extendsAbove)||(extendsAbove&&!extendsBelow));
                            if(newlyUnanchoredRaw)
                                require((admission.flags&1u) && !(admission.flags&2u) && !(admission.flags&4u) &&
                                    submitted.find(item.first)==submitted.end(),
                                    L"Raw UNKNOWN occupancy without an established same-size pan was submitted as a continued subtitle canvas.");
                            continue;
                        }
                        establishedPanRawSources.insert(item.first);
                        require(!(admission.flags&1u) && (admission.flags&2u) && !(admission.flags&4u),
                            L"Exact-current raw occupancy during an established Classic pan must continue unprocessed without waiting or composing.");
                        const auto output=submitted.find(item.first);
                        if(output==submitted.end())continue;
                        const auto& geometry=output->second;
                        require(geometry.mode==0 && !geometry.composed &&
                            std::abs(geometry.left-m[0])<0.01 && std::abs(geometry.right-m[2])<0.01 &&
                            std::abs((geometry.bottom-geometry.top)-(m[3]-m[1]))<0.01 &&
                            (bottomPan?geometry.bottom>=m[11]-0.01:geometry.top<=m[9]+0.01),
                            L"Classic failed to submit the exact current raw-occupancy source through its established occupied-side pan.");
                        validateNoAuthority(geometry,
                            L"Raw UNKNOWN continuation changed accepted native geometry/source or carried certificate/no-caption authority.");
                    }
                };
                auto allKnownSourcesJoined=[&] {
                    for(const auto& item:unknownCandidates)if(submitted.find(item.first)==submitted.end())return false;
                    for(const auto source:establishedPanRawSources)if(submitted.find(source)==submitted.end())return false;
                    return true;
                };
                unsigned laterSources=0;const uint64_t minimumProbeEnd=onset+24;
                do {
                    pump(caption);
                    joinAndValidate();
                    if(firstUnknownSource) {
                        const auto output=submitted.find(firstUnknownSource);
                        if(output!=submitted.end()) {
                            laterSources=0;uint64_t previousSource=firstUnknownSource;
                            for(auto it=std::next(output);it!=submitted.end() && laterSources<2;++it) {
                                require(it->first==previousSource+1,
                                    L"Classic renderer left a source-sequence gap after an UNKNOWN caption candidate.");
                                require(it->second.pts>submitted.at(previousSource).pts,
                                    L"Classic submitted source timestamps failed to advance after an UNKNOWN caption candidate.");
                                require(std::abs((it->second.right-it->second.left)-(initial.right-initial.left))<0.01 &&
                                    std::abs((it->second.bottom-it->second.top)-(initial.bottom-initial.top))<0.01 &&
                                    std::abs(it->second.left-initial.left)<0.01 && std::abs(it->second.right-initial.right)<0.01,
                                    L"A following Classic source changed the configured same-size fill viewport.");
                                previousSource=it->first;++laterSources;
                            }
                        }
                        if(laterSources>=2 && sequence>=minimumProbeEnd && allKnownSourcesJoined())break;
                    }
                }while(std::chrono::steady_clock::now()<probeDeadline && callback.state!=RENDERSTATE_FAILED);
                size_t previousAdmissionCount=(std::numeric_limits<size_t>::max)();unsigned stableDrains=0;
                for(unsigned drain=0;drain<8 && std::chrono::steady_clock::now()<probeDeadline;++drain) {
                    std::this_thread::sleep_for(std::chrono::duration_cast<std::chrono::steady_clock::duration>(sourcePeriod));
                    joinAndValidate();
                    const size_t admissionCount=unknownCandidates.size()+currentRawUnknownWithoutCandidate.size();
                    if(allKnownSourcesJoined() && admissionCount==previousAdmissionCount)++stableDrains;
                    else stableDrains=0;
                    previousAdmissionCount=admissionCount;
                    if(stableDrains>=2)break;
                }
                joinAndValidate();
                require(firstUnknownSource>0,
                    L"The real Classic renderer fixture never exposed an exact-current UNKNOWN text candidate.");
                require(unknownCandidates.find(firstUnknownSource)!=unknownCandidates.end(),
                    L"The UNKNOWN candidate did not carry exact-current visible and retention source identities.");
                require(submitted.find(firstUnknownSource)!=submitted.end(),
                    L"Classic withheld the exact source carrying the current UNKNOWN caption candidate.");
                for(const auto& item:unknownCandidates)
                    require(submitted.find(item.first)!=submitted.end(),
                        L"A later exact-current UNKNOWN candidate source was withheld by Classic.");
                for(const auto source:establishedPanRawSources)
                    require(submitted.find(source)!=submitted.end(),
                        L"An exact-current UNKNOWN raw-occupancy source was withheld during an established Classic pan.");
                require(laterSources>=2,
                    L"Classic did not submit two advancing sources after the UNKNOWN candidate.");
                Logger::WriteMessage((std::string("Classic UNKNOWN renderer regression: candidates=")+std::to_string(unknownCandidates.size())+
                    " established_pan_raw_no_candidate="+std::to_string(establishedPanRawSources.size())+
                    " actual_source_ids=consecutive native_geometry=unchanged certificates=none\n").c_str());
                return;
            }
            Logger::WriteMessage("Classic lifecycle: caption onset with 500 ms engage drift\n");
            uint64_t onset=sequence,finalSequence=0;double previousTop=initial.top;std::set<int> intermediate;
            std::set<uint64_t> settledSources;const auto engageDeadline=GetTickCount64()+5000;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=onset) {
                    require(g.mode==0,L"Classic onset changed subtitle composition mode.");
                    require(g.top<=captionTop+0.01&&g.bottom>=captionBottom-0.01,
                        L"Classic confirmation or engage clipped current source-caption pixels.");
                    require(sameSize(g),L"Processed Classic subtitle changed the picture size during confirmation or engage.");
                    require(g.top>=previousTop-0.01&&g.top>=initial.top-0.01&&g.top<=settled.top+0.01,
                        L"Classic engage drift reversed or left its bounded native-to-caption translation interval.");
                    previousTop=g.top;if(g.top>initial.top+0.01&&g.top<settled.top-0.01)intermediate.insert(int(std::lround(g.top)));
                    if(same(g,settled)&&!finalSequence)finalSequence=g.sequence;
                }
                if(finalSequence&&value.presentedSequence>=finalSequence)settledSources.insert(value.presentedSequence);
                if(finalSequence&&settledSources.size()>=24&&scans(onset))break;
            }while(GetTickCount64()<engageDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(finalSequence>0&&settledSources.size()>=24,L"Classic engage never reached and retained its exact final filled pose for 24 distinct presented sources.");
            require(intermediate.size()>=2,L"The configured 500 ms Classic engage drift snapped instead of exercising intermediate actual poses.");
            require(scans(onset),L"Classic normal-drift onset must exercise natural scheduled scans and, above 30 Hz, skipped dense scans.");
            Logger::WriteMessage("Classic lifecycle: two-second hold then two-second release drift\n");
            const uint64_t removal=sequence;const double removalPts=double(clock.TimingClockNow())/clock.TimingClockTicksPerSecond();
            double releaseStarted=0,releaseFinished=0;previousTop=settled.top;std::set<int> releaseIntermediate;std::set<uint64_t> returnedSources;
            uint64_t returnedSequence=0;const auto releaseDeadline=GetTickCount64()+7000;
            do {
                const auto value=pump(native);
                for(const auto& g:geometries())if(g.sequence>=removal) {
                    require(g.mode==0&&sameSize(g),L"Classic hold/release resized its filled viewport or failed open between dense scans.");
                    require(g.top<=previousTop+0.01&&g.top>=initial.top-0.01&&g.top<=settled.top+0.01,
                        L"Classic release drift reversed direction or jumped outside its bounded interval.");
                    if(g.top<settled.top-0.01&&!releaseStarted)releaseStarted=g.pts;
                    previousTop=g.top;if(g.top>initial.top+0.01&&g.top<settled.top-0.01)releaseIntermediate.insert(int(std::lround(g.top)));
                    if(same(g,initial)&&!returnedSequence){returnedSequence=g.sequence;releaseFinished=g.pts;}
                }
                if(returnedSequence&&value.presentedSequence>=returnedSequence)returnedSources.insert(value.presentedSequence);
                if(returnedSources.size()>=24)break;
            }while(GetTickCount64()<releaseDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(returnedSources.size()>=24&&releaseIntermediate.size()>=2,L"Classic did not exercise and complete its normal hold/release lifecycle.");
            require(releaseStarted-removalPts>=1.7,L"Classic subtitle hold released substantially before the configured two seconds.");
            require(releaseFinished-releaseStarted>=1.2,L"Classic two-second release drift snapped instead of moving through its timed interval.");
            // Re-engage before a real picture transition so a previous caption owner
            // cannot conceal newly exposed broad picture pixels in either bar.
            Logger::WriteMessage("Classic lifecycle: real picture expansion overrides an established caption pose\n");
            const auto reacquireDeadline=GetTickCount64()+5000;bool translatedAgain=false;
            do {pump(caption);for(const auto& g:geometries())translatedAgain|=same(g,settled);}
            while(!translatedAgain&&GetTickCount64()<reacquireDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(translatedAgain,L"Classic failed to establish its caption pose before the real-picture expansion challenge.");
            const uint64_t pictureFirst=sequence;bool pictureAccepted=false;std::set<uint64_t> pictureSources;const auto pictureDeadline=GetTickCount64()+15000;
            do {
                const auto value=pump(expanded);
                // Pixel preservation starts with the first observed submitted source;
                // AR confirmation and the stability window may complete later.
                if(value.presentedSequence>=pictureFirst)
                    require(value.inUseAvailable && value.inUseState!=PictureAspectUse::UNAVAILABLE &&
                        std::isfinite(value.inUseRatio) && value.inUseRatio>0 &&
                        value.inUseRatio<=double(width)/(height-16)+0.00001,
                        L"The first observed expanded-picture output retained the old cropped viewport while AR confirmation was pending.");
                for(const auto& g:geometries())if(g.sequence>=pictureFirst)
                    require(g.top<=8.01&&g.bottom>=height-8-0.01&&g.left<=0.01&&g.right>=width-0.01,
                        L"An old Classic caption pose clipped actual broad picture expansion.");
                pictureAccepted=value.available&&!value.provisional&&value.detectedSequence>=pictureFirst&&std::abs(value.ratio-double(width)/(height-16))<0.005;
                if(pictureAccepted&&value.presentedSequence>=pictureFirst)pictureSources.insert(value.presentedSequence);
                if(pictureAccepted&&pictureSources.size()>=24)break;
            }while(GetTickCount64()<pictureDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(pictureAccepted&&pictureSources.size()>=24,L"Actual broad picture expansion did not replace the old scope detection and presentation.");
            acquireNative();
            Logger::WriteMessage("Classic lifecycle: live application profile switch to Reconstruction\n");
            const auto captionDeadline=GetTickCount64()+5000;translatedAgain=false;
            do {pump(caption);for(const auto& g:geometries())translatedAgain|=same(g,settled);}
            while(!translatedAgain&&GetTickCount64()<captionDeadline&&callback.state!=RENDERSTATE_FAILED);
            require(translatedAgain,L"Classic did not establish its pose before the profile transition challenge.");
            UnifiedProfileRuntime::SelectionResult selection;
            require(runtime.SelectKey("F6",{},selection,error)&&selection.changed,L"Application runtime failed to select the Reconstruction profile.");
            require(selection.snapshot && selection.snapshot->effectiveSelections.count("subtitles") &&
                selection.snapshot->effectiveSelections.at("subtitles").size()==1 &&
                selection.snapshot->effectiveSelections.at("subtitles").front()=="reconstruction",
                L"Profile transition fixture did not publish the requested Reconstruction selection.");
            restart=reset=false;require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset),L"Actual renderer rejected the Reconstruction profile snapshot.");
            require(!restart,L"Subtitle profile transition unexpectedly requires a renderer rebuild.");
            const uint64_t reconstructionFirst=sequence;uint64_t reconstructionReady=0,observationDeadline=0;unsigned lastMode=0;
            std::set<uint64_t> reconstructionSources;const auto preparationDeadline=GetTickCount64()+ColdShaderPreparationBudgetMs;
            do {
                const auto value=pump(caption);
                for(const auto& g:geometries())if(g.sequence>=reconstructionFirst) {
                    lastMode=g.mode;if(g.mode!=3)continue;
                    require(!g.poseEligible && std::abs(g.poseDelta)<0.01,L"Classic-to-Reconstruction advanced output applied a Classic translation pose.");
                    if(g.composed && same(g,initial) && !reconstructionReady) {
                        reconstructionReady=g.sequence;observationDeadline=GetTickCount64()+5000;
                    }
                    if(reconstructionReady)require(same(g,initial),L"Ready Classic-to-Reconstruction output changed its native presentation.");
                    else require(same(g,initial) && g.composed,
                        L"Reconstruction submitted an unprocessed caption or changed the native picture size during preparation.");
                }
                if(reconstructionReady && value.presentedSequence>=reconstructionReady) {
                    bool hasPresentation=false,composed=false;
                    std::vector<AnalysisDiagnosticRecordV1> trace;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                    for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                        if(!strcmp(record.stage,"submitted-presentation"))composed=(record.flags&2u)!=0;
                        if(!strcmp(record.stage,"submitted-presentation")) {
                            hasPresentation=true;
                            if(!IsExactCompletedOutput(record,value.sourceGeneration,3)) {
                                char failure[1200]{};
                                sprintf_s(failure,"Reconstruction final identity veto: ready=%llu snapshot=%llu snapshot_gen=%llu record_gen=%llu renderer=%llu accepted=%llu flags=%u current=%llu selected=%llu cue=%llu mode=%llu native_source=%llu cert_source=%llu cert_origin=%llu pose_delta=%.6f displayed=%d,%d-%d,%d actual=%.3f,%.3f-%.3f,%.3f\n",
                                    reconstructionReady,value.presentedSequence,value.sourceGeneration,record.transportGeneration,
                                    record.rendererGeneration,record.acceptedSequence,record.flags,record.counters[0],record.counters[1],
                                    record.counters[2],record.counters[3],record.counters[4],record.counters[5],record.counters[6],record.metrics[2],
                                    record.displayed[0],record.displayed[1],record.displayed[2],record.displayed[3],
                                    record.metrics[8],record.metrics[9],record.metrics[10],record.metrics[11]);
                                Logger::WriteMessage(failure);
                            }
                            require(IsExactCompletedOutput(record,value.sourceGeneration,3),L"Reconstruction final output lacks current mode/source/certificate identity.");
                            require(record.displayed[0]==initial.left && record.displayed[1]==initial.top &&
                                record.displayed[2]==initial.right && record.displayed[3]==initial.bottom,
                                L"Prepared Reconstruction exact-source output lost native picture presentation.");
                        }
                    }
                    require(hasPresentation,L"Prepared Reconstruction lacks exact-source presentation diagnostics.");
                    if(lastMode==3 && composed)reconstructionSources.insert(value.presentedSequence);
                }
                if(reconstructionSources.size()>=24)break;
            }while(GetTickCount64()<(reconstructionReady?observationDeadline:preparationDeadline) && callback.state!=RENDERSTATE_FAILED);
            require(reconstructionReady>0,L"Reconstruction did not acquire an actual composed native output within the bounded cold-preparation budget.");
            require(reconstructionSources.size()>=24,L"Ready Reconstruction did not present 24 distinct actually composed native sources within five seconds.");
        }

        void VerifyClassicUhdBarOverlay(bool beta,bool bothBars,bool reconstruction=false,bool nativeV210=false,bool crossingEdge=false,bool nativePreflightOnly=false,bool blackMode=false)
        {
            ScopedCadenceDiagnostics cadenceDiagnostics(reconstruction&&crossingEdge&&!nativePreflightOnly);
            constexpr bool narrower=false;
            const unsigned advancedMode=static_cast<unsigned>(blackMode?SubtitlePreviewMode::BlackBackground:SubtitlePreviewMode::GeneratedGrayBackground);
            struct ProbeClock final : ITimingClock {
                LARGE_INTEGER frequency{};
                ProbeClock(){Assert::IsTrue(QueryPerformanceFrequency(&frequency)!=FALSE && frequency.QuadPart>0);}
                timingclocktime_t TimingClockNow() override {LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
                timingclocktime_t TimingClockTicksPerSecond() const override {return frequency.QuadPart;}
                const TCHAR* TimingClockDescription() override {return _T("Classic lifecycle integration QPC");}
            } clock;
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup {HMODULE value;~ModuleCleanup(){FreeLibrary(value);}} moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
            Assert::IsNotNull(reinterpret_cast<void*>(diagnostics));
            struct DiagnosticCleanup {VideoProcessorLibplaceboConfigureDiagnosticFn configure;
                ~DiagnosticCleanup(){configure(nullptr,nullptr,nullptr,nullptr);}} diagnosticCleanup{diagnostics};
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);classicPoseAllRecordStages=false;}
            diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
            {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseMessages.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);}
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0 && GetTempFileNameA(temp,"clm",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.scope]\nautomatic_crop: true\nsubtitle_fit: true\n"
                "crop_narrower_content_to_fill_screen: true\ncrop_wider_content_to_fill_screen: true\n"
                "crop_narrower_content_aspect_limit: 2.32:1\ncrop_wider_content_aspect_limit: 2.42:1\n"
                "[vprenderer.subtitles.classic]\ntype: classic\nshortcut: F5\noffset_pixels: 15\n"
                "subtitle_engage_drift_ms: 500\nsubtitle_release_drift_ms: 2000\n"
                "subtitle_target_buffer_pixels: 10\nsubtitle_hold_seconds: 2\n"
                "[vprenderer.subtitles.reconstruction]\ntype: "<<(blackMode?"black":"generated_gray")<<"\nshortcut: F6\noffset_pixels: 10\n"
                "text_reduction_percent: 30\ncleanup_fill: stable_surround\nsubtitle_hold_ms: 250\nsubtitle_near_bar_px: 20\nsubtitle_generated_gray_color: 000000\nsubtitle_generated_gray_max_luminance: .25\nsubtitle_generated_gray_opacity: .65\nsubtitle_generated_gray_border_width: 1\nsubtitle_generated_gray_border_opacity: .4\nsubtitle_generated_gray_blur_px: 30\nrounded_corners: true\nminimum_headroom_pixels: 30\n";
            constexpr int width=3840,height=2160;
            const int captionHeight=crossingEdge?50:22;
            const int bar=250,pictureBottom=1906;
            const int captionBottom=1940,captionTop=captionBottom-captionHeight;
            VideoStateComPtr state=new VideoState();state->valid=true;state->videoFrameEncoding=nativeV210?VideoFrameEncoding::V210:VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,narrower?24000:60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> native(size_t(width)*height*2,uint16_t(512<<6));
            auto paintPicture=[&](std::vector<uint16_t>& pixels,int top,int bottom) {
                for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                    pixels[size_t(y)*width+x]=uint16_t((y>=top&&y<bottom?300:64)<<6);
            };
            paintPicture(native,bar,pictureBottom);
            // Cross-edge Latin acquisition requires distributed dark scene backing.
            // This full-width native scene band is ABOVE the detector's maximum
            // black cutoff104, below the existing glyph backing limit133, and has
            // no finite subtitle-card boundary at the physical bar.
            if(crossingEdge)for(int y=1840;y<pictureBottom;++y)for(int x=0;x<width;++x)
                native[size_t(y)*width+x]=uint16_t(128<<6);
            auto caption=native;auto expanded=native;paintPicture(expanded,8,height-8);
            for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                for(int x=1400+glyph*64;x<1432+glyph*64;++x)
                    if(x<1406+glyph*64 || x>=1426+glyph*64 || (y>=captionTop+captionHeight/2-2&&y<captionTop+captionHeight/2+2))
                        caption[size_t(y)*width+x]=uint16_t(892<<6);
            auto both=caption;
            // Pixel owners outlive Cleanup, which drains renderer references first.
            std::vector<uint16_t> residual[2],motionOnly[2];
            std::map<const uint16_t*,std::vector<uint32_t>> packedFrames;
            // Sparse bar UI crosses the trusted top boundary, as in the live
            // 134..262 top row; the separate bottom cue remains present.
            for(int glyph=0;glyph<16;++glyph)for(int y=134;y<262;++y)
                for(int x=158+glyph*144;x<238+glyph*144;++x)
                    if(x<174+glyph*144 || x>=222+glyph*144 || (y>=190&&y<206))
                        both[size_t(y)*width+x]=uint16_t(892<<6);
            const ActivePictureBounds physical{0,bar,width,pictureBottom,width,height,double(width)/(pictureBottom-bar),ActivePictureBounds::BarAxes::TOP_BOTTOM};
            const int target=(std::min)(bar,captionBottom+(std::max)(8,height/90)+15-pictureBottom+10);
            const int shift=(target+1)&~1;
            auto fillFor=[&](ActivePictureBounds bounds) {
                AlphaSourceCrop::AspectLimitFillInput fill;fill.sourceBounds=bounds;fill.contentReferenceAvailable=true;
                fill.contentReferenceBounds=physical;fill.trustedContentAuthorityAccepted=true;fill.screenAspect=2.35;
                fill.cropNarrowerContentToFillScreen=fill.narrowerLimitConfigured=true;fill.narrowerAspectLimit=2.32;
                fill.cropWiderContentToFillScreen=fill.widerLimitConfigured=true;fill.widerAspectLimit=2.42;
                return AlphaSourceCrop::EvaluateAspectLimitFill(fill);
            };
            const auto initialFill=fillFor(physical);auto translated=physical;translated.top+=shift;translated.bottom+=shift;
            const auto translatedFill=fillFor(translated);const auto initial=initialFill.sourceBounds,settled=translatedFill.sourceBounds;
            Assert::IsFalse(initialFill.applied || translatedFill.applied,
                L"Live native aspect must remain just below the configured narrower-fill limit.");

            if(reconstruction) {
                residual[0]=caption;residual[1]=caption;
                for(int phase=0;phase<2;++phase) {
                    for(int y=bar;y<pictureBottom;++y)for(int x=0;x<width;++x)
                        residual[phase][size_t(y)*width+x]=uint16_t(((crossingEdge && y>=1840)?
                            110+18*((x/64+y/64+phase)&1):300+40*((x/64+y/64+phase)&1))<<6);
                    for(int glyph=0;glyph<12;++glyph)for(int y=captionTop;y<captionBottom;++y)
                        for(int x=1400+glyph*64;x<1432+glyph*64;++x)
                            if(x<1406+glyph*64 || x>=1426+glyph*64 || (y>=captionTop+captionHeight/2-2&&y<captionTop+captionHeight/2+2))
                                residual[phase][size_t(y)*width+x]=uint16_t(892<<6);
                    motionOnly[phase]=residual[phase];
                    // An off-center solid UI badge is not a subtitle glyph row.
                    // Bottom text ownership cannot authorize erasing this badge.
                    for(int y=140;y<200;++y)for(int x=40;x<220;++x)
                        residual[phase][size_t(y)*width+x]=uint16_t(892<<6);
                }

            }
            if(nativeV210) {
                const size_t pitch=state->BytesPerRow();
                Assert::IsTrue(pitch==10240,L"UHD native V210 capture must use the live 10240-byte stride.");
                auto pack=[&](const std::vector<uint16_t>& pixels) {
                    auto& words=packedFrames[pixels.data()];words.resize(pitch*height/4);
                    for(int y=0;y<height;++y)for(int x=0;x<width;x+=6) {
                        const auto yv=[&](int i){return uint32_t(pixels[size_t(y)*width+x+i]>>6);};
                        const auto uv=[&](int i){return uint32_t(pixels[size_t(width)*height+size_t(y)*width+x+i]>>6);};
                        auto* out=words.data()+size_t(y)*pitch/4+size_t(x/6)*4;
                        out[0]=uv(0)|(yv(0)<<10)|(uv(1)<<20);
                        out[1]=yv(1)|(uv(2)<<10)|(yv(2)<<20);
                        out[2]=uv(3)|(yv(3)<<10)|(uv(4)<<20);
                        out[3]=yv(4)|(uv(5)<<10)|(yv(5)<<20);
                    }
                    AnalysisLumaSource source{reinterpret_cast<const uint8_t*>(words.data()),words.size()*4,width,height,
                        pitch,0,AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
                    // Verify all six packing slots, glyph edges and chroma directly.
                    for(int y:{0,134,140,199,249,250,captionTop,captionBottom-1,1905,1906,2159})
                        for(int x:{0,1,2,3,4,5,40,157,158,173,219,1400,1405,1406,1431,3839}) {
                            AnalysisLumaSample p;Assert::IsTrue(source.Sample(x,y,p),L"Native V210 fixture sample was unavailable.");
                            Assert::IsTrue(p.luma==(pixels[size_t(y)*width+x]>>6) && p.chromaU==512 && p.chromaV==512,
                                L"Native V210 fixture changed a 10-bit source luma/chroma sample.");
                        }
                };
                pack(native);pack(caption);pack(both);
                if(reconstruction){pack(residual[0]);pack(residual[1]);pack(motionOnly[0]);pack(motionOnly[1]);}
            }
            if(reconstruction) {
                // Exercise the exact source producer and native worker contract before
                // allocating a GPU renderer or waiting for shader preparation.
                AnalysisLumaSource pixels;
                if(nativeV210) {
                    const auto& packed=packedFrames.at(caption.data());
                    pixels={reinterpret_cast<const uint8_t*>(packed.data()),packed.size()*4,width,height,state->BytesPerRow(),0,
                        AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                } else pixels={reinterpret_cast<const uint8_t*>(caption.data()),caption.size()*2,width,height,
                    size_t(width)*2,size_t(width)*2,AnalysisLumaFormat::P210,VideoFrameEncoding::P210,ColorSpace::REC_709,7};
                auto sampler=std::make_unique<SubtitleMeasurementSampler>();
                auto observations=std::make_unique<std::vector<SubtitleBoxObservation>>();observations->reserve(4);
                SubtitleBarTrackingReference prior;
                const auto firstTick=clock.TimingClockNow();
                for(uint64_t index=0;index<4;++index) {
                    auto key=std::make_unique<SubtitleBoxObservation>();
                    key->identity.transportGeneration=7;key->identity.rendererGeneration=7;
                    key->identity.acceptedSequence=key->identity.sourceFrameNumber=1+index*2;
                    key->identity.captureTimestamp=firstTick+index*clock.TimingClockTicksPerSecond()*1001/30000;
                    key->identity.sourceFormatGeneration=17;key->identity.analysisDomainKey=19;key->identity.viewportGeneration=5;
                    key->width=width;key->height=height;key->policyGeneration=1;key->continuityGeneration=1;
                    key->analysisStride=2;key->nearBarDistance=20;key->optimizationMode=0;key->scriptHint=SubtitleScriptHint::Latin;
                    key->sharedPicture.required=true;key->sharedPicture.bounds=physical;key->sharedPicture.identity=key->identity;
                    auto measured=sampler->Measure(pixels,*key,prior,60000.0/1001);
                    Logger::WriteMessage((std::string("UHD native fixture preflight: encoding=")+(nativeV210?"V210":"P210")+
                        " cross="+std::to_string(crossingEdge)+" sequence="+std::to_string(key->identity.acceptedSequence)+
                        " analyzed="+std::to_string(measured.analyzed)+" bar_authority="+std::to_string(measured.barAuthority)+
                        " search="+std::to_string(measured.text.topSearchComplete)+"/"+std::to_string(measured.text.bottomSearchComplete)+
                        " components="+std::to_string(measured.text.diagnosticComponentCount)+" lines="+std::to_string(measured.text.lineCount)+
                        " reason="+measured.text.diagnosticReason+" bars="+measured.barEvidence.reason).c_str());
                    prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);
                    observations->push_back(std::move(measured));
                }
                for(const auto& observed:*observations)Assert::IsTrue(observed.analyzed && observed.barAuthority &&
                    observed.text.detected && !observed.text.workLimit && observed.text.topSearchComplete && observed.text.bottomSearchComplete,
                    L"Exact native subtitle fixture failed complete current acquisition; see preflight reasons. GPU run deliberately not started.");
                auto preview=std::make_unique<SubtitleBoxPreview>();
                *preview=SubtitleBoxLookahead::Resolve(observations->data(),observations->size(),1,1);
                auto presentation=std::make_unique<SubtitleBoxPresentation>();
                presentation->RefreshReferencePixels(*preview,pixels);
                const auto acquired=presentation->Consume(*preview,250,1001.0/60);
                Logger::WriteMessage((std::string("UHD native four-frame acquisition: ")+presentation->DecisionReason()+
                    " lines="+std::to_string(acquired.lineCount)+" detected="+std::to_string(acquired.detected)+
                    " pending="+std::to_string(acquired.confirmationPending)).c_str());
                Assert::IsTrue(acquired.detected && !acquired.held && !acquired.confirmationPending,
                    L"Exact native fixture did not acquire a strong four-frame current cue; GPU run deliberately not started.");
                if(crossingEdge) {
                    auto sourceFor=[&](const std::vector<uint16_t>& frame) {
                        if(nativeV210) {
                            const auto& words=packedFrames.at(frame.data());
                            return AnalysisLumaSource{reinterpret_cast<const uint8_t*>(words.data()),words.size()*4,width,height,
                                state->BytesPerRow(),0,AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                        }
                        return AnalysisLumaSource{reinterpret_cast<const uint8_t*>(frame.data()),frame.size()*2,width,height,
                            size_t(width)*2,size_t(width)*2,AnalysisLumaFormat::P210,VideoFrameEncoding::P210,ColorSpace::REC_709,7};
                    };
                    const auto raw=ExtractActivePictureEvidence(sourceFor(native));
                    Assert::IsTrue(raw.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                        raw.trustedBounds.left==0 && raw.trustedBounds.top==bar && raw.trustedBounds.right==width && raw.trustedBounds.bottom==pictureBottom,
                        L"Dark native scene backing must not become an encoded bar or change measured physical boundaries.");
                    auto padding=std::make_unique<SubtitlePaddingGuard>();SubtitlePaddingPolicy policy;
                    policy.glyphRelative=true;policy.minimumHeadroomPixels=30;
                    const auto safe=padding->Consume(acquired,*preview,{60,40,21},policy);const auto display=padding->DisplayBounds();
                    auto compositor=std::make_unique<SubtitleCutPastePresentation>();
                    auto geometry=compositor->Consume(acquired,*preview,safe,10,&display);
                    geometry=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(geometry,{0,bar,width,pictureBottom},10),30,safe,true,false);
                    Assert::IsTrue(geometry.valid,L"Cross-edge preflight acquired no real fitted/reduced composition geometry.");
                    bool intersects=false;
                    auto changedInside=[&](const SubtitleBoxRect& r) {
                        for(int y=(std::max)(bar,r.top);y<(std::min)(pictureBottom,r.bottom);++y)
                            for(int x=(std::max)(0,r.left);x<(std::min)(width,r.right);++x)
                                if(caption[size_t(y)*width+x]!=motionOnly[1][size_t(y)*width+x])return true;
                        return false;
                    };
                    if(geometry.generatedCleanupRowCount) {
                        for(unsigned row=0;row<geometry.generatedCleanupRowCount;++row)intersects|=changedInside(geometry.generatedCleanupRows[row]);
                    } else if(geometry.generatedCleanup.Valid())intersects=changedInside(geometry.generatedCleanup);
                    Logger::WriteMessage((std::string("Cross-edge preflight cleanup: complete_card=")+std::to_string(acquired.provenSourcePanel.Valid())+
                        " cleanup="+std::to_string(geometry.generatedCleanup.left)+","+std::to_string(geometry.generatedCleanup.top)+"-"+
                        std::to_string(geometry.generatedCleanup.right)+","+std::to_string(geometry.generatedCleanup.bottom)+
                        " rows="+std::to_string(geometry.generatedCleanupRowCount)+" actual_changed_row_intersection="+std::to_string(intersects)).c_str());
                    Assert::IsTrue(intersects,L"Acquired cross-edge fixture has no real erase-row intersection with native motion; GPU coverage not established.");
                    auto guard=std::make_unique<SubtitleCadenceGuard>();
                    Assert::IsTrue(guard->RememberAccepted(*preview,acquired,pixels,physical,geometry,1001.0/60,250,blackMode?3:5),
                        L"Cross-edge real native geometry failed cadence seeding before GPU initialization.");
                    auto skipped=std::make_unique<SubtitleBoxPreview>();skipped->policyGeneration=1;skipped->continuityGeneration=1;
                    skipped->current=observations->front();auto& current=skipped->current;
                    current.identity.acceptedSequence=2;current.identity.sourceFrameNumber=2;
                    current.identity.captureTimestamp=firstTick+clock.TimingClockTicksPerSecond()*1001/60000;
                    current.sharedPicture.identity=current.identity;current.analyzed=false;current.cadenceSkipped=true;
                    current.text={};current.ink.reset();current.barAuthority=false;current.independentBarAnalysis=false;
                    auto proof=std::make_unique<SubtitleCadenceProof>();*proof=guard->Prepare(*skipped,sourceFor(motionOnly[1]),physical,1001.0/60,250);
                    Logger::WriteMessage((std::string("Cross-edge native motion proof: reason=")+proof->reason+" samples="+std::to_string(proof->samples)).c_str());
                    Assert::IsTrue(!proof->valid && !strcmp(proof->reason,"cadence-current-cleanup-changed") && proof->samples>0,
                        L"Native cross-edge motion did not demonstrate sampled unowned actual-cleanup change; GPU coverage not established.");
                }
                if(nativePreflightOnly)return;
            }
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"Classic UHD simultaneous bar UI",WS_POPUP,0,0,2000,1125,
                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,44,cleanup.window,&clock,true,16,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
            Assert::IsNotNull(cleanup.renderer);
            ConfigFile config;Assert::IsTrue(config.Load(configPath));UnifiedProfileRuntime::Runtime runtime;std::string error;
            const bool initialized=runtime.Initialize(config,{},error);
            if(!initialized)Logger::WriteMessage(error.c_str());
            Assert::IsTrue(initialized,std::wstring(error.begin(),error.end()).c_str());
            CString active;bool restart=false,reset=false;
            Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(),active,restart,reset));
            Assert::IsFalse(restart);
            cleanup.renderer->SetQueueFramePolicy(4,9,true);
            cleanup.renderer->SetActivePictureLookaheadFrames(4);
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;size_t finalRecordCursor=0;
            auto messages=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseMessages;};
            auto dump=[&] {
                std::vector<AnalysisDiagnosticRecordV1> trace;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                LogSubmittedFailureTail(trace);
                for(const auto& line:messages())Logger::WriteMessage(line.c_str());
            };
            auto require=[&](bool valid,const wchar_t* message) {if(!valid){dump();Assert::Fail(message);}};
            auto integer=[](const std::string& line,const char* key) {
                const auto at=line.find(key);return at==std::string::npos?uint64_t(0):uint64_t(std::strtoull(line.c_str()+at+strlen(key),nullptr,10));
            };

            AnalysisDiagnosticRecordV1 previousSuccessfulOutput{};bool havePreviousSuccessfulOutput=false;
            struct Geometry {uint64_t sequence=0;double left=0,top=0,right=0,bottom=0,pts=0;unsigned mode=0;bool composed=false,sourceBoundCaption=false,shaderBuilding=false,poseEligible=false;int routedTranslation=0;double poseDelta=0,minimumTop=0,maximumTop=0;};
            auto geometries=[&] {
                std::vector<Geometry> result;std::vector<AnalysisDiagnosticRecordV1> trace;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);trace=classicPoseRecords;}
                for(;finalRecordCursor<trace.size();++finalRecordCursor) {
                    const auto& record=trace[finalRecordCursor];
                    if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))continue;
                    require(record.acceptedSequence>0 && record.counters[1]==record.acceptedSequence,
                        L"Actual successful output lacks a coherent selected source identity.");
                    Geometry g;g.sequence=record.counters[1];g.pts=record.metrics[0];g.mode=unsigned(record.counters[3]);
                    g.left=record.metrics[8];g.top=record.metrics[9];g.right=record.metrics[10];g.bottom=record.metrics[11];
                    g.composed=(record.flags&2u)!=0;g.poseEligible=(record.flags&1024u)!=0;
                    g.sourceBoundCaption=IsSourceBoundComposedCaption(record,
                        havePreviousSuccessfulOutput?&previousSuccessfulOutput:nullptr,1,advancedMode,
                        1001.0/(narrower?24000.0:60000.0));
                    {
                        std::lock_guard<std::mutex> lock(classicPoseLogMutex);
                        const auto preparation=classicSubmittedShaderPreparation.find(
                            {record.rendererInstanceKey,record.transportGeneration,record.acceptedSequence});
                        g.shaderBuilding=preparation!=classicSubmittedShaderPreparation.end() &&
                            preparation->second.AllowsInitialBuildFallback(record.transportGeneration,record.sourceFormatGeneration,advancedMode);
                    }
                    previousSuccessfulOutput=record;havePreviousSuccessfulOutput=true;
                    g.poseDelta=record.metrics[2];g.routedTranslation=int(std::lround(record.metrics[4]));
                    g.minimumTop=record.metrics[6];g.maximumTop=record.metrics[7];
                    require(std::isfinite(g.pts) && g.pts>0 && std::isfinite(g.left) && std::isfinite(g.top) &&
                        std::isfinite(g.right) && std::isfinite(g.bottom) && g.left<g.right && g.top<g.bottom,
                        L"Actual successful output has invalid native source coordinates or QPC timestamps.");
                    result.push_back(g);
                }
                return result;
            };
            auto same=[](const Geometry& actual,const ActivePictureBounds& wanted) {
                return std::abs(actual.left-wanted.left)<0.01&&std::abs(actual.top-wanted.top)<0.01&&
                    std::abs(actual.right-wanted.right)<0.01&&std::abs(actual.bottom-wanted.bottom)<0.01;
            };
            auto sameSize=[&](const Geometry& g) {
                return std::abs(g.right-g.left-(settled.right-settled.left))<0.01&&
                    std::abs(g.bottom-g.top-(settled.bottom-settled.top))<0.01;
            };
            const auto sourcePeriod=std::chrono::duration<double>(1001.0/(narrower?24000.0:60000.0));
            auto nextSource=std::chrono::steady_clock::now();
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                if(callback.state==RENDERSTATE_RENDERING){const void* data=nativeV210?static_cast<const void*>(packedFrames.at(pixels.data()).data()):static_cast<const void*>(pixels.data());VideoFrame frame(data,sequence++,clock.TimingClockNow(),&buffer);cleanup.renderer->OnVideoFrame(frame);}
                nextSource+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(sourcePeriod);
                std::this_thread::sleep_until(nextSource);
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);UseCompletedSubmittedPresentation(value);return value;
            };
            auto scans=[&](uint64_t first) {
                bool scheduled=false,skipped=false;
                for(const auto& line:messages())if(line.find("Alpha source crop:")!=std::string::npos&&integer(line," sequence=")>=first) {
                    scheduled|=line.find(" dense_scan=scheduled")!=std::string::npos;skipped|=line.find(" dense_scan=skipped")!=std::string::npos;
                }
                return scheduled&&(narrower||skipped);
            };
            auto acquireNative=[&] {
                const auto first=sequence,deadlineMs=GetTickCount64()+15000;bool gotActual=false,gotNative=false;
                do {
                    const auto value=pump(native);
                    gotNative=value.available&&!value.provisional&&value.detectedSequence>=first&&std::abs(value.ratio-physical.aspectRatio)<0.005;
                    for(const auto& g:geometries())if(g.sequence>=first&&same(g,initial))gotActual=true;
                    if(gotNative&&gotActual)return;
                }while(GetTickCount64()<deadlineMs&&callback.state!=RENDERSTATE_FAILED);
                require(false,L"Normal-drift fixture failed independent native-plane acquisition or exact configured fill.");
            };
            acquireNative();
            uint64_t acceptedNativeSequence=0;
            auto nativeMatches=[&](const std::string& line) {
                Geometry g;const auto at=line.find(" native_rect=");
                return at!=std::string::npos && sscanf_s(line.c_str()+at+13,"%lf,%lf-%lf,%lf",
                    &g.left,&g.top,&g.right,&g.bottom)==4 && same(g,physical);
            };
            for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos && nativeMatches(line))
                acceptedNativeSequence=integer(line," accepted=");
            require(acceptedNativeSequence>0,L"UHD fixture did not acquire a source-owned accepted native contract.");
            auto verifyNativeContract=[&](uint64_t first) {
                for(const auto& line:messages())if(line.find("Alpha native presentation commit:")!=std::string::npos &&
                    integer(line," selected=")>=first)
                    require(integer(line," native=")==1 && nativeMatches(line) &&
                        integer(line," accepted=")==acceptedNativeSequence,
                        L"Sparse UI changed the independently accepted native picture contract.");
            };
            Logger::WriteMessage("Classic UHD live reproduction: bottom cue with no optional fill\n");
            const uint64_t onset=sequence;uint64_t settledFirst=0;double previousTop=initial.top;
            bool safeNativePanSeen=false;
            std::set<uint64_t> settledSources;std::set<int> intermediate;
            const auto engageDeadline=GetTickCount64()+10000;
            do {
                pump(caption);
                for(const auto& g:geometries())if(g.sequence>=onset) {
                    require(g.mode==0 && std::abs(g.left-physical.left)<0.01 &&
                        std::abs(g.right-physical.right)<0.01 && g.top>=-0.01 && g.bottom<=height+0.01,
                        L"UHD Classic engage changed mode, horizontal picture coverage, or raster bounds.");
                    if(settledFirst) {
                        require(same(g,settled),L"UHD Classic left its exact independently derived settled pose.");
                        settledSources.insert(g.sequence);
                        continue;
                    }
                    // Every submitted Classic frame keeps the native dimensions
                    // and contains the complete caption, including initial entry.
                    const bool nativePan=sameSize(g) && g.top>=initial.top-0.01 &&
                        g.top<=settled.top+0.01 && g.bottom>=captionBottom-0.01;
                    require(nativePan,
                        L"UHD Classic engage resized the picture or clipped current caption pixels.");
                    if(nativePan) {
                        require(g.top>=previousTop-0.01,
                            L"UHD Classic caption-safe native pan reversed its measured translation.");
                        safeNativePanSeen=true;previousTop=g.top;
                        if(g.top>initial.top+0.01 && g.top<settled.top-0.01)intermediate.insert(int(std::lround(g.top)));
                    }
                    if(same(g,settled)) {
                        settledFirst=g.sequence;
                        settledSources.insert(g.sequence);
                    }
                }
                if(settledSources.size()>=24 && scans(onset))break;
            }while(GetTickCount64()<engageDeadline && callback.state!=RENDERSTATE_FAILED);
            require(settledSources.size()>=24 && intermediate.size()>=2 && scans(onset),
                L"UHD Classic no-fill bottom cue failed its timed ramp, 24-source stable pose, or natural dense cadence.");
            if(reconstruction) {
                UnifiedProfileRuntime::SelectionResult selection;
                require(runtime.SelectKey("F6",{},selection,error) && selection.changed,
                    L"UHD Reconstruction fixture could not select its actual application profile.");
                restart=reset=false;
                require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset) && !restart,
                    L"UHD Reconstruction profile was not accepted as a live renderer update.");
                const uint64_t requested=sequence;uint64_t ready=0,observationDeadline=0,warmSettle=0;unsigned lastMode=0;
                bool warmPinned=false;int32_t warmBounds[4]={};
                std::set<uint64_t> warmSources;const auto preparationDeadline=GetTickCount64()+ColdShaderPreparationBudgetMs;
                auto records=[] {std::lock_guard<std::mutex> lock(classicPoseLogMutex);return classicPoseRecords;};
                do {
                    const auto value=pump(caption);
                    for(const auto& g:geometries())if(g.sequence>=requested) {
                        lastMode=g.mode;
                        if(ready)require(g.mode==advancedMode && g.sourceBoundCaption && same(g,initial),
                            L"Ready UHD output lost its current caption or changed the native viewport.");
                        if(g.shaderBuilding && !ready) {
                            require(same(g,initial),L"Shader building changed the accepted native picture viewport.");
                        }else if(g.mode==advancedMode) {
                            require(g.sourceBoundCaption && same(g,initial) && !g.poseEligible && std::abs(g.poseDelta)<0.01,
                                L"Shader-ready UHD output lost its current source-bound caption or native picture.");
                            if(!ready){
                                ready=g.sequence;observationDeadline=GetTickCount64()+5000;warmSettle=GetTickCount64()+500;
                                std::ostringstream status;status<<"UHD first composed native output: selected="<<ready<<" mode="<<advancedMode
                                    <<" preparation_elapsed_ms="<<(GetTickCount64()-(preparationDeadline-ColdShaderPreparationBudgetMs))
                                    <<" current_source_caption="<<g.sourceBoundCaption<<" native="<<bar<<'-'<<pictureBottom;
                                Logger::WriteMessage(status.str().c_str());
                            }
                        }
                    }
                    if(ready && value.presentedSequence>=ready) {
                        const auto trace=records();const AnalysisDiagnosticRecordV1* presentation=nullptr;

                        for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                            if(!strcmp(record.stage,"submitted-presentation"))presentation=&record;

                        }
                        require(presentation && IsExactCompletedOutput(*presentation,value.sourceGeneration,advancedMode),
                            L"Prepared UHD output lacks exact final source/mode/certificate identity.");
                        if(!crossingEdge) {
                            require(presentation->displayed[0]==initial.left && presentation->displayed[1]==initial.top &&
                                presentation->displayed[2]==initial.right && presentation->displayed[3]==initial.bottom,
                                L"Prepared UHD Reconstruction changed its native presentation during the observation window.");
                            if(lastMode==advancedMode && (presentation->flags&2u)!=0)warmSources.insert(value.presentedSequence);
                        } else {
                            const bool composed=(presentation->flags&2u)!=0;
                            require(presentation->published[0]==0 && presentation->published[1]==bar &&
                                presentation->published[2]==width && presentation->published[3]==pictureBottom,
                                L"Cross-edge initialization changed accepted native picture geometry.");
                            require(presentation->displayed[0]<=0 && presentation->displayed[2]>=width &&
                                presentation->displayed[1]<=bar && presentation->displayed[3]>=(composed?pictureBottom:captionBottom),
                                L"Cross-edge initialization lost current source content visibility.");
                            verifyNativeContract(requested);
                            if(GetTickCount64()>=warmSettle) {
                                if(!warmPinned){for(int e=0;e<4;++e)warmBounds[e]=presentation->displayed[e];warmPinned=true;}
                                for(int e=0;e<4;++e)require(warmBounds[e]==presentation->displayed[e],
                                    L"Cross-edge initialization alternated its settled safe viewport across composition states.");
                                if(lastMode==advancedMode)warmSources.insert(value.presentedSequence);
                            }
                        }
                    }
                    if(warmSources.size()>=24)break;
                }while(GetTickCount64()<(ready?observationDeadline:preparationDeadline) && callback.state!=RENDERSTATE_FAILED);
                if(!ready) {
                    std::string preparation="no shader preparation completion/rejection logged",acquisition="no raw acquisition result logged";
                    for(const auto& line:messages()) {
                        if(line.find("VP subtitle background preparation:")!=std::string::npos ||
                            line.find("Subtitle shader preparation rejected:")!=std::string::npos)preparation=line;
                        if(line.find("SUBTITLE BBOX:")!=std::string::npos)acquisition=line;
                    }
                    Logger::WriteMessage((std::string("UHD first composed native-output readiness expired: last_actual_mode=")+
                        std::to_string(lastMode)+"; "+preparation+"; "+acquisition).c_str());
                }
                require(ready>0,L"UHD Reconstruction acquired no actual mode3 composed native output within its bounded 180-second cold-preparation budget.");
                require(warmSources.size()>=24,crossingEdge?
                    L"Ready cross-edge Reconstruction did not retain one safe viewport for 24 distinct sources after acquisition.":
                    L"Ready UHD Reconstruction did not present 24 distinct actually composed native sources within its separate five-second observation budget.");
                bool cleanupIntersection=false,cleanupChangedRejection=false;
                auto inspectMovingCleanup=[&](uint64_t first,uint64_t presented) {
                    for(const auto& line:messages()) {
                        const auto frame=integer(line," frame=");if(frame<first || frame>presented)continue;
                        if(line.find("SUBTITLE CADENCE:")!=std::string::npos &&
                            line.find(" reason=cadence-current-cleanup-changed ")!=std::string::npos && integer(line," samples=")>0)
                            cleanupChangedRejection=true;
                        SubtitleBoxRect cleanup;bool submitted=false;
                        for(const auto& output:records())if(output.acceptedSequence>=first && output.acceptedSequence<=presented &&
                            TrySubmittedSubtitleCleanup(line,output,1,cleanup)){submitted=true;break;}
                        if(submitted)for(int y=(std::max)(bar,cleanup.top);y<(std::min)(pictureBottom,cleanup.bottom)&&!cleanupIntersection;++y)
                            for(int x=(std::max)(0,cleanup.left);x<(std::min)(width,cleanup.right);++x)
                                if(motionOnly[0][size_t(y)*width+x]!=motionOnly[1][size_t(y)*width+x]){cleanupIntersection=true;break;}
                    }
                };
                if(crossingEdge) {
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseAllRecordStages=true;}
                    const uint64_t motionFirst=sequence;const auto motionSettle=GetTickCount64()+500,motionDeadline=GetTickCount64()+12000;
                    std::set<uint64_t> stableMotion,composedMotion,uncomposedMotion;bool pinned=false;int32_t fixed[4]={};unsigned phase=0;
                    do {
                        const auto value=pump(motionOnly[phase++%2]);
                        for(const auto& g:geometries())if(g.sequence>=motionFirst)
                            require(g.mode==advancedMode && g.sourceBoundCaption && same(g,initial) &&
                                !g.poseEligible && std::abs(g.poseDelta)<0.01,
                                L"Moving cross-edge output lost its source-bound complete caption or fixed native picture.");
                        if(value.presentedSequence>=motionFirst) {
                            const auto trace=records();const AnalysisDiagnosticRecordV1* p=nullptr;
                            for(const auto& r:trace)if(r.acceptedSequence==value.presentedSequence) {
                                if(!strcmp(r.stage,"submitted-presentation"))p=&r;
                            }
                            require(p && IsExactCompletedOutput(*p,value.sourceGeneration,advancedMode),
                                L"Moving cross-edge output lacks exact final source/mode/certificate identity.");
                            const bool composed=(p->flags&2u)!=0;
                            require(p->published[0]==0 && p->published[1]==bar && p->published[2]==width && p->published[3]==pictureBottom,
                                L"Motion in the source erasure footprint changed accepted native picture geometry.");
                            const bool currentVisible=p->displayed[0]<=0 && p->displayed[2]>=width && p->displayed[1]<=bar &&
                                p->displayed[3]>=(composed?pictureBottom:captionBottom);
                            if(!currentVisible) {
                                std::ostringstream report;
                                report<<"CROSS EXACT-SOURCE FAILURE: presented="<<value.presentedSequence<<" next_input="<<sequence
                                    <<" source_generation="<<value.sourceGeneration<<" publication="<<value.publicationGeneration
                                    <<" detected_sequence="<<value.detectedSequence<<" detected_ratio="<<value.ratio<<" provisional="<<value.provisional
                                    <<" actual_in_use="<<value.inUseRatio<<" in_use_state="<<static_cast<unsigned>(value.inUseState)
                                    <<" qpc_now="<<clock.TimingClockNow()<<" qpc_frequency="<<clock.TimingClockTicksPerSecond()
                                    <<" phase="<<phase<<" composed_from_subtitle_output="<<composed
                                    <<" selected_p_flags="<<p->flags<<" selected_p_reason="<<p->reason
                                    <<" selected_final_flags="<<p->flags<<" selected_final_processing="<<p->counters[0]
                                    <<" selected_final_selected="<<p->counters[1]<<" selected_final_reason="<<p->reason;
                                Logger::WriteMessage(report.str().c_str());
                                // Preserve every captured stage for this exact source,
                                // including duplicate stages and their instance/context.
                                const auto failureTrace=records();
                                for(const auto& r:failureTrace)if(r.acceptedSequence==value.presentedSequence) {
                                    std::ostringstream row;
                                    row<<"CROSS RECORD: stage="<<r.stage<<" record_sequence="<<r.recordSequence<<" timestamp_ns="<<r.timestampNs
                                        <<" instance="<<r.rendererInstanceKey<<" frame_identity="<<r.frameIdentity<<" accepted="<<r.acceptedSequence
                                        <<" transport="<<r.transportGeneration<<" frame="<<r.sourceFrameNumber<<" capture_qpc="<<r.captureTimestamp
                                        <<" format="<<r.sourceFormatGeneration<<" domain="<<r.analysisDomainKey<<" viewport="<<r.viewportGeneration
                                        <<" renderer="<<r.rendererGeneration<<" policy="<<r.policyGeneration<<" continuity="<<r.continuityGeneration
                                        <<" flags="<<r.flags<<" result="<<r.resultCode<<" cadence_repeat="<<r.cadenceRepeat
                                        <<" reason="<<r.reason<<" detail="<<r.detail;
                                    const auto rect=[&](const char* label,const int32_t* b){row<<' '<<label<<'='<<b[0]<<','<<b[1]<<'-'<<b[2]<<','<<b[3];};
                                    rect("published",r.published);rect("displayed",r.displayed);rect("candidate",r.candidate);rect("subtitle",r.subtitle);
                                    for(int i=0;i<8;++i)row<<" counter"<<i<<'='<<r.counters[i];
                                    for(int i=0;i<24;++i)row<<" metric"<<i<<'='<<r.metrics[i];
                                    Logger::WriteMessage(row.str().c_str());
                                }
                                std::string latest="no submitted geometry log",selected="no geometry at/before selected source";
                                for(const auto& line:messages())if(line.find("VP submitted geometry:")!=std::string::npos) {
                                    latest=line;if(integer(line," selected=")<=value.presentedSequence)selected=line;
                                }
                                Logger::WriteMessage((std::string("CROSS LAST GEOMETRY AT/BEFORE PRESENTED: ")+selected).c_str());
                                Logger::WriteMessage((std::string("CROSS LATEST SUBMITTED GEOMETRY: ")+latest).c_str());
                            }
                            require(currentVisible,L"Moving source erasure footprint lost current picture/caption visibility.");
                            if(GetTickCount64()>=motionSettle) {
                                if(!pinned){for(int e=0;e<4;++e)fixed[e]=p->displayed[e];pinned=true;}
                                for(int e=0;e<4;++e)require(fixed[e]==p->displayed[e],
                                    L"Bottom-only moving cleanup alternated composed/uncomposed viewport size or position after acquisition.");
                                stableMotion.insert(value.presentedSequence);
                                if(composed)composedMotion.insert(value.presentedSequence);else uncomposedMotion.insert(value.presentedSequence);
                            }
                            inspectMovingCleanup(motionFirst,value.presentedSequence);
                        }
                        if(stableMotion.size()>=24 && composedMotion.size()>=24 && cleanupIntersection)break;
                    }while(GetTickCount64()<motionDeadline && callback.state!=RENDERSTATE_FAILED);
                    Logger::WriteMessage((std::string("Cross-edge bottom-only motion: stable=")+std::to_string(stableMotion.size())+
                        " composed="+std::to_string(composedMotion.size())+" uncomposed="+std::to_string(uncomposedMotion.size())+
                        " cleanup_hull_intersection="+std::to_string(cleanupIntersection)+" actual_erase_change_rejection="+std::to_string(cleanupChangedRejection)).c_str());
                    require(cleanupIntersection,
                        L"No successfully composed source cleanup intersected actual changing native picture pixels.");
                    require(stableMotion.size()>=24 && composedMotion.size()>=24,
                        L"Moving cleanup did not preserve source-bound complete captions and one fixed native viewport over24distinct sources.");
                }
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseAllRecordStages=false;}
                const uint64_t residualFirst=sequence;std::set<uint64_t> residualSources,composedSources,uncomposedSources;
                bool stableEnvelope=false;int32_t stableBounds[4]={};
                const auto stableAfter=GetTickCount64()+500;
                const auto residualDeadline=GetTickCount64()+12000;unsigned phase=0;
                do {
                    const auto value=pump(residual[phase++%2]);
                    for(const auto& g:geometries())if(g.sequence>=residualFirst) {
                        // This off-center top-bar badge is unowned UI in encoded
                        // bars, not native picture. It must not enlarge the movie
                        // viewport; the actual bottom caption remains source-owned.
                        require(g.mode==advancedMode && g.sourceBoundCaption && same(g,initial),
                            L"Residual bar UI changed the native picture or revoked current complete caption pixels.");
                    }
                    if(value.presentedSequence>=residualFirst) {
                        const auto trace=records();const AnalysisDiagnosticRecordV1* presentation=nullptr;

                        for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                            if(!strcmp(record.stage,"submitted-presentation"))presentation=&record;

                        }
                        require(presentation && IsExactCompletedOutput(*presentation,value.sourceGeneration,advancedMode),
                            L"UHD residual UI lacks exact final source/mode/certificate identity.");
                        require(presentation->published[0]==0 && presentation->published[1]==bar &&
                            presentation->published[2]==width && presentation->published[3]==pictureBottom,
                            L"Residual UI changed the source-owned native picture contract.");
                        const bool composed=(presentation->flags&2u)!=0;
                        require(composed && presentation->counters[2]==presentation->counters[1] &&
                            presentation->displayed[0]==initial.left && presentation->displayed[1]==initial.top &&
                            presentation->displayed[2]==initial.right && presentation->displayed[3]==initial.bottom,
                            L"An exact submitted caption lost source ownership or changed native framing for unrelated bar UI.");
                        verifyNativeContract(residualFirst);
                                                if(GetTickCount64()>=stableAfter) {
                            if(!stableEnvelope){for(int edge=0;edge<4;++edge)stableBounds[edge]=presentation->displayed[edge];stableEnvelope=true;}
                            for(int edge=0;edge<4;++edge)require(stableBounds[edge]==presentation->displayed[edge],
                                L"Persistent residual UI made Reconstruction alternate its settled submitted size or position.");
                            residualSources.insert(value.presentedSequence);
                            if(composed)composedSources.insert(value.presentedSequence);else uncomposedSources.insert(value.presentedSequence);
                        }
                    }
                                        if(crossingEdge)inspectMovingCleanup(residualFirst,value.presentedSequence);

                    if(residualSources.size()>=24 && composedSources.size()>=24 && (!crossingEdge || cleanupIntersection))break;
                }while(GetTickCount64()<residualDeadline && callback.state!=RENDERSTATE_FAILED);
                require(residualSources.size()>=24 && composedSources.size()>=24,
                    L"UHD moving-picture residual UI did not survive 24 distinct actually composed and presented sources.");
                Logger::WriteMessage((std::string("Residual UI observed composition states: composed=")+std::to_string(composedSources.size())+
                    " uncomposed="+std::to_string(uncomposedSources.size())+" moving_source_footprint_intersection="+std::to_string(cleanupIntersection)).c_str());
                if(crossingEdge)require(cleanupIntersection,
                    L"Cross-edge fixture acquired no actual composed cleanup rectangle intersecting changing source pixels.");
                // Exact current top-band clearance retires that edge independently.
                // The still-occupied raw bottom band may retain its safe margin
                // through composed/uncomposed outputs without changing native AR.
                if(!crossingEdge) {
                    const uint64_t topClearFirst=sequence;std::set<uint64_t> topCleared;
                    const auto topDeadline=GetTickCount64()+12000;bool pinned=false;int32_t stable[4]={};
                    do {
                        const auto value=pump(caption);
                        for(const auto& g:geometries())if(g.sequence>=topClearFirst)
                            require(g.mode==advancedMode && g.sourceBoundCaption && same(g,initial) &&
                                !g.poseEligible && std::abs(g.poseDelta)<0.01,
                                L"Top-UI clearance changed native framing or lost the current composed caption.");
                        if(value.presentedSequence>=topClearFirst) {
                            const auto trace=records();const AnalysisDiagnosticRecordV1* p=nullptr;
                            for(const auto& r:trace)if(r.acceptedSequence==value.presentedSequence) {
                                if(!strcmp(r.stage,"submitted-presentation"))p=&r;
                            }
                            require(p && IsExactCompletedOutput(*p,value.sourceGeneration,advancedMode),
                                L"Top-UI clearance lacks exact final source/mode/certificate identity.");
                            const bool composed=(p->flags&2u)!=0;
                            require(p->published[0]==0 && p->published[1]==bar && p->published[2]==width && p->published[3]==pictureBottom,
                                L"Clearing only top UI changed source-derived native geometry.");
                            require(p->displayed[0]<=0 && p->displayed[2]>=width && p->displayed[1]<=bar &&
                                p->displayed[3]>=(composed?pictureBottom:captionBottom),
                                L"Top-UI clearance hid a current bottom caption before erasure was owned.");
                            require(p->displayed[0]==0 && p->displayed[1]==bar && p->displayed[2]==width,
                                L"Exact current raw top clearance did not immediately retire the unrelated top visibility margin.");
                            if(!pinned){for(int e=0;e<4;++e)stable[e]=p->displayed[e];pinned=true;}
                            for(int e=0;e<4;++e)require(stable[e]==p->displayed[e],
                                L"After retiring the clear top edge, the occupied bottom viewport alternated with composition readiness.");
                            topCleared.insert(value.presentedSequence);
                            verifyNativeContract(topClearFirst);
                        }
                        if(topCleared.size()>=24)break;
                    }while(GetTickCount64()<topDeadline && callback.state!=RENDERSTATE_FAILED);
                    require(topCleared.size()>=24,L"Current top clearance did not retire the top margin while retaining one safe bottom viewport for 24 distinct sources.");
                }
                // Only actual raw absence of ALL bar content must retire every
                // presentation margin. An empty cue does not need composition.
                const uint64_t clearFirst=sequence;std::set<uint64_t> cleared;const auto clearDeadline=GetTickCount64()+12000;
                do {
                    const auto value=pump(native);geometries();
                    const auto trace=records();bool exact=false,rawAbsent=false;
                    for(const auto& record:trace)if(record.acceptedSequence==value.presentedSequence) {
                        if(!strcmp(record.stage,"submitted-presentation")) {
                            exact=record.displayed[0]==initial.left && record.displayed[1]==initial.top &&
                                record.displayed[2]==initial.right && record.displayed[3]==initial.bottom &&
                                record.published[0]==0 && record.published[1]==bar && record.published[2]==width && record.published[3]==pictureBottom;
                            // Source-bound current absence was frozen with THIS successful
                            // selected packet; no raw-attempt/retry trace is joined here.
                            rawAbsent=IsExactCompletedOutput(record,value.sourceGeneration,advancedMode) && (record.flags&8192u)!=0 &&
                                record.counters[1]==record.acceptedSequence && record.counters[0]==record.acceptedSequence &&
                                record.counters[3]==advancedMode && record.counters[4]==acceptedNativeSequence;
                        }
                    }
                    if(value.presentedSequence>=clearFirst && exact && rawAbsent)cleared.insert(value.presentedSequence);
                    if(cleared.size()>=24)break;
                }while(GetTickCount64()<clearDeadline && callback.state!=RENDERSTATE_FAILED);
                require(cleared.size()>=24,L"Removing all bar content did not restore 24 exact current raw-absence native outputs and retire the visibility envelope.");                require(runtime.SelectKey("F5",{},selection,error) && selection.changed,L"UHD fixture could not return to Classic.");
                restart=reset=false;require(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset) && !restart,
                    L"Reconstruction-to-Classic transition required a restart or rejected live controller intent.");
                const uint64_t classicFirst=sequence;uint64_t final=0;std::set<uint64_t> resumed;std::set<int> ramp;double lastTop=initial.top;
                bool classicSafeNativePanSeen=false;
                const auto classicDeadline=GetTickCount64()+10000;
                do {
                    const auto value=pump(caption);
                    for(const auto& g:geometries())if(g.sequence>=classicFirst && g.mode==0) {
                        const bool nativePan=sameSize(g)&&g.top>=initial.top-0.01&&g.top<=settled.top+0.01&&g.bottom>=captionBottom-0.01;
                        require(nativePan,
                            L"Returning to Classic resized the picture or clipped its fresh caption.");
                        if(nativePan){
                            const double requested=initial.top+g.routedTranslation;
                            const double projected=(std::max)(g.minimumTop,(std::min)(requested,g.maximumTop));
                            require(g.minimumTop<=g.maximumTop && g.top>=lastTop-0.01 && std::abs(g.top-projected)<0.01,
                                L"Returning Classic pan reversed or lost its source-specific caption-safe controller projection.");
                            classicSafeNativePanSeen=true;lastTop=g.top;
                            if(g.top>initial.top+0.01&&g.top<settled.top-0.01)ramp.insert(int(std::lround(g.top)));
                        }
                        if(final)require(same(g,settled),L"Returning Classic left its exact settled native-height pose.");
                        if(same(g,settled)&&!final)final=g.sequence;
                    }
                    if(final && value.presentedSequence>=final)resumed.insert(value.presentedSequence);
                    if(resumed.size()>=24)break;
                }while(GetTickCount64()<classicDeadline && callback.state!=RENDERSTATE_FAILED);
                verifyNativeContract(classicFirst);
                require(ramp.size()>=2,L"A genuinely new caption after current raw absence did not exercise the configured 500 ms Classic engage drift.");
                require(resumed.size()>=24,L"Classic controller did not resume its valid routed intent and stable final pose after Reconstruction.");
                return;
            }
            if(!bothBars)return;
            Logger::WriteMessage("Classic UHD live reproduction: opposite sparse top UI while bottom cue remains\n");
            const uint64_t bothFirst=sequence;uint64_t fitFirst=0;Geometry fixedFit;
            std::set<uint64_t> fitSources;bool scanSeen=false,skipSeen=false;
            bool unknownWithCurrentRetention=false,freshRetentionInterval=false;
            const auto bothDeadline=GetTickCount64()+10000;
            do {
                const auto value=pump(both);
                for(const auto& g:geometries())if(g.sequence>=bothFirst) {
                    require(g.mode==0 && sameSize(g) && g.left<=0.01 && g.right>=width-0.01 &&
                        g.top<=captionTop+0.01 && g.bottom>=captionBottom-0.01,
                        L"Opposite unclassified UI resized Classic's native movie canvas or clipped the bottom caption.");
                    if(!fitFirst){fitFirst=g.sequence;fixedFit=g;}
                    else require(std::abs(g.left-fixedFit.left)<0.01 && std::abs(g.top-fixedFit.top)<0.01 &&
                        std::abs(g.right-fixedFit.right)<0.01 && std::abs(g.bottom-fixedFit.bottom)<0.01,
                        L"Classic opposite UI alternated its successfully submitted fixed-size canvas.");
                }
                if(value.presentedSequence>=bothFirst) {
                    require(value.inUseAvailable && std::isfinite(value.inUseRatio) && value.inUseRatio>0 &&
                        std::abs(value.inUseRatio-double(width)/(pictureBottom-bar))<0.00001,
                        L"Unclassified opposite UI changed Classic's submitted native movie aspect.");
                    verifyNativeContract(bothFirst);

                    if(fitFirst)fitSources.insert(value.presentedSequence);
                }
                uint64_t previousIntervalSource=0;
                for(const auto& line:messages()) {
                    if(line.find("Alpha source crop:")!=std::string::npos && integer(line," sequence=")>=bothFirst) {
                        scanSeen|=line.find(" dense_scan=scheduled")!=std::string::npos;
                        skipSeen|=line.find(" dense_scan=skipped")!=std::string::npos;
                        unknownWithCurrentRetention|=integer(line," latest_evidence=")==0 &&
                            integer(line," retention_eval=")==1 && integer(line," coarse_current=")==1 &&
                            line.find(" dense_scan=skipped")!=std::string::npos &&
                            line.find(" authority=none")!=std::string::npos;
                    }
                    if(line.find("VP source measurement interval:")!=std::string::npos) {
                        const auto end=integer(line," source="),batches=integer(line," batches=");
                        const auto requests=integer(line," subtitle_requests="),hits=integer(line," subtitle_hits=");
                        if(previousIntervalSource>=bothFirst && batches>0 && requests>=hits) {
                            scanSeen|=requests-hits>0;skipSeen|=requests-hits<batches;
                            // Unknown geometry cannot authorize the Classic fit
                            // scan. Its replacement safety path must still inspect
                            // current excluded bands for every output batch over a
                            // complete interval wholly inside this UI phase.
                            freshRetentionInterval|=batches>=24 && integer(line," retention_scans=")>=batches;
                        }
                        previousIntervalSource=end;
                    }
                }
                if(fitSources.size()>=24 && ((scanSeen && skipSeen) ||
                    (unknownWithCurrentRetention && freshRetentionInterval)))break;
            }while(GetTickCount64()<bothDeadline && callback.state!=RENDERSTATE_FAILED);
            require(fitSources.size()>=24 && ((scanSeen && skipSeen) ||
                (unknownWithCurrentRetention && freshRetentionInterval)),
                L"Both-bar Classic needs 24 distinct constant-AR caption-safe outputs plus current scan cadence or explicit unknown geometry with fresh per-batch retention coverage.");
        }

        // Exercise the complete production tunnel/worker/render path. Every successful
        // output is inspected in sink order, including outputs between OSD publications.
        void VerifyPersistentDecodedDvCadence(bool beta,unsigned requestedMode,bool requireUnknownInputRepeat=false,bool nativePreflightOnly=false,bool alternatingPreflight=false)
        {
            const bool requireSpatial=SourceSpatialProductionEnabled();
            ScopedCadenceDiagnostics cadenceDiagnostics(!nativePreflightOnly&&SubtitlePreviewOverridesClassicHandling(static_cast<SubtitlePreviewMode>(requestedMode)));
            constexpr int width=3840,height=2160,pictureTop=262,pictureBottom=1898,captionBottom=1940;
            struct ProbeClock final : ITimingClock {
                LARGE_INTEGER frequency{};
                ProbeClock(){Assert::IsTrue(QueryPerformanceFrequency(&frequency)!=FALSE);}
                timingclocktime_t TimingClockNow() override {LARGE_INTEGER n{};QueryPerformanceCounter(&n);return n.QuadPart;}
                timingclocktime_t TimingClockTicksPerSecond() const override{return frequency.QuadPart;}
                const TCHAR* TimingClockDescription() override{return _T("Persistent decoded DV cadence QPC");}
            } clock;
            std::vector<BYTE> blank[2],caption[2],pressure[2],alternatingDecoded[2];
            VideoStateComPtr sourceState;
            // Forward RGB sender supplies real valid Dolby metadata. Low 12-bit
            // picture bits and parity-carrying chroma survive the actual formatter.
            for(int phase=0;phase<2;++phase) {
                DolbyVisionTestSender::SenderPicture sender(true,width,height,VideoFrameEncoding::R210);
                sender.state->eotf=EOTF::SDR;sender.state->colorspace=ColorSpace::BT_2020;
                // FULL matte0 and ink892 give backing limit74. Picture48/64
                // exceeds native black24, stays below backing74, and changes16
                // codes across sources: above the cadence cleanup tolerance8.
                for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                    const size_t at=size_t(y)*width+x;
                    sender.intensity[at]=uint16_t(y>=pictureTop&&y<pictureBottom?
                        (y>=1720?48+16*((x/64+y/32+phase)&1):300+40*((x/64+y/32+phase)&1))*4:0);
                    sender.chroma[at]=2048;sender.Pack(at);
                }
                sender.Embed(DolbyVisionTestSender::Packetize(DolbyVisionTestSender::DmBody()));
                blank[phase]=sender.input;
                for(int row=0;row<3;++row)for(int glyph=0;glyph<12;++glyph)
                    for(int y=1770+row*60;y<1820+row*60;++y)
                        for(int x=1552+glyph*64;x<1584+glyph*64;++x)
                            if(x<1558+glyph*64 || x>=1578+glyph*64 ||
                                (y>=1793+row*60&&y<1797+row*60)) {
                                const size_t at=size_t(y)*width+x;
                                sender.intensity[at]=3568;sender.Pack(at);
                            }
                sender.Embed(DolbyVisionTestSender::Packetize(DolbyVisionTestSender::DmBody()));
                caption[phase]=sender.input;
                sourceState=sender.state;
                CRGBDolbyVisionTunnelFormatter formatter;formatter.OnVideoState(sourceState);
                std::vector<BYTE> converted;
                const auto metadata=DolbyVisionTestSender::Deliver(formatter,sender,1,&converted);
                Assert::IsTrue(metadata.valid,L"Independent sender must produce a trusted Dolby picture before renderer testing.");
                AnalysisLumaSource decoded{converted.data(),converted.size(),width,height,size_t(width)*2,size_t(width)*2,
                    AnalysisLumaFormat::DecodedDolbyVision422,VideoFrameEncoding::R210,ColorSpace::BT_2020,1};
                decoded.range=AnalysisLumaRange::Full; // DmBody explicitly declares full range.
                ActivePictureEvidence spatialPreflight;
                if(requireSpatial) {
                    const auto measured=DetectSourcePictureForWorker(decoded);spatialPreflight=measured.evidence;
                    Logger::WriteMessage((std::string("Active DV spatial preflight: phase=")+std::to_string(phase)+
                        " available="+std::to_string(spatialPreflight.available)+" classification="+std::to_string(int(spatialPreflight.classification))+
                        " physical="+std::to_string(spatialPreflight.physicalEdges.estimate.top)+"-"+std::to_string(spatialPreflight.physicalEdges.estimate.bottom)+
                        " reason="+spatialPreflight.reason).c_str());
                    Assert::IsTrue(spatialPreflight.physicalEdges.estimateAvailable &&
                        spatialPreflight.physicalEdges.estimate.top==pictureTop && spatialPreflight.physicalEdges.estimate.bottom==pictureBottom,
                        L"Active spatial measurement must preserve the exact DV picture despite crossing captions.");
                }
                if(alternatingPreflight)alternatingDecoded[phase]=converted; // Immutable base copy before pressure mutation.
                AnalysisLumaSample sample;
                Assert::IsTrue(decoded.Sample(1552,1890,sample)&&sample.luma==892,
                    L"Tunnel fixture must decode the persistent native caption, not silently use SDR RGB fallback.");
                // Prove exact decoded glyph ownership and moving cleanup before
                // allocating a GPU renderer. Report actual detected line count.
                auto baseSampler=std::make_unique<SubtitleMeasurementSampler>();
                auto observations=std::make_unique<std::vector<SubtitleBoxObservation>>();observations->reserve(4);
                SubtitleBarTrackingReference prior;
                for(uint64_t index=0;index<4;++index) {
                    auto key=std::make_unique<SubtitleBoxObservation>();
                    key->identity.transportGeneration=key->identity.rendererGeneration=1;
                    key->identity.acceptedSequence=key->identity.sourceFrameNumber=index+1;
                    key->identity.captureTimestamp=1000000+index*166833;
                    key->identity.sourceFormatGeneration=key->identity.analysisDomainKey=key->identity.viewportGeneration=1;
                    key->width=width;key->height=height;key->policyGeneration=key->continuityGeneration=1;
                    key->analysisStride=1;key->nearBarDistance=20;key->scriptHint=SubtitleScriptHint::Latin;
                    key->sharedPicture.required=true;key->sharedPicture.identity=key->identity;
                    key->sharedPicture.bounds={0,pictureTop,width,pictureBottom,width,height,
                        double(width)/(pictureBottom-pictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                    auto measured=baseSampler->Measure(decoded,*key,prior,60.0,spatialPreflight);
                    if(requireSpatial)Logger::WriteMessage((std::string("Active DV caption proof: accepted=")+std::to_string(measured.subtitleArProof.accepted)+
                        " pending="+std::to_string(measured.text.confirmationPending)+" local="+std::to_string(measured.glyphLocalTop)+"/"+std::to_string(measured.glyphLocalBottom)+
                        " reason="+measured.subtitleArProof.reason).c_str());
                    if(requireSpatial && index>=2)Assert::IsTrue(measured.subtitleArProof.accepted &&
                        measured.subtitleArProof.evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured &&
                        measured.subtitleArProof.candidate.top==pictureTop && measured.subtitleArProof.candidate.bottom==pictureBottom,
                        L"Current caption occlusion must verify the supplied spatial nominee without replacing its geometry.");
                    Logger::WriteMessage((std::string("Decoded-DV base preflight: phase=")+std::to_string(phase)+
                        " sequence="+std::to_string(index+1)+" analyzed="+std::to_string(measured.analyzed)+
                        " detected="+std::to_string(measured.text.detected)+" lines="+std::to_string(measured.text.lineCount)+
                        " bounds="+std::to_string(measured.text.bounds.top)+"-"+std::to_string(measured.text.bounds.bottom)+
                        " components="+std::to_string(measured.text.diagnosticComponentCount)+
                        " searched="+std::to_string(measured.text.topSearchComplete)+"/"+std::to_string(measured.text.bottomSearchComplete)+
                        " bar_authority="+std::to_string(measured.barAuthority)+
                        " picture="+std::to_string(measured.pictureTop)+"-"+std::to_string(measured.pictureBottom)+
                        " shared_valid="+std::to_string(key->sharedPicture.AvailableFor(key->identity,width,height))+
                        " probe="+measured.sharedProbeReason+" reason="+measured.text.diagnosticReason).c_str());
                    Assert::IsTrue(measured.analyzed&&measured.text.detected&&!measured.text.workLimit,
                        L"Decoded crossing-caption fixture did not acquire real glyphs; no GPU regression coverage claimed.");
                    prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);observations->push_back(std::move(measured));
                }
                auto preview=std::make_unique<SubtitleBoxPreview>();
                *preview=SubtitleBoxLookahead::Resolve(observations->data(),observations->size(),1,1);
                auto presentation=std::make_unique<SubtitleBoxPresentation>();presentation->RefreshReferencePixels(*preview,decoded);
                const auto cue=presentation->Consume(*preview,250,1000.0/60);
                Logger::WriteMessage((std::string("Decoded-DV three-line ownership: lines=")+std::to_string(cue.lineCount)+
                    " bounds="+std::to_string(cue.bounds.top)+"-"+std::to_string(cue.bounds.bottom)).c_str());
                Assert::IsTrue(cue.detected&&!cue.confirmationPending&&cue.lineCount==3&&cue.bounds.top<=1770&&cue.bounds.bottom>=1940,
                    L"Decoded base fixture must own all three backed source lines from1770 through1940; partial-line coverage cannot qualify.");
                auto padding=std::make_unique<SubtitlePaddingGuard>();SubtitlePaddingPolicy policy;
                policy.glyphRelative=true;policy.minimumHeadroomPixels=30;
                const auto safe=padding->Consume(cue,*preview,{60,40,21},policy);const auto display=padding->DisplayBounds();
                auto compositor=std::make_unique<SubtitleCutPastePresentation>();
                auto geometry=compositor->Consume(cue,*preview,safe,10,&display);
                geometry=FitSubtitleToVisiblePicture(geometry,{0,pictureTop,width,pictureBottom},10);
                bool movingCleanup=false;
                auto inspectCleanup=[&](const SubtitleBoxRect& rectangle) {
                    for(int y=(std::max)(pictureTop,rectangle.top);y<(std::min)(pictureBottom,rectangle.bottom)&&!movingCleanup;++y)
                        for(int x=(std::max)(0,rectangle.left);x<(std::min)(width,rectangle.right);++x) {
                            AnalysisLumaSample point;
                            const int expected=y>=1720?48+16*((x/64+y/32+phase)&1):300+40*((x/64+y/32+phase)&1);
                            if(decoded.Sample(x,y,point)&&point.luma==expected){movingCleanup=true;break;}
                        }
                };
                if(geometry.generatedCleanupRowCount)for(unsigned row=0;row<geometry.generatedCleanupRowCount;++row)
                    inspectCleanup(geometry.generatedCleanupRows[row]);
                else inspectCleanup(geometry.generatedCleanup);
                Logger::WriteMessage((std::string("Decoded-DV exact cleanup preflight: lines=")+std::to_string(cue.lineCount)+
                    " cleanup="+std::to_string(geometry.generatedCleanup.top)+"-"+std::to_string(geometry.generatedCleanup.bottom)+
                    " rows="+std::to_string(geometry.generatedCleanupRowCount)+" moving_picture="+std::to_string(movingCleanup)).c_str());
                Assert::IsTrue(geometry.valid&&movingCleanup,
                    L"Decoded-DV fixture lacks measured cleanup over changing picture pixels; benign bar-only coverage cannot qualify.");
                if(requireUnknownInputRepeat) {
                    // A genuinely inconclusive source, independent of worker timing:
                    // the real detector bounds noisy-frame work at 768 components.
                    for(int row=0;row<3;++row)for(int column=0;column<320;++column)
                        for(int y=1908+row*12;y<1912+row*12;++y)
                            for(int x=column*12;x<column*12+4;++x) {
                                const size_t at=size_t(y)*width+x;
                                sender.intensity[at]=3568;sender.Pack(at);
                            }
                    sender.Embed(DolbyVisionTestSender::Packetize(DolbyVisionTestSender::DmBody()));
                    pressure[phase]=sender.input;
                    DolbyVisionTestSender::Deliver(formatter,sender,2,&converted);
                    AnalysisLumaSource unknown{converted.data(),converted.size(),width,height,size_t(width)*2,size_t(width)*2,
                        AnalysisLumaFormat::DecodedDolbyVision422,VideoFrameEncoding::R210,ColorSpace::BT_2020,1};
                    unknown.range=AnalysisLumaRange::Full;
                    auto key=std::make_unique<SubtitleBoxObservation>();
                    key->identity.transportGeneration=key->identity.rendererGeneration=1;
                    key->identity.acceptedSequence=key->identity.sourceFrameNumber=1;
                    key->identity.captureTimestamp=1;key->identity.sourceFormatGeneration=1;
                    key->identity.analysisDomainKey=1;key->identity.viewportGeneration=1;
                    key->width=width;key->height=height;key->policyGeneration=key->continuityGeneration=1;
                    key->analysisStride=1;key->nearBarDistance=20;key->scriptHint=SubtitleScriptHint::Latin;
                    key->sharedPicture.required=true;key->sharedPicture.identity=key->identity;
                    key->sharedPicture.bounds={0,pictureTop,width,pictureBottom,width,height,
                        double(width)/(pictureBottom-pictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                    auto sampler=std::make_unique<SubtitleMeasurementSampler>();
                    const auto measured=std::make_unique<SubtitleBoxObservation>(sampler->Measure(unknown,*key,{},60.0));
                    Logger::WriteMessage((std::string("Actual-repeat source preflight: analyzed=")+std::to_string(measured->analyzed)+
                        " work_limit="+std::to_string(measured->text.workLimit)+" reason="+measured->text.diagnosticReason+
                        " components="+std::to_string(measured->text.diagnosticComponentCount)).c_str());
                    Assert::IsTrue(measured->analyzed&&measured->text.workLimit&&
                        !strcmp(measured->text.diagnosticReason,"component-budget"),
                        L"Real decoded source must independently prove component-budget UNKNOWN; no timing-only repeat coverage is claimed.");
                }
            }
            if(alternatingPreflight) {
                auto sampler=std::make_unique<SubtitleMeasurementSampler>();
                auto observations=std::make_unique<std::vector<SubtitleBoxObservation>>();observations->reserve(12);
                SubtitleBarTrackingReference prior;
                const auto sourceFor=[&](unsigned phase) {
                    auto& pixels=alternatingDecoded[phase];
                    AnalysisLumaSource source{pixels.data(),pixels.size(),width,height,size_t(width)*2,size_t(width)*2,
                        AnalysisLumaFormat::DecodedDolbyVision422,VideoFrameEncoding::R210,ColorSpace::BT_2020,1};
                    source.range=AnalysisLumaRange::Full;return source;
                };
                for(uint64_t sequence=1;sequence<=12;++sequence) {
                    auto key=std::make_unique<SubtitleBoxObservation>();
                    key->identity.transportGeneration=key->identity.rendererGeneration=1;
                    key->identity.acceptedSequence=key->identity.sourceFrameNumber=sequence;
                    key->identity.captureTimestamp=1000000+(sequence-1)*10000000/60;
                    key->identity.sourceFormatGeneration=key->identity.analysisDomainKey=key->identity.viewportGeneration=1;
                    key->width=width;key->height=height;key->policyGeneration=key->continuityGeneration=1;
                    key->analysisStride=1;key->nearBarDistance=20;key->scriptHint=SubtitleScriptHint::Latin;
                    key->sharedPicture.required=true;key->sharedPicture.identity=key->identity;
                    key->sharedPicture.bounds={0,pictureTop,width,pictureBottom,width,height,
                        double(width)/(pictureBottom-pictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
                    const auto source=sourceFor(unsigned(sequence%2));
                    for(int y:{1770,1830,1890}){AnalysisLumaSample glyph;
                        Assert::IsTrue(source.Sample(1552,y,glyph)&&glyph.luma==892,L"Owned glyph luminance must remain unchanged in every source.");}
                    auto measured=sampler->Measure(source,*key,prior,60.0);
                    Assert::IsTrue(measured.analyzed&&!measured.text.workLimit&&measured.text.detected&&measured.text.lineCount==3,
                        L"Each alternating source requires its own complete three-line acquisition.");
                    prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);observations->push_back(std::move(measured));
                }
                auto presentation=std::make_unique<SubtitleBoxPresentation>();
                auto padding=std::make_unique<SubtitlePaddingGuard>();SubtitlePaddingPolicy paddingPolicy;
                paddingPolicy.glyphRelative=true;paddingPolicy.minimumHeadroomPixels=30;
                auto compositor=std::make_unique<SubtitleCutPastePresentation>();
                for(size_t index=0;index+4<=observations->size();++index) {
                    auto preview=std::make_unique<SubtitleBoxPreview>();
                    *preview=SubtitleBoxLookahead::Resolve(observations->data()+index,4,1,1);
                    const auto sequence=uint64_t(index+1);const auto source=sourceFor(unsigned(sequence%2));
                    Logger::WriteMessage((std::string("Alternating decoded-DV confirmation: sequence=")+std::to_string(sequence)+
                        " future="+std::to_string(preview->futureAvailable)+" matching="+std::to_string(preview->matchingFrames)+
                        " lines_confirmed="+std::to_string(preview->currentLinesConfirmed)).c_str());
                    Assert::IsTrue(preview->available&&preview->futureAvailable&&preview->matchingFrames>=2&&preview->currentLinesConfirmed,
                        L"Changing nonglyph cleanup pixels cannot prevent matching unchanged current three-line glyphs across source parity.");
                    presentation->RefreshReferencePixels(*preview,source);
                    const auto cue=presentation->Consume(*preview,250,1000.0/60);
                    Assert::IsTrue(cue.detected&&!cue.held&&!cue.confirmationPending&&cue.lineCount==3&&
                        cue.bounds.top<=1770&&cue.bounds.bottom>=1940,
                        L"Every alternating current source must confirm all three lines without held history or relaxed confirmation.");
                    const auto safe=padding->Consume(cue,*preview,{60,40,21},paddingPolicy);const auto display=padding->DisplayBounds();
                    const auto geometry=FitSubtitleToVisiblePicture(compositor->Consume(cue,*preview,safe,10,&display),
                        {0,pictureTop,width,pictureBottom},10);
                    bool changedCleanup=false;const auto other=sourceFor(unsigned(1-sequence%2));
                    const auto inspect=[&](const SubtitleBoxRect& rectangle) {
                        for(int y=(std::max)(pictureTop,rectangle.top);y<(std::min)(pictureBottom,rectangle.bottom)&&!changedCleanup;++y)
                            for(int x=(std::max)(0,rectangle.left);x<(std::min)(width,rectangle.right);++x) {
                                AnalysisLumaSample a,b;
                                if(source.Sample(x,y,a)&&other.Sample(x,y,b)&&a.luma!=892&&b.luma!=892&&
                                    std::abs(int(a.luma)-int(b.luma))==16){changedCleanup=true;break;}
                            }
                    };
                    if(geometry.generatedCleanupRowCount)for(unsigned row=0;row<geometry.generatedCleanupRowCount;++row)
                        inspect(geometry.generatedCleanupRows[row]);
                    else inspect(geometry.generatedCleanup);
                    Assert::IsTrue(geometry.valid&&changedCleanup,
                        L"Each confirmed alternating source must retain a genuine changing nonglyph cleanup footprint in native picture.");
                }
            }
            if(nativePreflightOnly)return;
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ClassicPoseTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring root(modulePath);root.resize(root.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            root+=L"\\..";
#endif
            // A read-only override permits the SAME harness to prove RED using
            // a preserved/deployed old matching plugin+dependency directory.
            char* overrideValue=nullptr;size_t overrideSize=0;
            Assert::AreEqual(0,_dupenv_s(&overrideValue,&overrideSize,"VP_RENDERER_REGRESSION_PLUGIN_ROOT"));
            std::unique_ptr<char,decltype(&std::free)> overrideOwner(overrideValue,&std::free);
            if(overrideValue&&*overrideValue){const std::string path(overrideValue);root.assign(path.begin(),path.end());}
            const auto pluginPath=root+(beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":L"\\vprenderer\\VideoProcessorVPRenderer.dll");
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup{HMODULE module;~ModuleCleanup(){FreeLibrary(module);}} moduleCleanup{plugin};
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
            Assert::IsTrue(create&&destroy&&diagnostics);
            struct DiagnosticCleanup{VideoProcessorLibplaceboConfigureDiagnosticFn configure;
                ~DiagnosticCleanup(){configure(nullptr,nullptr,nullptr,nullptr);}} diagnosticCleanup{diagnostics};
            for(const unsigned duration:{1001u,1000u}) {
                // Source cadence is independent of the current output display.
                constexpr unsigned scale=60000;
                const double inputHz=double(scale)/duration;
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();ResetShaderPreparationHistory(classicSubmittedShaderPreparation,classicCurrentShaderPreparation);classicPoseMessages.clear();classicPoseAllRecordStages=true;}
                diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
                ClassicPreparationLogForwarder preparationLog;
                char temp[MAX_PATH]={},configPath[MAX_PATH]={};
                Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0&&GetTempFileNameA(temp,"dvc",0,configPath)!=0);
                const char* initialType=requestedMode==unsigned(SubtitlePreviewMode::GeneratedGrayBackground)?"generated_gray":
                    requestedMode==unsigned(SubtitlePreviewMode::BlackBackground)?"black":"classic";
                // A cadence test starts with exactly one intended profile. Live
                // profile switching has its own independent lifecycle coverage.
                std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                    "[vprenderer]\nquality: fast\n"
                    "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                    "[vprenderer.zoom.scope]\nautomatic_crop: true\nsubtitle_fit: true\n"
                    "[vprenderer.subtitles.cadence]\ntype: "<<initialType<<"\nshortcut: F5\noffset_pixels: "<<(requestedMode?10:15)<<"\n"
                    "subtitle_engage_drift_ms: 500\nsubtitle_release_drift_ms: 2000\nsubtitle_hold_seconds: 2\nsubtitle_target_buffer_pixels: 10\n"
                    "subtitle_hold_ms: 250\nsubtitle_near_bar_px: 20\ncleanup_fill: stable_surround\nminimum_headroom_pixels: 30\n";
                SnapshotTestCallback callback;NativeP210Buffer buffer;
                struct Cleanup{HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;const char* path;
                    ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(path);}}
                    cleanup{CreateWindowExW(0,L"STATIC",L"Persistent native Dolby cadence",WS_POPUP,0,0,2000,1125,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
                Assert::IsNotNull(cleanup.window);
                cleanup.renderer=create(&callback,69,cleanup.window,&clock,true,16,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,ClassicPoseTestLog);
                Assert::IsNotNull(cleanup.renderer);
                ConfigFile config;Assert::IsTrue(config.Load(configPath));UnifiedProfileRuntime::Runtime runtime;std::string error;
                Assert::IsTrue(runtime.Initialize(config,{},error));CString active;bool restart=false,reset=false;
                const auto initialSnapshot=runtime.GetSnapshot();
                const auto initialSelection=initialSnapshot->effectiveSelections.find("subtitles");
                Assert::IsTrue(initialSelection!=initialSnapshot->effectiveSelections.end()&&initialSelection->second.size()==1&&
                    initialSelection->second.front()=="cadence",L"The cadence fixture must initially resolve its sole intended subtitle profile.");
                Logger::WriteMessage((std::string("Cadence initial snapshot: generation=")+std::to_string(initialSnapshot->generation)+
                    " profile=cadence type="+initialType+" expected_mode="+std::to_string(requestedMode)).c_str());
                Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*initialSnapshot,active,restart,reset)&&!restart);
                cleanup.renderer->SetQueueFramePolicy(4,9,true);cleanup.renderer->SetActivePictureLookaheadFrames(4);
                VideoStateComPtr state=new VideoState(*sourceState);state->displayMode=std::make_shared<DisplayMode>(width,height,false,scale,duration);
                Assert::IsTrue(cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();cleanup.renderer->Start();
                uint64_t sequence=1;size_t cursor=0;std::map<uint64_t,timingclocktime_t> delivered;
                std::map<uint64_t,AnalysisDiagnosticRecordV1> nativeInputs;
                std::vector<AnalysisDiagnosticRecordV1> outputs;
                std::vector<ClassicShaderPreparationSelection> outputPreparation;
                std::set<uint64_t> repeatedAnchors;
                PersistentCaptionProofIndex captionProofs;
                // Numeric-only bounded producer telemetry. Formatting happens
                // only at final success/failure reporting, never per input.
                struct ArrivalTiming {uint64_t source=0;int64_t capture=0,entered=0,returned=0,pump=0,messages=0,
                    sleepStarted=0,slept=0,drainStarted=0,drained=0,flushed=0,previous=0;};
                auto arrivalStorage=std::make_unique<std::array<ArrivalTiming,64>>();
                std::array<ArrivalTiming,9> pressureArrivals{};
                uint64_t pressureInput=0;
                uint64_t arrivalCount=0,fastArrivalGaps=0;int64_t firstArrival=0,firstCapture=0,lastArrival=0,lastCapture=0;
                int64_t longestArrivalGap=0,longestOnFrame=0,longestPump=0;
                const auto arrivalFrequency=clock.TimingClockTicksPerSecond();
                const auto arrivalPeriod=static_cast<int64_t>(uint64_t(arrivalFrequency)*duration/scale);
                auto reportProducerTiming=[&](bool failure) {
                    const auto failureQpc=clock.TimingClockNow();
                    if(pressureInput) {
                        for(const auto& a:pressureArrivals)if(a.source) {
                            std::ostringstream line;line<<"DV PRESSURE ARRIVAL: input="<<a.source
                                <<" capture_qpc="<<a.capture<<" entered_qpc="<<a.entered<<" returned_qpc="<<a.returned
                                <<" slept_qpc="<<a.slept<<" drained_qpc="<<a.drained<<" flushed_qpc="<<a.flushed;
                            Logger::WriteMessage(line.str().c_str());
                        }
                        for(const auto& output:outputs)if(output.counters[0]+4>=pressureInput && output.counters[0]<=pressureInput+4) {
                            const auto selected=nativeInputs.find(output.counters[1]);
                            std::ostringstream line;line.setf(std::ios::fixed);line.precision(9);
                            line<<"DV PRESSURE OUTPUT: consumed="<<output.counters[0]<<" selected="<<output.counters[1]
                                <<" ordinal="<<output.recordSequence<<" output_timestamp_ns=";
                            if(output.timestampNs)line<<output.timestampNs;else line<<"unavailable";
                            if(selected!=nativeInputs.end()) {
                                const auto& original=selected->second;
                                line<<" selected_input_metadata=joined original_frame="<<original.sourceFrameNumber
                                    <<" capture_qpc="<<original.captureTimestamp<<" input_continuity="<<original.continuityGeneration
                                    <<" input_generation="<<original.transportGeneration<<" input_domain="<<original.analysisDomainKey;
                            } else line<<" selected_input_metadata=unavailable";
                            line<<" flags="<<output.flags<<" source_pts="<<output.metrics[0]<<" output_pts="<<output.metrics[1];
                            Logger::WriteMessage(line.str().c_str());
                        }
                    }
                    const auto milliseconds=[&](int64_t a,int64_t b){return a>0&&b>=a?1000.0*double(b-a)/arrivalFrequency:-1.0;};
                    std::ostringstream timingSummary;timingSummary<<"DV PRODUCER TIMING: samples="<<arrivalCount
                        <<" input_hz="<<inputHz<<" qpc_hz="<<arrivalFrequency<<" observed_qpc="<<failureQpc
                        <<" outcome="<<(failure?"failure":"success")
                        <<" actual_input_hz="<<(lastArrival>firstArrival?double(arrivalCount-1)*arrivalFrequency/(lastArrival-firstArrival):0)
                        <<" fast_gaps_below_half_period="<<fastArrivalGaps
                        <<" max_arrival_gap_ms="<<(1000.0*longestArrivalGap/arrivalFrequency)
                        <<" max_onframe_ms="<<(1000.0*longestOnFrame/arrivalFrequency)
                        <<" max_completed_pump_ms="<<(1000.0*longestPump/arrivalFrequency)
                        <<" actual_elapsed_ms="<<milliseconds(firstArrival,lastArrival)
                        <<" nominal_elapsed_ms="<<milliseconds(firstCapture,lastCapture);
                    Logger::WriteMessage(timingSummary.str().c_str());
                    if(!failure && pressureInput) {
                        std::vector<std::string> queueDiagnostics;
                        {std::lock_guard<std::mutex> guard(classicPoseLogMutex);
                            for(const auto& line:classicPoseMessages)
                                if(line.find("Alpha queue ")!=std::string::npos ||
                                    line.find("Alpha backlog recovery:")!=std::string::npos)
                                    queueDiagnostics.push_back(line);
                        }
                        for(const auto& line:queueDiagnostics)Logger::WriteMessage(line.c_str());
                    }
                    if(!failure)return;
                    const auto arrivalBegin=arrivalCount>arrivalStorage->size()?arrivalCount-arrivalStorage->size():0;
                    for(uint64_t index=arrivalBegin;index<arrivalCount;++index) {
                        const auto& a=(*arrivalStorage)[size_t(index%arrivalStorage->size())];
                        std::ostringstream timing;timing<<"DV PRODUCER ARRIVAL: input="<<a.source<<" capture_qpc="<<a.capture
                            <<" entered_qpc="<<a.entered<<" returned_qpc="<<a.returned
                            <<" arrival_gap_ms="<<milliseconds(a.previous,a.entered)
                            <<" initial_render_offset_ms="<<(1000.0*double(firstArrival-firstCapture)/arrivalFrequency)
                            <<" schedule_lag_ms="<<(1000.0*double(a.entered-a.capture)/arrivalFrequency)
                            <<" adjusted_lag_ms="<<(1000.0*double((a.entered-firstArrival)-(a.capture-firstCapture))/arrivalFrequency)
                            <<" onframe_ms="<<milliseconds(a.entered,a.returned)
                            <<" message_ms="<<milliseconds(a.pump,a.messages)
                            <<" sleep_ms="<<milliseconds(a.sleepStarted,a.slept)
                            <<" drain_ms="<<milliseconds(a.drainStarted,a.drained)
                            <<" flush_ms="<<milliseconds(a.drained,a.flushed)
                            <<" in_progress_drain_ms="<<(!a.drained&&a.drainStarted?milliseconds(a.drainStarted,failureQpc):-1.0);
                        Logger::WriteMessage(timing.str().c_str());
                    }
                };
                auto require=[&](bool ok,const wchar_t* why){if(ok)return;
                    reportProducerTiming(true);
                    std::vector<AnalysisDiagnosticRecordV1> failureRecords;std::vector<std::string> failureMessages;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);failureRecords=classicPoseRecords;failureMessages=classicPoseMessages;}
                    size_t firstRecord=failureRecords.size(),remaining=80;
                    for(size_t i=failureRecords.size();i>0;--i) {
                        const auto& record=failureRecords[i-1];
                        if(!strcmp(record.stage,"submitted-presentation")&&(record.flags&1u)) {
                            firstRecord=i-1;if(--remaining==0)break;
                        }
                    }
                    Logger::WriteMessage("DV FAILURE FINAL OUTPUTS BEGIN (up to80; original sink order, no fabricated timestamps)");
                    for(size_t i=firstRecord;i<failureRecords.size();++i) {
                        const auto& record=failureRecords[i];
                        if(strcmp(record.stage,"submitted-presentation")||!(record.flags&1u))continue;
                        const AnalysisDiagnosticRecordV1* current=nullptr;const AnalysisDiagnosticRecordV1* selected=nullptr;
                        const AnalysisDiagnosticRecordV1* chain=nullptr;const AnalysisDiagnosticRecordV1* subtitle=nullptr;
                        for(const auto& candidate:failureRecords) {
                            if(candidate.transportGeneration!=record.transportGeneration ||
                                candidate.rendererInstanceKey!=record.rendererInstanceKey)continue;
                            if(!strcmp(candidate.stage,"analysis-input")) {
                                if(candidate.acceptedSequence==record.counters[0])current=&candidate;
                                if(candidate.acceptedSequence==record.counters[1])selected=&candidate;
                            }
                            if(candidate.acceptedSequence!=record.counters[0])continue;
                            if(!strcmp(candidate.stage,"ar-boundary-chain"))chain=&candidate;
                            if(!strcmp(candidate.stage,"subtitle-output"))subtitle=&candidate;
                        }
                        std::ostringstream row;row<<"DV FINAL: generation="<<record.transportGeneration
                            <<" accepted_displayed="<<record.acceptedSequence<<" consumed="<<record.counters[0]
                            <<" selected="<<record.counters[1]<<" consumed_input="<<(current?current->sourceFrameNumber:0)
                            <<" displayed_input="<<(selected?selected->sourceFrameNumber:0)
                            <<" flags="<<record.flags<<" repeat="<<record.cadenceRepeat<<" reason="<<record.reason
                            <<" mode="<<record.counters[3]<<" cue="<<record.counters[2]<<" native="<<record.counters[4]
                            <<" cert="<<record.counters[5]<<" cert_origin="<<record.counters[6]<<" packet="<<record.counters[7]
                            <<" viewport="<<record.viewportGeneration<<" domain="<<record.analysisDomainKey
                            <<" format="<<record.sourceFormatGeneration<<" policy="<<(chain?chain->policyGeneration:0)
                            <<" continuity="<<(chain?chain->continuityGeneration:0)<<" chain_present="<<(chain!=nullptr)
                            <<" selected_pts="<<record.metrics[0]<<" current_pts="<<record.metrics[5];
                        row<<" native_rect=";for(int e=0;e<4;++e)row<<record.published[e]<<',';
                        row<<" actual_crop=";for(int e=8;e<12;++e)row<<record.metrics[e]<<',';
                        row<<" certificate_crop=";for(int e=0;e<4;++e)row<<record.candidate[e]<<',';
                        if(subtitle)row<<" subtitle_flags="<<subtitle->flags<<" subtitle_reason="<<subtitle->reason
                            <<" subtitle_decision="<<subtitle->detail<<" accepted_lines="<<subtitle->resultCode
                            <<" raw_lines="<<subtitle->counters[4]<<" components="<<subtitle->counters[5]
                            <<" matching="<<subtitle->counters[3]<<" ink_context="<<subtitle->counters[6]
                            <<" ink_preconditions="<<subtitle->counters[7]<<" ink_compared="<<subtitle->metrics[4];
                        Logger::WriteMessage(row.str().c_str());
                    }
                    Logger::WriteMessage("DV FAILURE FINAL OUTPUTS END");
                    // Worker callbacks identify the worker object, not the renderer
                    // Impl. Join their exact source domain instead of filtering the
                    // worker's instance pointer against the output's renderer. Callback
                    // ordinals establish ordering only; Emit timestamps are unstamped.
                    const AnalysisDiagnosticRecordV1* failedOutput=nullptr;const AnalysisDiagnosticRecordV1* failedInput=nullptr;
                    for(auto it=failureRecords.rbegin();it!=failureRecords.rend();++it)
                        if(!strcmp(it->stage,"submitted-presentation")&&(it->flags&1u)){failedOutput=&*it;break;}
                    if(failedOutput)for(auto it=failureRecords.rbegin();it!=failureRecords.rend();++it)
                        if(!strcmp(it->stage,"analysis-input")&&it->transportGeneration==failedOutput->transportGeneration&&
                            it->rendererInstanceKey==failedOutput->rendererInstanceKey&&
                            it->acceptedSequence==failedOutput->counters[0]){failedInput=&*it;break;}
                    if(failedInput) {
                        const uint64_t low=failedInput->sourceFrameNumber>15?failedInput->sourceFrameNumber-15:0;
                        const uint64_t high=failedInput->sourceFrameNumber+5;
                        std::vector<const AnalysisDiagnosticRecordV1*> nearby;
                        for(const auto& r:failureRecords)if(!strncmp(r.stage,"worker-",7)&&
                            r.transportGeneration==failedInput->transportGeneration&&
                            r.sourceFormatGeneration==failedInput->sourceFormatGeneration&&
                            r.analysisDomainKey==failedInput->analysisDomainKey&&
                            r.sourceFrameNumber>=low&&r.sourceFrameNumber<=high)nearby.push_back(&r);
                        std::ostringstream header;header<<"DV FAILURE WORKER WINDOW BEGIN: consumed="<<failedOutput->counters[0]
                            <<" input="<<failedInput->sourceFrameNumber<<" range="<<low<<'-'<<high
                            <<" matched="<<nearby.size()<<" emitted="<<(std::min)(size_t(160),nearby.size());
                        Logger::WriteMessage(header.str().c_str());
                        const size_t begin=nearby.size()>160?nearby.size()-160:0;
                        for(size_t i=begin;i<nearby.size();++i) {
                            const auto& r=*nearby[i];std::ostringstream row;
                            row<<"DV WORKER: stage="<<r.stage<<" callback_ordinal="<<r.recordSequence
                                <<" worker="<<r.rendererInstanceKey<<" generation="<<r.transportGeneration
                                <<" renderer_generation="<<r.rendererGeneration<<" accepted="<<r.acceptedSequence
                                <<" source_frame="<<r.sourceFrameNumber<<" capture="<<r.captureTimestamp
                                <<" format="<<r.sourceFormatGeneration<<" domain="<<r.analysisDomainKey
                                <<" viewport="<<r.viewportGeneration<<" policy="<<r.policyGeneration
                                <<" continuity="<<r.continuityGeneration<<" flags="<<r.flags
                                <<" mismatch="<<r.counters[0]<<" analysis_ms="<<r.metrics[0]
                                <<" lines="<<r.resultCode<<" reason="<<r.reason;
                            Logger::WriteMessage(row.str().c_str());
                        }
                        Logger::WriteMessage("DV FAILURE WORKER WINDOW END");
                        // Worker and renderer instances differ. Join exact original
                        // source context and preserve callback order, never relabel
                        // a later AR publication as proof for an earlier output.
                        std::vector<const AnalysisDiagnosticRecordV1*> arNearby;
                        for(const auto& r:failureRecords)if(
                            (!strcmp(r.stage,"ar-decision") || !strcmp(r.stage,"accepted-picture-detector") ||
                             !strcmp(r.stage,"source-picture-active")) &&
                            r.transportGeneration==failedInput->transportGeneration &&
                            r.sourceFormatGeneration==failedInput->sourceFormatGeneration &&
                            r.analysisDomainKey==failedInput->analysisDomainKey &&
                            r.sourceFrameNumber>=low && r.sourceFrameNumber<=high)arNearby.push_back(&r);
                        std::ostringstream arHeader;
                        arHeader<<"DV FAILURE AR WINDOW BEGIN: consumed="<<failedOutput->counters[0]
                            <<" selected="<<failedOutput->counters[1]<<" original_input="<<failedInput->sourceFrameNumber
                            <<" range="<<low<<'-'<<high<<" matched="<<arNearby.size()
                            <<" emitted="<<(std::min)(size_t(80),arNearby.size());
                        Logger::WriteMessage(arHeader.str().c_str());
                        const size_t arBegin=arNearby.size()>80?arNearby.size()-80:0;
                        for(size_t i=arBegin;i<arNearby.size();++i) {
                            const auto& r=*arNearby[i];std::ostringstream row;
                            row<<"DV AR: stage="<<r.stage<<" callback_ordinal="<<r.recordSequence
                                <<" instance="<<r.rendererInstanceKey<<" generation="<<r.transportGeneration
                                <<" renderer_generation="<<r.rendererGeneration<<" accepted="<<r.acceptedSequence
                                <<" source_frame="<<r.sourceFrameNumber<<" capture="<<r.captureTimestamp
                                <<" format="<<r.sourceFormatGeneration<<" domain="<<r.analysisDomainKey
                                <<" viewport="<<r.viewportGeneration<<" policy="<<r.policyGeneration
                                <<" continuity="<<r.continuityGeneration<<" flags="<<r.flags
                                <<" result="<<r.resultCode<<" counter0="<<r.counters[0]
                                <<" counter1="<<r.counters[1]<<" metric0="<<r.metrics[0]
                                <<" reason="<<r.reason<<" detail="<<r.detail;
                            const auto rect=[&](const char* name,const int32_t* values) {
                                row<<' '<<name<<'='<<values[0]<<','<<values[1]<<','<<values[2]<<','<<values[3];
                            };
                            rect("measured",r.measured);rect("candidate",r.candidate);
                            rect("published",r.published);rect("displayed",r.displayed);
                            Logger::WriteMessage(row.str().c_str());
                        }
                        Logger::WriteMessage("DV FAILURE AR WINDOW END");
                        std::vector<const AnalysisDiagnosticRecordV1*> seedAttempts;
                        for(const auto& record:failureRecords)if(!strcmp(record.stage,"subtitle-pending-seed") &&
                            record.rendererInstanceKey==failedInput->rendererInstanceKey &&
                            record.transportGeneration==failedInput->transportGeneration &&
                            record.sourceFormatGeneration==failedInput->sourceFormatGeneration &&
                            record.analysisDomainKey==failedInput->analysisDomainKey &&
                            record.sourceFrameNumber>=low && record.sourceFrameNumber<=high)seedAttempts.push_back(&record);
                        const size_t seedBegin=seedAttempts.size()>32?seedAttempts.size()-32:0;
                        for(size_t i=seedBegin;i<seedAttempts.size();++i) {
                            const auto& record=*seedAttempts[i];std::ostringstream row;
                            row<<"DV PENDING SEED: callback_ordinal="<<record.recordSequence<<" source="<<record.acceptedSequence
                                <<" policy="<<record.policyGeneration<<" continuity="<<record.continuityGeneration
                                <<" flags="<<record.flags<<" raw_lines="<<record.counters[0]<<" resolved_lines="<<record.counters[1]
                                <<" accepted_lines="<<record.counters[2]<<" previous_verified="<<record.counters[3]
                                <<" last_verified="<<record.counters[4]<<" current_observation="<<record.counters[5]
                                <<" previous_reference_continuity="<<record.counters[6]<<" reference_continuity="<<record.counters[7]
                                <<" elapsed_ms="<<record.metrics[0]<<" previous_elapsed_ms="<<record.metrics[1]
                                <<" mode="<<record.metrics[2]<<" reason="<<record.reason;
                            Logger::WriteMessage(row.str().c_str());
                        }

                    }
                    for(const auto& line:failureMessages)Logger::WriteMessage(line.c_str());
                    Assert::Fail(why);};
                auto drain=[&] {
                    std::vector<AnalysisDiagnosticRecordV1> records;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);CopyUnreadCallbackTail(classicPoseRecords,cursor,records);}
                    // Index every source-bound proof in this tail before checking
                    // submissions; the unchanged oracle enforces original ordinals.
                    for(const auto& record:records)captionProofs.Observe(record);
                    for(const auto& record:records){
                        if(!strcmp(record.stage,"analysis-input")) {
                            require(record.analysisFormat==uint64_t(AnalysisLumaFormat::DecodedDolbyVision422)&&
                                record.analysisWidth==width&&record.analysisHeight==height&&record.counters[4]==12&&record.counters[5]==4,
                                L"Actual production source must be full-resolution 12-bit decoded Dolby 4:2:2.");
                            const auto source=delivered.find(record.sourceFrameNumber);
                            require(source!=delivered.end()&&uint64_t(source->second)==record.captureTimestamp,
                                L"Actual native analysis must retain the original delivered source identity and timestamp.");
                            nativeInputs[record.acceptedSequence]=record;
                        }
                        if(strcmp(record.stage,"submitted-presentation")||!(record.flags&1u))continue;
                        const auto input=nativeInputs.find(record.counters[1]);
                        require(input!=nativeInputs.end()&&record.acceptedSequence==record.counters[1]&&
                            input->second.transportGeneration==record.transportGeneration&&
                            (!record.analysisDomainKey||input->second.analysisDomainKey==record.analysisDomainKey)&&
                            (!record.sourceFormatGeneration||input->second.sourceFormatGeneration==record.sourceFormatGeneration),
                            L"Every actual submission must identify an existing original displayed source, never an invented current source.");
                        const auto consumed=nativeInputs.find(record.counters[0]);
                        require(consumed!=nativeInputs.end()&&consumed->second.transportGeneration==record.transportGeneration,
                            L"Every submitted output must identify its exact consumed input independently of displayed source.");
                        if(record.counters[0]!=record.counters[1]) {
                            require((record.flags&16384u)!=0&&record.cadenceRepeat&&
                                !strcmp(record.reason,"complete-output-input-cadence-repeat")&&
                                record.counters[0]==record.counters[1]+1&&
                                consumed->second.sourceFrameNumber==input->second.sourceFrameNumber+1&&
                                repeatedAnchors.insert(record.counters[1]).second&&!outputs.empty(),
                                L"Only an explicit successful one-input whole-image repeat may display a previous source.");
                            const auto& original=outputs.back();
                            require(!(original.flags&16384u)&&original.acceptedSequence==record.acceptedSequence&&
                                original.rendererInstanceKey==record.rendererInstanceKey&&original.transportGeneration==record.transportGeneration&&
                                original.rendererGeneration==record.rendererGeneration&&original.viewportGeneration==record.viewportGeneration&&
                                original.sourceFormatGeneration==record.sourceFormatGeneration&&original.analysisDomainKey==record.analysisDomainKey&&
                                original.policyGeneration==record.policyGeneration&&original.continuityGeneration==record.continuityGeneration&&
                                original.sourceFrameNumber==record.sourceFrameNumber&&original.captureTimestamp==record.captureTimestamp&&
                                std::isfinite(record.metrics[0])&&std::isfinite(record.metrics[5])&&
                                record.flags==(original.flags|16384u)&&record.metrics[0]==original.metrics[0]&&
                                record.metrics[5]>record.metrics[0]&&record.metrics[5]-record.metrics[0]<=1.5/inputHz,
                                L"Repeated output must retain its original complete successful image and bounded source age.");
                            if(requireSpatial)require(((record.flags>>24)&3u)==((original.flags>>24)&3u),
                                L"Whole-image repeat must retain the original producing detector, not borrow current metadata.");
                            for(int i=1;i<8;++i)require(record.counters[i]==original.counters[i],
                                L"Repeating final pixels cannot renew caption, native or certificate source ownership.");
                            for(int i=0;i<4;++i)require(record.displayed[i]==original.displayed[i]&&
                                record.published[i]==original.published[i]&&record.candidate[i]==original.candidate[i],
                                L"Whole-image repeat must preserve original caption/crop/native geometry together.");
                            for(int i=8;i<16;++i)require(record.metrics[i]==original.metrics[i],
                                L"Whole-image repeat cannot silently change source or target crop coordinates.");
                        } else require((record.flags&16384u)==0,
                            L"A repeat must expose consumed current and original displayed source separately.");
                        if(requireSpatial && record.counters[4])require(IsMeasuredSpatialNativeSubmission(record),
                            L"ACTIVE native authority and successfully submitted certificate must originate in source-spatial measurement (flags24..25), not legacy or unavailable evidence.");
                        require(std::isfinite(record.metrics[8])&&std::isfinite(record.metrics[9])&&
                            record.metrics[10]>record.metrics[8]&&record.metrics[11]>record.metrics[9],
                            L"Every successful output must publish valid actual source coordinates.");
                        ClassicShaderPreparationSelection preparation;
                        {
                            std::lock_guard<std::mutex> lock(classicPoseLogMutex);
                            const auto found=classicSubmittedShaderPreparation.find(
                                {record.rendererInstanceKey,record.transportGeneration,record.acceptedSequence});
                            if(found!=classicSubmittedShaderPreparation.end())preparation=found->second;
                        }
                        outputs.push_back(record);outputPreparation.push_back(preparation);
                    }
                };
                auto next=std::chrono::steady_clock::now();
                const auto captureOrigin=clock.TimingClockNow();
                auto pump=[&](bool cue,bool forceUnknown=false) {
                    const auto pumpStarted=clock.TimingClockNow();
                    MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
                    const auto messagesFinished=clock.TimingClockNow();ArrivalTiming* arrival=nullptr;
                    if(callback.state==RENDERSTATE_RENDERING){
                        // HDMI capture cadence is independent of driver/compiler
                        // stalls in this producer thread. Arrival pacing stays wall based.
                        const auto ticks=(sequence-1)*uint64_t(clock.TimingClockTicksPerSecond())*duration/scale;
                        const auto stamp=captureOrigin+static_cast<timingclocktime_t>(ticks);delivered[sequence]=stamp;
                        if(forceUnknown){
                            pressureInput=sequence;
                            for(const auto& prior:*arrivalStorage)
                                if(prior.source && prior.source+4>=pressureInput && prior.source<pressureInput)
                                    pressureArrivals[size_t(prior.source+4-pressureInput)]=prior;
                            std::ostringstream injection;injection<<"DV UNKNOWN INJECT: source_input="<<pressureInput
                                <<" capture_timestamp="<<stamp<<" expected_clean_input="<<(pressureInput+1)
                                <<" last_submitted_consumed="<<(outputs.empty()?0:outputs.back().counters[0])
                                <<" last_submitted_selected="<<(outputs.empty()?0:outputs.back().counters[1]);
                            Logger::WriteMessage(injection.str().c_str());
                        }
                        const auto& pixels=forceUnknown?pressure[sequence%2]:(cue?caption[sequence%2]:blank[sequence%2]);
                        arrival=&(*arrivalStorage)[size_t(arrivalCount%arrivalStorage->size())];*arrival={};
                        arrival->source=sequence;arrival->capture=stamp;arrival->pump=pumpStarted;arrival->messages=messagesFinished;
                        arrival->previous=lastArrival;
                        VideoFrame frame(pixels.data(),sequence++,stamp,&buffer);
                        arrival->entered=clock.TimingClockNow();
                        if(!arrivalCount){firstArrival=arrival->entered;firstCapture=stamp;}
                        if(lastArrival){const auto gap=arrival->entered-lastArrival;
                            if(gap>0&&gap<arrivalPeriod/2)++fastArrivalGaps;
                            longestArrivalGap=(std::max)(longestArrivalGap,gap);}
                        ++arrivalCount;lastArrival=arrival->entered;lastCapture=stamp;
                        cleanup.renderer->OnVideoFrame(frame);arrival->returned=clock.TimingClockNow();
                        longestOnFrame=(std::max)(longestOnFrame,arrival->returned-arrival->entered);
                    }
                    next+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0/inputHz));
                    if(arrival)arrival->sleepStarted=clock.TimingClockNow();
                    std::this_thread::sleep_until(next);
                    if(arrival){arrival->slept=clock.TimingClockNow();arrival->drainStarted=arrival->slept;}
                    drain();if(arrival)arrival->drained=clock.TimingClockNow();
                    preparationLog.Flush();if(arrival)arrival->flushed=clock.TimingClockNow();
                    if(arrival && pressureInput && arrival->source+4>=pressureInput && arrival->source<=pressureInput+4)
                        pressureArrivals[size_t(arrival->source+4-pressureInput)]=*arrival;
                    longestPump=(std::max)(longestPump,clock.TimingClockNow()-pumpStarted);
                    require(callback.state!=RENDERSTATE_FAILED,L"Persistent Dolby cadence renderer entered failed state.");
                };
                int32_t native[4]={0,pictureTop,width,pictureBottom};
                if(!requestedMode) {
                    const auto nativeDeadline=GetTickCount64()+20000;bool nativeReady=false;
                    do {pump(false);for(const auto& record:outputs)if(record.counters[4]&&record.published[1]>0&&record.published[3]<height&&
                        std::abs(record.metrics[8]-record.published[0])<0.01&&std::abs(record.metrics[9]-record.published[1])<0.01&&
                        std::abs(record.metrics[10]-record.published[2])<0.01&&std::abs(record.metrics[11]-record.published[3])<0.01){
                        std::copy_n(record.published,4,native);nativeReady=true;}
                    }while(!nativeReady&&GetTickCount64()<nativeDeadline);
                    require(nativeReady,L"Classic fixture failed independent native baseline acquisition.");
                    require(native[1]>=pictureTop-8&&native[1]<=pictureTop&&native[3]>=pictureBottom&&native[3]<=pictureBottom+8,
                        L"Classic native guard must preserve the known encoded source edges.");
                }
                for(const unsigned mode:{requestedMode}) {
                    // Advanced cold initialization may need the first prepared cue
                    // to establish native presentation. Feed captions from input1
                    // and inspect every safe fallback output, without a blank gate.
                    const auto first=mode?uint64_t(1):sequence;
                    size_t inspected=mode?0:outputs.size();uint64_t ready=0;size_t checked=0;
                    std::set<uint64_t> distinct;bool odd=false,even=false,displayedOdd=false,displayedEven=false;
                    double pinned[4]={},previousTop=native[1];
                    bool warmupNativeKnown=false;int32_t warmupNative[4]={};
                    // Same fixture contract as the public Classic controller:
                    // measured lower extent + source padding + configured offset,
                    // then its target buffer, clipped to the available encoded bar.
                    const int classicTargetBuffer=10,classicOffset=15;
                    const int classicRequested=(std::min)(height-native[3],
                        captionBottom+(std::max)(8,height/90)+classicOffset+classicTargetBuffer-native[3]);
                    const int classicShift=(classicRequested+1)&~1;
                    const double classicSettledTop=native[1]+classicShift;
                    const double classicSettledBottom=native[3]+classicShift;
                    bool unknownSent=false,unknownRepeated=false,nextCleanFresh=false;
                    uint64_t retiredAccepted=0;
                    auto cleanupMotionCoverage=[&](bool requireLegacyRejection) {
                        bool intersection=false,rejected=false;
                        std::lock_guard<std::mutex> guard(classicPoseLogMutex);
                        for(const auto& line:classicPoseMessages) {
                            if(line.find("SUBTITLE CADENCE:")!=std::string::npos &&
                                line.find("reason=cadence-current-cleanup-changed")!=std::string::npos) {
                                const auto at=line.find(" samples=");
                                rejected|=at!=std::string::npos&&std::strtoull(line.c_str()+at+9,nullptr,10)>0;
                            }
                            // CAPTURE is throttled and can miss a short steady
                            // interval after a long warmup. LAYOUT's accepted
                            // cleanup is equally concrete, but only a matching
                            // successful current composed submission may prove it.
                            SubtitleBoxRect actualCleanup;bool submitted=false;
                            for(const auto& output:outputs) {
                                const auto input=nativeInputs.find(output.counters[1]);
                                if(input!=nativeInputs.end() && TrySubmittedSubtitleCleanup(line,output,
                                    input->second.transportGeneration,actualCleanup)){submitted=true;break;}
                            }
                            if(!submitted)continue;
                            const int l=actualCleanup.left,t=actualCleanup.top,right=actualCleanup.right,bottom=actualCleanup.bottom;
                            for(int y=(std::max)(pictureTop,t);y<(std::min)(pictureBottom,bottom)&&!intersection;++y)
                                for(int x=(std::max)(0,l);x<(std::min)(width,right);++x) {
                                    bool glyph=false;
                                    for(int row=0;row<3;++row)for(int g=0;g<12;++g)
                                        glyph|=y>=1770+row*60&&y<1820+row*60&&x>=1552+g*64&&x<1584+g*64&&
                                            (x<1558+g*64||x>=1578+g*64||(y>=1793+row*60&&y<1797+row*60));
                                    if(!glyph){intersection=true;break;}
                                }
                        }
                        return intersection&&(!requireLegacyRejection||rejected);
                    };
                    const auto startupStarted=GetTickCount64();
                    const auto startupDeadline=startupStarted+(mode?ColdShaderPreparationBudgetMs:20000);
                    uint64_t steadyDeadline=0;
                    auto beginSteadyObservation=[&](uint64_t source) {
                        ready=source;
                        const auto now=GetTickCount64();steadyDeadline=now+90000;
                        std::ostringstream status;
                        status<<"Decoded-DV startup ready: input="<<inputHz<<" mode="<<mode<<" selected="<<source
                            <<" startup_elapsed_ms="<<(now-startupStarted)<<" steady_budget_ms=90000";
                        Logger::WriteMessage(status.str().c_str());
                    };
                    auto inspectEverySubmittedOutput=[&] {
                        for(;inspected<outputs.size();++inspected){const auto& record=outputs[inspected];
                            const auto displayedSource=nativeInputs.at(record.counters[1]).sourceFrameNumber;
                            const auto consumedSource=nativeInputs.at(record.counters[0]).sourceFrameNumber;
                            if(consumedSource<first)continue;
                            // The very first caption input may legally repeat the last
                            // complete blank image for one input. Its provenance was
                            // checked by drain(); current cue assertions begin when a
                            // caption-bearing original is actually displayed.
                            if(displayedSource<first){require((record.flags&16384u)!=0&&consumedSource==first,
                                L"Only the first cue input may repeat an immediately preceding complete blank output.");continue;}
                            const bool composed=(record.flags&2u)!=0;
                            if(composed&&mode) {
                                const auto& lineRecords=captionProofs.For(record);
                                const auto& selectedInput=nativeInputs.at(record.counters[1]);
                                const bool completeLines=HasCompletePersistentThreeLineCaption(lineRecords,record,selectedInput);
                                if(!completeLines) {
                                    auto describe=[](const char* role,const AnalysisDiagnosticRecordV1& r) {
                                        std::ostringstream row;row<<"DV LINE JOIN: role="<<role<<" stage="<<r.stage
                                            <<" ordinal="<<r.recordSequence<<" instance="<<r.rendererInstanceKey
                                            <<" accepted="<<r.acceptedSequence<<" generation="<<r.transportGeneration
                                            <<" renderer_generation="<<r.rendererGeneration<<" frame="<<r.sourceFrameNumber
                                            <<" capture="<<r.captureTimestamp<<" format="<<r.sourceFormatGeneration
                                            <<" domain="<<r.analysisDomainKey<<" viewport="<<r.viewportGeneration
                                            <<" policy="<<r.policyGeneration<<" continuity="<<r.continuityGeneration
                                            <<" flags="<<r.flags<<" lines="<<r.resultCode<<" counter0="<<r.counters[0]
                                            <<" counter1="<<r.counters[1]<<" raw_lines="<<r.counters[4]<<" reason="<<r.reason;
                                        Logger::WriteMessage(row.str().c_str());
                                    };
                                    describe("output",record);describe("selected-input",selectedInput);
                                    size_t candidates=0;
                                    for(auto it=lineRecords.rbegin();it!=lineRecords.rend()&&candidates<6;++it)
                                        if(!strcmp(it->stage,"subtitle-output")&&it->acceptedSequence==record.counters[1]) {
                                            describe(it->recordSequence<record.recordSequence?"preceding-candidate":"later-candidate",*it);++candidates;
                                        }
                                    if(!candidates)Logger::WriteMessage("DV LINE JOIN: no subtitle-output candidate for selected original source");
                                }
                                require(completeLines,
                                    L"Persistent composed caption must preserve all three detected/analyzed/current source-bound lines, without work-limit or later-attempt substitution.");
                            }
                            require(record.counters[3]==mode||(mode&&!ready&&!composed&&record.counters[3]==0),
                                L"Actually submitted source used an invalid frozen subtitle profile after preparation.");
                            if(requireUnknownInputRepeat&&pressureInput) {
                                if(consumedSource==pressureInput) {
                                    require((record.flags&16384u)!=0&&displayedSource+1==consumedSource,
                                        L"The single component-budget UNKNOWN input must repeat the complete immediately preceding successful image.");
                                    retiredAccepted=record.counters[0];unknownRepeated=true;
                                }
                                if(consumedSource==pressureInput+1) {
                                    require(unknownRepeated&&record.counters[0]==retiredAccepted+1&&
                                        record.counters[0]==record.counters[1]&&!(record.flags&16384u)&&
                                        displayedSource==consumedSource,
                                        L"The clean input after one retired UNKNOWN must be a fresh adjacent source, without repeat renewal or a synthetic gap.");
                                    nextCleanFresh=true;
                                }
                            }
                            const auto top=record.metrics[9],bottom=record.metrics[11];
                            if(mode){
                                // The user explicitly permits clipped raw subtitles while
                                // shader programs are being built. This exception belongs
                                // ONLY to an exact current Missing/Pending preparation key
                                // before the first composed output; never to failure, a
                                // Ready-but-unprepared cue, or any later cadence gap.
                                const auto& preparation=outputPreparation[inspected];
                                const auto& displayedInput=nativeInputs.at(record.counters[1]);
                                const bool building=!ready && preparation.AllowsInitialBuildFallback(
                                    displayedInput.transportGeneration,displayedInput.sourceFormatGeneration,mode);
                                if(!composed && (record.flags&16384u)) {
                                    require(building && record.counters[3]==0,
                                        L"A startup image repeat requires its original exact Missing/Pending mode0 image, without fabricated caption ownership.");
                                    std::ostringstream startupRepeat;
                                    startupRepeat<<"Decoded-DV startup whole-image repeat: consumed="<<record.counters[0]
                                        <<" original="<<record.counters[1]<<" original_shader_state="<<preparation.state
                                        <<" pipeline_key="<<preparation.key<<" original_effective="<<preparation.effective;
                                    Logger::WriteMessage(startupRepeat.str().c_str());
                                }
                                require(preparation.state!="failed",L"Shader preparation failed instead of completing bounded startup.");
                                if(!composed&&!building)require(record.metrics[8]<=0&&record.metrics[10]>=width&&top<=pictureTop&&bottom>=captionBottom,
                                    L"Unprepared output outside explicit shader building clipped the persistent raw caption or picture.");
                                if(record.counters[4]>0) {
                                    require(record.published[0]==0&&record.published[2]==width&&
                                        record.published[1]>=pictureTop-8&&record.published[1]<=pictureTop&&
                                        record.published[3]>=pictureBottom&&record.published[3]<=pictureBottom+8,
                                        L"Shader preparation or caption presence inflated the accepted native picture bounds.");
                                    if(!warmupNativeKnown){
                                        std::copy_n(record.published,4,warmupNative);warmupNativeKnown=true;
                                        std::ostringstream acquisition;
                                        acquisition<<"Decoded-DV first accepted native: input="<<inputHz<<" selected="<<record.counters[1]
                                            <<" accepted="<<record.counters[4]<<" elapsed_ms="<<(GetTickCount64()-startupStarted)
                                            <<" bounds="<<warmupNative[0]<<','<<warmupNative[1]<<'-'<<warmupNative[2]<<','<<warmupNative[3]
                                            <<" shader_state="<<preparation.state<<" pipeline_key="<<preparation.key
                                            <<" requested="<<preparation.requested<<" effective="<<preparation.effective;
                                        Logger::WriteMessage(acquisition.str().c_str());
                                    }
                                    for(int edge=0;edge<4;++edge)require(record.published[edge]==warmupNative[edge]&&
                                        std::abs(record.metrics[8+edge]-record.published[edge])<0.01,
                                        L"Shader preparation changed the fixed native picture viewport.");
                                }
                                if(composed&&!ready){
                                    require(record.counters[4]>0&&record.published[0]==0&&record.published[2]==width&&
                                        record.published[1]>=pictureTop-8&&record.published[1]<=pictureTop&&
                                        record.published[3]>=pictureBottom&&record.published[3]<=pictureBottom+8,
                                        L"First composed source lacks independently accurate native boundary authority.");
                                    std::copy_n(record.published,4,native);beginSteadyObservation(record.counters[1]);
                                    for(int e=0;e<4;++e)pinned[e]=record.metrics[8+e];
                                }
                                if(ready){
                                    for(int edge=0;edge<4;++edge)require(record.published[edge]==native[edge],
                                        L"Persistent subtitle changed accepted native AR or black-bar boundaries.");
                                    if(!composed&&overrideValue&&*overrideValue)
                                        require(cleanupMotionCoverage(true),
                                            L"Old-renderer fixture did not prove sampled cleanup/picture-motion rejection; this is not valid cadence RED evidence.");
                                    require(composed,L"Persistent caption composition flashed OFF on an actually submitted source.");
                                    require(record.counters[2]==record.counters[1]&&record.counters[5]==record.counters[1]&&
                                        (record.flags&64u)!=0,L"Composed caption/certificate must belong to the actual displayed source.");
                                    for(int e=0;e<4;++e)require(std::abs(record.metrics[8+e]-pinned[e])<0.01,
                                        L"Persistent caption caused frame-by-frame displayed AR/crop bounce.");}
                            }else{
                                for(int edge=0;edge<4;++edge)require(record.published[edge]==native[edge],
                                    L"Persistent Classic subtitle changed native AR or black-bar boundaries.");
                                const bool fullWidth=std::abs(record.metrics[8])<0.01&&std::abs(record.metrics[10]-width)<0.01;
                                const bool captionVisible=top<=1770.01&&bottom>=captionBottom-0.01;
                                const bool nativePose=fullWidth&&captionVisible&&
                                    std::abs((bottom-top)-(native[3]-native[1]))<0.01&&
                                    top>=native[1]-0.01&&top<=classicSettledTop+0.01;
                                if(!ready) {
                                    require(nativePose,
                                        L"Classic startup resized the picture or clipped the current caption.");
                                    if(nativePose){require(top+0.01>=previousTop,
                                        L"Classic startup native translation reversed during engage.");previousTop=top;}
                                    if(nativePose&&std::abs(top-classicSettledTop)<0.01&&std::abs(bottom-classicSettledBottom)<0.01){
                                        beginSteadyObservation(record.counters[1]);
                                        for(int e=0;e<4;++e)pinned[e]=record.metrics[8+e];
                                        std::ostringstream target;target<<"Classic exact settled pose: shift="<<classicShift
                                            <<" caption_bottom="<<captionBottom<<" padding="<<(std::max)(8,height/90)
                                            <<" offset="<<classicOffset<<" buffer="<<classicTargetBuffer
                                            <<" top="<<classicSettledTop<<" bottom="<<classicSettledBottom;
                                        Logger::WriteMessage(target.str().c_str());
                                    }
                                }
                                if(ready) {
                                    require(nativePose,L"Settled Classic output lost its exact native-height caption-safe translation.");
                                    for(int e=0;e<4;++e)require(std::abs(record.metrics[8+e]-pinned[e])<0.01,
                                        L"Settled Classic crop bounced during a persistent subtitle.");
                                }
                            }
                            if(ready){++checked;const auto source=nativeInputs.at(record.counters[1]).sourceFrameNumber;
                                distinct.insert(source);odd|=(consumedSource%2)!=0;even|=(consumedSource%2)==0;
                                displayedOdd|=(source%2)!=0;displayedEven|=(source%2)==0;}
                        }
                    };
                    do {pump(true);inspectEverySubmittedOutput();
                        if(checked>=60&&distinct.size()>=60&&odd&&even) {
                            if(!requireUnknownInputRepeat)break;
                            if(!unknownSent){pump(true,true);unknownSent=true;inspectEverySubmittedOutput();}
                            if(unknownRepeated&&nextCleanFresh)break;
                        }
                    }while(GetTickCount64()<(ready?steadyDeadline:startupDeadline));
                    // Stop drains in-flight rendering. Inspect that final tail before
                    // locals expire; no successful caption output escapes assertions.
                    cleanup.renderer->Stop();drain();preparationLog.Flush();inspectEverySubmittedOutput();
                    std::ostringstream coverage;coverage<<"Actual persistent decoded-DV cadence: input="<<inputHz<<" mode="<<mode
                        <<" inspected="<<checked<<" distinct_displayed="<<distinct.size()<<" consumed_odd="<<odd<<" consumed_even="<<even
                        <<" displayed_odd="<<displayedOdd<<" displayed_even="<<displayedEven
                        <<" unknown_input="<<pressureInput<<" complete_repeat="<<unknownRepeated<<" next_clean_fresh="<<nextCleanFresh;
                    Logger::WriteMessage(coverage.str().c_str());
                    require(ready>0,mode?
                        L"Decoded-DV cold preparation did not produce its first valid composed output within180seconds; steady cadence was not exercised.":
                        L"Classic decoded-DV startup did not settle within20seconds; steady cadence was not exercised.");
                    require(checked>=60&&distinct.size()>=60&&odd&&even,
                        L"Actual persistent Dolby subtitle test did not inspect 60 distinct displayed sources and BOTH consumed input parities.");
                    if(mode)require(cleanupMotionCoverage(false),
                        L"Actual subtitle cleanup never intersected changing picture pixels; benign background motion is not cadence regression coverage.");
                    if(requireUnknownInputRepeat)require(unknownSent&&unknownRepeated&&nextCleanFresh,
                        L"Actual whole-output repeat and immediately fresh successor were not exercised; timing-only/unavailable coverage cannot pass.");
                    reportProducerTiming(false);
                }
            }
        }
        TEST_METHOD(AlternatingDecodedDvBackdropKeepsThreeLineCueConfirmed)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::GeneratedGrayBackground),false,true,true);}
        TEST_METHOD(DecodedDvCrossEdgeFixtureNativePreflight)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::GeneratedGrayBackground),false,true);}
        TEST_METHOD(DecodedDvUnknownInputFixtureNativePreflight)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::GeneratedGrayBackground),true,true);}
        TEST_METHOD(StandardRendererClassicPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(false,0);}
        TEST_METHOD(BetaRendererClassicPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(true,0);}
        TEST_METHOD(StandardRendererGeneratedPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::GeneratedGrayBackground));}
        TEST_METHOD(BetaRendererGeneratedPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(true,unsigned(SubtitlePreviewMode::GeneratedGrayBackground));}
        TEST_METHOD(StandardRendererBlackPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::BlackBackground));}
        TEST_METHOD(BetaRendererBlackPersistentDecodedDvCaptionHasNoCadenceFlicker)
        {VerifyPersistentDecodedDvCadence(true,unsigned(SubtitlePreviewMode::BlackBackground));}

        TEST_METHOD(StandardRendererDecodedDvUnknownInputRepeatsOneCompleteOutput)
        {VerifyPersistentDecodedDvCadence(false,unsigned(SubtitlePreviewMode::GeneratedGrayBackground),true);}
        TEST_METHOD(BetaRendererDecodedDvUnknownInputRepeatsOneCompleteOutput)
        {VerifyPersistentDecodedDvCadence(true,unsigned(SubtitlePreviewMode::GeneratedGrayBackground),true);}

        TEST_METHOD(StandardRendererClassicUhdBottomCaptionOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,false); }
        TEST_METHOD(BetaRendererClassicUhdBottomCaptionOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,false); }
        TEST_METHOD(StandardRendererClassicUhdBothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,true); }
        TEST_METHOD(BetaRendererClassicUhdBothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,true); }

        TEST_METHOD(StandardRendererReconstructionUhdResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true); }
        TEST_METHOD(StandardRendererClassicUhdV210BothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(false,true,false,true); }
        TEST_METHOD(BetaRendererClassicUhdV210BothBarUiOutsideFillLimit)
        { VerifyClassicUhdBarOverlay(true,true,false,true); }
        TEST_METHOD(StandardRendererReconstructionUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true,true); }
        TEST_METHOD(StandardRendererReconstructionUhdV210CrossEdgeMovingCleanup)
        { VerifyClassicUhdBarOverlay(false,true,true,true,true); }
        TEST_METHOD(BetaRendererReconstructionUhdV210CrossEdgeMovingCleanup)
        { VerifyClassicUhdBarOverlay(true,true,true,true,true); }
        TEST_METHOD(UhdP210CrossEdgeFixtureNativePreflight)
        { VerifyClassicUhdBarOverlay(false,true,true,false,true,true); }
        TEST_METHOD(UhdV210CrossEdgeFixtureNativePreflight)
        { VerifyClassicUhdBarOverlay(false,true,true,true,true,true); }
        TEST_METHOD(StandardRendererBlackUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(false,true,true,true,false,false,true); }
        TEST_METHOD(BetaRendererBlackUhdV210ResidualUiWithMovingPicture)
        { VerifyClassicUhdBarOverlay(true,true,true,true,false,false,true); }
        TEST_METHOD(StandardRendererUnknownSubtitleInspectionCannotFreezeCurrentVideo)
        {
            for(auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
                VerifyNativeAspectTransitionSequence(false,false,unsigned(mode));
        }
        TEST_METHOD(BetaRendererUnknownSubtitleInspectionCannotFreezeCurrentVideo)
        {
            for(auto mode:{SubtitlePreviewMode::BlackBackground,SubtitlePreviewMode::GeneratedGrayBackground})
                VerifyNativeAspectTransitionSequence(true,false,unsigned(mode));
        }
        void VerifyNativeAspectTransitionSequence(bool beta,bool configuredFill=false,unsigned processedMode=0)
        {
            const bool requireSpatial=SourceSpatialProductionEnabled();
            Logger::WriteMessage(requireSpatial?"AR transition fixture: detector=spatial":"AR transition fixture: detector=legacy");
            HMODULE self=nullptr;
            Assert::IsTrue(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SnapshotTestLog),&self)!=FALSE);
            wchar_t modulePath[MAX_PATH]={};
            GetModuleFileNameW(self,modulePath,MAX_PATH);
            std::wstring pluginPath(modulePath);pluginPath.resize(pluginPath.find_last_of(L"\\/"));
#ifdef VP_LIBPLACEBO_BETA
            pluginPath+=L"\\..";
#endif
            pluginPath+=beta?L"\\vprenderer-beta\\VideoProcessorVPRendererBeta.dll":
                L"\\vprenderer\\VideoProcessorVPRenderer.dll";
            HMODULE plugin=LoadLibraryExW(pluginPath.c_str(),nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Assert::IsNotNull(plugin);
            struct ModuleCleanup { HMODULE value; ~ModuleCleanup(){FreeLibrary(value);} } moduleCleanup{plugin};
            struct FixtureLogCleanup {
                ~FixtureLogCleanup(){std::lock_guard<std::mutex> lock(snapshotTestLogMutex);snapshotTestLog.close();}
            } fixtureLogCleanup;
            { std::lock_guard<std::mutex> lock(snapshotTestLogMutex);
              snapshotTestLog.close();snapshotTestLog.clear();
              snapshotTestLog.open(configuredFill
                  ? (beta?"ar-fill-beta.log":"ar-fill-standard.log")
                  : (requireSpatial?(beta?"ar-transition-spatial-beta.log":"ar-transition-spatial-standard.log"):
                    (beta?"ar-transition-beta.log":"ar-transition-standard.log")),std::ios::trunc); }
            const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
            const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
            Assert::IsTrue(create&&destroy);
            const auto diagnostics=requireSpatial?reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(
                GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT)):nullptr;
            struct DiagnosticCleanup {
                VideoProcessorLibplaceboConfigureDiagnosticFn callback;
                ~DiagnosticCleanup(){if(callback)callback(nullptr,nullptr,nullptr,nullptr);}
            } diagnosticCleanup{diagnostics};
            size_t spatialCursor=0;std::vector<AnalysisDiagnosticRecordV1> spatialOutputs,spatialAccepted,spatialProduced,spatialCanvas;
            if(requireSpatial) {
                Assert::IsTrue(diagnostics!=nullptr);
                {std::lock_guard<std::mutex> lock(classicPoseLogMutex);classicPoseRecords.clear();classicPoseAllRecordStages=true;}
                diagnostics(nullptr,ClassicPoseTestRecord,nullptr,nullptr);
            }
            char temp[MAX_PATH]={},configPath[MAX_PATH]={};
            Assert::IsTrue(GetTempPathA(MAX_PATH,temp)!=0);
            Assert::IsTrue(GetTempFileNameA(temp,"art",0,configPath)!=0);
            std::ofstream(configPath)<<"[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n"
                "[vprenderer]\nquality: fast\n"
                "[vprenderer.viewport.scope]\nscreen_aspect: 2.35:1\n"
                "[vprenderer.zoom.native]\nautomatic_crop: true\nsubtitle_fit: false\n"
                "crop_narrower_content_to_fill_screen: false\ncrop_wider_content_to_fill_screen: "
                <<(configuredFill?"true":"false")<<"\n";
            if(processedMode)std::ofstream(configPath,std::ios::app)
                <<"[vprenderer.subtitles.processed]\ntype: "<<(processedMode==unsigned(SubtitlePreviewMode::BlackBackground)?"black":"generated_gray")
                <<"\nshortcut: F6\nsubtitle_hold_ms: 250\ncleanup_fill: stable_surround\n";
            constexpr int width=640,height=360;
            const int scopeBar=configuredFill?46:44;
            const double nativeScope=double(width)/(height-2*scopeBar);
            VideoStateComPtr state=new VideoState();
            state->valid=true;state->videoFrameEncoding=VideoFrameEncoding::P210;
            state->displayMode=std::make_shared<DisplayMode>(width,height,false,60000,1001);
            state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
            std::vector<uint16_t> scope(state->BytesPerFrame()/2,uint16_t(512<<6));
            std::vector<uint16_t> taller=scope,dark=scope;
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                scope[y*width+x]=uint16_t(((y>=scopeBar&&y<height-scopeBar)?300:64)<<6);
                taller[y*width+x]=uint16_t(((requireSpatial || (y>=12&&y<348))?300:64)<<6);
                dark[y*width+x]=uint16_t(64<<6);
            }
            SnapshotTestCallback callback;NativeP210Buffer buffer;
            struct Cleanup {
                HWND window;IVideoRenderer* renderer;VideoProcessorLibplaceboDestroyRendererFn destroy;
                const char* config;
                ~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}if(window)DestroyWindow(window);DeleteFileA(config);}
            } cleanup{CreateWindowExW(0,L"STATIC",L"AR transition fixture",WS_POPUP,
                0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,configPath};
            Assert::IsNotNull(cleanup.window);
            cleanup.renderer=create(&callback,42,cleanup.window,nullptr,requireSpatial,requireSpatial?9:1,
                VideoConversionOverride::VIDEOCONVERSION_NONE,configPath,SnapshotTestLog);
            Assert::IsNotNull(cleanup.renderer);
            if(processedMode) {
                ConfigFile config;Assert::IsTrue(config.Load(configPath));
                UnifiedProfileRuntime::Runtime runtime;std::string error;
                Assert::IsTrue(runtime.Initialize(config,{},error));
                UnifiedProfileRuntime::SelectionResult selected;
                Assert::IsTrue(runtime.SelectKey("F6",{},selected,error));
                CString active;bool restart=false,reset=false;
                Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*runtime.GetSnapshot(),active,restart,reset));
                Assert::IsFalse(restart);
            }
            // This experiment requires prepared-source lead. Keep the legacy
            // zero-depth case, and exercise the active mode with the user's
            // queued playback contract rather than inventing a stale-frame vote.
            if(requireSpatial) { cleanup.renderer->SetQueueFramePolicy(4,9,true);cleanup.renderer->SetActivePictureLookaheadFrames(4); }
            Assert::IsTrue(cleanup.renderer->OnVideoState(state));
            cleanup.renderer->Build();cleanup.renderer->Start();
            uint64_t sequence=1;std::set<uint64_t> witnessedNativeOwners;
            auto pump=[&](std::vector<uint16_t>& pixels) {
                MSG message;
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
                    {TranslateMessage(&message);DispatchMessageW(&message);}
                if(callback.state==RENDERSTATE_RENDERING) {
                    VideoFrame frame(pixels.data(),sequence++,0,&buffer);
                    cleanup.renderer->OnVideoFrame(frame);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                if(requireSpatial) {
                    std::vector<AnalysisDiagnosticRecordV1> tail;
                    {std::lock_guard<std::mutex> lock(classicPoseLogMutex);CopyUnreadCallbackTail(classicPoseRecords,spatialCursor,tail);}
                    for(const auto& record:tail) {
                        if(!strcmp(record.stage,"subtitle-canvas-admission"))spatialCanvas.push_back(record);
                        if(!strcmp(record.stage,"accepted-picture-detector"))spatialAccepted.push_back(record);
                        if(!strcmp(record.stage,"source-picture-active"))spatialProduced.push_back(record);
                        if(strcmp(record.stage,"submitted-presentation") || !(record.flags&1u))continue;
                        if(record.counters[4]) {
                            const bool wholeSource=std::abs(record.metrics[8])<0.01 && std::abs(record.metrics[9])<0.01 &&
                                std::abs(record.metrics[10]-width)<0.01 && std::abs(record.metrics[11]-height)<0.01;
                            const bool measured=((record.flags>>24)&3u)==unsigned(ActivePictureDetectorKind::SourceSpatialMeasured);
                            const bool certified=IsMeasuredSpatialNativeSubmission(record);
                            if(certified)witnessedNativeOwners.insert(record.counters[4]);
                            const bool retained=processedMode && !wholeSource && measured &&
                                record.resultCode==int(AlphaSourceCrop::DecisionOwner::SUBMITTED_SCOPE_RETENTION) &&
                                witnessedNativeOwners.count(record.counters[4]) && record.counters[4]<record.acceptedSequence &&
                                record.counters[0]==record.acceptedSequence && record.counters[1]==record.acceptedSequence &&
                                !(record.flags&(2u|32u|64u|8192u|16384u)) &&
                                std::abs(record.metrics[8]-record.published[0])<0.01 &&
                                std::abs(record.metrics[9]-record.published[1])<0.01 &&
                                std::abs(record.metrics[10]-record.published[2])<0.01 &&
                                std::abs(record.metrics[11]-record.published[3])<0.01 &&
                                std::any_of(spatialCanvas.begin(),spatialCanvas.end(),[&](const AnalysisDiagnosticRecordV1& canvas) {
                                    return canvas.acceptedSequence==record.acceptedSequence &&
                                        (canvas.flags&2u) && !(canvas.flags&(1u|4u));
                                });
                            Assert::IsTrue(measured && (wholeSource ? (record.flags&4096u)!=0 : (certified || retained)),
                                L"A crop needs current native proof or an explicitly unprocessed current-source continuation of an already witnessed native owner; full raster needs current authority.");
                        }
                        if(((record.flags>>24)&3u)==unsigned(ActivePictureDetectorKind::SourceSpatialMeasured))spatialOutputs.push_back(record);
                    }
                }
                DetectedPictureAspect value;cleanup.renderer->GetDetectedPictureAspect(value);
                return value;
            };
            auto acquire=[&](std::vector<uint16_t>& pixels,double expected,double applied,const wchar_t* message) {
                const auto first=sequence;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
                DetectedPictureAspect value;
                do {
                    value=pump(pixels);
                    if(value.inUseAvailable&&value.available&&!value.provisional&&
                        value.presentedSequence>=first&&value.detectedSequence>=first&&
                        std::abs(value.inUseRatio-applied)<0.015&&std::abs(value.ratio-expected)<0.015) {
                        Assert::AreEqual(int(requireSpatial?ActivePictureDetectorKind::SourceSpatialMeasured:
                            ActivePictureDetectorKind::Legacy),int(value.detectorKind),
                            L"The actual renderer must use the common production source measurement.");
                        if(requireSpatial) {
                            Assert::AreEqual(int(ActivePictureDetectorKind::SourceSpatialMeasured),int(value.detectorKind));
                            bool submitted=false;
                            for(const auto& record:spatialOutputs)if(record.counters[1]>=first) {
                                const double cropWidth=record.metrics[10]-record.metrics[8],cropHeight=record.metrics[11]-record.metrics[9];
                                for(const auto& accepted:spatialAccepted) {
                                    const double nativeWidth=accepted.published[2]-accepted.published[0],nativeHeight=accepted.published[3]-accepted.published[1];
                                    submitted|=accepted.resultCode==int(ActivePictureDetectorKind::SourceSpatialMeasured) &&
                                        accepted.acceptedSequence>=first && accepted.acceptedSequence<=record.counters[1] &&
                                        accepted.transportGeneration==record.transportGeneration &&
                                        nativeWidth>0&&nativeHeight>0&&cropWidth>0&&cropHeight>0&&
                                        std::abs(nativeWidth/nativeHeight-expected)<0.015&&std::abs(cropWidth/cropHeight-applied)<0.015;
                                }
                                // Current full-source geometry may preserve all
                                // pixels before logical history changes. Join it
                                // to its own measured input, never the old crop.
                                for(const auto& produced:spatialProduced) {
                                    const double measuredWidth=produced.published[2]-produced.published[0],measuredHeight=produced.published[3]-produced.published[1];
                                    const bool full=produced.published[0]==0 && produced.published[1]==0 && produced.published[2]==width && produced.published[3]==height;
                                    const bool admitted=full ? (record.flags&4096u)!=0 :
                                        IsMeasuredSpatialNativeSubmission(record) && record.counters[5]==record.counters[1];
                                    submitted|=produced.resultCode==int(ActivePictureDetectorKind::SourceSpatialMeasured) && (produced.flags&1u) &&
                                        produced.acceptedSequence==record.counters[1] && produced.transportGeneration==record.transportGeneration && admitted &&
                                        measuredWidth>0 && measuredHeight>0 && std::abs(measuredWidth/measuredHeight-expected)<0.015 &&
                                        cropWidth>0 && cropHeight>0 && std::abs(cropWidth/cropHeight-applied)<0.015;
                                }
                            }
                            Assert::IsTrue(submitted,L"Matching ACTIVE detected AR must reach accepted native bounds and actual submitted crop for the changed source.");
                        }
                        return value;
                    }
                }while(std::chrono::steady_clock::now()<deadline&&callback.state!=RENDERSTATE_FAILED);
                Logger::WriteMessage((std::string("AR actual in_use=")+std::to_string(value.inUseRatio)+
                    " detected="+std::to_string(value.ratio)+" provisional="+std::to_string(value.provisional)).c_str());
                Assert::Fail(message);return value;
            };
            const auto established=acquire(scope,nativeScope,configuredFill?2.35:nativeScope,L"Real renderer did not acquire initial native scope and selected presentation");
            const auto darkFirst=sequence;
            unsigned checked=0;std::set<uint64_t> freshUnknownSources;
            // Longer than the legacy two-second ambiguity hold, with distinct input identities.
            const auto darkUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(3200);
            while(std::chrono::steady_clock::now()<darkUntil&&callback.state!=RENDERSTATE_FAILED) {
                const auto value=pump(dark);
                if(value.inUseAvailable&&value.presentedSequence>=darkFirst) {
                    ++checked;freshUnknownSources.insert(value.presentedSequence);
                    Assert::AreEqual(established.inUseRatio,value.inUseRatio,0.0005,
                        L"Uncertain source replaced successfully submitted scope geometry");
                }
            }
            Assert::IsTrue(checked>20,L"Dark interval did not exercise enough actual submissions");
            if(processedMode) {
                std::set<uint64_t> continued,submitted;
                for(const auto& row:spatialCanvas)if(row.acceptedSequence>=darkFirst && (row.flags&2u) && !(row.flags&1u))
                    continued.insert(row.acceptedSequence);
                for(const auto& row:spatialOutputs)if(row.acceptedSequence>=darkFirst &&
                    row.counters[0]==row.acceptedSequence && row.counters[1]==row.acceptedSequence && !(row.flags&16384u))
                    submitted.insert(row.acceptedSequence);
                unsigned joined=0;for(auto source:submitted)joined+=continued.count(source)!=0;
                Assert::IsTrue(freshUnknownSources.size()>20 && joined>20,
                    L"Unknown inspection must produce distinct actually submitted current sources at fixed movie size, not frozen repeats or synthetic caption absence.");
            }
            const double tallerAspect=requireSpatial?double(width)/height:640.0/336;
            acquire(taller,tallerAspect,tallerAspect,L"Real renderer retained old scope over verified taller/full-raster picture");
            acquire(scope,nativeScope,configuredFill?2.35:nativeScope,L"Real renderer did not return to verified scope and selected presentation");
        }
		TEST_METHOD(ActivePictureLookaheadHasExplicitProxyOverride)
		{
			using ExpectedMethod = void (
				LibplaceboPluginVideoRenderer::*)(size_t);
			Assert::IsTrue((std::is_same_v<
				decltype(&LibplaceboPluginVideoRenderer::
					SetActivePictureLookaheadFrames),
				ExpectedMethod>));
		}

		TEST_METHOD(ShutdownFinalizationHasExplicitProxyOverride)
		{
#if defined(__cpp_noexcept_function_type)
			using ExpectedMethod = bool (
				LibplaceboPluginVideoRenderer::*)() noexcept;
#else
			using ExpectedMethod = bool (
				LibplaceboPluginVideoRenderer::*)();
#endif
			Assert::IsTrue((std::is_same_v<
				decltype(&LibplaceboPluginVideoRenderer::
					FinalizeRetirementForShutdown),
				ExpectedMethod>));
		}

		TEST_METHOD(OptionalRendererUsesOnePrivateDependencyDirectory)
		{
			wchar_t temporaryDirectory[MAX_PATH] = {};
			Assert::IsTrue(GetTempPathW(
				ARRAYSIZE(temporaryDirectory), temporaryDirectory) > 0);
			const std::wstring root = std::wstring(temporaryDirectory) +
				L"VideoProcessor-vp0107-" +
				std::to_wstring(GetCurrentProcessId());
			const std::wstring pluginDirectory =
				OptionalRendererLayout::Directory(root);
			std::wstring missing;
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::PluginPath(root).c_str(),
				missing.c_str());
			CreateDirectoryW(root.c_str(), nullptr);
			Assert::IsTrue(CreateDirectoryW(pluginDirectory.c_str(), nullptr) != FALSE);

			auto touch = [](const std::wstring& path)
			{
				const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
					nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				Assert::IsTrue(file != INVALID_HANDLE_VALUE);
				CloseHandle(file);
			};
			touch(OptionalRendererLayout::PluginPath(root));
			for (size_t index = 0;
				index + 1 < OptionalRendererLayout::PrivateDependencies().size();
				++index)
			{
				touch(OptionalRendererLayout::Join(pluginDirectory,
					OptionalRendererLayout::PrivateDependencies()[index]));
			}

			const wchar_t* finalDependency =
				OptionalRendererLayout::PrivateDependencies().back();
			// A same-named root/PATH-style decoy must not satisfy the private
			// plugin contract.
			touch(OptionalRendererLayout::Join(root, finalDependency));
			Assert::IsTrue(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			Assert::AreEqual(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency).c_str(),
				missing.c_str());

			touch(OptionalRendererLayout::Join(
				pluginDirectory, finalDependency));
			Assert::IsFalse(OptionalRendererLayout::FindMissingRuntimeFile(
				root, missing));
			DeleteFileW(OptionalRendererLayout::Join(root, finalDependency).c_str());
			DeleteFileW(OptionalRendererLayout::PluginPath(root).c_str());
			for (const wchar_t* dependency :
				OptionalRendererLayout::PrivateDependencies())
			{
				DeleteFileW(OptionalRendererLayout::Join(
					pluginDirectory, dependency).c_str());
			}
			RemoveDirectoryW(pluginDirectory.c_str());
			RemoveDirectoryW(root.c_str());
		}
	};
}
