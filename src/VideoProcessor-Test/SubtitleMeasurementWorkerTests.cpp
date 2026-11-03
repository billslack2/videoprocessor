#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleMeasurementWorker.h>
#include <SubtitleCadenceGuard.h>
#include <vprenderer/SourceCadencePresentation.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(SubtitleMeasurementWorkerTests) {
    static ActivePictureFrameIdentity Identity(uint64_t sequence) {
        return {1,sequence,sequence,sequence*1000,2,3,4};
    }
    static SubtitleBoxPreview Ready(uint64_t sequence) {
        SubtitleBoxPreview preview;preview.available=true;
        preview.policyGeneration=11;preview.continuityGeneration=21;
        auto& current=preview.current;
        current.identity=Identity(sequence);current.analyzed=current.barAuthority=true;
        current.width=160;current.height=90;current.pictureTop=12;current.pictureBottom=78;
        current.policyGeneration=11;current.continuityGeneration=21;
        current.sharedPicture.required=true;current.sharedPicture.identity=current.identity;
        current.sharedPicture.bounds={0,12,160,78,160,90};
        auto& text=current.text;text.detected=true;text.lineCount=3;text.cue=1;
        text.bounds={50,50,110,80};text.capturePanelMeasured=true;
        text.capturePanel=text.sourcePanel={40,47,120,82};
        for(int line=0;line<3;++line) {
            text.lineBounds[line]={50,50+12*line,110,56+12*line};
            text.lineCapturePanels[line]=text.capturePanel;text.linePanels[line]=text.sourcePanel;
        }
        preview.text=text;return preview;
    }
    static std::vector<uint16_t> Pixels() {
        std::vector<uint16_t> pixels(160*90*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
        for(int line=0;line<3;++line)for(int y=50+12*line;y<56+12*line;++y)
            for(int x=50;x<110;++x)if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        return pixels;
    }
    static std::vector<uint16_t> PixelsWithCurrentBars() {
        auto pixels=Pixels();
        for(int y=12;y<78;++y)for(int x=0;x<160;++x)pixels[y*160+x]=uint16_t(300<<6);
        // Preserve the same accepted glyph rows, including the two bottom-row
        // pixels that cross the current lower bar plane.
        for(int line=0;line<3;++line)for(int y=50+12*line;y<56+12*line;++y)
            for(int x=50;x<110;++x)if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        return pixels;
    }
    static AnalysisLumaSource Source(std::vector<uint16_t>& pixels) {
        AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
        source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=160;source.height=90;
        source.rowBytes=source.chromaRowBytes=320;source.format=AnalysisLumaFormat::P010;
        source.generation=2;return source;
    }
    static AnalysisLumaSource CadenceSource(std::vector<uint16_t>& pixels) {
        auto source=Source(pixels);source.generation=1;return source;
    }
    static AnalysisLumaSource OwnedSource(const std::shared_ptr<std::vector<uint16_t>>& pixels) {
        auto source=Source(*pixels);source.backingOwner=pixels;return source;
    }
    static AnalysisLumaSource OwnedCadenceSource(const std::shared_ptr<std::vector<uint16_t>>& pixels) {
        auto source=CadenceSource(*pixels);source.backingOwner=pixels;return source;
    }
    static SubtitleBoxPreview Pending(uint64_t sequence) {
        auto preview=Ready(sequence);preview.available=false;preview.text={};
        preview.current.analyzed=false;preview.current.text={};return preview;
    }
    static SubtitleBoxObservation CompleteThreeLineScan(uint64_t sequence,
        const std::vector<uint16_t>& pixels,uint64_t continuity=21,int nearBarDistance=0,uint64_t rawCue=1) {
        auto scan=Ready(sequence).current;scan.continuityGeneration=continuity;
        scan.nearBarDistance=nearBarDistance;
        scan.independentBarAnalysis=true;scan.barEvidence.available=true;
        scan.barEvidence.top=scan.pictureTop;scan.barEvidence.bottom=scan.pictureBottom;
        scan.text.cue=rawCue;
        scan.text.topSearchComplete=scan.text.bottomSearchComplete=true;
        scan.text.signature.fill(0x55);
        for(auto& signature:scan.text.lineSignatures)signature.fill(0x55);
        auto ink=std::make_shared<SubtitleInkSnapshot>();
        ink->width=160;ink->height=90;ink->step=1;ink->sourceRows.resize(90);
        ink->rawEvidenceComplete=ink->paletteValid=true;ink->inkFloor=500;ink->blackLimit=96;
        ink->rawInk.resize((160*90+63)/64);ink->ownedInk.resize(ink->rawInk.size());
        ink->blackBacking.resize(ink->rawInk.size());
        for(int y=0;y<90;++y) {ink->sourceRows[y]=y;for(int x=0;x<160;++x) {
            const size_t i=size_t(y)*160+x;const uint64_t bit=uint64_t(1)<<(i%64);
            const int value=pixels[i]>>6;
            if(value>=500) {ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
            if(value<=96)ink->blackBacking[i/64]|=bit;
        }}
        scan.ink=ink;return scan;
    }
    static SubtitleBoxPreview ReassociateThreeLineCue(SubtitleBoxPresentation& tracker,
        const std::vector<uint16_t>& pixels,SubtitleBoxResult& accepted,bool addUnownedRaw=false,
        int nearBarDistance=0,uint64_t firstSequence=1,uint64_t rawCue=1) {
        const auto addUnownedRawBit=[&](SubtitleBoxObservation& observation) {
            if(!addUnownedRaw)return;
            auto ink=std::make_shared<SubtitleInkSnapshot>(*observation.ink);
            const size_t raw=size_t(35)*160+20,fringe=size_t(50)*160+49;
            ink->rawInk[raw/64]|=uint64_t{1}<<(raw%64);
            ink->ownedInk[fringe/64]&=~(uint64_t{1}<<(fringe%64));
            observation.ink=std::move(ink);
        };
        std::array<SubtitleBoxObservation,4> initial;
        for(size_t i=0;i<initial.size();++i) {
            initial[i]=CompleteThreeLineScan(firstSequence+i,pixels,21,nearBarDistance,rawCue);addUnownedRawBit(initial[i]);
        }
        const auto acquired=tracker.Consume(SubtitleBoxLookahead::Resolve(initial.data(),initial.size(),11,21));
        Assert::IsTrue(acquired.detected);Assert::AreEqual(3,acquired.lineCount);
        auto current=CompleteThreeLineScan(firstSequence+4,pixels,22,nearBarDistance,rawCue);addUnownedRawBit(current);
        auto preview=SubtitleBoxLookahead::Resolve(&current,1,11,22);
        Assert::IsFalse(preview.text.detected,L"One ready frame cannot freshly acquire a crowded cue.");
        accepted=tracker.Consume(preview,250,40,&initial[0].identity,true);
        Assert::IsTrue(accepted.detected);Assert::AreEqual(3,accepted.lineCount);
        Assert::IsFalse(accepted.held);Assert::AreEqual(acquired.cue,accepted.cue);
        Assert::AreEqual("backlog-current-cue-reassociated",tracker.DecisionReason());
        Assert::IsTrue(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
        return preview;
    }
    static SubtitleBoxPreview PendingAfterReassociation(uint64_t sequence,int nearBarDistance=0) {
        auto pending=Pending(sequence);pending.continuityGeneration=pending.current.continuityGeneration=22;
        pending.current.nearBarDistance=nearBarDistance;
        return pending;
    }
    static SubtitleBoxObservation WorkerKey(uint64_t sequence,uint64_t analysisDomain) {
        SubtitleBoxObservation key;key.identity=Identity(sequence);
        key.identity.analysisDomainKey=analysisDomain;
        key.width=640;key.height=360;key.policyGeneration=11;key.continuityGeneration=21;
        return key;
    }
    static bool WaitForTake(SubtitleMeasurementWorker& worker,
        const SubtitleBoxObservation& key,SubtitleBoxObservation& result) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            if(worker.TryTake(key,result))return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    static bool WaitForRead(SubtitleMeasurementWorker& worker,
        const SubtitleBoxObservation& key,SubtitleBoxObservation& result) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            if(worker.TryRead(key,result))return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    // Overload fallback keeps these regressions compilable and red before the
    // production queue bridge is present. It is test-only compatibility.
    template<class Guard>
    static auto ResolvePendingQueue(Guard& guard,SubtitleBoxPreview& preview,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const ActivePictureFrameIdentity* identity,bool queueOnly,int)
        -> decltype(guard.Resolve(preview,source,frameMs,holdMs,identity,queueOnly)) {
        return guard.Resolve(preview,source,frameMs,holdMs,identity,queueOnly);
    }
    template<class Guard>
    static bool ResolvePendingQueue(Guard& guard,SubtitleBoxPreview& preview,
        const AnalysisLumaSource& source,double frameMs,int holdMs,
        const ActivePictureFrameIdentity*,bool,long) {
        return guard.Resolve(preview,source,frameMs,holdMs);
    }
    struct ReleaseBlockedWorkers {
        std::atomic_bool& release;
        ~ReleaseBlockedWorkers(){release.store(true,std::memory_order_release);}
    };
    static bool WaitForEntered(const std::atomic_uint& entered,unsigned count) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(entered.load(std::memory_order_acquire)<count && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return entered.load(std::memory_order_acquire)>=count;
    }
    static bool WaitForWorkerIdle(SubtitleMeasurementWorker& worker) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto d=worker.TakeDiagnostics();if(!d.activeWorkers && !d.pending)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    static SubtitleBoxObservation HistoricalRawScan(uint64_t sequence) {
        auto pixels=PixelsWithCurrentBars();auto scan=CompleteThreeLineScan(sequence,pixels);
        scan.identity.analysisDomainKey=0xA11CE;scan.sharedPicture.identity=scan.identity;
        scan.text.topSearchComplete=scan.text.bottomSearchComplete=true;
        return scan;
    }
public:
// Standalone native-test fragment for SubtitleMeasurementWorkerTests.cpp.
// Put helper functions inside SubtitleMeasurementWorkerTests and the TEST_METHOD
// blocks at class scope. This remains an artifact fragment; no build was run.

static constexpr int kFootprintWidth=3840, kFootprintHeight=2160;
static constexpr int kFootprintTop=250, kFootprintBottom=1906;

static std::vector<uint16_t> CurrentFootprintUhdPixels(unsigned phase,bool caption=true) {
    const size_t area=size_t(kFootprintWidth)*kFootprintHeight;
    std::vector<uint16_t> pixels(area*3/2,uint16_t(512<<6)); // P010 Y + interleaved UV.
    auto* yplane=pixels.data();
    for(int y=0;y<kFootprintHeight;++y)for(int x=0;x<kFootprintWidth;++x) {
        int value=64;
        if(y>=kFootprintTop&&y<kFootprintBottom) {
            const int patterned=300+40*((x/64+y/32+int(phase))&1);
            const bool darkMovieStripe=(y>=1720&&x<640);
            const bool darkBacking=(y>=1720&&x>=1000&&x<2800);
            value=(darkMovieStripe||darkBacking)?48+16*((x/64+y/32+int(phase))&1):patterned;
        }
        yplane[size_t(y)*kFootprintWidth+x]=uint16_t(value<<6);
    }
    // Native two-line crossing-card fixture. Its actual placement is measured
    // below; this code only creates source pixels, never a detector mask.
    if(caption)for(int y:{kFootprintBottom-134,kFootprintBottom-52})
        for(int glyph=0;glyph<26;++glyph) {
            const int x=1210+glyph*52;
            for(int yy=y-2;yy<y+58;++yy)for(int xx=x-2;xx<x+36;++xx)
                if(xx>=0&&xx<kFootprintWidth&&yy>=0&&yy<kFootprintHeight)
                    yplane[size_t(yy)*kFootprintWidth+xx]=uint16_t(64<<6);
            for(int yy=y;yy<y+56;++yy)for(int xx=x;xx<x+34;++xx)
                yplane[size_t(yy)*kFootprintWidth+xx]=uint16_t(800<<6);
            for(int yy=y+10;yy<y+46;++yy)for(int xx=x+8;xx<x+26;++xx)
                yplane[size_t(yy)*kFootprintWidth+xx]=uint16_t(64<<6);
        }
    return pixels;
}
static AnalysisLumaSource CurrentFootprintUhdSource(std::vector<uint16_t>& pixels) {
    AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
    source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=kFootprintWidth;source.height=kFootprintHeight;
    source.rowBytes=source.chromaRowBytes=kFootprintWidth*2;source.generation=1;
    source.format=AnalysisLumaFormat::P010;source.range=AnalysisLumaRange::Limited;return source;
}
static SubtitleBoxObservation CurrentFootprintUhdKey(uint64_t sequence,const ActivePictureBounds& base) {
    SubtitleBoxObservation key;key.identity=Identity(sequence);
    key.identity.captureTimestamp=sequence*16683;key.identity.analysisDomainKey=0xA11CE;
    key.identity.sourceFormatGeneration=2;key.width=kFootprintWidth;key.height=kFootprintHeight;
    key.analysisStride=1;key.policyGeneration=11;key.continuityGeneration=51;
    key.nearBarDistance=20;key.scriptHint=SubtitleScriptHint::Latin;
    key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;key.sharedPicture.bounds=base;
    return key;
}
static void AddU800BarH(std::vector<uint16_t>& pixels,int x,int y) {
    const size_t area=size_t(kFootprintWidth)*kFootprintHeight;auto* yplane=pixels.data();
    for(int yy=y;yy<y+18;++yy)for(int xx=x;xx<x+16;++xx)
        if(xx<x+3||xx>=x+13||(yy>=y+7&&yy<y+10)) {
            yplane[size_t(yy)*kFootprintWidth+xx]=uint16_t(64<<6);
            const size_t uv=area+size_t(yy/2)*kFootprintWidth+(xx&~1);
            pixels[uv]=uint16_t(800<<6); // chroma-only cue over <=blackLimit luma.
        }
}
static std::unique_ptr<SubtitleBoxPreview> CurrentFootprintPendingPreview(
    const SubtitleBoxPreview& seed,uint64_t sequence,uint64_t continuity) {
    auto pending=std::make_unique<SubtitleBoxPreview>(seed);
    pending->available=false;pending->futureAvailable=false;pending->text={};pending->followingCount=0;
    auto& current=pending->current;current.identity.acceptedSequence=sequence;
    current.identity.sourceFrameNumber=sequence;
    current.identity.captureTimestamp=seed.current.identity.captureTimestamp+(sequence-seed.current.identity.acceptedSequence)*16683;
    current.sharedPicture.identity=current.identity;current.analysisStride=1;
    current.cadenceSkipped=false;current.analyzed=false;current.pendingRefresh=true;
    current.continuityGeneration=continuity;current.text={};current.ink.reset();
    pending->continuityGeneration=continuity;return pending;
}

TEST_METHOD(CurrentFootprintUhdWorkerAcceptsBlackMotionAndRejectsNewInk) {
    const ActivePictureBounds base{0,kFootprintTop,kFootprintWidth,kFootprintBottom,
        kFootprintWidth,kFootprintHeight,double(kFootprintWidth)/(kFootprintBottom-kFootprintTop),
        ActivePictureBounds::BarAxes::TOP_BOTTOM};
    auto seedPixels=CurrentFootprintUhdPixels(0);auto seedSource=CurrentFootprintUhdSource(seedPixels);
    Assert::IsTrue(seedSource.IsValid());

    // Real UHD source scans establish accepted source 88. Frame analysisStride
    // is 1; the detector's source-pixel grid is independently step 4.
    auto sampler=std::make_unique<SubtitleMeasurementSampler>();
    auto scans=std::make_unique<std::vector<SubtitleBoxObservation>>();scans->reserve(4);
    SubtitleBarTrackingReference prior;
    for(uint64_t sequence=88;sequence<=91;++sequence) {
        auto key=CurrentFootprintUhdKey(sequence,base);
        auto measured=sampler->Measure(seedSource,key,prior,60.0);
        Assert::IsTrue(measured.analyzed&&measured.text.detected&&!measured.text.workLimit&&
            measured.ink&&measured.ink->rawEvidenceComplete&&measured.ink->paletteValid,
            L"Only genuine current UHD detector ink may seed the footprint.");
        Assert::AreEqual(1u,measured.analysisStride);
        Assert::AreEqual(4,measured.ink->step,
            L"Observation cadence and ink-grid sampling step are independent.");
        prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);
        scans->push_back(std::move(measured));
    }
    auto preview=std::make_unique<SubtitleBoxPreview>(SubtitleBoxLookahead::Resolve(scans->data(),scans->size(),11,51));
    Assert::AreEqual(uint64_t(88),preview->current.identity.acceptedSequence,
        L"The accepted current source is exactly 88; later scans only confirm it.");
    auto tracker=std::make_unique<SubtitleBoxPresentation>();tracker->RefreshReferencePixels(*preview,seedSource);
    auto accepted=tracker->Consume(*preview,250,1000.0/60);
    Assert::IsTrue(accepted.detected&&!accepted.held&&!accepted.confirmationPending&&accepted.lineCount==2);
    auto compositor=std::make_unique<SubtitleCutPastePresentation>();
    const auto geometry=compositor->Consume(accepted,*preview,{},0);
    Assert::IsTrue(geometry.valid&&geometry.pictureCapture.Valid());

    // Exercise the typed worker route with a real accepted Black-background
    // operation. A single alternate-chroma pixel in its erase footprint must
    // fail current verification even though its luma remains bar-black and it
    // does not resemble the accepted subtitle palette.
    auto typedSeedPixels=std::make_shared<std::vector<uint16_t>>(seedPixels);
    auto typedSeedSource=CurrentFootprintUhdSource(*typedSeedPixels);
    typedSeedSource.backingOwner=typedSeedPixels;
    SubtitleBoxRect initialCleanup=geometry.generatedCleanup;
    if(!initialCleanup.Valid()&&geometry.generatedCleanupRowCount)for(unsigned i=0;i<geometry.generatedCleanupRowCount;++i) {
        const auto& row=geometry.generatedCleanupRows[i];
        if(!initialCleanup.Valid())initialCleanup=row;
        else initialCleanup={(std::min)(initialCleanup.left,row.left),(std::min)(initialCleanup.top,row.top),
            (std::max)(initialCleanup.right,row.right),(std::max)(initialCleanup.bottom,row.bottom)};
    }
    int alternateX=-1,alternateY=-1;
    for(int y=(std::max)(initialCleanup.top,base.top);initialCleanup.Valid()&&
        y<(std::min)(initialCleanup.bottom,base.bottom)&&alternateY<0;++y)
        for(int x=initialCleanup.left;x<initialCleanup.right;++x) {
            bool inCleanup=geometry.generatedCleanupRowCount==0 && geometry.generatedCleanup.Valid() &&
                x>=geometry.generatedCleanup.left && x<geometry.generatedCleanup.right &&
                y>=geometry.generatedCleanup.top && y<geometry.generatedCleanup.bottom;
            for(unsigned row=0;row<geometry.generatedCleanupRowCount;++row) {
                const auto& r=geometry.generatedCleanupRows[row];
                inCleanup|=r.Valid()&&x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
            }
            if(!inCleanup)continue;
            bool inGlyph=false;
            for(const auto& glyph:geometry.glyphLines)
                inGlyph|=glyph.Valid()&&x>=glyph.left&&x<glyph.right&&y>=glyph.top&&y<glyph.bottom;
            AnalysisLumaSample sample;
            if(!inGlyph&&seedSource.Sample(x,y,sample)&&sample.luma<=96&&
                std::abs(int(sample.chromaU)-512)<=48&&std::abs(int(sample.chromaV)-512)<=48) {
                alternateX=x;alternateY=y;break;
            }
        }
    Assert::IsTrue(alternateX>=0&&alternateY>=0,
        L"The compositor fixture must expose a non-glyph black pixel inside its cleanup footprint.");
    SubtitleCurrentCueTemplateInput typedSeed;typedSeed.observation=preview->current;
    typedSeed.acceptedCue=accepted;typedSeed.source=typedSeedSource;typedSeed.physicalBase=base;
    typedSeed.composedOperation=geometry;typedSeed.sourceMode=SubtitlePreviewMode::BlackBackground;
    typedSeed.compositionSucceeded=true;typedSeed.acceptedCurrentScan=true;typedSeed.backgroundMode=3;
    auto alternatePixels=std::make_shared<std::vector<uint16_t>>(*typedSeedPixels);
    const size_t typedArea=size_t(kFootprintWidth)*kFootprintHeight;
    const size_t typedUv=typedArea+size_t(alternateY/2)*kFootprintWidth+(alternateX&~1);
    (*alternatePixels)[typedUv]=uint16_t(800<<6);
    auto alternateSource=CurrentFootprintUhdSource(*alternatePixels);alternateSource.backingOwner=alternatePixels;
    auto alternateKey=CurrentFootprintUhdKey(93,base);
    alternateKey.pictureTop=preview->current.pictureTop;alternateKey.pictureBottom=preview->current.pictureBottom;
    alternateKey.barEvidence=preview->current.barEvidence;alternateKey.independentBarAnalysis=true;
    alternateKey.barAuthority=true;
    SubtitleMeasurementWorker typedWorker;typedWorker.AdvanceCurrentCueRenderWindow(alternateKey.identity,93);
    uint64_t typedRevision=0;
    Assert::IsTrue(typedWorker.SubmitCurrentCueTemplate(std::move(typedSeed),typedRevision)==
        SubtitleCurrentCueProofStatus::Queued);
    SubtitleCurrentCueProofRequest alternateRequest;alternateRequest.currentKey=alternateKey;
    alternateRequest.source=alternateSource;alternateRequest.physicalBase=base;
    alternateRequest.sourceMode=SubtitlePreviewMode::BlackBackground;alternateRequest.backgroundMode=3;
    alternateRequest.eligibleRenderHead=alternateKey.identity;alternateRequest.eligibleTailSequence=93;
    alternateRequest.templateRevision=typedRevision;alternateRequest.fullScanDue=false;
    Assert::IsTrue(typedWorker.SubmitCurrentCueProof(std::move(alternateRequest))==
        SubtitleCurrentCueProofStatus::Queued);
    SubtitleCurrentCueProofReceipt alternateReceipt;SubtitleCurrentCueProofStatus alternateStatus=SubtitleCurrentCueProofStatus::Queued;
    for(int attempt=0;attempt<1000;++attempt) {
        alternateStatus=typedWorker.TryTakeCurrentCueProof(alternateKey,typedRevision,alternateReceipt);
        if(alternateStatus==SubtitleCurrentCueProofStatus::Failed || alternateStatus==SubtitleCurrentCueProofStatus::Ready ||
            alternateStatus==SubtitleCurrentCueProofStatus::Expired)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert::IsTrue(alternateStatus==SubtitleCurrentCueProofStatus::Failed,
        L"A single alternate-chroma current pixel inside the cleanup footprint cannot be erased as old backing.");
    Assert::IsFalse(alternateReceipt.Valid());

    // Let a later full current proof start, then move the eligible render head
    // while it is running. Its eventual success must be discarded as stale.
    auto stalePixels=std::make_shared<std::vector<uint16_t>>(*typedSeedPixels);
    const std::weak_ptr<std::vector<uint16_t>> weakStale=stalePixels;
    auto staleSource=CurrentFootprintUhdSource(*stalePixels);staleSource.backingOwner=stalePixels;
    auto typedStaleKey=CurrentFootprintUhdKey(94,base);
    typedStaleKey.pictureTop=preview->current.pictureTop;typedStaleKey.pictureBottom=preview->current.pictureBottom;
    typedStaleKey.barEvidence=preview->current.barEvidence;typedStaleKey.independentBarAnalysis=true;
    typedStaleKey.barAuthority=true;typedWorker.AdvanceCurrentCueRenderWindow(typedStaleKey.identity,94);
    SubtitleCurrentCueProofRequest staleRequest;staleRequest.currentKey=typedStaleKey;
    staleRequest.source=staleSource;staleRequest.physicalBase=base;
    staleRequest.sourceMode=SubtitlePreviewMode::BlackBackground;staleRequest.backgroundMode=3;
    staleRequest.eligibleRenderHead=typedStaleKey.identity;staleRequest.eligibleTailSequence=94;
    staleRequest.templateRevision=typedRevision;staleRequest.fullScanDue=false;
    Assert::IsTrue(typedWorker.SubmitCurrentCueProof(std::move(staleRequest))==
        SubtitleCurrentCueProofStatus::Queued);
    SubtitleCurrentCueProofReceipt staleReceipt;SubtitleCurrentCueProofStatus staleStatus=SubtitleCurrentCueProofStatus::Queued;
    for(int attempt=0;attempt<100000 && staleStatus==SubtitleCurrentCueProofStatus::Queued;++attempt) {
        staleStatus=typedWorker.TryTakeCurrentCueProof(typedStaleKey,typedRevision,staleReceipt);
        if(staleStatus==SubtitleCurrentCueProofStatus::Queued)std::this_thread::yield();
    }
    Assert::IsTrue(staleStatus==SubtitleCurrentCueProofStatus::Pending,
        L"The UHD verification must be observed running before the source window advances.");
    auto newerHead=typedStaleKey.identity;++newerHead.acceptedSequence;++newerHead.sourceFrameNumber;
    newerHead.captureTimestamp+=16683;typedWorker.AdvanceCurrentCueRenderWindow(newerHead,newerHead.acceptedSequence);
    staleSource.backingOwner.reset(); // Drop the fixture caller copy as well as the pixel variable.
    stalePixels.reset();
    for(int attempt=0;attempt<1000;++attempt) {
        staleStatus=typedWorker.TryTakeCurrentCueProof(typedStaleKey,typedRevision,staleReceipt);
        if(staleStatus==SubtitleCurrentCueProofStatus::Expired)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert::IsTrue(staleStatus==SubtitleCurrentCueProofStatus::Expired,
        L"A successful completion after head advancement cannot produce a renderable receipt.");
    Assert::IsFalse(staleReceipt.Valid());
    for(int attempt=0;attempt<1000 && !weakStale.expired();++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Assert::IsTrue(weakStale.expired(),L"A stale proof releases its exact current source after completion.");

    auto cleanup=geometry.generatedCleanup;
    if(!cleanup.Valid()&&geometry.generatedCleanupRowCount)for(unsigned i=0;i<geometry.generatedCleanupRowCount;++i) {
        const auto& r=geometry.generatedCleanupRows[i];
        if(!cleanup.Valid())cleanup=r;else cleanup={
            (std::min)(cleanup.left,r.left),(std::min)(cleanup.top,r.top),
            (std::max)(cleanup.right,r.right),(std::max)(cleanup.bottom,r.bottom)};
    }
    Assert::IsTrue(cleanup.Valid(),L"The source-derived fixture must provide a measured cleanup rectangle.");
    int preparedRight=geometry.content.right;
    for(const auto& capture:geometry.glyphLines)if(capture.Valid())preparedRight=(std::max)(preparedRight,capture.right);
    const int paddedRawRight=(std::min)(kFootprintWidth,cleanup.right+8);
    Assert::IsTrue(paddedRawRight>preparedRight,
        L"The regression pads only detector line bounds beyond the already-prepared source/capture geometry.");
    for(int line=0;line<accepted.lineCount&&line<3;++line)
        if(accepted.lineBounds[line].Valid())accepted.lineBounds[line].right=
            (std::max)(accepted.lineBounds[line].right,paddedRawRight);
    auto paddedPreview=std::make_unique<SubtitleBoxPreview>(*preview);
    paddedPreview->text=accepted;paddedPreview->current.text=accepted;
    preview=std::move(paddedPreview);
    Assert::IsTrue(accepted.lineBounds[0].right>preparedRight&&
        accepted.lineBounds[0].right>=paddedRawRight,
        L"The raw detector box is deliberately wider than the prepared operation footprint.");
    int rawPaddingProbeX=-1,rawPaddingProbeY=-1,rawPaddingProbeLine=-1;
    for(int line=0;line<accepted.lineCount&&rawPaddingProbeX<0;++line) {
        const auto& raw=accepted.lineBounds[line];if(!raw.Valid())continue;
        const int top=(std::max)(cleanup.top,raw.top),bottom=(std::min)(cleanup.bottom,raw.bottom);
        for(int y=top;y<bottom&&rawPaddingProbeX<0;++y)
            for(int x=(std::max)(cleanup.left,preparedRight);x<(std::min)(cleanup.right,raw.right);++x) {
                bool inCapture=false;
                for(const auto& capture:geometry.glyphLines)
                    inCapture|=capture.Valid()&&x>=capture.left&&x<capture.right&&y>=capture.top&&y<capture.bottom;
                if(!inCapture&&x>=geometry.content.right) {
                    rawPaddingProbeX=x;rawPaddingProbeY=y;rawPaddingProbeLine=line;break;
                }
            }
    }
    Assert::IsTrue(rawPaddingProbeX>=0&&rawPaddingProbeY>=0,
        L"A cleanup pixel must lie outside prepared content/capture but inside the padded raw line box.");
    Assert::IsTrue(rawPaddingProbeLine>=0&&rawPaddingProbeX<accepted.lineBounds[rawPaddingProbeLine].right);
    int preparedContentProbeX=-1,preparedContentProbeY=-1;
    for(int y=(std::max)(cleanup.top,geometry.content.top);
        y<(std::min)(cleanup.bottom,geometry.content.bottom)&&preparedContentProbeX<0;++y)
        for(int x=(std::max)(cleanup.left,geometry.content.left);x<(std::min)(cleanup.right,geometry.content.right);++x) {
            AnalysisLumaSample sample;
            if(!seedSource.Sample(x,y,sample)||sample.luma>=preview->current.ink->inkFloor)continue;
            preparedContentProbeX=x;preparedContentProbeY=y;break;
        }
    Assert::IsTrue(preparedContentProbeX>=0&&preparedContentProbeY>=0,
        L"The fixture must provide non-glyph pixels inside the prepared cleanup/content intersection.");
    // This isolated non-caption pixel is outside the prepared operation. Its
    // current change must survive by shrinking that operation at its edge.
    seedPixels[size_t(rawPaddingProbeY)*kFootprintWidth+rawPaddingProbeX]=uint16_t(300<<6);
    // Keep one low-luma non-glyph sample inside the actual operation so the
    // matching changed-pixel control must still fail closed.
    seedPixels[size_t(preparedContentProbeY)*kFootprintWidth+preparedContentProbeX]=uint16_t(300<<6);
    const auto preservePreparedBackground=[&](std::vector<uint16_t>& pixels) {
        pixels[size_t(rawPaddingProbeY)*kFootprintWidth+rawPaddingProbeX]=uint16_t(340<<6);
        pixels[size_t(preparedContentProbeY)*kFootprintWidth+preparedContentProbeX]=uint16_t(300<<6);
    };

    auto footprint=std::make_unique<SubtitleCadenceGuard>();
    Assert::IsTrue(footprint->RememberCurrentFootprint(*preview,accepted,seedSource,base,geometry,
        1000.0/60,250,3));
    Assert::IsTrue(footprint->MatchesAcceptedSeed(*preview,accepted,seedSource,base));

    // A stride-1 verified presentation seed cannot become the previous frame
    // for cadence's stride-2 skipped-source path. Pixels are valid and unchanged;
    // eligibility must fail on the seed's observation stride itself.
    auto skipped89=CurrentFootprintPendingPreview(*preview,89,51);
    skipped89->current.analysisStride=2;skipped89->current.cadenceSkipped=true;
    skipped89->current.analyzed=false;skipped89->current.pendingRefresh=false;
    skipped89->current.analysisRefresh=false;skipped89->current.text={};skipped89->current.ink.reset();
    const auto rejectedCadenceSeed=footprint->Prepare(*skipped89,seedSource,base,1000.0/60,250);
    Assert::IsFalse(rejectedCadenceSeed.valid,
        L"An analyzed stride-1 footprint seed cannot authorize stride-2 skipped-frame reuse.");
    Assert::AreEqual("cadence-context-changed",rejectedCadenceSeed.reason);
    Assert::IsTrue(footprint->RememberCurrentFootprint(*preview,accepted,seedSource,base,geometry,
        1000.0/60,250,3),L"A rejected cadence probe must not prevent a fresh exact footprint seed.");
    auto source93=CurrentFootprintPendingPreview(*preview,93,55);

    // Source 88 -> 93 changes a picture-wide edge at x=4/y=1716 plus the
    // 48/64 dark backing inside the measured cleanup. Both dark values remain
    // black with neutral UV; the x=4 movie edge lies outside all mutation spans.
    auto movingPixels=CurrentFootprintUhdPixels(1);
    preservePreparedBackground(movingPixels);
    auto movingSource=CurrentFootprintUhdSource(movingPixels);
    const auto pureMotion=footprint->VerifyCurrentFootprint(source93->current,movingSource,
        preview->current,accepted,base);
    if(!pureMotion.valid)Logger::WriteMessage((std::string("Current footprint motion: ")+pureMotion.reason+" samples="+std::to_string(pureMotion.samples)+" mismatch="+std::to_string(pureMotion.mismatchX)+","+std::to_string(pureMotion.mismatchY)).c_str());
    Assert::IsTrue(pureMotion.valid&&pureMotion.samples>0,
        L"Current movie texture outside the footprint cannot veto this source; black backing may vary 48/64.");
    Assert::IsTrue(pureMotion.protectedCleanupRight<=rawPaddingProbeX,
        L"Changed nonblack movie pixels under the padded raw line bound must be kept by shrinking the prepared cleanup edge.");
    Logger::WriteMessage((std::string("Padded raw line projection point=")+std::to_string(rawPaddingProbeX)+","+
        std::to_string(rawPaddingProbeY)+" prepared_right="+std::to_string(preparedRight)+
        " raw_right="+std::to_string(accepted.lineBounds[rawPaddingProbeLine].right)+
        " cleanup_right="+std::to_string(cleanup.right)+
        " projected_right="+std::to_string(pureMotion.protectedCleanupRight)).c_str());

    Assert::IsTrue(pureMotion.protectedCleanupLeft>cleanup.left ||
        pureMotion.protectedCleanupRight<cleanup.right ||
        pureMotion.protectedCleanupTop>cleanup.top ||
        pureMotion.protectedCleanupBottom<cleanup.bottom,
        L"The positive case must exercise a verified cleanup-edge projection.");
    Assert::IsTrue(pureMotion.protectedCleanupLeft>cleanup.left ||
        pureMotion.protectedCleanupRight<cleanup.right,
        L"The moving UHD fixture must exercise a horizontal cleanup-edge projection.");

    auto changedInsidePixels=movingPixels;
    changedInsidePixels[size_t(preparedContentProbeY)*kFootprintWidth+preparedContentProbeX]=uint16_t(332<<6);
    auto changedInsideSource=CurrentFootprintUhdSource(changedInsidePixels);
    const auto insideFailure=footprint->VerifyCurrentFootprint(source93->current,changedInsideSource,
        preview->current,accepted,base);
    Assert::IsFalse(insideFailure.valid,
        L"The same nonblack change inside the actual content hull must still reject reuse.");
    Assert::AreEqual("current-cleanup-edge-not-protectable",insideFailure.reason);

    // Put dim current pixels in the exact original-minus-inner rectangle.
    // This is below the accepted ink floor, so it must remain current video
    // without granting caption or absence authority.
    const auto inProtectedBand=[&](int x,int y,int left,int right,int top,int bottom) {
        return x>=cleanup.left&&x<cleanup.right&&y>=cleanup.top&&y<cleanup.bottom&&
            !(x>=left&&x<right&&y>=top&&y<bottom);
    };
    const auto mapsIntoOldContent=[&](int x,int y) {
        if(!geometry.content.Valid()||!geometry.destination.Valid()||!geometry.source.Valid())return true;
        double sx=x+0.5+geometry.source.left-geometry.destination.left;
        double sy=y+0.5+geometry.source.top-geometry.destination.top;
        if(geometry.glyphScale<0.9999f||geometry.centeredGlyphMapping) {
            if(geometry.glyphScale<=0.0f)return true;
            sx=(x+0.5-geometry.glyphTranslateX)/geometry.glyphScale;
            sy=(y+0.5-geometry.glyphTranslateY)/geometry.glyphScale;
        }
        constexpr double glyphFilterHalo=4.0;
        return sx>=geometry.content.left-glyphFilterHalo&&sx<geometry.content.right+glyphFilterHalo&&
            sy>=geometry.content.top-glyphFilterHalo&&sy<geometry.content.bottom+glyphFilterHalo;
    };
    auto dimProtectedPixels=CurrentFootprintUhdPixels(1);preservePreparedBackground(dimProtectedPixels);
    int protectedX=-1,protectedY=-1;
    for(int y=cleanup.top;y<cleanup.bottom&&protectedY<0;++y)
        for(int x=cleanup.left;x<cleanup.right;++x) {
            if(!inProtectedBand(x,y,pureMotion.protectedCleanupLeft,pureMotion.protectedCleanupRight,
                pureMotion.protectedCleanupTop,pureMotion.protectedCleanupBottom))continue;
            const bool horizontalStrip=x<pureMotion.protectedCleanupLeft||x>=pureMotion.protectedCleanupRight;
            if(!horizontalStrip)continue;
            AnalysisLumaSample oldPixel;
            if(!seedSource.Sample(x,y,oldPixel)||oldPixel.luma<=preview->current.ink->blackLimit)continue;
            bool inLine=false;
            for(int line=0;line<accepted.lineCount;++line) {
                const auto& r=accepted.lineBounds[line];
                inLine|=x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
            }
            if(inLine||mapsIntoOldContent(x,y))continue;
            protectedX=x;protectedY=y;break;
        }
    Assert::IsTrue(protectedX>=0&&protectedY>=0,
        L"The projected horizontal edge must contain a nonblack, non-glyph source sample.");
    Assert::IsTrue(protectedX<pureMotion.protectedCleanupLeft||
        protectedX>=pureMotion.protectedCleanupRight,
        L"The preservation marker must exercise the horizontal strip, not only a vertical strip.");
    Assert::IsTrue(footprint->ContainsFootprintSample(protectedX,protectedY),
        L"The protected source sample must belong to the exact captured footprint.");
    dimProtectedPixels[size_t(protectedY)*kFootprintWidth+protectedX]=uint16_t(110<<6);
    auto dimProtectedSource=CurrentFootprintUhdSource(dimProtectedPixels);
    auto positiveWorker=std::make_unique<SubtitlePendingMeasurementGuard>();
    Assert::IsTrue(positiveWorker->RememberAccepted(*preview,accepted,seedSource,1000.0/60,250,
        nullptr,nullptr,&base,footprint.get()));
    auto positivePending=CurrentFootprintPendingPreview(*preview,93,55);
    Assert::IsTrue(positiveWorker->Resolve(*positivePending,dimProtectedSource,1000.0/60,250,
        &preview->current.identity,true,footprint.get()),L"Worker must retain the source-88 backlog token and validate source-93 current pixels.");
    Assert::IsTrue(positivePending->available&&positivePending->current.ink&&!positivePending->current.ink->rawEvidenceComplete);
    Assert::AreEqual(1u,positivePending->current.analysisStride);
    Assert::IsFalse(positivePending->current.cadenceSkipped,
        L"The source-93 frame is pending for the worker; it is not relabeled as cadence-skipped.");
    Assert::AreEqual(uint64_t(88),positiveWorker->AcceptedSeedIdentity().acceptedSequence,
        L"The original accepted source remains the seed.");
    Assert::AreEqual(uint64_t(93),positiveWorker->LastVerifiedIdentity().acceptedSequence,
        L"Only the current pixel-verified source advances the timeline.");
    Assert::AreEqual(5.0*(1000.0/60),positiveWorker->PendingElapsedMilliseconds(),0.01,
        L"Source 88→93 spends the complete five-frame gap against the original hold lease.");
    Assert::IsFalse(positivePending->current.subtitleArProof.accepted,
        L"A footprint continuation is not current subtitle-to-native authority.");
    Assert::IsFalse(positivePending->current.independentBarAnalysis||positivePending->current.barAuthority||
        positivePending->current.hasBarTrackingReference,
        L"Protected-row continuation grants no current bar or caption-absence authority.");

    const auto sameRect=[](const SubtitleBoxRect& a,const SubtitleBoxRect& b) {
        return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
    };
    SubtitleCutPasteGeometry copiedGeometry;
    auto staleKey=positivePending->current;++staleKey.identity.acceptedSequence;
    Assert::IsFalse(footprint->CopyCurrentFootprintGeometry(staleKey,base,copiedGeometry),
        L"A geometry copy is available only for the exact source whose pixels were just verified.");
    auto wrongBase=base;++wrongBase.top;
    Assert::IsFalse(footprint->CopyCurrentFootprintGeometry(positivePending->current,wrongBase,copiedGeometry),
        L"A receipt cannot transfer to a different accepted native base.");
    Assert::IsTrue(footprint->CopyCurrentFootprintGeometry(positivePending->current,base,copiedGeometry));
    Assert::IsTrue(copiedGeometry.valid);
    Assert::IsTrue(sameRect(geometry.source,copiedGeometry.source));
    Assert::IsTrue(sameRect(geometry.destination,copiedGeometry.destination));
    Assert::IsTrue(sameRect(geometry.content,copiedGeometry.content));
    Assert::IsTrue(sameRect(geometry.generatedCleanup,copiedGeometry.generatedCleanup));
    Assert::IsTrue(sameRect(geometry.pictureCapture,copiedGeometry.pictureCapture));
    Assert::IsTrue(sameRect(geometry.layoutCoverage,copiedGeometry.layoutCoverage));
    Assert::AreEqual(geometry.generatedCleanupRowCount,copiedGeometry.generatedCleanupRowCount);
    for(size_t line=0;line<geometry.glyphLines.size();++line)
        Assert::IsTrue(sameRect(geometry.glyphLines[line],copiedGeometry.glyphLines[line]));

    auto wrongGeometry=geometry;
    ++wrongGeometry.destination.top;
    Assert::IsFalse(footprint->ApplyCurrentFootprintProtection(positivePending->current,base,wrongGeometry),
        L"A protected-row receipt cannot be applied to modified placement geometry.");
    Assert::IsTrue(footprint->VerifyCurrentFootprint(positivePending->current,dimProtectedSource,
        preview->current,accepted,base).valid,
        L"The exact current source may renew a one-use receipt after the mismatch check.");
    auto appliedGeometry=geometry;
    Assert::IsTrue(footprint->ApplyCurrentFootprintProtection(positivePending->current,base,appliedGeometry),
        L"A successful worker receipt must apply to this exact source and original geometry before composition.");
    Assert::IsTrue(appliedGeometry.pendingProtectedCleanup.Valid(),
        L"The compositor must receive the one-frame protected interval.");
    Assert::AreEqual(cleanup.left,appliedGeometry.pendingProtectedCleanup.left);
    Assert::AreEqual(cleanup.top,appliedGeometry.pendingProtectedCleanup.top);
    Assert::AreEqual(cleanup.right,appliedGeometry.pendingProtectedCleanup.right);
    Assert::AreEqual(cleanup.bottom,appliedGeometry.pendingProtectedCleanup.bottom);
    Assert::AreEqual(pureMotion.protectedCleanupLeft,appliedGeometry.generatedCleanup.left);
    Assert::AreEqual(pureMotion.protectedCleanupTop,appliedGeometry.generatedCleanup.top);
    Assert::AreEqual(pureMotion.protectedCleanupRight,appliedGeometry.generatedCleanup.right);
    Assert::AreEqual(pureMotion.protectedCleanupBottom,appliedGeometry.generatedCleanup.bottom);
    Assert::AreEqual(geometry.generatedCleanupExclusion.left,appliedGeometry.generatedCleanupExclusion.left);
    Assert::AreEqual(geometry.generatedCleanupExclusion.top,appliedGeometry.generatedCleanupExclusion.top);
    Assert::AreEqual(geometry.generatedCleanupExclusion.right,appliedGeometry.generatedCleanupExclusion.right);
    Assert::AreEqual(geometry.generatedCleanupExclusion.bottom,appliedGeometry.generatedCleanupExclusion.bottom);
    Assert::AreEqual(geometry.layoutCoverage.left,appliedGeometry.layoutCoverage.left);
    Assert::AreEqual(geometry.layoutCoverage.top,appliedGeometry.layoutCoverage.top);
    Assert::AreEqual(geometry.layoutCoverage.right,appliedGeometry.layoutCoverage.right);
    Assert::AreEqual(geometry.layoutCoverage.bottom,appliedGeometry.layoutCoverage.bottom);
    SubtitleCutPasteGeometry consumedCopy;
    Assert::IsFalse(footprint->CopyCurrentFootprintGeometry(positivePending->current,base,consumedCopy),
        L"Applying the exact protected projection consumes the one-frame receipt.");
    Assert::IsTrue(protectedX>=appliedGeometry.pendingProtectedCleanup.left&&
        protectedX<appliedGeometry.pendingProtectedCleanup.right&&
        protectedY>=appliedGeometry.pendingProtectedCleanup.top&&protectedY<appliedGeometry.pendingProtectedCleanup.bottom);
    Assert::IsFalse(protectedX>=appliedGeometry.generatedCleanup.left&&
        protectedX<appliedGeometry.generatedCleanup.right&&
        protectedY>=appliedGeometry.generatedCleanup.top&&protectedY<appliedGeometry.generatedCleanup.bottom,
        L"The dim current pixel must be outside the cleanup actually applied to the renderer.");

    // Find an exact location in the REMAINING applied cleanup, outside the old
    // line bounds, for independent low-luma and alternate-chroma negatives.
    const auto insideErase=[&](int x,int y) {
        if(appliedGeometry.generatedCleanupRowCount) {
            for(unsigned i=0;i<appliedGeometry.generatedCleanupRowCount;++i) {
                const auto& r=appliedGeometry.generatedCleanupRows[i];
                if(y>=r.top&&y<r.bottom&&x>=r.left&&x<r.right)return true;
            }
            return false;
        }
        const auto& r=appliedGeometry.generatedCleanup;
        return r.Valid()&&x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
    };
    const auto inOldLine=[&](int x,int y) {
        for(int line=0;line<accepted.lineCount;++line) {
            const auto& r=accepted.lineBounds[line];
            if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return true;
        }
        return false;
    };
    int patchX=-1,patchY=-1;
    for(int y=appliedGeometry.generatedCleanup.top;y<appliedGeometry.generatedCleanup.bottom-18&&patchY<0;++y)
        for(int x=appliedGeometry.generatedCleanup.left;x<appliedGeometry.generatedCleanup.right-16;++x) {
            bool usable=true;
            for(int yy=y;yy<y+18&&usable;++yy)for(int xx=x;xx<x+16;++xx)
                if(!insideErase(xx,yy)||inOldLine(xx,yy)){usable=false;break;}
            if(usable){patchX=x;patchY=y;break;}
        }
    Assert::IsTrue(patchX>=0&&patchY>=0,L"Fixture needs a black non-glyph area inside the remaining applied cleanup.");
    const auto drawH=[&](std::vector<uint16_t>& pixels,bool alternate) {
        auto* yplane=pixels.data();const size_t area=size_t(kFootprintWidth)*kFootprintHeight;
        for(int y=patchY;y<patchY+18;++y)for(int x=patchX;x<patchX+16;++x)
            if(x<patchX+3||x>=patchX+13||(y>=patchY+7&&y<patchY+10)) {
                yplane[size_t(y)*kFootprintWidth+x]=uint16_t((alternate?64:110)<<6);
                if(alternate)pixels[area+size_t(y/2)*kFootprintWidth+(x&~1)]=uint16_t(800<<6);
            }
    };
    for(bool alternate:{false,true}) {
        auto changedPixels=CurrentFootprintUhdPixels(1);preservePreparedBackground(changedPixels);drawH(changedPixels,alternate);
        auto changedSource=CurrentFootprintUhdSource(changedPixels);
        const auto proof=footprint->VerifyCurrentFootprint(source93->current,changedSource,
            preview->current,accepted,base);
        Assert::IsFalse(proof.valid,L"Luma110 or U800 new text inside erased pixels cannot be reused as old backing.");
        Assert::AreEqual("cadence-current-cleanup-changed",proof.reason);
    }

    // A U=800, luma=64 caption outside the exact mutation footprint is allowed
    // by that pure footprint witness, but must be caught by the worker's full
    // top/bottom search-band sample comparison (luma alone calls this black).
    auto barPixels=CurrentFootprintUhdPixels(1);preservePreparedBackground(barPixels);AddU800BarH(barPixels,600,kFootprintBottom+100);
    auto barSource=CurrentFootprintUhdSource(barPixels);
    Assert::IsFalse(footprint->ContainsFootprintSample(600,kFootprintBottom+100));
    Assert::IsTrue(footprint->VerifyCurrentFootprint(source93->current,barSource,
        preview->current,accepted,base).valid,
        L"The changed bar caption is outside the exact erase footprint; the separate full-bar worker check owns this decision.");
    auto barWorker=std::make_unique<SubtitlePendingMeasurementGuard>();
    Assert::IsTrue(barWorker->RememberAccepted(*preview,accepted,seedSource,1000.0/60,250,
        nullptr,nullptr,&base,footprint.get()));
    auto barPending=CurrentFootprintPendingPreview(*preview,93,55);
    Assert::IsFalse(barWorker->Resolve(*barPending,barSource,1000.0/60,250,
        &preview->current.identity,true,footprint.get()));
    Assert::AreEqual("new-current-bar-ink",barWorker->DecisionReason());
}




static constexpr int UhdWidth = 3840;
static constexpr int UhdHeight = 2160;
static constexpr uint64_t UhdPolicy = 11;
static constexpr uint64_t UhdContinuity = 21;
static constexpr uint64_t UhdDomain = 0xA11CE;

static ActivePictureFrameIdentity UhdIdentity(uint64_t sequence, AnalysisLumaFormat format) {
    ActivePictureFrameIdentity id;
    id.transportGeneration=1; id.acceptedSequence=sequence; id.sourceFrameNumber=sequence;
    id.captureTimestamp=sequence*1000; id.sourceFormatGeneration=100+static_cast<uint64_t>(format);
    id.viewportGeneration=4; id.rendererGeneration=5; id.analysisDomainKey=UhdDomain;
    return id;
}
static std::vector<uint16_t> UhdCaption(AnalysisLumaFormat format) {
    const size_t raster=size_t(UhdWidth)*UhdHeight;
    const size_t planes=format==AnalysisLumaFormat::P010?3:4;
    std::vector<uint16_t> p(raster*planes/2,uint16_t(512<<6));
    auto fill=[&](int l,int t,int r,int b,int value) {
        for(int y=t;y<b;++y) for(int x=l;x<r;++x) p[size_t(y)*UhdWidth+x]=uint16_t(value<<6);
    };
    fill(0,0,UhdWidth,UhdHeight,64); fill(0,276,UhdWidth,1884,200);
    fill(1250,1790,2580,1880,64);
    const int top=1808;
    for(int n=0;n<30;++n) {
        const int left=1320+n*40;
        fill(left,top,left+28,top+64,700); fill(left+8,top+8,left+20,top+56,64);
    }
    const int open=1270,close=2534;
    fill(open,top,open+3,top+64,700); fill(open,top,open+24,top+3,700);
    fill(open,top+61,open+24,top+64,700); fill(close+21,top,close+24,top+64,700);
    fill(close,top,close+24,top+3,700); fill(close,top+61,close+24,top+64,700);
    return p;
}
static AnalysisLumaSource UhdSource(std::vector<uint16_t>& pixels,AnalysisLumaFormat format) {
    AnalysisLumaSource s; s.data=reinterpret_cast<const uint8_t*>(pixels.data());
    s.dataBytes=pixels.size()*sizeof(uint16_t); s.width=UhdWidth; s.height=UhdHeight;
    s.rowBytes=s.chromaRowBytes=size_t(UhdWidth)*2; s.format=format; s.generation=1; return s;
}
static SubtitlePictureAuthority UhdPicture(const ActivePictureFrameIdentity& id) {
    SubtitlePictureAuthority p; p.required=true; p.identity=id;
    p.bounds={0,276,UhdWidth,1884,UhdWidth,UhdHeight}; return p;
}
static SubtitleBoxObservation MeasureUhd(SubtitleBoxDetector& detector,
    const AnalysisLumaSource& source,const ActivePictureFrameIdentity& id) {
    const auto raw=ExtractActivePictureEvidence(source);
    return SubtitleBoxLookahead::Measure(detector,source,raw,id,false,{},UhdPolicy,
        UhdContinuity,UhdPicture(id));
}
static std::unique_ptr<SubtitleBoxPreview> UhdPending(const SubtitleBoxPreview& ready,
    const ActivePictureFrameIdentity& id) {
    auto p=std::make_unique<SubtitleBoxPreview>();
    p->available=false; p->policyGeneration=UhdPolicy; p->continuityGeneration=ready.continuityGeneration;
    p->current=ready.current; p->current.identity=id; p->current.sharedPicture.identity=id;
    p->current.continuityGeneration=ready.continuityGeneration;
    p->current.analyzed=false; p->current.text={}; p->current.ink.reset(); p->text={}; return p;
}
static double Pct(std::vector<double> v,double p) {
    std::sort(v.begin(),v.end());
    return v.at(static_cast<size_t>(std::ceil(p*(v.size()-1))));
}

TEST_METHOD(UhdPendingCueVerifierReportsCoverageAndLatencyAcrossNativeFormats) {
    constexpr double frameMs=1000.0/23.976;
    for(const auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,
            AnalysisLumaFormat::DecodedDolbyVision422}) {
        auto pixels=UhdCaption(format); auto source=UhdSource(pixels,format);
        Assert::IsTrue(source.IsValid());
        std::vector<double> analyzeMs,seedMs,pendingMs;
        size_t minBand=std::numeric_limits<size_t>::max(),maxBand=0,maxBacking=0,maxGlyph=0,maxSeedBand=0;
        uint64_t next=1;
        // Six exact detector acquisitions x twenty UNKNOWN frames = 120
        // pending frames/format. Each 20-frame lease is about 834ms.
        for(int cycle=0;cycle<6;++cycle) {
            SubtitleBoxDetector detector; detector.SetNearBarDistance(20);
            auto scans=std::make_unique<std::array<SubtitleBoxObservation,4>>();
            for(size_t i=0;i<scans->size();++i) {
                const auto id=UhdIdentity(next+i,format);
                const auto start=std::chrono::steady_clock::now();
                (*scans)[i]=MeasureUhd(detector,source,id);
                analyzeMs.push_back(std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count());
                Assert::IsTrue((*scans)[i].text.detected && (*scans)[i].ink &&
                    (*scans)[i].ink->rawEvidenceComplete && (*scans)[i].independentBarAnalysis,
                    L"Seed must be a complete actual UHD detector pass.");
            }
            auto ready=std::make_unique<SubtitleBoxPreview>();
            SubtitleBoxLookahead::ResolveInto(*ready,scans->data(),scans->size(),UhdPolicy,UhdContinuity);
            Assert::IsTrue(ready->available && ready->current.text.detected);
            auto tracker=std::make_unique<SubtitleBoxPresentation>();
            const auto accepted=tracker->Consume(*ready,1000,frameMs);
            Assert::IsTrue(accepted.detected && !accepted.held && !accepted.confirmationPending);
            auto guard=std::make_unique<SubtitlePendingMeasurementGuard>();
            const auto start=std::chrono::steady_clock::now();
            const bool seeded=guard->RememberAccepted(*ready,accepted,source,frameMs,1000,
                &tracker->AcceptedCueSeedCertificate());
            seedMs.push_back(std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count());
            if(!seeded) Logger::WriteMessage((std::string("UHD seed failure: ")+guard->DecisionReason()).c_str());
            Assert::IsTrue(seeded,L"The actual detector observation must seed the current verifier.");
            maxSeedBand=(std::max)(maxSeedBand,guard->DecisionDiagnostics().seedBandSamplesChecked);
            Assert::IsTrue(guard->DecisionDiagnostics().seedBandSamplesChecked>0);

            for(uint64_t offset=4;offset<24;++offset) {
                const uint16_t shifted=uint16_t((offset&1)?220:200)<<6;
                for(int y=1600;y<1700;++y) for(int x=0;x<UhdWidth;++x) pixels[size_t(y)*UhdWidth+x]=shifted;
                const auto id=UhdIdentity(next+offset,format);
                auto pending=UhdPending(*ready,id);
                const auto t=std::chrono::steady_clock::now();
                const bool resolved=guard->Resolve(*pending,source,frameMs,1000);
                pendingMs.push_back(std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-t).count());
                const auto& d=guard->DecisionDiagnostics();
                minBand=(std::min)(minBand,d.currentBandSamplesChecked);
                maxBand=(std::max)(maxBand,d.currentBandSamplesChecked);
                maxBacking=(std::max)(maxBacking,d.backingSamplesChecked);
                maxGlyph=(std::max)(maxGlyph,d.glyphSamplesChecked);
                if(!resolved) Logger::WriteMessage((std::string("UHD pending failure: ")+guard->DecisionReason()+
                    " mismatch="+std::to_string(d.glyphMismatchX)+","+std::to_string(d.glyphMismatchY)).c_str());
                Assert::IsTrue(resolved,L"Current source must pass the exact static-cue verifier.");
                Assert::IsTrue(pending->available && pending->current.pendingRefresh);
                Assert::IsTrue(pending->current.ink && pending->current.ink!=ready->current.ink);
                Assert::IsFalse(pending->current.ink->rawEvidenceComplete);
                Assert::IsFalse(pending->current.subtitleArProof.accepted);
                Assert::IsTrue(d.currentBandSamplesChecked>0 && d.glyphSamplesChecked>0);
            }
            next+=24;
        }

        // This deliberately records the conservative texture false-positive
        // risk outside the accepted subtitle/card without asking to relax it.
        auto texture=UhdCaption(format); auto textureSource=UhdSource(texture,format);
        SubtitleBoxDetector detector; detector.SetNearBarDistance(20);
        auto scans=std::make_unique<std::array<SubtitleBoxObservation,4>>();
        for(size_t i=0;i<scans->size();++i)
            (*scans)[i]=MeasureUhd(detector,textureSource,UhdIdentity(next+i,format));
        auto ready=std::make_unique<SubtitleBoxPreview>();
            SubtitleBoxLookahead::ResolveInto(*ready,scans->data(),scans->size(),UhdPolicy,UhdContinuity);
        auto tracker=std::make_unique<SubtitleBoxPresentation>(); const auto accepted=tracker->Consume(*ready,1000,frameMs);
        auto guard=std::make_unique<SubtitlePendingMeasurementGuard>();
        Assert::IsTrue(guard->RememberAccepted(*ready,accepted,textureSource,frameMs,1000,
            &tracker->AcceptedCueSeedCertificate()));
        auto unchanged=UhdPending(*ready,UhdIdentity(next+4,format));
        Assert::IsTrue(guard->Resolve(*unchanged,textureSource,frameMs,1000),
            L"An unchanged existing card edge must pass before testing newly added texture.");
        for(int y=1600;y<1632;y+=4) for(int x=800;x<832;x+=4)
            if(((x+y)/4)&1) for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx)
                texture[size_t(y+dy)*UhdWidth+x+dx]=uint16_t(350<<6);
        auto pending=UhdPending(*ready,UhdIdentity(next+5,format));
        const bool texturedAccepted=guard->Resolve(*pending,textureSource,frameMs,1000);
        const auto d=guard->DecisionDiagnostics();
        Logger::WriteMessage((std::string("UHD texture format=")+
            std::to_string(static_cast<int>(format))+" accepted="+
            std::to_string(texturedAccepted)+" reason="+guard->DecisionReason()+
            " band="+std::to_string(d.currentBandSamplesChecked)+
            " backing="+std::to_string(d.backingSamplesChecked)+
            " glyph="+std::to_string(d.glyphSamplesChecked)+
            " mismatch="+std::to_string(d.glyphMismatchX)+","+std::to_string(d.glyphMismatchY)+"\n").c_str());
        Assert::IsFalse(texturedAccepted,
            L"Novel picture-side contrast outside the cue remains a fail-closed veto.");

        const auto stat=[](const std::vector<double>& v) {
            return std::string("p50=")+std::to_string(Pct(v,.50))+
                " p95="+std::to_string(Pct(v,.95))+
                " max="+std::to_string(*std::max_element(v.begin(),v.end()));
        };
        Logger::WriteMessage((std::string("UHD verifier format=")+
            std::to_string(static_cast<int>(format))+" pending=120 frame_ms="+
            std::to_string(frameMs)+" analyze_ms["+stat(analyzeMs)+"] seed_ms["+
            stat(seedMs)+"] pending_ms["+stat(pendingMs)+"] band_samples[min="+
            std::to_string(minBand)+" max="+std::to_string(maxBand)+
            "] seed_band_max="+std::to_string(maxSeedBand)+" backing_max="+std::to_string(maxBacking)+" glyph_max="+
            std::to_string(maxGlyph)+" max_samples_per_sec="+
            std::to_string(double(maxBand)*1000.0/frameMs)+"\n").c_str());
    }
}









    TEST_METHOD(UhdFreshReadyRearmsWithBoundedCurrentPixelChecks) {
        constexpr auto format=AnalysisLumaFormat::P010;
        constexpr double frameMs=1000.0/23.976;
        auto pixels=UhdCaption(format);auto source=UhdSource(pixels,format);
        SubtitleBoxDetector detector;detector.SetNearBarDistance(20);
        auto scans=std::make_unique<std::array<SubtitleBoxObservation,4>>();
        for(size_t i=0;i<scans->size();++i) {
            (*scans)[i]=MeasureUhd(detector,source,UhdIdentity(i+1,format));
            Assert::IsTrue((*scans)[i].analyzed && (*scans)[i].text.detected &&
                (*scans)[i].ink && (*scans)[i].ink->rawEvidenceComplete,
                L"Initial UHD seed must come from complete source scans.");
        }
        auto seed=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*seed,scans->data(),scans->size(),UhdPolicy,UhdContinuity);
        SubtitleBoxPresentation tracker;const auto acquired=tracker.Consume(*seed,1000,frameMs);
        Assert::IsTrue(acquired.detected && !acquired.held && !acquired.confirmationPending);
        Assert::AreNotEqual(uint64_t(0),acquired.cue);
        auto guard=std::make_unique<SubtitlePendingMeasurementGuard>();
        Assert::IsTrue(guard->RememberAccepted(*seed,acquired,source,frameMs,1000),
            L"The first independent UHD positive scan establishes the immutable reference.");
        const auto original=guard->AcceptedSeedIdentity();

        // The actual renderer supplies a complete four-source lookahead window
        // for a fresh ready result. This full positive may adopt the current
        // caption-continuity wrapper while keeping the original pixel baseline.
        auto currentScans=std::make_unique<std::array<SubtitleBoxObservation,4>>();
        for(size_t i=0;i<currentScans->size();++i) {
            (*currentScans)[i]=MeasureUhd(detector,source,UhdIdentity(i+2,format));
            (*currentScans)[i].continuityGeneration=UhdContinuity+1;
            Assert::IsTrue((*currentScans)[i].analyzed && (*currentScans)[i].text.detected &&
                (*currentScans)[i].ink && (*currentScans)[i].ink->rawEvidenceComplete);
        }
        auto current=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*current,currentScans->data(),currentScans->size(),UhdPolicy,UhdContinuity+1);
        const auto currentAccepted=tracker.Consume(*current,1000,frameMs,&seed->current.identity,true);
        const auto& certificate=tracker.AcceptedCueSeedCertificate();
        const bool certificatePresent=certificate.Matches(*current,currentAccepted);
        Logger::WriteMessage((std::string("UHD ready tracker reason=")+tracker.DecisionReason()+
            " certificate_present="+std::to_string(certificatePresent)+
            " accepted="+std::to_string(currentAccepted.detected)+
            " held="+std::to_string(currentAccepted.held)+
            " pending="+std::to_string(currentAccepted.confirmationPending)+
            " lines="+std::to_string(currentAccepted.lineCount)+
            " currentLinesConfirmed="+std::to_string(current->currentLinesConfirmed)+
            " future="+std::to_string(current->followingCount)).c_str());
        Assert::IsTrue(currentAccepted.detected && !currentAccepted.held && !currentAccepted.confirmationPending);
        Assert::AreEqual(1,currentAccepted.lineCount,
            L"This UHD fixture is one accepted line, so ordinary full-positive coverage must suffice.");
        const auto started=std::chrono::steady_clock::now();
        // Exercise the ordinary full-positive path even if the tracker also
        // happens to expose a certificate for this current source.
        const bool rearmed=guard->RememberAccepted(*current,currentAccepted,source,frameMs,1000,nullptr);
        const double elapsed=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        Logger::WriteMessage((std::string("UHD ready rearm ms=")+std::to_string(elapsed)+
            " source="+std::to_string(current->current.identity.acceptedSequence)+
            " raw_cue="+std::to_string(current->current.text.cue)+
            " accepted_cue="+std::to_string(currentAccepted.cue)+
            " certificate_present="+std::to_string(certificatePresent)+
            " mask_words="+std::to_string(guard->DecisionDiagnostics().readyRearmMaskWordsChecked)+
            " glyph_samples="+std::to_string(guard->DecisionDiagnostics().readyRearmGlyphSamplesChecked)+
            " backing_samples="+std::to_string(guard->DecisionDiagnostics().readyRearmBackingSamplesChecked)+
            " known_samples="+std::to_string(guard->DecisionDiagnostics().readyRearmKnownSamplesChecked)).c_str());
        Assert::IsTrue(rearmed);
        Assert::AreEqual("accepted-same-cue-current-scan-rearmed",guard->DecisionReason());
        Assert::AreEqual(uint64_t(0),guard->DecisionDiagnostics().seedBandSamplesChecked,
            L"Ready rearm must not reread the entire UHD search bands.");
        Assert::IsTrue(guard->DecisionDiagnostics().readyRearmGlyphSamplesChecked>0);
        Assert::IsTrue(guard->DecisionDiagnostics().readyRearmBackingSamplesChecked>0);
        Assert::IsTrue(guard->DecisionDiagnostics().readyRearmKnownSamplesChecked>0);
        Assert::AreEqual(original.acceptedSequence,guard->AcceptedSeedIdentity().acceptedSequence,
            L"The original UHD pixel reference remains immutable.");
        Assert::AreEqual(UhdContinuity+1,guard->ReferenceContinuity(),
            L"Only the current source wrapper adopts the fresh continuity generation.");
        Assert::IsFalse(guard->RememberAccepted(*current,currentAccepted,source,frameMs,1000,nullptr),
            L"Replaying the same full-positive source cannot renew the pending lease twice.");
        Assert::AreEqual("accepted-current-positive-already-consumed",guard->DecisionReason());
        Assert::AreEqual(current->current.identity.acceptedSequence,guard->LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(0.0,guard->PendingElapsedMilliseconds(),0.001);

        auto currentPending=UhdPending(*current,
            UhdIdentity(current->current.identity.acceptedSequence+1,format));
        Assert::IsTrue(guard->Resolve(*currentPending,source,frameMs,1000),
            L"A pending source in the freshly accepted continuity wrapper still verifies against the original pixels.");
        Assert::AreEqual(original.acceptedSequence,guard->AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(UhdContinuity+1,guard->ReferenceContinuity());
        Assert::AreEqual(frameMs,guard->PendingElapsedMilliseconds(),0.001);
        auto foreignPending=UhdPending(*current,
            UhdIdentity(currentPending->current.identity.acceptedSequence+1,format));
        foreignPending->continuityGeneration=UhdContinuity+2;
        foreignPending->current.continuityGeneration=UhdContinuity+2;
        Assert::IsFalse(guard->Resolve(*foreignPending,source,frameMs,1000),
            L"An unproven pending continuity change cannot use the fresh-ready rearm exception.");
        Assert::AreEqual("pending-context-changed",guard->DecisionReason());
        Assert::AreEqual(uint64_t(0),guard->AcceptedSeedIdentity().acceptedSequence,
            L"An unproven context change withdraws the seed rather than retaining an old template.");
        Assert::AreEqual(uint64_t(0),guard->ReferenceContinuity());
        Assert::AreEqual(uint64_t(0),guard->LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(0.0,guard->PendingElapsedMilliseconds(),0.001);
    }

    TEST_METHOD(ReadyCertifiedExistingCueRearmsAcrossContinuityWithOriginalPixelSeed) {
        constexpr double frameMs=1000.0/60;
        auto pixels=PixelsWithCurrentBars();auto source=Source(pixels);
        SubtitleBoxPresentation tracker;
        auto scans=std::make_unique<std::array<SubtitleBoxObservation,4>>();
        for(size_t i=0;i<scans->size();++i)
            (*scans)[i]=CompleteThreeLineScan(i+1,pixels,21);
        auto seed=std::make_unique<SubtitleBoxPreview>();
        SubtitleBoxLookahead::ResolveInto(*seed,scans->data(),scans->size(),11,21);
        const auto acquired=tracker.Consume(*seed,250,frameMs);
        Assert::IsTrue(acquired.detected && acquired.lineCount==3 && !acquired.held);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsTrue(guard.RememberAccepted(*seed,acquired,source,frameMs,250));
        const auto originalSeed=guard.AcceptedSeedIdentity();

        auto currentRaw=CompleteThreeLineScan(5,pixels,22);
        auto current=SubtitleBoxLookahead::Resolve(&currentRaw,1,11,22);
        Assert::IsTrue(current.text.lineCount<3,
            L"The short-horizon preview intentionally prunes the three-line cue.");
        const auto accepted=tracker.Consume(current,250,frameMs,&(*scans)[0].identity,true);
        const auto& certificate=tracker.AcceptedCueSeedCertificate();
        Assert::IsTrue(accepted.detected && accepted.lineCount==3 && !accepted.held && !accepted.confirmationPending);
        Assert::IsTrue(certificate.Matches(current,accepted),
            L"A fresh exact-current certificate may retain the tracker’s accepted cue through grouping pruning.");
        const bool remembered=guard.RememberAccepted(current,accepted,source,frameMs,250,&certificate);
        if(!remembered)Logger::WriteMessage((std::string("certified continuity rearm: ")+guard.DecisionReason()).c_str());
        Assert::IsTrue(remembered);
        Assert::AreEqual("accepted-same-cue-current-scan-rearmed",guard.DecisionReason());
        Assert::AreEqual(uint64_t(0),guard.DecisionDiagnostics().seedBandSamplesChecked,
            L"Certified reuse still uses bounded current-pixel checks, not a repeated full-band seed.");
        Assert::AreEqual(originalSeed.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence,
            L"Continuity rebase must not replace or relabel the immutable original pixel seed.");
        Assert::AreEqual(uint64_t(22),guard.ReferenceContinuity());
        Assert::AreEqual(current.current.identity.acceptedSequence,guard.LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(0.0,guard.PendingElapsedMilliseconds(),0.001);

        const auto repeatedAccepted=tracker.Consume(current,250,frameMs);
        const auto& repeatedCertificate=tracker.AcceptedCueSeedCertificate();
        Assert::IsTrue(repeatedAccepted.detected && !repeatedAccepted.held);
        Assert::IsFalse(repeatedCertificate.Matches(current,repeatedAccepted),
            L"Repeated tracker output does not export a new acquisition certificate.");
        Assert::IsFalse(guard.RememberAccepted(current,repeatedAccepted,source,frameMs,250,&repeatedCertificate),
            L"The exact consumed certificate cannot rearm the pruned current cue twice.");
        Assert::AreEqual("accepted-certificate-already-consumed",guard.DecisionReason());
        Assert::AreEqual(originalSeed.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(current.current.identity.acceptedSequence,guard.LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(0.0,guard.PendingElapsedMilliseconds(),0.001);

        auto pending=PendingAfterReassociation(6);
        Assert::IsTrue(guard.Resolve(pending,source,frameMs,250),
            L"The adopted current wrapper accepts a same-context pending source without renewing the original seed.");
        Assert::AreEqual(originalSeed.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(frameMs,guard.PendingElapsedMilliseconds(),0.001);
    }
    TEST_METHOD(SubtitleCompletionSignalFollowsReadyCachePublication) {
        SubtitleMeasurementWorker worker;
        auto signal=std::make_shared<AnalysisCompletionSignal>();
        auto key=Ready(1).current;
        const auto revision=signal->Snapshot();
        Assert::IsTrue(worker.Submit(key,[key](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            return key;
        },nullptr,0,false,signal));
        Assert::IsTrue(signal->WaitUntil(revision,std::chrono::steady_clock::now()+std::chrono::seconds(5)));
        auto result=std::make_unique<SubtitleBoxObservation>();
        Assert::IsTrue(worker.TryRead(key,*result));
        Assert::IsTrue(result->analyzed);
        Assert::IsTrue(SameAnalysisFrameIdentity(key.identity,result->identity));
    }
    TEST_METHOD(ParallelSamplersShareOneDiagnosticCaptureClaimForEveryCaptureMode) {
        struct RestoreClaim {
            bool original=SubtitleDiagnosticCaptureClaimed().exchange(false);
            ~RestoreClaim(){SubtitleDiagnosticCaptureClaimed().store(original);}
        } restore;
        for(const bool proofFailureMode:{false,true}) {
            SubtitleDiagnosticCaptureClaimed().store(false);
            std::atomic_uint entered{0},claimed{0};std::atomic_bool start{false};
            const auto attempt=[&] {
                ++entered;
                while(!start.load())std::this_thread::yield();
                if(TryClaimSubtitleDiagnosticCapture(proofFailureMode))++claimed;
            };
            std::thread first(attempt),second(attempt);
            while(entered.load()!=2)std::this_thread::yield();
            start.store(true);first.join();second.join();
            Assert::AreEqual(1u,claimed.load(),L"Parallel samplers must share a single diagnostic file claim in every capture mode");
            Assert::IsFalse(TryClaimSubtitleDiagnosticCapture(proofFailureMode),L"A later sampler cannot overwrite the claimed capture");
        }
    }
    TEST_METHOD(HighInputCadenceRunsTwoIndependentSamplersAndDeduplicatesBothActiveKeys) {
        std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
        worker.SetInputRateHz(59.94);
        std::atomic_uint entered{0},active{0},peak{0},calls{0};std::mutex ownersMutex;
        std::vector<const SubtitleMeasurementSampler*> owners;
        const auto work=[&](SubtitleBoxObservation key) {
            return [&,key](SubtitleMeasurementSampler& sampler,const SubtitleBarTrackingReference&) {
                ++calls;const unsigned now=++active;auto prior=peak.load();
                while(prior<now && !peak.compare_exchange_weak(prior,now)){}
                {std::lock_guard<std::mutex> lock(ownersMutex);owners.push_back(&sampler);}
                ++entered;
                while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
                --active;auto result=key;result.analyzed=true;result.text.cue=key.identity.acceptedSequence;return result;
            };
        };
        const auto first=WorkerKey(1,0xA11CE),second=WorkerKey(2,0xA11CE),third=WorkerKey(3,0xA11CE);
        worker.Submit(first,work(first));worker.Submit(second,work(second));worker.Submit(third,work(third));
        const bool twoActive=WaitForEntered(entered,2);
        const unsigned beforeRelease=entered.load();
        worker.Submit(first,work(first));worker.Submit(second,work(second));
        release.store(true,std::memory_order_release);
        SubtitleBoxObservation a,b,c;
        const bool allReady=WaitForTake(worker,first,a)&&WaitForTake(worker,second,b)&&WaitForTake(worker,third,c);
        worker.Stop();
        Assert::IsTrue(twoActive,L"59.94 Hz must run two complete independent source scans before either blocked job is released.");
        Assert::AreEqual(2u,beforeRelease,L"The source worker pool must remain bounded to two active jobs.");
        Assert::AreEqual(2u,peak.load());Assert::AreEqual(3u,calls.load());Assert::IsTrue(allReady);
        Assert::IsTrue(owners.size()==3 && owners[0]!=owners[1],L"Concurrent source scans must own different stateful samplers.");
        Assert::AreEqual(uint64_t(1),a.text.cue);Assert::AreEqual(uint64_t(2),b.text.cue);Assert::AreEqual(uint64_t(3),c.text.cue);
        Assert::IsTrue(worker.TakeDiagnostics().deduplicated>=2);
    }
    TEST_METHOD(CurrentInspectedGlyphScanCanVerifyOnlyTheIndependentSpatialNominee) {
        constexpr int width=3840,height=2160,top=264,bottom=1896;
        std::vector<uint16_t> pixels(size_t(width)*height*3/2,uint16_t(512<<6));
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            pixels[size_t(y)*width+x]=uint16_t((y>=top&&y<bottom?300:64)<<6);
        const auto fill=[&](int l,int t,int r,int b,int code) {
            for(int y=t;y<b;++y)for(int x=l;x<r;++x)pixels[size_t(y)*width+x]=uint16_t(code<<6);
        };
        // Same native two-line crossing-card fixture as the independently
        // qualified supplied-candidate mask controls; no copied detector result.
        fill(1190,bottom-146,2650,bottom+16,64);
        for(int y:{bottom-134,bottom-52})for(int glyph=0;glyph<26;++glyph) {
            const int x=1210+glyph*52;
            fill(x-2,y-2,x+36,y+58,64);fill(x,y,x+34,y+56,800);fill(x+8,y+10,x+26,y+46,64);
        }
        AnalysisLumaSource source;
        source.data=reinterpret_cast<const uint8_t*>(pixels.data());source.dataBytes=pixels.size()*2;
        source.width=width;source.height=height;source.rowBytes=source.chromaRowBytes=width*2;
        source.format=AnalysisLumaFormat::P010;source.range=AnalysisLumaRange::Limited;source.generation=2;
        ActivePictureBounds spatial{0,top,width,bottom,width,height,double(width)/(bottom-top),ActivePictureBounds::BarAxes::TOP_BOTTOM};
        auto native=VerifySuppliedActivePictureCandidate(source,spatial);
        native.detector=ActivePictureDetectorKind::SourceSpatialMeasured;
        native.physicalEdges.evaluated=native.physicalEdges.estimateAvailable=true;
        native.physicalEdges.estimate=spatial;native.physicalEdges.sourceGeneration=2;
        SubtitleBoxObservation key;key.identity=Identity(10);key.identity.analysisDomainKey=0xA11CE;
        key.width=width;key.height=height;key.policyGeneration=11;key.continuityGeneration=21;
        key.nearBarDistance=20;key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;
        auto& hint=key.sharedPicture.inspectionCandidate;
        hint.valid=true;hint.bounds=spatial;hint.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        hint.originIdentity=Identity(8);hint.originIdentity.analysisDomainKey=key.identity.analysisDomainKey;
        hint.policyGeneration=11;hint.continuityGeneration=21;hint.nativeGeometryRevision=1;
        SubtitleMeasurementSampler sampler;
        const auto measured=sampler.Measure(source,key,{},23.976,native);
        Assert::IsTrue(measured.analyzed&&measured.inspectionCandidateUsed&&measured.text.detected&&
            measured.ink&&measured.ink->rawEvidenceComplete&&measured.ink->paletteValid,
            L"The native source must produce a fresh complete owned glyph mask using the valid contextual inspection hint");
        Assert::AreEqual(2,measured.text.lineCount);
        auto proofSource=source;proofSource.generation=key.identity.transportGeneration;
        const auto direct=VerifySuppliedActivePictureCandidateWithSubtitle(proofSource,native,measured,
            key.identity,key.policyGeneration,key.continuityGeneration,key.scriptHint);
        Assert::IsTrue(direct.accepted,
            L"Current inspected glyph ownership must verify the independent spatial nominee in either test process mode");
        Assert::AreEqual(top,direct.candidate.top);Assert::AreEqual(bottom,direct.candidate.bottom);
        Assert::AreEqual(int(ActivePictureDetectorKind::SourceSpatialMeasured),int(direct.evidence.detector));
        Assert::IsTrue(direct.IsCurrentFor(proofSource,key.identity,11,21,key.scriptHint));
        if(SourcePictureDetectorEnabled()) {
            Assert::IsTrue(measured.subtitleArProof.accepted,
                L"Explicit active-mode integration must consume inspected current glyphs through its normal sampler gate");
            Assert::IsTrue(measured.subtitleArProof.IsCurrentFor(proofSource,key.identity,11,21,key.scriptHint));
            Logger::WriteMessage("Active source detector sampler gate exercised; independent supplied mask proof accepted.");
        } else {
            Assert::IsFalse(measured.subtitleArProof.accepted,
                L"Legacy mode must retain its original prohibition on hint-derived AR proof");
            Logger::WriteMessage("Legacy sampler gate preserved; independent supplied mask verifier exercised directly. Active sampler coverage requires explicit env1 run.");
        }
    }

    TEST_METHOD(CompletedInspectionScanSurvivesIdenticalCanonicalPlaneWithoutRescan) {
        auto pixels=PixelsWithCurrentBars();
        auto completed=CompleteThreeLineScan(10,pixels);
        completed.identity.analysisDomainKey=0xA11CE;
        completed.sharedPicture.identity=completed.identity;
        completed.text.topSearchComplete=completed.text.bottomSearchComplete=true;
        completed.text.anchor=completed.text.lineBounds[2];
        auto canonical=completed;
        canonical.analyzed=false;canonical.text={};canonical.ink.reset();
        auto inspected=canonical;
        inspected.sharedPicture.bounds={};
        auto& nominee=inspected.sharedPicture.inspectionCandidate;
        nominee.valid=true;nominee.bounds=canonical.sharedPicture.bounds;
        nominee.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        nominee.originIdentity=inspected.identity;nominee.originIdentity.acceptedSequence=8;
        nominee.originIdentity.sourceFrameNumber=8;nominee.originIdentity.captureTimestamp=8000;
        nominee.policyGeneration=inspected.policyGeneration;
        nominee.continuityGeneration=inspected.continuityGeneration;
        nominee.nativeGeometryRevision=1;
        inspected.pictureAuthorityUnavailableForMeasurement=true;
        completed.sharedPicture=inspected.sharedPicture;
        completed.pictureAuthorityUnavailableForMeasurement=true;
        completed.inspectionCandidateUsed=true;
        completed.independentBarAnalysis=false;
        const auto originalInk=completed.ink;
        SubtitleMeasurementWorker worker;worker.SetInputRateHz(23.976);
        worker.Submit(inspected,[completed](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {return completed;});
        SubtitleBoxObservation original;
        Assert::IsTrue(WaitForRead(worker,inspected,original));
        Assert::AreEqual(uint64_t(1),worker.TakeDiagnostics().submitted);
        SubtitleBoxObservation rebound;
        Assert::IsTrue(worker.TryRead(canonical,rebound),
            L"Completed exact-source glyph work against the identical plane must survive inspection-to-canonical publication without a new job");
        Assert::IsTrue(rebound.ink==originalInk);
        Assert::IsTrue(rebound.inspectedPlaneWork!=nullptr);
        Assert::IsTrue(SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(rebound));
        Assert::IsTrue(rebound.sharedPicture.Matches(canonical.sharedPicture));
        Assert::IsTrue(rebound.inspectionCandidateUsed,L"Original nominated-scan provenance must remain explicit");
        Assert::IsFalse(rebound.independentBarAnalysis,L"Canonical geometry cannot manufacture independent native bar proof");
        Assert::IsFalse(rebound.subtitleArProof.accepted);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(rebound,canonical.identity,
            canonical.width,canonical.height,canonical.policyGeneration,canonical.continuityGeneration,false,{},canonical.scriptHint));
        Assert::AreEqual(uint64_t(0),worker.TakeDiagnostics().submitted,
            L"Metadata publication alone cannot resubmit an already completed scan");
        Assert::IsFalse(SubtitleMeasurementWorker::CompleteIndependentScanForKey(rebound,canonical),
            L"Work equivalence must never become independent bar/crop authority");
        SubtitleBoxObservation originalAgain;
        Assert::IsTrue(worker.TryRead(inspected,originalAgain));
        Assert::IsTrue(originalAgain.sharedPicture.Matches(inspected.sharedPicture));
        Assert::IsTrue(originalAgain.inspectedPlaneWork==nullptr,L"READY cache retains its original interpretation");
        auto foreign=rebound;++foreign.identity.acceptedSequence;
        Assert::IsFalse(SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(foreign),
            L"An immutable work stamp cannot be copied to another source");
        for(int fault=0;fault<8;++fault) {
            auto changed=rebound;
            switch(fault) {
            case 0:++changed.sharedPicture.identity.transportGeneration;break;
            case 1:++changed.sharedPicture.identity.sourceFormatGeneration;break;
            case 2:++changed.sharedPicture.identity.analysisDomainKey;break;
            case 3:++changed.sharedPicture.identity.viewportGeneration;break;
            case 4:++changed.sharedPicture.identity.rendererGeneration;break;
            case 5:++changed.sharedPicture.bounds.top;break;
            case 6:++changed.policyGeneration;break;
            case 7:++changed.continuityGeneration;break;
            }
            Assert::IsFalse(SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(changed),
                L"A copied stamp cannot authorize stale or altered canonical source metadata");
        }
        worker.Stop();
    }

    TEST_METHOD(InspectedPlaneWorkRejectsChangedSourceSettingsGeometryAndIncompleteInk) {
        auto pixels=PixelsWithCurrentBars();auto completed=CompleteThreeLineScan(10,pixels);
        completed.identity.analysisDomainKey=0xA11CE;completed.sharedPicture.identity=completed.identity;
        completed.text.topSearchComplete=completed.text.bottomSearchComplete=true;
        completed.text.anchor=completed.text.lineBounds[2];
        auto canonical=completed;canonical.analyzed=false;canonical.text={};canonical.ink.reset();
        auto inspected=canonical;inspected.sharedPicture.bounds={};
        auto& nominee=inspected.sharedPicture.inspectionCandidate;
        nominee.valid=true;nominee.bounds=canonical.sharedPicture.bounds;
        nominee.bounds.trustedBarAxes=ActivePictureBounds::BarAxes::NONE;
        nominee.originIdentity=Identity(8);nominee.originIdentity.analysisDomainKey=0xA11CE;
        nominee.policyGeneration=11;nominee.continuityGeneration=21;nominee.nativeGeometryRevision=1;
        inspected.pictureAuthorityUnavailableForMeasurement=true;
        completed.sharedPicture=inspected.sharedPicture;completed.pictureAuthorityUnavailableForMeasurement=true;
        completed.inspectionCandidateUsed=true;completed.independentBarAnalysis=false;
        Assert::IsTrue(SubtitleBoxLookahead::HasCurrentInspectionGlyphMeasurement(completed));
        SubtitleMeasurementWorker worker;
        worker.Submit(inspected,[completed](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return completed;});
        SubtitleBoxObservation original;Assert::IsTrue(WaitForRead(worker,inspected,original));
        Assert::AreEqual(uint64_t(1),worker.TakeDiagnostics().submitted);
        for(int fault=0;fault<19;++fault) {
            auto changed=canonical;
            switch(fault) {
            case 0:++changed.sharedPicture.bounds.top;break;
            case 1:++changed.identity.acceptedSequence;break;
            case 2:++changed.identity.sourceFrameNumber;break;
            case 3:++changed.identity.captureTimestamp;break;
            case 4:++changed.identity.transportGeneration;break;
            case 5:++changed.identity.sourceFormatGeneration;break;
            case 6:++changed.identity.analysisDomainKey;break;
            case 7:++changed.identity.viewportGeneration;break;
            case 8:++changed.identity.rendererGeneration;break;
            case 9:++changed.policyGeneration;break;
            case 10:++changed.continuityGeneration;break;
            case 11:++changed.nearBarDistance;break;
            case 12:++changed.optimizationMode;break;
            case 13:changed.scriptHint=SubtitleScriptHint::Arabic;break;
            case 14:changed.discontinuity=true;break;
            case 15:changed.analysisStride=2;break;
            case 16:++changed.width;break;
            case 17:++changed.height;break;
            case 18:changed.sharedPicture.inspectionCandidate.valid=true;break;
            }
            SubtitleBoxObservation rejected;
            Assert::IsFalse(worker.TryRead(changed,rejected),L"Different source/settings/geometry must not reuse inspected work");
            Assert::IsTrue(worker.TryRead(inspected,rejected),L"A failed read must preserve the original immutable READY entry");
        }
        Assert::AreEqual(uint64_t(0),worker.TakeDiagnostics().submitted);worker.Stop();
        for(int fault=0;fault<18;++fault) {
            auto bad=completed;
            switch(fault) {
            case 0:bad.text.workLimit=true;break;
            case 1:bad.text.held=true;break;
            case 2:bad.text.confirmationPending=true;break;
            case 3:bad.pendingRefresh=true;break;
            case 4:bad.analysisRefresh=true;break;
            case 5:bad.glyphLocalBottom=true;break;
            case 6:bad.hasBarTrackingReference=true;break;
            case 7:bad.barTrackingAuthority=true;break;
            case 8:bad.text.topSearchComplete=false;break;
            case 9:bad.text.bottomSearchComplete=false;break;
            case 10:bad.sharedPicture.inspectionCandidate.nativeGeometryRevision=0;break;
            case 11:bad.sharedPicture.inspectionCandidate.valid=false;break;
            case 12:bad.barAuthority=false;break;
            case 13:bad.barEvidence.available=false;break;
            case 14: {auto ink=std::make_shared<SubtitleInkSnapshot>(*bad.ink);ink->rawEvidenceComplete=false;bad.ink=ink;break;}
            case 15: {auto ink=std::make_shared<SubtitleInkSnapshot>(*bad.ink);ink->paletteValid=false;bad.ink=ink;break;}
            case 16: {auto ink=std::make_shared<SubtitleInkSnapshot>(*bad.ink);ink->rawInk[0]=0;ink->ownedInk[0]=1;bad.ink=ink;break;}
            case 17:bad.text.anchor={};break;
            }
            Assert::IsFalse(SubtitleBoxLookahead::RebindCompletedInspectedPlane(bad,canonical.sharedPicture),
                L"A canonical publication cannot repair incomplete, stale or unowned glyph work");
        }
        auto proofCarrier=completed;proofCarrier.subtitleArProof.accepted=true;
        proofCarrier.subtitleArDarkness.global.evaluated=true;
        Assert::IsTrue(SubtitleBoxLookahead::RebindCompletedInspectedPlane(proofCarrier,canonical.sharedPicture));
        Assert::IsFalse(proofCarrier.subtitleArProof.accepted);
        Assert::IsFalse(proofCarrier.subtitleArDarkness.global.evaluated);
    }

    TEST_METHOD(UnavailablePlaneWorkCoalescesCanonicalRequestWithoutDuplicatingActiveOrPendingScan) {
        {
            auto canonical=Ready(10).current;canonical.identity.analysisDomainKey=0xA11CE;
            canonical.sharedPicture.identity=canonical.identity;canonical.analyzed=false;canonical.text={};
            auto raw=canonical;raw.sharedPicture.bounds={};raw.pictureAuthorityUnavailableForMeasurement=true;
            Assert::IsTrue(SubtitleMeasurementWorker::SamePendingFullSourceWork(raw,canonical));
            Assert::IsFalse(SubtitleMeasurementWorker::SamePendingFullSourceWork(canonical,raw),L"The new alias is directional, not a replacement READY key.");
            for(unsigned fault=0;fault<27;++fault) {
                auto changed=canonical;auto prior=raw;
                switch(fault) {
                case 0:++changed.identity.acceptedSequence;break;
                case 1:++changed.identity.sourceFrameNumber;break;
                case 2:++changed.identity.captureTimestamp;break;
                case 3:++changed.identity.transportGeneration;break;
                case 4:++changed.identity.sourceFormatGeneration;break;
                case 5:++changed.identity.analysisDomainKey;break;
                case 6:++changed.identity.viewportGeneration;break;
                case 7:++changed.identity.rendererGeneration;break;
                case 8:++changed.width;break;
                case 9:++changed.height;break;
                case 10:++changed.nearBarDistance;break;
                case 11:++changed.optimizationMode;break;
                case 12:changed.scriptHint=SubtitleScriptHint::Arabic;break;
                case 13:++changed.policyGeneration;break;
                case 14:++changed.continuityGeneration;break;
                case 15:changed.analysisStride=2;break;
                case 16:changed.discontinuity=true;break;
                case 17:changed.sharedPicture.inspectionCandidate.valid=true;break;
                case 18:prior.sharedPicture.inspectionCandidate.valid=true;break;
                case 19:prior.glyphLocalTop=true;break;
                case 20:prior.currentAnchorUsesTrackedEdge=true;break;
                case 21:prior.analysisRefresh=true;break;
                case 22:prior.pendingRefresh=true;break;
                case 23:prior.incomingBarTrackingReference.valid=true;break;
                case 24:prior.pictureAuthorityUnavailableForMeasurement=false;break;
                case 25:changed.sharedPicture.required=false;break;
                case 26:prior.trackedBottomHypothesisAttempted=true;break;
                }
                Assert::IsFalse(SubtitleMeasurementWorker::SamePendingFullSourceWork(prior,changed),
                    L"Work coalescing cannot cross source/settings/epochs or dependent scan paths.");
            }
        }
        for(bool pending:{false,true}) {
            std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
            worker.SetInputRateHz(24.0);std::atomic_uint entered{0},rawCalls{0},replacementCalls{0};
            auto pixels=PixelsWithCurrentBars();auto completed=CompleteThreeLineScan(10,pixels);
            completed.identity.analysisDomainKey=0xA11CE;completed.sharedPicture.identity=completed.identity;
            completed.text.topSearchComplete=completed.text.bottomSearchComplete=true;
            auto canonical=completed;canonical.analyzed=false;canonical.text={};canonical.ink.reset();
            auto raw=canonical;raw.sharedPicture.bounds={};raw.pictureAuthorityUnavailableForMeasurement=true;
            completed.sharedPicture=raw.sharedPicture;completed.pictureAuthorityUnavailableForMeasurement=true;
            if(pending) {
                auto blocker=WorkerKey(9,0xA11CE);
                worker.Submit(blocker,[&,blocker](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                    ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    auto result=blocker;result.analyzed=true;return result;
                });
                Assert::IsTrue(WaitForEntered(entered,1));
            }
            worker.Submit(raw,[&,completed](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++rawCalls;++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return completed;
            });
            if(!pending)Assert::IsTrue(WaitForEntered(entered,1));
            worker.Submit(canonical,[&,canonical](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++replacementCalls;auto result=canonical;result.analyzed=true;return result;
            });
            const auto before=worker.TakeDiagnostics();
            release.store(true);SubtitleBoxObservation result;
            const bool ready=WaitForRead(worker,canonical,result);worker.Stop();
            Assert::AreEqual(uint64_t(pending?2:1),before.submitted,
                L"A newly available native plane must not duplicate the same owned source full scan already active or pending.");
            Assert::AreEqual(1u,rawCalls.load());Assert::AreEqual(0u,replacementCalls.load());
            Assert::IsTrue(ready,L"Only the existing strict completed independent-result rebind may admit the original raw scan.");
            Assert::IsTrue(result.independentBarAnalysis && result.sharedPicture.Matches(canonical.sharedPicture));
            Assert::IsTrue(SubtitleBoxLookahead::SameContext(completed,result));
        }
    }
    TEST_METHOD(CoalescedUnavailableWorkPreservesReadyEvidenceAndAllowsCanonicalRetry) {
        std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
        worker.SetInputRateHz(24.0);std::atomic_uint entered{0},replacementCalls{0};
        auto pixels=PixelsWithCurrentBars();auto complete=CompleteThreeLineScan(10,pixels);
        complete.identity.analysisDomainKey=0xA11CE;complete.sharedPicture.identity=complete.identity;
        complete.text.topSearchComplete=complete.text.bottomSearchComplete=true;
        auto canonical=complete;canonical.analyzed=false;canonical.text={};canonical.ink.reset();
        auto raw=canonical;raw.sharedPicture.bounds={};raw.pictureAuthorityUnavailableForMeasurement=true;
        auto failed=raw;failed.analyzed=true;failed.text.workLimit=true;failed.independentBarAnalysis=false;
        worker.Submit(raw,[&,failed](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return failed;
        });
        Assert::IsTrue(WaitForEntered(entered,1));
        const auto replacement=[&,complete](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            ++replacementCalls;return complete;
        };
        worker.Submit(canonical,replacement);
        const auto before=worker.TakeDiagnostics();release.store(true);
        SubtitleBoxObservation result;Assert::IsTrue(WaitForRead(worker,raw,result));
        Assert::IsFalse(worker.TryRead(canonical,result),L"A dependent result without independent bars cannot rebind to the new canonical plane.");
        worker.Submit(canonical,replacement);
        Assert::IsTrue(WaitForRead(worker,canonical,result));worker.Stop();
        Assert::AreEqual(uint64_t(1),before.submitted,L"Only active work is coalesced; READY compatibility remains strict.");
        Assert::AreEqual(1u,replacementCalls.load(),L"An incompatible completed result must permit a subsequent canonical scan.");
        // A complete independent negative is reusable too. Every incomplete
        // changed-plane observation remains readable under its ORIGINAL key,
        // but must allow a canonical retry instead of suppressing that work.
        for(unsigned fault=0;fault<15;++fault) {
            SubtitleMeasurementWorker independentWorker;auto observation=complete;
            observation.sharedPicture=raw.sharedPicture;observation.pictureAuthorityUnavailableForMeasurement=true;
            const bool expectedConclusive=fault<2;
            switch(fault) {
            case 0:break;
            case 1: {
                auto emptyPixels=PixelsWithCurrentBars();
                for(int y=0;y<90;++y)for(int x=0;x<160;++x)
                    emptyPixels[y*160+x]=uint16_t((y>=12 && y<78?300:64)<<6);
                auto empty=CompleteThreeLineScan(10,emptyPixels);
                observation.ink=empty.ink;observation.text={};
                observation.text.topSearchComplete=observation.text.bottomSearchComplete=true;
                observation.text.diagnosticReason="no-bright-ink";break;
            }
            case 2:observation.text.workLimit=true;break;
            case 3:observation.text.topSearchComplete=false;break;
            case 4:observation.text.bottomSearchComplete=false;break;
            case 5:observation.ink.reset();break;
            case 6:observation.independentBarAnalysis=false;break;
            case 7:observation.text.held=true;break;
            case 8:observation.text.confirmationPending=true;break;
            case 9:observation.text.bounds={};break;
            case 10: {auto ink=std::make_shared<SubtitleInkSnapshot>(*observation.ink);ink->rawEvidenceComplete=false;observation.ink=ink;break;}
            case 11: {auto ink=std::make_shared<SubtitleInkSnapshot>(*observation.ink);ink->paletteValid=false;observation.ink=ink;break;}
            case 12:observation.text={};observation.text.topSearchComplete=true;break;
            case 13:observation.text={};observation.text.topSearchComplete=observation.text.bottomSearchComplete=true;observation.barEvidence.available=false;break;
            case 14:observation.barEvidence.captionBackedBottom=true;break;
            }
            independentWorker.Submit(raw,[observation](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                return observation;
            });
            Assert::IsTrue(WaitForRead(independentWorker,raw,result),L"Same-key partial/negative observations remain readable without becoming caption proof.");
            Assert::AreEqual(expectedConclusive,independentWorker.TryRead(canonical,result),
                L"Only a full independent current scan can satisfy a changed unavailable-to-canonical plane request.");
            if(!expectedConclusive) {
                std::atomic_uint retried{0};
                independentWorker.Submit(canonical,[&,complete](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                    ++retried;return complete;
                });
                const bool ran=WaitForEntered(retried,1);
                SubtitleBoxObservation oldResult;
                Assert::IsTrue(independentWorker.TryTake(raw,oldResult));
                Assert::IsTrue(WaitForRead(independentWorker,canonical,result));
                const auto d=independentWorker.TakeDiagnostics();independentWorker.Stop();
                Assert::IsTrue(ran);Assert::AreEqual(uint64_t(2),d.submitted);
                Assert::IsTrue(SourceCadenceObservationComplete(result,canonical.identity));
            } else independentWorker.Stop();
        }
    }
    TEST_METHOD(LateCompletedRawPredecessorsSurviveReadyExpiryWithoutBecomingCurrentEvidence) {
        std::atomic_bool release{false};std::atomic_uint entered{0};
        SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};worker.SetInputRateHz(24.0);
        for(uint64_t sequence=15;sequence<18;++sequence) {
            auto scan=HistoricalRawScan(sequence);auto key=scan;key.analyzed=false;key.text={};key.ink.reset();
            worker.BeginSourceWindow(key.identity);release.store(false);const auto target=entered.load()+1;
            worker.Submit(key,[&,scan](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return scan;
            });
            const bool started=WaitForEntered(entered,target);
            worker.BeginSourceWindow(HistoricalRawScan(sequence+1).identity);release.store(true);
            Assert::IsTrue(started&&WaitForWorkerIdle(worker));
            SubtitleBoxObservation rejected;
            Assert::IsFalse(worker.TryRead(key,rejected),L"Expired predecessor scans must remain unavailable through current READY reads.");
        }
        auto current=HistoricalRawScan(18);auto key=current;key.analyzed=false;key.text={};key.ink.reset();
        worker.Submit(key,[current](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return current;});
        SubtitleBoxObservation ready;Assert::IsTrue(WaitForRead(worker,key,ready));
        auto snapshot=worker.ReadMeasuredRawPredecessors(ready);worker.Stop();
        Assert::IsTrue(snapshot && snapshot->MatchesCurrent(ready));
        Assert::AreEqual(size_t(3),snapshot->Frames().size(),
            L"Real completed inputs15/16/17 must remain available as RAW-only confirmation when current18 is ready, even though every old READY result expired.");
        for(size_t i=0;i<snapshot->Frames().size();++i) {
            const auto& raw=snapshot->Frames()[i];
            Assert::AreEqual(uint64_t(15+i),raw.identity.acceptedSequence);
            Assert::AreEqual(uint64_t((15+i)*1000),raw.identity.captureTimestamp);
            Assert::IsFalse(raw.subtitleArProof.accepted,L"RAW history must not export native AR publication proof.");
        }
        auto owned=std::make_unique<std::array<SubtitleBoxObservation,4>>();
        for(size_t i=0;i<3;++i)(*owned)[i]=snapshot->Frames()[i];(*owned)[3]=ready;
        const auto confirmed=SubtitleBoxLookahead::Resolve(owned->data(),4,ready.policyGeneration,ready.continuityGeneration);
        Assert::AreEqual(3,confirmed.text.lineCount,L"The unchanged four-source current-pixel/backing proof still decides which rows exist.");
        // The original RAW job may predate canonical native authority. Only
        // its copied context can rebind, with the measured physical bars exact.
        SubtitleMeasurementWorker coldWorker;
        for(uint64_t sequence=23;sequence<=26;++sequence) {
            auto scan=HistoricalRawScan(sequence);auto request=scan;
            if(sequence<26) {scan.sharedPicture.bounds={};scan.pictureAuthorityUnavailableForMeasurement=true;}
            request=scan;request.analyzed=false;request.text={};request.ink.reset();
            coldWorker.BeginSourceWindow(request.identity);
            coldWorker.Submit(request,[scan](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return scan;});
            SubtitleBoxObservation observed;Assert::IsTrue(WaitForRead(coldWorker,request,observed));
            if(sequence==26) {
                auto cold=coldWorker.ReadMeasuredRawPredecessors(observed);
                Assert::AreEqual(size_t(3),cold->Frames().size());
                for(const auto& raw:cold->Frames()) {
                    Assert::IsTrue(raw.sharedPicture.Matches(observed.sharedPicture));
                    Assert::IsTrue(raw.pictureAuthorityUnavailableForMeasurement,L"Original scan provenance is retained despite copy-only context rebinding.");
                    Assert::AreEqual(12,raw.pictureTop);Assert::AreEqual(78,raw.pictureBottom);
                }
                auto changed=observed;changed.pictureTop=14;changed.barEvidence.top=14;
                auto rejected=coldWorker.ReadMeasuredRawPredecessors(changed);
                Assert::IsTrue(rejected->Frames().empty(),L"A changed current physical picture cannot borrow old RAW history.");
            }
        }
        coldWorker.Stop();
        SubtitleMeasurementWorker epochWorker;
        epochWorker.BeginSourceWindow(HistoricalRawScan(30).identity);
        auto oldEpoch=HistoricalRawScan(30);auto oldKey=oldEpoch;oldKey.analyzed=false;oldKey.text={};oldKey.ink.reset();
        epochWorker.Submit(oldKey,[oldEpoch](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return oldEpoch;});
        SubtitleBoxObservation oldReady;Assert::IsTrue(WaitForRead(epochWorker,oldKey,oldReady));
        // A valid new-epoch FUTURE can finish before current adopts that epoch.
        // Adopting current must only remove incompatible old-epoch metadata.
        for(uint64_t sequence:{uint64_t(31),uint64_t(30),uint64_t(32)}) {
            auto scan=HistoricalRawScan(sequence);++scan.policyGeneration;++scan.continuityGeneration;
            scan.hasBarTrackingReference=true; // supplied but UNUSED by independent current pixels
            auto request=scan;request.analyzed=false;request.text={};request.ink.reset();
            if(sequence==32)epochWorker.BeginSourceWindow(request.identity);
            epochWorker.Submit(request,[scan](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return scan;});
            SubtitleBoxObservation observed;Assert::IsTrue(WaitForRead(epochWorker,request,observed));
            if(sequence==32) {
                auto retained=epochWorker.ReadMeasuredRawPredecessors(observed);
                Assert::AreEqual(size_t(2),retained->Frames().size(),L"A current epoch change must preserve already-dispatched matching new-epoch future work.");
                Assert::AreEqual(uint64_t(30),retained->Frames()[0].identity.acceptedSequence);
                Assert::AreEqual(uint64_t(31),retained->Frames()[1].identity.acceptedSequence);
                Assert::IsTrue(retained->Frames()[0].hasBarTrackingReference,L"An unused supplied reference cannot erase independently measured current RAW evidence.");
            }
        }
        epochWorker.Stop();
    }
    TEST_METHOD(RawPredecessorHistoryCannotResurrectAnOlderCompletionAfterAChangedDuplicate) {
        std::atomic_bool release{false};std::atomic_uint entered{0};
        SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};worker.SetInputRateHz(60.0);
        auto old=HistoricalRawScan(15);auto oldKey=old;oldKey.analyzed=false;oldKey.text={};oldKey.ink.reset();
        worker.BeginSourceWindow(old.identity);
        worker.Submit(oldKey,[&,old](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return old;
        });
        Assert::IsTrue(WaitForEntered(entered,1));
        auto changed=oldKey;changed.sharedPicture.bounds={0,14,160,76,160,90};
        worker.Submit(changed,[changed](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            auto incomplete=changed;incomplete.analyzed=true;incomplete.text.workLimit=true;return incomplete;
        });
        SubtitleBoxObservation partial;Assert::IsTrue(WaitForRead(worker,changed,partial));
        SubtitleBoxObservation current;
        for(uint64_t sequence=16;sequence<=18;++sequence) {
            auto scan=HistoricalRawScan(sequence);auto key=scan;key.analyzed=false;key.text={};key.ink.reset();
            worker.BeginSourceWindow(key.identity);
            worker.Submit(key,[scan](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return scan;});
            SubtitleBoxObservation done;Assert::IsTrue(WaitForRead(worker,key,done));if(sequence==18)current=done;
        }
        // The older valid15 finishes AFTER a newer incompatible interpretation.
        // Its late completion cannot reverse that withdrawal of RAW evidence.
        release.store(true);Assert::IsTrue(WaitForWorkerIdle(worker));
        auto snapshot=worker.ReadMeasuredRawPredecessors(current);worker.Stop();
        Assert::IsTrue(snapshot && snapshot->MatchesCurrent(current));
        Assert::AreEqual(size_t(2),snapshot->Frames().size(),L"Only the newest contiguous16/17 suffix survives; the invalidated15 cannot resurrect from out-of-order completion.");
        Assert::AreEqual(uint64_t(16),snapshot->Frames()[0].identity.acceptedSequence);
        Assert::AreEqual(uint64_t(17),snapshot->Frames()[1].identity.acceptedSequence);
    }
    TEST_METHOD(TypedBacklogWitnessCoalescesOwnedActivePendingAndCompletedSourceWork) {
        for(unsigned phase=0;phase<3;++phase) {
            ActivePictureDecisionTimeline timeline;
            for(uint64_t i=1;i<=6;++i)Assert::IsTrue(timeline.TrackAcceptedFrame(HistoricalRawScan(i).identity));
            const auto before=timeline.ContinuityGeneration(),policy=timeline.LookaheadPolicyGeneration();
            timeline.MarkDiscarded(HistoricalRawScan(1).identity,1,ActivePictureDiscardReason::BacklogRecovery);
            timeline.MarkDiscarded(HistoricalRawScan(2).identity,2,ActivePictureDiscardReason::QueueCapacityOverflow);
            const auto after=timeline.ContinuityGeneration();
            auto old=HistoricalRawScan(3);old.policyGeneration=policy;old.continuityGeneration=before;
            old.sharedPicture.bounds={0,10,160,80,160,90}; // old retained collar; RAW bars remain12..78
            auto original=old;original.analyzed=false;original.text={};original.ink.reset();
            auto canonical=old;canonical.continuityGeneration=after;canonical.sharedPicture.bounds={0,12,160,78,160,90};
            auto incoming=canonical;incoming.analyzed=false;incoming.text={};incoming.ink.reset();
            const auto witness=timeline.QualifyOwnedScanBacklogWitness(incoming.identity,before,after);
            Assert::IsTrue(witness.Matches(incoming.identity,policy,before,after));
            std::atomic_bool release{phase==2};std::atomic_uint entered{0},replacements{0};
            SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};worker.SetInputRateHz(phase==1?24.0:60.0);
            if(phase==1) {
                auto blocker=WorkerKey(2,0xA11CE);
                worker.Submit(blocker,[&,blocker](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                    ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    auto done=blocker;done.analyzed=true;return done;
                });Assert::IsTrue(WaitForEntered(entered,1));
            }
            worker.BeginSourceWindow(original.identity);
            worker.Submit(original,[&,old](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++entered;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return old;
            });
            bool observed=false;uint64_t observedContinuity=0;SubtitleBoxObservation result;
            if(phase==2) {
                Assert::IsTrue(WaitForRead(worker,original,result));
                Assert::IsTrue(worker.TryReadOwnedScan(incoming,result,witness,policy),L"Completed original pixels already satisfy the existing typed rebind path.");
            } else {
                if(phase==0)Assert::IsTrue(WaitForEntered(entered,1));
                observed=worker.PeekOwnedWorkContinuity(incoming,observedContinuity);
            }
            worker.Submit(incoming,[&,canonical](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++replacements;return canonical;
            },&witness,policy);
            const auto submitted=worker.TakeDiagnostics().submitted;
            release.store(true);const bool idle=WaitForWorkerIdle(worker);
            const bool available=worker.TryReadOwnedScan(incoming,result,witness,policy);worker.Stop();
            if(phase!=2) {Assert::IsTrue(observed,L"The queue owner must be able to qualify already-owned pending RAW work before dispatching a duplicate.");Assert::AreEqual(before,observedContinuity);}
            Assert::AreEqual(uint64_t(phase==1?2:1),submitted,L"A pure recorded queue retirement must not run the same immutable source scan twice.");
            Assert::IsTrue(idle&&available);Assert::AreEqual(0u,replacements.load());
            Assert::IsTrue(result.ink.get()==old.ink.get());
            Assert::AreEqual(after,result.continuityGeneration);Assert::AreEqual(before,old.continuityGeneration);
            Assert::IsFalse(result.subtitleArProof.accepted,L"A copied subtitle measurement cannot restore old native AR history.");
        }
    }
    TEST_METHOD(TypedBacklogWorkCoalescingRejectsMissingOwnershipAndIncompleteCompletedResults) {
        for(unsigned fault=0;fault<10;++fault) {
            ActivePictureDecisionTimeline timeline;
            for(uint64_t i=1;i<=6;++i)Assert::IsTrue(timeline.TrackAcceptedFrame(HistoricalRawScan(i).identity));
            const auto before=timeline.ContinuityGeneration(),policy=timeline.LookaheadPolicyGeneration();
            timeline.MarkDiscarded(HistoricalRawScan(1).identity,1,fault==1?ActivePictureDiscardReason::Unknown:ActivePictureDiscardReason::BacklogRecovery);
            const auto after=timeline.ContinuityGeneration();
            auto old=HistoricalRawScan(3);old.policyGeneration=policy;old.continuityGeneration=before;
            old.sharedPicture.bounds={0,10,160,80,160,90};
            auto original=old;original.analyzed=false;original.text={};original.ink.reset();
            auto canonical=old;canonical.continuityGeneration=after;canonical.sharedPicture.bounds={0,12,160,78,160,90};
            auto incoming=canonical;incoming.analyzed=false;incoming.text={};incoming.ink.reset();
            const auto witness=timeline.QualifyOwnedScanBacklogWitness(incoming.identity,before,after);
            auto ownerPolicy=policy;
            switch(fault) {
            case 0:break; // missing witness below
            case 1:break; // unknown retirement cannot mint permission
            case 2:++ownerPolicy;break;
            case 3:++incoming.policyGeneration;canonical.policyGeneration=incoming.policyGeneration;break;
            case 4:++incoming.nearBarDistance;canonical.nearBarDistance=incoming.nearBarDistance;break;
            case 5:++incoming.optimizationMode;canonical.optimizationMode=incoming.optimizationMode;break;
            case 6:++incoming.identity.analysisDomainKey;incoming.sharedPicture.identity=incoming.identity;canonical.identity=incoming.identity;canonical.sharedPicture.identity=incoming.identity;break;
            case 7:old.text.workLimit=true;break;
            case 8:old.text.bottomSearchComplete=false;break;
            case 9:old.ink.reset();break;
            }
            SubtitleMeasurementWorker worker;std::atomic_uint replacements{0};
            worker.Submit(original,[old](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return old;});
            SubtitleBoxObservation result;Assert::IsTrue(WaitForRead(worker,original,result));
            worker.Submit(incoming,[&,canonical](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++replacements;return canonical;
            },fault==0?nullptr:&witness,ownerPolicy);
            Assert::IsTrue(WaitForRead(worker,incoming,result));worker.Stop();
            Assert::AreEqual(1u,replacements.load(),L"Unowned, changed settings, or incomplete old work must permit a real canonical scan.");
        }
    }
    TEST_METHOD(LowInputCadenceRetainsOneSerialSampler) {
        for(double hz:{23.976,24.0,29.97,30.0}) {
            std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
            worker.SetInputRateHz(hz);std::atomic_uint entered{0};
            const auto work=[&](SubtitleBoxObservation key) {
                return [&,key](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                    ++entered;while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    auto result=key;result.analyzed=true;return result;
                };
            };
            const auto first=WorkerKey(1,0xA11CE),second=WorkerKey(2,0xA11CE);
            worker.Submit(first,work(first));worker.Submit(second,work(second));
            const bool started=WaitForEntered(entered,1);std::this_thread::sleep_for(std::chrono::milliseconds(30));
            const unsigned beforeRelease=entered.load();release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;const bool complete=WaitForTake(worker,first,result)&&WaitForTake(worker,second,result);
            worker.Stop();Assert::IsTrue(started&&complete);Assert::AreEqual(1u,beforeRelease);
        }
    }
    TEST_METHOD(ParallelSourceCompletionOutOfOrderRemainsBoundToItsExactIdentity) {
        std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
        worker.SetInputRateHz(60.0);std::atomic_uint entered{0};
        const auto first=WorkerKey(9,0xA11CE),second=WorkerKey(10,0xA11CE);
        worker.Submit(first,[&,first](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            ++entered;while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=first;result.analyzed=true;result.text.cue=91;return result;
        });
        const bool firstActive=WaitForEntered(entered,1);
        worker.Submit(second,[second](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            auto result=second;result.analyzed=true;result.text.cue=101;return result;
        });
        SubtitleBoxObservation early;const bool secondReady=WaitForRead(worker,second,early);
        SubtitleBoxObservation unavailable;const bool firstReadyTooSoon=worker.TryRead(first,unavailable);
        auto wrong=second;++wrong.identity.analysisDomainKey;const bool wrongRead=worker.TryRead(wrong,unavailable);
        release.store(true,std::memory_order_release);SubtitleBoxObservation late;
        const bool firstReady=WaitForTake(worker,first,late);worker.Stop();
        Assert::IsTrue(firstActive&&secondReady&&firstReady,L"Later independent source completion must not wait behind an older blocked scan.");
        Assert::IsFalse(firstReadyTooSoon||wrongRead);Assert::AreEqual(uint64_t(101),early.text.cue);
        Assert::AreEqual(uint64_t(10),early.identity.acceptedSequence);Assert::AreEqual(uint64_t(91),late.text.cue);
    }
    TEST_METHOD(StopJoinsBothActiveSamplersAndCancelsPendingWithoutPublication) {
        std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
        worker.SetInputRateHz(60.0);std::atomic_uint entered{0},calls{0};
        const auto work=[&](SubtitleBoxObservation key) {
            return [&,key](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
                ++calls;++entered;while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
                auto result=key;result.analyzed=true;return result;
            };
        };
        const auto first=WorkerKey(1,0xA11CE),second=WorkerKey(2,0xA11CE),pending=WorkerKey(3,0xA11CE);
        worker.Submit(first,work(first));worker.Submit(second,work(second));
        const bool twoActive=WaitForEntered(entered,2);worker.Submit(pending,work(pending));
        std::thread stopping([&]{worker.Stop();});
        bool stopObserved=false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<deadline) {
            if(worker.TakeDiagnostics().stopping){stopObserved=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto rejected=WorkerKey(4,0xA11CE);worker.Submit(rejected,work(rejected));
        release.store(true,std::memory_order_release);stopping.join();
        SubtitleBoxObservation result;
        Assert::IsTrue(twoActive&&stopObserved);Assert::AreEqual(2u,calls.load());
        Assert::IsFalse(worker.TryRead(first,result)||worker.TryRead(second,result)||worker.TryRead(pending,result));
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::IsTrue(diagnostics.stopping&&!diagnostics.active&&diagnostics.activeWorkers==0&&diagnostics.pending==0);
        worker.SetInputRateHz(120);worker.Submit(rejected,work(rejected));
        Assert::AreEqual(2u,calls.load(),L"A stopped worker pool cannot restart through cadence configuration or submission.");
    }
    TEST_METHOD(LateOldGenerationCompletionCannotReplaceTheCurrentReadySource) {
        std::atomic_bool release{false};SubtitleMeasurementWorker worker;ReleaseBlockedWorkers unblock{release};
        worker.SetInputRateHz(59.94);std::atomic_uint entered{0};
        const auto old=WorkerKey(5,0xA11CE);
        worker.Submit(old,[&,old](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {
            ++entered;while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=old;result.analyzed=true;result.text.cue=55;return result;
        });
        const bool started=WaitForEntered(entered,1);
        auto current=WorkerKey(1,0xA11CE);current.identity.transportGeneration=2;
        worker.BeginSourceWindow(current.identity);
        worker.Submit(current,[current](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference& prior) {
            auto result=current;result.analyzed=true;result.text.cue=prior.valid?999:21;return result;
        });
        SubtitleBoxObservation result;const bool currentReady=WaitForRead(worker,current,result);
        release.store(true,std::memory_order_release);
        uint64_t discarded=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(discarded==0 && std::chrono::steady_clock::now()<deadline) {
            discarded+=worker.TakeDiagnostics().staleCompleted;
            if(!discarded)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        worker.Stop();
        Assert::IsTrue(started&&currentReady);Assert::AreEqual(uint64_t(1),discarded);
        Assert::IsFalse(worker.TryRead(old,result));Assert::IsTrue(worker.TryRead(current,result));
        Assert::AreEqual(uint64_t(21),result.text.cue);Assert::AreEqual(uint64_t(2),result.identity.transportGeneration);
    }
    TEST_METHOD(NonAdjacentOrNewGenerationSourceCannotBorrowWorkerTrackingReference) {
        SubtitleMeasurementWorker worker;worker.SetInputRateHz(24.0);
        auto first=Ready(1).current;first.identity.analysisDomainKey=0xA11CE;
        worker.Submit(first,[first](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) {return first;});
        SubtitleBoxObservation result;Assert::IsTrue(WaitForTake(worker,first,result));
        Assert::IsTrue(SubtitleBoxLookahead::AdvanceBarTrackingReference(result).valid);
        auto gap=first;gap.identity=Identity(3);gap.identity.analysisDomainKey=0xA11CE;
        std::atomic_bool borrowedGap{false},borrowedGeneration{false};
        worker.Submit(gap,[&,gap](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference& prior) {
            borrowedGap=prior.valid;return gap;
        });
        Assert::IsTrue(WaitForTake(worker,gap,result));
        auto next=first;next.identity.transportGeneration=2;next.identity.acceptedSequence=4;
        worker.BeginSourceWindow(next.identity);
        worker.Submit(next,[&,next](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference& prior) {
            borrowedGeneration=prior.valid;return next;
        });
        Assert::IsTrue(WaitForTake(worker,next,result));worker.Stop();
        Assert::IsFalse(borrowedGap.load()||borrowedGeneration.load(),
            L"Worker-local state is a hint only for the exact immediately preceding source in the same context.");
    }
    TEST_METHOD(FaultCapturePredicateRequiresCurrentSharedFailureAndBoundedSource) {
        auto pixels=Pixels();
        auto source=Source(pixels);
        auto key=Ready(17).current;
        auto result=key;
        result.text={};
        result.pictureBottom=source.height;
        result.sharedProbeAttempted=result.sharedProbeDetected=true;
        Assert::IsTrue(IsSubtitleProofFailureCaptureCandidate(source,key,result));
        auto changed=result;changed.sharedProbeDetected=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.sharedProbeAttempted=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.sharedProbeWorkLimit=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.text.workLimit=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.text.detected=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.pictureBottom=key.sharedPicture.bounds.bottom;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.identity.acceptedSequence++;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        changed=result;changed.pendingRefresh=true;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,key,changed));
        auto unavailable=key;unavailable.sharedPicture.required=false;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(source,unavailable,result));
        auto invalid=source;invalid.data=nullptr;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(invalid,key,result));
        auto oversized=source;oversized.dataBytes=64u*1024u*1024u+1u;
        Assert::IsFalse(IsSubtitleProofFailureCaptureCandidate(oversized,key,result));
    }
    TEST_METHOD(FaultCaptureModeWaitsForFailureAndPreservesLegacyStartupRules) {
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(false,true,true,false,17,false,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,false,true,false,17,false,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,true,false,17,false,false));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,true,false,17,true,true));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,false,17,false,false));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,false,17,true,true));
        Assert::IsFalse(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,true,239,true,true));
        Assert::IsTrue(ShouldCaptureSubtitleDiagnosticFrame(true,true,false,true,240,true,false));
    }

    TEST_METHOD(CompletedWorkerResultIsAvailableToTheExactQueuedFrameRequest) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(17,0xA11CE);
        std::atomic_uint calls{0};
        worker.Submit(key,[key,&calls](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++calls;auto result=key;result.analyzed=true;return result;
        });

        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForTake(worker,key,ready),
            L"A completed lookahead scan must be consumable when the same frame and analysis domain become current.");
        Assert::IsTrue(SubtitleMeasurementWorker::SameKey(key,ready));
        Assert::IsTrue(ready.analyzed);
        Assert::AreEqual(1u,calls.load());
    }

    TEST_METHOD(RepeatedExactReadsKeepOneImmutableReadyResultAndDeduplicateSubmission) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(19,0xA11CE);
        std::atomic_uint calls{0};
        const auto work=[&calls,key](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++calls;auto result=key;result.analyzed=true;
            result.text.cue=37;return result;
        };
        worker.Submit(key,work);
        SubtitleBoxObservation first;
        Assert::IsTrue(WaitForRead(worker,key,first));
        Assert::AreEqual(static_cast<uint64_t>(37),first.text.cue);
        first.text.cue=999;
        first.identity.acceptedSequence=999;
        worker.Submit(key,work); // Priming must not rescan a retained ready key.
        SubtitleBoxObservation second;
        Assert::IsTrue(worker.TryRead(key,second));
        Assert::AreEqual(static_cast<uint64_t>(37),second.text.cue);
        Assert::AreEqual(static_cast<uint64_t>(19),second.identity.acceptedSequence);
        Assert::AreEqual(1u,calls.load());
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.submitted);
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.fullScans);
        Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.deduplicated);
        Assert::AreEqual(static_cast<uint64_t>(2),diagnostics.exactReads);
        Assert::AreEqual(static_cast<size_t>(1),diagnostics.ready);
    }

    TEST_METHOD(NonDestructiveReadRefusesWrongFrameDomainAndPolicy) {
        SubtitleMeasurementWorker worker;
        const auto key=WorkerKey(29,0xA11CE);
        worker.Submit(key,[key](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            auto result=key;result.analyzed=true;return result;
        });
        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForRead(worker,key,ready));
        auto wrong=key;wrong.identity.acceptedSequence++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        wrong=key;wrong.identity.analysisDomainKey++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        wrong=key;wrong.policyGeneration++;
        Assert::IsFalse(worker.TryRead(wrong,ready));
        Assert::IsTrue(worker.TryRead(key,ready));
        Assert::IsTrue(worker.TryTake(key,ready));
        Assert::IsFalse(worker.TryRead(key,ready));
    }
    TEST_METHOD(DifferentAnalysisDomainsCannotConsumeOrCoalesceEachOthersJobs) {
        SubtitleMeasurementWorker worker;
        const auto queuedKey=WorkerKey(23,0x4E4154495645ull);
        auto currentKey=queuedKey;currentKey.identity.analysisDomainKey=0x50303130ull;
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(queuedKey,currentKey),
            L"The analysis domain is part of an exact-frame measurement identity.");

        std::atomic_uint calls{0};
        const auto work=[&calls](const SubtitleBoxObservation& key) {
            return [key,&calls](SubtitleMeasurementSampler&,
                const SubtitleBarTrackingReference&) {
                ++calls;auto result=key;result.analyzed=true;return result;
            };
        };
        worker.Submit(queuedKey,work(queuedKey));
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryTake(currentKey,ready),
            L"A request using another analysis interpretation must not accept queued pixels.");
        worker.Submit(currentKey,work(currentKey));

        Assert::IsTrue(WaitForTake(worker,queuedKey,ready));
        Assert::IsTrue(WaitForTake(worker,currentKey,ready));
        Assert::AreEqual(2u,calls.load(),
            L"A persistent interpretation mismatch submits two independent scans for one transport frame.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::IsTrue(diagnostics.requestMisses>=1,
            L"The mismatched lookup must be recorded as a miss.");
    }

    TEST_METHOD(StaleSharedPictureAuthorityCannotSatisfyTheCurrentFrameRequest) {
        SubtitleMeasurementWorker worker;
        auto staleKey=WorkerKey(31,0xD0A1A);
        staleKey.sharedPicture.required=true;staleKey.sharedPicture.identity=staleKey.identity;
        staleKey.sharedPicture.bounds={0,0,640,360,640,360};
        auto currentKey=staleKey;
        currentKey.sharedPicture.bounds={0,45,640,315,640,360};
        Assert::IsTrue(SameAnalysisFrameIdentity(staleKey.identity,currentKey.identity),
            L"Only the retained picture authority changed across this simulated transition.");
        Assert::IsFalse(SubtitleMeasurementWorker::SameKey(staleKey,currentKey));

        std::atomic_uint calls{0};
        const auto work=[&calls](const SubtitleBoxObservation& key) {
            return [key,&calls](SubtitleMeasurementSampler&,
                const SubtitleBarTrackingReference&) {
                ++calls;auto result=key;result.analyzed=true;return result;
            };
        };
        worker.Submit(staleKey,work(staleKey));
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryTake(currentKey,ready),
            L"A full-raster scan cannot be rebound to a new authority when it supplied no independent bar proof.");
        Assert::IsTrue(WaitForRead(worker,staleKey,ready));
        Assert::IsFalse(worker.TryRead(currentKey,ready),
            L"A completed dependent full-raster result must still fail canonical READY admission.");
        // Coalescing active work grants no READY authority. Once the original
        // result is known to be incompatible, a new canonical request may run.
        worker.Submit(currentKey,work(currentKey));
        Assert::IsTrue(WaitForTake(worker,staleKey,ready));
        Assert::IsTrue(WaitForTake(worker,currentKey,ready));
        Assert::AreEqual(2u,calls.load(),
            L"An incompatible completed snapshot must permit a subsequent canonical scan.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::IsTrue(diagnostics.requestMisses>=1,
            L"The stale-authority lookup must be recorded as a miss.");
        auto otherAvailable=currentKey;otherAvailable.sharedPicture.bounds={0,30,640,330,640,360};
        Assert::IsFalse(SubtitleMeasurementWorker::SamePendingFullSourceWork(currentKey,otherAvailable),
            L"Distinct available native planes are distinct dependent scan paths.");
        worker.Submit(currentKey,work(currentKey));worker.Submit(otherAvailable,work(otherAvailable));
        Assert::IsTrue(WaitForTake(worker,currentKey,ready));
        Assert::IsTrue(WaitForTake(worker,otherAvailable,ready));
        Assert::AreEqual(4u,calls.load(),L"Two distinct available plane requests must both execute independently.");
    }

    TEST_METHOD(UnavailablePictureSnapshotsCoalesceOneActiveExactFrameScan) {
        SubtitleMeasurementWorker worker;
        auto oldKey=WorkerKey(41,0xD0A1A);
        oldKey.width=160;oldKey.height=90;
        oldKey.sharedPicture.required=true;
        oldKey.sharedPicture.identity=oldKey.identity;
        ++oldKey.sharedPicture.identity.viewportGeneration;
        oldKey.sharedPicture.bounds={0,0,160,90,160,90};
        auto currentKey=oldKey;
        ++currentKey.sharedPicture.identity.rendererGeneration;
        currentKey.sharedPicture.bounds={};
        Assert::IsFalse(oldKey.sharedPicture.AvailableFor(oldKey.identity,160,90));
        Assert::IsFalse(currentKey.sharedPicture.AvailableFor(currentKey.identity,160,90));
        Assert::IsFalse(oldKey.sharedPicture.Matches(currentKey.sharedPicture));

        std::vector<uint16_t> pixels(160*90*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(300<<6));
        std::atomic_bool entered{false},release{false};
        std::atomic_uint calls{0};
        const auto work=[&](const SubtitleBoxObservation& key) {
            return [&,key](SubtitleMeasurementSampler& sampler,
                const SubtitleBarTrackingReference& prior) {
                ++calls;entered.store(true,std::memory_order_release);
                while(!release.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return sampler.Measure(Source(pixels),key,prior,24.0);
            };
        };
        worker.Submit(oldKey,work(oldKey));
        const auto enteredDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<enteredDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool enteredOnTime=entered.load(std::memory_order_acquire);
        if(enteredOnTime)worker.Submit(currentKey,work(currentKey));
        release.store(true,std::memory_order_release);
        Assert::IsTrue(enteredOnTime,L"The first exact-frame scan must be active before the authority changes.");

        SubtitleBoxObservation ready;
        Assert::IsTrue(WaitForTake(worker,currentKey,ready),
            L"The completed raw-current-frame result remains consumable while both presentation snapshots are unavailable.");
        Assert::IsTrue(ready.pictureAuthorityUnavailableForMeasurement);
        Assert::IsFalse(ready.text.detected);
        Assert::AreEqual(1u,calls.load(),
            L"Changing only between unavailable authorities must not enqueue a duplicate active scan.");
        const auto diagnostics=worker.TakeDiagnostics();
        Assert::AreEqual<uint64_t>(1,diagnostics.fullScans);
    }

    TEST_METHOD(SourceWindowExpiresOnlyPendingOldFramesWithoutWaitingForActiveWork) {
        SubtitleMeasurementWorker worker;
        auto active=WorkerKey(1,0xA11CE);
        std::atomic_bool entered{false},release{false};
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto work=[](SubtitleBoxObservation key) {
                return [key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    auto result=key;result.analyzed=true;return result;
                };
            };
            const auto expired=WorkerKey(2,0xA11CE);
            auto oldTransport=WorkerKey(20,0xA11CE);
            oldTransport.identity.transportGeneration=0;
            const auto current=WorkerKey(3,0xA11CE);
            worker.Submit(expired,work(expired));
            worker.Submit(oldTransport,work(oldTransport));
            worker.Submit(current,work(current));
            worker.BeginSourceWindow(current.identity);
            worker.Submit(expired,work(expired));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,current,result));
            Assert::IsFalse(worker.TryRead(expired,result));
            Assert::IsFalse(worker.TryRead(oldTransport,result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(3),diagnostics.expired);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started,L"The blocked active scan must start before pending pruning.");
    }

    TEST_METHOD(FullPendingQueueKeepsCurrentAndNearestSources) {
        SubtitleMeasurementWorker worker;
        const auto active=WorkerKey(50,0xA11CE);
        std::atomic_bool entered{false},release{false};
        std::mutex executionMutex;
        std::vector<uint64_t> execution;
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            {std::lock_guard<std::mutex> guard(executionMutex);execution.push_back(active.identity.acceptedSequence);}
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto current=WorkerKey(51,0xA11CE);
            worker.BeginSourceWindow(current.identity);
            const auto work=[&](SubtitleBoxObservation key) {
                return [&,key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    {std::lock_guard<std::mutex> guard(executionMutex);
                        execution.push_back(key.identity.acceptedSequence);}
                    auto result=key;result.analyzed=true;return result;
                };
            };
            for(uint64_t sequence=52;sequence<=59;++sequence) {
                const auto future=WorkerKey(sequence,0xA11CE);
                worker.Submit(future,work(future));
            }
            const auto farther=WorkerKey(60,0xA11CE);
            worker.Submit(farther,work(farther));
            worker.Submit(current,work(current));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,current,result));
            {std::lock_guard<std::mutex> guard(executionMutex);
                Assert::IsTrue(execution.size()>=2);
                Assert::AreEqual(static_cast<uint64_t>(50),execution[0]);
                Assert::AreEqual(static_cast<uint64_t>(51),execution[1],
                    L"The current source must execute immediately after the active scan.");}
            Assert::IsFalse(worker.TryRead(WorkerKey(59,0xA11CE),result));
            Assert::IsFalse(worker.TryRead(farther,result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(2),diagnostics.dropped);
            Assert::AreEqual(static_cast<uint64_t>(10),diagnostics.submitted);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started,L"The blocked active scan must start before filling pending capacity.");
    }
    TEST_METHOD(NewTransportCurrentRequestSupersedesOldFullPendingQueueWithoutPriming) {
        SubtitleMeasurementWorker worker;
        const auto active=WorkerKey(1,0xA11CE);
        std::atomic_bool entered{false},release{false};
        worker.Submit(active,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result=active;result.analyzed=true;return result;
        });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!entered.load(std::memory_order_acquire) &&
            std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool started=entered.load(std::memory_order_acquire);
        if(started) {
            const auto work=[](SubtitleBoxObservation key) {
                return [key](SubtitleMeasurementSampler&,
                    const SubtitleBarTrackingReference&) {
                    auto result=key;result.analyzed=true;return result;
                };
            };
            for(uint64_t sequence=2;sequence<=9;++sequence) {
                const auto old=WorkerKey(sequence,0xA11CE);
                worker.Submit(old,work(old));
            }
            auto fresh=WorkerKey(1,0xA11CE);
            fresh.identity.transportGeneration=2;
            worker.Submit(fresh,work(fresh));
            release.store(true,std::memory_order_release);
            SubtitleBoxObservation result;
            Assert::IsTrue(WaitForRead(worker,fresh,result));
            Assert::IsFalse(worker.TryRead(WorkerKey(9,0xA11CE),result));
            const auto diagnostics=worker.TakeDiagnostics();
            Assert::AreEqual(static_cast<uint64_t>(1),diagnostics.dropped);
            Assert::AreEqual(static_cast<uint64_t>(10),diagnostics.submitted);
        } else release.store(true,std::memory_order_release);
        Assert::IsTrue(started);
    }
    TEST_METHOD(OwnedStartupScanReusesOnlyItsExactRetiredQueueContextWithoutAnAcquiredCue) {
        ActivePictureDecisionTimeline timeline;timeline.Reset(1);
        auto identity=[](uint64_t seq){auto id=Identity(seq);id.analysisDomainKey=17;return id;};
        for(uint64_t seq=1;seq<=4;++seq)Assert::IsTrue(timeline.TrackAcceptedFrame(identity(seq)));
        const auto policy=timeline.LookaheadPolicyGeneration(),before=timeline.ContinuityGeneration();
        auto pixels=PixelsWithCurrentBars();auto scan=CompleteThreeLineScan(3,pixels,before);
        scan.identity=identity(3);scan.sharedPicture.identity=scan.identity;scan.policyGeneration=policy;
        scan.text.topSearchComplete=scan.text.bottomSearchComplete=true;
        scan.subtitleArProof.accepted=true;
        auto key=scan;key.analyzed=false;key.independentBarAnalysis=false;
        SubtitleMeasurementWorker worker;worker.Submit(key,[scan](SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&){return scan;});
        SubtitleBoxObservation original;Assert::IsTrue(WaitForRead(worker,key,original));
        timeline.MarkDiscarded(identity(1),1,ActivePictureDiscardReason::BacklogRecovery);
        timeline.MarkDiscarded(identity(2),2,ActivePictureDiscardReason::QueueCapacityOverflow);
        auto current=key;current.continuityGeneration=timeline.ContinuityGeneration();
        uint64_t old=0;Assert::IsTrue(worker.PeekIndependentContinuity(current,old));
        Assert::AreEqual(before,old);
        const auto witness=timeline.QualifyOwnedScanBacklogWitness(current.identity,old,current.continuityGeneration);
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryRead(current,ready),L"No acquired cue or implicit history token exists at startup");
        Assert::IsTrue(worker.TryReadOwnedScan(current,ready,witness,policy));
        Assert::IsTrue(SameAnalysisFrameIdentity(ready.identity,current.identity));
        Assert::AreEqual(current.continuityGeneration,ready.continuityGeneration);
        Assert::IsFalse(ready.subtitleArProof.accepted,L"Reused current glyph pixels cannot restore old AR proof");
        for(int fault=0;fault<6;++fault) {
            auto wrong=current;
            if(fault==0)++wrong.identity.analysisDomainKey;
            if(fault==1)++wrong.identity.sourceFrameNumber;
            if(fault==2)++wrong.policyGeneration;
            if(fault==3)wrong.discontinuity=true;
            if(fault==4)++wrong.optimizationMode;
            if(fault==5)++wrong.identity.captureTimestamp;
            Assert::IsFalse(worker.PeekIndependentContinuity(wrong,old));
            Assert::IsFalse(worker.TryReadOwnedScan(wrong,ready,witness,policy));
        }
        for(int fault=0;fault<10;++fault) {
            auto partial=original;
            if(fault==0)partial.barTrackingAuthority=true;
            if(fault==1)partial.currentAnchorUsesTrackedEdge=true;
            if(fault==2)partial.pendingRefresh=true;
            if(fault==3)partial.analysisRefresh=true;
            if(fault==4)partial.text.workLimit=true;
            if(fault==5)partial.text.topSearchComplete=false;
            if(fault==6)partial.barEvidence.bottomRevalidatedAtReference=true;
            if(fault==7)partial.barEvidence.captionBackedBottom=true;
            if(fault==8){auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);ink->rawEvidenceComplete=false;partial.ink=ink;}
            if(fault==9){auto ink=std::make_shared<SubtitleInkSnapshot>(*partial.ink);ink->paletteValid=false;partial.ink=ink;}
            Assert::IsFalse(SubtitleMeasurementWorker::CompleteIndependentScanForKey(partial,current),
                L"Only a fully searched independent source can request a startup continuity witness");
        }
        Assert::IsFalse(worker.TryReadOwnedScan(current,ready,OwnedScanBacklogWitness{},policy));
        Assert::IsFalse(worker.TryReadOwnedScan(current,ready,witness,policy+1));
    }
    TEST_METHOD(BacklogTokenReusesOnlyCompletedExactCurrentGlyphAndNativeBars) {
        SubtitleMeasurementWorker worker;
        auto oldKey=WorkerKey(51,0xA11CE);
        oldKey.continuityGeneration=21;
        std::atomic_uint scans{0};
        worker.Submit(oldKey,[oldKey,&scans](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            ++scans;
            auto scan=oldKey;
            scan.analyzed=scan.independentBarAnalysis=scan.barAuthority=true;
            scan.pictureTop=30;scan.pictureBottom=330;
            scan.barEvidence.available=true;
            scan.barEvidence.top=30;scan.barEvidence.bottom=330;
            scan.text.detected=true;scan.text.lineCount=1;
            scan.text.bounds=scan.text.lineBounds[0]={200,300,400,325};
            auto ink=std::make_shared<SubtitleInkSnapshot>();
            ink->width=2;ink->height=2;ink->step=4;
            ink->sourceRows={300,304};
            ink->rawInk={1};ink->ownedInk={1};
            scan.ink=ink;
            scan.subtitleArProof.accepted=true; // Old AR continuity must be discarded.
            return scan;
        });
        SubtitleBoxObservation first;
        Assert::IsTrue(WaitForRead(worker,oldKey,first));
        auto current=oldKey;current.continuityGeneration=22;
        SubtitleBoxObservation ready;
        Assert::IsFalse(worker.TryRead(current,ready),
            L"A continuity mismatch alone does not authorize re-association.");
        Assert::IsTrue(worker.TryRead(current,ready,true),
            L"An explicit backlog-only token may reuse the immutable exact-frame scan.");
        Assert::IsTrue(ready.continuityGeneration==22);
        Assert::IsTrue(ready.text.detected);
        Assert::IsFalse(ready.subtitleArProof.accepted);
        Assert::IsTrue(SubtitleBoxLookahead::CanReuseMeasurement(ready,current.identity,
            current.width,current.height,current.policyGeneration,current.continuityGeneration,
            false,{},current.scriptHint));
        Assert::AreEqual(1u,scans.load());
        auto wrong=current;wrong.identity.analysisDomainKey++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.identity.acceptedSequence++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.policyGeneration++;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
        wrong=current;wrong.discontinuity=true;
        Assert::IsFalse(worker.TryRead(wrong,ready,true));
    }
    TEST_METHOD(BacklogRebindRejectsTrackedChangedOrIncompleteGlyphEvidence) {
        SubtitleBoxObservation scan=WorkerKey(61,0xA11CE);
        scan.analyzed=scan.independentBarAnalysis=scan.barAuthority=true;
        scan.pictureTop=30;scan.pictureBottom=330;
        scan.barEvidence.available=true;scan.barEvidence.top=30;scan.barEvidence.bottom=330;
        scan.text.detected=true;scan.text.lineCount=1;
        scan.ink=std::make_shared<SubtitleInkSnapshot>();
        const auto accepted=scan;
        auto unusedPredecessor=accepted;
        unusedPredecessor.hasBarTrackingReference=true;
        Assert::IsTrue(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
            unusedPredecessor,22,true),
            L"An offered predecessor is harmless when neither edge used it.");
        Assert::IsFalse(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
            scan,22,false));
        for(int scenario=0;scenario<7;++scenario) {
            scan=accepted;
            if(scenario==0)scan.text.detected=false;
            if(scenario==1)scan.text.held=true;
            if(scenario==2)scan.text.workLimit=true;
            if(scenario==3)scan.barTrackingAuthority=true;
            if(scenario==4)scan.barEvidence.bottomRevalidatedAtReference=true;
            if(scenario==5)scan.barEvidence.captionBackedBottom=true;
            if(scenario==6)scan.ink.reset();
            Assert::IsFalse(SubtitleBoxLookahead::RebindCompletedIndependentScanAfterBacklog(
                scan,22,true));
        }
    }
    TEST_METHOD(AcceptedCompleteExtractionSeedsCurrentPixelPendingFallback) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        const auto bars=ExtractSubtitleBarEvidence(Source(pixels));
        Assert::IsTrue(bars.available);Assert::AreEqual(12,bars.top);Assert::AreEqual(78,bars.bottom);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsTrue(pending.current.pendingRefresh);
        Assert::AreEqual(3,pending.current.text.lineCount);
        for(int line=0;line<3;++line)
            Assert::AreEqual(ready.text.lineBounds[line].top,pending.current.text.lineBounds[line].top);
    }
    TEST_METHOD(PendingCurrentInkProofIgnoresMovingMarginsAndRefreshesCurrentSnapshot) {
        auto pixels=PixelsWithCurrentBars();
        SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
        auto ready=ReassociateThreeLineCue(tracker,pixels,accepted,false,20);
        const auto certificate=tracker.AcceptedCueSeedCertificate();
        SubtitlePendingMeasurementGuard guard;
        const bool remembered=guard.RememberAccepted(ready,accepted,Source(pixels),40,250,&certificate);
        if(!remembered)Logger::WriteMessage((std::string("current ink seed: ")+guard.DecisionReason()).c_str());
        Assert::IsTrue(remembered,
            L"A complete current detector scan must seed the bounded raw/owned witness.");
        const auto oldInk=ready.current.ink;
        // These pixels are in the detector's one-step line-box margin, not in
        // the accepted owned glyph mask. Their source background may move.
        pixels[50*160+49]=uint16_t(340<<6);
        pixels[50*160+111]=uint16_t(270<<6);
        // A large uniform picture-background shift also remains below the
        // local-contrast veto; it is not new subtitle ink.
        for(int y=28;y<36;++y)for(int x=0;x<160;++x)
            pixels[y*160+x]=uint16_t(340<<6);
        // A small current antialias change remains within the owned-pixel
        // identity tolerance while proving the current source was re-sampled.
        pixels[50*160+50]=uint16_t(703<<6);
        auto pending=PendingAfterReassociation(6,20);
        const bool resolved=guard.Resolve(pending,Source(pixels),40,250);
        if(!resolved)Logger::WriteMessage((std::string("current ink pending: ")+guard.DecisionReason()).c_str());
        Assert::IsTrue(resolved,
            L"Changed non-owned picture margins must not invalidate an otherwise exact static cue.");
        Assert::IsTrue(pending.current.pendingRefresh,
            L"The fast path must stay an unprocessed pending refresh.");
        Assert::IsTrue(pending.current.ink && pending.current.ink!=oldInk,
            L"Pixel-dependent ink must come from this source, not the accepted source snapshot.");
        Assert::IsTrue(pending.current.ink->paletteValid);
        Assert::IsFalse(pending.current.ink->rawEvidenceComplete,
            L"A continuation witness cannot masquerade as a completed semantic detector scan.");
        Assert::IsTrue(pending.current.ink->Get(50,50,true),
            L"Only current-source pixels that still match the accepted owned glyph may be retained.");
        Assert::IsTrue(guard.DecisionDiagnostics().currentBandSamplesChecked>0);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked>=12);
    }
    TEST_METHOD(IndependentSameCueScansRearmOriginalPendingSeedWithoutBandResampling) {
        auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation firstTracker;SubtitleBoxResult firstAccepted;
        auto first=ReassociateThreeLineCue(firstTracker,pixels,firstAccepted,false,0,1,0);
        const auto firstCertificate=firstTracker.AcceptedCueSeedCertificate();
        Assert::AreEqual(uint64_t(0),first.current.text.cue);
        Assert::AreNotEqual(uint64_t(0),firstAccepted.cue,
            L"The synthetic raw detector source intentionally has no tracker cue ID.");
        SubtitlePendingMeasurementGuard guard;
        Assert::IsTrue(guard.RememberAccepted(first,firstAccepted,Source(pixels),1000.0/60,250,&firstCertificate));
        const auto originalSeed=guard.AcceptedSeedIdentity();
        Assert::AreEqual(uint64_t(5),originalSeed.acceptedSequence);

        // Non-owned current-picture margins move while the complete raw cue,
        // owned glyphs, physical bars and accepted geometry remain unchanged.
        pixels[50*160+49]=uint16_t(340<<6);
        pixels[50*160+111]=uint16_t(270<<6);
        for(uint64_t firstSequence:{uint64_t(6),uint64_t(11),uint64_t(16)}) {
            SubtitleBoxPresentation currentTracker;SubtitleBoxResult currentAccepted;
            auto current=ReassociateThreeLineCue(currentTracker,pixels,currentAccepted,false,0,firstSequence,0);
            const auto& certificate=currentTracker.AcceptedCueSeedCertificate();
            Assert::AreEqual(uint64_t(0),current.current.text.cue);
            Assert::AreNotEqual(uint64_t(0),currentAccepted.cue);
            Assert::IsTrue(certificate.Matches(current,currentAccepted),
                L"Each rearm needs a fresh tracker certificate for this exact complete current scan.");
            const auto started=std::chrono::steady_clock::now();
            Assert::IsTrue(guard.RememberAccepted(current,currentAccepted,Source(pixels),1000.0/60,250,&certificate));
            const double elapsed=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-started).count();
            Logger::WriteMessage((std::string("ready same-cue rearm ms=")+std::to_string(elapsed)+
                " mask_words="+std::to_string(guard.DecisionDiagnostics().readyRearmMaskWordsChecked)+
                " glyph_samples="+std::to_string(guard.DecisionDiagnostics().readyRearmGlyphSamplesChecked)+
                " backing_samples="+std::to_string(guard.DecisionDiagnostics().readyRearmBackingSamplesChecked)+
                " known_samples="+std::to_string(guard.DecisionDiagnostics().readyRearmKnownSamplesChecked)).c_str());
            Assert::AreEqual("accepted-same-cue-current-scan-rearmed",guard.DecisionReason());
            Assert::AreEqual(uint64_t(0),guard.DecisionDiagnostics().seedBandSamplesChecked,
                L"Ready rearm must not reread the full subtitle search bands.");
            Assert::IsTrue(guard.DecisionDiagnostics().readyRearmMaskWordsChecked>0);
            Assert::IsTrue(guard.DecisionDiagnostics().readyRearmGlyphSamplesChecked>0);
            Assert::IsTrue(guard.DecisionDiagnostics().readyRearmBackingSamplesChecked>0);
            Assert::IsTrue(guard.DecisionDiagnostics().readyRearmKnownSamplesChecked>0);
            Assert::AreEqual(originalSeed.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence,
                L"Rearm must retain the first immutable source and pixel baseline.");
            Assert::AreEqual(current.current.identity.acceptedSequence,guard.LastVerifiedIdentity().acceptedSequence);
            Assert::AreEqual(0.0,guard.PendingElapsedMilliseconds(),0.001,
                L"Only this fresh complete positive certificate resets the pending age.");

            Assert::IsFalse(guard.RememberAccepted(current,currentAccepted,Source(pixels),1000.0/60,250,&certificate),
                L"Replaying the same certified source must not rearm twice.");
            Assert::AreEqual("accepted-certificate-already-consumed",guard.DecisionReason());
            Assert::AreEqual(current.current.identity.acceptedSequence,guard.LastVerifiedIdentity().acceptedSequence);
            Assert::AreEqual(0.0,guard.PendingElapsedMilliseconds(),0.001);
        }

        // A pending frame still checks against the original seed after several
        // fresh ready re-arms; it cannot replace that seed or extend its age.
        auto pending=PendingAfterReassociation(21);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),1000.0/60,250));
        Assert::IsTrue(pending.current.pendingRefresh);
        Assert::AreEqual(originalSeed.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(uint64_t(21),guard.LastVerifiedIdentity().acceptedSequence);
        Assert::IsTrue(guard.PendingElapsedMilliseconds()>0);
    }
    TEST_METHOD(ReadySameCueRearmFallsBackToFreshSeedOnChangedInkBackingOrContext) {
        for(int scenario=0;scenario<4;++scenario) {
            auto seedPixels=PixelsWithCurrentBars();SubtitleBoxPresentation seedTracker;SubtitleBoxResult seedAccepted;
            auto seed=ReassociateThreeLineCue(seedTracker,seedPixels,seedAccepted,false,0,1);
            const auto seedCertificate=seedTracker.AcceptedCueSeedCertificate();SubtitlePendingMeasurementGuard guard;
            Assert::IsTrue(guard.RememberAccepted(seed,seedAccepted,Source(seedPixels),1000.0/60,250,&seedCertificate));
            const auto oldIdentity=guard.AcceptedSeedIdentity();

            auto currentPixels=PixelsWithCurrentBars();
            if(scenario==0)currentPixels[50*160+50]=uint16_t(300<<6); // Acquired owned glyph disappears.
            if(scenario==1)currentPixels[48*160+41]=uint16_t(64<<6); // Existing panel backing changes black state.
            if(scenario==2)for(int y=38;y<44;++y)for(int x=20;x<44;++x)
                if((x%7)<3)currentPixels[y*160+x]=uint16_t(700<<6); // A new companion line enters the complete raw mask.
            SubtitleBoxPresentation currentTracker;SubtitleBoxResult currentAccepted;
            auto current=ReassociateThreeLineCue(currentTracker,currentPixels,currentAccepted,false,0,6);
            if(scenario==3) {
                ++current.current.identity.analysisDomainKey;
                current.current.sharedPicture.identity=current.current.identity;
            }
            const auto& certificate=currentTracker.AcceptedCueSeedCertificate();
            const bool certified=certificate.Matches(current,currentAccepted);
            const bool remembered=guard.RememberAccepted(current,currentAccepted,Source(currentPixels),1000.0/60,250,
                certified?&certificate:nullptr);
            const char* scenarioName=scenario==0?"glyph-changed":scenario==1?"backing-changed":
                scenario==2?"new-raw-line":"foreign-domain";
            if(!remembered)Logger::WriteMessage((std::string("ready rearm fallback scenario=")+scenarioName+
                " certified="+std::to_string(certified)+" reason="+guard.DecisionReason()).c_str());
            if(scenario==3) {
                Assert::IsFalse(remembered,
                    L"An incomplete grouping from a foreign physical-analysis domain cannot create a seed or rearm.");
                Assert::AreNotEqual("accepted-same-cue-current-scan-rearmed",guard.DecisionReason());
                Assert::AreEqual(uint64_t(0),guard.AcceptedSeedIdentity().acceptedSequence,
                    L"The rejected foreign context leaves no stale or false current seed.");
                Assert::AreEqual(uint64_t(0),guard.LastVerifiedIdentity().acceptedSequence);
                Assert::AreEqual(0.0,guard.PendingElapsedMilliseconds(),0.001);
                continue;
            }
            Assert::IsTrue(remembered,
                L"A complete positive current scan may establish a fresh baseline after rearm is rejected.");
            Assert::AreNotEqual("accepted-same-cue-current-scan-rearmed",guard.DecisionReason(),
                L"Changed glyph, backing, raw ink or context cannot reuse the immutable seed through the rearm shortcut.");
            Assert::IsTrue(guard.DecisionDiagnostics().seedBandSamplesChecked>0,
                L"A rejected rearm must go through complete current-source seeding.");
            Assert::AreEqual(current.current.identity.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence);
            Assert::AreNotEqual(oldIdentity.acceptedSequence,guard.AcceptedSeedIdentity().acceptedSequence);
        }
    }
    TEST_METHOD(PendingCurrentInkProofRejectsChangedGlyphAndNovelCompanion) {
        for(int scenario=0;scenario<6;++scenario) {
            auto pixels=PixelsWithCurrentBars();
            if(scenario==4)pixels[50*160+108]=uint16_t(420<<6);
            SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            auto ready=ReassociateThreeLineCue(tracker,pixels,accepted,false,20);
            const auto certificate=tracker.AcceptedCueSeedCertificate();
            SubtitlePendingMeasurementGuard guard;
            const bool remembered=guard.RememberAccepted(ready,accepted,Source(pixels),40,250,&certificate);
            if(!remembered)Logger::WriteMessage((std::string("current ink negative seed: ")+guard.DecisionReason()).c_str());
            Assert::IsTrue(remembered);
            if(scenario==0) {
                pixels[50*160+50]=uint16_t(300<<6);
            } else if(scenario==1) {
                // A new companion row is outside every accepted line rectangle
                // but inside the complete sampled subtitle-search band.
                for(int y=38;y<44;++y)for(int x=20;x<44;++x)
                    if((x%7)<3)pixels[y*160+x]=uint16_t(700<<6);
            } else if(scenario==2) {
                // A lower-luma, alternate-chroma companion must not disappear
                // merely because the accepted cue's palette cannot classify it.
                const size_t chromaBase=size_t(160)*90;
                for(int y=38;y<44;++y)for(int x=20;x<44;++x) {
                    const size_t uv=chromaBase+size_t(y/2)*160+size_t(x/2)*2;
                    pixels[uv]=uint16_t(800<<6);pixels[uv+1]=uint16_t(512<<6);
                }
            } else if(scenario==3) {
                // One tiny alternate-chroma mark immediately beside the old
                // glyph is enough to invalidate held cleanup. It must not be
                // hidden by the old-mask neighborhood or a component-size rule.
                const size_t uv=size_t(160)*90+size_t(50/2)*160+size_t(110/2)*2;
                pixels[uv]=uint16_t(800<<6);pixels[uv+1]=uint16_t(512<<6);
            } else if(scenario==4) {
                // A same-palette low-luma point was quiet at seed time. A new
                // strong local edge at that exact fringe point must invalidate
                // the witness even though it remains below inkFloor.
                pixels[50*160+108]=uint16_t(470<<6);
            } else {
                // A tiny alternate-chroma mark immediately inside the active
                // picture's lower bar seam must be detected by same-side
                // evidence; the unchanged picture/bar luminance step itself
                // must not look like a new subtitle.
                const size_t uv=size_t(160)*90+size_t(76/2)*160+size_t(20/2)*2;
                pixels[uv]=uint16_t(800<<6);pixels[uv+1]=uint16_t(512<<6);
            }
            auto pending=PendingAfterReassociation(6,20);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250),
                L"The current witness must reject lost accepted glyphs and any new raw subtitle ink outside the template.");
            Assert::IsFalse(pending.available);
            const char* expected=scenario==0?"current-owned-glyph-changed":
                scenario==1?"new-current-raw-ink":scenario==4?"current-known-ink-changed":
                    "new-current-contrast-ink";
            if(scenario==0)Assert::IsTrue(std::strcmp(guard.DecisionReason(),"current-owned-glyph-changed")==0 ||
                std::strcmp(guard.DecisionReason(),"current-known-ink-changed")==0);
            else Assert::AreEqual(expected,guard.DecisionReason());
        }
    }
    TEST_METHOD(PendingCurrentInkProofRejectsChangedBackingSourceContextAndUnavailablePixels) {
        for(int scenario=0;scenario<4;++scenario) {
            auto pixels=PixelsWithCurrentBars();
            if(scenario==3)pixels[50*160+49]=uint16_t(500<<6);
            SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            auto ready=ReassociateThreeLineCue(tracker,pixels,accepted,scenario==3,20);
            const auto certificate=tracker.AcceptedCueSeedCertificate();
            SubtitlePendingMeasurementGuard guard;
            const bool remembered=guard.RememberAccepted(ready,accepted,Source(pixels),40,250,&certificate);
            if(!remembered)Logger::WriteMessage((std::string("current ink context seed: ")+guard.DecisionReason()).c_str());
            Assert::IsTrue(remembered);
            auto current=Source(pixels);
            if(scenario==0) {
                // Still a valid physical bar, but a changed black-card support
                // sample inside the remembered cleanup panel.
                pixels[79*160+41]=uint16_t(300<<6);
                current=Source(pixels);
            } else if(scenario==1) ++current.generation;
            else if(scenario==2)current.data=nullptr;
            else {
                const size_t uv=size_t(160)*90+size_t(50/2)*160+size_t(49/2)*2;
                pixels[uv]=uint16_t(800<<6);pixels[uv+1]=uint16_t(512<<6);
                current=Source(pixels);
            }
            auto pending=PendingAfterReassociation(6,20);
            Assert::IsFalse(guard.Resolve(pending,current,40,250));
            Assert::IsFalse(pending.available);
            const char* expected=scenario==0?"current-black-backing-changed":
                scenario==1?"current-source-layout-changed":
                    scenario==3?"current-known-ink-changed":"pending-context-changed";
            Assert::AreEqual(expected,guard.DecisionReason());
            if(scenario==2)Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextSource),
                guard.DecisionDiagnostics().contextMismatch);
        }
    }
    struct HistoryConfirmedSeedWorkspace {
        std::vector<uint16_t> pixels=PixelsWithCurrentBars();
        std::array<SubtitleBoxObservation,6> frames;
        SubtitleBoxPreview preview;
        SubtitleBoxResult accepted;
        SubtitleBoxPresentation tracker;
        SubtitlePendingMeasurementGuard guard;
    };
    static std::unique_ptr<HistoryConfirmedSeedWorkspace> HistoryConfirmedSeed() {
        auto storage=std::make_unique<HistoryConfirmedSeedWorkspace>();auto& w=*storage;
        for(size_t i=0;i<w.frames.size();++i)w.frames[i]=CompleteThreeLineScan(9322+i,w.pixels);
        for(size_t i=0;i<3;++i) {
            SubtitleBoxLookahead::ResolveInto(w.preview,&w.frames[i],1,11,21);
            w.tracker.Consume(w.preview,250,1000.0/59.94);
        }
        SubtitleBoxLookahead::ResolveInto(w.preview,w.frames.data()+3,3,11,21);
        Assert::AreEqual(3,w.preview.current.text.lineCount);
        Assert::IsTrue(w.preview.text.lineCount<3,
            L"The caller retains the genuine short-horizon pruned grouping; do not fabricate three resolved rows.");
        w.accepted=w.tracker.Consume(w.preview,250,1000.0/59.94);
        Assert::IsTrue(w.accepted.detected && !w.accepted.held && !w.accepted.confirmationPending);
        Assert::AreEqual(3,w.accepted.lineCount,
            L"Four independently measured raw sources establish all current rows despite short future grouping.");
        return storage;
    }
    TEST_METHOD(HistoryConfirmedThreeLineCueExportsPendingSeedWithoutBorrowedFutureGeometry) {
        auto storage=HistoryConfirmedSeed();auto& w=*storage;const double frameMs=1000.0/59.94;
        const auto& certificate=w.tracker.AcceptedCueSeedCertificate();
        Assert::IsTrue(certificate.Matches(w.preview,w.accepted),
            L"Strong current acquisition confirmed by exact raw history must export its current-only seed proof, just as strong backlog reassociation does.");
        Assert::IsTrue(w.guard.RememberAccepted(w.preview,w.accepted,Source(w.pixels),frameMs,250,&certificate));
        const auto original=w.preview.current.identity;
        auto pending=Pending(original.acceptedSequence+6);
        pending.continuityGeneration=pending.current.continuityGeneration=26;
        const bool historyPendingResolved=w.guard.Resolve(pending,Source(w.pixels),frameMs,250,&original,true);
        if(!historyPendingResolved)Logger::WriteMessage((std::string("history pending proof: ")+w.guard.DecisionReason()).c_str());
        Assert::IsTrue(historyPendingResolved,
            L"Authorized queue-only retirement must verify unchanged current pixels from the original acquired three-line seed.");
        Assert::AreEqual(3,pending.current.text.lineCount);
        for(int line=0;line<3;++line) {
            Assert::AreEqual(w.accepted.lineBounds[line].top,pending.current.text.lineBounds[line].top);
            Assert::AreEqual(w.accepted.lineBounds[line].bottom,pending.current.text.lineBounds[line].bottom);
        }
        Assert::AreEqual(6*frameMs,w.guard.PendingElapsedMilliseconds(),0.001);
        const auto last=w.guard.LastVerifiedIdentity();const auto elapsed=w.guard.PendingElapsedMilliseconds();
        Assert::IsFalse(w.guard.RememberAccepted(w.preview,w.accepted,Source(w.pixels),frameMs,250,&certificate),
            L"Replaying the same acquisition certificate cannot restart or renew its original pending lease.");
        Assert::AreEqual(last.acceptedSequence,w.guard.LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(elapsed,w.guard.PendingElapsedMilliseconds(),0.001);
        auto expired=Pending(original.acceptedSequence+16);
        expired.continuityGeneration=expired.current.continuityGeneration=26;
        Assert::IsFalse(w.guard.Resolve(expired,Source(w.pixels),frameMs,250),
            L"The original source-age limit still applies after queue reassociation.");
    }
    TEST_METHOD(CurrentInkSeedSurvivesAuthorizedHeldAndCadenceContinuationsWithoutRenewal) {
        struct Workspace {
            std::vector<uint16_t> pixels=PixelsWithCurrentBars(),cadencePixels=PixelsWithCurrentBars();
            SubtitleBoxPresentation tracker,cadenceTracker;
            SubtitleBoxResult accepted,held,cadenceAccepted;
            SubtitleBoxPreview preview,heldPreview,cadencePreview,cadenceAnalysis,skipped,afterHeld,heldExpired,afterCadence;
            std::array<SubtitleBoxObservation,2> heldScans;
            SubtitlePendingMeasurementGuard guard,invalidHeld,cadenceGuard,invalidCadence;
            SubtitleCadenceGuard cadence;
            SubtitleCutPasteGeometry geometry;
            SubtitleCadenceProof proof,badProof;
        };
        auto storage=std::make_unique<Workspace>();auto& w=*storage;
        const double frameMs=40.0;
        w.preview=ReassociateThreeLineCue(w.tracker,w.pixels,w.accepted,false,20);
        const auto original=w.preview.current.identity;
        const auto certificate=w.tracker.AcceptedCueSeedCertificate();
        Assert::IsTrue(w.guard.RememberAccepted(w.preview,w.accepted,Source(w.pixels),frameMs,250,&certificate));

        // A current complete source can produce an accepted held pose when its
        // lookahead grouping is pruned. Its current prepared certificate may
        // preserve, but cannot advance or renew, the original cue seed.
        w.heldScans={CompleteThreeLineScan(original.acceptedSequence+1,w.pixels,22,20),
            CompleteThreeLineScan(original.acceptedSequence+2,w.pixels,22,20)};
        w.heldPreview=SubtitleBoxLookahead::Resolve(w.heldScans.data(),w.heldScans.size(),11,22);
        w.heldPreview.text.lineCount=1;w.heldPreview.text.lineBounds[1]={};w.heldPreview.text.lineBounds[2]={};
        w.heldPreview.currentLinesConfirmed=false;w.heldPreview.matchingFrames=1;
        w.held=w.tracker.Consume(w.heldPreview,250,frameMs);
        Assert::IsTrue(w.held.detected && w.held.held,
            L"A pruned current grouping must exercise the held-cue continuation path.");
        const auto& heldCertificate=w.tracker.CurrentPreparedCueCertificate();
        Assert::IsTrue(heldCertificate.Matches(w.heldPreview.current,w.held,w.heldPreview.current.sharedPicture.bounds));
        Assert::IsTrue(w.invalidHeld.RememberAccepted(w.preview,w.accepted,Source(w.pixels),frameMs,250,&certificate));
        auto wrongBase=w.heldPreview.current.sharedPicture.bounds;++wrongBase.bottom;
        Assert::IsFalse(w.invalidHeld.RememberAccepted(w.heldPreview,w.held,Source(w.pixels),frameMs,250,nullptr,
            &heldCertificate,&wrongBase));
        Assert::AreEqual("held-current-seed-unqualified",w.invalidHeld.DecisionReason());
        w.afterHeld=PendingAfterReassociation(original.acceptedSequence+2,20);
        Assert::IsFalse(w.invalidHeld.Resolve(w.afterHeld,Source(w.pixels),frameMs,250));

        Assert::IsFalse(w.guard.RememberAccepted(w.heldPreview,w.held,Source(w.pixels),frameMs,250,nullptr,
            &heldCertificate,&w.heldPreview.current.sharedPicture.bounds));
        Assert::AreEqual("held-current-proof-preserves-original-seed",w.guard.DecisionReason());
        Assert::AreEqual(original.acceptedSequence,w.guard.AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(original.acceptedSequence,w.guard.LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(0.0,w.guard.PendingElapsedMilliseconds(),0.001);
        w.afterHeld=PendingAfterReassociation(original.acceptedSequence+6,20);
        Assert::IsTrue(w.guard.Resolve(w.afterHeld,Source(w.pixels),frameMs,250));
        Assert::AreEqual(6*frameMs,w.guard.PendingElapsedMilliseconds(),0.001);
        Assert::AreEqual(original.acceptedSequence,w.guard.AcceptedSeedIdentity().acceptedSequence);
        w.heldExpired=PendingAfterReassociation(original.acceptedSequence+7,20);
        Assert::IsFalse(w.guard.Resolve(w.heldExpired,Source(w.pixels),frameMs,250));
        Assert::AreEqual("pending-budget-expired",w.guard.DecisionReason());

        // A cadence proof is a distinct exact-source advance. It must work with
        // the new bounded raw/owned seed while preserving its source and lease.
        w.cadencePreview=ReassociateThreeLineCue(w.cadenceTracker,w.cadencePixels,w.cadenceAccepted,false,20);
        const auto cadenceOriginal=w.cadencePreview.current.identity;
        const auto cadenceCertificate=w.cadenceTracker.AcceptedCueSeedCertificate();
        Assert::IsTrue(w.cadenceGuard.RememberAccepted(w.cadencePreview,w.cadenceAccepted,Source(w.cadencePixels),
            frameMs,250,&cadenceCertificate));
        w.geometry.valid=true;w.geometry.content=w.cadenceAccepted.bounds;
        w.geometry.glyphLines=w.cadenceAccepted.lineBounds;
        w.geometry.source=w.geometry.destination=w.geometry.generatedCleanup=w.geometry.pictureCapture=
            w.cadenceAccepted.sourcePanel;
        w.geometry.pictureTop=12;w.geometry.pictureBottom=78;
        w.cadenceAnalysis=w.cadencePreview;w.cadenceAnalysis.current.analysisStride=2;
        const auto cadenceSource=CadenceSource(w.cadencePixels);
        const bool cadenceSeeded=w.cadence.RememberAccepted(w.cadenceAnalysis,w.cadenceAccepted,cadenceSource,
            w.cadencePreview.current.sharedPicture.bounds,w.geometry,frameMs,250);
        if(!cadenceSeeded)Logger::WriteMessage((std::string("cadence seed: ")+w.cadence.DecisionReason()).c_str());
        Assert::IsTrue(cadenceSeeded);
        w.skipped=PendingAfterReassociation(cadenceOriginal.acceptedSequence+1,20);
        w.skipped.current.cadenceSkipped=true;w.skipped.current.analysisStride=2;
        w.proof=w.cadence.PrepareReference(w.skipped,cadenceSource,
            w.cadencePreview.current.sharedPicture.bounds,frameMs,250);
        Assert::IsTrue(w.proof.valid);
        Assert::IsTrue(w.invalidCadence.RememberAccepted(w.cadencePreview,w.cadenceAccepted,Source(w.cadencePixels),
            frameMs,250,&cadenceCertificate));
        w.badProof=w.proof;w.badProof.valid=false;
        Assert::IsFalse(w.invalidCadence.AdvanceVerifiedCadence(w.badProof,w.proof.continuation.accepted,frameMs,250));
        Assert::AreEqual("cadence-advance-context-changed",w.invalidCadence.DecisionReason());
        Assert::IsTrue(w.cadenceGuard.AdvanceVerifiedCadence(w.proof,w.proof.continuation.accepted,frameMs,250));
        Assert::AreEqual(cadenceOriginal.acceptedSequence,w.cadenceGuard.AcceptedSeedIdentity().acceptedSequence);
        Assert::AreEqual(w.skipped.current.identity.acceptedSequence,w.cadenceGuard.LastVerifiedIdentity().acceptedSequence);
        Assert::AreEqual(frameMs,w.cadenceGuard.PendingElapsedMilliseconds(),0.001);
        w.afterCadence=PendingAfterReassociation(cadenceOriginal.acceptedSequence+2,20);
        Assert::IsTrue(w.cadenceGuard.Resolve(w.afterCadence,Source(w.cadencePixels),frameMs,250));
        Assert::AreEqual(2*frameMs,w.cadenceGuard.PendingElapsedMilliseconds(),0.001);
        Assert::AreEqual(cadenceOriginal.acceptedSequence,w.cadenceGuard.AcceptedSeedIdentity().acceptedSequence);
    }
    TEST_METHOD(HistoryConfirmedPendingSeedRejectsForeignPartialHeldAndReusedProof) {
        for(int fault=0;fault<10;++fault) {
            auto storage=HistoryConfirmedSeed();auto& w=*storage;
            const auto certificate=w.tracker.AcceptedCueSeedCertificate();
            if(fault==1)++w.preview.current.identity.analysisDomainKey;
            if(fault==2)++w.preview.policyGeneration;
            if(fault==3)++w.preview.continuityGeneration;
            if(fault==4) {w.preview.current.text.lineCount=2;w.preview.current.text.lineBounds[2]={};}
            if(fault==5)w.accepted.held=true;
            if(fault==6)w.accepted.revised=true;
            if(fault==7)w.preview.current.text.workLimit=true;
            if(fault==8)w.preview.current.pendingRefresh=true;
            if(fault==9)w.preview.current.ink=std::make_shared<SubtitleInkSnapshot>(*w.preview.current.ink);
            Assert::IsFalse(w.guard.RememberAccepted(w.preview,w.accepted,Source(w.pixels),1000.0/59.94,250,
                fault==0?nullptr:&certificate),
                L"No missing, foreign, partial, held or changed-ownership proof may seed a new fallback lease.");
            Assert::AreEqual(uint64_t(0),w.guard.LastVerifiedIdentity().acceptedSequence);
        }
    }
    TEST_METHOD(CompletedThreeLineBacklogScanSeedsNextPendingOnlyWithTrackerCertificate) {
        auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
        const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250));
        auto missing=PendingAfterReassociation(6);
        Assert::IsFalse(guard.Resolve(missing,Source(pixels),40,250),L"Reproduces the old no-accepted-seed loss.");
        Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()));
        Assert::AreEqual("accepted-existing-cue-certified",guard.DecisionReason());
        auto pending=PendingAfterReassociation(6);
        const bool pendingResolved=guard.Resolve(pending,Source(pixels),40,250);
        if(!pendingResolved)Logger::WriteMessage((std::string("certificate pending proof: ")+guard.DecisionReason()).c_str());
        Assert::IsTrue(pendingResolved);
        Assert::IsTrue(pending.current.ink && pending.current.ink->Get(50,52,true),
            L"The exact current pixels for an accepted glyph outside the refreshed bar-search band remain available for composition.");
        Assert::IsFalse(pending.current.ink->rawEvidenceComplete,
            L"Those accepted source pixels do not become a fresh detector scan or current semantic proof.");
        auto changedPixels=PixelsWithCurrentBars();changedPixels[52*160+50]=uint16_t(300<<6);
        SubtitlePendingMeasurementGuard changedGuard;
        Assert::IsTrue(changedGuard.RememberAccepted(preview,accepted,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()));
        auto changedPending=PendingAfterReassociation(6);
        Assert::IsFalse(changedGuard.Resolve(changedPending,Source(changedPixels),40,250),
            L"A changed accepted glyph sample outside the current search band must still fail exact current-pixel verification.");
        Assert::AreEqual("current-known-ink-changed",changedGuard.DecisionReason());
        const auto retained=tracker.Consume(pending,250,40);
        Assert::IsTrue(retained.detected);Assert::AreEqual(3,retained.lineCount);
        Assert::AreEqual(accepted.cue,retained.cue);
        for(int line=0;line<3;++line)
            Assert::AreEqual(accepted.lineBounds[line].top,retained.lineBounds[line].top);
    }
    TEST_METHOD(UnresolvedFreshThreeLineCueCannotMintPendingSeedCertificate) {
        auto pixels=PixelsWithCurrentBars();auto current=CompleteThreeLineScan(1,pixels);
        const auto preview=SubtitleBoxLookahead::Resolve(&current,1,11,21);
        SubtitleBoxPresentation tracker;const auto accepted=tracker.Consume(preview);
        Assert::IsFalse(accepted.detected);
        SubtitlePendingMeasurementGuard guard;
        Assert::IsFalse(guard.RememberAccepted(preview,current.text,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()));
        const SubtitleAcceptedCueSeedCertificate empty;
        Assert::IsFalse(guard.RememberAccepted(preview,current.text,Source(pixels),40,250,&empty));
    }
    TEST_METHOD(CertifiedSeedRejectsPartialRawChangedInkContextAndHeldRenewal) {
        for(int defect=0;defect<23;++defect) {
            auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
            const auto certificate=tracker.AcceptedCueSeedCertificate();
            if(defect==0)preview.current.text.lineCount=2;
            if(defect==1)preview.current.text.capturePanel.top=60;
            if(defect==2)preview.current.identity.acceptedSequence++;
            if(defect==3)preview.current.identity.transportGeneration++;
            if(defect==4)preview.current.identity.sourceFormatGeneration++;
            if(defect==5)preview.current.identity.analysisDomainKey++;
            if(defect==6)preview.current.identity.viewportGeneration++;
            if(defect==7)preview.current.identity.rendererGeneration++;
            if(defect==8)preview.policyGeneration++;
            if(defect==9)preview.continuityGeneration++;
            if(defect==10)preview.current.pendingRefresh=true;
            if(defect==11)preview.current.analysisRefresh=true;
            if(defect==12)preview.current.discontinuity=true;
            if(defect==13)preview.current.independentBarAnalysis=false;
            if(defect==14)preview.current.text.workLimit=true;
            if(defect==15)accepted.held=true;
            if(defect==16)accepted.revised=true;
            if(defect==17)accepted.lineBounds[0].top++;
            if(defect==18)preview.current.pictureBottom++;
            if(defect==19)preview.current.barEvidence.bottomRevalidatedAtReference=true;
            if(defect==20)preview.current.barEvidence.captionBackedBottom=true;
            if(defect==21)preview.current.scriptHint=SubtitleScriptHint::Arabic;
            if(defect==22) {
                auto changed=std::make_shared<SubtitleInkSnapshot>(*preview.current.ink);
                changed->rawInk.assign(changed->rawInk.size(),0);preview.current.ink=changed;
            }
            SubtitlePendingMeasurementGuard guard;
            Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
            auto pending=PendingAfterReassociation(6);Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        }
    }
    TEST_METHOD(CertifiedSeedCannotHoldRemovedLineCueEndOrLostPhysicalBar) {
        for(int change=0;change<5;++change) {
            auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
            const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
            SubtitlePendingMeasurementGuard guard;
            Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
                &tracker.AcceptedCueSeedCertificate()));
            if(change<3)for(int y=50+12*change;y<56+12*change;++y)for(int x=50;x<110;++x)
                pixels[y*160+x]=uint16_t((y<78?300:64)<<6);
            if(change==3)std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
            if(change==4)for(int y=0;y<90;++y)for(int x=0;x<160;++x)
                if(y<12 || y>=78)pixels[y*160+x]=uint16_t(300<<6);
            auto pending=PendingAfterReassociation(6);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(pending.available);
        }
    }
    TEST_METHOD(ExportedCertificateCannotRenewPendingBudgetOrSurviveRepeatedConsume) {
        auto pixels=PixelsWithCurrentBars();SubtitleBoxPresentation tracker;SubtitleBoxResult accepted;
        const auto preview=ReassociateThreeLineCue(tracker,pixels,accepted);
        const auto certificate=tracker.AcceptedCueSeedCertificate();SubtitlePendingMeasurementGuard guard;
        Assert::IsTrue(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        tracker.Consume(preview,250,40);
        Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,
            &tracker.AcceptedCueSeedCertificate()),L"A repeated presentation must neither clear nor renew its seed.");
        for(uint64_t sequence=6;sequence<=11;++sequence) {
            auto pending=PendingAfterReassociation(sequence);
            const bool pendingResolved=guard.Resolve(pending,Source(pixels),40,250);
            if(!pendingResolved)Logger::WriteMessage((std::string("renewal pending proof sequence ")+std::to_string(sequence)+": "+guard.DecisionReason()).c_str());
            Assert::IsTrue(pendingResolved);
            Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        }
        auto expired=PendingAfterReassociation(12);Assert::IsFalse(guard.Resolve(expired,Source(pixels),40,250));
        Assert::IsFalse(guard.RememberAccepted(preview,accepted,Source(pixels),40,250,&certificate));
        tracker.Consume(preview,250,40);
        Assert::IsFalse(tracker.AcceptedCueSeedCertificate().Matches(preview,accepted));
    }
    TEST_METHOD(PartialDetectionPruningAndNarrowCaptureCannotSeedSmallerFallback) {
        for(int scenario=0;scenario<4;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto initial=Ready(1);
            Assert::IsTrue(guard.RememberAccepted(initial,initial.text,Source(pixels),40,250));
            auto partial=Ready(2);const auto accepted=partial.text;
            if(scenario==0)partial.current.text.lineCount=2;
            if(scenario==1)partial.text.lineCount=2;
            if(scenario==2)partial.current.text.capturePanel.top=60;
            if(scenario==3)partial.text.capturePanel.top=60;
            Assert::IsFalse(guard.RememberAccepted(partial,accepted,Source(pixels),40,250));
            auto pending=Pending(3);
            Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
            Assert::IsFalse(pending.available);
        }
    }
    TEST_METHOD(PendingSeedNeverPromotesUnacceptedDetectorRows) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        auto accepted=ready.text;accepted.lineCount=2;
        accepted.lineBounds[2]={};accepted.lineCapturePanels[2]={};accepted.linePanels[2]={};
        Assert::IsTrue(guard.RememberAccepted(ready,accepted,Source(pixels),40,250));
        auto pending=Pending(2);
        Assert::IsTrue(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual(2,pending.current.text.lineCount);
        Assert::IsFalse(pending.current.text.lineBounds[2].Valid());
    }
    TEST_METHOD(PendingFallbackRejectsChangedAnalysisDomainWithIdenticalPixels) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.identity.analysisDomainKey=11;
        ready.current.sharedPicture.required=false;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        pending.current.identity.analysisDomainKey=12;
        pending.current.sharedPicture.required=false;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("pending-context-changed",guard.DecisionReason());
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextDomain),
            guard.DecisionDiagnostics().contextMismatch);
        Assert::IsFalse(pending.available);
    }

    TEST_METHOD(PendingQueueBridgeRequiresCurrentNativePixelsAndClearsNonSubtitleAuthority) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.subtitleArProof.accepted=true;
        ready.current.subtitleArDarkness.global.nearBlack=true;
        ready.futureAvailable=true;ready.followingCount=1;
        ready.following[0]=ready.current;ready.matchingFrames=3;
        ready.currentLinesConfirmed=true;ready.newScanFrames=2;ready.newScanMs=17;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(3);pending.current.continuityGeneration=22;pending.continuityGeneration=22;
        const auto token=ready.current.identity;
        Assert::IsTrue(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
        Assert::IsTrue(pending.available && pending.current.pendingRefresh);
        Assert::IsTrue(SameAnalysisFrameIdentity(Identity(3),pending.current.identity));
        Assert::AreEqual(uint64_t(22),pending.continuityGeneration);
        Assert::AreEqual(uint64_t(22),pending.current.continuityGeneration);
        Assert::AreEqual(ready.text.cue,pending.text.cue);
        Assert::AreEqual(3,pending.text.lineCount);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked>0);
        Assert::IsTrue(guard.DecisionDiagnostics().barsAvailable);
        Assert::IsFalse(pending.current.subtitleArProof.accepted);
        Assert::IsFalse(pending.current.subtitleArDarkness.global.nearBlack);
        Assert::IsFalse(pending.current.barTrackingAuthority);
        Assert::IsFalse(pending.current.currentAnchorUsesTrackedEdge);
        Assert::IsFalse(pending.futureAvailable || pending.currentLinesConfirmed);
        Assert::AreEqual(0u,pending.followingCount);
        Assert::AreEqual(uint64_t(0),pending.following[0].identity.acceptedSequence);
        Assert::AreEqual(1u,pending.matchingFrames);
        Assert::AreEqual(0u,pending.newScanFrames);
        Assert::AreEqual(0.0,pending.newScanMs);
    }
    TEST_METHOD(PendingQueueBridgeNeverRelaxesOtherContextOrTokenRequirements) {
        for(int scenario=0;scenario<22;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            auto pending=Pending(3);pending.current.continuityGeneration=22;pending.continuityGeneration=22;
            auto token=ready.current.identity;const ActivePictureFrameIdentity* tokenPointer=&token;
            bool queueOnly=true;auto source=Source(pixels);
            switch(scenario) {
            case 0: pending.current.discontinuity=true;break;
            case 1: ++pending.current.identity.transportGeneration;break;
            case 2: ++pending.current.identity.sourceFormatGeneration;break;
            case 3: ++pending.current.identity.analysisDomainKey;break;
            case 4: ++pending.current.identity.viewportGeneration;break;
            case 5: ++pending.current.identity.rendererGeneration;break;
            case 6: ++pending.current.policyGeneration;++pending.policyGeneration;break;
            case 7: ++pending.current.nearBarDistance;break;
            case 8: pending.current.scriptHint=SubtitleScriptHint::Arabic;break;
            case 9: --pending.current.width;break;
            case 10: source.data=nullptr;break;
            case 11: queueOnly=false;break;
            case 12: tokenPointer=nullptr;break;
            case 13: --token.acceptedSequence;break;
            case 14: ++token.analysisDomainKey;break;
            case 15: ++pending.policyGeneration;break;
            case 16: ++pending.continuityGeneration;break;
            case 17: pending.current.continuityGeneration=pending.continuityGeneration=20;break;
            case 18: pending.current.continuityGeneration=pending.continuityGeneration=0;break;
            case 19: ++pending.current.optimizationMode;break;
            case 20: pending.current.identity.captureTimestamp=token.captureTimestamp;break;
            case 21: ++pending.current.identity.sourceFrameNumber;break;
            }
            Assert::IsFalse(ResolvePendingQueue(guard,pending,source,40,250,tokenPointer,queueOnly,0));
            Assert::IsFalse(pending.available);
            Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
        }
    }
    TEST_METHOD(PendingQueueBridgeStillRejectsChangedGlyphChromaAndPhysicalBars) {
        for(int scenario=0;scenario<3;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            ready.current.sharedPicture.required=false;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            if(scenario==0)pixels[50*160+50]=uint16_t(1000<<6);
            if(scenario==1)pixels[160*90+25*160+50]=uint16_t(700<<6);
            if(scenario==2)for(int y=0;y<12;++y)for(int x=0;x<160;++x)
                pixels[y*160+x]=uint16_t(300<<6);
            auto pending=Pending(3);pending.current.sharedPicture.required=false;
            pending.current.continuityGeneration=pending.continuityGeneration=22;
            const auto token=ready.current.identity;
            Assert::IsFalse(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
            Assert::IsFalse(pending.available);
            Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked>0,
                L"Queue reassociation must reach current native proof, not bypass it.");
            Assert::AreEqual(scenario==2?"current-bars-changed":"current-pixels-changed",guard.DecisionReason());
        }
    }
    TEST_METHOD(PendingQueueBridgeRequiresSeedAndStrictlyForwardSource) {
        for(int scenario=0;scenario<3;++scenario) {
            auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
            if(scenario!=0)Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
            auto pending=Pending(scenario==0?3:scenario==1?1:0);
            pending.current.continuityGeneration=pending.continuityGeneration=22;
            const auto token=ready.current.identity;
            Assert::IsFalse(ResolvePendingQueue(guard,pending,Source(pixels),40,250,&token,true,0));
            Assert::IsFalse(pending.available);
            Assert::AreEqual(scenario==0?"no-accepted-seed":"non-forward-source",guard.DecisionReason());
        }
    }
    TEST_METHOD(PendingQueueBridgeTokenMustNameLastValidatedPendingFrameNotOriginalSeed) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        const auto original=ready.current.identity;
        auto first=Pending(3);first.current.continuityGeneration=first.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,first,Source(pixels),40,250,&original,true,0));
        auto next=Pending(5);next.current.continuityGeneration=next.continuityGeneration=23;
        Assert::IsFalse(ResolvePendingQueue(guard,next,Source(pixels),40,250,&original,true,0));
        Assert::IsFalse(next.available);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
    }
    TEST_METHOD(PendingQueueBridgePreservesEntireLeaseAcrossSuccessiveRetirements) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),50,250));
        auto token=ready.current.identity;
        auto first=Pending(3);first.current.continuityGeneration=first.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,first,Source(pixels),50,250,&token,true,0)); // 100ms
        // Re-presenting recovered output cannot reseed or refresh its lease.
        Assert::IsFalse(ResolvePendingQueue(guard,first,Source(pixels),50,250,&token,true,0));
        auto middle=Pending(4);middle.current.continuityGeneration=middle.continuityGeneration=22;
        Assert::IsTrue(ResolvePendingQueue(guard,middle,Source(pixels),50,250,nullptr,false,0)); // 150ms
        token=middle.current.identity;
        auto last=Pending(6);last.current.continuityGeneration=last.continuityGeneration=23;
        Assert::IsTrue(ResolvePendingQueue(guard,last,Source(pixels),50,250,&token,true,0)); // 250ms
        Assert::AreEqual(uint64_t(23),last.current.continuityGeneration);
        auto expired=Pending(7);expired.current.continuityGeneration=expired.continuityGeneration=23;
        Assert::IsFalse(ResolvePendingQueue(guard,expired,Source(pixels),50,250,nullptr,false,0));
        Assert::AreEqual("pending-budget-expired",guard.DecisionReason());
        token=last.current.identity;
        auto after=Pending(9);after.current.continuityGeneration=after.continuityGeneration=24;
        Assert::IsFalse(ResolvePendingQueue(guard,after,Source(pixels),50,250,&token,true,0));
        Assert::AreEqual("no-accepted-seed",guard.DecisionReason());
    }
    TEST_METHOD(VerifiedCadenceAdvancesPendingAnchorAcrossDeterministicQueueRetirement) {
        const auto identity=[](uint64_t sequence) {
            auto value=Identity(sequence);value.analysisDomainKey=7;return value;
        };
        const auto pendingKey=[&](uint64_t sequence) {
            auto value=Pending(sequence);value.current.identity=identity(sequence);
            value.current.sharedPicture.identity=value.current.identity;return value;
        };
        auto pixels=PixelsWithCurrentBars();
        // One independently measured, entirely bar-owned caption.
        for(int y=0;y<90;++y)for(int x=0;x<160;++x)
            pixels[y*160+x]=uint16_t((y>=12 && y<78?300:64)<<6);
        for(int y=80;y<82;++y)for(int x=50;x<110;++x)
            if(x%7<3)pixels[y*160+x]=uint16_t(700<<6);
        ActivePictureDecisionTimeline timeline;timeline.Reset(1);
        for(uint64_t seq=3159;seq<=3164;++seq)Assert::IsTrue(timeline.TrackAcceptedFrame(identity(seq)));
        const auto before=timeline.ContinuityGeneration();
        auto ready=Ready(3159);ready.current=CompleteThreeLineScan(3159,pixels,before);
        ready.current.identity=identity(3159);ready.current.sharedPicture.identity=ready.current.identity;
        ready.continuityGeneration=before;ready.current.analysisStride=2;
        auto& text=ready.current.text;text.lineCount=1;text.bounds={50,80,110,82};
        text.sourcePanel=text.capturePanel={48,78,112,84};
        text.lineBounds[0]=text.bounds;text.lineBounds[1]=text.lineBounds[2]={};
        text.lineCapturePanels[0]=text.capturePanel;text.linePanels[0]=text.sourcePanel;
        text.lineCapturePanels[1]=text.lineCapturePanels[2]={};text.linePanels[1]=text.linePanels[2]={};
        ready.text=text;
        SubtitleBoxPresentation tracker;
        const auto acquired=tracker.Consume(ready,250,50);
        Assert::IsTrue(acquired.detected && !acquired.held);
        Assert::IsTrue(tracker.HasAcceptedSourceTemplate());
        auto stale=std::make_unique<SubtitlePendingMeasurementGuard>();
        auto advanced=std::make_unique<SubtitlePendingMeasurementGuard>();
        Assert::IsTrue(stale->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
        Assert::IsTrue(advanced->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
        SubtitleCutPasteGeometry g;g.valid=true;g.content=text.bounds;g.glyphLines=text.lineBounds;
        g.source=g.destination=g.generatedCleanup=g.pictureCapture=text.sourcePanel;
        g.pictureTop=12;g.pictureBottom=78;
        SubtitleCadenceGuard cadence;
        Assert::IsTrue(cadence.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g,50,250));
        auto skipped=pendingKey(3160);skipped.continuityGeneration=skipped.current.continuityGeneration=before;
        skipped.current.cadenceSkipped=true;skipped.current.analysisStride=2;
        const auto& proof=cadence.PrepareReference(skipped,CadenceSource(pixels),ready.current.sharedPicture.bounds,50,250);
        Assert::IsTrue(proof.valid);
        const auto tracked=tracker.Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
        Assert::IsTrue(tracked.detected && tracked.held && proof.continuation.MatchesCue(tracked));
        Assert::AreEqual(0x1000u,tracker.InkDiagnostics().contextMismatch,
            L"Cadence deliberately strips bar metadata while retaining separately verified current glyphs.");
        Assert::IsFalse(tracker.HasAcceptedSourceTemplate(),L"Cadence cannot restore measured bar/source-template authority.");
        Assert::IsTrue(tracker.HasAcceptedQueueTemplate(),
            L"An explicitly accepted current continuation must expose its source identity for queue-only reassociation without restoring bar authority.");
        for(int fault=0;fault<20;++fault) {
            auto rejected=std::make_unique<SubtitleBoxPresentation>();
            Assert::IsTrue(rejected->Consume(ready,250,50).detected);
            auto copied=std::make_unique<SubtitleBoxPreview>(proof.preview);
            auto changed=std::make_unique<SubtitleCueContinuation>(proof.continuation);
            const SubtitleCueContinuation* supplied=changed.get();
            if(fault==0)supplied=nullptr;
            if(fault==1)changed->localGlyphsOnly=true;
            if(fault==2)++copied->current.identity.analysisDomainKey;
            if(fault==3)++changed->accepted.cue;
            if(fault==4)changed->physicalBase.bottom-=2;
            if(fault==5)++changed->identity.acceptedSequence;
            if(fault==6){++copied->policyGeneration;++copied->current.policyGeneration;}
            if(fault==7){++copied->continuityGeneration;++copied->current.continuityGeneration;}
            if(fault==8)++copied->current.nearBarDistance;
            if(fault==9)copied->current.text.workLimit=true;
            if(fault==10)changed->accepted.confirmationPending=true;
            if(fault==11)changed->accepted.workLimit=true;
            if(fault==12)copied->current.sharedPicture.bounds.bottom-=2;
            if(fault==13)copied->current.discontinuity=true;
            if(fault==14)copied->current.analysisRefresh=true;
            if(fault==15)copied->current.analysisStride=1;
            if(fault==16)copied->current.cadenceSkipped=false;
            if(fault==17)copied->current.independentBarAnalysis=true;
            if(fault==18)copied->current.scriptHint=SubtitleScriptHint::Arabic;
            if(fault==19)changed->samples=0;
            rejected->Consume(*copied,250,50,nullptr,false,false,supplied);
            Assert::IsFalse(rejected->HasAcceptedQueueTemplate(),
                L"Only a private exact accepted shared cadence certificate may replace missing bar metadata for queue identity.");
        }
        tracker.Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
        Assert::IsTrue(SameAnalysisFrameIdentity(identity(3160),tracker.AcceptedQueueTemplateIdentity()),
            L"Repeated accepted output cannot advance or replace its original queue certificate.");
        {
            auto missingReference=std::make_unique<SubtitleBoxPresentation>();
            auto stripped=std::make_unique<SubtitleBoxPreview>(ready);
            stripped->current.barAuthority=false;stripped->current.barEvidence={};
            missingReference->Consume(*stripped,250,50);
            missingReference->Consume(proof.preview,250,50,nullptr,false,false,&proof.continuation);
            Assert::IsFalse(missingReference->HasAcceptedQueueTemplate(),
                L"A cadence certificate cannot manufacture a measured-bar acquisition reference.");
        }
        for(int fault=0;fault<18;++fault) {
            auto guarded=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(guarded->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
            auto bad=std::make_unique<SubtitleCadenceProof>(proof);auto changedAccepted=proof.continuation.accepted;
            if(fault==0)bad->valid=false;
            if(fault==1)bad->continuation.valid=false;
            if(fault==2)++changedAccepted.cue;
            if(fault==3)++bad->continuation.previousIdentity.acceptedSequence;
            if(fault==4)++bad->continuation.continuityGeneration;
            if(fault==5)++bad->preview.current.continuityGeneration;
            if(fault==6)++bad->preview.current.identity.analysisDomainKey;
            if(fault==7)++bad->preview.current.identity.viewportGeneration;
            if(fault==8)++bad->preview.current.identity.sourceFormatGeneration;
            if(fault==9)bad->preview.current.discontinuity=true;
            if(fault==10)++bad->preview.current.identity.sourceFrameNumber;
            if(fault==11)bad->preview.current.identity.captureTimestamp=ready.current.identity.captureTimestamp;
            if(fault==12)bad->preview.current.pendingRefresh=false;
            if(fault==13)bad->samples=0;
            if(fault==14)bad->continuation.localGlyphsOnly=true;
            if(fault==15)++bad->continuation.policyGeneration;
            if(fault==16)bad->continuation.physicalBase.bottom-=2;
            if(fault==17)changedAccepted.workLimit=true;
            Assert::IsFalse(guarded->AdvanceVerifiedCadence(*bad,changedAccepted,50,250));
        }
        auto accepted=proof.continuation.accepted;
        Assert::IsTrue(advanced->AdvanceVerifiedCadence(proof,accepted,50,250));
        Assert::IsTrue(advanced->AdvanceVerifiedCadence(proof,accepted,50,250),L"Retry cannot spend or reset the lease twice.");
        {
            auto tightened=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(tightened->RememberAccepted(ready,ready.text,CadenceSource(pixels),50,250));
            Assert::IsTrue(tightened->AdvanceVerifiedCadence(proof,accepted,50,250));
            Assert::IsFalse(tightened->AdvanceVerifiedCadence(proof,accepted,50,49),
                L"A same-source retry cannot survive a newly shortened hold budget.");
            Assert::AreEqual("pending-budget-expired",tightened->DecisionReason());
        }
        timeline.MarkConsumed(identity(3159));timeline.MarkConsumed(identity(3160));
        for(uint64_t seq=3161;seq<=3163;++seq)
            timeline.MarkDiscarded(identity(seq),seq,ActivePictureDiscardReason::BacklogRecovery);
        const auto after=timeline.ContinuityGeneration();auto token=tracker.AcceptedQueueTemplateIdentity();
        Assert::IsTrue(SameAnalysisFrameIdentity(identity(3160),token));
        Assert::IsTrue(timeline.OnlyBacklogRetirementBetween(token,identity(3164),before,after));
        for(int fault=0;fault<3;++fault) {
            auto changed=pixels;auto guarded=std::make_unique<SubtitlePendingMeasurementGuard>();
            Assert::IsTrue(guarded->RememberAccepted(ready,ready.text,CadenceSource(changed),50,250));
            Assert::IsTrue(guarded->AdvanceVerifiedCadence(proof,accepted,50,250));
            if(fault==0)changed[80*160+50]=uint16_t(64<<6);
            if(fault==1)changed[160*90+(80/2)*160+50]=0;
            if(fault==2)for(int y=0;y<12;++y)for(int x=0;x<160;++x)changed[y*160+x]=uint16_t(300<<6);
            auto pending=pendingKey(3164);pending.current.continuityGeneration=pending.continuityGeneration=after;
            Assert::IsFalse(guarded->Resolve(pending,CadenceSource(changed),50,250,&token,true));
            Assert::IsTrue(guarded->DecisionDiagnostics().glyphSamplesChecked>0);
        }
        auto current=pendingKey(3164);current.current.continuityGeneration=current.continuityGeneration=after;
        auto red=current;
        Assert::IsFalse(stale->Resolve(red,CadenceSource(pixels),50,250,&token,true));
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextContinuity),stale->DecisionDiagnostics().contextMismatch);
        Assert::AreEqual(size_t(0),stale->DecisionDiagnostics().glyphSamplesChecked);
        Assert::IsTrue(advanced->Resolve(current,CadenceSource(pixels),50,250,&token,true)); // Original 250ms, not a new 200ms lease.
        Assert::IsTrue(advanced->DecisionDiagnostics().glyphSamplesChecked>0);
        Assert::IsTrue(advanced->DecisionDiagnostics().barsAvailable && advanced->DecisionDiagnostics().backlogReassociated);
        auto expired=pendingKey(3165);expired.current.continuityGeneration=expired.continuityGeneration=after;
        Assert::IsFalse(advanced->Resolve(expired,CadenceSource(pixels),50,250));
        Assert::AreEqual("pending-budget-expired",advanced->DecisionReason());
        auto unknown=pendingKey(3164);unknown.continuityGeneration=unknown.current.continuityGeneration=after;
        Assert::IsFalse(tracker.Consume(unknown,250,50,&token,true).detected);
        Assert::IsTrue(tracker.HasAcceptedQueueTemplate());
        Assert::IsTrue(SameAnalysisFrameIdentity(token,tracker.AcceptedQueueTemplateIdentity()),
            L"Intentional backlog wait preserves the same old anchor, never unknown source identity.");
        tracker.Reset();Assert::IsFalse(tracker.HasAcceptedQueueTemplate());
    }
    TEST_METHOD(PendingGuardReportsQueueContinuityWithoutRelaxingIt) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        auto pending=Pending(2);
        ++pending.current.continuityGeneration;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("pending-context-changed",guard.DecisionReason());
        Assert::AreEqual(static_cast<uint32_t>(SubtitlePendingMeasurementGuard::ContextContinuity),
            guard.DecisionDiagnostics().contextMismatch);
        Assert::IsTrue(guard.DecisionDiagnostics().glyphSamplesChecked==0);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(PendingGuardReportsExactChangedGlyphPixel) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("current-pixels-changed",guard.DecisionReason());
        const auto& detail=guard.DecisionDiagnostics();
        Assert::IsTrue(detail.glyphSamplesChecked>0);
        Assert::AreEqual(50,detail.glyphMismatchX);
        Assert::AreEqual(50,detail.glyphMismatchY);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(PendingGuardReportsCurrentBarFailureWithoutCaptionAuthority) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        ready.current.sharedPicture.required=false;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        // Fill the former top bar with picture-level pixels. Glyphs are unchanged,
        // so rejection must come from the current native bar proof.
        for(int y=0;y<12;++y)for(int x=0;x<160;++x)
            pixels[y*160+x]=uint16_t(300<<6);
        auto pending=Pending(2);pending.current.sharedPicture.required=false;
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::AreEqual("current-bars-changed",guard.DecisionReason());
        const auto& detail=guard.DecisionDiagnostics();
        Assert::IsTrue(detail.glyphSamplesChecked>0);
        Assert::AreEqual(12,detail.expectedTop);
        Assert::AreEqual(78,detail.expectedBottom);
        Assert::IsTrue(!detail.barsAvailable || detail.measuredTop!=detail.expectedTop);
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(CadenceGuardUhdOppositeEdgeLossUsesActualTwoRowFootprintInNativeFormats) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422}) {
            std::vector<uint16_t> pixels(size_t(3840)*2160*(format==AnalysisLumaFormat::P010?3:4)/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+3840*2160,uint16_t(64<<6));
            for(int y=256;y<1904;++y)for(int x=0;x<3840;++x)pixels[size_t(y)*3840+x]=uint16_t(300<<6);
            for(int y=1730;y<1904;++y)for(int x=1520;x<2340;++x)pixels[size_t(y)*3840+x]=uint16_t(64<<6);
            auto ready=Ready(1);auto& c=ready.current;c.width=3840;c.height=2160;
            c.pictureTop=0;c.pictureBottom=1904;c.analysisStride=2;
            c.sharedPicture.bounds={0,256,3840,1904,3840,2160};
            c.text.lineCount=2;c.text.bounds={1580,1750,2260,1992};
            c.text.lineBounds[0]={1580,1890,2260,1992};
            c.text.lineBounds[1]={1640,1750,2200,1848};c.text.lineBounds[2]={};
            c.text.sourcePanel={1520,1730,2340,1904};
            c.text.nearBarEligibilityMeasured=c.text.nearBarEligible=true;c.nearBarDistance=20;
            for(int line=0;line<2;++line)for(int y=c.text.lineBounds[line].top;y<c.text.lineBounds[line].bottom;++y)
                for(int x=c.text.lineBounds[line].left;x<c.text.lineBounds[line].right;++x)
                    if((x-c.text.lineBounds[line].left)%28<12)pixels[size_t(y)*3840+x]=uint16_t(700<<6);
            auto ink=std::make_shared<SubtitleInkSnapshot>();ink->width=960;ink->height=540;ink->step=4;
            ink->sourceRows.resize(540);ink->rawEvidenceComplete=ink->paletteValid=true;
            ink->inkFloor=500;ink->blackLimit=96;
            ink->rawInk.resize(8100);ink->ownedInk.resize(8100);ink->blackBacking.resize(8100);
            for(int y=0;y<540;++y) {ink->sourceRows[y]=4*y;for(int x=0;x<960;++x) {
                const size_t i=size_t(y)*960+x;const uint64_t bit=uint64_t(1)<<(i%64);
                const int value=pixels[size_t(y*4)*3840+x*4]>>6;
                if(value>=500){ink->rawInk[i/64]|=bit;ink->ownedInk[i/64]|=bit;}
                if(value<=96)ink->blackBacking[i/64]|=bit;
            }}
            c.ink=ink;ready.text=c.text;
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*2;source.width=3840;source.height=2160;
            source.rowBytes=source.chromaRowBytes=7680;source.format=format;source.generation=1;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source={1480,1700,2380,2024};g.destination=g.source;g.generatedCleanup=c.text.sourcePanel;
            g.pictureCapture=c.text.sourcePanel;g.pictureTop=0;g.pictureBottom=1904;
            g=ApplySubtitleTextReduction(FitSubtitleToVisiblePicture(g,{0,256,3840,1904},20),
                30,SubtitleBoxPadding(40),true,false);
            Assert::IsTrue(g.valid);Assert::AreEqual(0.7f,g.glyphScale);
            SubtitleCadenceGuard guard;const auto started=std::chrono::steady_clock::now();
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
            const double seedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            auto pending=ready;pending.available=false;pending.current.identity=Identity(2);
            pending.current.sharedPicture.identity=pending.current.identity;
            pending.current.analyzed=false;pending.current.cadenceSkipped=true;pending.current.text={};pending.text={};
            // Motion outside the exact card/content footprint cannot veto.
            for(int y=256;y<1600;++y)for(int x=0;x<3840;++x)pixels[size_t(y)*3840+x]=uint16_t(450<<6);
            const auto proofStart=std::chrono::steady_clock::now();
            const auto result=guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250);
            const double proofMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-proofStart).count();
            Logger::WriteMessage((std::string("UHD cadence format=")+std::to_string(int(format))+
                " seed_ms="+std::to_string(seedMs)+" proof_ms="+std::to_string(proofMs)+
                " samples="+std::to_string(result.samples)+" reason="+result.reason+"\n").c_str());
            Assert::IsTrue(result.valid);Assert::IsFalse(result.preview.current.barAuthority);
            Assert::IsTrue(result.continuation.Matches(pending.current.identity,11,21,c.sharedPicture.bounds));
            Assert::AreEqual(202520u,result.samples,L"Every native union pixel is counted once, not step-4 samples.");
            Assert::IsFalse(result.continuation.backingVerifiedBounds.Valid());
            // A source-padding pixel is not extracted or erased.
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
            pixels[size_t(1711)*3840+1491]=uint16_t(700<<6);
            Assert::IsTrue(guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250).valid);
            for(int mutation=0;mutation<3;++mutation) {
                Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
                const int x=mutation==0?1581:mutation==1?1597:1521;
                const int y=mutation==2?1731:1893;
                const auto before=pixels[size_t(y)*3840+x];
                pixels[size_t(y)*3840+x]=uint16_t((mutation==0?64:700)<<6);
                Assert::IsFalse(guard.Prepare(pending,source,c.sharedPicture.bounds,1000.0/60,250).valid,
                    L"One native pixel between the detector grid points must revoke the certificate.");
                pixels[size_t(y)*3840+x]=before;
            }
        }
    }
    TEST_METHOD(CadenceGuardKeepsCurrentGlyphsAcrossAlternatingInputsAndMovingBackground) {
        auto pixels=Pixels();SubtitleCadenceGuard guard;SubtitleBoxPresentation tracker;unsigned rearms=0;
        for(uint64_t sequence=1;sequence<601;sequence+=2) {
            auto ready=Ready(sequence);
            ready.current=CompleteThreeLineScan(sequence,pixels);
            ready.current.analysisStride=2;ready.current.pictureTop=0;
            ready.current.independentBarAnalysis=false;ready.current.barEvidence.top=0;
            ready.text=ready.current.text;ready.futureAvailable=true;
            ready.matchingFrames=4;ready.currentLinesConfirmed=true;
            const auto accepted=tracker.Consume(ready,250,1000.0/60);
            Assert::IsTrue(accepted.detected && !accepted.held);
            Assert::AreEqual(ready.text.cue,accepted.cue);
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.pictureTop=0;g.pictureBottom=78;g.source=g.content;g.destination=g.content;
            Assert::IsTrue(guard.RememberAccepted(ready,accepted,CadenceSource(pixels),
                ready.current.sharedPicture.bounds,g,1000.0/60,250));
            if(sequence>1) {
                Assert::AreEqual("cadence-seed-rearmed",guard.DecisionReason());
                Assert::AreEqual(0u,guard.SampleCount());++rearms;
            }
            // More than the legacy eight-code raw-pixel tolerance, while the
            // unchanged neutral backing and caption-color mask remain proved.
            for(int y=47;y<82;++y)for(int x=40;x<120;++x)
                if((pixels[y*160+x]>>6)<500)pixels[y*160+x]=uint16_t((sequence%4==1?90:64)<<6);
            // Unrelated picture can change arbitrarily outside source ownership.
            for(int y=12;y<40;++y)for(int x=0;x<160;++x)
                pixels[y*160+x]=uint16_t((200+sequence%500)<<6);
            auto pending=Pending(sequence+1);pending.current.analysisStride=2;
            pending.current.cadenceSkipped=true;
            const auto result=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::IsTrue(result.valid,L"an explicit skipped input must retain only freshly verified caption pixels");
            Assert::IsTrue(result.preview.available && result.preview.current.analyzed && result.preview.current.pendingRefresh);
            Assert::IsTrue(result.preview.text.held);Assert::AreEqual(ready.text.cue,result.preview.text.cue);
            Assert::IsFalse(result.preview.current.barAuthority || result.preview.current.independentBarAnalysis);
            Assert::IsFalse(result.preview.current.subtitleArProof.accepted);
            Assert::AreEqual(0u,result.preview.followingCount);Assert::IsFalse(result.preview.currentLinesConfirmed);
            const auto held=tracker.Consume(result.preview,250,1000.0/60,nullptr,false,false,&result.continuation);
            Assert::IsTrue(held.detected && held.held);Assert::AreEqual(accepted.cue,held.cue);
            Assert::IsTrue(result.samples>0 && result.samples<=SubtitleCadenceGuard::SampleBudget);
            const auto repeat=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::IsTrue(repeat.valid);Assert::AreEqual(result.continuation.elapsedMs,repeat.continuation.elapsedMs);
        }
        Assert::AreEqual(299u,rearms,L"A repeated stable cue reuses its immutable native baseline after each fresh positive scan.");
    }
    TEST_METHOD(CadenceGuardRejectsGlyphChangesCleanupIntrusionAndUnknownContexts) {
        for(int fault=0;fault<16;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.current.pictureTop=0;ready.current.independentBarAnalysis=false;
            ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.pictureTop=0;g.pictureBottom=78;g.source=g.content;g.destination=g.content;
            if(fault==14)pixels[48*160+45]=uint16_t(300<<6); // baseline nonblack cleanup, outside capture
            if(fault==15){g.pictureBottom=60;g.generatedCleanup={};ready.text.sourcePanel={};ready.current.text.sourcePanel={};}
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            auto base=ready.current.sharedPicture.bounds;auto source=CadenceSource(pixels);
            if(fault==0)for(int y=50;y<56;++y)for(int x=50;x<110;++x)pixels[y*160+x]=uint16_t(64<<6);
            if(fault==1)pixels[51*160+50]=uint16_t(64<<6);
            if(fault==2)pixels[51*160+53]=uint16_t(700<<6); // new glyph stroke
            if(fault==3)pixels[48*160+45]=uint16_t(300<<6); // exposed picture in cleanup
            if(fault==4)++pending.current.identity.analysisDomainKey;
            if(fault==5)++pending.current.identity.viewportGeneration;
            if(fault==6)pending.current.discontinuity=true;
            if(fault==7){pending.current.identity=Identity(4);pending.current.sharedPicture.identity=pending.current.identity;}
            if(fault==8)pending.current.cadenceSkipped=false;
            if(fault==9)pending.current.analysisStride=1;
            if(fault==10)++base.bottom;
            if(fault==11)source.range=AnalysisLumaRange::Full;
            if(fault==12)pending.current.identity.captureTimestamp=ready.current.identity.captureTimestamp;
            if(fault==13)source.dataBytes=1;
            if(fault==14)pixels[48*160+45]=uint16_t(700<<6);
            if(fault==15)pixels[70*160+70]=uint16_t(700<<6); // erased bar gap between rows
            Assert::IsFalse(guard.Prepare(pending,source,base,1000.0/60,250).valid);
        }
    }
    TEST_METHOD(CadenceGuardAllowsOnlyOwnedOuterFringeJitterAndKeepsSnapshotCurrent) {
        for(int fault=0;fault<6;++fault) {
            auto pixels=Pixels();
            // A weak outer edge of an 18-pixel stroke, plus a weak non-ink
            // neighbour of another unchanged strong edge of that same stroke.
            pixels[52*160+56]=uint16_t(505<<6);
            pixels[53*160+55]=uint16_t(495<<6);
            if(fault==3)pixels[52*160+61]=uint16_t(700<<6); // isolated punctuation
            auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
            g.source=g.destination=g.content;g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            pixels[52*160+56]=uint16_t(495<<6);
            pixels[53*160+55]=uint16_t(505<<6);
            if(fault==1)pixels[52*160+61]=uint16_t(700<<6); // disconnected new glyph
            if(fault==2)pixels[52*160+57]=uint16_t(64<<6); // lost strong interior
            if(fault==3)pixels[52*160+61]=uint16_t(64<<6); // punctuation disappeared
            if(fault==4)pixels[52*160+56]=uint16_t(450<<6); // beyond encoded fringe
            if(fault==5)pixels[48*160+45]=uint16_t(110<<6); // noncaption cleanup intrusion
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            if(fault)Assert::IsFalse(proof.valid);
            else {
                Assert::IsTrue(proof.valid);
                Assert::IsFalse(proof.preview.current.ink->Get(56,52));
                Assert::IsTrue(proof.preview.current.ink->Get(55,53));
                Assert::IsFalse(proof.preview.current.ink->Get(55,53,true),L"New fringe is current ink, not invented old ownership.");
                Assert::IsFalse(proof.preview.current.ink->rawEvidenceComplete);
            }
        }
    }
    TEST_METHOD(CadenceGuardDoesNotTolerateFringeChangesInsideGlyphHoles) {
        for(int mutation=0;mutation<2;++mutation) {
            auto pixels=Pixels();
            std::fill(pixels.begin(),pixels.begin()+160*90,uint16_t(64<<6));
            for(int y=50;y<61;++y)for(int x=50;x<61;++x)
                if(x<52 || x>=59 || y<52 || y>=59)pixels[y*160+x]=uint16_t(700<<6);
            const int x=mutation?52:51;
            pixels[55*160+x]=uint16_t((mutation?495:505)<<6);
            auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            auto& c=ready.current;c.analysisStride=2;c.text.lineCount=1;
            c.text.bounds=c.text.lineBounds[0]={50,50,61,61};c.text.lineBounds[1]=c.text.lineBounds[2]={};
            ready.text=c.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source=g.destination=g.content;g.pictureCapture=c.text.capturePanel;
            g.generatedCleanup=c.text.sourcePanel;g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),c.sharedPicture.bounds,g));
            pixels[55*160+x]=uint16_t((mutation?505:495)<<6);
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            Assert::IsFalse(guard.Prepare(pending,CadenceSource(pixels),c.sharedPicture.bounds,1000.0/60,250).valid,
                L"Inner hole pixels and the strokes separating holes from outside stay strict.");
        }
    }
    TEST_METHOD(CadenceGuardRoundedCleanupPreservesMovingCornersAndRejectsActualEraseIntrusion) {
        for(int fault=0;fault<2;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;g.generatedCleanup={60,47,100,78};
            for(int y=47;y<78;++y)g.generatedCleanupRows[g.generatedCleanupRowCount++]={
                y<50?60:40,y,y<50?100:120,y+1};
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            // Both points sit inside the sourcePanel/hull, but only the second
            // belongs to a shader erase span. The first corner stays live.
            pixels[48*160+(fault?65:45)]=uint16_t(300<<6);
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250);
            Assert::AreEqual(fault==0,proof.valid);
        }
    }
    TEST_METHOD(CadenceGuardUhdCrossingCaptionKeepsOnlyProvedRowFootprintAndGlyphFringe) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422})
        for(int mutation=0;mutation<4;++mutation) {
            constexpr int width=3840,height=2160;
            std::vector<uint16_t> pixels(size_t(width)*height*(format==AnalysisLumaFormat::P010?3:4)/2,uint16_t(512<<6));
            std::fill(pixels.begin(),pixels.begin()+size_t(width)*height,uint16_t(64<<6));
            for(int y=250;y<1906;++y)for(int x=0;x<width;++x)pixels[size_t(y)*width+x]=uint16_t(300<<6);
            auto ready=Ready(1);auto& c=ready.current;
            c.width=width;c.height=height;c.pictureTop=250;c.pictureBottom=1906;c.analysisStride=2;
            c.sharedPicture.bounds={0,250,width,1906,width,height};
            c.text.lineCount=2;c.text.bounds={1354,1746,2470,1992};
            c.text.lineBounds[0]={1354,1892,2470,1992};
            c.text.lineBounds[1]={1640,1746,2194,1828};c.text.lineBounds[2]={};
            c.text.sourcePanel=c.text.capturePanel={1302,1740,2528,1906};
            c.text.nearBarEligibilityMeasured=c.text.nearBarEligible=true;
            for(int line=0;line<2;++line) {
                const auto& r=c.text.lineBounds[line];
                for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
                    if((x-r.left)%28<12)pixels[size_t(y)*width+x]=uint16_t(700<<6);
            }
            // One external AA fringe beside a large, surviving owned core.
            pixels[size_t(1950)*width+1366]=uint16_t(495<<6);
            auto ink=std::make_shared<SubtitleInkSnapshot>();ink->width=960;ink->height=540;ink->step=4;
            ink->sourceRows.resize(540);ink->rawEvidenceComplete=ink->paletteValid=true;
            ink->inkFloor=500;ink->blackLimit=96;
            ink->rawInk.resize(8100);ink->ownedInk.resize(8100);ink->blackBacking.resize(8100);
            for(int y=0;y<540;++y) {ink->sourceRows[y]=4*y;for(int x=0;x<960;++x) {
                const size_t at=size_t(y)*960+x;const uint64_t bit=uint64_t(1)<<(at%64);
                const int value=pixels[size_t(y*4)*width+x*4]>>6;
                if(value>=500){ink->rawInk[at/64]|=bit;ink->ownedInk[at/64]|=bit;}
                if(value<=96)ink->blackBacking[at/64]|=bit;
            }}
            c.ink=ink;ready.text=c.text;
            AnalysisLumaSource source;source.data=reinterpret_cast<const uint8_t*>(pixels.data());
            source.dataBytes=pixels.size()*sizeof(uint16_t);source.width=width;source.height=height;
            source.rowBytes=source.chromaRowBytes=width*2;source.generation=1;source.format=format;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=c.text.bounds;g.glyphLines=c.text.lineBounds;
            g.source=g.destination={1280,1712,2544,2018};g.pictureCapture=c.text.capturePanel;
            g.pictureTop=250;g.pictureBottom=1906;g.generatedCleanup=c.text.sourcePanel;
            for(int y=1740;y<1852;++y) {
                const bool rounded=y<1748 || y>=1844;
                g.generatedCleanupRows[g.generatedCleanupRowCount++]={rounded?1340:1302,y,rounded?2490:2528,y+1};
            }
            for(int y=1892;y<1906;++y)
                g.generatedCleanupRows[g.generatedCleanupRowCount++]={1354,y,2470,y+1};
            auto guard=std::make_unique<SubtitleCadenceGuard>();
            const bool seeded=guard->RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g);
            if(!seeded)Logger::WriteMessage(guard->DecisionReason());
            Assert::IsTrue(seeded);
            if(mutation==0) {
                // Moving native picture includes the corners INSIDE the cleanup
                // hull but OUTSIDE its actual row spans. Neither is erased.
                for(int y=250;y<1906;++y)for(int x=0;x<width;++x) {
                    bool captured=false;for(int line=0;line<2;++line) {
                        const auto& r=c.text.lineBounds[line];
                        captured|=x>=r.left && x<r.right && y>=r.top && y<r.bottom;
                    }
                    bool erased=false;
                    if(y>=1740 && y<1852) {
                        const auto& r=g.generatedCleanupRows[y-1740];erased=x>=r.left && x<r.right;
                    }
                    if(y>=1892 && y<1906)erased=x>=1354 && x<2470;
                    if(!captured && !erased)pixels[size_t(y)*width+x]=uint16_t((340+((x+y)&1)*20)<<6);
                }
                Assert::AreEqual(340,int(pixels[size_t(1742)*width+1304]>>6));
            }
            if(mutation==1)pixels[size_t(1805)*width+1310]=uint16_t(340<<6); // unowned picture inside actual erasure
            if(mutation==2)pixels[size_t(1950)*width+1360]=uint16_t(64<<6); // loss of a strong owned core
            if(mutation==3)pixels[size_t(1950)*width+1366]=uint16_t(505<<6); // bounded external AA only
            auto pending=Pending(2);pending.current.width=width;pending.current.height=height;
            pending.current.pictureTop=250;pending.current.pictureBottom=1906;
            pending.current.sharedPicture.bounds=c.sharedPicture.bounds;
            pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto& proof=guard->PrepareReference(pending,source,c.sharedPicture.bounds,1000.0/60,250);
            Assert::AreEqual(mutation==0 || mutation==3,proof.valid,
                L"Only outside-footprint native motion and bounded owned external AA may continue; unowned erasure and core changes stay strict.");
            if(mutation==1)Assert::AreEqual("cadence-current-cleanup-changed",proof.reason);
            if(mutation==2)Assert::AreEqual("cadence-current-glyph-changed",proof.reason);
        }
    }
    TEST_METHOD(CadenceGuardRejectsNativeWorkOverBudgetAndUnsupportedBackground) {
        auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
        ready.current.analysisStride=2;ready.text=ready.current.text;
        SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
        g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.source=g.destination=g.content;g.pictureTop=0;g.pictureBottom=78;
        SubtitleCadenceGuard guard;
        Assert::IsFalse(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g,1000.0/60,250,2));
        Assert::AreEqual("cadence-unsupported-background",guard.DecisionReason());
        // A valid 1920x600 footprint exceeds the one-million native work cap.
        pixels.assign(size_t(1920)*600*3/2,uint16_t(512<<6));
        std::fill(pixels.begin(),pixels.begin()+1920*600,uint16_t(64<<6));
        auto source=CadenceSource(pixels);source.width=1920;source.height=600;source.rowBytes=source.chromaRowBytes=3840;
        auto& c=ready.current;c.width=1920;c.height=600;c.sharedPicture.bounds={0,12,1920,588,1920,600};
        auto ink=std::make_shared<SubtitleInkSnapshot>();ink->step=2;ink->width=960;ink->height=300;
        ink->sourceRows.resize(300);for(int y=0;y<300;++y)ink->sourceRows[y]=2*y;
        ink->paletteValid=ink->rawEvidenceComplete=true;ink->inkFloor=500;ink->blackLimit=96;
        ink->rawInk.resize(4500);ink->ownedInk.resize(4500);ink->blackBacking.resize(4500);c.ink=ink;
        g.generatedCleanup={0,0,1920,600};g.pictureBottom=600;
        Assert::IsFalse(guard.RememberAccepted(ready,ready.text,source,c.sharedPicture.bounds,g));
        Assert::AreEqual("cadence-sample-budget-or-source",guard.DecisionReason());
        Assert::AreEqual(SubtitleCadenceGuard::SampleBudget,guard.SampleCount());
    }
    TEST_METHOD(CadenceGuardValidatedPlanarReaderMatchesCanonicalSamplesWithUnalignedPaddedPlanes) {
        for(auto format:{AnalysisLumaFormat::P010,AnalysisLumaFormat::P210,AnalysisLumaFormat::DecodedDolbyVision422})
        for(int fault=0;fault<7;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            // Deliberately odd and different pitches, plus an unaligned base.
            // Dirty padding makes accidental tight/16-bit row access observable.
            const size_t lumaPitch=327,chromaPitch=333;
            const int chromaHeight=format==AnalysisLumaFormat::P010?45:90;
            const size_t lumaBytes=lumaPitch*90;
            std::vector<uint8_t> bytes(1+lumaBytes+chromaPitch*chromaHeight,uint8_t(0xA5));
            auto* data=bytes.data()+1;
            const auto write=[&](size_t offset,unsigned code,unsigned low) {
                const unsigned word=(code<<6)|(low&63);
                data[offset]=uint8_t(word);data[offset+1]=uint8_t(word>>8);
            };
            for(int y=0;y<90;++y)for(int x=0;x<160;++x)
                write(size_t(y)*lumaPitch+size_t(x)*2,pixels[y*160+x]>>6,unsigned(x+3*y));
            for(int y=0;y<chromaHeight;++y)for(int x=0;x<80;++x) {
                const size_t offset=lumaBytes+size_t(y)*chromaPitch+size_t(x)*4;
                write(offset,512,unsigned(x+y));write(offset+2,512,unsigned(2*x+y));
            }
            auto source=CadenceSource(pixels);source.data=data;source.dataBytes=bytes.size()-1;
            source.rowBytes=lumaPitch;source.chromaRowBytes=chromaPitch;source.format=format;
            Assert::IsTrue(source.IsValid());
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;
            g.generatedCleanup=ready.text.sourcePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,source,ready.current.sharedPicture.bounds,g));
            // All three formats expose upper ten bits. Altering low storage bits
            // must remain invisible even for decoded 12-bit DV storage.
            data[size_t(52)*lumaPitch+56*2]^=uint8_t(63);
            const auto chromaOffset=[&](int x,int y) {
                return lumaBytes+size_t(format==AnalysisLumaFormat::P010?y/2:y)*chromaPitch+size_t(x/2)*4;
            };
            if(fault==1)write(chromaOffset(56,52),700,31); // owned core loses caption chroma
            if(fault==2)write(chromaOffset(44,48)+2,700,31); // cleanup ceases to be neutral black
            if(fault==3)--source.dataBytes; // last plane row is truncated
            if(fault==4)source.rowBytes=319;
            if(fault==5)source.chromaRowBytes=319;
            if(fault==6)write(size_t(53)*lumaPitch+57*2,64,31); // odd native pixel/row
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            const auto proof=guard.Prepare(pending,source,ready.current.sharedPicture.bounds,1000.0/60,250);
            if(fault) {
                Assert::IsFalse(proof.valid);
                if(fault>=3 && fault<=5) {
                    Assert::AreEqual(0u,proof.samples,L"Invalid complete allocation is rejected before the fast reader.");
                    Assert::IsFalse(guard.RememberAccepted(ready,ready.text,source,ready.current.sharedPicture.bounds,g));
                }
            } else {
                Assert::IsTrue(proof.valid);Assert::AreEqual(2800u,proof.samples);
                for(int y=47;y<82;++y)for(int x=40;x<120;++x) {
                    AnalysisLumaSample expected;Assert::IsTrue(source.Sample(x,y,expected));
                    Assert::AreEqual(int(pixels[y*160+x]>>6),int(expected.luma));
                    Assert::AreEqual(512,int(expected.chromaU));Assert::AreEqual(512,int(expected.chromaV));
                    Assert::AreEqual(expected.luma>=500,proof.preview.current.ink->Get(x,y));
                    Assert::AreEqual(expected.luma<=96,proof.preview.current.ink->Black(x,y));
                }
            }
        }
    }
    TEST_METHOD(CadenceGuardRearmRequiresFreshIndependentScanAndExactFinalGeometry) {
        for(int fault=0;fault<8;++fault) {
            auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
            ready.current.analysisStride=2;ready.text=ready.current.text;
            SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
            g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;
            g.generatedCleanup=ready.text.sourcePanel;g.source=g.destination=g.content;
            g.pictureTop=0;g.pictureBottom=78;
            SubtitleCadenceGuard guard;
            Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
            auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
            Assert::IsTrue(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
            auto fresh=ready;fresh.current.identity=Identity(fault==7?5:3);
            if(fault==1)fresh.current.text.held=true;
            if(fault==2)fresh.current.pendingRefresh=true;
            if(fault==3)g.glyphScale=0.7f;
            if(fault==4)++g.pictureCapture.left;
            if(fault==5)++fresh.current.identity.viewportGeneration;
            if(fault==6)++fresh.current.text.cue;
            fresh.current.sharedPicture.identity=fresh.current.identity;fresh.text=fresh.current.text;
            const bool remembered=guard.RememberAccepted(fresh,fresh.text,CadenceSource(pixels),fresh.current.sharedPicture.bounds,g);
            if(fault==1 || fault==2)Assert::IsFalse(remembered,L"Held/pending output cannot rearm a native baseline.");
            else {
                Assert::IsTrue(remembered);
                if(!fault) {
                    Assert::AreEqual("cadence-seed-rearmed",guard.DecisionReason());Assert::AreEqual(0u,guard.SampleCount());
                    auto next=Pending(4);next.current.analysisStride=2;next.current.cadenceSkipped=true;
                    const auto proof=guard.Prepare(next,CadenceSource(pixels),fresh.current.sharedPicture.bounds,1000.0/60,250);
                    Assert::IsTrue(proof.valid);Assert::AreEqual(uint64_t(3),proof.continuation.previousIdentity.acceptedSequence);
                    Assert::IsTrue(proof.samples>0,L"A rearmed seed still verifies every native pixel on the next skipped source.");
                } else {
                    Assert::AreEqual("cadence-seeded",guard.DecisionReason());Assert::IsTrue(guard.SampleCount()>0);
                }
            }
        }
    }
    TEST_METHOD(CurrentFootprintRearmsStrideOneOnlyAndKeepsOriginalPixelBaseline) {
        auto makeGeometry=[](const SubtitleBoxResult& accepted,int top,int bottom) {
            SubtitleCutPasteGeometry g;g.valid=true;g.content=accepted.bounds;
            g.glyphLines=accepted.lineBounds;g.pictureCapture=accepted.capturePanel;
            g.generatedCleanup=accepted.sourcePanel;g.source=g.destination=g.content;
            g.pictureTop=top;g.pictureBottom=bottom;return g;
        };
        auto pixels=PixelsWithCurrentBars();
        auto seed=Ready(1);seed.current=CompleteThreeLineScan(1,pixels);seed.current.analysisStride=1;
        seed.text=seed.current.text;const auto accepted=seed.current.text;
        const auto base=seed.current.sharedPicture.bounds;const auto geometry=makeGeometry(accepted,12,78);
        auto source=CadenceSource(pixels);
        auto footprint=std::make_unique<SubtitleCadenceGuard>();
        Assert::IsTrue(footprint->RememberCurrentFootprint(seed,accepted,source,base,geometry));

        // A consecutive independently analyzed stride-1 source may reuse the
        // immutable footprint topology. A black-to-nonblack change is not
        // silently incorporated into that baseline.
        auto movedPixels=pixels;movedPixels[80*160+42]=uint16_t(110<<6);
        auto movedSeed=Ready(4);movedSeed.current=CompleteThreeLineScan(4,movedPixels);
        movedSeed.current.analysisStride=1;movedSeed.continuityGeneration=22;
        movedSeed.current.continuityGeneration=22;movedSeed.text=movedSeed.current.text;
        auto movedSource=CadenceSource(movedPixels);
        Assert::IsTrue(footprint->RememberCurrentFootprint(movedSeed,movedSeed.text,movedSource,base,geometry));
        Assert::AreEqual("cadence-seed-rearmed",footprint->DecisionReason());
        Assert::AreEqual(0u,footprint->SampleCount(),L"Stride-1 rearm must reuse the immutable baseline, not rebuild it.");
        auto laterSeed=Ready(12);laterSeed.current=CompleteThreeLineScan(12,movedPixels,22);
        laterSeed.current.analysisStride=1;laterSeed.continuityGeneration=22;
        laterSeed.current.continuityGeneration=22;laterSeed.text=laterSeed.current.text;
        Assert::IsTrue(footprint->RememberCurrentFootprint(laterSeed,laterSeed.text,movedSource,base,geometry),
            L"A fresh complete positive source may rearm after a multi-source gap without replacing the native baseline.");
        Assert::AreEqual("cadence-seed-rearmed",footprint->DecisionReason());
        Assert::AreEqual(0u,footprint->SampleCount());
        auto verifyCurrent=CompleteThreeLineScan(13,movedPixels,22);
        const auto movedProof=footprint->VerifyCurrentFootprint(verifyCurrent,movedSource,
            laterSeed.current,laterSeed.text,base);
        Assert::IsFalse(movedProof.valid,
            L"A changed old-black cleanup sample must still fail against the original seed after repeated gap re-arms.");
        Assert::AreEqual("cadence-current-cleanup-changed",movedProof.reason);

        // Source sequence, transport frame and capture time must all advance.
        for(int fault=0;fault<3;++fault) {
            auto invalid=std::make_unique<SubtitleCadenceGuard>();
            Assert::IsTrue(invalid->RememberCurrentFootprint(seed,accepted,source,base,geometry));
            auto candidate=Ready(fault==0?1:2);candidate.current=CompleteThreeLineScan(fault==0?1:2,pixels);
            candidate.current.analysisStride=1;candidate.text=candidate.current.text;
            if(fault==1)candidate.current.identity.sourceFrameNumber=1;
            if(fault==2)candidate.current.identity.captureTimestamp=seed.current.identity.captureTimestamp;
            Assert::IsTrue(invalid->RememberCurrentFootprint(candidate,candidate.text,source,base,geometry));
            Assert::AreNotEqual("cadence-seed-rearmed",invalid->DecisionReason(),
                L"A duplicate/backward sequence, transport frame, or timestamp cannot use the retained baseline shortcut.");
            Assert::IsTrue(invalid->SampleCount()>0,
                L"A non-forward candidate must use the ordinary complete seed path.");
        }

        // Changed owned masks and changed shader geometry cannot reuse the
        // old topology. A fresh independent accepted source can seed anew.
        for(int fault=0;fault<2;++fault) {
            auto faultGuard=std::make_unique<SubtitleCadenceGuard>();
            Assert::IsTrue(faultGuard->RememberCurrentFootprint(seed,accepted,source,base,geometry));
            auto changedPixels=pixels;
            if(fault==0)changedPixels[50*160+50]=uint16_t(300<<6);
            auto changed=Ready(2);changed.current=CompleteThreeLineScan(2,changedPixels);
            changed.current.analysisStride=1;changed.text=changed.current.text;
            auto changedSource=CadenceSource(changedPixels);auto changedGeometry=geometry;
            if(fault==1)++changedGeometry.generatedCleanup.left;
            Assert::IsTrue(faultGuard->RememberCurrentFootprint(changed,changed.text,changedSource,base,changedGeometry));
            Assert::AreNotEqual("cadence-seed-rearmed",faultGuard->DecisionReason(),
                L"Changed glyph ownership or final geometry must not use the prior footprint baseline.");
            Assert::IsTrue(faultGuard->SampleCount()>0,
                L"An ineligible rearm must take the complete fresh seed path.");
        }

        // The ordinary cadence guard retains its original stride-2 rearm rule.
        auto cadenceSeed=Ready(1);cadenceSeed.current=CompleteThreeLineScan(1,pixels);
        cadenceSeed.current.analysisStride=2;cadenceSeed.text=cadenceSeed.current.text;
        auto cadenceGeometry=makeGeometry(cadenceSeed.text,12,78);
        auto cadence=std::make_unique<SubtitleCadenceGuard>();
        Assert::IsTrue(cadence->RememberAccepted(cadenceSeed,cadenceSeed.text,source,base,cadenceGeometry));
        auto strideOne=Ready(2);strideOne.current=CompleteThreeLineScan(2,pixels);
        strideOne.current.analysisStride=2;strideOne.text=strideOne.current.text;
        Assert::IsTrue(cadence->RememberAccepted(strideOne,strideOne.text,source,base,cadenceGeometry));
        Assert::AreNotEqual("cadence-seed-rearmed",cadence->DecisionReason());
        Assert::IsTrue(cadence->SampleCount()>0,
            L"The ordinary cadence seed remains tied to its stride-2 source spacing.");

        auto continuityChanged=Ready(4);continuityChanged.current=CompleteThreeLineScan(4,pixels,22);
        continuityChanged.continuityGeneration=22;continuityChanged.current.analysisStride=2;
        continuityChanged.text=continuityChanged.current.text;
        Assert::IsTrue(cadence->RememberAccepted(continuityChanged,continuityChanged.text,source,base,cadenceGeometry));
        Assert::AreNotEqual("cadence-seed-rearmed",cadence->DecisionReason(),
            L"The ordinary stride-2 cadence guard does not get the fresh-footprint continuity exception.");
        Assert::IsTrue(cadence->SampleCount()>0);
    }
    TEST_METHOD(CadenceGuardRequiresNewStrongScanAndRejectsUnownedExtraCapture) {
        auto pixels=Pixels();auto ready=Ready(1);ready.current=CompleteThreeLineScan(1,pixels);
        ready.current.analysisStride=2;ready.current.pictureTop=0;ready.current.independentBarAnalysis=false;
        ready.text=ready.current.text;
        SubtitleCutPasteGeometry g;g.valid=true;g.content=ready.text.bounds;
        g.glyphLines=ready.text.lineBounds;g.pictureCapture=ready.text.capturePanel;g.generatedCleanup=ready.text.sourcePanel;
        g.pictureTop=0;g.pictureBottom=78;
        SubtitleCadenceGuard guard;
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,CadenceSource(pixels),ready.current.sharedPicture.bounds,g));
        auto pending=Pending(2);pending.current.analysisStride=2;pending.current.cadenceSkipped=true;
        Assert::IsTrue(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
        pending.current.identity=Identity(3);pending.current.sharedPicture.identity=pending.current.identity;
        Assert::IsFalse(guard.Prepare(pending,CadenceSource(pixels),ready.current.sharedPicture.bounds,1000.0/60,250).valid);
        Assert::AreEqual("cadence-needs-new-independent-scan",guard.DecisionReason());
        auto one=ready;one.text.lineCount=one.current.text.lineCount=1;
        one.text.bounds=one.current.text.bounds=one.text.lineBounds[0];
        g.content=one.text.bounds;g.glyphLines[1]={120,10,140,20};g.glyphLines[2]={};
        Assert::IsFalse(guard.RememberAccepted(one,one.text,CadenceSource(pixels),one.current.sharedPicture.bounds,g));
        g.glyphLines[1]={};g.glyphLines[0].left+=5;
        Assert::IsFalse(guard.RememberAccepted(one,one.text,CadenceSource(pixels),one.current.sharedPicture.bounds,g),
            L"actual owned glyphs cannot fall outside the shader capture");
    }
    TEST_METHOD(AcceptedSeedStillRejectsChangedCurrentPixels) {
        auto pixels=PixelsWithCurrentBars();SubtitlePendingMeasurementGuard guard;auto ready=Ready(1);
        Assert::IsTrue(guard.RememberAccepted(ready,ready.text,Source(pixels),40,250));
        pixels[50*160+50]=uint16_t(1000<<6);
        auto pending=Pending(2);
        Assert::IsFalse(guard.Resolve(pending,Source(pixels),40,250));
        Assert::IsFalse(pending.available);
    }
    TEST_METHOD(CurrentCueWorkerProofUsesFreshPixelsAndIgnoresMovingPictureOutsideCue) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        auto seed=CompleteThreeLineScan(1,*seedPixels);
        SubtitleCurrentCueTemplateInput templateInput;templateInput.observation=seed;
        templateInput.acceptedCue=seed.text;templateInput.source=OwnedSource(seedPixels);
        templateInput.physicalBase=seed.sharedPicture.bounds;
        templateInput.sourceMode=SubtitlePreviewMode::None;
        templateInput.compositionSucceeded=false; // Classic seeds from the accepted full scan.
        templateInput.acceptedCurrentScan=true;templateInput.frameMs=1000.0/60;templateInput.holdMs=250;

        auto currentPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        (*currentPixels)[30*160+5]=uint16_t(340<<6); // Moving picture background, outside cue glyphs.
        auto current=CompleteThreeLineScan(2,*currentPixels);
        SubtitleMeasurementWorker worker;worker.AdvanceCurrentCueRenderWindow(current.identity,2);
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(std::move(templateInput),revision)==
            SubtitleCurrentCueProofStatus::Queued);
        Assert::IsTrue(revision!=0);
        SubtitleCurrentCueProofRequest request;request.currentKey=current;
        request.source=OwnedSource(currentPixels);request.physicalBase=current.sharedPicture.bounds;
        request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
        request.eligibleRenderHead=current.identity;request.eligibleTailSequence=2;
        request.templateRevision=revision;request.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);

        SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Failed ||
                status==SubtitleCurrentCueProofStatus::Expired || status==SubtitleCurrentCueProofStatus::NotEligible)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready,
            L"The worker proves the current glyphs and excluded bars without comparing unrelated movie background.");
        Assert::AreEqual(uint64_t(1),receipt.SeedIdentity().acceptedSequence);
        Assert::IsTrue(receipt.SourceMode()==SubtitlePreviewMode::None);
        SubtitleBoxPresentation tracker;SubtitleBoxPreview preview;SubtitleBoxResult held;
        Assert::IsTrue(tracker.ConsumeCurrentCueProof(receipt,preview,held));
        Assert::IsTrue(preview.presentationOnlyCurrentCueProof && preview.current.pendingRefresh);
        Assert::IsTrue(preview.current.text.held && preview.current.text.cue==seed.text.cue);
        Assert::IsFalse(preview.current.barAuthority || preview.current.independentBarAnalysis);
        Assert::IsFalse(preview.current.ink->rawEvidenceComplete);
        SubtitleBoxPreview replayedPreview;SubtitleBoxResult replayedCue;
        Assert::IsFalse(tracker.ConsumeCurrentCueProof(receipt,replayedPreview,replayedCue),
            L"A sealed current proof receipt is single-use, including through the presentation adapter.");
        Assert::IsTrue(worker.TryTakeCurrentCueProof(current,revision,receipt)==
            SubtitleCurrentCueProofStatus::Consumed);
    }
    TEST_METHOD(CurrentCueWorkerRejectsNovelInkInsideAdvancedDestinationAndAllowsUniformMotion) {
        const auto operationFor=[](const SubtitleBoxResult& cue) {
            SubtitleCutPasteGeometry operation;operation.valid=true;
            operation.content=cue.bounds;operation.glyphLines=cue.lineBounds;
            operation.pictureCapture=cue.capturePanel;
            operation.source=cue.sourcePanel;operation.destination={0,12,80,47};
            operation.generatedCleanup=cue.sourcePanel;operation.pictureTop=12;operation.pictureBottom=78;
            operation.extendToBar=false;return operation;
        };
        const auto run=[&](int mutation) {
            auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
            const auto fillTexturedDestination=[](std::vector<uint16_t>& pixels,int offset) {
                for(int y=12;y<47;++y)for(int x=0;x<80;++x) {
                    const int value=280+(((x/4+y/4)&1)?60:0)+offset;
                    pixels[size_t(y)*160+x]=uint16_t(value<<6);
                }
            };
            if(mutation==0)fillTexturedDestination(*seedPixels,0);
            const auto seed=CompleteThreeLineScan(1,*seedPixels);
            const auto operation=operationFor(seed.text);
            SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
            input.source=OwnedCadenceSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
            input.sourceMode=SubtitlePreviewMode::BlackBackground;input.backgroundMode=3;
            input.compositionSucceeded=true;input.composedOperation=operation;
            input.acceptedCurrentScan=true;input.frameMs=1000.0/60;input.holdMs=250;

            auto currentPixels=std::make_shared<std::vector<uint16_t>>(*seedPixels);
            if(mutation==0) {
                // Textured movie pixels move four luma codes while retaining
                // their seeded local edge pairs. This is allowed by the exact
                // current-point/current-neighbor tolerance, not by a broad ROI
                // exemption.
                fillTexturedDestination(*currentPixels,4);
            } else if(mutation==1) {
                // Odd-coordinate glyph pixels must be checked individually,
                // even when the detector's larger source image uses a grid.
                for(int y=31;y<33;++y)for(int x=61;x<63;++x)
                    (*currentPixels)[size_t(y)*160+x]=uint16_t(700<<6);
            } else if(mutation==2) {
                // Single-pixel UV-cell marker: chroma-only caption color is
                // still a possible new glyph inside the overwritten panel.
                constexpr size_t lumaWords=160u*90u;
                const size_t uv=lumaWords+size_t(31/2)*160u+size_t(61/2)*2u;
                (*currentPixels)[uv]=uint16_t(800<<6);
            } else {
                // Dark neutral text on the bright destination has no positive
                // luma edge; the lower-luma directional check must catch it.
                (*currentPixels)[size_t(31)*160+61]=uint16_t(100<<6);
            }
            const auto current=CompleteThreeLineScan(2,*currentPixels);
            SubtitleMeasurementWorker worker;worker.AdvanceCurrentCueRenderWindow(current.identity,2);
            uint64_t revision=0;
            Assert::IsTrue(worker.SubmitCurrentCueTemplate(std::move(input),revision)==
                SubtitleCurrentCueProofStatus::Queued);
            SubtitleCurrentCueProofRequest request;request.currentKey=current;
            request.source=OwnedCadenceSource(currentPixels);request.physicalBase=current.sharedPicture.bounds;
            request.sourceMode=SubtitlePreviewMode::BlackBackground;request.backgroundMode=3;
            request.eligibleRenderHead=current.identity;request.eligibleTailSequence=2;
            request.templateRevision=revision;request.fullScanDue=false;
            Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);
            SubtitleCurrentCueProofReceipt receipt;auto status=SubtitleCurrentCueProofStatus::Queued;
            for(int attempt=0;attempt<1000;++attempt) {
                status=worker.TryTakeCurrentCueProof(current,revision,receipt);
                if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Failed ||
                    status==SubtitleCurrentCueProofStatus::Expired || status==SubtitleCurrentCueProofStatus::NotEligible)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return status;
        };
        Assert::IsTrue(run(0)==SubtitleCurrentCueProofStatus::Ready,
            L"Uniform low-contrast motion in the destination does not compare all movie pixels byte-for-byte.");
        Assert::IsTrue(run(1)==SubtitleCurrentCueProofStatus::Failed,
            L"A newly added odd-coordinate bright glyph inside the shader destination rejects reuse.");
        Assert::IsTrue(run(2)==SubtitleCurrentCueProofStatus::Failed,
            L"A one-cell alternate-chroma glyph inside the shader destination rejects reuse.");
        Assert::IsTrue(run(3)==SubtitleCurrentCueProofStatus::Failed,
            L"A dark neutral glyph inside the shader destination rejects reuse.");
    }
    TEST_METHOD(CurrentCueWorkerProofRejectsChangedOwnedGlyphPixels) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        auto seed=CompleteThreeLineScan(1,*seedPixels);
        SubtitleCurrentCueTemplateInput templateInput;templateInput.observation=seed;
        templateInput.acceptedCue=seed.text;templateInput.source=OwnedSource(seedPixels);
        templateInput.physicalBase=seed.sharedPicture.bounds;templateInput.acceptedCurrentScan=true;

        auto currentPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        (*currentPixels)[50*160+50]=uint16_t(400<<6);
        auto current=CompleteThreeLineScan(2,*currentPixels);
        SubtitleMeasurementWorker worker;worker.AdvanceCurrentCueRenderWindow(current.identity,2);
        uint64_t revision=0;worker.SubmitCurrentCueTemplate(std::move(templateInput),revision);
        SubtitleCurrentCueProofRequest request;request.currentKey=current;request.source=OwnedSource(currentPixels);
        request.physicalBase=current.sharedPicture.bounds;request.eligibleRenderHead=current.identity;
        request.eligibleTailSequence=2;request.templateRevision=revision;
        request.sourceMode=SubtitlePreviewMode::None;request.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);
        SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Failed || status==SubtitleCurrentCueProofStatus::Expired ||
                status==SubtitleCurrentCueProofStatus::Ready)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Failed,
            L"A changed owned glyph is rejected by exact current-source YUV checks.");
        Assert::IsFalse(receipt.Valid());
    }
    TEST_METHOD(CurrentCueWorkerProofPersistsAcrossStaticCueAndRunsBesideBlockedGrouping) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        const std::weak_ptr<std::vector<uint16_t>> weakSeed=seedPixels;
        const auto seed=CompleteThreeLineScan(1,*seedPixels);
        const auto makeTemplate=[&]() {
            SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
            input.source=OwnedSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
            input.sourceMode=SubtitlePreviewMode::None;input.acceptedCurrentScan=true;
            input.frameMs=1000.0/60;input.holdMs=250;return input;
        };
        SubtitleMeasurementWorker worker;worker.SetInputRateHz(24.0);
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(makeTemplate(),revision)==SubtitleCurrentCueProofStatus::Queued);
        bool seeded=false;
        for(int attempt=0;attempt<1000;++attempt) {
            uint64_t observedRevision=0;
            const auto status=worker.SubmitCurrentCueTemplate(makeTemplate(),observedRevision);
            if(status==SubtitleCurrentCueProofStatus::Ready) {
                Assert::AreEqual(revision,observedRevision);seeded=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(seeded,L"The initial full accepted cue is seeded asynchronously.");
        seedPixels.reset();
        for(int attempt=0;attempt<1000 && !weakSeed.expired();++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Assert::IsTrue(weakSeed.expired(),L"The immutable cue template retains no seed frame owner.");

        std::atomic_bool entered{false},release{false};std::atomic<uint64_t> groupingThread{0};
        ReleaseBlockedWorkers unblock{release};
        const auto blocker=WorkerKey(900,0xA11CE);
        Assert::IsTrue(worker.Submit(blocker,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            groupingThread.store(static_cast<uint64_t>(::GetCurrentThreadId()),std::memory_order_release);
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return blocker;
        }));
        for(int attempt=0;attempt<1000 && !entered.load(std::memory_order_acquire);++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Assert::IsTrue(entered.load(std::memory_order_acquire),L"The ordinary full-grouping worker is blocked.");

        for(uint64_t sequence=2;sequence<302;++sequence) {
            auto pixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
            (*pixels)[30*160+5]=uint16_t((sequence&1?300:340)<<6);
            const auto current=CompleteThreeLineScan(sequence,*pixels);
            const std::weak_ptr<std::vector<uint16_t>> weakCurrent=pixels;
            worker.AdvanceCurrentCueRenderWindow(current.identity,sequence);
            SubtitleCurrentCueProofRequest request;request.currentKey=current;
            request.source=OwnedSource(pixels);request.physicalBase=current.sharedPicture.bounds;
            request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
            request.eligibleRenderHead=current.identity;request.eligibleTailSequence=sequence;
            request.templateRevision=revision;request.fullScanDue=false;
            Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);
            SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
            for(int attempt=0;attempt<1000;++attempt) {
                status=worker.TryTakeCurrentCueProof(current,revision,receipt);
                if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Failed ||
                    status==SubtitleCurrentCueProofStatus::Expired)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready,
                L"Every advancing source remains provable beyond the former 250 ms pending window.");
            Assert::AreEqual(uint64_t(1),receipt.SeedIdentity().acceptedSequence,
                L"Successful current proofs never replace the immutable acquisition baseline.");
            Assert::IsTrue(receipt.WorkerThreadId()!=groupingThread.load(std::memory_order_acquire),
                L"Current pixel proofs run on the reserved worker while ordinary grouping is blocked.");
            pixels.reset();
            for(int attempt=0;attempt<1000 && !weakCurrent.expired();++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Assert::IsTrue(weakCurrent.expired(),L"A consumed proof keeps no source frame owner.");
            receipt={};std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsFalse(release.load(std::memory_order_acquire),
            L"The current-cue verifier completed its 300-source run while full grouping remained blocked.");
    }
    TEST_METHOD(CurrentCueWorkerRejectsNewBarInkAndForeignModePolicyOrBase) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        const auto seed=CompleteThreeLineScan(1,*seedPixels);
        auto currentPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        // This is new high-luma caption-like ink in the encoded top bar, well
        // outside the accepted cue's line/capture geometry.
        (*currentPixels)[5*160+7]=uint16_t(700<<6);
        const auto current=CompleteThreeLineScan(2,*currentPixels);
        SubtitleMeasurementWorker worker;worker.AdvanceCurrentCueRenderWindow(current.identity,2);
        SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
        input.source=OwnedSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
        input.sourceMode=SubtitlePreviewMode::None;input.acceptedCurrentScan=true;
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(std::move(input),revision)==
            SubtitleCurrentCueProofStatus::Queued);

        SubtitleCurrentCueProofRequest request;request.currentKey=current;
        request.source=OwnedSource(currentPixels);request.physicalBase=current.sharedPicture.bounds;
        request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
        request.eligibleRenderHead=current.identity;request.eligibleTailSequence=2;
        request.templateRevision=revision;request.fullScanDue=false;
        auto foreign=request;foreign.sourceMode=SubtitlePreviewMode::BlackBackground;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross presentation mode.");
        foreign=request;foreign.backgroundMode=5;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross compositor background mode.");
        foreign=request;++foreign.currentKey.policyGeneration;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross policy generation.");
        foreign=request;++foreign.physicalBase.top;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross its native base.");
        foreign=request;++foreign.currentKey.identity.sourceFormatGeneration;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross source format generation.");
        foreign=request;foreign.source.format=AnalysisLumaFormat::P210;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(foreign))==SubtitleCurrentCueProofStatus::NotEligible,
            L"A cue template cannot cross source pixel format/layout.");

        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);
        SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Failed || status==SubtitleCurrentCueProofStatus::Ready ||
                status==SubtitleCurrentCueProofStatus::Expired)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Failed,
            L"A one-pixel bar caption must veto the current cue proof even outside the old ROI.");
        Assert::IsFalse(receipt.Valid());
    }
    TEST_METHOD(CurrentCueWorkerPollingIsNonblockingAndDropsQueuedExpiredOwners) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        const auto seed=CompleteThreeLineScan(1,*seedPixels);
        auto currentPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        const std::weak_ptr<std::vector<uint16_t>> weakCurrent=currentPixels;
        const auto current=CompleteThreeLineScan(2,*currentPixels);
        SubtitleMeasurementWorker worker;
        std::atomic_bool entered{false},release{false};ReleaseBlockedWorkers unblock{release};
        const auto blocker=WorkerKey(900,0xA11CE);
        Assert::IsTrue(worker.Submit(blocker,[&](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {
            entered.store(true,std::memory_order_release);
            while(!release.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return blocker;
        }));
        for(int attempt=0;attempt<1000 && !entered.load(std::memory_order_acquire);++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Assert::IsTrue(entered.load(std::memory_order_acquire),L"The ordinary grouping lane is intentionally occupied.");

        SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
        input.source=OwnedSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
        input.sourceMode=SubtitlePreviewMode::None;input.acceptedCurrentScan=true;
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(std::move(input),revision)==
            SubtitleCurrentCueProofStatus::Queued);
        worker.AdvanceCurrentCueRenderWindow(current.identity,2);
        SubtitleCurrentCueProofRequest request;request.currentKey=current;
        request.source=OwnedSource(currentPixels);request.physicalBase=current.sharedPicture.bounds;
        request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
        request.eligibleRenderHead=current.identity;request.eligibleTailSequence=2;
        request.templateRevision=revision;request.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==SubtitleCurrentCueProofStatus::Queued);

        SubtitleCurrentCueProofReceipt receipt;
        const auto pollStart=std::chrono::steady_clock::now();
        const auto firstPoll=worker.TryTakeCurrentCueProof(current,revision,receipt);
        const auto pollMs=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-pollStart).count();
        Assert::IsTrue(firstPoll==SubtitleCurrentCueProofStatus::Queued ||
            firstPoll==SubtitleCurrentCueProofStatus::Pending,
            L"A queued proof is reported immediately while the grouping lane is blocked.");
        Assert::IsTrue(pollMs<50.0,L"Render-side proof polling never waits for worker completion.");

        auto nextHead=current.identity;++nextHead.acceptedSequence;++nextHead.sourceFrameNumber;
        nextHead.captureTimestamp+=1000;
        worker.AdvanceCurrentCueRenderWindow(nextHead,nextHead.acceptedSequence);
        currentPixels.reset();
        for(int attempt=0;attempt<1000 && !weakCurrent.expired();++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Assert::IsTrue(weakCurrent.expired(),
            L"Advancing the render head drops a queued stale proof and releases its frame owner.");
        Assert::IsTrue(worker.TryTakeCurrentCueProof(current,revision,receipt)==
            SubtitleCurrentCueProofStatus::Expired,
            L"A completion outside the current head window cannot become a usable receipt.");
        Assert::IsFalse(receipt.Valid());
    }
    TEST_METHOD(CurrentCueWorkerTailOnlyCaptureExtensionPreservesProofAndAcceptsSubsetHead) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        const auto seed=CompleteThreeLineScan(1,*seedPixels);
        SubtitleMeasurementWorker worker;worker.AdvanceCurrentCueRenderWindow(seed.identity,1);
        auto makeTemplate=[&]() {
            SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
            input.source=OwnedSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
            input.sourceMode=SubtitlePreviewMode::None;input.acceptedCurrentScan=true;return input;
        };
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(makeTemplate(),revision)==
            SubtitleCurrentCueProofStatus::Queued);
        bool seeded=false;
        for(int attempt=0;attempt<1000;++attempt) {
            uint64_t observed=0;
            if(worker.SubmitCurrentCueTemplate(makeTemplate(),observed)==SubtitleCurrentCueProofStatus::Ready) {
                Assert::AreEqual(revision,observed);seeded=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(seeded);

        auto pixels2=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        auto current2=CompleteThreeLineScan(2,*pixels2);
        worker.AdvanceCurrentCueRenderWindow(current2.identity,2,true);
        SubtitleCurrentCueProofRequest request2;request2.currentKey=current2;
        request2.source=OwnedSource(pixels2);request2.physicalBase=current2.sharedPicture.bounds;
        request2.sourceMode=SubtitlePreviewMode::None;request2.backgroundMode=3;
        request2.eligibleRenderHead=current2.identity;request2.eligibleTailSequence=2;
        request2.templateRevision=revision;request2.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request2))==SubtitleCurrentCueProofStatus::Queued);

        auto queueFront=current2.identity;++queueFront.acceptedSequence;++queueFront.sourceFrameNumber;
        queueFront.captureTimestamp+=1000;
        worker.AdvanceCurrentCueRenderWindow(queueFront,3,false);
        SubtitleCurrentCueProofReceipt receipt;auto status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current2,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Expired ||
                status==SubtitleCurrentCueProofStatus::Failed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready,
            L"Capture tail extension cannot expire the still-rendering head source's proof.");
        Assert::AreEqual(uint64_t(2),receipt.CurrentKey().identity.acceptedSequence);

        auto pixels3=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        auto current3=CompleteThreeLineScan(3,*pixels3);
        SubtitleCurrentCueProofRequest request3;request3.currentKey=current3;
        request3.source=OwnedSource(pixels3);request3.physicalBase=current3.sharedPicture.bounds;
        request3.sourceMode=SubtitlePreviewMode::None;request3.backgroundMode=3;
        request3.eligibleRenderHead=current3.identity;request3.eligibleTailSequence=3;
        request3.templateRevision=revision;request3.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request3))==SubtitleCurrentCueProofStatus::Queued,
            L"A future queue-front head inside the existing window is a valid request subset.");
        status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current3,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Expired ||
                status==SubtitleCurrentCueProofStatus::Failed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready);
        Assert::AreEqual(uint64_t(3),receipt.CurrentKey().identity.acceptedSequence);

        auto pixels7=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        auto current7=CompleteThreeLineScan(7,*pixels7);
        worker.AdvanceCurrentCueRenderWindow(current7.identity,7,true);
        SubtitleCurrentCueProofRequest early;early.currentKey=current7;
        early.source=OwnedSource(pixels7);early.physicalBase=current7.sharedPicture.bounds;
        early.sourceMode=SubtitlePreviewMode::None;early.backgroundMode=3;
        early.eligibleRenderHead=current7.identity;early.eligibleTailSequence=7;
        early.templateRevision=revision;early.frameMs=1000.0/60.0;early.fullScanDue=false;
        Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(early))==SubtitleCurrentCueProofStatus::Queued);
        status=SubtitleCurrentCueProofStatus::Queued;
        for(int attempt=0;attempt<1000;++attempt) {
            status=worker.TryTakeCurrentCueProof(current7,revision,receipt);
            if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Expired ||
                status==SubtitleCurrentCueProofStatus::Failed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready);
        std::atomic_uint lateDueScans{0};
        SubtitleCurrentCueProofRequest due;due.currentKey=current7;
        due.source=OwnedSource(pixels7);due.physicalBase=current7.sharedPicture.bounds;
        due.sourceMode=SubtitlePreviewMode::None;due.backgroundMode=3;
        due.eligibleRenderHead=current7.identity;due.eligibleTailSequence=7;
        due.templateRevision=revision;due.frameMs=1000.0/60.0;due.fullScanDue=true;
        due.fullScanOnMiss=[current7,&lateDueScans](SubtitleMeasurementSampler&,
            const SubtitleBarTrackingReference&) {++lateDueScans;return current7;};
        worker.SubmitCurrentCueProof(std::move(due));
        SubtitleBoxObservation semantic;
        Assert::IsTrue(WaitForRead(worker,current7,semantic),
            L"A later due request upgrades the same proof source when its semantic refresh interval elapsed.");
        Assert::AreEqual(1u,lateDueScans.load(std::memory_order_acquire));
    }
    TEST_METHOD(CurrentCueWorkerRunsSemanticRefreshAtBoundedInputCadence) {
        for(const auto rate:{24.0,60.0}) {
            auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
            auto seed=CompleteThreeLineScan(1,*seedPixels);
            if(rate>30.0)seed.analysisStride=2;
            SubtitleMeasurementWorker worker;worker.SetInputRateHz(rate);
            worker.AdvanceCurrentCueRenderWindow(seed.identity,1);
            const auto frameMs=1000.0/rate;
            const auto makeTemplate=[&]() {
                SubtitleCurrentCueTemplateInput input;input.observation=seed;input.acceptedCue=seed.text;
                input.source=OwnedSource(seedPixels);input.physicalBase=seed.sharedPicture.bounds;
                input.sourceMode=SubtitlePreviewMode::None;input.acceptedCurrentScan=true;
                input.frameMs=frameMs;input.holdMs=250;return input;
            };
            uint64_t revision=0;
            Assert::IsTrue(worker.SubmitCurrentCueTemplate(makeTemplate(),revision)==
                SubtitleCurrentCueProofStatus::Queued);
            bool seeded=false;
            for(int attempt=0;attempt<1000;++attempt) {
                uint64_t sameRevision=0;
                if(worker.SubmitCurrentCueTemplate(makeTemplate(),sameRevision)==SubtitleCurrentCueProofStatus::Ready) {
                    Assert::AreEqual(revision,sameRevision);seeded=true;break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Assert::IsTrue(seeded,L"A current full accepted scan seeds before refresh cadence begins.");

            std::atomic_uint semanticScans{0};
            std::mutex semanticScanMutex;std::vector<uint64_t> semanticScanSequences;
            const uint64_t interval=static_cast<uint64_t>(std::ceil(100.0/frameMs));
            const uint64_t frameCount=static_cast<uint64_t>(std::ceil(500.0/frameMs));
            const uint64_t finalSequence=1+frameCount;
            const auto frameDuration=std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double,std::milli>(frameMs));
            auto nextFrame=std::chrono::steady_clock::now();
            for(uint64_t sequence=2;sequence<=finalSequence;++sequence) {
                nextFrame+=frameDuration;
                std::this_thread::sleep_until(nextFrame);
                auto pixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
                auto current=CompleteThreeLineScan(sequence,*pixels);
                if(rate>30.0)current.analysisStride=2;
                worker.AdvanceCurrentCueRenderWindow(current.identity,sequence);
                SubtitleCurrentCueProofRequest request;request.currentKey=current;
                request.source=OwnedSource(pixels);request.physicalBase=current.sharedPicture.bounds;
                request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
                request.eligibleRenderHead=current.identity;request.eligibleTailSequence=sequence;
                request.templateRevision=revision;request.frameMs=frameMs;
                request.fullScanDue=rate<=30.0 || sequence%2==1;
                request.fullScanOnMiss=[current,&semanticScans,&semanticScanMutex,&semanticScanSequences](
                    SubtitleMeasurementSampler&,const SubtitleBarTrackingReference&) mutable {
                    ++semanticScans;
                    {std::lock_guard<std::mutex> lock(semanticScanMutex);
                        semanticScanSequences.push_back(current.identity.acceptedSequence);}
                    return current;
                };
                Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==
                    SubtitleCurrentCueProofStatus::Queued);
                SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
                for(int attempt=0;attempt<1000;++attempt) {
                    status=worker.TryTakeCurrentCueProof(current,revision,receipt);
                    if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Failed ||
                        status==SubtitleCurrentCueProofStatus::Expired)break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready,
                    L"A semantic refresh runs off-thread and never delays the exact current cue proof.");
                Assert::AreEqual(uint64_t(1),receipt.SeedIdentity().acceptedSequence,
                    L"Periodic full scans do not replace or renew the immutable cue seed.");
            }
            const unsigned expected=static_cast<unsigned>(frameCount/interval);
            for(int attempt=0;attempt<1000 && semanticScans.load(std::memory_order_acquire)<expected;++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto scans=semanticScans.load(std::memory_order_acquire);
            Assert::IsTrue(scans>=expected-1 && scans<=expected,
                L"A wall-clock-paced source stream receives bounded semantic refreshes near the configured 100 ms interval.");
            {
                std::lock_guard<std::mutex> lock(semanticScanMutex);
                for(size_t i=1;i<semanticScanSequences.size();++i)
                    Assert::IsTrue(semanticScanSequences[i]-semanticScanSequences[i-1]>=interval,
                        L"Semantic refresh scans remain below the source cadence and are not queued every frame.");
            }
        }
    }
    TEST_METHOD(CurrentCueWorkerBurstsScansAfterFreshInsidePictureLineChange) {
        auto seedPixels=std::make_shared<std::vector<uint16_t>>(PixelsWithCurrentBars());
        // The accepted template has two lines. A later third line is inserted
        // inside the active picture, outside the old cue ROI, while both old
        // glyphs stay byte-for-byte unchanged.
        for(int y=74;y<78;++y)for(int x=50;x<110;++x)if(x%7<3)
            (*seedPixels)[size_t(y)*160+x]=uint16_t(300<<6);
        auto seed=CompleteThreeLineScan(1,*seedPixels);
        seed.text.lineCount=2;seed.text.bounds={50,50,110,68};seed.text.lineBounds[2]={};
        seed.text.linePanels[2]={};seed.text.provenLinePanels[2]={};seed.text.lineCapturePanels[2]={};
        SubtitleMeasurementWorker worker;worker.SetInputRateHz(24.0);
        worker.AdvanceCurrentCueRenderWindow(seed.identity,1);
        const auto makeTemplate=[&]() {
            SubtitleCurrentCueTemplateInput value;value.observation=seed;value.acceptedCue=seed.text;
            value.source=OwnedSource(seedPixels);value.physicalBase=seed.sharedPicture.bounds;
            value.sourceMode=SubtitlePreviewMode::None;value.acceptedCurrentScan=true;
            value.frameMs=1000.0/24.0;value.holdMs=250;return value;
        };
        uint64_t revision=0;
        Assert::IsTrue(worker.SubmitCurrentCueTemplate(makeTemplate(),revision)==
            SubtitleCurrentCueProofStatus::Queued);
        bool seeded=false;
        for(int attempt=0;attempt<1000;++attempt) {
            uint64_t sameRevision=0;
            if(worker.SubmitCurrentCueTemplate(makeTemplate(),sameRevision)==SubtitleCurrentCueProofStatus::Ready) {
                Assert::AreEqual(revision,sameRevision);seeded=true;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Assert::IsTrue(seeded,L"The prior two-line cue is seeded from the accepted full source.");

        std::atomic_uint semanticScans{0};
        for(uint64_t sequence=2;sequence<=7;++sequence) {
            auto pixels=std::make_shared<std::vector<uint16_t>>(*seedPixels);
            const std::weak_ptr<std::vector<uint16_t>> weakPixels=pixels;
            for(int y=40;y<46;++y)for(int x=50;x<110;++x)if(x%7<3)
                (*pixels)[size_t(y)*160+x]=uint16_t(700<<6);
            auto current=CompleteThreeLineScan(sequence,*pixels);
            current.text.lineCount=3;current.text.bounds={50,40,110,68};
            current.text.lineBounds[0]={50,40,110,46};current.text.lineBounds[1]={50,50,110,56};
            current.text.lineBounds[2]={50,62,110,68};
            current.text.capturePanel=current.text.sourcePanel={40,37,120,70};
            for(int line=0;line<3;++line) {
                current.text.linePanels[line]=current.text.sourcePanel;
                current.text.provenLinePanels[line]=current.text.sourcePanel;
                current.text.lineCapturePanels[line]=current.text.capturePanel;
            }
            worker.AdvanceCurrentCueRenderWindow(current.identity,sequence);
            SubtitleCurrentCueProofRequest request;request.currentKey=current;
            request.source=OwnedSource(pixels);request.physicalBase=current.sharedPicture.bounds;
            request.sourceMode=SubtitlePreviewMode::None;request.backgroundMode=3;
            request.eligibleRenderHead=current.identity;request.eligibleTailSequence=sequence;
            request.templateRevision=revision;request.frameMs=1000.0/24.0;request.fullScanDue=true;
            request.fullScanOnMiss=[current,pixels,&semanticScans](SubtitleMeasurementSampler&,
                const SubtitleBarTrackingReference&) mutable {(void)pixels;++semanticScans;return current;};
            Assert::IsTrue(worker.SubmitCurrentCueProof(std::move(request))==
                SubtitleCurrentCueProofStatus::Queued);
            SubtitleCurrentCueProofReceipt receipt;SubtitleCurrentCueProofStatus status=SubtitleCurrentCueProofStatus::Queued;
            for(int attempt=0;attempt<1000;++attempt) {
                status=worker.TryTakeCurrentCueProof(current,revision,receipt);
                if(status==SubtitleCurrentCueProofStatus::Ready || status==SubtitleCurrentCueProofStatus::Failed ||
                    status==SubtitleCurrentCueProofStatus::Expired)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Assert::IsTrue(status==SubtitleCurrentCueProofStatus::Ready,
                L"Old glyphs remain current and the already accepted cue stays present while the semantic scan runs.");
            Assert::AreEqual(2,receipt.AcceptedCue().lineCount,
                L"A possible new line does not become presentation text until a full accepted current scan replaces the template.");
            if(sequence>=4) {
                SubtitleBoxObservation scanned;
                Assert::IsTrue(WaitForRead(worker,current,scanned),
                    L"The periodic full scan remains available for normal confirmation of the new layout.");
                Assert::IsTrue(WaitForWorkerIdle(worker),
                    L"The bounded full scan completes before the next due source in this small fixture.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            pixels.reset();
            for(int attempt=0;attempt<1000 && !weakPixels.expired();++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Assert::IsTrue(weakPixels.expired(),
                L"The proof and any semantic scan release this source owner after their current-source work completes.");
        }
        Assert::IsTrue(semanticScans.load(std::memory_order_acquire)>=4,
            L"After a full current scan finds a changed line count, every due source is rescanned until a new cue replaces the template.");
    }
};
}
