// Offline replay of the exact production detector. stdin: little-endian P010
// frames (luma followed by interleaved CbCr, codes in bits 15..6). stdout: CSV. No video decoder needed.
#include <SubtitleBoxDetector.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <vector>
#include <cstring>
int main(int argc,char** argv) {
    if(argc!=5 && argc!=6) { std::fprintf(stderr,"width height pictureTop pictureBottom [latin|cjk|arabic|thai|general (legacy alias)]\n");return 2; }
    int w=std::atoi(argv[1]),h=std::atoi(argv[2]),top=std::atoi(argv[3]),bottom=std::atoi(argv[4]);
    if(w<2||h<2||w>8192||h>4320) return 2;
    _setmode(_fileno(stdin),_O_BINARY);
    std::vector<uint16_t> pixels(static_cast<size_t>(w)*h*3/2,512<<6);
    AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
    source.dataBytes=pixels.size()*2;source.width=w;source.height=h;source.rowBytes=w*2;source.chromaRowBytes=w*2;
    source.format=AnalysisLumaFormat::P010;source.generation=1;
    SubtitleBoxDetector detector;uint64_t frame=0;
    if(argc==6) {
        if(!std::strcmp(argv[5],"cjk")) detector.SetScriptHint(SubtitleScriptHint::Cjk);
        else if(!std::strcmp(argv[5],"arabic")) detector.SetScriptHint(SubtitleScriptHint::Arabic);
        else if(!std::strcmp(argv[5],"latin")) detector.SetScriptHint(SubtitleScriptHint::Latin);
        else if(!std::strcmp(argv[5],"thai")) detector.SetScriptHint(SubtitleScriptHint::Thai);
        else if(std::strcmp(argv[5],"general")) return 2;
    }
    std::puts("frame,left,top,right,bottom,cue,detected,held,revised,lines,cost_ms,reason,components,panel_left,panel_top,panel_right,panel_bottom");
    while(std::fread(pixels.data(),sizeof(uint16_t),pixels.size(),stdin)==pixels.size()) {
        auto start=std::chrono::steady_clock::now();
        auto r=detector.Analyze(source,top,bottom,++frame,1);
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("%llu,%d,%d,%d,%d,%llu,%d,%d,%d,%d,%.4f,%s,%u,%d,%d,%d,%d\n",frame,r.bounds.left,r.bounds.top,r.bounds.right,r.bounds.bottom,r.cue,r.detected,r.held,r.revised,r.lineCount,ms,r.diagnosticReason,r.diagnosticComponentCount,r.sourcePanel.left,r.sourcePanel.top,r.sourcePanel.right,r.sourcePanel.bottom);
    }
    return 0;
}
