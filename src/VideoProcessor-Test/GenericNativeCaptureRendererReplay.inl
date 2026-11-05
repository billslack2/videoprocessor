// Included inside LibplaceboPluginProxyTests. Test-only generic source replay.
void VerifyGenericNativeCaptureRenderer(bool beta)
{
    using Steady=std::chrono::steady_clock;
    using OriginalIdentity=std::array<uint64_t,16>;
    const auto env=[](const char* name){char* value=nullptr;size_t n=0;_dupenv_s(&value,&n,name);
        std::string result=value?value:"";free(value);Assert::IsTrue(!result.empty(),L"Generic replay requires explicit input/options");return result;};
    const auto capture=env("VP_GENERIC_CAPTURE_ROOT"), manifestPath=env("VP_GENERIC_CAPTURE_MANIFEST"),
        pinsPath=env("VP_GENERIC_CAPTURE_PINS"), output=env("VP_GENERIC_CAPTURE_OUTPUT"),
        pluginRoot=env("VP_RENDERER_REGRESSION_PLUGIN_ROOT"), configPath=env("VP_GENERIC_CAPTURE_CONFIG");
    const uint64_t clockHz=std::stoull(env("VP_GENERIC_CAPTURE_CLOCK_HZ"));
    Assert::IsTrue(clockHz>0&&clockHz<=1000000000,L"Supply a verified source clock frequency; do not infer it from aspect or frame count");
    struct Input {unsigned index=0;OriginalIdentity original{};std::array<int64_t,11> layout{};std::string binaryHash,sidecarHash;};
    std::vector<Input> inputs;std::ifstream manifest(manifestPath);unsigned count=0;manifest>>count;
    Assert::IsTrue(bool(manifest)&&count>0&&count<=4096,L"Generic replay is bounded to 4096 originals per segment");
    for(unsigned n=0;n<count;++n){Input f;manifest>>f.index;for(auto& v:f.original)manifest>>v;
        Assert::IsTrue(bool(manifest)&&f.index>0&&f.original[1]&&f.original[2]&&f.original[3]&&f.original[12]&&f.original[3]<uint64_t(INT64_MAX));
        if(n){const auto& old=inputs.back();Assert::IsTrue(f.index>old.index&&f.original[1]==old.original[1]+1&&
            f.original[2]==old.original[2]+1&&f.original[3]>old.original[3],L"Export gaps into separate segments; never renumber or fill them");
            for(unsigned k:{0u,4u,5u,6u,7u,8u,9u,12u,15u})Assert::IsTrue(f.original[k]==old.original[k]);
            Assert::IsTrue(!f.original[13]&&!f.original[14],L"Discontinuity/repeat starts a distinct segment");}
        inputs.push_back(f);
    }
    std::string extra;Assert::IsFalse(bool(manifest>>extra));
    const auto hash=[](const unsigned char* bytes,size_t length){BCRYPT_ALG_HANDLE algorithm=nullptr;
        Assert::IsTrue(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
        std::array<unsigned char,32> digest{};const auto status=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(bytes),ULONG(length),digest.data(),ULONG(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm,0);Assert::IsTrue(status>=0);std::ostringstream s;
        for(auto v:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(v);return s.str();};
    const auto read=[](const std::string& path,size_t limit){std::ifstream file(path,std::ios::binary|std::ios::ate);
        Assert::IsTrue(bool(file)&&file.tellg()>0&&uint64_t(file.tellg())<=limit,L"Native input missing or exceeds memory bound");
        std::vector<unsigned char> data(size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());Assert::IsTrue(bool(file));return data;};
    const auto stem=[&](unsigned index){std::ostringstream s;s<<capture<<"/frame-"<<std::setw(6)<<std::setfill('0')<<index;return s.str();};
    std::ifstream pins(pinsPath);
    for(auto& f:inputs){unsigned index=0;pins>>index>>f.binaryHash>>f.sidecarHash;Assert::IsTrue(bool(pins)&&index==f.index&&f.binaryHash.size()==64&&f.sidecarHash.size()==64);
        const auto side=read(stem(f.index)+".txt",4096);Assert::IsTrue(hash(side.data(),side.size())==f.sidecarHash);
        std::istringstream meta(std::string(side.begin(),side.end()));for(auto& v:f.layout)meta>>v;
        std::string version;OriginalIdentity original{};meta>>version;for(auto& v:original)meta>>v;
        Assert::IsTrue(bool(meta)&&version=="identity_v1"&&original==f.original);Assert::IsFalse(bool(meta>>extra));
        const auto& l=f.layout;Assert::IsTrue(l[0]>0&&l[0]<=8192&&l[1]>0&&l[1]<=4320&&l[2]==((l[0]+47)/48)*128&&
            l[3]==0&&l[4]==3&&l[5]==int(VideoFrameEncoding::V210)&&l[6]==int(ColorSpace::REC_709)&&l[10]==1&&f.original[12]<=10000000,
            L"Initial generic fixture supports native limited-range SDR REC709 V210, no inferred conversions");
        if(&f!=&inputs.front())for(unsigned k:{0u,1u,2u,3u,4u,5u,6u,10u})Assert::IsTrue(f.layout[k]==inputs.front().layout[k]);
    }
    Assert::IsFalse(bool(pins>>extra));
    Assert::IsTrue(inputs.back().original[3]-inputs.front().original[3]<=clockHz*120,L"One replay segment is bounded to 120 seconds");
    const int width=int(inputs.front().layout[0]),height=int(inputs.front().layout[1]);
    const auto duration=inputs.front().original[12];
    char preloadText[32]={};const auto preloadLength=GetEnvironmentVariableA("VP_GENERIC_CAPTURE_PRELOAD_MIB",preloadText,32);
    Assert::IsTrue(preloadLength<32);
    const uint64_t preloadMiB=preloadLength?std::stoull(preloadText):0;Assert::IsTrue(preloadMiB<=16384);
    const uint64_t preloadBudget=preloadMiB*1024*1024;uint64_t totalNativeBytes=0;
    for(const auto& f:inputs)totalNativeBytes+=uint64_t(f.layout[2])*uint64_t(f.layout[1]);
    std::vector<std::vector<unsigned char>> preloaded;std::vector<double> preloadCosts;
    if(preloadBudget){MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
        Assert::IsTrue(GlobalMemoryStatusEx(&memory)&&totalNativeBytes<=preloadBudget&&totalNativeBytes+512ull*1024*1024<=memory.ullAvailPhys/2,
            L"Explicit preload bound or available-memory reserve insufficient; no GPU created");
        preloaded.reserve(inputs.size());preloadCosts.reserve(inputs.size());
        for(const auto& f:inputs){const auto start=Steady::now();auto bytes=read(stem(f.index)+".bin",128ull*1024*1024);
            Assert::IsTrue(bytes.size()==size_t(f.layout[2])*size_t(f.layout[1])&&hash(bytes.data(),bytes.size())==f.binaryHash);
            preloaded.push_back(std::move(bytes));preloadCosts.push_back(std::chrono::duration<double,std::milli>(Steady::now()-start).count());}
    }

    struct ReplayClock final:ITimingClock {uint64_t first=0,hz=0;std::atomic<int64_t> epochNs{0};
        timingclocktime_t TimingClockNow()override{const auto epoch=epochNs.load();if(!epoch)return timingclocktime_t(first);
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now().time_since_epoch()).count();
            return timingclocktime_t(first+uint64_t((static_cast<long double>(now-epoch)/1000000000)*hz));}
        timingclocktime_t TimingClockTicksPerSecond()const override{return timingclocktime_t(hz);}
        const TCHAR* TimingClockDescription()override{return _T("Original capture ticks, explicitly supplied frequency, rebased offline epoch");}
    } clock;
    clock.first=inputs.front().original[3];clock.hz=clockHz;Assert::IsTrue(clock.first<uint64_t(INT64_MAX)-clockHz*124);
    const auto pluginPath=pluginRoot+(beta?"/vprenderer-beta/VideoProcessorVPRendererBeta.dll":"/vprenderer/VideoProcessorVPRenderer.dll");
    HMODULE plugin=LoadLibraryExA(pluginPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);Assert::IsNotNull(plugin);
    struct Module{HMODULE value;~Module(){FreeLibrary(value);}}module{plugin};
    const auto version=reinterpret_cast<VideoProcessorLibplaceboGetApiVersionFn>(GetProcAddress(plugin,VP_LIBPLACEBO_VERSION_EXPORT));
    const auto create=reinterpret_cast<VideoProcessorLibplaceboCreateRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_CREATE_EXPORT));
    const auto destroy=reinterpret_cast<VideoProcessorLibplaceboDestroyRendererFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DESTROY_EXPORT));
    const auto diagnostics=reinterpret_cast<VideoProcessorLibplaceboConfigureDiagnosticFn>(GetProcAddress(plugin,VP_LIBPLACEBO_DIAGNOSTIC_EXPORT));
    Assert::IsTrue(version&&version()==VP_LIBPLACEBO_PLUGIN_API_VERSION&&create&&destroy&&diagnostics);
    const auto queryQueue=reinterpret_cast<SourceQueueDispositionQueryV1>(GetProcAddress(plugin,VP_LIBPLACEBO_QUEUE_DISPOSITION_EXPORT));
    SourceQueueDispositionSnapshotV1 queueInitial,queueDrained,queueStopped;
    const bool queueAvailable=queryQueue&&queryQueue(&queueInitial)&&queueInitial.Supported();
    struct LogLine {uint64_t wallNs=0;std::array<char,1024> text{};};
    struct LogState {std::vector<LogLine> lines;std::atomic<uint64_t> next{0},lost{0},truncated{0};} logs;
    logs.lines.resize(16384);static std::atomic<LogState*> logContext{nullptr};logContext.store(&logs);
    struct LogHook {std::atomic<LogState*>* slot;~LogHook(){slot->store(nullptr);}} logHook{&logContext};
    const auto logger=[](const char* message){auto* sink=logContext.load();if(!sink||!message)return;
        const auto i=sink->next.fetch_add(1);if(i>=sink->lines.size()){++sink->lost;return;}
        auto& line=sink->lines[size_t(i)];line.wallNs=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now().time_since_epoch()).count());
        if(strlen(message)>=line.text.size())++sink->truncated;strncpy_s(line.text.data(),line.text.size(),message,_TRUNCATE);};
    struct Record {AnalysisDiagnosticRecordV1 value;uint64_t wallNs;};
    struct Collector {std::vector<Record> rows;std::atomic<uint64_t> next{0},lost{0};} collector;
    collector.rows.resize(131072); // Fixed bounded slots, unique producer index; read only after renderer joins.
    const auto wallNs=[](){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now().time_since_epoch()).count());};
    diagnostics(&collector,[](void* context,const AnalysisDiagnosticRecordV1* record){
        auto& c=*static_cast<Collector*>(context);if(!record)return;
        if(record->structSize!=sizeof(*record)||record->schemaVersion!=1){++c.lost;return;}
        if(!strcmp(record->stage,"submitted-presentation")&&(record->flags&1u)&&!nativeStackHostSubmittedReserve.load())
            nativeStackHostSubmittedReserve.store(NativeRegressionThreadStackReserve());
        const auto slot=c.next.fetch_add(1);
        if(slot>=c.rows.size()){++c.lost;return;}
        c.rows[size_t(slot)]={*record,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now().time_since_epoch()).count())};
    },nullptr,nullptr);
    SnapshotTestCallback callback;
    struct Cleanup {HWND window=nullptr;IVideoRenderer* renderer=nullptr;VideoProcessorLibplaceboDestroyRendererFn destroy;
        VideoProcessorLibplaceboConfigureDiagnosticFn diagnostics;~Cleanup(){if(renderer){renderer->Stop();destroy(renderer);}diagnostics(nullptr,nullptr,nullptr,nullptr);if(window)DestroyWindow(window);}
    } cleanup{CreateWindowExW(0,L"STATIC",L"Offline native capture renderer",WS_POPUP,0,0,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr),nullptr,destroy,diagnostics};
    Assert::IsNotNull(cleanup.window);
    cleanup.renderer=create(&callback,73,cleanup.window,&clock,true,10,VideoConversionOverride::VIDEOCONVERSION_NONE,configPath.c_str(),logger);
    Assert::IsNotNull(cleanup.renderer);
    ConfigFile config;Assert::IsTrue(config.Load(configPath.c_str()));UnifiedProfileRuntime::Runtime runtime;std::string error;
    Assert::IsTrue(runtime.Initialize(config,{},error));UnifiedProfileRuntime::SelectionResult selection;
    Assert::IsTrue(runtime.SelectProfile("subtitles",env("VP_GENERIC_CAPTURE_PROFILE"),true,{},selection,error)&&selection.snapshot);
    CString active;bool restart=false,reset=false;
    Assert::IsTrue(cleanup.renderer->ApplyApplicationState(*selection.snapshot,active,restart,reset)&&!restart);
    cleanup.renderer->SetQueueFramePolicy(4,4,true);cleanup.renderer->SetActivePictureLookaheadFrames(3);
    VideoStateComPtr state=new VideoState();state->valid=true;state->videoFrameEncoding=VideoFrameEncoding::V210;
    state->displayMode=std::make_shared<DisplayMode>(width,height,false,10000000,unsigned(duration));state->eotf=EOTF::SDR;state->colorspace=ColorSpace::REC_709;
    Assert::IsTrue(state->BytesPerRow()==inputs.front().layout[2]&&cleanup.renderer->OnVideoState(state));cleanup.renderer->Build();
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);DEVMODEW display{};display.dmSize=sizeof(display);
    Assert::IsTrue(GetMonitorInfoW(MonitorFromWindow(cleanup.window,MONITOR_DEFAULTTONEAREST),reinterpret_cast<LPMONITORINFO>(&monitor))&&
        EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&display)&&display.dmDisplayFrequency>1);
    cleanup.renderer->SetRenderLoadDisplayRefreshRate(display.dmDisplayFrequency);cleanup.renderer->Start();
    const auto pump=[](){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}};
    const auto readyDeadline=Steady::now()+std::chrono::seconds(30);
    while(callback.state!=RENDERSTATE_RENDERING&&callback.state!=RENDERSTATE_FAILED&&Steady::now()<readyDeadline){pump();Sleep(1);}
    Assert::IsTrue(callback.state==RENDERSTATE_RENDERING,L"Renderer did not start; no source was repeated to warm it");
    struct Owner:IUnknown {std::atomic<ULONG> refs{1};std::vector<unsigned char> bytes;std::shared_ptr<std::atomic<unsigned>> alive;
        Owner(std::vector<unsigned char>&& b,std::shared_ptr<std::atomic<unsigned>> a):bytes(std::move(b)),alive(a){++*alive;}
        ~Owner(){--*alive;}HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
        ULONG AddRef()override{return ++refs;}ULONG Release()override{const auto remaining=--refs;if(!remaining)delete this;return remaining;}
    };
    auto owners=std::make_shared<std::atomic<unsigned>>(0);unsigned peakOwners=0;std::vector<uint64_t> offeredWall;std::vector<double> readCosts,offerCosts,lateness;
    std::ofstream inputLog(output+"/inputs.jsonl");Assert::IsTrue(bool(inputLog));
    char visualFlag[4]={};const bool visual=GetEnvironmentVariableA("VP_GENERIC_CAPTURE_VISUAL",visualFlag,4)>0&&visualFlag[0]=='1';
    struct Timer {HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        ~Timer(){if(handle)CloseHandle(handle);}} pacing;
    Assert::IsNotNull(pacing.handle,L"High-resolution timer unavailable; do not silently qualify rounded Sleep pacing");
    Steady::time_point playbackEpoch;
    for(const auto& f:inputs){auto begin=Steady::now();const size_t ordinal=size_t(&f-inputs.data());
        auto bytes=preloadBudget?std::move(preloaded[ordinal]):read(stem(f.index)+".bin",128ull*1024*1024);
        if(!preloadBudget)Assert::IsTrue(bytes.size()==size_t(f.layout[2])*size_t(f.layout[1])&&hash(bytes.data(),bytes.size())==f.binaryHash,L"Actual pixel bytes differ from the pinned original");
        readCosts.push_back(preloadBudget?preloadCosts[ordinal]:std::chrono::duration<double,std::milli>(Steady::now()-begin).count());
        if(offeredWall.empty()){playbackEpoch=Steady::now();clock.epochNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(playbackEpoch.time_since_epoch()).count());}
        const auto target=playbackEpoch+std::chrono::duration_cast<Steady::duration>(std::chrono::duration<long double>(static_cast<long double>(f.original[3]-clock.first)/clockHz));
        while(Steady::now()<target){pump();const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(target-Steady::now()).count();
            if(remaining<=0)break;LARGE_INTEGER due{};due.QuadPart=-(std::max)(int64_t(1),(std::min)(int64_t(40000),int64_t((remaining+99)/100)));
            Assert::IsTrue(SetWaitableTimer(pacing.handle,&due,0,nullptr,nullptr,FALSE)!=FALSE);
            Assert::AreEqual(DWORD(WAIT_OBJECT_0),WaitForSingleObject(pacing.handle,1000));}
        std::unique_ptr<Owner,void(*)(Owner*)> retained(new Owner(std::move(bytes),owners),[](Owner* value){value->Release();});auto* owner=retained.get();peakOwners=(std::max)(peakOwners,unsigned(owners->load()));
        VideoFrame frame(owner->bytes.data(),f.original[2],timingclocktime_t(f.original[3]),owner,timingclocktime_t(f.original[3]));frame.SetSourceDiscontinuity(f.original[13]!=0);
        bool visualRequested=false;if(visual&&(offeredWall.size()==inputs.size()/3||offeredWall.size()==2*inputs.size()/3)){
            CString status;visualRequested=cleanup.renderer->RequestRenderedOutputCapture(status);}
        const auto offered=wallNs();offeredWall.push_back(offered);const auto start=Steady::now();
        cleanup.renderer->OnVideoFrame(frame);offerCosts.push_back(std::chrono::duration<double,std::milli>(Steady::now()-start).count());
        lateness.push_back(std::chrono::duration<double,std::milli>(start-target).count());
        inputLog<<"{\"index\":"<<f.index<<",\"originalIdentity\":[";for(unsigned k=0;k<16;++k){if(k)inputLog<<',';inputLog<<f.original[k];}
        inputLog<<"],\"offeredWallNs\":"<<offered<<",\"readHashMs\":"<<readCosts.back()<<",\"ingressMs\":"<<offerCosts.back()<<",\"scheduleLateMs\":"<<lateness.back()<<",\"visualRequested\":"<<(visualRequested?"true":"false")<<"}\n";
        pump();Assert::IsTrue(callback.state!=RENDERSTATE_FAILED);
    }
    // Drain observations without adding or repeating any source. Tail omissions remain visible.
    const auto drainEnd=Steady::now()+std::chrono::seconds(2);while(Steady::now()<drainEnd){pump();Sleep(1);}
    const bool drainedQueueAvailable=queueAvailable&&queryQueue(&queueDrained)&&queueDrained.Supported();
    const auto stopStart=Steady::now();cleanup.renderer->Stop();destroy(cleanup.renderer);cleanup.renderer=nullptr;
    const double stopMs=std::chrono::duration<double,std::milli>(Steady::now()-stopStart).count();
    const bool stoppedQueueAvailable=queueAvailable&&queryQueue(&queueStopped)&&queueStopped.Supported();
    diagnostics(nullptr,nullptr,nullptr,nullptr);logContext.store(nullptr);
    std::ofstream logFile(output+"/renderer.log");for(size_t i=0;i<(std::min)(size_t(logs.next.load()),logs.lines.size());++i)
        logFile<<logs.lines[i].wallNs<<'\t'<<logs.lines[i].text.data()<<'\n';
    inputLog.close();Assert::IsTrue(owners->load()==0,L"Original pixel owners leaked after renderer destruction");
    std::ofstream records(output+"/records.jsonl");Assert::IsTrue(bool(records));records<<std::setprecision(17);
    const auto quote=[](const char* text){std::ostringstream s;s<<'"';for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){
        if(*p=='"'||*p=='\\')s<<'\\'<<*p;else if(*p<32)s<<' ';else s<<*p;}s<<'"';return s.str();};
    std::map<std::pair<uint64_t,uint64_t>,size_t> originalBySource;for(size_t n=0;n<inputs.size();++n)originalBySource[{inputs[n].original[2],inputs[n].original[3]}]=n;
    size_t submitted=0,foreign=0;std::set<unsigned> submittedIndices;
    for(size_t ri=0;ri<(std::min)(size_t(collector.next.load()),collector.rows.size());++ri){const auto& item=collector.rows[ri];const auto& r=item.value;
        records<<"{\"stage\":"<<quote(r.stage)<<",\"reason\":"<<quote(r.reason)<<",\"detail\":"<<quote(r.detail)<<",\"flags\":"<<r.flags<<",\"wallNs\":"<<item.wallNs
            <<",\"source\":"<<r.sourceFrameNumber<<",\"timestamp\":"<<r.captureTimestamp<<",\"accepted\":"<<r.acceptedSequence<<",\"transport\":"<<r.transportGeneration<<",\"format\":"<<r.sourceFormatGeneration
            <<",\"viewport\":"<<r.viewportGeneration<<",\"renderer\":"<<r.rendererGeneration<<",\"domain\":"<<r.analysisDomainKey<<",\"policy\":"<<r.policyGeneration<<",\"continuity\":"<<r.continuityGeneration;
        const auto ints=[&](const char* name,const auto& a){records<<",\""<<name<<"\":[";bool first=true;for(auto v:a){if(!first)records<<',';first=false;records<<v;}records<<']';};
        ints("published",r.published);ints("measured",r.measured);ints("candidate",r.candidate);ints("displayed",r.displayed);ints("subtitle",r.subtitle);ints("counters",r.counters);
        records<<",\"metrics\":[";for(unsigned k=0;k<24;++k){if(k)records<<',';if(std::isfinite(r.metrics[k]))records<<r.metrics[k];else records<<"null";}records<<']';
        if(!strcmp(r.stage,"submitted-presentation")&&(r.flags&1u)){++submitted;const auto match=originalBySource.find({r.sourceFrameNumber,r.captureTimestamp});
            if(match==originalBySource.end())++foreign;else{const auto n=match->second;submittedIndices.insert(inputs[n].index);records<<",\"originalIndex\":"<<inputs[n].index<<",\"offerToSubmissionMs\":"<<double(item.wallNs-offeredWall[n])/1e6;}}
        records<<"}\n";
    }
    std::ofstream summary(output+"/summary.json");summary<<"{\"inputs\":"<<inputs.size()<<",\"submittedEvents\":"<<submitted<<",\"distinctSubmittedInputs\":"<<submittedIndices.size()<<",\"foreignSubmitted\":"<<foreign
        <<",\"diagnosticRecordsLost\":"<<collector.lost.load()<<",\"peakNativeOwners\":"<<peakOwners<<",\"clockHz\":"<<clockHz<<",\"originalTimestampsPreserved\":true,\"runtimeTransportIdentityRecreated\":true,\"sourcesRepeatedForWarmup\":false,\"sidecarPlanesUsed\":false,\"screenVisibilityClaim\":false,\"expectedAspectSupplied\":false,\"unsubmittedOriginalIndices\":[";
    bool first=true;for(const auto& f:inputs)if(!submittedIndices.count(f.index)){if(!first)summary<<',';first=false;summary<<f.index;}summary<<"],\"sourcePixelsPreloaded\":"<<(preloadBudget?"true":"false")<<",\"preloadBudgetBytes\":"<<preloadBudget<<",\"nativeInputBytes\":"<<totalNativeBytes
        <<",\"maximumInputLatenessMs\":"<<*std::max_element(lateness.begin(),lateness.end())<<",\"inputScheduleQualified\":"<<(*std::max_element(lateness.begin(),lateness.end())<double(duration)/10000?"true":"false")
        <<",\"stopJoinMs\":"<<stopMs<<",\"logMessagesLost\":"<<logs.lost.load()<<",\"logMessagesTruncated\":"<<logs.truncated.load();
    const auto queueJson=[&](const char* label,const SourceQueueDispositionSnapshotV1& q,bool available){summary<<",\""<<label<<"\":{\"available\":"<<(available?"true":"false")
        <<",\"capacity\":"<<q.capacity<<",\"recovery\":"<<q.recovery<<",\"lifecycle\":"<<q.lifecycle<<",\"dueSource\":"<<q.dueSource<<",\"repeatCopies\":"<<q.repeatCopies<<'}';};
    queueJson("queueInitial",queueInitial,queueAvailable);queueJson("queueBeforeStop",queueDrained,drainedQueueAvailable);queueJson("queueAfterStop",queueStopped,stoppedQueueAvailable);summary<<"}\n";
    Assert::IsTrue(foreign==0&&collector.lost==0,L"Replay source mapping or diagnostics incomplete");
    Assert::IsTrue(submitted>0,L"No actual source was submitted; this is not renderer coverage");
}
