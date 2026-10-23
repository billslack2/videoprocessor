#include "pch.h"
#include "CppUnitTest.h"
#include <vprenderer/ActivePictureMeasurementCache.h>
#include <AnalysisEvaluationBudget.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace {
LARGE_INTEGER frequency;
long long Now() { LARGE_INTEGER v; QueryPerformanceCounter(&v);return v.QuadPart; }
double Ms(long long ticks) { return double(ticks)*1000.0/double(frequency.QuadPart); }
unsigned long long Cycles() { ULONG64 c=0; QueryThreadCycleTime(GetCurrentThread(),&c);return c; }
double Quantile(std::vector<double> x,double q) { std::sort(x.begin(),x.end());return x[size_t(std::ceil((x.size()-1)*q))]; }
void Summary(std::ostream& o,const std::vector<double>& v) { o<<Quantile(v,.5)<<','<<Quantile(v,.95)<<','<<Quantile(v,.99)<<','<<Quantile(v,1); }
        struct OwnedMeasurementFrame
        {
            std::shared_ptr<const std::vector<uint8_t>> pixels;
            int width = 320, height = 180;
            size_t pitch = 656;

            // Construct each immutable backing before handing it to the cache.
            explicit OwnedMeasurementFrame(int pattern = 0)
            {
                std::vector<uint8_t> bytes(pitch * height * 2 + 32, 0);
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        const bool bar = pattern == 0 && (y < 22 || y >= 158);
                        const int code = pattern == 1 || bar ? 64 :
                            300 + (x % 16) * 4 + (y % 8) * 3;
                        Write(bytes, static_cast<size_t>(y) * pitch + x * 2, code);
                    }
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                        Write(bytes, pitch * height + static_cast<size_t>(y) * pitch + x * 2, 512);
                pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
            }

            static void Write(std::vector<uint8_t>& bytes, size_t offset, int code)
            {
                const auto packed = static_cast<uint16_t>(code << 6);
                bytes[offset] = static_cast<uint8_t>(packed);
                bytes[offset + 1] = static_cast<uint8_t>(packed >> 8);
            }

            AnalysisLumaSource Source() const
            {
                AnalysisLumaSource source{ pixels->data(), pixels->size(), width, height,
                    pitch, pitch, AnalysisLumaFormat::P010, VideoFrameEncoding::V210,
                    ColorSpace::REC_709, 17 };
                source.backingOwner = pixels;
                source.range = AnalysisLumaRange::Limited;
                return source;
            }
        };

        ActivePictureFrameIdentity Identity()
        {
            return { 1, 42, 1042, 17500000, 2, 3, 4, 5 };
        }

        struct Owned4kMeasurementFrame
        {
            static constexpr int Width = 3840, Height = 2160, Top = 260, Bottom = 1900;
            std::shared_ptr<const std::vector<uint8_t>> pixels;
            AnalysisLumaFormat format;
            size_t pitch;
            Owned4kMeasurementFrame(AnalysisLumaFormat representation, int pattern) : format(representation),
                pitch(size_t(Width) * (format == AnalysisLumaFormat::NativeRgb ? 4 : 2))
            {
                const int chromaRows = format == AnalysisLumaFormat::P010 ? Height / 2 : Height;
                std::vector<uint8_t> bytes(pitch * (format == AnalysisLumaFormat::NativeRgb ? Height : Height + chromaRows));
                for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x)
                {
                    const int top = pattern == 4 ? 200 : Top, bottom = pattern == 4 ? 1960 : Bottom;
                    const bool picture = pattern == 1 || (y >= top && y < bottom);
                    int code = picture ? 300 + (x % 16) * 4 + (y % 8) * 3 : 64;
                    if (pattern == 2 && picture)
                    {
                        const int innerTop = Top + (Bottom - Top) / 10;
                        const int innerHeight = (Bottom - Top) * 8 / 10;
                        const int gridX = (std::max)(0, (std::min)(23, (x - Width / 20) * 24 / (Width * 9 / 10)));
                        const int gridY = (std::max)(0, (std::min)(23, (y - innerTop) * 24 / innerHeight));
                        const bool lower = (gridX / 6 == 0 && gridY / 6 == 0) || (gridX / 6 == 3 && gridY / 6 == 3);
                        code = gridX % 6 < 4 ? (lower ? 80 : 96) : (lower ? 68 : 84);
                    }
                    if (pattern == 3 && x >= 1360 && x < 2480 && y >= 1740 && y < Bottom) code = 64;
                    if (pattern == 3 && x >= 1420 && x < 2420 && y >= 1860 && y < 1950 && (x / 16) % 2 == 0) code = 700;
                    const size_t offset = size_t(y) * pitch + size_t(x) * (format == AnalysisLumaFormat::NativeRgb ? 4 : 2);
                    if (format == AnalysisLumaFormat::NativeRgb)
                    {
                        const auto gray = static_cast<uint8_t>((std::max)(0, (std::min)(255, (code - 64) * 255 / 876)));
                        bytes[offset] = bytes[offset + 1] = bytes[offset + 2] = gray; bytes[offset + 3] = 255;
                    }
                    else OwnedMeasurementFrame::Write(bytes, offset, code);
                }
                if (format != AnalysisLumaFormat::NativeRgb)
                    for (int y = 0; y < chromaRows; ++y) for (int x = 0; x < Width; ++x)
                        OwnedMeasurementFrame::Write(bytes, pitch * Height + size_t(y) * pitch + size_t(x) * 2, 512);
                pixels = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
            }
            AnalysisLumaSource Source() const
            {
                AnalysisLumaSource source{ pixels->data(), pixels->size(), Width, Height, pitch,
                    format == AnalysisLumaFormat::NativeRgb ? 0 : pitch, format,
                    format == AnalysisLumaFormat::NativeRgb ? VideoFrameEncoding::BGRA_8BIT : VideoFrameEncoding::V210,
                    ColorSpace::REC_709, Identity().transportGeneration };
                source.range = format == AnalysisLumaFormat::NativeRgb ? AnalysisLumaRange::Full : AnalysisLumaRange::Limited;
                source.backingOwner = pixels; return source;
            }
            static ActivePictureBounds Bounds()
            { return { 0, Top, Width, Bottom, Width, Height, double(Width) / (Bottom - Top), ActivePictureBounds::BarAxes::TOP_BOTTOM }; }
        };


struct Prepared {
 ActivePictureEvidence raw;
 ActivePictureGlobalNearBlackEvidence global;
 ActivePictureArDarknessEvidence darkness;
};
Prepared Prepare(const AnalysisLumaSource& s) { Prepared p;p.raw=ExtractActivePictureEvidence(s);p.global=EvaluateActivePictureGlobalNearBlack(s);p.darkness=EvaluateActivePictureArDarkness(s,p.raw,p.global);return p; }
volatile size_t observed=0;
size_t Consume(const ActivePicturePresentationRetentionEvidence& p) { return p.lumaSamples+p.chromaSamples+size_t(p.CanRetainPresentation()); }
void ReuseRun(const AnalysisLumaSource& s,const AnalysisLumaSource& queued,const Prepared& ready,const ActivePictureBounds& base,int mode,ActivePictureMeasurementCache::Statistics* stats=nullptr) {
 if(mode==0) { const auto global=EvaluateActivePictureGlobalNearBlack(s);const auto retention=EvaluateActivePicturePresentationRetention(s,base);const auto ar=EvaluateActivePictureArDarkness(s,retention.activePicture,global);observed+=Consume(retention)+ar.lumaSamples;return; }
 ActivePictureMeasurementCache cache;cache.Begin(s,Identity(),7,8);
 if(mode==2) Assert::IsTrue(cache.SeedQueued(queued,Identity(),ready.raw,ready.darkness));
 const auto global=cache.GlobalNearBlack();const auto retention=cache.Retention(base);const auto ar=cache.ArDarkness(retention.activePicture,global);observed+=Consume(retention)+ar.lumaSamples;
 if(stats)*stats=cache.Snapshot();
}
struct Deadline { long long end;size_t checks=0;static bool Expired(void* p) { auto& d=*static_cast<Deadline*>(p);++d.checks;return Now()>=d.end; } };
struct BudgetResult { double elapsed=0,late=0;size_t frames=0,checks=0;unsigned long long cycles=0;bool cancelled=false,published=false; };
BudgetResult BudgetRun(const AnalysisLumaSource& s,const Prepared& ready,const ActivePictureBounds& base,double allowance,bool cooperative) {
 BudgetResult r;const auto c0=Cycles();const auto start=Now();Deadline d{start+long long(double(frequency.QuadPart)*allowance/1000.0)};
 AnalysisEvaluationBudget budget(&Deadline::Expired,&d);
 try {
  for(int frame=0;frame<3;++frame) {
   if(Now()>=d.end) {r.cancelled=true;break;}
   const auto v=EvaluateActivePicturePresentationRetention(s,base,ready.raw,ready.global,ready.darkness,cooperative?&budget:nullptr);
   observed+=Consume(v);++r.frames;
  }
  if(Now()>=d.end)r.cancelled=true;
  r.published=!r.cancelled&&r.frames==3;
 } catch(const AnalysisEvaluationCancelled&) { r.cancelled=true;r.published=false; }
 r.elapsed=Ms(Now()-start);r.late=(std::max)(0.0,r.elapsed-allowance);r.cycles=Cycles()-c0;r.checks=d.checks;
 Assert::IsFalse(r.cancelled&&r.published);return r;
}
}

namespace Measurements {
 TEST_CLASS(LookaheadBudgetSweep) {
 public:
 TEST_METHOD(Owned4kThreeFrameRetentionProofBudgetSweep) {
 QueryPerformanceFrequency(&frequency);
 const char* patterns[]={"scope","full","dark-textured","subtitle-edge-panel","expansion"};
 const AnalysisLumaFormat formats[]={AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::NativeRgb};
 const char* names[]={"P010","P210","BGRA8"};
 const double allowances[]={1.0,2.0,3.0,4.0,6.0,8.0,50.0};
 std::ofstream raw(R"(E:\codex\videoprocessor\render-jitter-deploy-20261022\artifacts\vp-0204\lookahead-review\helper-budget-sweep.csv)");
 raw<<"format,pattern,allowance_ms,trial,elapsed_ms,late_ms,completed_frames,cancelled,published,checkpoints,thread_cycles\n";
 raw<<std::fixed<<std::setprecision(6);
 for(int format=0;format<3;++format)for(int pattern=0;pattern<5;++pattern) {
  const Owned4kMeasurementFrame fixture(formats[format],pattern);const auto source=fixture.Source();const auto ready=Prepare(source);const auto base=Owned4kMeasurementFrame::Bounds();
  for(int warm=0;warm<4;++warm)for(double allowance:allowances)BudgetRun(source,ready,base,allowance,true);
  for(int trial=0;trial<101;++trial)for(int order=0;order<7;++order) {
   const double allowance=allowances[(order+trial)%7];const auto r=BudgetRun(source,ready,base,allowance,true);
   raw<<names[format]<<','<<patterns[pattern]<<','<<allowance<<','<<trial<<','<<r.elapsed<<','<<r.late<<','<<r.frames<<','<<r.cancelled<<','<<r.published<<','<<r.checks<<','<<r.cycles<<'\n';
  }
 }
 Assert::IsTrue(observed>0);Assert::IsTrue(raw.good());
 Logger::WriteMessage("15 owned 4K fixtures, 101 rotated trials at each allowance. Isolated three-frame retention helper; not renderer replay or runtime proof adoption.");
 }
 };
}
