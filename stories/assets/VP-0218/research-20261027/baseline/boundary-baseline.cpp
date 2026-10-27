#define wmain unused_original_entry
#include "native-ad-replay.cpp"
#undef wmain
#include <LocalBoundaryDiagnostics.h>
#include <chrono>
int wmain(int argc,wchar_t**argv){try{
 if(argc!=3)throw std::runtime_error("usage: boundary-baseline capture out");
 fs::path cap=argv[1],out=argv[2]; std::vector<fs::path> paths;
 for(const auto&e:fs::directory_iterator(cap))if(e.path().extension()==L".bin")paths.push_back(e.path());
 std::sort(paths.begin(),paths.end());std::ofstream o(out/L"boundary-baseline.csv");o<<std::setprecision(12);
 o<<"frame,sequence,raw_class,raw_top,raw_bottom,raw_trusted_top,raw_trusted_bottom,raw_y_samples,raw_uv_samples,grid_available,grid_candidate,grid_top,grid_bottom,grid_columns,grid_rows,grid_samples,grid_clean,top_violations,bottom_violations,top_support,bottom_support,top_continuity,bottom_continuity,top_max_dy,bottom_max_dy,top_max_duv,bottom_max_duv,extract_us,grid_us\n";
 int i=0;for(const auto&p:paths){auto f=Read(p);f.source.data=f.bytes.data();
 const auto a=std::chrono::steady_clock::now(); const auto raw=ExtractActivePictureEvidence(f.source);
 const auto b=std::chrono::steady_clock::now(); const auto grid=SampleLocalBoundaryDiagnostics(f.source);
 const auto c=std::chrono::steady_clock::now();
 o<<++i<<','<<f.identity.acceptedSequence<<','<<Class(raw.classification)<<','<<raw.proposedBounds.top<<','<<raw.proposedBounds.bottom<<','<<raw.trustedBounds.top<<','<<raw.trustedBounds.bottom<<','<<raw.lumaSamples<<','<<raw.chromaSamples<<','<<grid.available<<','<<grid.candidateAvailable<<','<<grid.top<<','<<grid.bottom<<','<<grid.columns<<','<<grid.rows<<','<<grid.sampleCount<<','<<grid.sampledBarsClean<<','<<grid.topExcludedViolations<<','<<grid.bottomExcludedViolations<<','<<grid.topEdge.supportedSamples<<','<<grid.bottomEdge.supportedSamples<<','<<grid.topEdge.inwardContinuity<<','<<grid.bottomEdge.inwardContinuity<<','<<grid.topEdge.maxLumaDelta<<','<<grid.bottomEdge.maxLumaDelta<<','<<grid.topEdge.maxChromaDelta<<','<<grid.bottomEdge.maxChromaDelta<<','<<std::chrono::duration<double,std::micro>(b-a).count()<<','<<std::chrono::duration<double,std::micro>(c-b).count()<<'\n';
 }
 std::cout<<"Frames="<<i<<"; unchanged beta extractor and existing diagnostic grid; no crop authority added\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
