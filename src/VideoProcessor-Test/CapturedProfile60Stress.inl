// Included inside VerifyCapturedV210CueCadence's separate60Hz stress branch.
// No production authority or prior fixture assertions are replaced here.
std::set<uint64_t> stressSources;
std::set<uint64_t> stressWorkSources;
std::vector<int> stressModeRuns;
std::array<unsigned,4> stressModeOutputs{};
std::vector<double> stressLatencies;
unsigned stressOutputs=0,stressRaw=0,stressComposed=0,stressRepeats=0,stressUnknownProfiles=0;
unsigned stressMovieChanges=0,stressCaptionOutside=0,stressWorkSamples=0;
bool stressExact=true,stressOrdered=true;
uint64_t stressPrevious=0,stressLastOutputSource=0,stressMaxSourceGap=0,stressFirstWall=0,stressLastWall=0;
double stressMaxSubmitGap=0,stressMaxLatency=0,stressMaxWork=0,stressMaxPresent=0,stressMaxCritical=0;
const double stressScreenTop=(1125.0-2000.0/2.35)*.5,stressScreenBottom=1125.0-stressScreenTop;
for(const auto& r:observed) {
    if(r.sourceFrameNumber<steadyStart||r.sourceFrameNumber>steadyEnd)continue;
    if(!strcmp(r.stage,"source-frame-work")&&(r.flags&1u)) {
        const auto ingress=ingressBySource.find(r.sourceFrameNumber);
        if(ingress!=ingressBySource.end()&&std::isfinite(r.metrics[0])&&std::isfinite(r.metrics[4])&&
            r.metrics[0]>=0&&r.metrics[4]>=0) {
            ++stressWorkSamples;
            stressWorkSources.insert(r.sourceFrameNumber);
            stressMaxWork=(std::max)(stressMaxWork,r.metrics[0]+ingress->second);
            stressMaxPresent=(std::max)(stressMaxPresent,r.metrics[4]);
            stressMaxCritical=(std::max)(stressMaxCritical,r.metrics[0]+r.metrics[4]+ingress->second);
        }
    }
    if(strcmp(r.stage,"submitted-presentation")||!(r.flags&1u))continue;
    ++stressOutputs;
    const bool composed=(r.flags&2u)!=0,repeat=(r.flags&16384u)!=0;
    if(composed)++stressComposed;else ++stressRaw;
    if(repeat)++stressRepeats;
    const auto input=inputs.find(r.sourceFrameNumber);
    const bool temporal=(r.flags&(128u|256u))!=0;
    const bool order=r.counters[0]>=r.counters[1];
    const auto lag=order?r.counters[0]-r.counters[1]:uint64_t(11);
    const bool exact=input!=inputs.end()&&r.captureTimestamp==uint64_t(input->second.first)&&
        r.acceptedSequence==r.counters[1]&&order&&(temporal?lag<=10:(repeat?lag<=1:lag==0))&&
        (!(r.flags&4u)||((r.flags&8u)&&r.counters[7]==r.counters[1]));
    stressExact&=exact;
    const int profile=std::isfinite(r.metrics[20])?int(std::lround(r.metrics[20])):-1;
    const bool validProfile=profile>=0&&profile<4&&std::abs(r.metrics[20]-profile)<.001;
    if(!validProfile)++stressUnknownProfiles;
    else {++stressModeOutputs[profile];if(stressModeRuns.empty()||stressModeRuns.back()!=profile)stressModeRuns.push_back(profile);}
    stressOrdered&=!stressLastOutputSource||r.sourceFrameNumber>=stressLastOutputSource;
    stressLastOutputSource=r.sourceFrameNumber;
    if(stressSources.insert(r.sourceFrameNumber).second) {
        if(stressPrevious) {
            stressOrdered&=r.sourceFrameNumber>stressPrevious;
            if(r.sourceFrameNumber>stressPrevious)stressMaxSourceGap=(std::max)(stressMaxSourceGap,r.sourceFrameNumber-stressPrevious);
        }
        stressPrevious=r.sourceFrameNumber;
    }
    const auto wall=classicPoseSubmitWallNs.find(r.recordSequence);
    if(wall!=classicPoseSubmitWallNs.end()) {
        if(stressLastWall)stressMaxSubmitGap=(std::max)(stressMaxSubmitGap,double(wall->second-stressLastWall)/1e6);
        if(!stressFirstWall)stressFirstWall=wall->second;stressLastWall=wall->second;
        if(input!=inputs.end()&&wall->second>=input->second.second) {
            const double latency=double(wall->second-input->second.second)/1e6;
            stressLatencies.push_back(latency);stressMaxLatency=(std::max)(stressMaxLatency,latency);
        }
    }
    const double sw=r.metrics[10]-r.metrics[8],sh=r.metrics[11]-r.metrics[9];
    bool movieChanged=true,captionInside=false;
    std::array<double,4> landmarks{};
    const auto phase=capturedPhaseBySource.find(r.sourceFrameNumber);
    bool finiteGeometry=true;for(unsigned i=8;i<20;++i)finiteGeometry&=std::isfinite(r.metrics[i]);
    if(finiteGeometry&&sw>0&&sh>0&&phase!=capturedPhaseBySource.end()) {
        const auto px=[&](double x){return r.metrics[12]+(x-r.metrics[8])*(r.metrics[14]-r.metrics[12])/sw;};
        const auto py=[&](double y){return r.metrics[13]+(y-r.metrics[9])*(r.metrics[15]-r.metrics[13])/sh;};
        landmarks={{px(960),py(720),px(2880),py(1440)}};movieChanged=false;
        for(unsigned i=0;i<4;++i)movieChanged|=std::abs(landmarks[i]-stressLandmarks[i])>.001;
        const auto inScreen=[&](double l,double t,double rr,double b) {
            return px(l)>=-.001&&px(rr)<=2000.001&&py(t)>=stressScreenTop-.001&&py(b)<=stressScreenBottom+.001;
        };
        const auto& lines=currentLineExpectations.at(phase->second);
        if(composed) {
            // This selected-operation envelope is NOT an exhaustive glyph mask.
            captionInside=r.metrics[18]>r.metrics[16]&&r.metrics[19]>r.metrics[17]&&
                inScreen(r.metrics[16],r.metrics[17],r.metrics[18],r.metrics[19]);
            for(const auto& line:lines)captionInside&=r.subtitle[0]<=line.left&&r.subtitle[1]<=line.top&&
                r.subtitle[2]>=line.right&&r.subtitle[3]>=line.bottom;
        } else {
            captionInside=true;
            for(const auto& line:lines)captionInside&=r.metrics[8]<=line.left&&r.metrics[9]<=line.top&&
                r.metrics[10]>=line.right&&r.metrics[11]>=line.bottom&&inScreen(line.left,line.top,line.right,line.bottom);
        }
    }
    if(movieChanged)++stressMovieChanges;if(!captionInside)++stressCaptionOutside;
    std::ostringstream row;row<<"PROFILE60 OUTPUT: source="<<r.sourceFrameNumber<<" accepted="<<r.acceptedSequence
        <<" original_index="<<(phase==capturedPhaseBySource.end()?0:profileCaptureIndices.at(phase->second))
        <<" selected_profile="<<profile<<" policy="<<r.policyGeneration<<" continuity="<<r.continuityGeneration
        <<" composed="<<composed<<" repeat="<<repeat<<" exact="<<exact<<" movie_changed="<<movieChanged
        <<" caption_inside_physical_screen="<<captionInside<<" landmarks="<<landmarks[0]<<','<<landmarks[1]<<','<<landmarks[2]<<','<<landmarks[3];
    NativeRendererFixtureLog(row.str().c_str());
}
const double stressInputHz=double(steadyCount-1)*1e9/double(inputs.at(steadyEnd).second-inputs.at(steadyStart).second);
double stressMaxInputGap=0,stressMaxOfferLateness=0;
for(uint64_t s=steadyStart+1;s<=steadyEnd;++s)stressMaxInputGap=(std::max)(stressMaxInputGap,double(inputs.at(s).second-inputs.at(s-1).second)/1e6);
for(uint64_t s=steadyStart;s<=steadyEnd;++s)stressMaxOfferLateness=(std::max)(stressMaxOfferLateness,stressOfferLateness.at(s));
const bool stressInputPacingQualified=stressInputHz>=.97*inputHz&&stressMaxInputGap<=1.25*periodMs&&stressMaxOfferLateness<=.25*periodMs;
const double stressOutputHz=stressLastWall>stressFirstWall?double(stressOutputs-1)*1e9/double(stressLastWall-stressFirstWall):0;
const double stressDistinctHz=stressLastWall>stressFirstWall?double(stressSources.size()-1)*1e9/double(stressLastWall-stressFirstWall):0;
double stressEarly=0,stressLate=0;
if(stressLatencies.size()>=48)for(size_t i=0;i<24;++i) {stressEarly+=stressLatencies[i]/24;stressLate+=stressLatencies[stressLatencies.size()-24+i]/24;}
const auto capacity=stressQueueAfter.capacity-stressQueueBefore.capacity;
const auto recovery=stressQueueAfter.recovery-stressQueueBefore.recovery;
const auto lifecycle=stressQueueAfter.lifecycle-stressQueueBefore.lifecycle;
const auto due=stressQueueAfter.dueSource-stressQueueBefore.dueSource;
auto appliedRuns=stressModeRuns;
// An already queued output may still carry the original Reconstruction profile
// before the first requested Black profile is actually selected. Its pixels and
// timing were checked above; only the expected run-name sequence accounts for it.
if(appliedRuns.size()==7&&appliedRuns.front()==3)appliedRuns.erase(appliedRuns.begin());
const bool profileOrder=appliedRuns==std::vector<int>({2,3,1,0,2,3});
const double expectedOutputHz=(std::min)(inputHz,outputHz),displayPeriodMs=1000.0/outputHz;
const uint64_t expectedSourceGap=uint64_t(std::ceil(inputHz/expectedOutputHz))+1;
std::ostringstream summary;summary<<"PROFILE60 SUMMARY: original_hz="<<24000.0/1001<<" synthetic_input_hz="<<inputHz
    <<" actual_input_hz="<<stressInputHz<<" display_hz="<<outputHz<<" submitted_hz="<<stressOutputHz<<" distinct_hz="<<stressDistinctHz
    <<" inputs="<<steadyCount<<" outputs="<<stressOutputs<<" distinct="<<stressSources.size()<<" repeats="<<stressRepeats
    <<" max_source_gap="<<stressMaxSourceGap<<" cadence_gap_limit="<<expectedSourceGap<<" max_input_gap_ms="<<stressMaxInputGap
    <<" max_offer_lateness_ms="<<stressMaxOfferLateness<<" producer_pacing_qualified="<<stressInputPacingQualified
    <<" max_submit_gap_ms="<<stressMaxSubmitGap<<" exact="<<stressExact<<" ordered="<<stressOrdered
    <<" requested_profiles="<<stressAppliedProfiles<<" profile_order="<<profileOrder<<" unknown_profile_outputs="<<stressUnknownProfiles
    <<" composed="<<stressComposed<<" raw="<<stressRaw<<" all_prefix_movie_changes="<<stressMovieChanges
    <<" all_prefix_caption_outside_screen="<<stressCaptionOutside<<" capacity_retirements="<<capacity<<" recovery_retirements="<<recovery
    <<" lifecycle_retirements="<<lifecycle<<" due_source_retirements="<<due
    <<" work_samples="<<stressWorkSamples<<" distinct_work_sources="<<stressWorkSources.size()
    <<" unsubmitted_input_sources="<<steadyCount-stressSources.size()<<" max_active_work_plus_ingress_ms="<<stressMaxWork
    <<" max_present_block_ms="<<stressMaxPresent<<" max_work_present_ingress_ms="<<stressMaxCritical
    <<" max_latency_ms="<<stressMaxLatency<<" early_latency_ms="<<stressEarly<<" late_latency_ms="<<stressLate;
for(unsigned i=0;i<6;++i)summary<<" profile_call"<<i<<"_ms="<<stressApplyMs[i];
for(unsigned i=0;i<4;++i)summary<<" profile"<<i<<"_outputs="<<stressModeOutputs[i];
NativeRendererFixtureLog(summary.str().c_str());
Assert::IsTrue(stressProfileOkay&&stressAppliedProfiles==6&&profileOrder&&stressUnknownProfiles==0,
    L"All actual selected profile epochs must be observed; requested input blocks are not output authority.");
Assert::IsTrue(stressExact&&stressOrdered&&stressSources.size()>0&&stressMaxSourceGap<=expectedSourceGap,
    L"Current-source identities/order must hold with only explicitly bounded cadence-conversion gaps.");
Assert::IsTrue(stressInputPacingQualified,
    L"Producer timing is not qualified: diagnose measured ingress stalls versus scheduling lateness before attributing output cadence failure to VP.");
Assert::IsTrue(stressDistinctHz>=.97*expectedOutputHz&&stressMaxSubmitGap<=2*displayPeriodMs+periodMs,
    L"Observed source arrival or output cadence failed the declared60Hz-input/display-rate contract.");
Assert::IsTrue(capacity==0&&recovery==0&&lifecycle==0,
    L"Optional profile updates must not hide capture queue losses behind cadence conversion.");
Assert::IsTrue(stressWorkSources.size()>=stressSources.size()*.9&&stressMaxWork<=periodMs&&
    stressMaxCritical<=(std::max)(periodMs,displayPeriodMs),
    L"Measured active work or work+Present+ingress exceeds its source/display deadline; this is not a GPU-total measurement.");
Assert::IsTrue(stressLatencies.size()>=48&&stressMaxLatency<=10*periodMs+displayPeriodMs&&stressLate<=stressEarly+periodMs,
    L"Profile transitions caused excessive or growing source latency.");
Assert::AreEqual(0u,stressMovieChanges,L"Every submitted profile-transition prefix must preserve the established movie landmark map; record legitimate policy changes separately rather than dropping prefixes.");
Assert::AreEqual(0u,stressCaptionOutside,L"Every current caption must remain inside the physical screen; no composition/geometry success may be inferred from stale metadata.");
