#include "pch.h"
#include <SourcePictureMeasurement.h>
#include "CppUnitTest.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <chrono>
#include <sstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VideoProcessorTest
{
    namespace
    {
        // Independent owned P210 fixture; all geometry is native source pixels.
        struct SourcePictureFrame
        {
            int width,height;
            size_t pitch;
            std::vector<uint8_t> bytes;
            SourcePictureFrame(int w,int h,int floor=64):width(w),height(h),pitch(size_t(w)*2),bytes(pitch*h*2)
            { Fill(0,0,width,height,floor); }
            static void Code(uint8_t* destination,int value)
            {
                const uint16_t word=uint16_t(value<<6);
                destination[0]=uint8_t(word);destination[1]=uint8_t(word>>8);
            }
            void Fill(int left,int top,int right,int bottom,int y,int u=512,int v=512)
            {
                left=std::max(0,left);right=std::min(width,right);
                top=std::max(0,top);bottom=std::min(height,bottom);
                for(int row=top;row<bottom;++row) {
                    auto* luma=bytes.data()+size_t(row)*pitch;
                    auto* chroma=bytes.data()+size_t(height+row)*pitch;
                    for(int x=left;x<right;++x)Code(luma+size_t(x)*2,y);
                    for(int x=(left/2)*2;x<right;x+=2) {
                        Code(chroma+size_t(x)*2,u);Code(chroma+size_t(x+1)*2,v);
                    }
                }
            }
            AnalysisLumaSource Source(AnalysisLumaRange range=AnalysisLumaRange::Limited)const
            {
                AnalysisLumaSource result{bytes.data(),bytes.size(),width,height,pitch,pitch,
                    AnalysisLumaFormat::P210,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                result.range=range;return result;
            }
            void Caption(int lines,int firstY)
            {
                for(int line=0;line<lines;++line)
                    for(int glyph=0;glyph<18;++glyph) {
                        const int x=width/2-126+glyph*14;
                        Fill(x,firstY+line*22,x+7,firstY+line*22+13,850);
                    }
            }
        };
    }
    TEST_CLASS(SourcePictureMeasurementTests)
    {
        static ActivePictureBounds Bounds(int width,int height,int left,int top,int right,int bottom)
        {
            ActivePictureBounds result;result.rasterWidth=width;result.rasterHeight=height;
            result.left=left;result.top=top;result.right=right;result.bottom=bottom;
            result.aspectRatio=double(right-left)/(bottom-top);return result;
        }
        static void Exact(const SourcePictureObservation& measured,const ActivePictureBounds& expected)
        {
            Logger::WriteMessage(measured.reason);
            Assert::IsTrue(measured.complete);Assert::IsTrue(measured.rectangleAvailable);
            Assert::IsTrue(measured.outcome==SourcePictureOutcome::Measured);
            Assert::AreEqual(expected.left,measured.measuredBounds.left);
            Assert::AreEqual(expected.top,measured.measuredBounds.top);
            Assert::AreEqual(expected.right,measured.measuredBounds.right);
            Assert::AreEqual(expected.bottom,measured.measuredBounds.bottom);
            Assert::AreEqual(expected.aspectRatio,measured.measuredBounds.aspectRatio,1e-9);
        }
    public:
        TEST_METHOD(SourcePictureCanvasEstimateRequiresIndependentPanelScaffold)
        {
            for(int variant=0;variant<6;++variant) {
                SourcePictureFrame frame(960,540);
                if(variant!=3)frame.Fill(34,100,960,325,200,530,490);
                if(variant!=1 && variant!=3)frame.Fill(34,385,960,540,180,490,540);
                if(variant==0 || variant==1 || variant==3) {
                    frame.Fill(34,15,60,43,200,560,480);
                    frame.Fill(360,20,422,40,180);
                    frame.Fill(908,15,926,43,220,450,650);
                    frame.Fill(35,64,165,82,200);
                }
                if(variant==4) {
                    frame.Caption(3,5);
                    frame.Fill(22,14,48,40,220,560,480);
                }
                if(variant==5)for(int x:{48,300,560,840})frame.Fill(x,20,x+4,24,240);
                const auto observed=MeasureSourcePicture(frame.Source());
                Logger::WriteMessage(observed.reason);
                Assert::IsTrue(observed.complete);
                const auto assessment=ReconcileSourcePictureCandidate(observed,Bounds(960,540,34,100,960,540));
                Assert::IsTrue(assessment.evaluated);
                if(variant==0) {
                    Assert::IsTrue(assessment.contradicted);
                    Assert::IsTrue(assessment.canvasScaffoldSupported);
                    Assert::IsTrue(observed.canvasSupported);
                    Assert::IsTrue(observed.outcome==SourcePictureOutcome::CanvasEstimate);
                    Assert::IsTrue(observed.blankMarginsUnresolved);
                } else {
                    Assert::IsFalse(assessment.canvasScaffoldSupported,
                        L"A contradictory header alone, one continuous movie, glyph rows, isolated logo or noise is not positive composite panel-layout evidence.");
                    Assert::IsFalse(observed.canvasSupported);
                }
            }
        }
        TEST_METHOD(SourcePictureClippedExteriorRetainsHeaderConnectedToBody)
        {
            SourcePictureFrame frame(3840,2160);
            frame.Fill(136,332,3840,1300,180,530,490);
            frame.Fill(136,1540,3840,2160,140,490,540);
            frame.Fill(136,59,234,165,180,560,480);
            frame.Fill(1430,80,1677,150,150);
            frame.Fill(3644,59,3706,165,190,450,650);
            frame.Fill(138,239,666,298,200);
            // This source column coincides with an overview center; the thin
            // connector makes avatar/title/body one component before clipping.
            frame.Fill(180,160,181,340,220);
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsTrue(observed.complete);Assert::IsTrue(observed.topologyComplete);
            const auto assessment=ReconcileSourcePictureCandidate(observed,Bounds(3840,2160,136,332,3840,2160));
            Assert::IsTrue(assessment.contradicted,
                L"Connectivity into the candidate interior cannot erase genuinely occupied exterior header pixels.");
            Assert::IsTrue(assessment.canvasScaffoldSupported);
            Assert::IsTrue(observed.canvasSupported);
            Assert::IsTrue(observed.blankMarginsUnresolved);
        }
        TEST_METHOD(SourcePictureAdversarialTopologyStopsWithoutClaimingCompleteGeometry)
        {
            SourcePictureFrame frame(960,540);
            for(int y=0;y<540;y+=6)for(int x=0;x<960;x+=6)frame.Fill(x,y,x+2,y+2,240);
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsFalse(observed.complete);
            Assert::IsFalse(observed.topologyComplete);
            Assert::IsTrue(observed.outcome==SourcePictureOutcome::Partial);
            Assert::IsFalse(observed.rectangleAvailable);Assert::IsFalse(observed.canvasSupported);
            Assert::IsTrue(observed.components.size()<size_t(observed.overviewWidth)*observed.overviewHeight,
                L"Pathological disconnected noise must hit a bounded work cap rather than enter unbounded pairwise component classification.");
            Assert::IsFalse(ReconcileSourcePictureCandidate(observed,Bounds(960,540,0,80,960,460)).evaluated);
        }
        TEST_METHOD(SourcePictureSmoothRasterGradientCannotBecomeAnInsetMeasuredBorder)
        {
            for(bool reverse:{false,true}) {
                SourcePictureFrame frame(960,540);
                for(int x=0;x<960;++x) {
                    const int position=reverse?959-x:x;
                    frame.Fill(x,0,x+1,540,80+80*position/959);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Logger::WriteMessage(observed.reason);
                Assert::IsTrue(observed.complete);
                Assert::IsTrue(!observed.rectangleAvailable ||
                    (observed.measuredBounds.left==0 && observed.measuredBounds.top==0 &&
                     observed.measuredBounds.right==960 && observed.measuredBounds.bottom==540),
                    L"A smooth current scene gradient has no encoded matte boundary; drifting six codes from its perimeter is not an inset edge.");
            }
        }
        TEST_METHOD(SourcePictureOverviewPreservesSupportedComponentsAcrossSamplingPhases)
        {
            SourcePictureFrame baseline(960,540);
            baseline.Fill(160,160,800,480,200,530,490);
            const auto coverage=MeasureSourcePicture(baseline.Source());
            Assert::IsTrue(coverage.overviewStepX>0 && coverage.overviewStepY>0);
            Assert::IsTrue(2*coverage.overviewStepX<=16 && 2*coverage.overviewStepY<=26,
                L"The declared support scale must resolve these avatar/logo-sized fixtures before their presence is required.");
            for(int phaseX=0;phaseX<coverage.overviewStepX;++phaseX)
                for(int phaseY=0;phaseY<coverage.overviewStepY;++phaseY) {
                SourcePictureFrame frame(960,540);
                frame.Fill(160,160,800,480,200,530,490);
                frame.Fill(32+phaseX,24+phaseY,56+phaseX,50+phaseY,240);
                frame.Fill(854+phaseX,24+phaseY,870+phaseX,50+phaseY,200);
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                for(int centerX:{44+phaseX,862+phaseX}) {
                    const int centerY=37+phaseY;
                    Assert::IsTrue(std::any_of(observed.components.begin(),observed.components.end(),[&](const SourcePictureComponent& component) {
                        return component.samples>0 && component.left<=centerX && component.right>centerX &&
                            component.top<=centerY && component.bottom>centerY;
                    }),L"A source component above the declared overview support scale must survive translation through each sampling phase.");
                }
            }
        }
        TEST_METHOD(SourcePictureSparseOverviewCannotClaimDenseAbsenceCoverage)
        {
            for(int phase=0;phase<2;++phase) {
                SourcePictureFrame frame(960,540);
                frame.Fill(160,160,800,480,200,530,490);
                frame.Fill(120,32+phase,360,33+phase,240);
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete,
                    L"Completed bounded overview work and dense source coverage are different facts.");
                Assert::IsTrue(observed.overviewStepX>1 || observed.overviewStepY>1);
                Assert::IsFalse(observed.denseCoverage,
                    L"An unobserved one-pixel structure cannot be certified absent by a sparse overview. Coarse completion is not safe discard permission.");
                // The one-pixel feature may be detected or unobserved. Neither
                // its absence from components nor no exterior contradiction
                // is a proof that the proposed excluded pixels are matte.
                const auto assessment=ReconcileSourcePictureCandidate(observed,Bounds(960,540,160,160,800,480));
                Assert::IsTrue(assessment.evaluated);
                Assert::IsFalse(observed.denseCoverage);
            }
        }
        TEST_METHOD(SourcePictureLongCenteredGlyphRowsDoNotExpandRealPicture)
        {
            for(int lines=1;lines<=3;++lines) {
                SourcePictureFrame frame(960,540);
                frame.Fill(0,66,960,426,220,530,490);
                for(int line=0;line<lines;++line)for(int glyph=0;glyph<52;++glyph) {
                    const int x=64+glyph*16,y=440+line*26;
                    frame.Fill(x,y,x+10,y+18,850);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Exact(observed,Bounds(960,540,0,66,960,426));
                Assert::IsFalse(observed.canvasSupported,
                    L"A long single subtitle row remains a local glyph structure even when it spans several width quarters.");
            }
        }
        TEST_METHOD(SourcePictureKeepsCompetingEdgesBeyondTheFirstHitOnEveryRay)
        {
            SourcePictureFrame frame(960,540);
            // A detached broad graphic occludes every vertical probe before
            // the independent scene boundary. Both are real measured features.
            frame.Fill(0,20,960,40,240);
            frame.Fill(0,80,960,460,200,530,490);
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsTrue(observed.complete);
            const auto& top=observed.edges[unsigned(SourcePictureEdge::Top)];
            for(int coordinate:{20,80})
                Assert::IsTrue(std::any_of(top.begin(),top.end(),[&](const SourcePictureEdgeCandidate& edge) {
                    return edge.minimum<=coordinate && edge.maximum>=coordinate && edge.support>0;
                }),L"Candidate generation must retain competing source boundaries, not stop after the first foreground departure on every ray.");
            // The test deliberately does not choose a canvas or force a
            // rectangle: reconciliation must decide what the detached graphic
            // means without having discarded the actual scene hypothesis.
        }
        TEST_METHOD(SourcePictureInvalidInputsCannotBecomeMeasuredBlackFrames)
        {
            SourcePictureFrame frame(960,540);frame.Fill(0,80,960,460,240);
            const auto valid=frame.Source();
            for(int fault=0;fault<5;++fault) {
                auto source=valid;
                if(fault==0)source.data=nullptr;
                if(fault==1)source.dataBytes=1;
                if(fault==2)source.width=0;
                if(fault==3)source.height=0;
                if(fault==4)source.rowBytes=1;
                const auto observed=MeasureSourcePicture(source);
                Assert::IsTrue(observed.outcome==SourcePictureOutcome::NotMeasured);
                Assert::IsFalse(observed.complete);Assert::IsFalse(observed.rectangleAvailable);
                Assert::IsFalse(observed.canvasSupported);
                Assert::AreEqual(uint64_t(7),observed.sourceGeneration);
            }
            const auto invalidAssessment=ReconcileSourcePictureCandidate({},Bounds(960,540,0,80,960,460));
            Assert::IsFalse(invalidAssessment.evaluated);
            Assert::IsFalse(invalidAssessment.contradicted);
        }
        TEST_METHOD(SourcePictureBudgetCancellationPreservesMissingWorkSemantics)
        {
            SourcePictureFrame frame(960,540);frame.Fill(0,80,960,460,240);
            struct BudgetState { int checks=0,allowed=0; };
            BudgetState counted;
            AnalysisEvaluationBudget countChecks([](void* context) {
                ++static_cast<BudgetState*>(context)->checks;return false;
            },&counted);
            Assert::IsTrue(MeasureSourcePicture(frame.Source(),&countChecks).complete);
            Assert::IsTrue(counted.checks>4);
            // Exercise both sampling and late topology/reconciliation checkpoints.
            for(int allowance:{0,3,counted.checks/2,counted.checks-1}) {
                BudgetState state;state.allowed=allowance;
                AnalysisEvaluationBudget budget([](void* context) {
                    auto& state=*static_cast<BudgetState*>(context);
                    return state.checks++>=state.allowed;
                },&state);
                const auto observed=MeasureSourcePicture(frame.Source(),&budget);
                Assert::IsTrue(budget.Cancelled());
                Assert::IsFalse(observed.complete);Assert::IsFalse(observed.rectangleAvailable);
                Assert::IsFalse(observed.canvasSupported);
                Assert::IsTrue(observed.outcome==SourcePictureOutcome::NotMeasured ||
                    observed.outcome==SourcePictureOutcome::Partial);
                Assert::AreEqual(uint64_t(7),observed.sourceGeneration);
                if(allowance==0) {
                    Assert::IsTrue(observed.outcome==SourcePictureOutcome::NotMeasured);
                    Assert::AreEqual(size_t(0),observed.samples);
                }
            }
            // Cancellation of one optional packet cannot poison a new full
            // measurement of the same immutable source.
            Exact(MeasureSourcePicture(frame.Source()),Bounds(960,540,0,80,960,460));
        }
        TEST_METHOD(SourcePictureOptionalNativeCaptureReplay)
        {
            const auto environment=[](const char* name) {
                char* value=nullptr;size_t size=0;
                Assert::AreEqual(0,_dupenv_s(&value,&size,name));
                std::unique_ptr<char,decltype(&std::free)> owner(value,&std::free);
                return owner?std::string(owner.get()):std::string();
            };
            auto directory=environment("VP_SOURCE_PICTURE_CAPTURE_DIRECTORY");
            if(directory.empty()) {
                Logger::WriteMessage("OPTIONAL_FIXTURE_NOT_EXERCISED: native source-picture capture directory absent; private frames exercised=0. This result is not capture coverage.");
                return;
            }
            const auto edges=environment("VP_SOURCE_PICTURE_CAPTURE_EXPECTED_EDGES");
            const auto outcome=environment("VP_SOURCE_PICTURE_CAPTURE_EXPECTED_OUTCOME");
            int left=0,top=0,right=0,bottom=0;
            Assert::AreEqual(4,sscanf_s(edges.c_str(),"%d,%d,%d,%d",&left,&top,&right,&bottom),
                L"A supplied private corpus requires independently verified explicit expected source coordinates.");
            Assert::IsTrue(outcome=="Measured" || outcome=="CanvasEstimate" || outcome=="Partial",
                L"Expected outcome must be Measured, CanvasEstimate or Partial; no inferred default.");
            if(directory.back()!='\\' && directory.back()!='/')directory+='\\';
            WIN32_FIND_DATAA entry{};
            struct Search {
                HANDLE value;
                ~Search(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}
            } search{FindFirstFileA((directory+"frame-*.bin").c_str(),&entry)};
            Assert::IsTrue(search.value!=INVALID_HANDLE_VALUE,L"Explicit native replay directory must contain readable frame-*.bin files.");
            size_t count=0,mismatches=0,samples=0;double totalMs=0,peakMs=0;
            do {
                if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
                const auto path=directory+entry.cFileName;
                std::ifstream input(path,std::ios::binary|std::ios::ate);
                Assert::IsTrue(input.good(),L"Supplied native pixel capture must be readable.");
                const auto end=input.tellg();Assert::IsTrue(end>0);
                std::vector<uint8_t> bytes(static_cast<size_t>(end));input.seekg(0);
                input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
                Assert::AreEqual(static_cast<std::streamsize>(bytes.size()),input.gcount(),L"Native frame must not be truncated.");
                std::ifstream sidecar(path.substr(0,path.size()-4)+".txt");
                int width=0,height=0,format=0,encoding=0,color=0,retainedTop=0,retainedBottom=0,script=0,range=0;
                size_t pitch=0,chromaPitch=0;
                Assert::IsTrue(bool(sidecar>>width>>height>>pitch>>chromaPitch>>format>>encoding>>color>>retainedTop>>retainedBottom>>script>>range),
                    L"Native replay requires the original interpretation sidecar.");
                Assert::IsTrue(format>=int(AnalysisLumaFormat::P010) && format<=int(AnalysisLumaFormat::SdrTunnelRgb));
                Assert::IsTrue(left>=0 && top>=0 && right>left && bottom>top && right<=width && bottom<=height);
                AnalysisLumaSource source{bytes.data(),bytes.size(),width,height,pitch,chromaPitch,
                    static_cast<AnalysisLumaFormat>(format),static_cast<VideoFrameEncoding>(encoding),static_cast<ColorSpace>(color),1};
                source.range=static_cast<AnalysisLumaRange>(range);
                Assert::IsTrue(source.IsValid(),L"Saved source must retain valid native strides/layout/precision.");
                // No sidecar retained crop is passed into the measurement.
                const auto started=std::chrono::steady_clock::now();
                const auto observed=MeasureSourcePicture(source);
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                ++count;samples+=observed.samples;totalMs+=elapsed;peakMs=std::max(peakMs,elapsed);
                bool matches=observed.complete && observed.sourceGeneration==1 && observed.width==width && observed.height==height;
                if(outcome=="Measured")matches=matches && observed.outcome==SourcePictureOutcome::Measured && observed.rectangleAvailable &&
                    observed.measuredBounds.left==left && observed.measuredBounds.top==top &&
                    observed.measuredBounds.right==right && observed.measuredBounds.bottom==bottom;
                if(outcome=="CanvasEstimate") {
                    Assert::IsTrue(left==0 && top==0 && right==width && bottom==height,
                        L"Canvas expectations must explicitly name the whole source; blank margins remain an inference.");
                    matches=matches && observed.outcome==SourcePictureOutcome::CanvasEstimate && observed.canvasSupported && observed.blankMarginsUnresolved;
                }
                if(outcome=="Partial") {
                    matches=matches && observed.outcome==SourcePictureOutcome::Partial && !observed.rectangleAvailable;
                    const int coordinates[4]={left,top,right,bottom};
                    for(unsigned side=0;side<4;++side)matches=matches &&
                        std::any_of(observed.edges[side].begin(),observed.edges[side].end(),[&](const SourcePictureEdgeCandidate& edge) {
                            return edge.coverage>0 && edge.minimum<=coordinates[side] && edge.maximum>=coordinates[side];
                        });
                }
                if(!matches) {
                    ++mismatches;
                    if(mismatches==1)for(unsigned side=0;side<4;++side) {
                        std::ostringstream detail;
                        detail<<"First native mismatch edge candidates: side="<<side<<" count="<<observed.edges[side].size();
                        const size_t limit=std::min(size_t(128),observed.edges[side].size());
                        for(size_t index=0;index<limit;++index) {
                            const auto& edge=observed.edges[side][index];
                            detail<<" [coordinate="<<edge.coordinate<<" interval="<<edge.minimum<<","<<edge.maximum
                                <<" support="<<edge.support<<" coverage="<<edge.coverage<<" regions="
                                <<edge.regionSupport[0]<<","<<edge.regionSupport[1]<<","<<edge.regionSupport[2]<<","<<edge.regionSupport[3]
                                <<" strong="<<edge.strong<<" contradicted="<<edge.contradicted<<"]";
                        }
                        if(limit<observed.edges[side].size())detail<<" (remaining candidates omitted after128)";
                        Logger::WriteMessage(detail.str().c_str());
                    }
                    Logger::WriteMessage((std::string("Native source-picture MISMATCH: ")+path+" reason="+observed.reason+
                        " expected_outcome="+outcome+" actual_outcome="+std::to_string(int(observed.outcome))+
                        " expected="+edges+" available="+std::to_string(observed.rectangleAvailable)+
                        " actual="+std::to_string(observed.measuredBounds.left)+","+std::to_string(observed.measuredBounds.top)+"-"+
                        std::to_string(observed.measuredBounds.right)+","+std::to_string(observed.measuredBounds.bottom)).c_str());
                }
            }while(FindNextFileA(search.value,&entry));
            Assert::AreEqual(DWORD(ERROR_NO_MORE_FILES),GetLastError(),L"Directory enumeration must finish without an IO error.");
            Logger::WriteMessage((std::string("Native source-picture corpus exercised: directory=")+directory+
                " frames="+std::to_string(count)+" mismatches="+std::to_string(mismatches)+" samples="+std::to_string(samples)+
                " total_ms="+std::to_string(totalMs)+" peak_ms="+std::to_string(peakMs)).c_str());
            Assert::IsTrue(count>0);Assert::AreEqual(size_t(0),mismatches);
        }
        TEST_METHOD(SourcePictureMeasuresUnevenlyLitScopeFromDistributedNativeEdges)
        {
            for(int scale:{1,4}) {
                const int width=960*scale,height=540*scale;
                const int top=scale==4?263:66,bottom=scale==4?1897:474;
                SourcePictureFrame frame(width,height);
                frame.Fill(0,top,width,bottom,200,530,490);
                const int dimTop=bottom-24*scale;
                frame.Fill(0,dimTop,width,bottom,68);
                const int starts[4]={0,1000,2100,3000},lengths[4]={960,252,143,196};
                for(int region=0;region<4;++region) {
                    const int left=starts[region]*scale/4;
                    const int right=(starts[region]+lengths[region])*scale/4;
                    frame.Fill(left,dimTop,right,bottom,240,530,490);
                }
                const auto measured=MeasureSourcePicture(frame.Source());
                Exact(measured,Bounds(width,height,0,top,width,bottom));
                const auto& candidates=measured.edges[unsigned(SourcePictureEdge::Bottom)];
                Assert::IsTrue(std::any_of(candidates.begin(),candidates.end(),[&](const SourcePictureEdgeCandidate& edge) {
                    return edge.coordinate==bottom && edge.support>0 && edge.coverage>=edge.support;
                }),L"The true unevenly lit bottom must remain a candidate even when a broader interior edge is stronger.");
                Assert::AreEqual(uint64_t(7),measured.sourceGeneration);
            }
        }
        TEST_METHOD(SourcePictureKeepsSparseHeaderOutsideItsDominantChildRectangle)
        {
            SourcePictureFrame frame(3840,2160);
            // Body geometry intentionally matches the reported false child.
            frame.Fill(136,332,3840,1300,180,530,490);
            frame.Fill(136,1540,3840,2160,140,490,540);
            frame.Fill(136,59,234,165,180,560,480); // Avatar, missed by legacy32 probes.
            frame.Fill(1430,80,1677,150,150);       // Filled navigation capsule.
            frame.Fill(1800,89,1920,119,750);
            frame.Fill(2110,89,2250,119,750);
            frame.Fill(3644,59,3706,165,190,450,650); // Far-right logo.
            frame.Fill(138,239,666,298,200);       // Separate title band.
            const auto measured=MeasureSourcePicture(frame.Source());
            Logger::WriteMessage(measured.reason);
            Assert::IsTrue(measured.complete);
            Assert::IsTrue(measured.canvasSupported);
            Assert::IsTrue(measured.blankMarginsUnresolved,
                L"Blank outer margins do not become exact measured edges merely because the child rectangle is contradicted.");
            Assert::IsTrue(measured.outcome==SourcePictureOutcome::CanvasEstimate,
                L"The positive composite canvas needs an explicit supported inference, not an indefinitely Unknown result.");
            const auto touches=[&](int x,int y) {
                return std::any_of(measured.components.begin(),measured.components.end(),[&](const SourcePictureComponent& component) {
                    return component.samples>0 && component.left<=x && component.right>x && component.top<=y && component.bottom>y;
                });
            };
            Assert::IsTrue(touches(180,100),L"Sparse avatar evidence must survive candidate generation.");
            Assert::IsTrue(touches(3670,100),L"Sparse far-right evidence must survive candidate generation.");
            Assert::IsTrue(touches(300,265),L"The separate title region is current source picture evidence.");
            const auto child=Bounds(3840,2160,136,332,3840,2160);
            const auto assessment=ReconcileSourcePictureCandidate(measured,child);
            Assert::IsTrue(assessment.evaluated);Assert::IsTrue(assessment.contradicted);
            Assert::IsTrue(assessment.exteriorComponents>=3 && assessment.exteriorRegions>=3,
                L"Detached distributed header components must invalidate the child rectangle without a service-specific menu rule.");
        }
        TEST_METHOD(SourcePictureIgnoresLocalCaptionsLogoAndProgressWhenMeasuringRealBars)
        {
            for(int lines=1;lines<=3;++lines)for(bool extraOverlay:{false,true}) {
                SourcePictureFrame frame(960,540);
                frame.Fill(0,80,960,460,240,530,490);
                frame.Caption(lines,470);
                if(extraOverlay) {
                    frame.Fill(22,18,50,40,240,560,470);
                    frame.Fill(0,536,960,539,300);
                }
                const auto measured=MeasureSourcePicture(frame.Source());
                Exact(measured,Bounds(960,540,0,80,960,460));
            }
        }
        TEST_METHOD(SourcePicturePreservesAsymmetricOddNonstandardCoordinates)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(17,63,939,487,240,530,490);
            const auto measured=MeasureSourcePicture(frame.Source());
            Exact(measured,Bounds(960,540,17,63,939,487));
            Assert::IsTrue(std::abs(measured.measuredBounds.aspectRatio-2.35)>0.1,
                L"Source aspect is derived from exact odd coordinates, never aligned or snapped to the screen or a standard.");
        }
        TEST_METHOD(SourcePictureRetainsFaintCollarUncertaintyWithPatternedMatte)
        {
            for(bool limited:{false,true})for(bool patterned:{false,true})for(bool sharp:{false,true}) {
                const int matte=limited?80:64;
                SourcePictureFrame frame(960,540,matte);
                frame.Fill(0,80,960,460,matte+8);
                if(sharp)frame.Fill(0,84,960,456,240,530,490);
                else for(int y=84;y<456;++y) {
                    const int distance=std::min(y-80,459-y);
                    frame.Fill(0,y,960,y+1,std::min(240,matte+8+distance/2));
                }
                if(patterned)for(int index:{1,10,18,26}) {
                    const int x=index*959/31;
                    frame.Fill(x-3,0,x+4,80,matte-32);
                    frame.Fill(x-3,460,x+4,540,matte-32);
                }
                const auto measured=MeasureSourcePicture(frame.Source(limited?AnalysisLumaRange::Limited:AnalysisLumaRange::Full));
                Logger::WriteMessage(measured.reason);
                if(measured.rectangleAvailable) {
                    Assert::AreEqual(80,measured.measuredBounds.top);
                    Assert::AreEqual(460,measured.measuredBounds.bottom);
                }
                // Either an exact edge or an interval must preserve the faint
                // current picture; a stronger inner edge is not its boundary.
                for(auto side:{SourcePictureEdge::Top,SourcePictureEdge::Bottom}) {
                    const int actual=side==SourcePictureEdge::Top?80:460;
                    const auto& candidates=measured.edges[unsigned(side)];
                    Assert::IsTrue(std::any_of(candidates.begin(),candidates.end(),[&](const SourcePictureEdgeCandidate& edge) {
                        return edge.minimum<=actual && edge.maximum>=actual && edge.coverage>0;
                    }),L"Faint first-picture uncertainty must remain explicit rather than vanish or move to a stronger interior edge.");
                }
            }
        }
    };

}

