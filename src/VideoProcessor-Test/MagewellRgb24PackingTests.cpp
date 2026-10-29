#include "pch.h"
#include "CppUnitTest.h"
#include <magewell/MagewellRgb24Packing.h>
#include <algorithm>
#include <chrono>
#include <vector>
#include <sstream>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests {
TEST_CLASS(MagewellRgb24PackingTests) { public:
    TEST_METHOD(ScalarAndDispatchedPackingPreserveEveryRgbByteAndAllRowGuards)
    {
        for(uint32_t width:{1u,2u,6u,8u,15u,16u,17u,30u,32u,34u,130u})
        for(size_t offset:{size_t(0),size_t(1),size_t(16)})
        {
            constexpr uint32_t height=9;
            const uint32_t srcStride=(width*3+3)&~3u,dstStride=width*4+16;
            std::vector<uint8_t> src(size_t(srcStride)*height+64,0xCD);
            std::vector<uint8_t> expected(size_t(dstStride)*height+offset+64,0xA7);
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
            {
                for(uint32_t c=0;c<3;++c) {
                    const uint8_t value=uint8_t(x*73+y*31+c*101);
                    src[size_t(y)*srcStride+x*3+c]=value;
                    expected[offset+size_t(y)*dstStride+x*4+c]=value;
                }
                expected[offset+size_t(y)*dstStride+x*4+3]=255;
            }
            const auto original=src;
            for(bool simd:{false,true}) {
                std::vector<uint8_t> result(expected.size(),0xA7);
                Assert::IsTrue(MagewellRgb24Packing::Expand(src.data(),size_t(srcStride)*height,
                    srcStride,result.data()+offset,size_t(dstStride)*height,dstStride,width,height,simd));
                Assert::IsTrue(result==expected,L"RGB, alpha or padding/canary changed");
                Assert::IsTrue(src==original,L"Packing altered source bytes");
            }
        }
    }
    TEST_METHOD(InvalidAndOverlappingBuffersRejectBeforeAnyWrite)
    {
        std::vector<uint8_t> src(1000,0xCD),dst(1000,0xA7); const auto untouched=dst;
        for(bool simd:{false,true}) {
            Assert::IsFalse(MagewellRgb24Packing::Expand(nullptr,100,12,dst.data(),100,16,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),23,12,dst.data(),32,16,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),24,12,dst.data(),31,16,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),100,11,dst.data(),100,16,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),100,12,dst.data(),100,15,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),100,12,dst.data(),100,16,0,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),100,12,dst.data(),100,16,4,0,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(dst.data(),100,12,dst.data()+4,100,16,4,2,simd));
            Assert::IsFalse(MagewellRgb24Packing::Expand(src.data(),100,UINT32_MAX,dst.data(),100,UINT32_MAX,UINT32_MAX,2,simd));
            Assert::IsTrue(dst==untouched);
        }
    }
    TEST_METHOD(UhdPackingMeasuresFullFrameCostAndPreservesChannels)
    {
        constexpr uint32_t width=3840,height=2160;
        std::vector<uint8_t> src(size_t(width)*height*3),dst(size_t(width)*height*4);
        for(size_t i=0;i<src.size();++i)src[i]=uint8_t(i*37+i/389);
        double total=0,peak=0;
        for(int n=0;n<70;++n) {
            const auto start=std::chrono::steady_clock::now();
            Assert::IsTrue(MagewellRgb24Packing::Expand(src.data(),src.size(),width*3,
                dst.data(),dst.size(),width*4,width,height));
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(n>=10){total+=ms;peak=(std::max)(peak,ms);}
        }
        for(size_t p=0;p<size_t(width)*height;++p) {
            for(size_t c=0;c<3;++c)if(dst[p*4+c]!=src[p*3+c])Assert::Fail(L"UHD channel changed");
            if(dst[p*4+3]!=255)Assert::Fail(L"UHD alpha changed");
        }
        std::wostringstream message; message<<L"RGB24 UHD lossless packing: avg_ms="<<total/60<<L" peak_ms="<<peak;
        Logger::WriteMessage(message.str().c_str());
    }
}; }
