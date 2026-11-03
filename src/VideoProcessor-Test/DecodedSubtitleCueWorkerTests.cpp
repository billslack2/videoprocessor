#include "pch.h"
#include "CppUnitTest.h"
#include <SubtitleCadenceGuard.h>
#include <SubtitleMeasurementWorker.h>
#include <SourcePictureDetector.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests {
TEST_CLASS(DecodedSubtitleCueWorkerTests) {
    static constexpr int Width=3840,Height=2160,PictureTop=262,PictureBottom=1898;

    static bool In(const SubtitleBoxRect& r,int x,int y) {
        return r.Valid() && x>=r.left && x<r.right && y>=r.top && y<r.bottom;
    }

    static bool IsGlyph(int x,int y) {
        if(y<1770 || y>=1940 || x<1552 || x>=2288)return false;
        for(int row=0;row<3;++row)for(int glyph=0;glyph<12;++glyph) {
            const int left=1552+glyph*64,top=1770+row*60;
            if(x>=left && x<left+32 && y>=top && y<top+50)
                return x<left+6 || x>=left+26 || (y>=top+23 && y<top+27);
        }
        return false;
    }

    static uint16_t FixtureLuma(int x,int y,int phase) {
        if(IsGlyph(x,y))return 892;
        if(y<PictureTop || y>=PictureBottom)return 0;
        if(y>=1720)return uint16_t(48+16*((x/64+y/32+phase)&1));
        return uint16_t(300+40*((x/64+y/32+phase)&1));
    }

    static void FillDecodedFrame(std::vector<uint16_t>& words,int phase) {
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)
            words[size_t(y)*Width+x]=uint16_t(FixtureLuma(x,y,phase)<<6);
        // The interleaved 4:2:2 UV plane was initialized to neutral 512.
    }

    static AnalysisLumaSource Source(std::vector<uint16_t>& words) {
        AnalysisLumaSource source;
        source.data=reinterpret_cast<const uint8_t*>(words.data());
        source.dataBytes=words.size()*sizeof(uint16_t);
        source.width=Width;source.height=Height;
        source.rowBytes=source.chromaRowBytes=size_t(Width)*2;
        source.format=AnalysisLumaFormat::DecodedDolbyVision422;
        source.encoding=VideoFrameEncoding::R210;source.colorspace=ColorSpace::BT_2020;
        source.generation=1;source.range=AnalysisLumaRange::Full;
        return source;
    }

    static SubtitleBoxObservation Key(uint64_t sequence) {
        SubtitleBoxObservation key;
        key.identity.transportGeneration=key.identity.rendererGeneration=1;
        key.identity.acceptedSequence=key.identity.sourceFrameNumber=sequence;
        key.identity.captureTimestamp=1000000+(sequence-1)*166833;
        key.identity.sourceFormatGeneration=key.identity.analysisDomainKey=key.identity.viewportGeneration=1;
        key.width=Width;key.height=Height;key.policyGeneration=key.continuityGeneration=1;
        key.analysisStride=1;key.nearBarDistance=20;key.scriptHint=SubtitleScriptHint::Latin;
        key.sharedPicture.required=true;key.sharedPicture.identity=key.identity;
        key.sharedPicture.bounds={0,PictureTop,Width,PictureBottom,Width,Height,
            double(Width)/(PictureBottom-PictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
        return key;
    }

    static std::string Rect(const SubtitleBoxRect& r) {
        std::ostringstream out;
        out<<r.left<<','<<r.top<<'-'<<r.right<<','<<r.bottom;
        return out.str();
    }

    static std::string FailureDetail(const SubtitleCadenceGuard::CurrentFootprintVerification& proof,
        const SubtitleBoxPreview& preview,const SubtitleBoxResult& cue,
        const SubtitleCutPasteGeometry& geometry,const AnalysisLumaSource& current) {
        const int x=proof.mismatchX>=0?proof.mismatchX:1600;
        const int y=proof.mismatchY>=0?proof.mismatchY:1800;
        AnalysisLumaSample now{};
        current.Sample(x,y,now);
        const auto* palette=preview.current.ink.get();
        bool inGlyph=false;
        for(int line=0;line<cue.lineCount && line<3;++line)inGlyph|=In(cue.lineBounds[line],x,y);
        bool inCleanupRow=false;
        for(unsigned row=0;row<geometry.generatedCleanupRowCount &&
            row<geometry.generatedCleanupRows.size();++row)
            inCleanupRow|=In(geometry.generatedCleanupRows[row],x,y);
        std::ostringstream out;
        out<<"unchanged decoded-DV cue footprint verification failed"
           <<" reason="<<(proof.reason?proof.reason:"null")
           <<" mismatch=("<<proof.mismatchX<<','<<proof.mismatchY<<")"
           <<" samples="<<proof.samples
           <<" pixel=("<<x<<','<<y<<")"
           <<" oldYUV="<<FixtureLuma(x,y,0)<<",512,512"
           <<" currentYUV="<<now.luma<<','<<now.chromaU<<','<<now.chromaV
           <<" inkFloor="<<(palette?palette->inkFloor:-1)
           <<" blackLimit="<<(palette?palette->blackLimit:-1)
           <<" captionBackingLimit="<<(palette?palette->captionBackingLimit:-1)
           <<" source="<<Rect(geometry.source)<<" inSource="<<In(geometry.source,x,y)
           <<" content="<<Rect(geometry.content)<<" inContent="<<In(geometry.content,x,y)
           <<" cleanup="<<Rect(geometry.generatedCleanup)<<" inCleanup="<<In(geometry.generatedCleanup,x,y)
           <<" capture="<<Rect(geometry.pictureCapture)<<" inCapture="<<In(geometry.pictureCapture,x,y)
           <<" glyph="<<inGlyph<<" cleanupRows="<<geometry.generatedCleanupRowCount
           <<" inCleanupRow="<<inCleanupRow
           <<" lines="<<cue.lineCount
           <<" sequence="<<preview.current.identity.acceptedSequence;
        return out.str();
    }

public:
    TEST_METHOD(DecodedDvUnchangedCleanupFootprintPassesCurrentPhaseOnePixels) {
        const size_t plane=size_t(Width)*Height;
        std::vector<uint16_t> words(plane*2,uint16_t(512<<6));
        FillDecodedFrame(words,0);
        auto source=Source(words);
        Assert::IsTrue(source.IsValid(),L"Decoded-DV 4:2:2 frame layout must be valid.");
        AnalysisLumaSample crossingGlyph;
        Assert::IsTrue(source.Sample(1552,1930,crossingGlyph) && crossingGlyph.luma==892,
            L"The genuine third subtitle line must remain present below the physical picture edge.");

        const auto measuredPicture=DetectSourcePictureForWorker(source);
        Assert::IsTrue(measuredPicture.evidence.physicalEdges.estimateAvailable &&
            measuredPicture.evidence.physicalEdges.estimate.top==PictureTop &&
            measuredPicture.evidence.physicalEdges.estimate.bottom==PictureBottom,
            L"The real source detector must independently measure the fixture picture boundaries.");

        auto sampler=std::make_unique<SubtitleMeasurementSampler>();
        std::vector<SubtitleBoxObservation> scans;scans.reserve(4);
        SubtitleBarTrackingReference prior;
        const ActivePictureBounds base{0,PictureTop,Width,PictureBottom,Width,Height,
            double(Width)/(PictureBottom-PictureTop),ActivePictureBounds::BarAxes::TOP_BOTTOM};
        for(uint64_t sequence=1;sequence<=4;++sequence) {
            auto key=Key(sequence);
            auto measured=sampler->Measure(source,key,prior,60.0,measuredPicture.evidence);
            Assert::IsTrue(measured.analyzed&&measured.text.detected&&
                measured.text.lineCount==3&&!measured.text.workLimit,
                L"Four genuine decoded-DV sampler scans must acquire all three H glyph rows.");
            Assert::AreEqual(18,measured.ink->blackLimit,
                L"Full-range encoded-bar tracking retains its original strict matte threshold.");
            Assert::AreEqual(74,measured.ink->captionBackingLimit,
                L"Composition backing uses the actual detector card threshold, separately from bar tracking.");
            prior=SubtitleBoxLookahead::AdvanceBarTrackingReference(measured);
            scans.push_back(std::move(measured));
        }

        auto preview=std::make_unique<SubtitleBoxPreview>(
            SubtitleBoxLookahead::Resolve(scans.data(),scans.size(),1,1));
        Assert::IsTrue(preview->available&&preview->current.identity.acceptedSequence==1,
            L"Four same-context scans must resolve with the first measured source as current.");
        auto presentation=std::make_unique<SubtitleBoxPresentation>();
        presentation->RefreshReferencePixels(*preview,source);
        const auto cue=presentation->Consume(*preview,250,1000.0/60.0);
        Assert::IsTrue(cue.detected&&!cue.confirmationPending&&cue.lineCount==3&&
            cue.bounds.top<=1770&&cue.bounds.bottom>=1940,
            L"Presentation must acquire the three exact source lines spanning y1770..1940.");

        auto padding=std::make_unique<SubtitlePaddingGuard>();
        SubtitlePaddingPolicy paddingPolicy;paddingPolicy.glyphRelative=true;
        paddingPolicy.minimumHeadroomPixels=30;
        const auto safe=padding->Consume(cue,*preview,{60,40,21},paddingPolicy);
        const auto display=padding->DisplayBounds();
        auto compositor=std::make_unique<SubtitleCutPastePresentation>();
        auto geometry=compositor->Consume(cue,*preview,safe,10,&display);
        geometry=FitSubtitleToVisiblePicture(geometry,{0,PictureTop,Width,PictureBottom},10);
        Assert::IsTrue(geometry.valid&&geometry.generatedCleanup.Valid(),
            L"Fixture must create a live compositor cleanup footprint inside the native picture.");

        auto guard=std::make_unique<SubtitleCadenceGuard>();
        Assert::IsTrue(guard->RememberCurrentFootprint(*preview,cue,source,base,geometry,
            1000.0/60.0,250,5),
            L"Exact accepted source and cleanup geometry must seed current-footprint reuse.");

        FillDecodedFrame(words,1);
        source=Source(words);
        auto current=Key(5);current.analysisStride=2;current.cadenceSkipped=true;current.analyzed=false;
        auto proof=guard->VerifyCurrentFootprint(current,source,preview->current,cue,base);
        if(!proof.valid) {
            const auto detail=FailureDetail(proof,*preview,cue,geometry,source);
            Logger::WriteMessage(detail.c_str());
            const std::wstring failure(detail.begin(),detail.end());
            Assert::IsTrue(proof.valid,failure.c_str());
        }
    }
};
}

