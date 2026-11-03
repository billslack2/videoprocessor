// Private native-capture regression. No captured pixels are checked in.
// Component-only evidence: sourcePrepared models a successful compositor;
// real source submissions, GPU pixels and frame deadlines need renderer tests.
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCutPaste.h>
#include <SourcePictureDetector.h>
#include <vprenderer/SubtitleAspectRetention.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <map>
#include <mutex>
#include <cstring>
#include <vector>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <windows.h>

namespace {
using Clock=std::chrono::steady_clock;
struct Frame {
    std::filesystem::path path;
    AnalysisLumaSource source;
    SubtitleBoxObservation key;
    ActivePictureBounds base;
    uint64_t duration=0;
    unsigned phase=0;
};
struct Cost {
    std::vector<double> values;
    void Add(Clock::time_point begin) {values.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());}
    void Print(const char* name) {
        std::sort(values.begin(),values.end());
        const auto at=[&](double p){return values.empty()?0.0:values[size_t(p*(values.size()-1))];};
        std::cout<<"{\"kind\":\"cost\",\"operation\":\""<<name<<"\",\"count\":"<<values.size()
            <<",\"p50_ms\":"<<at(.5)<<",\"p95_ms\":"<<at(.95)<<",\"max_ms\":"<<at(1)<<"}\n";
    }
};
void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
Frame Metadata(const std::filesystem::path& path) {
    Frame f;f.path=path;auto sidecar=path;sidecar.replace_extension(".txt");
    std::ifstream in(sidecar);std::string headerLine;std::getline(in,headerLine);std::istringstream header(headerLine);
    int format,encoding,color,top,bottom,script,range=0,discontinuity,repeat,alignment;
    header>>f.source.width>>f.source.height>>f.source.rowBytes>>f.source.chromaRowBytes
        >>format>>encoding>>color>>top>>bottom>>script>>range;
    Require(bool(header),"Missing native metadata including sample range");
    std::string version;auto& id=f.key.identity;
    in>>version>>id.transportGeneration>>id.acceptedSequence>>id.sourceFrameNumber>>id.captureTimestamp
        >>id.sourceFormatGeneration>>id.viewportGeneration>>id.rendererGeneration>>id.analysisDomainKey
        >>f.key.policyGeneration>>f.key.continuityGeneration>>f.key.nearBarDistance>>f.key.optimizationMode
        >>f.duration>>discontinuity>>repeat>>alignment;
    Require(bool(in)&&version=="identity_v1","Missing exact source identity");
    Require(format>=0&&format<=5&&range>=0&&range<=2&&alignment>=0&&alignment<=2,
        "Invalid native format/range/alignment");
    Require(f.source.width>0&&f.source.width<=8192&&f.source.height>0&&f.source.height<=4320&&
        top>=0&&bottom>top&&bottom<=f.source.height&&f.duration>0,"Invalid raster/base/duration");
    f.source.format=static_cast<AnalysisLumaFormat>(format);f.source.encoding=static_cast<VideoFrameEncoding>(encoding);
    f.source.colorspace=static_cast<ColorSpace>(color);f.source.range=static_cast<AnalysisLumaRange>(range);
    f.source.generation=id.transportGeneration;f.source.rgbBitAlignment=static_cast<uint8_t>(alignment);
    f.key.width=f.source.width;f.key.height=f.source.height;f.key.scriptHint=static_cast<SubtitleScriptHint>(script);
    f.key.discontinuity=discontinuity!=0;Require(repeat==0,"Repeated capture requires a separate replay scenario");f.key.analysisStride=1;
    f.base={0,top,f.source.width,bottom,f.source.width,f.source.height,double(f.source.width)/(bottom-top),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    f.key.sharedPicture.required=true;f.key.sharedPicture.identity=id;f.key.sharedPicture.bounds=f.base;
    return f;
}
void LoadPixels(Frame& f,std::vector<std::shared_ptr<std::vector<uint8_t>>>& unique) {
    std::ifstream in(f.path,std::ios::binary|std::ios::ate);Require(bool(in),"Native capture unavailable");
    auto size=in.tellg();Require(size>0&&size<=std::streamoff(128ull*1024*1024),"Capture exceeds packet bound");
    auto bytes=std::make_shared<std::vector<uint8_t>>(size_t(size));in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes->data()),size);Require(bool(in),"Truncated native capture");
    unsigned phase=0;
    for(;phase<unique.size();++phase)if(*bytes==*unique[phase]) {bytes=unique[phase];break;}
    if(phase==unique.size()) {
        Require(unique.size()<16,"This paused-cue replay requires at most16 byte-distinct rasters");
        unique.push_back(bytes);
    }
    f.phase=phase;f.source.data=bytes->data();f.source.dataBytes=bytes->size();f.source.backingOwner=bytes;
    Require(f.source.IsValid(),"Native source view is invalid");
}
SubtitleBoxObservation SyntheticKey(const Frame& f,uint64_t n) {
    auto k=f.key;const auto seed=f.key.identity;
    k.identity.acceptedSequence=seed.acceptedSequence+n;k.identity.sourceFrameNumber=seed.sourceFrameNumber+n;
    // Capture timestamps use microseconds; frame duration is 100 ns units.
    k.identity.captureTimestamp=seed.captureTimestamp+(n*f.duration)/10;
    k.sharedPicture.identity=k.identity;k.discontinuity=false;return k;
}
SubtitleCutPasteGeometry Geometry(SubtitleCutPastePresentation& compositor,SubtitlePaddingGuard& pad,
    const SubtitleBoxResult& cue,const SubtitleBoxPreview& preview,const ActivePictureBounds& base) {
    SubtitlePaddingPolicy policy;policy.glyphRelative=true;policy.minimumHeadroomPixels=30;
    const auto safe=pad.Consume(cue,preview,{60,40,21},policy);const auto display=pad.DisplayBounds();
    auto geo=compositor.Consume(cue,preview,safe,10,&display,nullptr);
    geo=FitSubtitleToVisiblePicture(geo,{base.left,base.top,base.right,base.bottom},10);
    return ApplySubtitleTextReduction(geo,30,safe,true,false);
}
void Matrix(const std::vector<Frame>& frames,unsigned phases,bool enforce,unsigned& failures) {
    Cost seedCost,proofCost,samePhaseCost,crossPhaseCost,validCost,invalidCost;unsigned tested=0,passed=0,seeded=0;
    for(unsigned phase=0;phase<phases;++phase) {
        auto found=std::find_if(frames.begin(),frames.end(),[&](const Frame& f){return f.phase==phase;});
        const auto& seed=*found;auto sampler=std::make_unique<SubtitleMeasurementSampler>();
        const auto native=DetectSourcePictureForWorker(seed.source).evidence;
        std::vector<SubtitleBoxObservation> scans(3);SubtitleBarTrackingReference prior;
        // Seed acquisition repeats this phase over a SYNTHETIC three-source
        // history. The accepted seed identity remains the captured first source.
        for(unsigned i=0;i<3;++i) {
            scans[i]=sampler->Measure(seed.source,SyntheticKey(seed,i),prior,1e7/double(seed.duration),native);
            prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(scans[i]);
        }
        auto preview=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*preview,scans.data(),scans.size(),seed.key.policyGeneration,seed.key.continuityGeneration);
        auto tracker=std::make_unique<SubtitleBoxPresentation>();
        const auto cue=tracker->Consume(*preview,250,double(seed.duration)/10000.0);
        auto compositor=std::make_unique<SubtitleCutPastePresentation>();auto pad=std::make_unique<SubtitlePaddingGuard>();
        const auto geometry=Geometry(*compositor,*pad,cue,*preview,seed.base);
        auto guard=std::make_unique<SubtitleCadenceGuard>();auto started=Clock::now();
        const bool ready=guard->RememberCurrentFootprint(*preview,cue,seed.source,seed.base,geometry,
            double(seed.duration)/10000.0,250,5);seedCost.Add(started);
        std::cout<<"{\"kind\":\"seed\",\"phase\":"<<phase<<",\"captured_sequence\":"<<seed.key.identity.acceptedSequence
            <<",\"ready\":"<<ready<<",\"reason\":\""<<guard->DecisionReason()<<"\"}\n";
        if(!ready){++failures;continue;}++seeded;
        for(const auto& current:frames) {
            if(current.key.identity.acceptedSequence<=seed.key.identity.acceptedSequence)continue;
            started=Clock::now();const auto proof=guard->VerifyCurrentFootprint(current.key,current.source,
                preview->current,cue,seed.base);proofCost.Add(started);
            const double elapsed=proofCost.values.back();
            (current.phase==phase?samePhaseCost:crossPhaseCost).values.push_back(elapsed);
            (proof.valid?validCost:invalidCost).values.push_back(elapsed);++tested;passed+=proof.valid;
            std::cout<<"{\"kind\":\"sampled_footprint\",\"seed_phase\":"<<phase<<",\"phase\":"<<current.phase
                <<",\"captured_sequence\":"<<current.key.identity.acceptedSequence<<",\"valid\":"<<proof.valid
                <<",\"samples\":"<<proof.samples<<",\"elapsed_ms\":"<<elapsed<<",\"reason\":\""<<proof.reason<<"\",\"mismatch\":["
                <<proof.mismatchX<<','<<proof.mismatchY<<"]}\n";
        }
    }
    if(enforce)failures+=tested-passed;
    seedCost.Print("footprint_seed");proofCost.Print("footprint_current_pixels");
    samePhaseCost.Print("footprint_same_phase");crossPhaseCost.Print("footprint_cross_phase");
    validCost.Print("footprint_valid");invalidCost.Print("footprint_invalid");
    std::cout<<"{\"kind\":\"matrix_summary\",\"seeds\":"<<seeded<<",\"checked\":"<<tested
        <<",\"passed\":"<<passed<<",\"seed_acquisition\":\"synthetic_three_source_same_phase\",\"proof_sources\":\"captured_sidecar_identities\"}\n";
}
void Timeline(const std::vector<Frame>& frames,bool enforce,unsigned& failures) {
    auto sampler=std::make_unique<SubtitleMeasurementSampler>();SubtitleBarTrackingReference prior;
    std::vector<SubtitleBoxObservation> scans(frames.size()+2);Cost scanCost,admissionCost;
    // Explicitly synthetic consecutive identities: captured pixels were sampled
    // every5 sources. This component experiment makes no live timing claim.
    const auto& context=frames.front();
    for(size_t i=0;i<scans.size();++i) {
        const auto& f=frames[i%frames.size()];auto key=SyntheticKey(context,i);
        const auto native=DetectSourcePictureForWorker(f.source).evidence;auto started=Clock::now();
        scans[i]=sampler->Measure(f.source,key,prior,1e7/double(context.duration),native);
        scanCost.Add(started);prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(scans[i]);
    }
    auto tracker=std::make_unique<SubtitleBoxPresentation>();auto compositor=std::make_unique<SubtitleCutPastePresentation>();
    auto pad=std::make_unique<SubtitlePaddingGuard>();unsigned strong=0,held=0,detected=0,composition=0;
    for(size_t i=0;i<frames.size();++i) {
        const auto& f=frames[i];auto preview=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*preview,scans.data()+i,3,context.key.policyGeneration,context.key.continuityGeneration);
        const auto cue=tracker->Consume(*preview,250,double(context.duration)/10000.0);
        const auto geometry=Geometry(*compositor,*pad,cue,*preview,context.base);
        auto started=Clock::now();const auto retention=EvaluateActivePicturePresentationRetention(f.source,context.base);
        QualifiedSubtitlePresentationInput input;input.mode=SubtitlePreviewMode::GeneratedGrayBackground;
        input.currentIdentity=preview->current.identity;input.observation=&preview->current;input.accepted=&cue;
        input.composition=&geometry;input.physicalBase=context.base;input.sourcePrepared=geometry.valid;
        input.currentDisplayPicture=preview->current.sharedPicture;
        input.policyGeneration=context.key.policyGeneration;input.continuityGeneration=context.key.continuityGeneration;
        input.currentPreparedCue=&tracker->CurrentPreparedCueCertificate();
        const auto proof=EvaluatePreparedSubtitlePresentationEvidence(f.source,retention,input);
        const auto prepared=QualifyPreparedSubtitleCropPresentation(proof,input.currentIdentity,context.base);
        admissionCost.Add(started);strong+=prepared.available;held+=cue.held;detected+=cue.detected;composition+=proof.currentCompositionVerified;
        std::cout<<"{\"kind\":\"synthetic_timeline\",\"index\":"<<i<<",\"phase\":"<<f.phase
            <<",\"captured_sequence\":"<<f.key.identity.acceptedSequence<<",\"synthetic_sequence\":"<<input.currentIdentity.acceptedSequence
            <<",\"detected\":"<<cue.detected<<",\"held\":"<<cue.held<<",\"held_certificate\":"
            <<tracker->CurrentPreparedCueCertificate().Matches(preview->current,cue,context.base)
            <<",\"composition_verified\":"<<proof.currentCompositionVerified<<",\"strong_prepared\":"<<prepared.available
            <<",\"status\":"<<int(proof.status)<<",\"reason\":\""<<proof.reason<<"\"}\n";
    }
    if(enforce)failures+=unsigned(frames.size())-strong;
    scanCost.Print("full_grouping");admissionCost.Print("cpu_prepared_admission");
    std::cout<<"{\"kind\":\"timeline_summary\",\"count\":"<<frames.size()<<",\"detected\":"<<detected
        <<",\"held\":"<<held<<",\"composition_verified\":"<<composition<<",\"strong_prepared\":"<<strong
        <<",\"sourcePrepared\":\"modeled_success_no_GPU\",\"timing\":\"synthetic_consecutive_unpaced\"}\n";
}
struct WorkerTraceCapture {
    std::mutex mutex;
    std::vector<AnalysisDiagnosticRecordV1> rows;
    static void __cdecl Record(void* context,const AnalysisDiagnosticRecordV1* row) {
        if(!row || std::strcmp(row->stage,"worker-cue-proof"))return;
        auto& self=*static_cast<WorkerTraceCapture*>(context);std::lock_guard<std::mutex> lock(self.mutex);
        if(self.rows.size()<512)self.rows.push_back(*row);
    }
};
void WorkerPath(const std::vector<Frame>& frames,unsigned phases,bool enforce,unsigned& failures) {
    Cost proofCost;unsigned total=0,ready=0,consumed=0,strong=0;uint64_t fullScans=0;
    for(unsigned phase=0;phase<phases;++phase) {
        const auto& seed=*std::find_if(frames.begin(),frames.end(),[&](const Frame& f){return f.phase==phase;});
        auto scanner=std::make_unique<SubtitleMeasurementSampler>();SubtitleBarTrackingReference prior;
        const auto native=DetectSourcePictureForWorker(seed.source).evidence;
        std::vector<SubtitleBoxObservation> scans(3);
        for(unsigned i=0;i<3;++i) {
            scans[i]=scanner->Measure(seed.source,SyntheticKey(seed,i),prior,1e7/double(seed.duration),native);
            prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(scans[i]);
        }
        auto preview=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*preview,scans.data(),scans.size(),seed.key.policyGeneration,seed.key.continuityGeneration);
        auto tracker=std::make_unique<SubtitleBoxPresentation>();
        const auto accepted=tracker->Consume(*preview,250,double(seed.duration)/10000.0);
        auto compositor=std::make_unique<SubtitleCutPastePresentation>();auto pad=std::make_unique<SubtitlePaddingGuard>();
        const auto operation=Geometry(*compositor,*pad,accepted,*preview,seed.base);
        QualifiedSubtitlePresentationInput seedAdmission;seedAdmission.mode=SubtitlePreviewMode::GeneratedGrayBackground;
        seedAdmission.currentIdentity=preview->current.identity;seedAdmission.observation=&preview->current;
        seedAdmission.accepted=&accepted;seedAdmission.composition=&operation;seedAdmission.physicalBase=seed.base;
        seedAdmission.currentDisplayPicture=preview->current.sharedPicture;seedAdmission.sourcePrepared=operation.valid;
        seedAdmission.policyGeneration=seed.key.policyGeneration;seedAdmission.continuityGeneration=seed.key.continuityGeneration;
        seedAdmission.currentPreparedCue=&tracker->CurrentPreparedCueCertificate();
        const auto seedRetention=EvaluateActivePicturePresentationRetention(seed.source,seed.base);
        const auto seedEvidence=EvaluatePreparedSubtitlePresentationEvidence(seed.source,seedRetention,seedAdmission);
        const bool seedQualified=QualifyPreparedSubtitleCropPresentation(seedEvidence,seedAdmission.currentIdentity,seed.base).available;
        std::cout<<"{\"kind\":\"worker_seed_admission\",\"phase\":"<<phase<<",\"strong_prepared\":"<<seedQualified
            <<",\"reason\":\""<<seedEvidence.reason<<"\"}\n";
        if(!seedQualified){++failures;continue;}
        SubtitleCurrentCueTemplateInput input;input.observation=preview->current;input.acceptedCue=accepted;
        input.source=seed.source;input.physicalBase=seed.base;input.composedOperation=operation;
        input.sourceMode=SubtitlePreviewMode::GeneratedGrayBackground;input.compositionSucceeded=operation.valid;
        input.acceptedCurrentScan=true;input.backgroundMode=5;input.frameMs=double(seed.duration)/10000.0;input.holdMs=250;
        WorkerTraceCapture traces;
        AnalysisDiagnosticTrace::SetExternalSinks(&traces,&WorkerTraceCapture::Record,nullptr,nullptr);
        auto worker=std::make_unique<SubtitleMeasurementWorker>();worker->SetInputRateHz(1e7/double(seed.duration));
        uint64_t revision=0;auto seedStatus=worker->SubmitCurrentCueTemplate(input,revision);
        const auto seedDeadline=Clock::now()+std::chrono::seconds(3);
        while(seedStatus==SubtitleCurrentCueProofStatus::Queued || seedStatus==SubtitleCurrentCueProofStatus::Pending) {
            if(Clock::now()>=seedDeadline)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            seedStatus=worker->SubmitCurrentCueTemplate(input,revision);
        }
        std::cout<<"{\"kind\":\"worker_seed\",\"phase\":"<<phase<<",\"status\":"<<int(seedStatus)<<",\"revision\":"<<revision<<"}\n";
        if(seedStatus!=SubtitleCurrentCueProofStatus::Ready){++failures;worker->Stop();AnalysisDiagnosticTrace::ClearExternalSinks();continue;}
        for(size_t i=1;i<=frames.size();++i) {
            const auto& f=frames[(i+phase)%frames.size()];const auto key=SyntheticKey(seed,i);
            const auto currentNative=DetectSourcePictureForWorker(f.source).evidence;
            worker->AdvanceCurrentCueRenderWindow(key.identity,key.identity.acceptedSequence);
            SubtitleCurrentCueProofRequest request;request.currentKey=key;request.source=f.source;
            request.physicalBase=seed.base;request.sourceMode=SubtitlePreviewMode::GeneratedGrayBackground;
            request.backgroundMode=5;request.eligibleRenderHead=key.identity;request.eligibleTailSequence=key.identity.acceptedSequence;
            request.templateRevision=revision;request.frameMs=double(seed.duration)/10000.0;
            // Always offer the real semantic fallback. The production worker
            // decides its bounded refresh cadence; this harness does not suppress it.
            request.fullScanDue=true;const auto currentSource=f.source;const double sourceRate=1e7/double(seed.duration);
            request.fullScanOnMiss=[currentSource,key,currentNative,sourceRate](SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference& previous) {
                return sampler.Measure(currentSource,key,previous,sourceRate,currentNative);
            };
            const auto offered=worker->SubmitCurrentCueProof(std::move(request));
            SubtitleCurrentCueProofReceipt receipt;auto status=offered;const auto deadline=Clock::now()+std::chrono::seconds(3);
            while(status==SubtitleCurrentCueProofStatus::Queued || status==SubtitleCurrentCueProofStatus::Pending ||
                status==SubtitleCurrentCueProofStatus::NoTask) {
                status=worker->TryTakeCurrentCueProof(key,revision,receipt);
                if(Clock::now()>=deadline)break;
                if(status==SubtitleCurrentCueProofStatus::Queued || status==SubtitleCurrentCueProofStatus::Pending || status==SubtitleCurrentCueProofStatus::NoTask)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ++total;bool adopted=false,recorded=false,preparedAvailable=false;const char* reason="worker-not-ready";
            if(status==SubtitleCurrentCueProofStatus::Ready) {
                ++ready;proofCost.values.push_back(receipt.WorkerMilliseconds());
                auto currentPreview=std::make_unique<SubtitleBoxPreview>();SubtitleBoxResult held;
                adopted=tracker->ConsumeCurrentCueProof(receipt,*currentPreview,held);consumed+=adopted;
                if(adopted) {
                    const auto geo=receipt.Operation();PendingPreparedSubtitleErasureReceipt erasure;
                    recorded=erasure.Record(currentPreview->current,held,seed.base,currentPreview->current.sharedPicture,
                        geo,receipt.SourceMode(),key.policyGeneration,key.continuityGeneration,true);
                    QualifiedSubtitlePresentationInput ownership;ownership.mode=receipt.SourceMode();ownership.currentIdentity=key.identity;
                    ownership.observation=&currentPreview->current;ownership.accepted=&held;ownership.composition=&geo;
                    ownership.physicalBase=seed.base;ownership.currentDisplayPicture=currentPreview->current.sharedPicture;
                    ownership.policyGeneration=key.policyGeneration;ownership.continuityGeneration=key.continuityGeneration;
                    ownership.sourcePrepared=geo.valid;ownership.pendingCurrentPixelsVerified=true;ownership.pendingPreparedErasure=&erasure;
                    const auto retention=EvaluateActivePicturePresentationRetention(f.source,seed.base);
                    const auto evidence=EvaluatePreparedSubtitlePresentationEvidence(f.source,retention,ownership);
                    preparedAvailable=QualifyPreparedSubtitleCropPresentation(evidence,key.identity,seed.base).available;reason=evidence.reason;
                    Require(SameAnalysisFrameIdentity(receipt.SeedIdentity(),preview->current.identity),"Worker proof replaced immutable acquisition baseline");
                }
            }
            strong+=preparedAvailable;
            std::cout<<"{\"kind\":\"worker_proof\",\"seed_phase\":"<<phase<<",\"phase\":"<<f.phase
                <<",\"index\":"<<i<<",\"synthetic_sequence\":"<<key.identity.acceptedSequence<<",\"status\":"<<int(status)
                <<",\"consumed\":"<<adopted<<",\"erasure_receipt\":"<<recorded<<",\"strong_prepared\":"<<preparedAvailable
                <<",\"worker_ms\":"<<receipt.WorkerMilliseconds()<<",\"reason\":\""<<reason<<"\"}\n";
        }
        worker->Stop();AnalysisDiagnosticTrace::ClearExternalSinks();
        for(const auto& row:traces.rows)std::cout<<"{\"kind\":\"worker_trace\",\"seed_phase\":"<<phase
            <<",\"synthetic_sequence\":"<<row.acceptedSequence<<",\"reason\":\""<<row.reason
            <<"\",\"mismatch\":["<<row.metrics[1]<<','<<row.metrics[2]<<"],\"worker_ms\":"<<row.metrics[0]
            <<",\"queue_ms\":"<<row.metrics[7]<<",\"enqueue_to_publish_ms\":"<<row.metrics[8]<<"}\n";
        const auto diagnostic=worker->TakeDiagnostics();fullScans+=diagnostic.fullScans;
        std::cout<<"{\"kind\":\"worker_seed_summary\",\"phase\":"<<phase<<",\"full_scans\":"<<diagnostic.fullScans
            <<",\"current_cue_tasks\":"<<diagnostic.currentCueTasks<<",\"worker_peak_ms\":"<<diagnostic.currentCuePeakMs<<"}\n";
    }
    if(enforce)failures+=total-strong;
    proofCost.Print("worker_proof_ready");
    std::cout<<"{\"kind\":\"worker_summary\",\"count\":"<<total<<",\"ready\":"<<ready<<",\"consumed\":"<<consumed
        <<",\"strong_prepared\":"<<strong<<",\"full_scans\":"<<fullScans
        <<",\"sourcePrepared\":\"modeled_success_no_GPU\",\"scheduling\":\"unpaced_serial_poll_no_deadline_claim\"}\n";
}
}
int main(int argc,char** argv) {
    SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
    try {
        Require(argc>=2,"Usage: subtitle_tolerance_replay CAPTURE_DIRECTORY [--require-pass]");
        const bool enforce=argc>2&&std::string(argv[2])=="--require-pass";
        std::vector<std::filesystem::path> paths;
        for(const auto& e:std::filesystem::directory_iterator(argv[1]))
            if(e.is_regular_file()&&e.path().extension()==".bin"&&e.path().filename().string().rfind("frame-",0)==0)paths.push_back(e.path());
        std::sort(paths.begin(),paths.end());Require(!paths.empty()&&paths.size()<=512,"Capture count out of range");
        std::vector<Frame> frames;std::vector<std::shared_ptr<std::vector<uint8_t>>> unique;
        std::map<uint64_t,unsigned> gaps;for(const auto& path:paths) {
            auto f=Metadata(path);LoadPixels(f,unique);
            if(!frames.empty()) {
                Require(f.key.identity.acceptedSequence>frames.back().key.identity.acceptedSequence,"Capture order not monotonic");
                ++gaps[f.key.identity.acceptedSequence-frames.back().key.identity.acceptedSequence];
                Require(SameSubtitlePresentationBounds(f.base,frames.front().base),"Changing base requires separate scenario");
            }
            frames.push_back(std::move(f));
        }
        std::cout<<std::setprecision(8)<<"{\"kind\":\"capture\",\"frames\":"<<frames.size()<<",\"unique_rasters\":"<<unique.size()
            <<",\"base\":["<<frames[0].base.left<<','<<frames[0].base.top<<','<<frames[0].base.right<<','<<frames[0].base.bottom
            <<"],\"frame_duration_100ns\":"<<frames[0].duration<<",\"source_steps\":[";
        bool first=true;for(auto gap:gaps){if(!first)std::cout<<',';first=false;std::cout<<'['<<gap.first<<','<<gap.second<<']';}std::cout<<"]}\n";
        unsigned failures=0;Matrix(frames,unsigned(unique.size()),enforce,failures);Timeline(frames,enforce,failures);WorkerPath(frames,unsigned(unique.size()),enforce,failures);
        std::cout<<"{\"kind\":\"result\",\"failures\":"<<failures<<",\"enforced\":"<<enforce<<",\"gpu_test\":false}\n";
        return failures?1:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 2;}
}
