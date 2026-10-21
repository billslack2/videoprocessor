// Standalone CPU benchmark: build with AnalysisLumaSource.cpp (without its PCH).
// Optional arguments are native capture paths with the existing .txt sidecars.
// Captured pixels are local diagnostic inputs and must never be committed.
#define NOMINMAX
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Content = ActivePictureMeasurementCache::VerticalBarContent;
Content Reference(const AnalysisLumaSource& source, const ActivePictureBounds& picture, int black)
{
    Content result;
    const int width=source.width, height=source.height;
    const int x0=width/32,x1=width-x0,xStep=(std::max)(1,width/1920);
    const int rowStep=(std::max)(1,height/1080);
    const int columns=(std::max)(1,(x1-x0)/xStep);
    const auto scan=[&](int top,int bottom,int& first,int& last,int& peak) {
        first=height;last=peak=0;int rows=0;
        for(int y=top;y<bottom;y+=rowStep) {
            int samples=0,left=width,right=0;
            for(int x=x0;x<x1;x+=xStep) {
                AnalysisLumaSample pixel;
                const int code=source.Sample(x,y,pixel)?pixel.luma:0;
                if(code<=(std::min)(1023,black+32))continue;
                ++samples;left=(std::min)(left,x);right=(std::max)(right,x);
            }
            if(samples<(std::max)(8,columns/192) || right-left<(std::max)(24,width/48))continue;
            ++rows;peak=(std::max)(peak,samples);first=(std::min)(first,y);last=(std::max)(last,y+rowStep);
        }
        if(rows<2){first=last=0;return false;}return true;
    };
    result.upper=picture.top>rowStep*2 && scan(rowStep,picture.top-rowStep,result.upperTop,result.upperBottom,result.upperPeak);
    result.lower=picture.bottom+rowStep*2<height && scan(picture.bottom+rowStep,height-rowStep,result.lowerTop,result.lowerBottom,result.lowerPeak);
    return result;
}
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Same(const Content& a,const Content& b) {
    Require(a.upper==b.upper && a.lower==b.lower && a.upperTop==b.upperTop && a.upperBottom==b.upperBottom &&
        a.lowerTop==b.lowerTop && a.lowerBottom==b.lowerBottom && a.upperPeak==b.upperPeak && a.lowerPeak==b.lowerPeak,
        "bar content differs from original scan");
}
volatile int sink=0;
template<class Work> double Time(Work work,int runs=40) {
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<runs;++i){const auto value=work();sink+=value.upperPeak+value.lowerPeak;}
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/runs;
}
void CheckRows(const AnalysisLumaSource& source) {
    for(int y: {0,source.height/2,source.height*9/10,source.height-1})for(int step:{1,2,7,source.width+1})for(int threshold:{0,64,96,512,1023}) {
        AnalysisLumaRowContent got;Require(source.MeasureLumaRowContent(y,1,source.width-1,step,uint16_t(threshold),got),"row read failed");
        int samples=0,left=source.width,right=0;
        for(int x=1;x<source.width-1;x+=step) {
            AnalysisLumaSample pixel;Require(source.Sample(x,y,pixel),"reference pixel failed");
            if(pixel.luma<=threshold)continue;
            ++samples;left=(std::min)(left,x);right=x;
        }
        Require(got.samples==samples && got.left==left && got.right==right,"row threshold/count/span mismatch");
    }
    AnalysisLumaRowContent bad;
    Require(!source.MeasureLumaRowContent(-1,0,source.width,1,64,bad),"invalid row accepted");
    Require(!source.MeasureLumaRowContent(0,0,source.width,0,64,bad),"zero step accepted");
    Require(!source.MeasureLumaRowContent(0,0,source.width+1,1,64,bad),"invalid extent accepted");
}
void Run(const std::string& name, AnalysisLumaSource source,ActivePictureBounds picture) {
    Require(source.IsValid(),"invalid fixture source");CheckRows(source);
    ActivePictureFrameIdentity identity{1,42,1042,17500000,2,3,4,5};
    ActivePictureMeasurementCache cache;
    cache.Begin(source,identity,7,8);
    for(int black:{0,64,512,1023})for(int top:{0,picture.top,picture.top+2}) {
        auto bounds=picture;bounds.top=top;
        Same(cache.SubtitleContent(bounds,black),Reference(source,bounds,black));
        Same(cache.SubtitleContent(bounds,black),Reference(source,bounds,black));
    }
    auto black=64;
    Same(cache.SubtitleContent(picture,black),Reference(source,picture,black));
    const auto before=cache.Snapshot();
    cache.SubtitleContent(picture,black);
    Require(cache.Snapshot().subtitleHits==before.subtitleHits+1,"same-frame reuse missed");
    ++identity.acceptedSequence;cache.Begin(source,identity,7,8);
    cache.SubtitleContent(picture,black);
    Require(cache.Snapshot().subtitleHits==before.subtitleHits+1,"new source reused old observation");
    auto hits=cache.Snapshot().subtitleHits;
    cache.Begin(source,identity,8,8);cache.SubtitleContent(picture,black);
    Require(cache.Snapshot().subtitleHits==hits,"policy change reused observation");
    cache.Begin(source,identity,8,9);cache.SubtitleContent(picture,black);
    Require(cache.Snapshot().subtitleHits==hits,"continuity change reused observation");
    auto borrowed=source;borrowed.backingOwner.reset();cache.Begin(borrowed,identity,8,9);
    cache.SubtitleContent(picture,black);cache.SubtitleContent(picture,black);
    Require(cache.Snapshot().subtitleHits==hits,"borrowed pixels reused observation");
    cache.Begin(source,identity,7,8);cache.SubtitleContent(picture,black);
    ActivePictureMeasurementCache direct(false);direct.Begin(source,identity,7,8);
    const auto original=Time([&]{return Reference(source,picture,black);});
    const auto fast=Time([&]{return direct.SubtitleContent(picture,black);});
    const auto cached=Time([&]{return cache.SubtitleContent(picture,black);});
    std::cout<<name<<" reference_ms="<<original<<" row_scan_ms="<<fast<<" cached_ms="<<cached<<" speedup="<<original/fast<<"\n";
}
int main(int argc,char** argv) try {
    const int width=3840,height=2160;const size_t pitch=width*2+32;
    auto pixels=std::make_shared<std::vector<uint8_t>>(pitch*height*2,0);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const bool glyph=y>1940 && y<2020 && x>1200 && x<2640 && (x%17)<6;
        const uint16_t code=uint16_t((glyph?900:64)<<6);
        const auto offset=y*pitch+x*2;(*pixels)[offset]=uint8_t(code);(*pixels)[offset+1]=uint8_t(code>>8);
    }
    for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422}) {
        AnalysisLumaSource source{pixels->data(),pixels->size(),width,height,pitch,pitch,format,VideoFrameEncoding::V210,ColorSpace::REC_709,1};
        source.backingOwner=pixels;
        Run(AnalysisLumaFormatName(source),source,{0,274,width,1886,width,height,2.38,ActivePictureBounds::BarAxes::TOP_BOTTOM});
    }
    for(int argument=1;argument<argc;++argument) {
        const std::string path=argv[argument];std::ifstream file(path,std::ios::binary);
        auto capture=std::make_shared<std::vector<uint8_t>>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
        std::ifstream sidecar(path+".txt");AnalysisLumaSource source;int format,encoding,color,top,bottom,script,range=0;
        Require(bool(sidecar>>source.width>>source.height>>source.rowBytes>>source.chromaRowBytes>>format>>encoding>>color>>top>>bottom>>script),"invalid capture sidecar");
        sidecar>>range;source.data=capture->data();source.dataBytes=capture->size();source.backingOwner=capture;
        source.format=AnalysisLumaFormat(format);source.encoding=VideoFrameEncoding(encoding);source.colorspace=ColorSpace(color);source.range=AnalysisLumaRange(range);source.generation=1;
        if(!top && !bottom){top=source.height/8;bottom=source.height-top;}
        Run(path,source,{0,top,source.width,bottom,source.width,source.height,double(source.width)/(bottom-top),ActivePictureBounds::BarAxes::TOP_BOTTOM});
    }
    std::cout<<"Exact row results, bar extents/peak counts and same-frame invalidation checks passed.\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
