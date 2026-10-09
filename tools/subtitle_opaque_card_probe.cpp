#include <SubtitleOpaqueCardRefinement.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include <chrono>
#include <string>

struct Fixture {
    int w=3840,h=2160;std::vector<unsigned char> pixels;
    Fixture():pixels(size_t(w)*h*4,255) { fill({0,0,w,262},0);fill({0,1897,w,h},0); }
    void fill(SubtitleBoxRect r,unsigned char v) {for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x){size_t i=(size_t(y)*w+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=v;pixels[i+3]=255;}}
    void letters(int left,int right,int top,int bottom){for(int x=left;x<right;x+=34)fill({x,top,(std::min)(right,x+15),bottom},220);}
    AnalysisLumaSource source()const {AnalysisLumaSource s;s.data=pixels.data();s.dataBytes=pixels.size();s.width=w;s.height=h;s.rowBytes=w*4;s.format=AnalysisLumaFormat::NativeRgb;s.encoding=VideoFrameEncoding::BGRA_8BIT;s.colorspace=ColorSpace::REC_709;s.generation=1;return s;}
};
int main(int argc,char**argv) {
    int fails=0;
    auto check=[&](const char*name, bool expected,Fixture&f,SubtitleBoxRect seed,int expectedLines){SubtitleOpaqueCardRefinement r;const auto t=std::chrono::steady_clock::now();bool ok=RefineSubtitleOpaqueCard(f.source(),seed,48,263,1897,28,550,512,512,r);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();bool pass=ok==expected&&(!ok||r.glyphRowCount==expectedLines);printf("%s %s accepted=%d lines=%d interior=%d,%d-%d,%d samples=%u limited=%d ms=%.3f\n",pass?"PASS":"FAIL",name,ok,r.glyphRowCount,r.interior.left,r.interior.top,r.interior.right,r.interior.bottom,r.sampledPixels,r.workLimit,ms);for(int i=0;i<r.glyphRowCount;++i){auto q=r.glyphRows[i];printf(" row=%d,%d-%d,%d\n",q.left,q.top,q.right,q.bottom);}if(!pass)++fails;return r;};
    Fixture full;full.fill({1457,1715,2390,1897},0);full.letters(1489,2354,1737,1785);full.letters(1751,2089,1825,1873);
    auto fullResult=check("partial_tail_complete_rectangle",true,full,{1903,1825,2094,1876},2);if(fullResult.interior.Valid()&&(fullResult.interior.left>1458||fullResult.interior.right<2389||fullResult.glyphRows[0].left>1494||fullResult.glyphRows[1].left>1756))++fails;
    Fixture dark;dark.fill({0,263,3840,1897},0);dark.letters(1751,2089,1825,1873);check("unbounded_dark_scene",false,dark,{1903,1825,2094,1876},0);
    Fixture stepped;stepped.fill({1750,1715,2100,1800},0);stepped.fill({1457,1800,2390,1897},0);stepped.letters(1780,2060,1737,1785);stepped.letters(1751,2089,1825,1873);check("stepped_card_no_union_ownership",false,stepped,{1903,1825,2094,1876},0);
    Fixture stripe;stripe.fill({1457,1715,2390,1894},0);stripe.letters(1489,2354,1737,1785);stripe.letters(1751,2089,1825,1873);stripe.fill({1457,1894,2390,1897},180);check("bright_boundary_strip",true,stripe,{1903,1825,2094,1876},2);
    Fixture narrow;narrow.fill({1750,1815,2120,1889},0);narrow.letters(1770,2090,1825,1873);check("finite_near_bar_card",true,narrow,{1903,1825,2094,1876},1);
    for(int i=1;i<argc;++i){Fixture f;std::ifstream in(argv[i],std::ios::binary);in.read(reinterpret_cast<char*>(f.pixels.data()),f.pixels.size());if(!in){printf("FAIL read %s\n",argv[i]);++fails;continue;}check(argv[i],true,f,{1903,1825,2094,1876},2);}
    {
        const int w=640,h=360;std::vector<uint16_t> pixels(size_t(w)*h*3/2,uint16_t(512<<6));
        auto fill=[&](int l,int t,int r,int b,int level){for(int y=t;y<b;++y)for(int x=l;x<r;++x)pixels[y*w+x]=uint16_t(level<<6);};
        fill(0,0,w,h,200);fill(0,0,w,45,64);fill(0,315,w,h,64);fill(180,250,470,313,64);
        auto text=[&](int l,int t,int count){for(int n=0;n<count;++n){int x=l+n*13;fill(x,t,x+8,t+14,700);fill(x+2,t+2,x+6,t+12,64);}};
        text(200,260,18);text(220,291,17);
        AnalysisLumaSource src;src.data=reinterpret_cast<const uint8_t*>(pixels.data());src.dataBytes=pixels.size()*2;src.width=w;src.height=h;src.rowBytes=w*2;src.chromaRowBytes=w*2;src.format=AnalysisLumaFormat::P010;
        SubtitleOpaqueCardRefinement r;bool ok=RefineSubtitleOpaqueCard(src,{380,291,436,305},14,45,315,100,375,512,512,r);
        bool pass=ok&&r.glyphRowCount==2&&r.interior.left<=181&&r.interior.right>=469&&r.glyphRows[0].left<=201&&r.glyphRows[1].left<=221;
        printf("%s off_center_seed_card accepted=%d lines=%d interior=%d,%d-%d,%d samples=%u\n",pass?"PASS":"FAIL",ok,r.glyphRowCount,r.interior.left,r.interior.top,r.interior.right,r.interior.bottom,r.sampledPixels);if(!pass)++fails;
        for(bool color:{false,true}) {
            fill(180,250,470,313,64);text(240,291,16);fill(200,291,215,305,color?700:600);
            for(int y=291/2;y<(305+1)/2;++y)for(int x=200/2;x<(215+1)/2;++x){const size_t uv=size_t(w)*h+y*w+x*2;pixels[uv]=uint16_t((color?475:512)<<6);pixels[uv+1]=uint16_t((color?441:512)<<6);}
            ok=RefineSubtitleOpaqueCard(src,{380,291,436,305},14,45,315,100,375,512,512,r,650);
            pass=ok&&r.glyphRowCount==1&&r.glyphRows[0].left>=240;
            printf("%s core_ink_only color=%d left=%d rows=%d\n",pass?"PASS":"FAIL",color,r.glyphRows[0].left,r.glyphRowCount);if(!pass)++fails;
        }
    }
    printf("Failures: %d\n",fails);return fails?1:0;
}
