// Replay actual decoded source-raster pixels through the production subtitle
// measurement, lookahead and presentation path. stdin: packed BGRA8 frames.
// Default operation discovers bars from pixels; --diagnostic-bars is explicitly
// labelled and only isolates spatial detection while investigating a failure.
#include <SubtitleBoxLookahead.h>
#include <SubtitleCutPaste.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

namespace {
struct Frame {
    SubtitleBoxObservation observation;
    ActivePictureEvidence raw;
    std::string recoveryReason;
    double boundaryMs = 0.0;
    bool injectedFailure = false, naturallyDetected = false;
};
std::string Csv(const std::string& value) {
    std::string escaped="\"";
    for(char c:value) { if(c=='\"') escaped+='\"'; escaped+=c; }
    return escaped+'\"';
}
bool Integer(const char* value,int& result) {
    char* end=nullptr;const long parsed=std::strtol(value,&end,10);
    if(!value[0] || !end || *end || parsed<0 || parsed>100000) return false;
    result=static_cast<int>(parsed);return true;
}
}

int main(int argc,char** argv) {
    int width=0,height=0,lookahead=0,providedTop=0,providedBottom=0;
    int nearBarDistance=0;
    bool suppliedBars=false, invalid=false;
    std::vector<std::pair<int,int>> injectedGaps;
    for(int i=4;i<argc && !invalid;i+=3) {
        if(std::string(argv[i])=="--near-bar-px" && i+1<argc) {
            invalid=!Integer(argv[i+1],nearBarDistance) || nearBarDistance>200;
            --i;continue;
        }
        int first=0,count=0;
        if(i+2>=argc || !Integer(argv[i+1],first) || !Integer(argv[i+2],count)) {invalid=true;break;}
        if(std::string(argv[i])=="--diagnostic-bars" && !suppliedBars) {
            suppliedBars=true;providedTop=first;providedBottom=count;
        } else if(std::string(argv[i])=="--inject-measurement-gap" && count>0)
            injectedGaps.emplace_back(first,count);
        else invalid=true;
    }
    if(argc<4 || invalid || !Integer(argv[1],width) || !Integer(argv[2],height) ||
        !Integer(argv[3],lookahead) || width<16 || height<16 || width>8192 || height>4320 ||
        lookahead>=static_cast<int>(SubtitleBoxLookahead::MaxFrames) ||
        (suppliedBars && (providedTop>=providedBottom || providedBottom>height))) {
        std::fprintf(stderr,"usage: SubtitleClipProbe width height lookahead[0..7] [--diagnostic-bars top bottom] [--inject-measurement-gap first count]\n");
        return 2;
    }
    _setmode(_fileno(stdin),_O_BINARY);
    std::vector<uint8_t> pixels(static_cast<size_t>(width)*height*4);
    AnalysisLumaSource source;
    source.data=pixels.data();source.dataBytes=pixels.size();source.width=width;source.height=height;
    source.rowBytes=static_cast<size_t>(width)*4;source.format=AnalysisLumaFormat::NativeRgb;
    source.encoding=VideoFrameEncoding::BGRA_8BIT;source.colorspace=ColorSpace::REC_709;source.generation=1;
    SubtitleBoxDetector scanner;scanner.SetNearBarDistance(nearBarDistance);
    SubtitleBoxPresentation presentation;
    SubtitleCutPastePresentation cutPresentation;
    SubtitlePaddingGuard paddingGuard;
    SubtitleBarTrackingReference barTrackingReference;
    std::deque<Frame> queue;
    uint64_t inputs=0,outputs=0,barFrames=0,measuredFrames=0,displayedFrames=0;
    double boundaryTotalMs=0,measurementTotalMs=0,resolveTotalMs=0;
    std::puts("input_frame,diagnostic_supplied_bars,raw_available,raw_class,raw_proposed_left,raw_proposed_top,raw_proposed_right,raw_proposed_bottom,raw_trusted_top,raw_trusted_bottom,raw_axes,top_trusted,bottom_trusted,top_black_fraction,bottom_black_fraction,top_continuity,bottom_continuity,bar_authority,bar_tracking_authority,current_anchor_uses_tracked_edge,has_bar_tracking_reference,bar_reference_sequence,bar_reference_top,bar_reference_bottom,picture_top,picture_bottom,tracked_bottom_attempted,tracked_bottom_detected,tracked_bottom_crosses,tracked_bottom_proved,tracked_bottom_anchor_left,tracked_bottom_anchor_top,tracked_bottom_anchor_right,tracked_bottom_anchor_bottom,tracked_bottom_support,tracked_bottom_black_fraction,tracked_bottom_max_intrusion,measured,measured_lines,measured_left,measured_top,measured_right,measured_bottom,future_available,matching_frames,current_lines_confirmed,displayed,cue,observations,lines,left,top,right,bottom,revised,work_limit,boundary_ms,measurement_ms,resolve_ms,raw_reason,recovery_reason,diagnostic_bar_reason,bar_top_depth,bar_bottom_depth,bar_top_support,bar_bottom_support,bar_top_black_fraction,bar_bottom_black_fraction,bar_samples,padded_left,padded_top,padded_right,padded_bottom,cut_valid,cut_source_left,cut_source_top,cut_source_right,cut_source_bottom,cut_destination_left,cut_destination_top,cut_destination_right,cut_destination_bottom,cut_picture_top,cut_picture_bottom,held,injected_measurement_failure,natural_measured,line0_left,line0_top,line0_right,line0_bottom,line0_farthest_owned_coverage,line1_left,line1_top,line1_right,line1_bottom,line1_farthest_owned_coverage,line2_left,line2_top,line2_right,line2_bottom,line2_farthest_owned_coverage,future_row_frame_index,future_row_line_index,future_row_left,future_row_top,future_row_right,future_row_bottom,future_row_coverage_current,future_row_coverage_farthest,future_row_track_support,panel0_top,panel0_support,panel1_top,panel1_support,panel2_top,panel2_support,source_panel_valid,source_panel_left,source_panel_top,source_panel_right,source_panel_bottom,guard_padding_sides,guard_padding_top,guard_padding_bottom");
    const auto consume=[&]() {
        std::vector<SubtitleBoxObservation> window;
        window.reserve(queue.size());for(const auto& frame:queue)window.push_back(frame.observation);
        const auto start=std::chrono::steady_clock::now();
        const auto preview=SubtitleBoxLookahead::Resolve(window.data(),window.size(),1,1);
        const auto result=presentation.Consume(preview);
        const auto safePadding=paddingGuard.Consume(result,preview,SubtitleBoxPadding{});
        const auto cut=cutPresentation.Consume(result,preview,safePadding);
        const auto padded=result.detected?ExpandSubtitleBox(result.bounds,width,height,safePadding):SubtitleBoxRect{};
        const double resolveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const auto& frame=queue.front();const auto& observation=frame.observation;
        const auto& raw=frame.raw;const auto& measured=observation.text;
        const auto& box=result.bounds;const auto& measuredBox=measured.bounds;
        std::printf("%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%llu,%d,%d,%d,%d,",
            static_cast<unsigned long long>(observation.identity.sourceFrameNumber),suppliedBars?1:0,
            raw.available?1:0,static_cast<int>(raw.classification),raw.proposedBounds.left,raw.proposedBounds.top,
            raw.proposedBounds.right,raw.proposedBounds.bottom,raw.trustedBounds.top,raw.trustedBounds.bottom,
            static_cast<unsigned>(raw.trustedBounds.trustedBarAxes),raw.top.trusted?1:0,raw.bottom.trusted?1:0,
            raw.top.blackFraction,raw.bottom.blackFraction,raw.top.continuity,raw.bottom.continuity,
            observation.barAuthority?1:0,observation.barTrackingAuthority?1:0,
            observation.currentAnchorUsesTrackedEdge?1:0,observation.hasBarTrackingReference?1:0,
            static_cast<unsigned long long>(observation.barTrackingReferenceIdentity.acceptedSequence),
            observation.barTrackingReferenceTop,observation.barTrackingReferenceBottom,
            observation.pictureTop,observation.pictureBottom);
        std::printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%d,",
            observation.trackedBottomHypothesisAttempted?1:0,
            observation.trackedBottomHypothesisDetected?1:0,
            observation.trackedBottomHypothesisCrosses?1:0,
            observation.trackedBottomHypothesisProved?1:0,
            observation.trackedBottomHypothesisAnchor.left,observation.trackedBottomHypothesisAnchor.top,
            observation.trackedBottomHypothesisAnchor.right,observation.trackedBottomHypothesisAnchor.bottom,
            observation.trackedBottomHypothesisSupport,
            observation.trackedBottomHypothesisBlackFraction,
            observation.trackedBottomHypothesisMaxIntrusion);
        std::printf("%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%llu,%u,%d,%d,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%s,%s,",
            measured.detected?1:0,measured.lineCount,measuredBox.left,measuredBox.top,measuredBox.right,measuredBox.bottom,
            preview.futureAvailable?1:0,preview.matchingFrames,preview.currentLinesConfirmed?1:0,
            result.detected?1:0,static_cast<unsigned long long>(result.cue),result.observations,result.lineCount,
            box.left,box.top,box.right,box.bottom,result.revised?1:0,measured.workLimit?1:0,
            frame.boundaryMs,observation.analysisMs,resolveMs,Csv(raw.reason).c_str(),Csv(frame.recoveryReason).c_str());
        const auto& bars=observation.barEvidence;
        std::printf("%s,%d,%d,%d,%d,%.6f,%.6f,%zu,",Csv(bars.reason).c_str(),bars.topDepth,bars.bottomDepth,
            bars.topSupport,bars.bottomSupport,bars.topBlackFraction,bars.bottomBlackFraction,bars.samples);
        std::printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            padded.left,padded.top,padded.right,padded.bottom,cut.valid?1:0,
            cut.source.left,cut.source.top,cut.source.right,cut.source.bottom,
            cut.destination.left,cut.destination.top,cut.destination.right,cut.destination.bottom,
            cut.pictureTop,cut.pictureBottom,result.held?1:0,frame.injectedFailure?1:0,frame.naturallyDetected?1:0);
        for(int line=0;line<3;++line) {
            const auto& bounds=measured.lineBounds[line];
            std::printf(",%d,%d,%d,%d",bounds.left,bounds.top,bounds.right,bounds.bottom);
            const double coverage=line<measured.lineCount && !window.empty()
                ? SubtitleBoxLookahead::OwnedLinePixelCoverage(observation,line,window.back()) : 0.0;
            std::printf(",%.6f",coverage);
        }
        int futureRowFrame=-1,futureRowLine=-1;SubtitleBoxRect futureRow{};
        double futureCurrentCoverage=0.0,futureFarthestCoverage=0.0;unsigned futureTrackSupport=0;
        const int lineTolerance=(std::max)(4,observation.height/180);
        for(size_t i=1;i<(std::min)(window.size(),static_cast<size_t>(preview.matchingFrames));++i)
            for(int line=0;line<window[i].text.lineCount;++line) {
                bool alreadyCurrent=false;
                for(int current=0;current<measured.lineCount;++current)
                    alreadyCurrent=alreadyCurrent ||
                        (SubtitleBoxLookahead::SameLine(window[i].text.lineBounds[line],
                            measured.lineBounds[current],lineTolerance) &&
                         SubtitleBoxLookahead::SimilarSignature(window[i].text.lineSignatures[line],
                            measured.lineSignatures[current]));
                const auto& bounds=window[i].text.lineBounds[line];
                if(alreadyCurrent || !bounds.Valid() || (futureRow.Valid() && bounds.top>=futureRow.top))continue;
                futureRow=bounds;futureRowFrame=static_cast<int>(i);futureRowLine=line;
                futureCurrentCoverage=SubtitleBoxLookahead::OwnedLinePixelCoverage(window[i],line,observation);
                futureFarthestCoverage=window.empty()?0.0:
                    SubtitleBoxLookahead::OwnedLinePixelCoverage(window[i],line,window.back());
                futureTrackSupport=0;
                for(size_t frame=1;frame<window.size();++frame)
                    for(int other=0;other<window[frame].text.lineCount;++other)
                        if(SubtitleBoxLookahead::SameCrowdedLine(bounds,
                            window[frame].text.lineBounds[other],lineTolerance) &&
                            SubtitleBoxLookahead::SimilarSignature(window[i].text.lineSignatures[line],
                                window[frame].text.lineSignatures[other])) {
                            ++futureTrackSupport;break;
                        }
            }
        std::printf(",%d,%d,%d,%d,%d,%d,%.6f,%.6f,%u",futureRowFrame,futureRowLine,
            futureRow.left,futureRow.top,futureRow.right,futureRow.bottom,
            futureCurrentCoverage,futureFarthestCoverage,futureTrackSupport);
        for(const auto& panel:observation.text.panelTopEdges)
            std::printf(",%d,%u",panel.y,panel.supportBasisPoints);
        std::printf(",%d,%d,%d,%d,%d",result.sourcePanel.Valid(),result.sourcePanel.left,
            result.sourcePanel.top,result.sourcePanel.right,result.sourcePanel.bottom);
        std::printf(",%d,%d,%d",safePadding.sides,safePadding.top,safePadding.bottom);
        std::putchar('\n');
        ++outputs;barFrames+=observation.barAuthority;measuredFrames+=measured.detected;displayedFrames+=result.detected;
        boundaryTotalMs+=frame.boundaryMs;measurementTotalMs+=observation.analysisMs;resolveTotalMs+=resolveMs;
        queue.pop_front();
    };
    for(;;) {
        const size_t bytes=std::fread(pixels.data(),1,pixels.size(),stdin);
        if(!bytes)break;
        if(bytes!=pixels.size()) {std::fprintf(stderr,"incomplete BGRA frame %llu: %zu/%zu bytes\n",inputs,bytes,pixels.size());return 3;}
        const ActivePictureFrameIdentity identity={1,inputs+1,inputs,inputs+1,1,1,1};
        Frame frame;
        const auto start=std::chrono::steady_clock::now();
        frame.raw=ExtractActivePictureEvidence(source);
        frame.boundaryMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        frame.observation=SubtitleBoxLookahead::Measure(scanner,source,frame.raw,identity,false,
            barTrackingReference,1,1);
        // Additional diagnostic explanation is outside production timing.
        frame.recoveryReason=EvaluateSymmetricVerticalBarHypothesis(source,frame.raw,true).reason;
        if(suppliedBars) {
            const auto diagnosticStart=std::chrono::steady_clock::now();
            frame.observation.pictureTop=providedTop;frame.observation.pictureBottom=providedBottom;
            frame.observation.barAuthority=providedTop>0 || providedBottom<height;
            scanner.Reset();
            frame.observation.nearBarDistance=nearBarDistance;
            frame.observation.text=scanner.Analyze(source,providedTop,providedBottom,identity.acceptedSequence,1);
            frame.observation.ink=scanner.InkSnapshot();
            frame.observation.analysisMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-diagnosticStart).count();
        }
        barTrackingReference=suppliedBars ? SubtitleBarTrackingReference{} :
            SubtitleBoxLookahead::AdvanceBarTrackingReference(frame.observation);
        frame.naturallyDetected=frame.observation.text.detected;
        for(const auto& gap:injectedGaps) if(inputs>=static_cast<uint64_t>(gap.first) &&
            inputs<static_cast<uint64_t>(gap.first)+gap.second) frame.injectedFailure=true;
        // Explicit fault injection changes segmentation output only. Every input
        // pixel, current bar proof, and pre-grouping ink snapshot remain intact.
        if(frame.injectedFailure) frame.observation.text={};
        ++inputs;queue.push_back(std::move(frame));
        if(queue.size()>static_cast<size_t>(lookahead))consume();
    }
    if(std::ferror(stdin)) {std::fprintf(stderr,"error reading BGRA input\n");return 3;}
    while(!queue.empty())consume();
    std::fprintf(stderr,"frames=%llu bars=%llu measured=%llu displayed=%llu boundary_ms=%.3f measurement_ms=%.3f resolve_ms=%.3f\n",
        outputs,barFrames,measuredFrames,displayedFrames,boundaryTotalMs,measurementTotalMs,resolveTotalMs);
    return 0;
}
