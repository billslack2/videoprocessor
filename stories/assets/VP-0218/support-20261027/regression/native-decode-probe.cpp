#include <ActivePictureEvidence.h>
#include <AnalysisLumaSource.h>
#include <ColorSpace.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
namespace fs=std::filesystem;
static const char* cls(ActivePictureClassification c){switch(c){case ActivePictureClassification::UNAVAILABLE:return "UNAVAILABLE";case ActivePictureClassification::FULL_RASTER_TRUSTED:return "FULL_RASTER_TRUSTED";case ActivePictureClassification::BAR_CROP_TRUSTED:return "BAR_CROP_TRUSTED";case ActivePictureClassification::PROVISIONAL:return "PROVISIONAL";}return "?";}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=4){std::wcerr<<L"usage: native-decode-probe frame.bin frame.bin.txt output-base\n";return 2;}
 fs::path pixelPath=argv[1], sidecar=argv[2], base=argv[3];
 std::ifstream m(sidecar);std::string line;std::getline(m,line);std::istringstream h(line);int w=0,hh=0,fmt=0,enc=0,color=0,top=0,bottom=0,script=0;size_t row=0,crow=0;h>>w>>hh>>row>>crow>>fmt>>enc>>color>>top>>bottom>>script;if(!h)throw std::runtime_error("bad sidecar");int r=0;bool hasRange=bool(h>>r);
 std::ifstream p(pixelPath,std::ios::binary|std::ios::ate);auto n=p.tellg();if(!p||n<=0)throw std::runtime_error("bad frame");std::vector<uint8_t> bytes((size_t)n);p.seekg(0);p.read((char*)bytes.data(),n);if(!p)throw std::runtime_error("short frame");
 AnalysisLumaSource s;s.data=bytes.data();s.dataBytes=bytes.size();s.width=w;s.height=hh;s.rowBytes=row;s.chromaRowBytes=crow;s.format=(AnalysisLumaFormat)fmt;s.encoding=(VideoFrameEncoding)enc;s.colorspace=(ColorSpace)color;s.generation=1;s.range=hasRange?(AnalysisLumaRange)r:AnalysisLumaRange::Unknown;if(!s.IsValid())throw std::runtime_error("VideoProcessor AnalysisLumaSource::IsValid rejected frame");
 const size_t plane=(size_t)w*hh;std::vector<uint16_t> values(3*plane);for(int y=0;y<hh;++y)for(int x=0;x<w;++x){AnalysisLumaSample q;if(!s.Sample(x,y,q))throw std::runtime_error("Sample failed");size_t k=(size_t)y*w+x;values[k]=q.luma;values[plane+k]=q.chromaU;values[2*plane+k]=q.chromaV;}
 fs::path rawPath=base;rawPath+=L".yuv16";std::ofstream raw(rawPath,std::ios::binary);raw.write((const char*)values.data(),values.size()*sizeof(uint16_t));if(!raw)throw std::runtime_error("write planes failed");
 auto e=ExtractActivePictureEvidence(s);auto g=EvaluateActivePictureGlobalNearBlack(s);auto d=EvaluateActivePictureArDarkness(s,e,g);auto retention=EvaluateActivePicturePresentationRetention(s,e.trustedBounds,e,g,d);
 fs::path csv=base;csv+=L".csv";std::ofstream o(csv);o<<std::setprecision(12)<<"file,width,height,bytes,format,encoding,colorspace,range_field_present,range,decoder_verified_pixels,class,proposed_left,proposed_top,proposed_right,proposed_bottom,trusted_left,trusted_top,trusted_right,trusted_bottom,trusted_axes,reason,black_reference_mode,black_observed_floor,black_observed_threshold,black_floor,black_threshold,global_nearblack,global_cutoff,ar_distributed_dark,retention_analysis_valid,retention_presentation_valid,retention_pixel_safe,retention_can_retain,retention_outward_visible,retention_outward_top,retention_outward_bottom,retention_reason\n";
 o<<"\""<<pixelPath.filename().string()<<"\","<<w<<','<<hh<<','<<bytes.size()<<','<<fmt<<','<<enc<<','<<color<<','<<hasRange<<','<<int(s.range)<<','<<plane<<','<<cls(e.classification)<<','<<e.proposedBounds.left<<','<<e.proposedBounds.top<<','<<e.proposedBounds.right<<','<<e.proposedBounds.bottom<<','<<e.trustedBounds.left<<','<<e.trustedBounds.top<<','<<e.trustedBounds.right<<','<<e.trustedBounds.bottom<<','<<int(e.trustedBounds.trustedBarAxes)<<",\""<<e.reason<<"\",\""<<ActivePictureBlackReferenceModeName(e.blackReference.mode)<<"\","<<e.blackReference.observedFloor<<','<<e.blackReference.observedThreshold<<','<<e.blackReference.floor<<','<<e.blackReference.threshold<<','<<g.nearBlack<<','<<g.lumaCutoff<<','<<d.distributedDarkPicture<<','<<retention.analysisValid<<','<<retention.presentationValid<<','<<retention.currentlyPixelSafe<<','<<retention.CanRetainPresentation()<<','<<retention.outwardVisibleBoundsAvailable<<','<<retention.outwardVisibleBounds.top<<','<<retention.outwardVisibleBounds.bottom<<",\""<<retention.reason<<"\"\n";
 std::wcout<<L"decoded="<<pixelPath.filename().wstring()<<L" pixels="<<plane<<L" range="<<int(s.range)<<L" rangeField="<<hasRange<<L" class="<<cls(e.classification)<<L"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
