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
 auto texturedSides=[](int x,int y,int&v){
   const int depth=std::min(x,3839-x);
   v=depth>=1120?460:136;
   if(depth<1000 && y>=160 && y<1200)
     v+=int(110*std::pow(std::sin(depth/210.0),2));
   return true;
 };
 const Prior falsePanel{3840,2160,1120,0,2720,2160,true};
 auto panel=Measure(3840,2160,64,texturedSides,[]{return true;},24000,&falsePanel);
 check(panel.complete&&panel.kind!=Kind::Bars&&panel.acceptedPairAttempt[0]==0,
       "distributed picture detail inside both proposed side bars vetoes the crop");
 auto narrow=scene(3840,2160,1120,0,1120,0);
 check(narrow.kind==Kind::Bars&&narrow.left==1120&&narrow.right==2720,
       "genuine black pillarbox can have the same narrow geometry");
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
 for(int black : {0,64}){
   auto raised=Measure(1920,1080,black,[=](int x,int y,int&v){
     v=black+((y<137||y>=943)?108+(x+3*y)%7:270+(x+y)%32);
     return true;
   },[]{return true;});
   check(raised.kind==Kind::Bars&&raised.top==137&&raised.bottom==943,"raised textured matte with straight relative edge");
   auto panel=Measure(1920,1080,black,[=](int,int y,int&v){
     v=black+((y<137||y>=943)?340:370);return true;
   },[]{return true;});
   check(panel.kind!=Kind::Bars,"small inward step on bright exterior cannot authorize crop");
 }
 auto tiny=scene(64,64,0,10,0,10);
 check(tiny.complete&&tiny.kind==Kind::Bars&&tiny.top==10&&tiny.bottom==54,"minimum supported raster");
 auto large=scene(8192,4320,0,600,0,600,true);
 dump(large);check(large.complete&&large.kind==Kind::Bars&&large.top==600&&large.bottom==3720&&large.samples<=24000,"large raster stays sample bounded");
 // Center graphics add an even stronger edge after an established pillarbox.
 // The shallow matte and first picture pixels determine the source boundary.
 for(int matte : {64,96}){
   Prior deepHint{3840,2160,320,0,3520,2160,true};
   auto nested=Measure(3840,2160,64,[=](int x,int y,int&v){
     const int depth=std::min(x,3839-x);
     v=depth<220?matte:(depth<320?matte+48+(y/18)%113:720);
     return true;
   },[]{return true;},24000,&deepHint);
   check(nested.kind==Kind::Bars&&nested.left==220&&nested.right==3620,
         "picture beyond clean exterior matte defeats stronger interior pair and history");
 }
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
 auto sidePicture=Measure(3840,2160,0,[](int x,int y,int&v){v=(y<276||y>=1884)&&x>=40?12:300;return true;},[]{return true;},24000,&prior);
 check(sidePicture.kind==Kind::Unknown,"visible picture at the side of a matte-like panel vetoes crop");
 auto uneven=Measure(1920,1080,64,[](int x,int y,int&v){int edge=x<400?120:124;v=y<edge||y>=1080-edge?64:300;return true;},[]{return true;});
 check(uneven.kind==Kind::Bars&&uneven.top==120&&uneven.bottom==960,"rough boundary retains earliest measured picture instead of median crop");
 // A stronger staggered interior edge wins coarse ranking, then loses native
 // support. The weaker straight picture boundary must remain available.
 auto refinementCompetition=[](int x,int y,int&v){
   const int lane=x/384;
   const int top=112+(lane%3)*12,bottom=112+((lane+1)%3)*12;
   v=(y<68||y>=2092)?12:((y<top||y>=2160-bottom)?22:600);
   return true;
 };
 auto recovered=Measure(3840,2160,0,refinementCompetition,[]{return true;});
 check(recovered.complete&&recovered.kind==Kind::Bars&&recovered.top==68&&recovered.bottom==2092&&
       recovered.pairAttempts[1]==2&&recovered.acceptedPairAttempt[1]==2,
       "failed native refinement retries retained true picture boundary");
 // Sweep interruption points through this small fixture, proving that retry
 // cannot publish an accepted prefix when either work bound expires.
 bool capSafe=true,deadlineSafe=true,capInterruptedRetry=false,deadlineInterruptedRetry=false;
 for(size_t limit=0;limit<recovered.samples;limit+=128){
   auto capped=Measure(3840,2160,0,refinementCompetition,[]{return true;},limit);
   capSafe=capSafe&&!capped.complete&&capped.kind==Kind::Unknown&&capped.samples<=limit&&
       capped.acceptedPairAttempt[0]==0&&capped.acceptedPairAttempt[1]==0;
   capInterruptedRetry=capInterruptedRetry||capped.pairAttempts[1]>=2;
   size_t reads=0;
   auto expired=Measure(3840,2160,0,[&](int x,int y,int&v){++reads;return refinementCompetition(x,y,v);},
       [&]{return reads<limit;});
   deadlineSafe=deadlineSafe&&!expired.complete&&expired.kind==Kind::Unknown&&
       expired.acceptedPairAttempt[0]==0&&expired.acceptedPairAttempt[1]==0;
   deadlineInterruptedRetry=deadlineInterruptedRetry||expired.pairAttempts[1]>=2;
 }
 check(capSafe&&capInterruptedRetry,"sample budget during retry publishes no incomplete crop");
 check(deadlineSafe&&deadlineInterruptedRetry,"deadline during retry publishes no incomplete crop");
 auto contradictedRetry=Measure(3840,2160,0,[&](int x,int y,int&v){
   refinementCompetition(x,y,v);if(x<40)v=600;return true;
 },[]{return true;});
 check(contradictedRetry.complete&&contradictedRetry.kind==Kind::Unknown&&
       contradictedRetry.pairAttempts[1]==2&&contradictedRetry.acceptedPairAttempt[1]==0,
       "retried pair still rejects exterior picture without further retry");
 // The full-extent hypothesis needs structured picture, not elevated black.
 auto structured=[](int x,int y){return int(250+90*std::sin(x/470.0)+60*std::cos(y/400.0));};
 auto noise=[](int x,int y){return (x*73+y*37+x*y*11)%17-8;};
 auto positiveFull=Measure(3840,2160,0,[&](int x,int y,int&v){v=structured(x,y);return true;},[]{return true;});
 check(positiveFull.kind==Kind::FullRaster&&positiveFull.fullExtentQualified&&positiveFull.fullExtentScore==1,
       "structured picture supports full extent on all four edges");
 auto raisedFull=Measure(3840,2160,0,[&](int x,int y,int&v){v=64+noise(x,y);return true;},[]{return true;});
 check(raisedFull.kind==Kind::FullRaster&&!raisedFull.fullExtentQualified,
       "raised noisy level alone does not qualify full extent");
 auto darkSidePicture=[&](int x,int y,int&v){
   const double blend=(std::min)(1.0,x/1200.0);
   v=int((1-blend)*(64+noise(x,y))+blend*structured(x,y));return true;
 };
 auto threeEdges=Measure(3840,2160,0,darkSidePicture,[]{return true;});
 check(threeEdges.kind==Kind::FullRaster&&threeEdges.fullExtentQualified&&
       !threeEdges.pictureEdgeSupported[0]&&threeEdges.unpairedBoundarySupport[0]==0,
       "three structured edges can outweigh dark side without coherent bar boundary");
 auto blackSide=Measure(3840,2160,0,[&](int x,int y,int&v){
   const double blend=(std::min)(1.0,x/1600.0);
   v=int(blend*structured(x,y));return true;
 },[]{return true;});
 check(blackSide.kind==Kind::FullRaster&&blackSide.fullExtentQualified&&blackSide.fullScore<.88&&
       !blackSide.pictureEdgeSupported[0],"three structured edges do not require brightness on dark fourth edge");
 for(int rise:{6,10,100}){
   auto oneSided=Measure(3840,2160,0,[&](int x,int y,int&v){
     if(x<490)v=64+noise(x,y);
     else {const double blend=(std::min)(1.0,(x-490)/1200.0);
       v=int((1-blend)*(64+rise+noise(x,y))+blend*structured(x,y));}
     return true;
   },[]{return true;});
   check(!oneSided.fullExtentQualified&&oneSided.unpairedBoundarySupport[0]>0,
         "coherent asymmetric raised bar competes with three-edge full hypothesis");
 }
 for(int rise:{6,100}){
   auto texturedBar=Measure(3840,2160,0,[&](int x,int y,int&v){
     const int matte=int(64+25*std::sin(y/400.0));
     if(x<490)v=matte;
     else {const double blend=(std::min)(1.0,(x-490)/1200.0);
       v=int((1-blend)*(matte+rise)+blend*structured(x,y));}
     return true;
   },[]{return true;});
   check(!texturedBar.fullExtentQualified&&texturedBar.unpairedBoundarySupport[0]>0,
       "structured texture on raised one-sided bar cannot erase its inward boundary");
 }
 auto raisedScope=Measure(3840,2160,0,[&](int x,int y,int&v){
   v=(y<276||y>=1884)?64+noise(x,y):structured(x,y);return true;
 },[]{return true;});
 check(!raisedScope.fullExtentQualified,"raised noisy scope bars do not qualify full extent");
 auto cornerOverlay=Measure(3840,2160,0,[&](int x,int y,int&v){
   v=64+noise(x,y);if((x<160||x>=3680)&&(y<90||y>=2070))v=structured(x,y);return true;
 },[]{return true;});
 check(!cornerOverlay.fullExtentQualified,"corner overlays do not establish full picture extent");
 auto incompleteFull=Measure(3840,2160,0,darkSidePicture,[]{return true;},threeEdges.samples-32);
 check(!incompleteFull.complete&&!incompleteFull.fullExtentQualified&&incompleteFull.kind==Kind::Unknown,
       "incomplete positive extent probe has no full-raster authority");
 size_t extentReads=0;
 auto expiredExtent=Measure(3840,2160,0,[&](int x,int y,int&v){++extentReads;return darkSidePicture(x,y,v);},
   [&]{return extentReads<threeEdges.samples-32;});
 check(!expiredExtent.complete&&!expiredExtent.fullExtentQualified&&expiredExtent.kind==Kind::Unknown,
       "deadline during structured extent probe publishes no accepted prefix");
 double total=0,worst=0;size_t samples=0;
 for(int i=0;i<100;++i){auto start=std::chrono::steady_clock::now();auto result=scene(3840,2160,0,277,0,277,true,true);
 auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;worst=std::max(worst,ms);samples=result.samples;}
 std::printf("Synthetic UHD: mean %.3f ms, max %.3f ms, samples %zu (not live pipeline timing)\n",total/100,worst,samples);
 return failures?1:0;
}
