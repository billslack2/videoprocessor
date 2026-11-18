
#define ExperimentalAspect BaselineAspect
#include "E:/codex/videoprocessor/ar-confidence-shadow-20261117/src/VideoProcessor-Lib/ExperimentalAspectDetector.h"
#undef ExperimentalAspect
#include "ExperimentalAspectDetector.h"
#include <cstdio>
int main(){
 int failures=0, recovered=0,newWrong=0,lostCorrect=0;
 auto read=[](int x,int y,int&v){int lane=x/384;int top=112+(lane%3)*12,bottom=112+((lane+1)%3)*12;v=(y<68||y>=2092)?12:((y<top||y>=2160-bottom)?22:600);return true;};
 auto good=ExperimentalAspect::Measure(3840,2160,0,read,[]{return true;});
 for(size_t cap=0;cap<good.samples;cap+=64){auto r=ExperimentalAspect::Measure(3840,2160,0,read,[]{return true;},cap);if(r.complete||r.kind!=ExperimentalAspect::Kind::Unknown||r.samples>cap)++failures;}
 for(int expiry=1;expiry<250;++expiry){int calls=0;auto r=ExperimentalAspect::Measure(3840,2160,0,read,[&]{return ++calls<expiry;});if(!r.complete&&r.kind!=ExperimentalAspect::Kind::Unknown)++failures;if(r.complete&&(r.top!=68||r.bottom!=2092))++failures;}
 auto exterior=[&](int x,int y,int&v){read(x,y,v);if(x<40)v=600;return true;};
 auto veto=ExperimentalAspect::Measure(3840,2160,0,exterior,[]{return true;});if(veto.kind==ExperimentalAspect::Kind::Bars)++failures;
 for(int base=80;base<=180;base+=4)for(int spread=0;spread<=48;spread+=6)for(int low=20;low<=60;low+=4){
  auto reader=[&](int x,int y,int&v){int lane=x/384,t=base+(lane%3)*(spread/2),b=base+((lane+1)%3)*(spread/2);v=(y<68||y>=2092)?12:((y<t||y>=2160-b)?low:600);return true;};
  auto old=BaselineAspect::Measure(3840,2160,0,reader,[]{return true;});
  auto r=ExperimentalAspect::Measure(3840,2160,0,reader,[]{return true;});
  bool wasRight=old.kind==BaselineAspect::Kind::Bars&&old.top==68&&old.bottom==2092;
  bool isRight=r.kind==ExperimentalAspect::Kind::Bars&&r.top==68&&r.bottom==2092;
  if(!wasRight&&isRight)++recovered;
  if(wasRight&&!isRight)++lostCorrect;
  if(old.kind!=BaselineAspect::Kind::Bars&&r.kind==ExperimentalAspect::Kind::Bars&&!isRight)++newWrong;
 }
 printf("budget_checks_failures=%d exterior_kind=%d recovered=%d lost_correct=%d newly_wrong=%d fixture_samples=%zu\n",failures,int(veto.kind),recovered,lostCorrect,newWrong,good.samples);
 return failures||lostCorrect||newWrong?1:0;
}
