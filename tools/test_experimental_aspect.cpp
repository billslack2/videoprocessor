// Standalone VP-0230 spatial detector tests; no renderer or subtitle dependencies.
#include "../src/VideoProcessor-Lib/ExperimentalAspectDetector.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace ExperimentalAspect;
int failures=0;
void dump(const Result&r){std::printf("kind=%d bounds=%d,%d,%d,%d score=%.3f reason=%s",int(r.kind),r.left,r.top,r.right,r.bottom,r.score,r.reason);for(auto&e:r.edges)std::printf(" edge=%d/%.2f/%.2f",e.depth,e.support,e.score);std::puts("");}
void check(bool b,const char* name){std::printf("%s %s\n",b?"PASS":"FAIL",name);if(!b)++failures;}
Result scene(int w,int h,int l,int t,int r,int b,bool dirty=false,bool captions=false){
 return Measure(w,h,64,[=](int x,int y,int& v){
   bool bar=x<l||x>=w-r||y<t||y>=h-b;
   v=bar?(dirty?80+(x*13+y*17)%11:64):300+(x*7+y*3)%90;
   if(captions && y>h-b+10 && y<h-10 && x>w/3 && x<2*w/3)v=800;
   return true;
 },[]{return true;});
}
int main(){
 auto a=scene(3840,2160,0,277,0,277,true,true);
 dump(a);check(a.kind==Kind::Bars&&a.top==277&&a.bottom==1883&&a.left==0&&a.right==3840,"dirty scope with bottom captions and unsnapped ratio");
 auto b=scene(1920,1080,240,0,240,0);
 check(b.kind==Kind::Bars&&b.left==240&&b.right==1680&&b.top==0,"pillarbox");
 auto c=scene(1920,1080,100,120,100,120);
 dump(c);check(c.kind==Kind::Bars&&c.left==100&&c.top==120&&c.right==1820&&c.bottom==960,"windowbox");
 auto full=scene(1920,1080,0,0,0,0);
 check(full.kind==Kind::FullRaster,"positive full raster");
 auto dark=Measure(1920,1080,64,[](int,int,int&v){v=70;return true;},[]{return true;});
 check(dark.complete&&dark.kind==Kind::Unknown,"dark ambiguity is not full raster");
 auto gradient=Measure(1920,1080,64,[](int,int y,int&v){v=64+std::min(y,1079-y)/3;return true;},[]{return true;});
 check(gradient.kind==Kind::Unknown,"smooth dark scene gradient is not a bar");
 auto read=Measure(1920,1080,64,[](int,int,int&){return false;},[]{return true;});
 check(!read.complete&&read.kind==Kind::Unknown,"failed source read");
 auto budget=Measure(1920,1080,64,[](int,int,int&v){v=300;return true;},[]{return false;});
 check(!budget.complete&&budget.kind==Kind::Unknown&&budget.samples==0,"expired deadline");
 auto cap=Measure(1920,1080,64,[](int,int,int&v){v=300;return true;},[]{return true;},70);
 check(!cap.complete&&cap.kind==Kind::Unknown&&cap.samples==70,"hard sample cap");
 auto fullRange=Measure(1920,1080,0,[](int,int y,int&v){v=y<140||y>=940?12:240;return true;},[]{return true;});
 check(fullRange.kind==Kind::Bars&&fullRange.top==140&&fullRange.bottom==940,"full-range lifted matte");
 auto tiny=scene(64,64,0,10,0,10);
 check(tiny.complete&&tiny.kind==Kind::Bars&&tiny.top==10&&tiny.bottom==54,"minimum supported raster");
 auto large=scene(8192,4320,0,600,0,600,true);
 dump(large);check(large.complete&&large.kind==Kind::Bars&&large.top==600&&large.bottom==3720&&large.samples<=24000,"large raster stays sample bounded");
 auto competing=[](int,int y,int&v){v=y<68||y>=2092?12:(y>=1838?40:600);return true;};
 auto paired=Measure(3840,2160,0,competing,[]{return true;});
 check(paired.kind==Kind::Bars&&paired.top==68&&paired.bottom==2092,"pair weaker real bottom edge against stronger interior edge");
 auto occluded=[](int x,int y,int&v){v=y<276||y>=1884?12:300;if(y>=1884&&x>1100&&x<2800)v=700;return true;};
 auto partial=Measure(3840,2160,0,occluded,[]{return true;});
 check(partial.kind==Kind::Bars&&partial.top==276&&partial.bottom==1884,"one strong edge with partial opposing evidence");
 Prior prior{3840,2160,0,276,3840,1884,true};
 auto withPrior=Measure(3840,2160,0,occluded,[]{return true;},24000,&prior);
 check(withPrior.kind==Kind::Bars&&withPrior.bottom==partial.bottom&&withPrior.score>partial.score,"measured history corroborates without moving boundaries");
 auto contradicted=Measure(3840,2160,0,competing,[]{return true;},24000,&prior);
 check(contradicted.kind==Kind::Bars&&contradicted.top==68&&contradicted.bottom==2092,"current expansion overrides incompatible history");
 auto missing=Measure(3840,2160,0,[](int,int y,int&v){v=y<276?12:300;return true;},[]{return true;},24000,&prior);
 check(missing.kind==Kind::Unknown,"one edge plus history cannot invent missing opposite bar");
 double total=0,worst=0;size_t samples=0;
 for(int i=0;i<100;++i){auto start=std::chrono::steady_clock::now();auto result=scene(3840,2160,0,277,0,277,true,true);
 auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;worst=std::max(worst,ms);samples=result.samples;}
 std::printf("Synthetic UHD: mean %.3f ms, max %.3f ms, samples %zu (not live pipeline timing)\n",total/100,worst,samples);
 return failures?1:0;
}
