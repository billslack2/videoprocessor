#include <Windows.h>
#include "../../src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.h"
#include <libplacebo/options.h>
#include <iostream>
int main() {
 if(!LoadLibraryW(L"libplacebo-360.dll")) return 2;
 for(const char* q:{"fast","balanced","high"}) {
  LibplaceboRenderParameters::Settings s; s.quality=q;
  LibplaceboRenderParameters::Projection p; std::string error;
  if(!LibplaceboRenderParameters::Build(s,false,p,error)) {std::cerr<<error;return 3;}
  const auto& r=p.renderParams;
  std::cout<<q<<" up="<<(r.upscaler?r.upscaler->name:"built-in")<<" down="<<(r.downscaler?r.downscaler->name:"built-in")
   <<" tone="<<p.colorMapParams.tone_mapping_function->name<<" gamut="<<p.colorMapParams.gamut_mapping->name
   <<" contrast="<<p.colorMapParams.contrast_recovery<<" peak="<<(r.peak_detect_params?"on":"off")
   <<" percentile="<<(r.peak_detect_params?r.peak_detect_params->percentile:0)
   <<" deband="<<(r.deband_params?"on":"off")<<" sigmoid="<<(r.sigmoid_params?"on":"off")
   <<" dither="<<(r.dither_params?static_cast<int>(r.dither_params->method):-1)
   <<" error_diffusion="<<(r.error_diffusion?r.error_diffusion->name:"none")
   <<" frame_mixer="<<(r.frame_mixer?r.frame_mixer->name:"none")<<"\n";
  auto o=pl_options_alloc(nullptr); pl_options_reset(o,&r); std::cout<<"serialized "<<q<<" "<<pl_options_save(o)<<"\n";pl_options_free(&o);
 }
 return 0;
}