#include "pch.h"
#include "CppUnitTest.h"
#include <AnalysisFrame.h>
#include <P210FrameLayout.h>
#include <VideoState.h>
#include <vector>
#include <memory>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(NativeP010CaptureTests) {
public:
 TEST_METHOD(PlanarLayoutRejectsOddHeightShortBuffersAndPreservesExistingEncodingIds) {
  P010FrameLayout p;Assert::IsTrue(GetP010FrameLayout(3840,2160,p));
  Assert::AreEqual(size_t(24883200),p.frameBytes);Assert::AreEqual(size_t(16588800),p.planeBytes);
  Assert::IsFalse(GetP010FrameLayout(130,129,p));Assert::IsFalse(GetP010FrameLayout(131,130,p));
  Assert::IsFalse(GetP010FrameLayout(0xffffffffu,0xfffffffeu,p));
  std::vector<uint8_t> bytes(130*130*3);NativeP010PlaneView v;
  Assert::IsFalse(GetNativeP010PlaneView(bytes.data(),bytes.size()-1,130,130,v));
  Assert::IsTrue(GetNativeP010PlaneView(bytes.data(),bytes.size(),130,130,v));
  Assert::IsTrue(v.uv==bytes.data()+130*130*2);
  VideoState state;state.videoFrameEncoding=VideoFrameEncoding::P010;
  state.displayMode=std::make_shared<DisplayMode>(130,130,false,60000,1001);
  Assert::AreEqual(uint32_t(260),state.BytesPerRow());Assert::AreEqual(uint32_t(bytes.size()),state.BytesPerFrame());
  Assert::AreEqual(24u,VideoFrameEncodingBitsPerPixel(VideoFrameEncoding::P010));
  Assert::AreEqual(int(VideoFrameEncoding::P210)+1,int(VideoFrameEncoding::P010));
 }
 TEST_METHOD(AllTenBitSamplesAndChromaRowsMatchForLiveAndQueuedConsumers) {
  constexpr int w=128,h=128;
  auto pixels=std::make_shared<std::vector<uint16_t>>(w*h*3/2);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x)(*pixels)[y*w+x]=uint16_t(((y*w+x)%1024)<<6);
  for(int y=0;y<h/2;++y)for(int x=0;x<w;x+=2){(*pixels)[w*h+y*w+x]=uint16_t((y*7+x/2)<<6);(*pixels)[w*h+y*w+x+1]=uint16_t((900-y*3-x/2)<<6);}
  uint64_t key=0;
  for(const auto profile:{AnalysisSourceProfile::LiveRenderer,AnalysisSourceProfile::QueuedCapture}) {
   AnalysisFrameInput input;input.width=w;input.height=h;input.encoding=VideoFrameEncoding::P010;
   input.colorspace=ColorSpace::BT_2020;input.nativeData=reinterpret_cast<const uint8_t*>(pixels->data());
   input.nativeBytes=pixels->size()*2;input.nativeRowBytes=w*2;input.nativeOwner=pixels;input.profile=profile;
   auto frame=PrepareAnalysisFrame(input);Assert::IsTrue(frame.IsValid());Assert::IsFalse(frame.usesConverted);
   Assert::AreEqual(10,int(frame.colorDepth));Assert::AreEqual(6,int(frame.bitShift));
   if(key)Assert::AreEqual(key,frame.domainKey);key=frame.domainKey;
   for(int y=0;y<h;++y)for(int x=0;x<w;++x){AnalysisLumaSample sample;Assert::IsTrue(frame.source.Sample(x,y,sample));
    Assert::AreEqual((y*w+x)%1024,int(sample.luma));
    Assert::AreEqual((y/2)*7+x/2,int(sample.chromaU));Assert::AreEqual(900-(y/2)*3-x/2,int(sample.chromaV));}
   --input.nativeBytes;Assert::IsFalse(PrepareAnalysisFrame(input).IsValid());
  }
 }
}; }
