#include "ExperimentalAspectDetector.h"
#include <cstdio>
using namespace ExperimentalAspect;
int main(){
 auto reader=[](int x,int y,int&v){int lane=x/384;int top=112+(lane%3)*12,bottom=112+((lane+1)%3)*12;v=(y<68||y>=2092)?12:((y<top||y>=2160-bottom)?22:600);return true;};
 auto r=Measure(3840,2160,0,reader,[]{return true;});
 printf("RESULT kind=%d bounds=%d,%d,%d,%d reason=%s support=%.3f/%.3f score=%.3f\n",int(r.kind),r.left,r.top,r.right,r.bottom,r.reason,r.edges[1].support,r.edges[3].support,r.score);
 return r.kind==Kind::Bars&&r.top==68&&r.bottom==2092?0:1;
}
