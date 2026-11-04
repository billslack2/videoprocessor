#include "pch.h"
#include <SourcePictureMeasurement.h>
#include <SourcePictureDetector.h>
#include <OwnedSourcePictureObservationCache.h>
#include <SubtitleMeasurementWorker.h>
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
        static SourcePictureFrame SparseInsetFrame(unsigned side,int phase,ActivePictureBounds& bounds,int fault=-1)
        {
            const bool horizontal=side==1 || side==3;const int along=horizontal?960:540;
            bounds=horizontal?Bounds(960,540,0,67+phase,960,463+2*phase):Bounds(960,540,67+phase,0,887+2*phase,540);
            SourcePictureFrame frame(960,540);
            const int edge=side==0?bounds.left:side==1?bounds.top:side==2?bounds.right:bounds.bottom;
            for(int y=bounds.top;y<bounds.bottom;++y)for(int x=bounds.left;x<bounds.right;++x) {
                const int a=horizontal?x:y;
                const int inward=side==0?x-edge:side==1?y-edge:side==2?edge-1-x:edge-1-y;
                const bool first=a<7*along/128,middle=fault!=2 && a>=40*along/128 && a<47*along/128;
                const bool last=fault==1?(a>=120*along/128 && a<123*along/128):a>=(fault==0?123:125)*along/128;
                const int value=first || middle || last?300:64+std::min(236,std::max(0,inward-16)*2);
                frame.Fill(x,y,x+1,y+1,value);
            }
            if(fault==0) {frame.Fill(0,edge,8,edge+30,300);frame.Fill(952,edge,960,edge+30,300);}
            return frame;
        }
    public:
        TEST_METHOD(SourcePictureOverlappingQuantizedRayIntervalsKeepCurrentEdgeAcrossPhases)
        {
            constexpr int width=960,height=540,coreTop=96,bottom=480;
            const int profiles[4][3]={{68,68,72},{68,68,76},{68,72,72},{68,72,72}};
            for(int phase=0;phase<4;++phase) {
                SourcePictureFrame frame(width,height);frame.Fill(0,coreTop,width,bottom,300);
                for(int x=0;x<width;++x) {
                    const int ray=x*128/width,ordinal=ray/2;
                    if((ray&1)||ordinal>=62)continue;
                    const int start=ordinal<16?67:ordinal<47?68:69;
                    frame.Fill(x,start+3,x+1,coreTop,(phase&1)?76:72);
                    for(int row=0;row<3;++row)frame.Fill(x,start+row,x+1,start+row+1,profiles[phase][row]);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                std::ostringstream diagnostic;diagnostic<<"phase="<<phase<<" reason="<<observed.reason;
                for(const auto& edge:observed.edges[unsigned(SourcePictureEdge::Top)])
                    diagnostic<<" edge="<<edge.coordinate<<"["<<edge.minimum<<","<<edge.maximum<<"]:"<<edge.support;
                Logger::WriteMessage(diagnostic.str().c_str());
                Assert::IsTrue(observed.complete&&observed.rectangleAvailable,
                    L"Current distributed ray uncertainty must not become UNKNOWN merely because exact starting bins each miss32.");
                Assert::AreEqual(68,observed.measuredBounds.top,
                    L"Row68 is the outermost qualified coordinate:47 unique source-ray intervals support it; later picture at96 is not the departure.");
                Assert::AreEqual(bottom,observed.measuredBounds.bottom);
                Assert::AreEqual(double(width)/(bottom-68),observed.measuredBounds.aspectRatio,1e-12);
                const auto& edges=observed.edges[unsigned(SourcePictureEdge::Top)];
                Assert::IsTrue(std::any_of(edges.begin(),edges.end(),[](const SourcePictureEdgeCandidate& edge) {
                    unsigned unique=0;for(auto word:edge.supportingRays)while(word){word&=word-1;++unique;}
                    return edge.coordinate==68&&edge.minimum<=68&&edge.maximum>=68&&edge.support==47&&unique==47;
                }),L"Support must be47 distinct current source rays at the selected coordinate, not a union of nominal candidate counts.");
            }
        }

        TEST_METHOD(SourcePictureRayIntervalConsensusRejectsSeparatedChainsAndDuplicateRays)
        {
            constexpr int width=960,height=540,coreTop=112,bottom=480;
            for(int fault=0;fault<3;++fault) {
                SourcePictureFrame frame(width,height);frame.Fill(0,coreTop,width,bottom,300);
                std::array<std::vector<std::pair<int,int>>,128> intervals;
                for(int ray=0;ray<128;++ray) {
                    const int ordinal=ray/2;
                    if(fault==2) {
                        if(ray%8==0)intervals[ray]={{67,69},{81,83}};
                    } else if(!(ray&1)&&ordinal<62) {
                        const int group=ordinal<16?0:ordinal<47?1:2;
                        const int start=67+group*(fault==0?3:2);
                        intervals[ray]={{start,start+2}};
                    }
                }
                for(int x=0;x<width;++x)for(const auto& interval:intervals[x*128/width]) {
                    const int start=interval.first;
                    frame.Fill(x,start,x+1,start+2,68);
                    frame.Fill(x,start+2,x+1,fault==2?start+9:coreTop,72);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                for(const auto& edge:observed.edges[unsigned(SourcePictureEdge::Top)])if(edge.coordinate<coreTop) {
                    unsigned compatible=0,unique=0;
                    for(const auto& ray:intervals)if(std::any_of(ray.begin(),ray.end(),[&](const std::pair<int,int>& interval) {
                        return interval.first<=edge.coordinate&&edge.coordinate<=interval.second;
                    }))++compatible;
                    for(auto word:edge.supportingRays)while(word){word&=word-1;++unique;}
                    Assert::IsTrue(edge.support<=compatible,
                        L"A candidate cannot borrow nonintersecting intervals through a transitive chain.");
                    Assert::AreEqual(edge.support,unique,L"Repeated departures on one ray count only once.");
                    if(fault==2)Assert::IsTrue(edge.support<=16);
                }
                if(fault!=1)Assert::IsFalse(observed.rectangleAvailable&&observed.measuredBounds.top<coreTop,
                    L"Separated under-supported edges and repeated same-ray departures cannot reach the unchanged support threshold.");
            }
        }

        TEST_METHOD(SourcePictureQuantizedFourPhaseEdgeKeepsBoundedNativeGeometry)
        {
            constexpr int width=960,height=540,top=17,bottom=523;
            // The same paused source can alternate four legal 8-bit HDMI
            // quantization phases: differences are only 0 or +/-4 in 10-bit
            // code space. The picture-to-matte departure occupies <=3 rows.
            const int profiles[4][3]={{64,72,76},{64,72,76},{68,68,72},{68,68,76}};
            for(int phase=0;phase<4;++phase) {
                SourcePictureFrame frame(width,height);
                frame.Fill(0,top,width,bottom-3,300);
                frame.Fill(0,bottom-3,width,bottom-2,80);
                for(int offset=0;offset<3;++offset)
                    frame.Fill(0,bottom-offset,width,bottom-offset+1,profiles[phase][offset]);
                const auto observed=MeasureSourcePicture(frame.Source());
                Logger::WriteMessage((std::string("Quantized native phase=")+std::to_string(phase)+" reason="+observed.reason).c_str());
                Assert::IsTrue(observed.complete && observed.rectangleAvailable && observed.outcome==SourcePictureOutcome::Measured,
                    L"A bounded 4+4-code native edge cannot disappear on one quantization phase while an equivalent 8-code step passes.");
                Assert::AreEqual(0,observed.measuredBounds.left);Assert::AreEqual(width,observed.measuredBounds.right);
                Assert::AreEqual(top,observed.measuredBounds.top);
                Assert::IsTrue(std::abs(observed.measuredBounds.bottom-bottom)<=1);
                Assert::AreEqual(double(width)/(observed.measuredBounds.bottom-top),observed.measuredBounds.aspectRatio,1e-12);
                const auto& edges=observed.edges[unsigned(SourcePictureEdge::Bottom)];
                Assert::IsTrue(std::any_of(edges.begin(),edges.end(),[&](const SourcePictureEdgeCandidate& edge) {
                    return edge.coordinate==observed.measuredBounds.bottom && edge.minimum<=bottom && edge.maximum>=bottom &&
                        edge.minimum>=bottom-1 && edge.maximum<=bottom+1 && edge.support<=edge.coverage;
                }),L"Quantized edge uncertainty must remain bounded in native rows rather than inventing a phase-invariant exact row.");
                const auto verified=DetectSourcePictureForWorker(frame.Source());
                Assert::IsTrue(verified.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"The independently measured rectangle must still pass the real excluded-band verifier.");
            }
        }
        TEST_METHOD(SourcePictureQuantizedContinuingRampCannotInventAnInsetBoundary)
        {
            SourcePictureFrame frame(960,540);
            for(int y=0;y<540;++y) {
                const int distance=std::min(y,539-y);
                frame.Fill(0,y,960,y+1,std::min(300,64+4*(distance/2)));
            }
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsTrue(observed.complete);
            Assert::IsFalse(observed.rectangleAvailable &&
                (observed.measuredBounds.top>0 || observed.measuredBounds.bottom<540),
                L"A continuing quantized 4,0,4 ramp has no localized matte-to-picture edge and must not be cropped.");
        }
        TEST_METHOD(SourcePictureNearbyCoherentEdgeVotesRetainTheirNativeInterval)
        {
            constexpr int width=960,height=540,top=17,bottom=523;
            SourcePictureFrame frame(width,height);
            // Each fixed ray sees exactly one current edge. Adjacent source
            // rows are one coherent boundary, not three competing pictures.
            for(int x=0;x<width;++x) {
                const int ray=std::min(127,x*128/width);
                frame.Fill(x,top,x+1,bottom+(ray%3)-1,300);
            }
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsTrue(observed.complete && observed.rectangleAvailable);
            const auto& edges=observed.edges[unsigned(SourcePictureEdge::Bottom)];
            for(int row=bottom-1;row<=bottom+1;++row) {
                const auto found=std::find_if(edges.begin(),edges.end(),[&](const SourcePictureEdgeCandidate& edge) {
                    return edge.coordinate==row;
                });
                Assert::IsTrue(found!=edges.end());
                Assert::AreEqual(row,found->minimum);Assert::AreEqual(row,found->maximum);
                unsigned expected=0;
                for(unsigned ray=0;ray<128;++ray) {
                    const bool supports=bottom+int(ray%3)-1==row;
                    const bool recorded=(found->supportingRays[ray/64]&(uint64_t(1)<<(ray%64)))!=0;
                    Assert::AreEqual(supports,recorded,L"An exact-row edge cannot borrow another source ray's different exact boundary.");
                    expected+=supports;
                }
                Assert::AreEqual(expected,found->support);Assert::AreEqual(128u,found->coverage);
                Assert::IsTrue(expected>=42&&expected<=43);
            }
            Assert::AreEqual(bottom+1,observed.measuredBounds.bottom,
                L"The existing outward-first selection retains the full coherent source edge span without invented same-coordinate votes.");
        }
        TEST_METHOD(SourcePictureDarkStructuredTerminalDoesNotEraseFullRasterPicture)
        {
            for(bool horizontal:{false,true})for(bool leading:{false,true}) {
                SourcePictureFrame frame(960,540,104);
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int depth=horizontal?960:540;
                    const int across=horizontal?x:y,along=horizontal?y:x;
                    const int distance=leading?across:depth-1-across;
                    // One connected picture fades continuously toward a dark
                    // textured terminal. There is no flat encoded outer matte
                    // or inset abrupt boundary. Its own dark pixels must not
                    // become an assumed black floor that erases that edge.
                    const int value=104+std::min(196,distance/2)+4*((along/8)%3-1);
                    frame.Fill(x,y,x+1,y+1,value);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Logger::WriteMessage(observed.reason);
                Exact(observed,Bounds(960,540,0,0,960,540));
            }
        }
        TEST_METHOD(SourcePictureWeakTerminalSurvivesEncodingPhasesAndEquivalentYuvFormats)
        {
            for(bool horizontal:{false,true})for(bool leading:{false,true})for(int phase=0;phase<4;++phase) {
                SourcePictureFrame frame(960,540,104);
                const int depth=horizontal?960:540;
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int across=horizontal?x:y,along=horizontal?y:x;
                    const int d=leading?across:depth-1-across;
                    // Quantize a genuinely weak continuous fade through four
                    // ordered rounding phases, rather than insert a flat prefix.
                    const int64_t level=104*256+int64_t(d)*256/80+int64_t(d)*d*196*256/(int64_t(depth)*depth);
                    const int value=int(4*((level+(2*((across+along+phase)%4)+1)*128)/1024));
                    frame.Fill(x,y,x+1,y+1,value);
                }
                const auto planar=frame.Source();
                const auto measured=MeasureSourcePicture(planar);
                if(!measured.rectangleAvailable)Logger::WriteMessage((std::string("Weak terminal: horizontal=")+std::to_string(horizontal)+
                    " leading="+std::to_string(leading)+" phase="+std::to_string(phase)+" reason="+measured.reason).c_str());
                Exact(measured,Bounds(960,540,0,0,960,540));
                auto p010=planar;p010.format=AnalysisLumaFormat::P010;
                Exact(MeasureSourcePicture(p010),Bounds(960,540,0,0,960,540));
                std::vector<uint32_t> v210(size_t(540)*640);
                for(int y=0;y<540;++y)for(int x=0;x<960;x+=6) {
                    unsigned codes[6];for(int k=0;k<6;++k){AnalysisLumaSample q;Assert::IsTrue(planar.Sample(x+k,y,q));codes[k]=q.luma;}
                    auto* out=v210.data()+size_t(y)*640+size_t(x/6)*4;
                    out[0]=512|(codes[0]<<10)|(512<<20);out[1]=codes[1]|(512<<10)|(codes[2]<<20);
                    out[2]=512|(codes[3]<<10)|(512<<20);out[3]=codes[4]|(512<<10)|(codes[5]<<20);
                }
                AnalysisLumaSource packed10{reinterpret_cast<const uint8_t*>(v210.data()),v210.size()*4,960,540,2560,0,
                    AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::V210,ColorSpace::REC_709,7};
                packed10.range=AnalysisLumaRange::Limited;
                Exact(MeasureSourcePicture(packed10),Bounds(960,540,0,0,960,540));
                // Same source codes packed as 8-bit 4:2:2 must not change AR.
                std::vector<uint8_t> packed(size_t(960)*540*2);
                for(int y=0;y<540;++y)for(int x=0;x<960;x+=2) {
                    const auto offset=size_t(y)*1920+size_t(x)*2;
                    AnalysisLumaSample a,b;Assert::IsTrue(planar.Sample(x,y,a)&&planar.Sample(x+1,y,b));
                    packed[offset]=128;packed[offset+1]=uint8_t(a.luma/4);
                    packed[offset+2]=128;packed[offset+3]=uint8_t(b.luma/4);
                }
                AnalysisLumaSource native{packed.data(),packed.size(),960,540,1920,0,
                    AnalysisLumaFormat::NativeYuv422,VideoFrameEncoding::HDYC,ColorSpace::REC_709,7};
                native.range=AnalysisLumaRange::Limited;
                Exact(MeasureSourcePicture(native),Bounds(960,540,0,0,960,540));
            }
        }
        TEST_METHOD(SourcePictureQuantizedRaisedMattePrefixCannotBecomeTerminalPicture)
        {
            for(bool horizontal:{false,true})for(bool leading:{false,true})for(int phase=0;phase<4;++phase)
            for(int divisor:{24,8}) {
                SourcePictureFrame frame(960,540,104);const int depth=horizontal?960:540;
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int across=horizontal?x:y,d=leading?across:depth-1-across;
                    const int prefix=depth/divisor+16;
                    const int level=d<prefix?104:std::min(300,104+(d-prefix)/2);
                    const int value=4*((level+(x+y+phase)%4)/4);
                    frame.Fill(x,y,x+1,y+1,value);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                Assert::IsFalse(observed.rectangleAvailable && observed.measuredBounds.left==0 && observed.measuredBounds.top==0 &&
                    observed.measuredBounds.right==960 && observed.measuredBounds.bottom==540,
                    L"Encoding phase and later body gradients cannot convert a genuine flat raised matte into raster contact.");
            }
        }
        TEST_METHOD(SourcePictureNoisyRaisedMattePrefixCannotBorrowLaterPictureTrend)
        {
            for(bool horizontal:{false,true})for(bool leading:{false,true})for(int phase=0;phase<4;++phase)
            for(bool blocks:{false,true}) {
                SourcePictureFrame frame(960,540,104);const int depth=horizontal?960:540;
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int across=horizontal?x:y,d=leading?across:depth-1-across;
                    const int prefix=depth/24+16;
                    const int level=d<prefix?104:std::min(300,104+(d-prefix)/2);
                    const int noise=4*((blocks?(x/8+3*(y/7)+phase):(x+3*y+phase))%3-1);
                    frame.Fill(x,y,x+1,y+1,level+noise);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                if(observed.rectangleAvailable && observed.measuredBounds.left==0 && observed.measuredBounds.top==0 &&
                    observed.measuredBounds.right==960 && observed.measuredBounds.bottom==540) {
                    Logger::WriteMessage((std::string("Noisy matte false raster: horizontal=")+std::to_string(horizontal)+
                        " leading="+std::to_string(leading)+" phase="+std::to_string(phase)+" blocks="+std::to_string(blocks)+
                        " floor="+std::to_string(observed.blackReference.floor)+" proof="+std::to_string(observed.darkTerminal.accepted)).c_str());
                    Assert::Fail(L"Quantization/noise in a flat matte cannot borrow later body contrast to claim a raster contact.");
                }
            }
        }
        TEST_METHOD(SourcePictureGrayTerminalAgreesAcrossRgbYuvRangeConversions)
        {
            for(bool matte:{false,true})for(int phase=0;phase<4;++phase)
            for(auto color:{ColorSpace::REC_709,ColorSpace::BT_2020}) {
                SourcePictureFrame frame(960,540,104);
                std::vector<uint8_t> r210(size_t(960)*540*4),bgra(r210.size());
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int d=959-x;int value=0;
                    if(matte) {
                        const int prefix=960/24+16;
                        value=(d<prefix?104:std::min(300,104+(d-prefix)/2))+4*((x/8+3*(y/7)+phase)%3-1);
                    } else {
                        const int64_t level=104*256+int64_t(d)*256/80+int64_t(d)*d*196*256/(int64_t(960)*960);
                        value=int(4*((level+(2*((x+y+phase)%4)+1)*128)/1024));
                    }
                    frame.Fill(x,y,x+1,y+1,value);
                    // Native r210 uses 64..960 RGB; limited YUV uses 64..940.
                    const unsigned rgb=unsigned(64+((value-64)*896+438)/876);
                    const unsigned word=(rgb<<20)|(rgb<<10)|rgb;const size_t at=(size_t(y)*960+x)*4;
                    for(unsigned k=0;k<4;++k)r210[at+k]=uint8_t(word>>(24-8*k));
                    const uint8_t rgb8=uint8_t(((value-64)*255+438)/876);
                    bgra[at]=bgra[at+1]=bgra[at+2]=rgb8;bgra[at+3]=255;
                }
                AnalysisLumaSource a{r210.data(),r210.size(),960,540,3840,0,
                    AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::R210,color,7};a.range=AnalysisLumaRange::Limited;
                AnalysisLumaSource b{bgra.data(),bgra.size(),960,540,3840,0,
                    AnalysisLumaFormat::NativeRgb,VideoFrameEncoding::BGRA_8BIT,color,7};b.range=AnalysisLumaRange::Full;
                for(const auto& source:{frame.Source(),a,b}) {
                    const auto observed=MeasureSourcePicture(source);
                    const bool full=observed.rectangleAvailable && observed.measuredBounds.left==0 && observed.measuredBounds.top==0 &&
                        observed.measuredBounds.right==960 && observed.measuredBounds.bottom==540;
                    if(full==matte)Logger::WriteMessage((std::string("RGB/YUV geometry mismatch: matte=")+std::to_string(matte)+
                        " phase="+std::to_string(phase)+" format="+std::to_string(int(source.format))+" encoding="+std::to_string(int(source.encoding))+
                        " color="+std::to_string(int(color))+" reason="+observed.reason).c_str());
                    Assert::IsTrue(observed.complete);Assert::AreEqual(!matte,full,
                        L"RGB matrix/range rounding cannot change a full picture into a bar or a raised matte into full-picture contact.");
                }
            }
        }
        TEST_METHOD(SourcePictureFlatRaisedPrefixBeforeFaintRampCannotProveFullRaster)
        {
            for(bool horizontal:{false,true})for(bool leading:{false,true}) {
                SourcePictureFrame frame(960,540,104);
                for(int y=0;y<540;++y)for(int x=0;x<960;++x) {
                    const int depth=horizontal?960:540,across=horizontal?x:y;
                    const int distance=leading?across:depth-1-across;
                    const int prefix=depth/8+16;
                    const int value=distance<prefix?104:std::min(300,104+(distance-prefix)/2);
                    frame.Fill(x,y,x+1,y+1,value);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                Assert::IsFalse(observed.rectangleAvailable && observed.measuredBounds.left==0 && observed.measuredBounds.top==0 &&
                    observed.measuredBounds.right==960 && observed.measuredBounds.bottom==540,
                    L"Later body gradients cannot manufacture picture contact through a genuine flat raised outer matte.");
            }
        }
        TEST_METHOD(SourcePictureRaisedTexturedMatteAndSparseWidgetsDoNotProveTerminalPicture)
        {
            for(bool widgets:{false,true}) {
                SourcePictureFrame frame(960,540,104);
                frame.Fill(128,0,960,540,300,530,490);
                for(int y=0;y<540;++y)
                    frame.Fill(0,y,128,y+1,104+4*((y/8)%3-1));
                if(widgets) {
                    frame.Fill(0,90,16,99,240);frame.Fill(0,420,16,429,240);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                Assert::IsFalse(observed.rectangleAvailable && observed.measuredBounds.left==0,
                    L"A genuinely raised textured sidebar cannot become picture merely because it exceeds nominal black or contains sparse widgets.");
                if(observed.rectangleAvailable)Assert::AreEqual(128,observed.measuredBounds.left);
            }
        }
        TEST_METHOD(SourcePictureQualifiedRasterContactsKeepMeasurementWorkBounded)
        {
            SourcePictureFrame frame(3840,2160,300);
            const auto observed=MeasureSourcePicture(frame.Source());
            Exact(observed,Bounds(3840,2160,0,0,3840,2160));
            Assert::IsTrue(observed.topologyComplete);
            // The complete fixed overview remains mandatory. Strong qualified
            // source contacts must not trigger four redundant half-raster scans.
            Assert::IsTrue(observed.samples<=size_t(480)*270+2048,
                L"Full-source contact acquisition must retain bounded work without discarding its topology or changing geometry.");
        }
        TEST_METHOD(SourcePictureMissingGeometryPreservesBlackReferenceWithoutNativeVotes)
        {
            const auto scope=Bounds(960,540,0,66,960,474);
            for(const auto range:{AnalysisLumaRange::Limited,AnalysisLumaRange::Full}) {
                const int floor=range==AnalysisLumaRange::Limited?64:0;
                SourcePictureFrame frame(960,540,floor);
                const auto source=frame.Source(range);
                auto missing=MissingSourcePictureEvidence(source);
                Assert::AreEqual(floor,missing.blackReference.floor);
                Assert::AreEqual(floor+24,missing.blackReference.threshold);
                Assert::IsFalse(missing.available);Assert::IsFalse(missing.physicalEdges.estimateAvailable);
                const auto retained=EvaluateActivePicturePresentationRetention(source,scope,missing,
                    EvaluateActivePictureGlobalNearBlack(source),EvaluateActivePictureArDarkness(source,missing));
                Assert::IsTrue(retained.excludedBandsPixelSafe);
                Assert::IsFalse(MakeActivePictureObservation(missing,1,1).available);
                AnalysisEvaluationBudget expired([](void*){return true;},nullptr);
                const auto cancelled=DetectSourcePictureForWorker(source,&expired);
                Assert::AreEqual(floor,cancelled.evidence.blackReference.floor);
                Assert::IsFalse(cancelled.evidence.available);
            }
            SourcePictureFrame raised(960,540,64);
            const auto source=raised.Source(AnalysisLumaRange::Full);
            const auto detected=DetectSourcePictureForWorker(source);
            Assert::IsTrue(detected.observation->blackReference.evaluated);
            Assert::AreEqual(64,detected.evidence.blackReference.floor);
            Assert::AreEqual(88,detected.evidence.blackReference.threshold);
            Assert::IsFalse(detected.evidence.available);
            Assert::IsFalse(detected.evidence.physicalEdges.estimateAvailable);
            Assert::IsTrue(detected.evidence.blackReference.mode==ActivePictureBlackReferenceMode::FULL_UNRESOLVED_RAISED);
            const auto retained=EvaluateActivePicturePresentationRetention(source,scope,detected.evidence,
                EvaluateActivePictureGlobalNearBlack(source),EvaluateActivePictureArDarkness(source,detected.evidence));
            Assert::IsTrue(retained.excludedBandsPixelSafe);
            Assert::IsFalse(MakeActivePictureObservation(detected.evidence,1,1).available);
        }
        TEST_METHOD(SourcePictureOppositeOuterEdgesSurviveAnOpaqueCentralCaptionPanel)
        {
            for(bool uhd:{false,true}) {
                const int width=uhd?3840:960,height=uhd?2160:540;
                const int top=uhd?263:63,bottom=uhd?1897:477,panelTop=uhd?1829:453;
                SourcePictureFrame frame(width,height);
                frame.Fill(0,top,width,bottom,240,530,490);
                frame.Fill(width/4,panelTop,3*width/4,std::min(height,bottom+80),64);
                for(int glyph=0;glyph<18;++glyph) {
                    const int x=width/2-126+14*glyph;
                    frame.Fill(x,bottom-12,x+7,bottom+1,850);
                    frame.Fill(x,bottom+8,x+7,bottom+21,850);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Exact(observed,Bounds(width,height,0,top,width,bottom));
                Assert::IsTrue(observed.measuredBounds.bottom!=panelTop,
                    L"The interior top of an opaque caption panel must not replace a same-row border seen broadly at both outer quarters.");
            }
        }
        TEST_METHOD(SourcePicturePairedNarrowWidgetsCannotImpersonateBroadOuterPictureSupport)
        {
            SourcePictureFrame frame(3840,2160);
            frame.Fill(0,263,250,1897,240,530,490);
            frame.Fill(3590,263,3840,1897,240,530,490);
            const auto widgets=MeasureSourcePicture(frame.Source());
            Assert::IsFalse(widgets.rectangleAvailable,
                L"Two narrow edge widgets supply too little opposite-quarter support to establish a picture rectangle.");
            frame.Fill(0,0,3840,2160,64);
            frame.Fill(960,1829,2880,1990,240,530,490);
            const auto center=MeasureSourcePicture(frame.Source());
            Assert::IsFalse(center.rectangleAvailable,
                L"A centered caption-sized panel cannot establish the full-raster picture's borders.");
        }
        TEST_METHOD(SourcePictureActiveAdapterSeparatesOddMeasurementFromOutwardGuard)
        {
            SourcePictureFrame frame(960,540);frame.Fill(17,63,939,487,240,530,490);
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(bool(detected.observation));
            Exact(*detected.observation,Bounds(960,540,17,63,939,487));
            const auto& evidence=detected.evidence;
            Assert::IsTrue(evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured);
            Assert::IsTrue(evidence.available && evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::IsTrue(evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(17,evidence.physicalEdges.estimate.left);Assert::AreEqual(63,evidence.physicalEdges.estimate.top);
            Assert::AreEqual(939,evidence.physicalEdges.estimate.right);Assert::AreEqual(487,evidence.physicalEdges.estimate.bottom);
            Assert::IsTrue(evidence.trustedBounds.left<=17 && evidence.trustedBounds.top<=63 &&
                evidence.trustedBounds.right>=939 && evidence.trustedBounds.bottom>=487);
            Assert::IsTrue(evidence.trustedBounds.left!=17 || evidence.trustedBounds.top!=63,
                L"The independently verified retained guard must not overwrite odd physical coordinates.");
            const auto admitted=MakeActivePictureObservation(evidence,31,59.94);
            Assert::IsTrue(admitted.available && admitted.detector==ActivePictureDetectorKind::SourceSpatialMeasured,
                L"The new source detector must reach native decision input, not only an OSD measurement attachment.");
            Assert::AreEqual(evidence.trustedBounds.left,admitted.bounds.left);
        }
        TEST_METHOD(SourcePictureActiveNomineeDoesNotRequireLegacySymmetryAcquisition)
        {
            SourcePictureFrame frame(960,540);frame.Fill(17,63,939,487,240,530,490);
            const auto legacy=ExtractActivePictureEvidence(frame.Source());
            Assert::IsTrue(legacy.axisEvidence.vertical.state!=ActivePictureAxisState::TRUSTED_BARS,
                L"This authentic asymmetric source deliberately fails the old paired-symmetry acquisition path.");
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(detected.evidence.available);
            Assert::IsTrue(detected.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::IsTrue(detected.evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured);
            Assert::AreEqual(63,detected.evidence.proposedBounds.top);
            Assert::AreEqual(487,detected.evidence.proposedBounds.bottom);
            Assert::IsTrue(detected.evidence.candidateVerification.vertical==ActivePictureCandidateVerificationState::VERIFIED);
        }
        TEST_METHOD(SourcePictureActiveCanvasRetainsSourceWithoutInventingPhysicalRatio)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(34,100,960,325,200,530,490);frame.Fill(34,385,960,540,180,490,540);
            frame.Fill(34,15,60,43,200,560,480);frame.Fill(360,20,422,40,180);
            frame.Fill(908,15,926,43,220,450,650);frame.Fill(35,64,165,82,200);
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(bool(detected.observation));
            Assert::IsTrue(detected.observation->outcome==SourcePictureOutcome::CanvasEstimate);
            const auto& evidence=detected.evidence;
            Assert::IsTrue(evidence.available && evidence.classification==ActivePictureClassification::FULL_RASTER_TRUSTED);
            Assert::IsTrue(evidence.detector==ActivePictureDetectorKind::SourceSpatialCanvas);
            Assert::IsFalse(evidence.physicalEdges.estimateAvailable);
            Assert::IsTrue(evidence.physicalEdges.confidence==PhysicalPictureEdgeConfidence::SOURCE_CANVAS_ESTIMATE);
            Assert::AreEqual(0,evidence.physicalEdges.estimate.right);Assert::AreEqual(0,evidence.physicalEdges.estimate.bottom);
            Assert::AreEqual(0,evidence.trustedBounds.left);Assert::AreEqual(0,evidence.trustedBounds.top);
            Assert::AreEqual(960,evidence.trustedBounds.right);Assert::AreEqual(540,evidence.trustedBounds.bottom);
            const auto admitted=MakeActivePictureObservation(evidence,32,60);
            Assert::IsTrue(admitted.available && admitted.detector==ActivePictureDetectorKind::SourceSpatialCanvas);
        }
        TEST_METHOD(SourcePictureMissingOrCancelledActiveFactsNeverBecomeLegacyVotes)
        {
            SourcePictureFrame frame(960,540);frame.Fill(0,80,960,460,240);
            const auto missing=MissingSourcePictureEvidence(frame.Source());
            Assert::IsFalse(missing.available);
            Assert::IsTrue(missing.detector==ActivePictureDetectorKind::SourceSpatialUnavailable);
            const auto missingVote=MakeActivePictureObservation(missing,40,60);
            Assert::IsFalse(missingVote.available);
            Assert::IsTrue(missingVote.detector==ActivePictureDetectorKind::SourceSpatialUnavailable);
            AnalysisEvaluationBudget expired([](void*){return true;},nullptr);
            const auto cancelled=DetectSourcePictureForWorker(frame.Source(),&expired);
            Assert::IsTrue(expired.Cancelled());Assert::IsFalse(cancelled.evidence.available);
            Assert::IsTrue(cancelled.evidence.detector==ActivePictureDetectorKind::SourceSpatialUnavailable);
            SourcePictureFrame noise(960,540);
            for(int y=0;y<540;y+=6)for(int x=0;x<960;x+=6)noise.Fill(x,y,x+2,y+2,240);
            const auto incomplete=DetectSourcePictureForWorker(noise.Source());
            Assert::IsTrue(bool(incomplete.observation) && !incomplete.observation->complete);
            Assert::IsFalse(incomplete.evidence.available);
            Assert::IsTrue(incomplete.evidence.detector==ActivePictureDetectorKind::SourceSpatialUnavailable);
        }
        TEST_METHOD(SourcePictureActiveAdapterPreservesPortraitAndCaptionMeasurement)
        {
            SourcePictureFrame portrait(540,960);portrait.Fill(31,41,509,917,240,530,490);
            const auto upright=DetectSourcePictureForWorker(portrait.Source());
            Assert::IsTrue(bool(upright.observation));
            Exact(*upright.observation,Bounds(540,960,31,41,509,917));
            Assert::IsTrue(upright.evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(31,upright.evidence.physicalEdges.estimate.left);
            for(int lines=0;lines<=3;++lines) {
                SourcePictureFrame frame(960,540);frame.Fill(0,66,960,426,220,530,490);
                for(int line=0;line<lines;++line)for(int glyph=0;glyph<52;++glyph) {
                    const int x=64+glyph*16,y=440+line*26;frame.Fill(x,y,x+10,y+18,850);
                }
                const auto detected=DetectSourcePictureForWorker(frame.Source());
                Assert::IsTrue(bool(detected.observation));
                Exact(*detected.observation,Bounds(960,540,0,66,960,426));
                Assert::IsTrue(detected.evidence.physicalEdges.estimateAvailable);
                Assert::AreEqual(66,detected.evidence.physicalEdges.estimate.top);
                Assert::AreEqual(426,detected.evidence.physicalEdges.estimate.bottom);
                Assert::IsTrue(detected.evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured);
                // Excluded glyph pixels still need the separate existing
                // caption/erasure proof; measurement is not that permission.
            }
        }
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
        TEST_METHOD(SourcePictureSparseInsetUsesBothPerpendicularContactsWithoutInventingVotes)
        {
            for(unsigned side=0;side<4;++side)for(int phase=0;phase<3;++phase)for(bool caption:{false,true}) {
                const bool horizontal=side==1 || side==3;
                if(!horizontal && caption)continue;
                ActivePictureBounds bounds;auto frame=SparseInsetFrame(side,phase,bounds);
                if(caption)frame.Caption(1,500);
                const auto measured=MeasureSourcePicture(frame.Source());Exact(measured,bounds);
                const auto& proof=measured.perpendicularInset;
                Assert::IsTrue(proof.accepted);Assert::AreEqual(int(side),proof.side);
                Assert::AreEqual(17u,proof.support,L"Independent contacts must not fabricate local ray votes.");
                Assert::AreEqual(3u,proof.regions);
            }
        }
        TEST_METHOD(SourcePictureSparseInsetRejectsContinuingOrMissingPerpendicularContact)
        {
            for(int fault=0;fault<3;++fault) {
                ActivePictureBounds bounds;auto frame=SparseInsetFrame(3,0,bounds,fault);
                const auto measured=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(measured.perpendicularInset.evaluated);
                if(fault==0)Assert::IsTrue(measured.perpendicularInset.outside[0]>0 && measured.perpendicularInset.outside[1]>0,
                    L"The continued-picture control must exercise both current outside-contact vetoes.");
                Assert::IsFalse(measured.perpendicularInset.accepted,
                    L"A continuing side, a side ending short, or support in only two regions cannot corroborate an inset.");
                Assert::IsFalse(measured.rectangleAvailable && measured.measuredBounds.bottom==bounds.bottom);
            }
            SourcePictureFrame caption(960,540);caption.Fill(0,70,960,438,300);caption.Caption(1,500);
            Exact(MeasureSourcePicture(caption.Source()),Bounds(960,540,0,70,960,438));
        }
        TEST_METHOD(SourcePictureRelativeMatteContactRecoversDarkSideWithoutChangingCaptionAr)
        {
            const int levels[8]={8,8,8,8,12,12,12,20};
            for(unsigned side=0;side<4;++side)for(bool full:{false,true})
                for(int raised:{0,16})for(bool caption:{false,true}) {
                const bool horizontal=side==1 || side==3;
                if(horizontal && caption)continue;
                const int floor=(full?0:64)+raised;
                const int width=horizontal?540:960,height=horizontal?960:540;
                SourcePictureFrame frame(width,height,floor);
                const auto bounds=horizontal?Bounds(width,height,70,0,470,height):
                    Bounds(width,height,0,70,width,470);
                for(int y=bounds.top;y<bounds.bottom;++y)for(int x=bounds.left;x<bounds.right;++x) {
                    const int along=(horizontal?x:y)-70;
                    const int inward=side==0?x:side==2?width-1-x:side==1?y:height-1-y;
                    const int base=floor+levels[(along/12)%8];
                    frame.Fill(x,y,x+1,y+1,base+(300-base)*inward/959);
                }
                if(caption)frame.Caption(1,500);
                const auto measured=MeasureSourcePicture(frame.Source(full?AnalysisLumaRange::Full:AnalysisLumaRange::Limited));
                Exact(measured,bounds);
                Assert::IsTrue(measured.relativeMatteContacts[side].accepted,
                    L"Every weak side must be proved against the measured bars, in both ranges and with raised source black.");
            }
        }
        TEST_METHOD(SourcePictureRelativeMatteContactRejectsFlatMatteAndUnmatchedReferences)
        {
            const int levels[8]={8,8,8,8,12,12,12,20};
            for(int fault=0;fault<4;++fault) {
                SourcePictureFrame frame(960,540);
                for(int y=70;y<470;++y) {
                    const int base=64+(fault==0?8:levels[((y-70)/12)%8]);
                    for(int x=0;x<960;++x) {
                        const int ramp=fault==0?std::max(0,x-192):x;
                        frame.Fill(x,y,x+1,y+1,base+(300-base)*ramp/(fault==0?767:959));
                    }
                }
                if(fault==1)frame.Fill(0,470,960,540,76);
                if(fault==3) {
                    // Quantized source noise in the bars is not picture texture.
                    for(int y=0;y<540;++y)for(int x=0;x<8;++x)
                        frame.Fill(x,y,x+1,y+1,(y>=70 && y<470?72:64)+(y%2)*4);
                }
                const auto measured=MeasureSourcePicture(frame.Source(fault==2?AnalysisLumaRange::Unknown:AnalysisLumaRange::Limited));
                Assert::IsFalse(measured.rectangleAvailable && measured.measuredBounds.left==0 && measured.measuredBounds.top==70,
                    L"A flat side matte, unmatched outer bars or unknown source range cannot supply relative-contact authority.");
            }
        }
        TEST_METHOD(SourcePictureChromaRoundingCannotEraseCoherentBoundaryCollar)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(0,70,960,470,300);
            frame.Fill(0,68,960,70,64,518,512);frame.Fill(0,470,960,472,64,518,512);
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(detected.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::AreEqual(68,detected.evidence.trustedBounds.top);
            Assert::AreEqual(472,detected.evidence.trustedBounds.bottom);
        }
        TEST_METHOD(SourcePictureNearBlackChromaRoundingKeepsIndependentCrop)
        {
            for(bool full:{false,true})for(int phase=0;phase<4;++phase) {
                const int floor=full?0:64;
                SourcePictureFrame frame(960,540,floor);
                frame.Fill(0,70,960,470,300);
                // Sparse near-black chroma differs by 5..8 canonical codes,
                // as in the captured black bars; luma stays within one code.
                frame.Fill(750,24,870,25,floor+4,517+phase,512);
                frame.Fill(30,511,150,512,floor,512,507-phase);
                const auto detected=DetectSourcePictureForWorker(frame.Source(full?AnalysisLumaRange::Full:AnalysisLumaRange::Limited));
                Exact(*detected.observation,Bounds(960,540,0,70,960,470));
                Assert::IsTrue(detected.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"Independent native crop verification must tolerate bounded near-black chroma rounding.");
                Assert::IsTrue(detected.evidence.candidateVerification.vertical==ActivePictureCandidateVerificationState::VERIFIED);
            }
        }
        TEST_METHOD(SourcePictureNearBlackChromaAllowancePreservesConnectedPicture)
        {
            for(int chroma:{518,520}) {
            SourcePictureFrame frame(960,540);
            frame.Fill(0,70,960,470,300);
            frame.Fill(80,60,200,70,68,chroma,512);
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsFalse(detected.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED);
            Assert::IsTrue(detected.evidence.candidateVerification.vertical==ActivePictureCandidateVerificationState::CONTRADICTED,
                L"Coherent current picture across the boundary remains a veto even at low luma.");
            }
        }
        TEST_METHOD(SourcePictureChromaAllowanceCannotHideLumaOrUnknownEncoding)
        {
            for(bool unknown:{false,true}) {
                SourcePictureFrame frame(960,540);
                frame.Fill(0,70,960,470,300);
                frame.Fill(750,24,870,25,unknown?64:76,unknown?518:512,512);
                const auto detected=DetectSourcePictureForWorker(frame.Source(unknown?AnalysisLumaRange::Unknown:AnalysisLumaRange::Limited));
                Assert::IsFalse(detected.evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED,
                    L"The chroma allowance cannot erase elevated luma or assume a known source range.");
            }
        }
        TEST_METHOD(SourcePictureInteriorComplexityCannotHideRasterCaption)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(0,0,960,12,300);frame.Fill(0,488,960,500,300);
            frame.Fill(0,0,12,500,300);frame.Fill(948,0,960,500,300);
            for(int y=30;y<470;y+=10)for(int x=30;x<930;x+=10)
                frame.Fill(x,y,x+2,y+2,240);
            for(int x=0;x<950;x+=14)frame.Fill(x,527,x+7,540,850);
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsFalse(observed.topologyComplete);
            Assert::IsTrue(std::any_of(observed.components.begin(),observed.components.end(),
                [](const auto& component){return component.glyphLike && component.bottom==540;}),
                L"Boundary captions retain classification priority over interior stars.");
            Assert::IsFalse(observed.rasterContact.sparseAccepted,
                L"The sparse native contact route must retain the same positive glyph veto.");
            Assert::IsFalse(observed.rectangleAvailable && observed.measuredBounds.bottom==540);
        }
        TEST_METHOD(SourcePictureOpposedSunContactsAndSparseStarsProveNativeRaster)
        {
            SourcePictureFrame frame(960,540,104);
            frame.Fill(0,0,440,540,500);
            // Bright source content reaches both vertical extremes over less
            // than half the width. Three separated near-edge stars and one
            // exact raster contact independently prove the dark right edge.
            for(int y:{60,280,460})frame.Fill(956,y,960,y+6,240);
            const auto observed=MeasureSourcePicture(frame.Source());
            Exact(observed,Bounds(960,540,0,0,960,540));
            Assert::IsTrue(observed.rasterContact.opposed);
            Assert::IsTrue(observed.rasterContact.sparseAccepted);
            Assert::IsTrue(observed.rasterContact.textureRegions>=3);
        }
        TEST_METHOD(SourcePictureOpposedSunCannotInventWidthThroughFlatMatteOrLoneLogo)
        {
            for(bool logo:{false,true}) {
                SourcePictureFrame frame(960,540,104);
                frame.Fill(0,0,440,540,500);
                if(logo)frame.Fill(956,460,960,470,240);
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsFalse(observed.rectangleAvailable);
                Assert::IsFalse(observed.rasterContact.sparseAccepted);
                Assert::IsTrue(observed.rasterContact.opposed);
            }
        }
        TEST_METHOD(SourcePictureEdgeMarksCannotBorrowBodyBehindThinMatte)
        {
            SourcePictureFrame frame(960,540,104);
            frame.Fill(0,0,956,540,500);
            for(int y:{60,280,460})frame.Fill(958,y,960,y+6,240);
            const auto observed=MeasureSourcePicture(frame.Source());
            Assert::IsFalse(observed.rasterContact.sparseAccepted);
            Assert::IsFalse(observed.rectangleAvailable && observed.measuredBounds.right==960,
                L"A distributed near-terminal inward onset must not be hidden by isolated edge marks.");
        }
        TEST_METHOD(SourcePictureComplexInteriorCannotInvalidateProvedRasterContacts)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(0,0,960,12,300);frame.Fill(0,528,960,540,300);
            frame.Fill(0,0,12,540,300);frame.Fill(948,0,960,540,300);
            for(int y=30;y<510;y+=10)for(int x=30;x<930;x+=10)
                frame.Fill(x,y,x+2,y+2,240);
            const auto observed=MeasureSourcePicture(frame.Source());
            Exact(observed,Bounds(960,540,0,0,960,540));
            Assert::IsFalse(observed.topologyComplete);
            Assert::AreEqual(size_t(512),observed.components.size());
            Assert::AreEqual(540,observed.components.front().bottom,
                L"Retained components must have complete extents even after the label budget fills.");
            Assert::IsFalse(ReconcileSourcePictureCandidate(observed,Bounds(960,540,0,80,960,460)).evaluated,
                L"Independent full-raster proof cannot turn incomplete topology into permission to crop.");
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(detected.evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(960,detected.evidence.physicalEdges.estimate.right);
            Assert::AreEqual(540,detected.evidence.physicalEdges.estimate.bottom);
        }
        TEST_METHOD(SourcePictureComplexInteriorCannotBypassInsetReconciliation)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(40,40,920,52,300);frame.Fill(40,488,920,500,300);
            frame.Fill(40,40,52,500,300);frame.Fill(908,40,920,500,300);
            for(int y=70;y<470;y+=10)for(int x=70;x<890;x+=10)
                frame.Fill(x,y,x+2,y+2,240);
            const auto detected=DetectSourcePictureForWorker(frame.Source());
            Assert::IsTrue(bool(detected.observation));
            const auto& observed=*detected.observation;
            Assert::IsFalse(observed.complete);Assert::IsFalse(observed.topologyComplete);
            Assert::IsFalse(observed.rectangleAvailable);Assert::IsFalse(observed.canvasSupported);
            Assert::IsTrue(observed.outcome==SourcePictureOutcome::Partial);
            Assert::AreEqual(size_t(512),observed.components.size());
            Assert::IsFalse(ReconcileSourcePictureCandidate(observed,Bounds(960,540,40,40,920,500)).evaluated);
            Assert::IsFalse(detected.evidence.physicalEdges.estimateAvailable);
            Assert::AreEqual(std::string("bounded-topology-component-limit"),
                std::string(detected.evidence.physicalEdges.reason));
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
        TEST_METHOD(SourcePictureOuterContradictionDominatesCompetingInnerEdges)
        {
            for(bool connected:{false,true}) {
                SourcePictureFrame frame(960,540);
                frame.Fill(0,80,960,110,200,530,490);
                frame.Fill(0,160,960,460,200,530,490);
                frame.Fill(34,15,60,43,220);
                frame.Fill(360,20,422,40,220);
                frame.Fill(908,15,926,43,220);
                if(connected) {
                    frame.Fill(44,40,48,84,220);
                    frame.Fill(400,36,404,84,220);
                    frame.Fill(916,40,920,84,220);
                }
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);Assert::IsTrue(observed.topologyComplete);
                const auto& top=observed.edges[unsigned(SourcePictureEdge::Top)];
                for(int coordinate:{80,160}) {
                    const auto found=std::find_if(top.begin(),top.end(),[&](const SourcePictureEdgeCandidate& edge) {
                        return edge.coordinate==coordinate;
                    });
                    Assert::IsTrue(found!=top.end());
                    Assert::IsFalse(found->overlayLike);
                    Assert::IsTrue(found->support>=32);
                    Assert::IsTrue(std::count_if(found->regionSupport.begin(),found->regionSupport.end(),
                        [](unsigned count){return count>=2;})>=3,
                        L"Both competing boundaries independently satisfy distributed edge qualification.");
                }
                for(unsigned side=0;side<4;++side) {
                    const auto& edges=observed.edges[side];
                    Assert::IsTrue(std::is_sorted(edges.begin(),edges.end(),[&](const auto& a,const auto& b) {
                        return side<2?a.coordinate<b.coordinate:a.coordinate>b.coordinate;
                    }),L"Qualification must walk from the enclosing edge toward nested alternatives.");
                }
                const auto outer=ReconcileSourcePictureCandidate(observed,Bounds(960,540,0,80,960,460));
                Assert::IsTrue(outer.evaluated);Assert::IsTrue(outer.contradicted);
                Assert::IsFalse(observed.rectangleAvailable,
                    L"Every nested eligible rectangle still excludes the positively observed exterior header.");
                Assert::IsTrue(std::all_of(top.begin(),top.end(),[](const auto& edge){return edge.contradicted;}));
                if(connected) {
                    const auto inner=ReconcileSourcePictureCandidate(observed,Bounds(960,540,0,160,960,460));
                    Assert::IsTrue(inner.evaluated);
                    Assert::IsFalse(inner.contradicted,
                        L"A deeper clip merges the header into one excluded panel; absence of a new count-based contradiction cannot revoke the outer witness.");
                }
            }
        }
        TEST_METHOD(SourcePicturePerpendicularDarkBoundaryFactsPreventFalseInsetGeometry)
        {
            for(bool transpose:{false,true})for(bool mirror:{false,true})for(int variant=0;variant<3;++variant) {
                SourcePictureFrame frame(transpose?540:960,transpose?960:540,variant==1?70:64);
                auto fill=[&](int left,int top,int right,int bottom,int luma) {
                    if(mirror) {const int oldLeft=left;left=960-right;right=960-oldLeft;}
                    if(transpose)frame.Fill(top,left,bottom,right,luma);
                    else frame.Fill(left,top,right,bottom,luma);
                };
                fill(280,70,960,470,300);
                if(variant==0)fill(0,70,280,470,70);
                const auto observed=MeasureSourcePicture(frame.Source());
                Assert::IsTrue(observed.complete);
                if(variant==0) {
                    Assert::IsTrue(observed.outcome==SourcePictureOutcome::Partial);
                    Assert::IsFalse(observed.rectangleAvailable,
                        L"A dark strip absent from coarse topology still supports both perpendicular native boundaries; its internal bright seam is not a measured picture edge.");
                    Assert::IsFalse(observed.canvasSupported);
                } else {
                    const int left=mirror?0:280,right=mirror?680:960;
                    Exact(observed,transpose?Bounds(540,960,70,left,470,right):Bounds(960,540,left,70,right,470));
                }
            }
            SourcePictureFrame original(3840,2160);
            original.Fill(0,276,3840,1884,300);
            original.Fill(0,276,1100,1884,70);
            original.Caption(2,1900);
            const auto observed=MeasureSourcePicture(original.Source());
            Assert::IsTrue(observed.complete);
            Assert::IsTrue(observed.outcome==SourcePictureOutcome::Partial);
            Assert::IsFalse(observed.rectangleAvailable,
                L"The original neutral +6-code scope strip must not publish left1100 as measured geometry, independently of crop-verifier vetoes.");
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
            const auto uncertaintyText=environment("VP_SOURCE_PICTURE_CAPTURE_EDGE_UNCERTAINTY");
            int uncertainty=0;
            if(!uncertaintyText.empty()) {
                Assert::AreEqual(1,sscanf_s(uncertaintyText.c_str(),"%d",&uncertainty));
                Assert::IsTrue(uncertainty>=0 && uncertainty<=2,L"Explicit native-edge uncertainty is bounded to at most two source rows/pixels.");
            }
            const auto sameCoordinate=[&](int actual,int expected){return std::abs(actual-expected)<=uncertainty;};
            const bool verify=environment("VP_SOURCE_PICTURE_CAPTURE_VERIFY")=="1";
            const bool requireCrop=environment("VP_SOURCE_PICTURE_CAPTURE_REQUIRE_TRUSTED_CROP")=="1";
            Assert::IsTrue(!requireCrop || verify,L"Private crop acceptance must execute the real verifier.");
            const bool captions=environment("VP_SOURCE_PICTURE_CAPTURE_CAPTION_VERIFY")=="1";
            const bool boundedWorker=environment("VP_SOURCE_PICTURE_CAPTURE_BOUNDED_WORKER")=="1";
            Assert::IsTrue(!boundedWorker || verify,L"Bounded worker replay requires the active detector and real verification path.");
            Assert::IsTrue(!captions || verify,L"Caption crop verification requires independent native spatial measurement.");
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
                if(boundedWorker) {
                    // Match live worker ownership; move the already loaded native
                    // buffer rather than making an extra full-raster copy.
                    auto owner=std::make_shared<std::vector<uint8_t>>(std::move(bytes));
                    source.data=owner->data();source.backingOwner=owner;
                }
                // No sidecar retained crop is passed into the measurement.
                const auto started=std::chrono::steady_clock::now();
                SourcePictureObservation direct;SourcePictureDetectionResult active;
                if(boundedWorker) {
                    ActivePictureFrameIdentity identity;
                    identity.transportGeneration=identity.sourceFormatGeneration=identity.analysisDomainKey=1;
                    identity.viewportGeneration=identity.rendererGeneration=1;
                    identity.acceptedSequence=identity.sourceFrameNumber=count+1;
                    identity.captureTimestamp=(count+1)*417083;
                    active=DetectPreparedSourcePictureBounded(source,identity);
                } else if(verify)active=DetectSourcePictureForWorker(source);
                else direct=MeasureSourcePicture(source);
                Assert::IsTrue(!verify || boundedWorker || bool(active.observation));
                // A real budget timeout is a failed frame, never an early exit
                // that prevents the rest of the explicitly supplied corpus.
                const auto& observed=verify && active.observation?*active.observation:direct;
                if(verify) {
                    const auto darkness=EvaluateActivePictureArDarkness(source,active.evidence);
                    Assert::IsTrue(darkness.global.evaluated,L"Active replay timing includes current source AR darkness evaluation.");
                }
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                ++count;samples+=observed.samples;totalMs+=elapsed;peakMs=std::max(peakMs,elapsed);
                bool matches=observed.complete && observed.sourceGeneration==1 && observed.width==width && observed.height==height;
                if(outcome=="Measured")matches=matches && observed.outcome==SourcePictureOutcome::Measured && observed.rectangleAvailable &&
                    sameCoordinate(observed.measuredBounds.left,left) && sameCoordinate(observed.measuredBounds.top,top) &&
                    sameCoordinate(observed.measuredBounds.right,right) && sameCoordinate(observed.measuredBounds.bottom,bottom);
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
                if(verify) {
                    const auto& evidence=active.evidence;
                    if(requireCrop) {
                        const auto& guard=evidence.trustedBounds;
                        const int collar=2*std::max(2,height/540);
                        matches=matches && evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED &&
                            guard.left<=left && guard.top<=top && guard.right>=right && guard.bottom>=bottom &&
                            left-guard.left<=collar && top-guard.top<=collar && guard.right-right<=collar && guard.bottom-bottom<=collar;
                    }
                    matches=matches && evidence.detector!=ActivePictureDetectorKind::Legacy;
                    if(outcome=="Measured")matches=matches && evidence.detector==ActivePictureDetectorKind::SourceSpatialMeasured &&
                        evidence.physicalEdges.estimateAvailable && sameCoordinate(evidence.physicalEdges.estimate.left,left) &&
                        sameCoordinate(evidence.physicalEdges.estimate.top,top) && sameCoordinate(evidence.physicalEdges.estimate.right,right) &&
                        sameCoordinate(evidence.physicalEdges.estimate.bottom,bottom);
                    if(outcome=="CanvasEstimate")matches=matches && evidence.detector==ActivePictureDetectorKind::SourceSpatialCanvas &&
                        evidence.available && !evidence.physicalEdges.estimateAvailable &&
                        evidence.trustedBounds.left==0 && evidence.trustedBounds.top==0 &&
                        evidence.trustedBounds.right==width && evidence.trustedBounds.bottom==height;
                    if(outcome=="Partial")matches=matches && evidence.detector==ActivePictureDetectorKind::SourceSpatialUnavailable && !evidence.available;
                    if(captions) {
                        // Replay the captured search hint only into the glyph
                        // scanner. Spatial geometry above never receives it.
                        auto key=std::make_unique<SubtitleBoxObservation>();
                        auto& id=key->identity;
                        id.transportGeneration=id.sourceFormatGeneration=id.analysisDomainKey=1;
                        id.viewportGeneration=id.rendererGeneration=1;
                        id.acceptedSequence=id.sourceFrameNumber=count;
                        id.captureTimestamp=count*417083;
                        key->width=width;key->height=height;key->analysisStride=1;
                        key->policyGeneration=key->continuityGeneration=1;
                        key->nearBarDistance=20;key->scriptHint=static_cast<SubtitleScriptHint>(script);
                        key->sharedPicture.required=true;key->sharedPicture.identity=id;
                        auto& hint=key->sharedPicture.inspectionCandidate;
                        hint.valid=retainedTop>0 && retainedBottom<height && retainedTop<retainedBottom;
                        hint.bounds={0,retainedTop,width,retainedBottom,width,height,
                            double(width)/std::max(1,retainedBottom-retainedTop),ActivePictureBounds::BarAxes::NONE};
                        hint.originIdentity=id;hint.policyGeneration=hint.continuityGeneration=hint.nativeGeometryRevision=1;
                        auto scanner=std::make_unique<SubtitleMeasurementSampler>();
                        auto caption=std::make_unique<SubtitleBoxObservation>(scanner->Measure(source,*key,{},24.0,evidence));
                        const auto masked=VerifySuppliedActivePictureCandidateWithSubtitle(source,evidence,*caption,id,1,1,key->scriptHint);
                        const bool cropped=evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED || masked.accepted;
                        matches=matches && cropped && caption->text.detected &&
                            (evidence.classification==ActivePictureClassification::BAR_CROP_TRUSTED || caption->subtitleArProof.accepted);
                        Logger::WriteMessage((std::string("Native captured caption/crop verification: ")+path+
                            " detected="+std::to_string(caption->text.detected)+" inspection="+std::to_string(caption->inspectionCandidateUsed)+
                            " worker_proof="+std::to_string(caption->subtitleArProof.accepted)+" crop_verified="+std::to_string(cropped)+" masked="+std::to_string(masked.accepted)+
                            " profile="+masked.maskedProfile.reason+
                            " mismatch_xy="+std::to_string(masked.maskedProfile.firstMismatchX)+","+std::to_string(masked.maskedProfile.firstMismatchY)+
                            " top_span="+std::to_string(masked.maskedProfile.topFirstMismatchY)+","+std::to_string(masked.maskedProfile.topLastMismatchY)+
                            " bottom_span="+std::to_string(masked.maskedProfile.bottomFirstMismatchY)+","+std::to_string(masked.maskedProfile.bottomLastMismatchY)+
                            " dy="+std::to_string(masked.maskedProfile.maxLumaDelta)+" duv="+std::to_string(masked.maskedProfile.maxChromaDelta)+
                            " caption_reason="+caption->text.diagnosticReason+" proof_reason="+masked.reason).c_str());
                    }
                    Logger::WriteMessage((std::string("Native source-picture active verification: ")+path+
                        " detector="+std::to_string(int(evidence.detector))+" available="+std::to_string(evidence.available)+
                        " classification="+std::to_string(int(evidence.classification))+
                        " vertical="+std::to_string(int(evidence.candidateVerification.vertical))+
                        " horizontal="+std::to_string(int(evidence.candidateVerification.horizontal))+
                        " mismatch_xy="+std::to_string(evidence.candidateVerification.firstMismatchX)+","+std::to_string(evidence.candidateVerification.firstMismatchY)+
                        " trusted="+std::to_string(evidence.trustedBounds.left)+","+std::to_string(evidence.trustedBounds.top)+"-"+
                        std::to_string(evidence.trustedBounds.right)+","+std::to_string(evidence.trustedBounds.bottom)+
                        " bounded_worker_24ms="+std::to_string(boundedWorker)+
                        " geometry_reused="+std::to_string(active.geometryReused)+" geometry_lookup_ms="+std::to_string(active.geometryLookupMs)+
                        " combined_measure_verify_darkness_ms="+std::to_string(elapsed)+" reason="+evidence.reason).c_str());
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
                " active_verification="+std::to_string(verify)+" frames="+std::to_string(count)+" mismatches="+std::to_string(mismatches)+" samples="+std::to_string(samples)+
                " total_ms="+std::to_string(totalMs)+" peak_ms="+std::to_string(peakMs)).c_str());
            Assert::IsTrue(count>0);Assert::AreEqual(size_t(0),mismatches);
        }
        TEST_METHOD(SourcePicturePausedPixelsRemainDeterministicAcrossCancelledWorkAndSourceIds)
        {
            SourcePictureFrame frame(960,540);
            frame.Fill(0,66,960,474,200,530,490);
            frame.Fill(0,450,960,474,68);
            const int starts[4]={0,250,525,750},lengths[4]={240,63,35,49};
            for(int region=0;region<4;++region)frame.Fill(starts[region],450,starts[region]+lengths[region],474,240,530,490);
            const auto expected=Bounds(960,540,0,66,960,474);
            for(uint64_t generation=1;generation<=12;++generation) {
                auto source=frame.Source();source.generation=generation;
                AnalysisEvaluationBudget cancelled([](void*) {return true;},nullptr);
                const auto missing=DetectSourcePictureForWorker(source,&cancelled);
                Assert::IsFalse(missing.evidence.physicalEdges.evaluated);
                Assert::IsFalse(missing.evidence.physicalEdges.estimateAvailable);
                Assert::AreEqual(int(SourcePictureOutcome::NotMeasured),int(missing.observation->outcome));
                const auto completed=DetectSourcePictureForWorker(source);
                Exact(*completed.observation,expected);
                Assert::IsTrue(completed.evidence.physicalEdges.estimateAvailable);
                Assert::AreEqual(generation,completed.evidence.physicalEdges.sourceGeneration);
                Assert::AreEqual(66,completed.evidence.physicalEdges.estimate.top);
                Assert::AreEqual(474,completed.evidence.physicalEdges.estimate.bottom);
            }
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
        TEST_METHOD(OwnedPictureFactsRequireExactPixelsAndCompatibleInterpretation)
        {
            auto frame=std::make_shared<SourcePictureFrame>(640,360);
            frame->Fill(0,40,640,320,220);
            auto source=frame->Source();source.backingOwner=frame;
            ActivePictureFrameIdentity id;id.transportGeneration=2;id.sourceFormatGeneration=7;
            id.analysisDomainKey=11;id.acceptedSequence=10;
            auto facts=std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(source));
            Assert::IsTrue(facts->complete);
            OwnedSourcePictureObservationCache cache;Assert::IsTrue(cache.Store(source,id,facts,3));
            auto copy=std::make_shared<SourcePictureFrame>(*frame);
            auto current=copy->Source();current.backingOwner=copy;current.generation=9;
            auto next=id;next.acceptedSequence=11;next.sourceFrameNumber=200;next.captureTimestamp=900;
            const auto reused=cache.Find(current,next,3);
            Assert::IsTrue(bool(reused));Assert::AreEqual(uint64_t(9),reused->sourceGeneration);
            Assert::AreEqual(uint64_t(7),facts->sourceGeneration);
            Exact(*reused,Bounds(640,360,0,40,640,320));
            // A single changed source pixel outside the measured rectangle is
            // still a different source and must not reuse geometry facts.
            copy->bytes[0]^=64;
            Assert::IsFalse(bool(cache.Find(current,next,3)));copy->bytes[0]^=64;
            for(int variant=0;variant<19;++variant) {
                auto altered=current;auto key=next;uint64_t continuity=3;
                if(variant==0)++key.transportGeneration;
                if(variant==1)++key.sourceFormatGeneration;
                if(variant==2)++key.analysisDomainKey;
                if(variant==3)++continuity;
                if(variant==4)key.acceptedSequence=9;
                if(variant==5)altered.range=AnalysisLumaRange::Full;
                if(variant==6)altered.chromaRowBytes+=2;
                if(variant==7)altered.colorspace=ColorSpace::UNKNOWN;
                if(variant==8)altered.rgbBitAlignment=1;
                if(variant==9)altered.width-=2;
                if(variant==10)altered.height-=2;
                if(variant==11)altered.format=AnalysisLumaFormat::P010;
                if(variant==12)altered.encoding=VideoFrameEncoding::UNKNOWN;
                if(variant==13)--altered.dataBytes;
                if(variant==14)key.transportGeneration=0;
                if(variant==15)key.sourceFormatGeneration=0;
                if(variant==16)key.analysisDomainKey=0;
                if(variant==17)key.acceptedSequence=0;
                if(variant==18)altered.generation=0;
                Assert::IsFalse(bool(cache.Find(altered,key,continuity)));
            }
            current.backingOwner.reset();Assert::IsFalse(bool(cache.Find(current,next,3)));

            // Valid padded planes allow row-stride and byte-extent changes to
            // remain valid source views, so their misses are interpretation
            // isolation rather than merely invalid-input rejection.
            const size_t paddedPitch=source.rowBytes+16;
            auto paddedBytes=std::make_shared<std::vector<uint8_t>>(paddedPitch*720+16,0);
            for(int row=0;row<720;++row)
                std::copy_n(frame->bytes.data()+source.rowBytes*row,source.rowBytes,paddedBytes->data()+paddedPitch*row);
            auto padded=source;padded.data=paddedBytes->data();padded.dataBytes=paddedBytes->size();
            padded.rowBytes=padded.chromaRowBytes=paddedPitch;padded.backingOwner=paddedBytes;
            auto paddedFacts=std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(padded));
            Exact(*paddedFacts,Bounds(640,360,0,40,640,320));
            Assert::IsTrue(cache.Store(padded,id,paddedFacts,3));
            auto reinterpreted=padded;reinterpreted.rowBytes=source.rowBytes;
            Assert::IsTrue(reinterpreted.IsValid());Assert::IsFalse(bool(cache.Find(reinterpreted,next,3)));
            reinterpreted=padded;reinterpreted.dataBytes-=16;
            Assert::IsTrue(reinterpreted.IsValid());Assert::IsFalse(bool(cache.Find(reinterpreted,next,3)));
            auto newBytes=std::make_shared<std::vector<uint8_t>>(*paddedBytes);
            auto samePadded=padded;samePadded.data=newBytes->data();samePadded.backingOwner=newBytes;
            Assert::IsTrue(bool(cache.Find(samePadded,next,3)));
            (*newBytes)[paddedPitch-1]^=1; // luma row padding, never a picture sample
            Assert::IsFalse(bool(cache.Find(samePadded,next,3)));
            Exact(MeasureSourcePicture(samePadded),Bounds(640,360,0,40,640,320));
        }
        TEST_METHOD(OwnedPictureFactsCacheRejectsMissingWorkAndBoundsRetainedStorage)
        {
            auto frame=std::make_shared<SourcePictureFrame>(640,360);
            frame->Fill(0,40,640,320,220);
            auto source=frame->Source();source.backingOwner=frame;
            ActivePictureFrameIdentity id;id.transportGeneration=1;id.sourceFormatGeneration=7;
            id.analysisDomainKey=1;id.acceptedSequence=1;
            auto facts=std::make_shared<SourcePictureObservation>(MeasureSourcePicture(source));
            OwnedSourcePictureObservationCache cache;Assert::IsTrue(cache.Store(source,id,facts));
            Assert::AreEqual(source.dataBytes,cache.RetainedSourceBytes());
            auto incomplete=std::make_shared<SourcePictureObservation>(*facts);incomplete->complete=false;
            Assert::IsFalse(cache.Store(source,id,incomplete));Assert::AreEqual(size_t(0),cache.RetainedSourceBytes());
            Assert::IsFalse(bool(cache.Find(source,id)));
            auto oversize=source;oversize.dataBytes=OwnedSourcePictureObservationCache::MaximumSourceBytes+1;
            Assert::IsFalse(cache.Store(oversize,id,facts));
            auto unowned=source;unowned.backingOwner.reset();Assert::IsFalse(cache.Store(unowned,id,facts));
            auto wrongGeneration=std::make_shared<SourcePictureObservation>(*facts);++wrongGeneration->sourceGeneration;
            Assert::IsFalse(cache.Store(source,id,wrongGeneration));
            Assert::IsTrue(cache.Store(source,id,facts));cache.Clear();
            Assert::AreEqual(size_t(0),cache.RetainedSourceBytes());
        }
        TEST_METHOD(OwnedPictureFactsUhdEqualityCostIsRecordedWithoutInventingGeometry)
        {
            auto frame=std::make_shared<SourcePictureFrame>(3840,2160);
            frame->Fill(0,263,3840,1897,220);
            auto source=frame->Source();source.backingOwner=frame;
            ActivePictureFrameIdentity id;id.transportGeneration=1;id.sourceFormatGeneration=7;
            id.analysisDomainKey=1;id.acceptedSequence=1;
            const auto facts=std::make_shared<const SourcePictureObservation>(MeasureSourcePicture(source));
            OwnedSourcePictureObservationCache cache;Assert::IsTrue(cache.Store(source,id,facts));
            auto copy=std::make_shared<SourcePictureFrame>(*frame);
            auto current=copy->Source();current.backingOwner=copy;
            double total=0,peak=0;
            for(unsigned repeat=0;repeat<12;++repeat) {
                ++id.acceptedSequence;
                const auto started=std::chrono::steady_clock::now();
                const auto reused=cache.Find(current,id);
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                total+=ms;peak=std::max(peak,ms);
                Assert::IsTrue(bool(reused));Exact(*reused,Bounds(3840,2160,0,263,3840,1897));
            }
            Logger::WriteMessage((std::string("Owned UHD source equality: bytes=")+std::to_string(source.dataBytes)+
                " repetitions=12 avg_ms="+std::to_string(total/12)+" peak_ms="+std::to_string(peak)).c_str());
        }
    };

}

